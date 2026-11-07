// Mla 09 - VST3 drum machine instrument in the style of the Roland TR-909
// Rhythm Composer.
//
// Eleven voices on the 909's MIDI keys: bass drum, snare, low, mid and high
// tom, rim shot and hand clap are analog-style synthesis; closed and open
// hi-hat, crash and ride play 6-bit samples from ROM. A note-on strikes its
// instrument; note-offs are ignored, as on the 909. Velocity above 100 is an
// accent, or, with Dynamics set to Velocity, velocity scales every hit.
//
// The controls are the 909's panel: Accent, and per instrument Level plus
// Tune, Attack, Decay, Tone or Snappy where the 909 has them.
//
// The synthesis and the ROMs are MLang: `src/mla_09_dsp.mla`. This file
// declares the buses, maps keys to instruments, schedules hits
// sample-accurately, maps VST3 parameters to physical values and persists
// state.

#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "base/source/fstreamer.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

// --- MLang DSP bridge (compiled from src/mla_09_dsp.mla) ---------------------
struct M09;
extern "C" M09 *mla09_create__f32(float sampleRate);
extern "C" void mla09_destroy__ptr_struct_M09(M09 *dsp);
extern "C" void mla09_reset__ptr_struct_M09(M09 *dsp);
extern "C" void mla09_trigger__ptr_struct_M09_i32_f32(M09 *dsp, int32_t which, float velocity);
extern "C" int32_t mla09_active__ptr_struct_M09(M09 *dsp);
extern "C" void mla09_set__ptr_struct_M09_i32_f32(M09 *dsp, int32_t index, float value);
extern "C" float mla09_process__ptr_struct_M09_ptr_f32(M09 *dsp, float *outRight);

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace mla_09 {

// Stable class id. Distinct from the other Mla plug-ins.
static const FUID kProcessorUID(0x4D6C6130, 0x39545239, 0x30394472, 0x2E6A5C17);

constexpr uint32 kStateMagic = 0x39304C4D; // "ML09" little-endian
constexpr uint32 kStateVersion = 1;

// Parameter IDs are stable: new parameters are appended. The DSP index of a
// parameter is its ID minus kFirstParam (see mla09_set in the .mla file).
enum ParamId : ParamID {
    kOutputParam = 100,
    kAccentParam,
    kDynamicsParam,
    kBdLevelParam,
    kBdTuneParam,
    kBdAttackParam,
    kBdDecayParam,
    kSdLevelParam,
    kSdTuneParam,
    kSdToneParam,
    kSdSnappyParam,
    kLtLevelParam,
    kLtTuneParam,
    kLtDecayParam,
    kMtLevelParam,
    kMtTuneParam,
    kMtDecayParam,
    kHtLevelParam,
    kHtTuneParam,
    kHtDecayParam,
    kRsLevelParam,
    kCpLevelParam,
    kChLevelParam,
    kChDecayParam,
    kOhLevelParam,
    kOhDecayParam,
    kCrLevelParam,
    kCrTuneParam,
    kRdLevelParam,
    kRdTuneParam,
};

constexpr ParamID kFirstParam = kOutputParam, kLastParam = kRdTuneParam;
constexpr int kNumParams = kLastParam - kFirstParam + 1;

static int indexOf(ParamID id)
{
    return id >= kFirstParam && id <= kLastParam ? static_cast<int>(id - kFirstParam) : -1;
}

// Instruments (see mla09_trigger in the .mla file).
enum Instrument : int32_t { kBD = 0, kSD, kLT, kMT, kHT, kRS, kCP, kCH, kOH, kCR, kRD };

// The 909's MIDI keys, plus General MIDI aliases for the cymbals; -1 is silent.
static int32_t instrumentFor(int16 key)
{
    switch(key) {
        case 35: case 36: return kBD;
        case 37: return kRS;
        case 38: case 40: return kSD;
        case 39: return kCP;
        case 41: case 43: return kLT;
        case 45: case 47: return kMT;
        case 48: case 50: return kHT;
        case 42: case 44: return kCH;
        case 46: return kOH;
        case 49: case 52: case 55: case 57: return kCR;
        case 51: case 53: case 59: return kRD;
        default: return -1;
    }
}

static bool isLevel(ParamID id)
{
    switch(id) {
        case kOutputParam: case kBdLevelParam: case kSdLevelParam: case kLtLevelParam: case kMtLevelParam:
        case kHtLevelParam: case kRsLevelParam: case kCpLevelParam: case kChLevelParam: case kOhLevelParam:
        case kCrLevelParam: case kRdLevelParam:
            return true;
        default:
            return false;
    }
}

// --- Normalized -> physical mappings ----------------------------------------
constexpr double kUnityLevelNorm = 60.0 / 66.0;

static float gainFromNorm(double norm)
{
    // -60 dB .. +6 dB; the bottom of the range is silence.
    if(norm <= 0.0)
        return 0.0f;
    return static_cast<float>(std::pow(10.0, (-60.0 + norm * 66.0) / 20.0));
}

class Processor final : public SingleComponentEffect, public IMidiMapping {
  public:
    Processor()
    {
        for(auto &value : norm_)
            value.store(0.0, std::memory_order_relaxed);
    }

    ~Processor() override { destroyDsp(); }

    DEFINE_INTERFACES
        DEF_INTERFACE(IMidiMapping)
    END_DEFINE_INTERFACES(SingleComponentEffect)
    REFCOUNT_METHODS(SingleComponentEffect)

    static FUnknown *createInstance(void *) { return static_cast<IComponent *>(new Processor()); }

    tresult PLUGIN_API initialize(FUnknown *context) SMTG_OVERRIDE
    {
        const tresult result = SingleComponentEffect::initialize(context);
        if(result != kResultOk)
            return result;

        addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
        addEventInput(STR16("MIDI In"), 16);

        addParam(kOutputParam, STR16("Output"), STR16("dB"), kUnityLevelNorm);
        addParam(kAccentParam, STR16("Accent"), nullptr, 0.5);
        addList(kDynamicsParam, STR16("Dynamics"), {STR16("909 Accent"), STR16("Velocity")}, 0);
        addLevel(kBdLevelParam, STR16("BD Level"));
        addKnob(kBdTuneParam, STR16("BD Tune"));
        addKnob(kBdAttackParam, STR16("BD Attack"));
        addKnob(kBdDecayParam, STR16("BD Decay"));
        addLevel(kSdLevelParam, STR16("SD Level"));
        addKnob(kSdTuneParam, STR16("SD Tune"));
        addKnob(kSdToneParam, STR16("SD Tone"));
        addKnob(kSdSnappyParam, STR16("SD Snappy"));
        addLevel(kLtLevelParam, STR16("LT Level"));
        addKnob(kLtTuneParam, STR16("LT Tune"));
        addKnob(kLtDecayParam, STR16("LT Decay"));
        addLevel(kMtLevelParam, STR16("MT Level"));
        addKnob(kMtTuneParam, STR16("MT Tune"));
        addKnob(kMtDecayParam, STR16("MT Decay"));
        addLevel(kHtLevelParam, STR16("HT Level"));
        addKnob(kHtTuneParam, STR16("HT Tune"));
        addKnob(kHtDecayParam, STR16("HT Decay"));
        addLevel(kRsLevelParam, STR16("RS Level"));
        addLevel(kCpLevelParam, STR16("CP Level"));
        addLevel(kChLevelParam, STR16("CH Level"));
        addKnob(kChDecayParam, STR16("CH Decay"));
        addLevel(kOhLevelParam, STR16("OH Level"));
        addKnob(kOhDecayParam, STR16("OH Decay"));
        addLevel(kCrLevelParam, STR16("CR Level"));
        addKnob(kCrTuneParam, STR16("CR Tune"));
        addLevel(kRdLevelParam, STR16("RD Level"));
        addKnob(kRdTuneParam, STR16("RD Tune"));
        return kResultOk;
    }

    tresult PLUGIN_API terminate() SMTG_OVERRIDE
    {
        destroyDsp();
        return SingleComponentEffect::terminate();
    }

    tresult PLUGIN_API getMidiControllerAssignment(int32 bus, int16 channel, CtrlNumber cc,
                                                   ParamID &id) SMTG_OVERRIDE
    {
        if(bus != 0 || channel < 0 || channel > 15)
            return kResultFalse;
        if(cc == kCtrlVolume) {
            id = kOutputParam;
            return kResultOk;
        }
        return kResultFalse;
    }

    tresult PLUGIN_API setBusArrangements(SpeakerArrangement *inputs, int32 numIns,
                                          SpeakerArrangement *outputs, int32 numOuts) SMTG_OVERRIDE
    {
        if(numIns != 0 || numOuts != 1 || outputs == nullptr || outputs[0] != SpeakerArr::kStereo)
            return kResultFalse;
        return SingleComponentEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
    }

    tresult PLUGIN_API canProcessSampleSize(int32 symbolicSampleSize) SMTG_OVERRIDE
    {
        return symbolicSampleSize == kSample32 ? kResultTrue : kResultFalse;
    }

    tresult PLUGIN_API setupProcessing(ProcessSetup &setup) SMTG_OVERRIDE
    {
        const tresult result = SingleComponentEffect::setupProcessing(setup);
        if(result != kResultOk)
            return result;
        // Called while inactive: nothing is rendering, the DSP (and its
        // sample ROMs) may be rebuilt.
        destroyDsp();
        dsp_ = mla09_create__f32(static_cast<float>(setup.sampleRate > 0 ? setup.sampleRate : 44100.0));
        pushAll();
        return kResultOk;
    }

    tresult PLUGIN_API setActive(TBool state) SMTG_OVERRIDE
    {
        if(dsp_)
            mla09_reset__ptr_struct_M09(dsp_);
        return SingleComponentEffect::setActive(state);
    }

    tresult PLUGIN_API process(ProcessData &data) SMTG_OVERRIDE
    {
        if(paramsDirty_.exchange(false, std::memory_order_acquire))
            pushAll();
        handleParameterChanges(data.inputParameterChanges);

        if(data.symbolicSampleSize != kSample32 || dsp_ == nullptr)
            return kResultFalse;
        if(data.numOutputs < 1 || data.outputs[0].numChannels < 2 || data.outputs[0].channelBuffers32 == nullptr)
            return kResultOk;
        AudioBusBuffers &out = data.outputs[0];
        float *outL = out.channelBuffers32[0];
        float *outR = out.channelBuffers32[1];

        int32 rendered = 0;
        IEventList *events = data.inputEvents;
        const int32 eventCount = events ? events->getEventCount() : 0;
        for(int32 i = 0; i < eventCount; ++i) {
            Event event{};
            if(events->getEvent(i, event) != kResultOk || event.type != Event::kNoteOnEvent ||
               event.noteOn.velocity <= 0.0f)
                continue;
            const int32_t which = instrumentFor(event.noteOn.pitch);
            if(which < 0)
                continue;
            const int32 at = std::clamp(event.sampleOffset, rendered, data.numSamples);
            render(outL, outR, rendered, at);
            rendered = at;
            mla09_trigger__ptr_struct_M09_i32_f32(dsp_, which, event.noteOn.velocity);
        }
        render(outL, outR, rendered, data.numSamples);
        // With every voice finished the block is exact zeros.
        out.silenceFlags = rendered == 0 && eventCount == 0 && mla09_active__ptr_struct_M09(dsp_) == 0 ? 3 : 0;
        return kResultOk;
    }

    // --- State ---------------------------------------------------------------
    tresult PLUGIN_API setState(IBStream *state) SMTG_OVERRIDE
    {
        if(state == nullptr)
            return kResultFalse;
        IBStreamer streamer(state, kLittleEndian);
        uint32 magic = 0, version = 0;
        if(!streamer.readInt32u(magic) || magic != kStateMagic || !streamer.readInt32u(version) ||
           version != kStateVersion)
            return kResultFalse;
        int32 count = 0;
        if(!streamer.readInt32(count) || count < 0 || count > 4096)
            return kResultFalse;
        for(int32 i = 0; i < count; ++i) {
            int32 id = 0;
            double value = 0;
            if(!streamer.readInt32(id) || !streamer.readDouble(value))
                return kResultFalse;
            const int index = indexOf(static_cast<ParamID>(id));
            if(index < 0 || !std::isfinite(value))
                continue; // Unknown parameter from a newer version.
            value = std::clamp(value, 0.0, 1.0);
            norm_[index].store(value, std::memory_order_relaxed);
            setParamNormalized(static_cast<ParamID>(id), value);
        }
        paramsDirty_.store(true, std::memory_order_release);
        return kResultOk;
    }

    tresult PLUGIN_API getState(IBStream *state) SMTG_OVERRIDE
    {
        if(state == nullptr)
            return kResultFalse;
        IBStreamer streamer(state, kLittleEndian);
        streamer.writeInt32u(kStateMagic);
        streamer.writeInt32u(kStateVersion);
        streamer.writeInt32(kNumParams);
        for(int i = 0; i < kNumParams; ++i) {
            streamer.writeInt32(static_cast<int32>(kFirstParam + i));
            streamer.writeDouble(norm_[i].load(std::memory_order_relaxed));
        }
        return kResultOk;
    }

  private:
    M09 *dsp_ = nullptr;
    std::atomic<double> norm_[kNumParams];
    std::atomic<bool> paramsDirty_{false};

    void addParam(ParamID id, const TChar *title, const TChar *units, double defaultNorm)
    {
        parameters.addParameter(title, units, 0, defaultNorm, ParameterInfo::kCanAutomate, id);
        norm_[indexOf(id)].store(defaultNorm, std::memory_order_relaxed);
    }

    void addLevel(ParamID id, const TChar *title) { addParam(id, title, STR16("dB"), kUnityLevelNorm); }

    // A panel knob, 0..1, centred by default.
    void addKnob(ParamID id, const TChar *title) { addParam(id, title, nullptr, 0.5); }

    void addList(ParamID id, const TChar *title, std::initializer_list<const TChar *> items, int defaultItem)
    {
        auto *list = new StringListParameter(title, id, nullptr,
                                             ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
        for(const TChar *item : items)
            list->appendString(item);
        const double defaultNorm = items.size() > 1 ? static_cast<double>(defaultItem) / (items.size() - 1) : 0.0;
        list->getInfo().defaultNormalizedValue = defaultNorm;
        list->setNormalized(defaultNorm);
        parameters.addParameter(list);
        norm_[indexOf(id)].store(defaultNorm, std::memory_order_relaxed);
    }

    double norm(ParamID id) const { return norm_[indexOf(id)].load(std::memory_order_relaxed); }

    void destroyDsp()
    {
        if(dsp_)
            mla09_destroy__ptr_struct_M09(dsp_);
        dsp_ = nullptr;
    }

    // Send one VST3 parameter's physical value to the DSP: levels as linear
    // gains, Dynamics as 0/1, knobs as 0..1.
    void apply(ParamID id)
    {
        const int index = indexOf(id);
        if(dsp_ == nullptr || index < 0)
            return;
        const double n = norm(id);
        double value = n;
        if(isLevel(id))
            value = gainFromNorm(n);
        else if(id == kDynamicsParam)
            value = n >= 0.5 ? 1.0 : 0.0;
        mla09_set__ptr_struct_M09_i32_f32(dsp_, index, static_cast<float>(value));
    }

    void pushAll()
    {
        for(int i = 0; i < kNumParams; ++i)
            apply(static_cast<ParamID>(kFirstParam + i));
    }

    void handleParameterChanges(IParameterChanges *changes)
    {
        if(changes == nullptr)
            return;
        const int32 count = changes->getParameterCount();
        for(int32 q = 0; q < count; ++q) {
            IParamValueQueue *queue = changes->getParameterData(q);
            if(queue == nullptr)
                continue;
            const int32 points = queue->getPointCount();
            int32 offset = 0;
            ParamValue value = 0;
            if(points <= 0 || queue->getPoint(points - 1, offset, value) != kResultOk)
                continue;
            const ParamID id = queue->getParameterId();
            const int index = indexOf(id);
            if(index < 0 || !std::isfinite(value))
                continue;
            norm_[index].store(std::clamp(value, 0.0, 1.0), std::memory_order_relaxed);
            apply(id);
        }
    }

    void render(float *outL, float *outR, int32 from, int32 to)
    {
        for(int32 i = from; i < to; ++i) {
            float right = 0.0f;
            outL[i] = mla09_process__ptr_struct_M09_ptr_f32(dsp_, &right);
            outR[i] = right;
        }
    }
};

} // namespace mla_09

BEGIN_FACTORY_DEF("MLang", "https://github.com/mattilaa/mlang", "mailto:devnull@example.invalid")
DEF_CLASS2(INLINE_UID_FROM_FUID(mla_09::kProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass,
           "Mla 09", 0, PlugType::kInstrumentDrum, "0.1.0", kVstVersionString,
           mla_09::Processor::createInstance)
END_FACTORY
