#include "base/source/fstreamer.h"
#include "convolution.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "wav.h"
#include <memory>
#include <string>
using namespace Steinberg;
using namespace Steinberg::Vst;
namespace mla_convolution
{
static const FUID uid(0x4D6C6143, 0x6F6E7631, 0xA817309B, 0x6834E20F);
class Processor final : public SingleComponentEffect
{
public:
    DEFINE_INTERFACES
    DEF_INTERFACE(IConnectionPoint)
    END_DEFINE_INTERFACES(SingleComponentEffect)
    REFCOUNT_METHODS(SingleComponentEffect)
    tresult PLUGIN_API connect(IConnectionPoint* other) override
    {
        return other == static_cast<IConnectionPoint*>(this)
                   ? kResultOk
                   : SingleComponentEffect::connect(other);
    }
    tresult PLUGIN_API disconnect(IConnectionPoint* other) override
    {
        return other == static_cast<IConnectionPoint*>(this)
                   ? kResultOk
                   : SingleComponentEffect::disconnect(other);
    }
    static FUnknown* createInstance(void*)
    {
        return static_cast<IComponent*>(new Processor);
    }
    tresult PLUGIN_API initialize(FUnknown* context) override
    {
        auto r = SingleComponentEffect::initialize(context);
        if(r != kResultOk)
            return r;
        addAudioInput(STR16("Stereo In"), SpeakerArr::kStereo);
        addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
        parameters.addParameter(STR16("Mix"), nullptr, 0, 1,
                                ParameterInfo::kCanAutomate, 100);
        return kResultOk;
    }
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement* in, int32 ni,
                                          SpeakerArrangement* out,
                                          int32 no) override
    {
        if(ni != 1 || no != 1 || !in || !out || *in != SpeakerArr::kStereo ||
           *out != SpeakerArr::kStereo)
            return kResultFalse;
        return SingleComponentEffect::setBusArrangements(in, ni, out, no);
    }
    tresult PLUGIN_API canProcessSampleSize(int32 s) override
    {
        return s == kSample32 ? kResultTrue : kResultFalse;
    }
    tresult PLUGIN_API setupProcessing(ProcessSetup& setup) override
    {
        if(!std::isfinite(setup.sampleRate) || setup.sampleRate < 8000 ||
           setup.sampleRate > 384000)
            return kResultFalse;
        // Hosts stop processing before setup/notify/state restoration.
        std::unique_ptr<Convolution> next;
        uint32 nextTail = Convolution::block;
        try
        {
            if(!path.empty())
            {
                auto pcm = loadWav(path.c_str(), setup.sampleRate);
                nextTail = uint32(pcm.size() / 2 + Convolution::block);
                next = std::make_unique<Convolution>(pcm);
            }
            else
            {
                next = std::make_unique<Convolution>(std::vector<float>{1, 1});
            }
        }
        catch(...)
        {
            return kResultFalse;
        }
        auto r = SingleComponentEffect::setupProcessing(setup);
        if(r == kResultOk)
        {
            rate = setup.sampleRate;
            engine = std::move(next);
            tail = nextTail;
        }
        return r;
    }
    uint32 PLUGIN_API getLatencySamples() override
    {
        return Convolution::block;
    }
    uint32 PLUGIN_API getTailSamples() override
    {
        return tail;
    }
    tresult PLUGIN_API setActive(TBool active) override
    {
        if(!active && engine)
            engine->reset();
        return SingleComponentEffect::setActive(active);
    }
    tresult PLUGIN_API notify(IMessage* message) override
    {
        if(!message ||
           std::strcmp(message->getMessageID(), "mla.convolution.load"))
            return SingleComponentEffect::notify(message);
        const void* raw = nullptr;
        uint32 size = 0;
        auto* a = message->getAttributes();
        if(a->getBinary("path", raw, size) != kResultOk || !raw || !size ||
           size > 4096)
            return kResultFalse;
        const std::string nextPath(static_cast<const char*>(raw), size);
        try
        {
            load(nextPath);
            return kResultOk;
        }
        catch(const std::exception& e)
        {
            a->setBinary("error", e.what(), uint32(std::strlen(e.what())));
            return kResultFalse;
        }
    }
    tresult PLUGIN_API process(ProcessData& data) override
    {
        if(data.inputParameterChanges)
            for(int32 i = 0;
                i < data.inputParameterChanges->getParameterCount(); ++i)
            {
                auto* q = data.inputParameterChanges->getParameterData(i);
                int32 offset = 0;
                double value = 0;
                if(q && q->getParameterId() == 100 && q->getPointCount() > 0 &&
                   q->getPoint(q->getPointCount() - 1, offset, value) ==
                       kResultOk &&
                   std::isfinite(value))
                    mix = float(std::clamp(value, 0., 1.));
            }
        if(data.numSamples <= 0 || !data.numInputs || !data.numOutputs)
            return kResultOk;
        if(data.symbolicSampleSize != kSample32)
            return kResultFalse;
        auto& in = data.inputs[0];
        auto& out = data.outputs[0];
        if(in.numChannels != 2 || out.numChannels != 2 ||
           !in.channelBuffers32 || !out.channelBuffers32)
            return kResultFalse;
        for(int32 i = 0; i < data.numSamples; ++i)
        {
            float l = (in.silenceFlags & 1) ? 0 : in.channelBuffers32[0][i];
            float r = (in.silenceFlags & 2) ? 0 : in.channelBuffers32[1][i];
            // An identity response preserves the same dry latency before
            // loading an IR.
            if(engine)
                engine->process(l, r, mix, out.channelBuffers32[0][i],
                                out.channelBuffers32[1][i]);
            else
            {
                out.channelBuffers32[0][i] = l;
                out.channelBuffers32[1][i] = r;
            }
        }
        out.silenceFlags = 0;
        return kResultOk;
    }
    tresult PLUGIN_API getState(IBStream* state) override
    {
        if(!state)
            return kResultFalse;
        IBStreamer s(state, kLittleEndian);
        return s.writeDouble(mix) && s.writeInt32(int32(path.size())) &&
                       s.writeRaw(path.data(), int32(path.size())) ==
                           int32(path.size())
                   ? kResultOk
                   : kResultFalse;
    }
    tresult PLUGIN_API setState(IBStream* state) override
    {
        if(!state)
            return kResultFalse;
        IBStreamer s(state, kLittleEndian);
        double value;
        int32 count;
        if(!s.readDouble(value) || !std::isfinite(value) || value < 0 ||
           value > 1 || !s.readInt32(count) || count < 0 || count > 4096)
            return kResultFalse;
        std::string next(count, '\0');
        if(s.readRaw(next.data(), count) != count)
            return kResultFalse;
        try
        {
            if(!next.empty())
                load(next);
            else
            {
                auto identity =
                    std::make_unique<Convolution>(std::vector<float>{1, 1});
                engine = std::move(identity);
                path.clear();
                tail = Convolution::block;
            }
        }
        catch(...)
        {
            return kResultFalse;
        }
        mix = float(value);
        setParamNormalized(100, value);
        return kResultOk;
    }

private:
    void load(const std::string& nextPath)
    {
        auto pcm = loadWav(nextPath.c_str(), rate);
        auto next = std::make_unique<Convolution>(pcm);
        path = nextPath;
        tail = uint32(pcm.size() / 2 + Convolution::block);
        engine = std::move(next);
    }
    std::unique_ptr<Convolution> engine;
    std::string path;
    double rate = 48000;
    float mix = 1;
    uint32 tail = Convolution::block;
};
} // namespace mla_convolution
#ifndef MLA_CONVOLUTION_TEST
BEGIN_FACTORY_DEF("MLang", "https://github.com/mattilaa/mlang",
                  "mailto:devnull@example.invalid")
DEF_CLASS2(INLINE_UID_FROM_FUID(mla_convolution::uid),
           PClassInfo::kManyInstances, kVstAudioEffectClass, "Mla Convolution",
           0, PlugType::kFxReverb, "0.1.0", kVstVersionString,
           mla_convolution::Processor::createInstance)
END_FACTORY
#endif
