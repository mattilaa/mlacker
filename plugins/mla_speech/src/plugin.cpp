// Mla Speech - VST3 text-to-speech instrument in the style of the Atari ST's
// speech synthesizer (STSPEECH.TOS).
//
// A note speaks a phrase. The words come with the note as a VST3
// note-expression text event (`kTextTypeID`, or `kPhonemeTypeID` for phoneme
// codes): mlacker sends a pattern's comment cells this way. A note without
// words repeats the last phrase at its own pitch, so a melody can sing it.
// The phrase plays to its end however short the note is, unless Gate is set
// to Note length.
//
// English text becomes phonemes through letter-to-sound rules
// (`speech_rules.h`); the formant synthesis, the YM2149 DAC emulation and the
// output stage are MLang: `src/mla_speech_dsp.mla`. This file declares the
// buses, schedules events sample-accurately, maps VST3 parameters to physical
// values and persists state.

#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstnoteexpression.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/base/ustring.h"
#include "base/source/fstreamer.h"

#include "speech_rules.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>

// --- MLang DSP bridge (compiled from src/mla_speech_dsp.mla) -----------------
struct Speech;
extern "C" Speech *mlaspeech_create__f32(float sampleRate);
extern "C" void mlaspeech_destroy__ptr_struct_Speech(Speech *dsp);
extern "C" void mlaspeech_reset__ptr_struct_Speech(Speech *dsp);
extern "C" void mlaspeech_clear__ptr_struct_Speech(Speech *dsp);
extern "C" void mlaspeech_push__ptr_struct_Speech_i32_f32_f32(Speech *dsp, int32_t code, float pitch, float duration);
extern "C" void mlaspeech_say__ptr_struct_Speech_f32_f32(Speech *dsp, float note, float velocity);
extern "C" void mlaspeech_stop__ptr_struct_Speech(Speech *dsp);
extern "C" int32_t mlaspeech_speaking__ptr_struct_Speech(Speech *dsp);
extern "C" void mlaspeech_set__ptr_struct_Speech_i32_f32(Speech *dsp, int32_t index, float value);
extern "C" float mlaspeech_process__ptr_struct_Speech_ptr_f32(Speech *dsp, float *outRight);

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace mla_speech {

// Stable class id. Distinct from the other Mla plug-ins.
static const FUID kProcessorUID(0x4D6C6153, 0x70656563, 0x68535401, 0x9A3C5E71);

constexpr uint32 kStateMagic = 0x53504C4D; // "MLPS" little-endian
constexpr uint32 kStateVersion = 1;

// Parameter IDs are stable: new parameters are appended.
enum ParamId : ParamID {
    kSpeedParam = 100,
    kFormantParam,
    kIntonationParam,
    kVoiceParam,
    kRateParam,
    kDacParam,
    kSmoothParam,
    kGateParam,
    kTuneParam,
    kVelocityParam,
    kOutputParam,
    kPitchBendParam,
};

constexpr ParamID kFirstParam = kSpeedParam, kLastParam = kPitchBendParam;
constexpr int kNumParams = kLastParam - kFirstParam + 1;

static int indexOf(ParamID id)
{
    return id >= kFirstParam && id <= kLastParam ? static_cast<int>(id - kFirstParam) : -1;
}

// DSP parameter indexes (see mlaspeech_set in the .mla file).
enum DspIndex : int32_t {
    kDspSpeed = 0, kDspFormant, kDspIntonation, kDspVoice, kDspRate, kDspDac, kDspSmooth, kDspTune,
    kDspVelocity, kDspOutput, kDspBend,
};

// --- Normalized -> physical mappings ----------------------------------------
constexpr double kUnityLevelNorm = 60.0 / 66.0;
constexpr double kBendRange = 2.0;

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

// Spoken until a note brings words of its own.
static const char16_t kDefaultPhrase[] = u"Hello. I am Mla Speech.";

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

        addParam(kSpeedParam, STR16("Speed"), STR16("x"), normOfLog(1.0, 0.5, 2.0));
        addParam(kFormantParam, STR16("Mouth"), STR16("st"), 0.5);
        addParam(kIntonationParam, STR16("Intonation"), nullptr, 0.6);
        addList(kVoiceParam, STR16("Voice"), {STR16("Buzz"), STR16("Soft"), STR16("Whisper")}, 0);
        addParam(kRateParam, STR16("ST Rate"), STR16("kHz"), normOfLog(10000.0, 4000.0, 24000.0));
        addList(kDacParam, STR16("DAC"), {STR16("Clean"), STR16("YM 3 voices"), STR16("YM 4-bit")}, 1);
        addParam(kSmoothParam, STR16("Smooth"), nullptr, 0.3);
        addList(kGateParam, STR16("Gate"), {STR16("Whole phrase"), STR16("Note length")}, 0);
        addParam(kTuneParam, STR16("Tune"), STR16("st"), 0.5);
        addParam(kVelocityParam, STR16("Velocity"), nullptr, 0.5);
        addParam(kOutputParam, STR16("Output"), STR16("dB"), kUnityLevelNorm);
        addParam(kPitchBendParam, STR16("Pitch Bend"), nullptr, 0.5);

        translator_ = std::make_unique<Translator>();
        utterance_ = std::make_unique<Utterance>();
        phrase_ = std::make_unique<char16_t[]>(kMaxPhrase);
        setPhrase(kDefaultPhrase, static_cast<int32>(sizeof(kDefaultPhrase) / sizeof(char16_t)) - 1, false);
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
            case kCtrlVolume: id = kOutputParam; return kResultOk;
            case 71: id = kFormantParam; return kResultOk;    // Sound controller 2 (timbre)
            case 74: id = kSmoothParam; return kResultOk;     // Sound controller 5 (brightness)
            case 76: id = kSpeedParam; return kResultOk;      // Sound controller 7
            case 77: id = kIntonationParam; return kResultOk; // Sound controller 8
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
        dsp_ = mlaspeech_create__f32(static_cast<float>(setup.sampleRate > 0 ? setup.sampleRate : 44100.0));
        pushAll();
        loadUtterance();
        return kResultOk;
    }

    tresult PLUGIN_API setActive(TBool state) SMTG_OVERRIDE
    {
        if(dsp_)
            mlaspeech_reset__ptr_struct_Speech(dsp_);
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

        int32 rendered = 0, consumed = -1;
        IEventList *events = data.inputEvents;
        const int32 eventCount = events ? events->getEventCount() : 0;
        for(int32 i = 0; i < eventCount; ++i) {
            Event event{};
            if(i == consumed || events->getEvent(i, event) != kResultOk)
                continue;
            const int32 at = std::clamp(event.sampleOffset, rendered, data.numSamples);
            render(outL, outR, rendered, at);
            rendered = at;
            if(event.type == Event::kNoteOnEvent && event.noteOn.velocity > 0.0f) {
                consumed = noteOn(event, *events, i, eventCount);
            } else if(event.type == Event::kNoteOffEvent ||
                      (event.type == Event::kNoteOnEvent && event.noteOn.velocity <= 0.0f)) {
                const int16 pitch = event.type == Event::kNoteOffEvent ? event.noteOff.pitch : event.noteOn.pitch;
                const int32 id = event.type == Event::kNoteOffEvent ? event.noteOff.noteId : event.noteOn.noteId;
                const bool same = (id != -1 && id == currentNoteId_) || pitch == currentPitch_;
                if(same && listIndex(norm(kGateParam), 2) == 1)
                    mlaspeech_stop__ptr_struct_Speech(dsp_);
            } else if(event.type == Event::kNoteExpressionTextEvent) {
                // Words for a note that is already speaking (or arrived
                // without one): speak them from the start.
                const auto &words = event.noteExpressionText;
                if(isWords(words.typeId) && words.text != nullptr) {
                    setPhrase(reinterpret_cast<const char16_t *>(words.text), static_cast<int32>(words.textLen),
                              words.typeId == kPhonemeTypeID);
                    loadUtterance();
                    if(words.noteId != -1 && words.noteId == currentNoteId_)
                        mlaspeech_say__ptr_struct_Speech_f32_f32(dsp_, currentNote_, currentVelocity_);
                }
            }
        }
        render(outL, outR, rendered, data.numSamples);
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
            streamer.writeInt32(static_cast<int32>(kFirstParam + i));
            streamer.writeDouble(norm_[i].load(std::memory_order_relaxed));
        }
        return kResultOk;
    }

  private:
    static constexpr int32 kMaxPhrase = 1024;

    Speech *dsp_ = nullptr;
    std::atomic<double> norm_[kNumParams];
    std::atomic<bool> paramsDirty_{false};
    // The phrase in use (audio thread once processing; set up in initialize).
    std::unique_ptr<Translator> translator_;
    std::unique_ptr<Utterance> utterance_;
    std::unique_ptr<char16_t[]> phrase_;
    int32 phraseLength_ = 0;
    bool phonetic_ = false;
    // The note speaking: its id, key, pitch (with bend applied in the DSP)
    // and velocity.
    int32 currentNoteId_ = -1;
    int16 currentPitch_ = -1;
    float currentNote_ = 48.0f, currentVelocity_ = 1.0f;

    static bool isWords(NoteExpressionTypeID type) { return type == kTextTypeID || type == kPhonemeTypeID; }

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
            mlaspeech_destroy__ptr_struct_Speech(dsp_);
        dsp_ = nullptr;
    }

    void set(int32_t index, double value)
    {
        mlaspeech_set__ptr_struct_Speech_i32_f32(dsp_, index, static_cast<float>(value));
    }

    void setPhrase(const char16_t *text, int32 length, bool phonetic)
    {
        phraseLength_ = std::clamp<int32>(length, 0, kMaxPhrase);
        std::copy_n(text, phraseLength_, phrase_.get());
        phonetic_ = phonetic;
    }

    // Translate the phrase and hand its phonemes to the DSP.
    void loadUtterance()
    {
        if(dsp_ == nullptr)
            return;
        translator_->translate(phrase_.get(), phraseLength_, *utterance_, phonetic_);
        mlaspeech_clear__ptr_struct_Speech(dsp_);
        for(int i = 0; i < utterance_->count; ++i)
            mlaspeech_push__ptr_struct_Speech_i32_f32_f32(dsp_, utterance_->code[i], utterance_->pitch[i],
                                                          utterance_->duration[i]);
    }

    // A note-on speaks. Words for it may follow at the same offset (VST3
    // sends a note's expressions after the note itself): they are taken
    // first, and their event index is returned so it is not handled again.
    int32 noteOn(const Event &event, IEventList &events, int32 index, int32 count)
    {
        const int32 id = event.noteOn.noteId;
        int32 consumed = -1;
        for(int32 j = index + 1; j < count && id != -1; ++j) {
            Event next{};
            if(events.getEvent(j, next) != kResultOk || next.sampleOffset != event.sampleOffset)
                break;
            if(next.type != Event::kNoteExpressionTextEvent || !isWords(next.noteExpressionText.typeId) ||
               next.noteExpressionText.text == nullptr || next.noteExpressionText.noteId != id)
                continue;
            setPhrase(reinterpret_cast<const char16_t *>(next.noteExpressionText.text),
                      static_cast<int32>(next.noteExpressionText.textLen),
                      next.noteExpressionText.typeId == kPhonemeTypeID);
            loadUtterance();
            consumed = j;
            break;
        }
        currentPitch_ = event.noteOn.pitch;
        currentNote_ = static_cast<float>(event.noteOn.pitch) + event.noteOn.tuning * 0.01f;
        currentVelocity_ = event.noteOn.velocity;
        mlaspeech_say__ptr_struct_Speech_f32_f32(dsp_, currentNote_, currentVelocity_);
        // Words that arrive later for this note restart it with them.
        currentNoteId_ = id;
        return consumed;
    }

    // Send one VST3 parameter's physical value to the DSP.
    void apply(ParamID id)
    {
        if(dsp_ == nullptr)
            return;
        const double n = norm(id);
        switch(id) {
            case kSpeedParam: set(kDspSpeed, logRange(n, 0.5, 2.0)); break;
            case kFormantParam: set(kDspFormant, std::pow(2.0, (n * 12.0 - 6.0) / 12.0)); break;
            case kIntonationParam: set(kDspIntonation, n); break;
            case kVoiceParam: set(kDspVoice, listIndex(n, 3)); break;
            case kRateParam: set(kDspRate, logRange(n, 4000.0, 24000.0)); break;
            case kDacParam: set(kDspDac, listIndex(n, 3)); break;
            case kSmoothParam: set(kDspSmooth, n); break;
            case kTuneParam: set(kDspTune, n * 48.0 - 24.0); break;
            case kVelocityParam: set(kDspVelocity, n); break;
            case kOutputParam: set(kDspOutput, gainFromNorm(n)); break;
            case kPitchBendParam: set(kDspBend, (n * 2.0 - 1.0) * kBendRange); break;
            default: break; // Gate is read in process().
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
            outL[i] = mlaspeech_process__ptr_struct_Speech_ptr_f32(dsp_, &right);
            outR[i] = right;
        }
    }
};

} // namespace mla_speech

BEGIN_FACTORY_DEF("MLang", "https://github.com/mattilaa/mlang", "mailto:devnull@example.invalid")
DEF_CLASS2(INLINE_UID_FROM_FUID(mla_speech::kProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass,
           "Mla Speech", 0, PlugType::kInstrumentSynth, "0.1.0", kVstVersionString,
           mla_speech::Processor::createInstance)
END_FACTORY
