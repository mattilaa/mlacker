// Mla Vocoder - VST3 VP-330 style vocoder with a built-in carrier synth.
//
// The main audio input is the modulator (a voice, a drum loop, any sample on
// the track). The carrier comes from the built-in "Human Voice" synth played
// over MIDI (polyphonic VCO -> VCF), from the sidechain input (any external
// synth or audio), or from both. Hosts without sidechain routing can use the
// Split input mode: the left input channel is the modulator and the right one
// the external carrier.
//
// The signal processing (bands, envelope followers, voices, VCF, choir
// formants, ensemble) is MLang: `src/mla_vocoder_dsp.mla`. This file declares
// the buses, schedules MIDI sample-accurately, maps VST3 parameters to
// physical values and persists state.

#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/base/ustring.h"
#include "base/source/fstreamer.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

// --- MLang DSP bridge (compiled from src/mla_vocoder_dsp.mla) ----------------
struct Vocoder;
extern "C" Vocoder *mlavocoder_create__f32(float sampleRate);
extern "C" void mlavocoder_destroy__ptr_struct_Vocoder(Vocoder *dsp);
extern "C" void mlavocoder_reset__ptr_struct_Vocoder(Vocoder *dsp);
extern "C" void mlavocoder_note_on__ptr_struct_Vocoder_i32_f32(Vocoder *dsp, int32_t note, float velocity);
extern "C" void mlavocoder_note_off__ptr_struct_Vocoder_i32(Vocoder *dsp, int32_t note);
extern "C" void mlavocoder_set__ptr_struct_Vocoder_i32_f32(Vocoder *dsp, int32_t index, float value);
extern "C" void mlavocoder_set_envelope__ptr_struct_Vocoder_f32_f32_f32_f32(Vocoder *dsp, float attack,
                                                                             float decay, float sustain,
                                                                             float release);
