// Mla 08 - VST3 drum machine instrument in the style of the Roland TR-808
// Rhythm Composer.
//
// Sixteen analog-style voices (bass drum, snare, three toms and three congas,
// rim shot, claves, hand clap, maracas, cowbell, cymbal, open and closed
// hi-hat) on General MIDI keys. A note-on strikes its instrument; note-offs
// are ignored, as on the 808. Velocity above 100 is an accent, or, with
// Dynamics set to Velocity, velocity scales every hit.
//
// The controls are the 808's panel: Accent, and per instrument Level plus
// Tone, Decay, Snappy or Tuning where the 808 has them. As on the 808, each
// tom shares its knobs with the conga of the same pitch, the rim shot its
// level with the claves and the hand clap its level with the maracas.
//
// The synthesis is MLang: `src/mla_08_dsp.mla`. This file declares the buses,
// maps keys to instruments, schedules hits sample-accurately, maps VST3
// parameters to physical values and persists state.

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

// --- MLang DSP bridge (compiled from src/mla_08_dsp.mla) ---------------------
struct M08;
extern "C" M08 *mla08_create__f32(float sampleRate);
extern "C" void mla08_destroy__ptr_struct_M08(M08 *dsp);
extern "C" void mla08_reset__ptr_struct_M08(M08 *dsp);
extern "C" void mla08_trigger__ptr_struct_M08_i32_f32(M08 *dsp, int32_t which, float velocity);
extern "C" int32_t mla08_active__ptr_struct_M08(M08 *dsp);
extern "C" void mla08_set__ptr_struct_M08_i32_f32(M08 *dsp, int32_t index, float value);
extern "C" float mla08_process__ptr_struct_M08_ptr_f32(M08 *dsp, float *outRight);

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace mla_08 {

// Stable class id. Distinct from the other Mla plug-ins.
static const FUID kProcessorUID(0x4D6C6130, 0x38545238, 0x30384472, 0x7D1B9E43);

constexpr uint32 kStateMagic = 0x38304C4D; // "ML08" little-endian
constexpr uint32 kStateVersion = 1;

// Parameter IDs are stable: new parameters are appended.
enum ParamId : ParamID {
    kOutputParam = 100,
    kAccentParam,
    kDynamicsParam,
    kBdLevelParam,
    kBdToneParam,
    kBdDecayParam,
    kSdLevelParam,
    kSdToneParam,
    kSdSnappyParam,
    kLtLevelParam,
    kLtTuningParam,
    kMtLevelParam,
    kMtTuningParam,
    kHtLevelParam,
    kHtTuningParam,
    kRsLevelParam,
    kCpLevelParam,
    kCbLevelParam,
    kCyLevelParam,
    kCyToneParam,
    kCyDecayParam,
    kOhLevelParam,
    kOhDecayParam,
    kChLevelParam,
};

constexpr ParamID kFirstParam = kOutputParam, kLastParam = kChLevelParam;
constexpr int kNumParams = kLastParam - kFirstParam + 1;

static int indexOf(ParamID id)
{
    return id >= kFirstParam && id <= kLastParam ? static_cast<int>(id - kFirstParam) : -1;
}

// Instruments (see mla08_trigger in the .mla file).
enum Instrument : int32_t { kBD = 0, kSD, kLT, kMT, kHT, kRS, kCP, kCB, kCY, kOH, kCH, kLC, kMC, kHC, kCL, kMA };

// DSP parameter indexes (see mla08_set in the .mla file).
enum DspIndex : int32_t {
    kDspOutput = 0, kDspAccent, kDspDynamics,
    kDspLevelBD, kDspLevelSD, kDspLevelLT, kDspLevelMT, kDspLevelHT, kDspLevelRS, kDspLevelCP, kDspLevelCB,
    kDspLevelCY, kDspLevelOH, kDspLevelCH,
    kDspBdTone, kDspBdDecay, kDspSdTone, kDspSnappy, kDspLtTuning, kDspMtTuning, kDspHtTuning,
    kDspCyTone, kDspCyDecay, kDspOhDecay,
};

// General MIDI drum keys; -1 is silent.
static int32_t instrumentFor(int16 key)
{
    switch(key) {
        case 35: case 36: return kBD;
        case 37: return kRS;
        case 38: case 40: return kSD;
        case 39: return kCP;
        case 41: case 43: case 45: return kLT;
        case 47: case 48: return kMT;
        case 50: return kHT;
        case 42: case 44: return kCH;
        case 46: return kOH;
        case 49: case 51: case 52: case 55: case 57: case 59: return kCY;
        case 56: return kCB;
        case 62: return kHC;
        case 63: return kMC;
        case 64: return kLC;
        case 70: return kMA;
        case 75: return kCL;
        default: return -1;
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
        addList(kDynamicsParam, STR16("Dynamics"), {STR16("808 Accent"), STR16("Velocity")}, 0);
        addLevel(kBdLevelParam, STR16("BD Level"));
        addKnob(kBdToneParam, STR16("BD Tone"));
        addKnob(kBdDecayParam, STR16("BD Decay"));
        addLevel(kSdLevelParam, STR16("SD Level"));
        addKnob(kSdToneParam, STR16("SD Tone"));
        addKnob(kSdSnappyParam, STR16("SD Snappy"));
        addLevel(kLtLevelParam, STR16("LT/LC Level"));
        addKnob(kLtTuningParam, STR16("LT/LC Tuning"));
        addLevel(kMtLevelParam, STR16("MT/MC Level"));
        addKnob(kMtTuningParam, STR16("MT/MC Tuning"));
        addLevel(kHtLevelParam, STR16("HT/HC Level"));
        addKnob(kHtTuningParam, STR16("HT/HC Tuning"));
        addLevel(kRsLevelParam, STR16("RS/CL Level"));
        addLevel(kCpLevelParam, STR16("CP/MA Level"));
        addLevel(kCbLevelParam, STR16("CB Level"));
        addLevel(kCyLevelParam, STR16("CY Level"));
        addKnob(kCyToneParam, STR16("CY Tone"));
        addKnob(kCyDecayParam, STR16("CY Decay"));
        addLevel(kOhLevelParam, STR16("OH Level"));
        addKnob(kOhDecayParam, STR16("OH Decay"));
        addLevel(kChLevelParam, STR16("CH Level"));
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
        // Called while inactive: nothing is rendering, the DSP may be rebuilt.
        destroyDsp();
        dsp_ = mla08_create__f32(static_cast<float>(setup.sampleRate > 0 ? setup.sampleRate : 44100.0));
        pushAll();
        return kResultOk;
    }

    tresult PLUGIN_API setActive(TBool state) SMTG_OVERRIDE
    {
        if(dsp_)
            mla08_reset__ptr_struct_M08(dsp_);
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
            mla08_trigger__ptr_struct_M08_i32_f32(dsp_, which, event.noteOn.velocity);
        }
        render(outL, outR, rendered, data.numSamples);
        // With every voice finished the block is exact zeros.
        out.silenceFlags = rendered == 0 && eventCount == 0 && mla08_active__ptr_struct_M08(dsp_) == 0 ? 3 : 0;
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
    M08 *dsp_ = nullptr;
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
            mla08_destroy__ptr_struct_M08(dsp_);
        dsp_ = nullptr;
    }

    void set(int32_t index, double value)
    {
        mla08_set__ptr_struct_M08_i32_f32(dsp_, index, static_cast<float>(value));
    }

    // Send one VST3 parameter's physical value to the DSP.
    void apply(ParamID id)
    {
        if(dsp_ == nullptr)
            return;
        const double n = norm(id);
        switch(id) {
            case kOutputParam: set(kDspOutput, gainFromNorm(n)); break;
            case kAccentParam: set(kDspAccent, n); break;
            case kDynamicsParam: set(kDspDynamics, n >= 0.5 ? 1.0 : 0.0); break;
            case kBdLevelParam: set(kDspLevelBD, gainFromNorm(n)); break;
            case kBdToneParam: set(kDspBdTone, n); break;
            case kBdDecayParam: set(kDspBdDecay, n); break;
            case kSdLevelParam: set(kDspLevelSD, gainFromNorm(n)); break;
            case kSdToneParam: set(kDspSdTone, n); break;
            case kSdSnappyParam: set(kDspSnappy, n); break;
            case kLtLevelParam: set(kDspLevelLT, gainFromNorm(n)); break;
            case kLtTuningParam: set(kDspLtTuning, n); break;
            case kMtLevelParam: set(kDspLevelMT, gainFromNorm(n)); break;
            case kMtTuningParam: set(kDspMtTuning, n); break;
            case kHtLevelParam: set(kDspLevelHT, gainFromNorm(n)); break;
            case kHtTuningParam: set(kDspHtTuning, n); break;
            case kRsLevelParam: set(kDspLevelRS, gainFromNorm(n)); break;
            case kCpLevelParam: set(kDspLevelCP, gainFromNorm(n)); break;
            case kCbLevelParam: set(kDspLevelCB, gainFromNorm(n)); break;
            case kCyLevelParam: set(kDspLevelCY, gainFromNorm(n)); break;
            case kCyToneParam: set(kDspCyTone, n); break;
            case kCyDecayParam: set(kDspCyDecay, n); break;
            case kOhLevelParam: set(kDspLevelOH, gainFromNorm(n)); break;
            case kOhDecayParam: set(kDspOhDecay, n); break;
            case kChLevelParam: set(kDspLevelCH, gainFromNorm(n)); break;
            default: break;
        }
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
            outL[i] = mla08_process__ptr_struct_M08_ptr_f32(dsp_, &right);
            outR[i] = right;
        }
    }
};

} // namespace mla_08

BEGIN_FACTORY_DEF("MLang", "https://github.com/mattilaa/mlang", "mailto:devnull@example.invalid")
DEF_CLASS2(INLINE_UID_FROM_FUID(mla_08::kProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass,
           "Mla 08", 0, PlugType::kInstrumentDrum, "0.1.0", kVstVersionString,
           mla_08::Processor::createInstance)
END_FACTORY