extern "C" float mlavocoder_process__ptr_struct_Vocoder_f32_f32_ptr_f32(Vocoder *dsp, float modulator,
                                                                        float external, float *outRight);

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace mla_vocoder {

// Stable class id. Distinct from the other Mla plug-ins.
static const FUID kProcessorUID(0x4D6C6156, 0x6F633301, 0x8E2B7C45, 0x3A91D6F0);

constexpr uint32 kStateMagic = 0x43564C4D; // "MLVC" little-endian
constexpr uint32 kStateVersion = 1;

// Parameter IDs are stable: new parameters are appended to a section.
enum ParamId : ParamID {
    // Vocoder
    kCarrierParam = 100,
    kInputParam,
    kBandsParam,
    kFormantParam,
    kEnvAttackParam,
    kEnvReleaseParam,
    kSibilanceParam,
    kNoiseParam,
    kWidthParam,
    kVocoderLevelParam,
    kChoirLevelParam,
    kSynthLevelParam,
    kDryLevelParam,
    kEnsembleParam,
    kEnsembleDepthParam,
    kOutputParam,
    // Carrier synth (VCO / VCF / envelope / vibrato / MIDI)
    kWaveParam = 200,
    kPulseWidthParam,
    kMaleParam,
    kFemaleParam,
    kDetuneParam,
    kCutoffParam,
    kResonanceParam,
    kKeyTrackParam,
    kEnvAmountParam,
    kAttackParam,
    kDecayParam,
    kSustainParam,
    kReleaseParam,
    kVibRateParam,
    kVibDepthParam,
    kVibDelayParam,
    kTuneParam,
    kVelocityParam,
    kBendRangeParam,
    kPitchBendParam,
    kModWheelParam,
};

constexpr ParamID kVocoderFirst = kCarrierParam, kVocoderLast = kOutputParam;
constexpr ParamID kSynthFirst = kWaveParam, kSynthLast = kModWheelParam;
constexpr int kNumVocoderParams = kVocoderLast - kVocoderFirst + 1;
constexpr int kNumParams = kNumVocoderParams + (kSynthLast - kSynthFirst + 1);

static ParamID paramIdAt(int index)
{
    if(index < kNumVocoderParams)
        return static_cast<ParamID>(kVocoderFirst + index);
    return static_cast<ParamID>(kSynthFirst + (index - kNumVocoderParams));
}

static int indexOf(ParamID id)
{
    if(id >= kVocoderFirst && id <= kVocoderLast)
        return static_cast<int>(id - kVocoderFirst);
    if(id >= kSynthFirst && id <= kSynthLast)
        return kNumVocoderParams + static_cast<int>(id - kSynthFirst);
    return -1;
}

// DSP parameter indexes (see mlavocoder_set in the .mla file).
enum DspIndex : int32_t {
    kDspCarrier = 0, kDspBands, kDspFormant, kDspEnvAttack, kDspEnvRelease, kDspSibilance, kDspNoise,
    kDspWidth, kDspVocoder, kDspChoir, kDspSynth, kDspDry, kDspEnsembleMix, kDspEnsembleDepth, kDspOutput,
    kDspWave, kDspPulseWidth, kDspMale, kDspFemale, kDspDetune, kDspCutoff, kDspResonance, kDspKeyTrack,
    kDspEnvAmount, kDspVibRate = 28, kDspVibDepth, kDspVibDelay, kDspTune, kDspVelocity, kDspBend,
    kDspModWheel,
};

// --- Normalized -> physical mappings ----------------------------------------
constexpr double kUnityLevelNorm = 60.0 / 66.0;
constexpr int kBendRangeSteps = 12;

static float gainFromNorm(double norm)
{
    // -60 dB .. +6 dB; the bottom of the range is silence.
    if(norm <= 0.0)
        return 0.0f;
    return static_cast<float>(std::pow(10.0, (-60.0 + norm * 66.0) / 20.0));
}
static double logRange(double norm, double low, double high) { return low * std::pow(high / low, norm); }
static double normOfLog(double value, double low, double high) { return std::log(value / low) / std::log(high / low); }
static int listIndex(double norm, int count) { return std::clamp(static_cast<int>(std::lround(norm * (count - 1))), 0, count - 1); }
static float attackFromNorm(double norm) { return static_cast<float>(2.0 * norm * norm * norm); } // 0..2 s
static float timeFromNorm(double norm) { return static_cast<float>(0.001 * std::pow(10000.0, norm)); } // 1 ms..10 s

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

        addAudioInput(STR16("Modulator"), SpeakerArr::kStereo);
        addAudioInput(STR16("Carrier"), SpeakerArr::kStereo, kAux, 0);
        addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
        addEventInput(STR16("MIDI In"), 16);

        addList(kCarrierParam, STR16("Carrier"), {STR16("Internal"), STR16("External"), STR16("Int + Ext")}, 0);
        addList(kInputParam, STR16("Input"), {STR16("Stereo"), STR16("Split L/R")}, 0);
        addList(kBandsParam, STR16("Bands"), {STR16("10"), STR16("16"), STR16("20")}, 0);
        addParam(kFormantParam, STR16("Formant"), STR16("st"), 0.5);
        addParam(kEnvAttackParam, STR16("Env Attack"), STR16("ms"), normOfLog(3.0, 0.5, 50.0));
        addParam(kEnvReleaseParam, STR16("Env Release"), STR16("ms"), normOfLog(60.0, 5.0, 1000.0));
        addParam(kSibilanceParam, STR16("Sibilance"), nullptr, 0.3);
        addParam(kNoiseParam, STR16("Noise"), nullptr, 0.1);
        addParam(kWidthParam, STR16("Width"), nullptr, 0.5);
        addParam(kVocoderLevelParam, STR16("Vocoder Level"), STR16("dB"), kUnityLevelNorm);
        addParam(kChoirLevelParam, STR16("Choir Level"), STR16("dB"), 0.0);
        addParam(kSynthLevelParam, STR16("Synth Level"), STR16("dB"), 0.0);
        addParam(kDryLevelParam, STR16("Dry Level"), STR16("dB"), 0.0);
        addParam(kEnsembleParam, STR16("Ensemble"), nullptr, 0.6);
        addParam(kEnsembleDepthParam, STR16("Ensemble Depth"), nullptr, 0.7);
        addParam(kOutputParam, STR16("Output"), STR16("dB"), kUnityLevelNorm);

        addList(kWaveParam, STR16("Wave"), {STR16("Saw"), STR16("Pulse"), STR16("Saw + Pulse")}, 0);
        addParam(kPulseWidthParam, STR16("Pulse Width"), nullptr, (0.35 - 0.05) / 0.45);
        addParam(kMaleParam, STR16("Male 8'"), nullptr, 1.0);
        addParam(kFemaleParam, STR16("Female 4'"), nullptr, 0.5);
        addParam(kDetuneParam, STR16("Detune"), STR16("ct"), 6.0 / 25.0);
        addParam(kCutoffParam, STR16("Cutoff"), STR16("Hz"), normOfLog(8000.0, 20.0, 20000.0));
        addParam(kResonanceParam, STR16("Resonance"), nullptr, 0.1);
        addParam(kKeyTrackParam, STR16("Key Track"), nullptr, 0.5);
        addParam(kEnvAmountParam, STR16("Filter Env"), nullptr, 0.0);
        addParam(kAttackParam, STR16("Attack"), STR16("s"), std::cbrt(0.005));
        addParam(kDecayParam, STR16("Decay"), STR16("s"), 0.75);
        addParam(kSustainParam, STR16("Sustain"), nullptr, 1.0);
        addParam(kReleaseParam, STR16("Release"), STR16("s"), std::log10(300.0) / 4.0);
        addParam(kVibRateParam, STR16("Vibrato Rate"), STR16("Hz"), normOfLog(5.5, 0.5, 10.0));
        addParam(kVibDepthParam, STR16("Vibrato Depth"), STR16("st"), 0.15);
        addParam(kVibDelayParam, STR16("Vibrato Delay"), STR16("s"), 0.5 / 3.0);
        addParam(kTuneParam, STR16("Tune"), STR16("st"), 0.5);
        addParam(kVelocityParam, STR16("Velocity"), nullptr, 0.3);
        parameters.addParameter(STR16("Bend Range"), STR16("st"), kBendRangeSteps, 2.0 / kBendRangeSteps,
                                ParameterInfo::kCanAutomate, kBendRangeParam);
        norm_[indexOf(kBendRangeParam)].store(2.0 / kBendRangeSteps, std::memory_order_relaxed);
        addParam(kPitchBendParam, STR16("Pitch Bend"), nullptr, 0.5);
        addParam(kModWheelParam, STR16("Mod Wheel"), nullptr, 0.0);
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
        switch(cc) {
            case kPitchBend: id = kPitchBendParam; return kResultOk;
            case kCtrlModWheel: id = kModWheelParam; return kResultOk;
            case kCtrlVolume: id = kOutputParam; return kResultOk;
            case 71: id = kResonanceParam; return kResultOk; // Sound controller 2 (timbre)
            case 74: id = kCutoffParam; return kResultOk;    // Sound controller 5 (brightness)
            case 73: id = kAttackParam; return kResultOk;    // Sound controller 4 (attack)
            case 72: id = kReleaseParam; return kResultOk;   // Sound controller 3 (release)
        }
        return kResultFalse;
    }

    tresult PLUGIN_API setBusArrangements(SpeakerArrangement *inputs, int32 numIns,
                                          SpeakerArrangement *outputs, int32 numOuts) SMTG_OVERRIDE
    {
        // Modulator and optional carrier sidechain: mono or stereo each.
        if(numIns < 1 || numIns > 2 || inputs == nullptr || numOuts != 1 || outputs == nullptr ||
           outputs[0] != SpeakerArr::kStereo)
            return kResultFalse;
        for(int32 i = 0; i < numIns; ++i)
            if(inputs[i] != SpeakerArr::kStereo && inputs[i] != SpeakerArr::kMono)
                return kResultFalse;
        return SingleComponentEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
    }

    tresult PLUGIN_API activateBus(MediaType type, BusDirection dir, int32 index, TBool state) SMTG_OVERRIDE
    {
        if(type == kAudio && dir == kInput && index == 1)
            sidechainActive_.store(state != 0, std::memory_order_relaxed);
        return SingleComponentEffect::activateBus(type, dir, index, state);
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
        dsp_ = mlavocoder_create__f32(static_cast<float>(setup.sampleRate > 0 ? setup.sampleRate : 44100.0));
        pushAll();
        return kResultOk;
    }

    tresult PLUGIN_API setActive(TBool state) SMTG_OVERRIDE
    {
        if(dsp_)
            mlavocoder_reset__ptr_struct_Vocoder(dsp_);
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

        // Inputs may alias the outputs (in-place hosts): read before writing.
        const bool split = listIndex(norm(kInputParam), 2) == 1;
        const AudioBusBuffers *main = data.numInputs >= 1 ? &data.inputs[0] : nullptr;
        const AudioBusBuffers *side = data.numInputs >= 2 && sidechainActive_.load(std::memory_order_relaxed)
                                          ? &data.inputs[1]
                                          : nullptr;
        if(main && (main->numChannels < 1 || main->channelBuffers32 == nullptr))
            main = nullptr;
        if(side && (side->numChannels < 1 || side->channelBuffers32 == nullptr))
            side = nullptr;

        int32 rendered = 0;
        IEventList *events = data.inputEvents;
        const int32 eventCount = events ? events->getEventCount() : 0;
        for(int32 i = 0; i < eventCount; ++i) {
            Event event{};
            if(events->getEvent(i, event) != kResultOk)
                continue;
            const int32 at = std::clamp(event.sampleOffset, rendered, data.numSamples);
            render(main, side, split, outL, outR, rendered, at);
            rendered = at;
            if(event.type == Event::kNoteOnEvent) {
                if(event.noteOn.velocity <= 0.0f)
                    mlavocoder_note_off__ptr_struct_Vocoder_i32(dsp_, event.noteOn.pitch);
                else
                    mlavocoder_note_on__ptr_struct_Vocoder_i32_f32(dsp_, event.noteOn.pitch, event.noteOn.velocity);
            } else if(event.type == Event::kNoteOffEvent) {
                mlavocoder_note_off__ptr_struct_Vocoder_i32(dsp_, event.noteOff.pitch);
            }
        }
        render(main, side, split, outL, outR, rendered, data.numSamples);
        out.silenceFlags = 0;
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
            streamer.writeInt32(static_cast<int32>(paramIdAt(i)));
            streamer.writeDouble(norm_[i].load(std::memory_order_relaxed));
        }
        return kResultOk;
    }

  private:
    Vocoder *dsp_ = nullptr;
    std::atomic<double> norm_[kNumParams];
    std::atomic<bool> paramsDirty_{false};
    std::atomic<bool> sidechainActive_{false};

    void addParam(ParamID id, const TChar *title, const TChar *units, double defaultNorm)
    {
        parameters.addParameter(title, units, 0, defaultNorm, ParameterInfo::kCanAutomate, id);
        norm_[indexOf(id)].store(defaultNorm, std::memory_order_relaxed);
    }

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
            mlavocoder_destroy__ptr_struct_Vocoder(dsp_);
        dsp_ = nullptr;
    }

    void set(int32_t index, double value)
    {
        mlavocoder_set__ptr_struct_Vocoder_i32_f32(dsp_, index, static_cast<float>(value));
    }

    // Send one VST3 parameter's physical value to the DSP.
    void apply(ParamID id)
    {
        if(dsp_ == nullptr)
            return;
        const double n = norm(id);
        switch(id) {
            case kCarrierParam: set(kDspCarrier, listIndex(n, 3)); break;
            case kBandsParam: {
                static const int counts[] = {10, 16, 20};
                set(kDspBands, counts[listIndex(n, 3)]);
                break;
            }
            case kFormantParam: set(kDspFormant, n * 24.0 - 12.0); break;
            case kEnvAttackParam: set(kDspEnvAttack, logRange(n, 0.5, 50.0) * 0.001); break;
            case kEnvReleaseParam: set(kDspEnvRelease, logRange(n, 5.0, 1000.0) * 0.001); break;
            case kSibilanceParam: set(kDspSibilance, n); break;
            case kNoiseParam: set(kDspNoise, n); break;
            case kWidthParam: set(kDspWidth, n); break;
            case kVocoderLevelParam: set(kDspVocoder, gainFromNorm(n)); break;
            case kChoirLevelParam: set(kDspChoir, gainFromNorm(n)); break;
            case kSynthLevelParam: set(kDspSynth, gainFromNorm(n)); break;
            case kDryLevelParam: set(kDspDry, gainFromNorm(n)); break;
            case kEnsembleParam: set(kDspEnsembleMix, n); break;
            case kEnsembleDepthParam: set(kDspEnsembleDepth, n); break;
            case kOutputParam: set(kDspOutput, gainFromNorm(n)); break;
            case kWaveParam: set(kDspWave, listIndex(n, 3)); break;
            case kPulseWidthParam: set(kDspPulseWidth, 0.05 + n * 0.45); break;
            case kMaleParam: set(kDspMale, n); break;
            case kFemaleParam: set(kDspFemale, n); break;
            case kDetuneParam: set(kDspDetune, n * 25.0); break;
            case kCutoffParam: set(kDspCutoff, logRange(n, 20.0, 20000.0)); break;
            case kResonanceParam: set(kDspResonance, n * 34.0); break;
            case kKeyTrackParam: set(kDspKeyTrack, n); break;
            case kEnvAmountParam: set(kDspEnvAmount, n); break;
            case kAttackParam:
            case kDecayParam:
            case kSustainParam:
            case kReleaseParam:
                mlavocoder_set_envelope__ptr_struct_Vocoder_f32_f32_f32_f32(
                    dsp_, attackFromNorm(norm(kAttackParam)), timeFromNorm(norm(kDecayParam)),
                    static_cast<float>(norm(kSustainParam)), timeFromNorm(norm(kReleaseParam)));
                break;
            case kVibRateParam: set(kDspVibRate, logRange(n, 0.5, 10.0)); break;
            case kVibDepthParam: set(kDspVibDepth, n); break;
            case kVibDelayParam: set(kDspVibDelay, n * 3.0); break;
            case kTuneParam: set(kDspTune, n * 24.0 - 12.0); break;
            case kVelocityParam: set(kDspVelocity, n); break;
            case kBendRangeParam:
            case kPitchBendParam: {
                const double range = std::lround(norm(kBendRangeParam) * kBendRangeSteps);
                set(kDspBend, (norm(kPitchBendParam) * 2.0 - 1.0) * range);
                break;
            }
            case kModWheelParam: set(kDspModWheel, n); break;
            default: break; // kInputParam is read in process().
        }
    }

    void pushAll()
    {
        for(int i = 0; i < kNumParams; ++i)
            apply(paramIdAt(i));
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

    static float mono(const AudioBusBuffers &bus, int32 i)
    {
        if(bus.numChannels == 1)
            return bus.channelBuffers32[0][i];
        return 0.5f * (bus.channelBuffers32[0][i] + bus.channelBuffers32[1][i]);
    }

    void render(const AudioBusBuffers *main, const AudioBusBuffers *side, bool split, float *outL, float *outR,
                int32 from, int32 to)
    {
        for(int32 i = from; i < to; ++i) {
            float modulator = 0.0f, external = 0.0f;
            if(main) {
                if(split && main->numChannels >= 2) {
                    modulator = main->channelBuffers32[0][i];
                    external = main->channelBuffers32[1][i];
                } else {
                    modulator = mono(*main, i);
                }
            }
            if(side && !split)
                external = mono(*side, i);
            if(!std::isfinite(modulator))
                modulator = 0.0f;
            if(!std::isfinite(external))
                external = 0.0f;
            float right = 0.0f;
            const float left =
                mlavocoder_process__ptr_struct_Vocoder_f32_f32_ptr_f32(dsp_, modulator, external, &right);
            outL[i] = left;
            outR[i] = right;
        }
    }
};

} // namespace mla_vocoder

BEGIN_FACTORY_DEF("MLang", "https://github.com/mattilaa/mlang", "mailto:devnull@example.invalid")
DEF_CLASS2(INLINE_UID_FROM_FUID(mla_vocoder::kProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass,
           "Mla Vocoder", 0, PlugType::kFxInstrument, "0.1.0", kVstVersionString,
           mla_vocoder::Processor::createInstance)
END_FACTORY
