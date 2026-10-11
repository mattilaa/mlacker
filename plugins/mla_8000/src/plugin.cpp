// Mla 8000 - VST3 instrument in the style of the Roland JP-8000.
//
// The voice is MLang: `src/mla_8000_dsp.mla` runs one voice's oscillators
// (the Super Saw and the JP-8000's other OSC 1 and OSC 2 waveforms), its
// pitch, filter and amp envelopes and its filter. This file plays the panel:
// it allocates eight voices (Poly, Mono, Legato or Unison), runs the two
// LFOs, portamento, pitch bend and the mod wheel at control rate (every 16
// frames), and runs the effects after the voices: tone (bass and treble
// shelves), chorus (stereo or super chorus) and a stereo or panning delay
// that can follow the host tempo.

#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "base/source/fstreamer.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <vector>

// --- MLang DSP bridge (compiled from src/mla_8000_dsp.mla) -------------------
struct Jp8Voice;
extern "C" Jp8Voice *mla8000_voice_create__f32(float sampleRate);
extern "C" void mla8000_voice_destroy__ptr_struct_Jp8Voice(Jp8Voice *voice);
extern "C" void mla8000_voice_set__ptr_struct_Jp8Voice_i32_f32(Jp8Voice *voice, int32_t index, float value);
extern "C" void mla8000_voice_update__ptr_struct_Jp8Voice(Jp8Voice *voice);
extern "C" void mla8000_voice_note_on__ptr_struct_Jp8Voice_i32(Jp8Voice *voice, int32_t retrigger);
extern "C" void mla8000_voice_note_off__ptr_struct_Jp8Voice(Jp8Voice *voice);
extern "C" void mla8000_voice_reset__ptr_struct_Jp8Voice(Jp8Voice *voice);
extern "C" int32_t mla8000_voice_active__ptr_struct_Jp8Voice(Jp8Voice *voice);
extern "C" float mla8000_voice_level__ptr_struct_Jp8Voice(Jp8Voice *voice);
extern "C" float mla8000_voice_process__ptr_struct_Jp8Voice_ptr_f32(Jp8Voice *voice, float *outRight);

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace mla_8000 {

// Stable class id. Distinct from the other Mla plug-ins.
static const FUID kProcessorUID(0x4D6C6138, 0x30303050, 0x4A503830, 0x30305C38);

constexpr uint32 kStateMagic = 0x504B384D; // "M8KP" little-endian
constexpr uint32 kStateVersion = 1;

// Parameter IDs are stable: new parameters are appended.
enum ParamId : ParamID {
    kOutputParam = 100,
    kKeyModeParam,
    kUnisonDetuneParam,
    kPortamentoParam,
    kPortaTimeParam,
    kBendRangeParam,
    kPitchBendParam,
    kModWheelParam,
    kWheelCutoffParam,
    kWheelPitchParam,
    kVelocityAmpParam,
    kVelocityFilterParam,
    // LFO 1 (shape, rate, fade) and LFO 2 (a triangle) with their depths.
    kLfo1WaveParam,
    kLfo1RateParam,
    kLfo1FadeParam,
    kLfo2RateParam,
    kLfo2PitchParam,
    kLfo2FilterParam,
    kLfo2AmpParam,
    // Oscillators.
    kOsc1WaveParam,
    kOsc1Ctrl1Param,
    kOsc1Ctrl2Param,
    kOsc2WaveParam,
    kOsc2RangeParam,
    kOsc2FineParam,
    kOsc2WidthParam,
    kSyncParam,
    kRingParam,
    kCrossModParam,
    kBalanceParam,
    kLfo1PitchParam,
    kPitchEnvDepthParam,
    kPitchEnvAttackParam,
    kPitchEnvDecayParam,
    // Filter.
    kFilterTypeParam,
    kFilterSlopeParam,
    kCutoffParam,
    kResonanceParam,
    kKeyFollowParam,
    kLfo1FilterParam,
    kFilterEnvDepthParam,
    kFilterAttackParam,
    kFilterDecayParam,
    kFilterSustainParam,
    kFilterReleaseParam,
    // Amp.
    kLevelParam,
    kLfo1AmpParam,
    kAmpAttackParam,
    kAmpDecayParam,
    kAmpSustainParam,
    kAmpReleaseParam,
    // Effects.
    kBassParam,
    kTrebleParam,
    kChorusTypeParam,
    kChorusRateParam,
    kChorusDepthParam,
    kChorusLevelParam,
    kDelayTypeParam,
    kDelaySyncParam,
    kDelayTimeParam,
    kDelayFeedbackParam,
    kDelayLevelParam,
    kDriveParam,
    kLastParam = kDriveParam,
};

constexpr ParamID kFirstSlot = kOutputParam;
constexpr int kSlots = kLastParam - kFirstSlot + 1;
static int slotOf(ParamID id) { return id >= kFirstSlot && id <= kLastParam ? static_cast<int>(id - kFirstSlot) : -1; }

// Voice settings (see mla8000_voice_set in the .mla file).
enum DspIndex : int32_t {
    kDspWave1 = 0, kDspCtrl1, kDspCtrl2, kDspHz1, kDspWave2, kDspHz2, kDspWidth2, kDspSync, kDspRing, kDspCross,
    kDspBalance, kDspLfo, kDspPitchDepth, kDspPitchAttack, kDspPitchDecay, kDspFilterType, kDspSlope, kDspCutoff,
    kDspResonance, kDspEnvDepth, kDspFilterAttack, kDspFilterDecay, kDspFilterSustain, kDspFilterRelease,
    kDspAmpAttack, kDspAmpDecay, kDspAmpSustain, kDspAmpRelease, kDspLevel, kDspPan, kDspDrive,
};

enum KeyMode : int { kPoly = 0, kMono, kLegato, kUnison };
enum ChorusType : int { kChorusOff = 0, kChorus1, kChorus2, kSuperChorus, kFlanger };
enum DelayType : int { kDelayOff = 0, kDelayStereo, kDelayPanning };

constexpr int kVoices = 8;
constexpr int kControlFrames = 16;
constexpr double kVoiceGain = 0.5;

// --- Normalized -> physical mappings ----------------------------------------
constexpr double kUnityLevelNorm = 60.0 / 66.0;
static double gainFromNorm(double n) { return n <= 0.0 ? 0.0 : std::pow(10.0, (-60.0 + n * 66.0) / 20.0); }
// Envelope and portamento times: 0, then 1 ms .. 10 s exponential.
static double timeFromNorm(double n) { return n <= 0.0 ? 0.0 : 0.001 * std::pow(10000.0, n); }
static double cutoffFromNorm(double n) { return 20.0 * std::pow(1000.0, n); }          // 20 Hz .. 20 kHz
static double lfoRateFromNorm(double n) { return 0.05 * std::pow(600.0, n); }          // 0.05 .. 30 Hz
static double chorusRateFromNorm(double n) { return 0.1 * std::pow(100.0, n); }        // 0.1 .. 10 Hz
static double delayMsFromNorm(double n) { return 1.0 + n * 1249.0; }                   // 1 .. 1250 ms
static double bipolar(double n) { return n * 2.0 - 1.0; }
static double normFor(double value, double (*map)(double))
{
    double lo = 0.0, hi = 1.0;
    for(int i = 0; i < 60; ++i) {
        const double mid = 0.5 * (lo + hi);
        (map(mid) < value ? lo : hi) = mid;
    }
    return 0.5 * (lo + hi);
}
static void formatTime(double n, char *s, size_t z)
{
    const double t = timeFromNorm(n);
    if(t < 1.0) std::snprintf(s, z, "%.0f ms", t * 1000.0);
    else std::snprintf(s, z, "%.2f s", t);
}

// A continuous parameter that shows its physical value.
class MappedParameter final : public Parameter {
  public:
    MappedParameter(const TChar *title, ParamID id, const TChar *units, double defaultNorm,
                    std::function<void(double, char *, size_t)> format)
    : Parameter(title, id, units, defaultNorm, 0, ParameterInfo::kCanAutomate), format_(std::move(format))
    {
    }
    void toString(ParamValue normalized, String128 string) const SMTG_OVERRIDE
    {
        char text[64];
        format_(normalized, text, sizeof(text));
        UString(string, 128).fromAscii(text);
    }

  private:
    std::function<void(double, char *, size_t)> format_;
};

// --- Effects ------------------------------------------------------------------

// An RBJ shelf (`high` false: low shelf).
struct Shelf {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void design(double rate, double hz, double db, bool high)
    {
        const double a = std::pow(10.0, db / 40.0);
        const double w = 2.0 * M_PI * hz / rate, c = std::cos(w), s = std::sin(w);
        const double alpha = s / 2.0 * std::sqrt(2.0);
        const double r = 2.0 * std::sqrt(a) * alpha;
        double n0, n1, n2, d0, d1, d2;
        if(high) {
            n0 = a * ((a + 1) + (a - 1) * c + r); n1 = -2 * a * ((a - 1) + (a + 1) * c); n2 = a * ((a + 1) + (a - 1) * c - r);
            d0 = (a + 1) - (a - 1) * c + r; d1 = 2 * ((a - 1) - (a + 1) * c); d2 = (a + 1) - (a - 1) * c - r;
        } else {
            n0 = a * ((a + 1) - (a - 1) * c + r); n1 = 2 * a * ((a - 1) - (a + 1) * c); n2 = a * ((a + 1) - (a - 1) * c - r);
            d0 = (a + 1) + (a - 1) * c + r; d1 = -2 * ((a - 1) + (a + 1) * c); d2 = (a + 1) + (a - 1) * c - r;
        }
        b0 = n0 / d0; b1 = n1 / d0; b2 = n2 / d0; a1 = d1 / d0; a2 = d2 / d0;
    }
    float run(float x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return static_cast<float>(y);
    }
    void clear() { z1 = z2 = 0; }
};

// A delay line read with linear interpolation.
struct Line {
    std::vector<float> data;
    int at = 0;
    void resize(int frames) { data.assign(static_cast<size_t>(std::max(frames, 4)), 0.0f); at = 0; }
    void clear() { std::fill(data.begin(), data.end(), 0.0f); }
    void push(float x) { data[static_cast<size_t>(at)] = x; at = (at + 1) % static_cast<int>(data.size()); }
    // `delay` frames ago (1 = the newest sample).
    float read(double delay) const
    {
        const int size = static_cast<int>(data.size());
        delay = std::clamp(delay, 1.0, static_cast<double>(size - 2));
        const int whole = static_cast<int>(delay);
        const double frac = delay - whole;
        const int i0 = (at - whole + 2 * size) % size, i1 = (i0 - 1 + size) % size;
        return static_cast<float>(data[static_cast<size_t>(i0)] * (1.0 - frac) + data[static_cast<size_t>(i1)] * frac);
    }
};

// --- The player ---------------------------------------------------------------

struct Held {
    int16 pitch;
    float velocity;
};

struct PlayVoice {
    int note = -1;          // the key it plays, -1 before the first
    bool gate = false;
    double pitch = 60.0;    // current (gliding) pitch in semitones
    double from = 60.0;     // where the glide started
    float velocity = 1.0f;
    double detune = 0.0;    // unison offset, semitones
    double pan = 0.0;
    uint64_t age = 0;
    double sinceOn = 0.0;   // seconds since the note started (LFO 1 fade)
};

class Processor final : public SingleComponentEffect, public IMidiMapping {
  public:
    Processor()
    {
        for(auto &value : norm_)
            value.store(0.0, std::memory_order_relaxed);
        registered_.fill(false);
        voice_.fill(nullptr);
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

        // The defaults are the JP-8000's trance lead: a Super Saw at Detune
        // 0.7 and Mix 0.8, the 24 dB low-pass mostly open, chorus and delay.
        addMapped(kOutputParam, "Output", "dB", kUnityLevelNorm, [](double n, char *s, size_t z) {
            if(n <= 0.0) std::snprintf(s, z, "-inf");
            else std::snprintf(s, z, "%.1f", -60.0 + n * 66.0);
        });
        addList(kKeyModeParam, "Key Mode", {"Poly", "Mono", "Legato", "Unison"}, kPoly);
        addMapped(kUnisonDetuneParam, "Unison Detune", "ct", 0.3,
                  [](double n, char *s, size_t z) { std::snprintf(s, z, "%.1f", n * 50.0); });
        addList(kPortamentoParam, "Portamento", {"Off", "On"}, 0);
        addMapped(kPortaTimeParam, "Portamento Time", nullptr, normFor(0.1, timeFromNorm), formatTime);
        addRange(kBendRangeParam, "Bend Range", "st", 0, 24, 2);
        addMapped(kPitchBendParam, "Pitch Bend", nullptr, 8192.0 / 16383.0, [](double n, char *s, size_t z) {
            std::snprintf(s, z, "%+.0f", n * 16383.0 - 8192.0);
        });
        addMapped(kModWheelParam, "Mod Wheel", nullptr, 0.0,
                  [](double n, char *s, size_t z) { std::snprintf(s, z, "%.0f", n * 127.0); });
        addBipolar(kWheelCutoffParam, "Wheel > Cutoff", "oct", 0.5, 8.0);
        addMapped(kWheelPitchParam, "Wheel > LFO 1 Pitch", "st", 0.0,
                  [](double n, char *s, size_t z) { std::snprintf(s, z, "%.2f", n * 2.0); });
        addPercent(kVelocityAmpParam, "Velocity > Amp", 0.5);
        addPercent(kVelocityFilterParam, "Velocity > Filter", 0.0);

        addList(kLfo1WaveParam, "LFO 1 Wave", {"Triangle", "Saw", "Square", "S&H"}, 0);
        addMapped(kLfo1RateParam, "LFO 1 Rate", "Hz", normFor(5.0, lfoRateFromNorm),
                  [](double n, char *s, size_t z) { std::snprintf(s, z, "%.2f", lfoRateFromNorm(n)); });
        addMapped(kLfo1FadeParam, "LFO 1 Fade", nullptr, 0.0, formatTime);
        addMapped(kLfo2RateParam, "LFO 2 Rate", "Hz", normFor(0.3, lfoRateFromNorm),
                  [](double n, char *s, size_t z) { std::snprintf(s, z, "%.2f", lfoRateFromNorm(n)); });
        addBipolar(kLfo2PitchParam, "LFO 2 Pitch", "st", 0.5, 1.0);
        addBipolar(kLfo2FilterParam, "LFO 2 Filter", "oct", 0.5, 4.0);
        addPercent(kLfo2AmpParam, "LFO 2 Amp", 0.0);

        addList(kOsc1WaveParam, "OSC 1 Wave",
                {"Super Saw", "Triangle Mod", "Noise", "Feedback Osc", "Square", "Saw", "Triangle"}, 0);
        addPercent(kOsc1Ctrl1Param, "OSC 1 Ctrl 1", 0.7);
        addPercent(kOsc1Ctrl2Param, "OSC 1 Ctrl 2", 0.8);
        addList(kOsc2WaveParam, "OSC 2 Wave", {"Square", "Saw", "Triangle", "Noise"}, 1);
        addRange(kOsc2RangeParam, "OSC 2 Range", "st", -24, 24, 0);
        addBipolar(kOsc2FineParam, "OSC 2 Fine", "ct", 0.5, 50.0);
        addPercent(kOsc2WidthParam, "OSC 2 PW", 0.5);
        addList(kSyncParam, "OSC Sync", {"Off", "On"}, 0);
        addList(kRingParam, "Ring Mod", {"Off", "On"}, 0);
        addPercent(kCrossModParam, "Cross Mod", 0.0);
        addPercent(kBalanceParam, "OSC Balance", 0.0);
        addMapped(kLfo1PitchParam, "LFO 1 Pitch", "st", 0.0,
                  [](double n, char *s, size_t z) { std::snprintf(s, z, "%.2f", n * 12.0); });
        addBipolar(kPitchEnvDepthParam, "Pitch Env Depth", "st", 0.5, 63.0);
        addMapped(kPitchEnvAttackParam, "Pitch Env Attack", nullptr, 0.0, formatTime);
        addMapped(kPitchEnvDecayParam, "Pitch Env Decay", nullptr, normFor(0.2, timeFromNorm), formatTime);

        addList(kFilterTypeParam, "Filter Type", {"LPF", "BPF", "HPF"}, 0);
        addList(kFilterSlopeParam, "Filter Slope", {"-12 dB", "-24 dB"}, 1);
        addMapped(kCutoffParam, "Cutoff", "Hz", normFor(8000.0, cutoffFromNorm),
                  [](double n, char *s, size_t z) { std::snprintf(s, z, "%.0f", cutoffFromNorm(n)); });
        addPercent(kResonanceParam, "Resonance", 0.15);
        addPercent(kKeyFollowParam, "Key Follow", 0.5);
        addBipolar(kLfo1FilterParam, "LFO 1 Filter", "oct", 0.5, 4.0);
        addBipolar(kFilterEnvDepthParam, "Filter Env Depth", "oct", 0.5, 8.0);
        addMapped(kFilterAttackParam, "Filter Attack", nullptr, 0.0, formatTime);
        addMapped(kFilterDecayParam, "Filter Decay", nullptr, normFor(0.5, timeFromNorm), formatTime);
        addPercent(kFilterSustainParam, "Filter Sustain", 0.6);
        addMapped(kFilterReleaseParam, "Filter Release", nullptr, normFor(0.4, timeFromNorm), formatTime);

        addPercent(kLevelParam, "Level", 0.8);
        addPercent(kLfo1AmpParam, "LFO 1 Amp", 0.0);
        addMapped(kAmpAttackParam, "Amp Attack", nullptr, normFor(0.005, timeFromNorm), formatTime);
        addMapped(kAmpDecayParam, "Amp Decay", nullptr, normFor(1.0, timeFromNorm), formatTime);
        addPercent(kAmpSustainParam, "Amp Sustain", 1.0);
        addMapped(kAmpReleaseParam, "Amp Release", nullptr, normFor(0.4, timeFromNorm), formatTime);

        addBipolar(kBassParam, "Tone Bass", "dB", 0.5, 12.0);
        addBipolar(kTrebleParam, "Tone Treble", "dB", 0.5, 12.0);
        addList(kChorusTypeParam, "Chorus", {"Off", "Chorus 1", "Chorus 2", "Super Chorus", "Flanger"}, kChorus1);
        addMapped(kChorusRateParam, "Chorus Rate", "Hz", normFor(0.6, chorusRateFromNorm),
                  [](double n, char *s, size_t z) { std::snprintf(s, z, "%.2f", chorusRateFromNorm(n)); });
        addPercent(kChorusDepthParam, "Chorus Depth", 0.6);
        addPercent(kChorusLevelParam, "Chorus Level", 0.5);
        addList(kDelayTypeParam, "Delay", {"Off", "Stereo", "Panning"}, kDelayPanning);
        addList(kDelaySyncParam, "Delay Sync", {"Off", "1/16", "1/8", "3/16", "1/4", "3/8", "1/2"}, 3);
        addMapped(kDelayTimeParam, "Delay Time", "ms", normFor(340.0, delayMsFromNorm),
                  [](double n, char *s, size_t z) { std::snprintf(s, z, "%.0f", delayMsFromNorm(n)); });
        addPercent(kDelayFeedbackParam, "Delay Feedback", 0.35);
        addPercent(kDelayLevelParam, "Delay Level", 0.25);
        addPercent(kDriveParam, "Drive", 0.0);
        return kResultOk;
    }

    tresult PLUGIN_API terminate() SMTG_OVERRIDE
    {
        destroyDsp();
        return SingleComponentEffect::terminate();
    }

    tresult PLUGIN_API getMidiControllerAssignment(int32 bus, int16 channel, CtrlNumber cc, ParamID &id) SMTG_OVERRIDE
    {
        if(bus != 0 || channel < 0 || channel > 15)
            return kResultFalse;
        switch(cc) {
            case kCtrlModWheel: id = kModWheelParam; return kResultOk;
            case kCtrlPortaTime: id = kPortaTimeParam; return kResultOk;
            case kCtrlVolume: id = kOutputParam; return kResultOk;
            case 16: id = kOsc1Ctrl1Param; return kResultOk; // Super Saw Detune
            case 17: id = kOsc1Ctrl2Param; return kResultOk; // Super Saw Mix
            case 18: id = kBalanceParam; return kResultOk;
            case 19: id = kFilterEnvDepthParam; return kResultOk;
            case 71: id = kResonanceParam; return kResultOk;
            case 72: id = kAmpReleaseParam; return kResultOk;
            case 73: id = kAmpAttackParam; return kResultOk;
            case 74: id = kCutoffParam; return kResultOk;
            case 75: id = kAmpDecayParam; return kResultOk;
            case 76: id = kLfo1RateParam; return kResultOk;
            case 91: id = kDelayLevelParam; return kResultOk;
            case 93: id = kChorusLevelParam; return kResultOk;
            case kPitchBend: id = kPitchBendParam; return kResultOk;
            default: return kResultFalse;
        }
    }

    tresult PLUGIN_API setBusArrangements(SpeakerArrangement *inputs, int32 numIns, SpeakerArrangement *outputs,
                                          int32 numOuts) SMTG_OVERRIDE
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
        rate_ = setup.sampleRate >= 8000.0 && setup.sampleRate <= 384000.0 ? setup.sampleRate : 44100.0;
        for(auto &voice : voice_)
            voice = mla8000_voice_create__f32(static_cast<float>(rate_));
        chorusL_.resize(static_cast<int>(rate_ * 0.05));
        chorusR_.resize(static_cast<int>(rate_ * 0.05));
        delayL_.resize(static_cast<int>(rate_ * 2.6));
        delayR_.resize(static_cast<int>(rate_ * 2.6));
        restart();
        return kResultOk;
    }

    tresult PLUGIN_API setActive(TBool state) SMTG_OVERRIDE
    {
        if(voice_[0])
            restart();
        return SingleComponentEffect::setActive(state);
    }

    tresult PLUGIN_API process(ProcessData &data) SMTG_OVERRIDE
    {
        if(voice_[0] == nullptr || data.symbolicSampleSize != kSample32)
            return kResultFalse;
        if(paramsDirty_.exchange(false, std::memory_order_acquire))
            pushAll();
        if(const ProcessContext *context = data.processContext)
            tempo_ = (context->state & ProcessContext::kTempoValid) && std::isfinite(context->tempo) && context->tempo > 0
                   ? context->tempo : 0.0;

        // Notes and parameter points in sample order; at one offset, the
        // parameters first (a CC on a tracker row shapes that row's note).
        int count = 0;
        if(IParameterChanges *changes = data.inputParameterChanges) {
            const int32 queues = changes->getParameterCount();
            for(int32 q = 0; q < queues; ++q) {
                IParamValueQueue *queue = changes->getParameterData(q);
                if(queue == nullptr || slotOf(queue->getParameterId()) < 0)
                    continue;
                for(int32 p = 0; p < queue->getPointCount() && count < kMaxItems; ++p) {
                    Item &item = items_[count];
                    if(queue->getPoint(p, item.offset, item.value) != kResultOk || !std::isfinite(item.value))
                        continue;
                    item.kind = Item::kParam;
                    item.id = queue->getParameterId();
                    item.order = count++;
                }
            }
        }
        if(IEventList *events = data.inputEvents) {
            const int32 eventCount = events->getEventCount();
            for(int32 i = 0; i < eventCount && count < kMaxItems; ++i) {
                Event event{};
                if(events->getEvent(i, event) != kResultOk)
                    continue;
                Item &item = items_[count];
                item.offset = event.sampleOffset;
                if(event.type == Event::kNoteOnEvent) {
                    item.kind = event.noteOn.velocity > 0.0f ? Item::kNoteOn : Item::kNoteOff;
                    item.pitch = event.noteOn.pitch;
                    item.velocity = event.noteOn.velocity;
                } else if(event.type == Event::kNoteOffEvent) {
                    item.kind = Item::kNoteOff;
                    item.pitch = event.noteOff.pitch;
                } else {
                    continue;
                }
                item.order = count++;
            }
        }
        std::sort(items_.begin(), items_.begin() + count, [](const Item &a, const Item &b) {
            if(a.offset != b.offset)
                return a.offset < b.offset;
            if((a.kind == Item::kParam) != (b.kind == Item::kParam))
                return a.kind == Item::kParam;
            return a.order < b.order;
        });

        if(data.numOutputs < 1 || data.outputs[0].numChannels < 2 || data.outputs[0].channelBuffers32 == nullptr) {
            for(int i = 0; i < count; ++i)
                apply(items_[i]);
            return kResultOk;
        }
        AudioBusBuffers &out = data.outputs[0];
        float *outL = out.channelBuffers32[0];
        float *outR = out.channelBuffers32[1];

        // Silent, with the effects' tails gone and nothing to do: zeros.
        if(count == 0 && !anyActive() && tail_ <= 0) {
            std::fill(outL, outL + data.numSamples, 0.0f);
            std::fill(outR, outR + data.numSamples, 0.0f);
            out.silenceFlags = 3;
            return kResultOk;
        }

        int32 rendered = 0;
        for(int i = 0; i < count; ++i) {
            const int32 at = std::clamp(items_[i].offset, rendered, data.numSamples);
            render(outL, outR, rendered, at);
            rendered = at;
            apply(items_[i]);
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
        if(!streamer.readInt32u(magic) || magic != kStateMagic || !streamer.readInt32u(version) || version != kStateVersion)
            return kResultFalse;
        int32 count = 0;
        if(!streamer.readInt32(count) || count < 0 || count > 4096)
            return kResultFalse;
        for(int32 i = 0; i < count; ++i) {
            int32 id = 0;
            double value = 0;
            if(!streamer.readInt32(id) || !streamer.readDouble(value))
                return kResultFalse;
            const int slot = slotOf(static_cast<ParamID>(id));
            if(slot < 0 || !registered_[slot] || !std::isfinite(value))
                continue; // Unknown parameter from a newer version.
            value = std::clamp(value, 0.0, 1.0);
            norm_[slot].store(value, std::memory_order_relaxed);
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
        int32 count = 0;
        for(bool used : registered_)
            count += used ? 1 : 0;
        streamer.writeInt32(count);
        for(int slot = 0; slot < kSlots; ++slot) {
            if(!registered_[slot])
                continue;
            streamer.writeInt32(static_cast<int32>(kFirstSlot + slot));
            streamer.writeDouble(norm_[slot].load(std::memory_order_relaxed));
        }
        return kResultOk;
    }

  private:
    struct Item {
        enum Kind : int { kParam, kNoteOn, kNoteOff };
        int32 offset = 0;
        int kind = kParam;
        ParamID id = 0;
        ParamValue value = 0;
        int16 pitch = 0;
        float velocity = 0;
        int order = 0;
    };
    static constexpr int kMaxItems = 4096;
    static constexpr int kMaxHeld = 128;

    std::array<Jp8Voice *, kVoices> voice_{};
    double rate_ = 44100.0;
    double tempo_ = 0.0;
    std::atomic<double> norm_[kSlots];
    std::array<bool, kSlots> registered_{};
    std::array<int, kSlots> steps_{};
    std::atomic<bool> paramsDirty_{false};
    std::array<Item, kMaxItems> items_{};

    // --- Player state (audio thread) -------------------------------------
    std::array<PlayVoice, kVoices> play_{};
    std::array<Held, kMaxHeld> held_{};
    int heldCount_ = 0;
    uint64_t clock_ = 0;
    int untilControl_ = 0;
    double lastNote_ = -1.0;  // the last key played (portamento in Poly)
    double lfo1Phase_ = 0.0, lfo2Phase_ = 0.0, lfo1Held_ = 0.0;
    uint32_t random_ = 0x12345678u;
    int tail_ = 0;            // frames the effects still ring after the voices stop

    // Effects.
    Shelf bassL_, bassR_, trebleL_, trebleR_;
    Line chorusL_, chorusR_, delayL_, delayR_;
    double chorusPhase_ = 0.0;
    float delayDampL_ = 0.0f, delayDampR_ = 0.0f;
    double delayFrames_ = 0.0, delayTarget_ = 0.0;

    // --- Parameter plumbing -----------------------------------------------
    void registerSlot(ParamID id, double defaultNorm, int steps = 0)
    {
        const int slot = slotOf(id);
        registered_[slot] = true;
        steps_[slot] = steps;
        norm_[slot].store(defaultNorm, std::memory_order_relaxed);
    }
    void addMapped(ParamID id, const char *title, const char *units, double defaultNorm,
                   std::function<void(double, char *, size_t)> format)
    {
        UString128 t(title), u(units ? units : "");
        parameters.addParameter(new MappedParameter(t, id, u, defaultNorm, std::move(format)));
        registerSlot(id, defaultNorm);
    }
    void addPercent(ParamID id, const char *title, double defaultNorm)
    {
        addMapped(id, title, "%", defaultNorm, [](double n, char *s, size_t z) { std::snprintf(s, z, "%.0f", n * 100.0); });
    }
    // -range .. +range around the middle.
    void addBipolar(ParamID id, const char *title, const char *units, double defaultNorm, double range)
    {
        addMapped(id, title, units, defaultNorm, [range](double n, char *s, size_t z) {
            std::snprintf(s, z, "%+.1f", bipolar(n) * range);
        });
    }
    void addRange(ParamID id, const char *title, const char *units, int low, int high, int defaultValue)
    {
        UString128 t(title), u(units ? units : "");
        const int steps = high - low;
        parameters.addParameter(new RangeParameter(t, id, u, low, high, defaultValue, steps, ParameterInfo::kCanAutomate));
        registerSlot(id, static_cast<double>(defaultValue - low) / steps, steps);
    }
    void addList(ParamID id, const char *title, std::initializer_list<const char *> items, int defaultItem)
    {
        UString128 t(title);
        auto *list = new StringListParameter(t, id, nullptr, ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
        for(const char *item : items) {
            UString128 text(item);
            list->appendString(text);
        }
        const int steps = static_cast<int>(items.size()) - 1;
        const double defaultNorm = steps > 0 ? static_cast<double>(defaultItem) / steps : 0.0;
        list->getInfo().defaultNormalizedValue = defaultNorm;
        list->setNormalized(defaultNorm);
        parameters.addParameter(list);
        registerSlot(id, defaultNorm, steps);
    }

    double norm(ParamID id) const { return norm_[slotOf(id)].load(std::memory_order_relaxed); }
    int step(ParamID id) const
    {
        const int steps = steps_[slotOf(id)];
        return std::min(steps, static_cast<int>(norm(id) * (steps + 1)));
    }
    int stepValue(ParamID id, int low) const { return low + step(id); }
    double bi(ParamID id, double range) const { return bipolar(norm(id)) * range; }

    void destroyDsp()
    {
        for(auto &voice : voice_) {
            if(voice)
                mla8000_voice_destroy__ptr_struct_Jp8Voice(voice);
            voice = nullptr;
        }
    }

    void set(int v, int32_t index, double value)
    {
        mla8000_voice_set__ptr_struct_Jp8Voice_i32_f32(voice_[v], index, static_cast<float>(value));
    }

    void restart()
    {
        for(int v = 0; v < kVoices; ++v) {
            mla8000_voice_reset__ptr_struct_Jp8Voice(voice_[v]);
            play_[v] = PlayVoice{};
        }
        heldCount_ = 0;
        untilControl_ = 0;
        tail_ = 0;
        chorusL_.clear(); chorusR_.clear(); delayL_.clear(); delayR_.clear();
        bassL_.clear(); bassR_.clear(); trebleL_.clear(); trebleR_.clear();
        delayDampL_ = delayDampR_ = 0.0f;
        delayFrames_ = 0.0;
        pushAll();
    }

    // Every patch setting to every voice (pitches and the cutoff follow at
    // the next control step).
    void pushAll()
    {
        if(voice_[0] == nullptr)
            return;
        for(int v = 0; v < kVoices; ++v) {
            set(v, kDspWave1, step(kOsc1WaveParam));
            set(v, kDspCtrl1, norm(kOsc1Ctrl1Param));
            set(v, kDspCtrl2, norm(kOsc1Ctrl2Param));
            set(v, kDspWave2, step(kOsc2WaveParam));
            set(v, kDspWidth2, 0.5 - 0.47 * bipolar(norm(kOsc2WidthParam)));
            set(v, kDspSync, step(kSyncParam));
            set(v, kDspRing, step(kRingParam));
            set(v, kDspCross, norm(kCrossModParam));
            set(v, kDspBalance, norm(kBalanceParam));
            set(v, kDspPitchDepth, bi(kPitchEnvDepthParam, 63.0));
            set(v, kDspPitchAttack, timeFromNorm(norm(kPitchEnvAttackParam)));
            set(v, kDspPitchDecay, timeFromNorm(norm(kPitchEnvDecayParam)));
            set(v, kDspFilterType, step(kFilterTypeParam));
            set(v, kDspSlope, step(kFilterSlopeParam));
            set(v, kDspResonance, norm(kResonanceParam));
            set(v, kDspEnvDepth, bi(kFilterEnvDepthParam, 8.0));
            set(v, kDspFilterAttack, timeFromNorm(norm(kFilterAttackParam)));
            set(v, kDspFilterDecay, timeFromNorm(norm(kFilterDecayParam)));
            set(v, kDspFilterSustain, norm(kFilterSustainParam));
            set(v, kDspFilterRelease, timeFromNorm(norm(kFilterReleaseParam)));
            set(v, kDspAmpAttack, timeFromNorm(norm(kAmpAttackParam)));
            set(v, kDspAmpDecay, timeFromNorm(norm(kAmpDecayParam)));
            set(v, kDspAmpSustain, norm(kAmpSustainParam));
            set(v, kDspAmpRelease, timeFromNorm(norm(kAmpReleaseParam)));
            set(v, kDspDrive, 1.0 + 3.0 * norm(kDriveParam));
        }
        const double bass = bi(kBassParam, 12.0), treble = bi(kTrebleParam, 12.0);
        bassL_.design(rate_, 200.0, bass, false); bassR_.design(rate_, 200.0, bass, false);
        trebleL_.design(rate_, 4000.0, treble, true); trebleR_.design(rate_, 4000.0, treble, true);
        untilControl_ = 0;
    }

    void apply(const Item &item)
    {
        if(item.kind == Item::kParam) {
            const int slot = slotOf(item.id);
            if(slot < 0 || !registered_[slot])
                return;
            const int before = step(item.id);
            norm_[slot].store(std::clamp(item.value, 0.0, 1.0), std::memory_order_relaxed);
            if(item.id == kKeyModeParam && step(item.id) != before)
                allNotesOff();
            pushAll();
        } else if(item.kind == Item::kNoteOn) {
            noteOn(item.pitch, item.velocity);
        } else {
            noteOff(item.pitch);
        }
    }

    // --- Notes -------------------------------------------------------------
    KeyMode mode() const { return static_cast<KeyMode>(step(kKeyModeParam)); }

    bool anyActive() const
    {
        for(int v = 0; v < kVoices; ++v)
            if(play_[v].gate || mla8000_voice_active__ptr_struct_Jp8Voice(voice_[v]))
                return true;
        return false;
    }

    void allNotesOff()
    {
        heldCount_ = 0;
        for(int v = 0; v < kVoices; ++v) {
            play_[v].gate = false;
            mla8000_voice_note_off__ptr_struct_Jp8Voice(voice_[v]);
        }
    }

    void removeHeld(int pitch)
    {
        int to = 0;
        for(int i = 0; i < heldCount_; ++i)
            if(held_[i].pitch != pitch)
                held_[to++] = held_[i];
        heldCount_ = to;
    }

    bool glides() const { return step(kPortamentoParam) != 0 && timeFromNorm(norm(kPortaTimeParam)) > 0.0; }

    // Start voice `v` on `pitch`; `retrigger` restarts its envelopes.
    void play(int v, int pitch, float velocity, bool retrigger, double detune, double pan)
    {
        PlayVoice &voice = play_[v];
        const double from = mode() == kPoly ? lastNote_ : (voice.note >= 0 ? voice.pitch : lastNote_);
        voice.from = glides() && from >= 0.0 ? from : pitch;
        voice.pitch = voice.from;
        voice.note = pitch;
        voice.detune = detune;
        voice.pan = pan;
        if(retrigger) {
            voice.velocity = velocity;
            voice.gate = true;
            voice.age = ++clock_;
            voice.sinceOn = 0.0;
            set(v, kDspPan, pan);
            control(v, 0.0);
            mla8000_voice_note_on__ptr_struct_Jp8Voice_i32(voice_[v], 1);
        }
    }

    void release(int v)
    {
        play_[v].gate = false;
        play_[v].age = ++clock_;
        mla8000_voice_note_off__ptr_struct_Jp8Voice(voice_[v]);
    }

    void noteOn(int pitch, float velocity)
    {
        removeHeld(pitch);
        const bool wasEmpty = heldCount_ == 0;
        if(heldCount_ < kMaxHeld)
            held_[heldCount_++] = Held{static_cast<int16>(pitch), velocity};
        switch(mode()) {
            case kPoly: {
                // A released voice (the quietest first), else the oldest.
                int best = -1;
                float quietest = 2.0f;
                for(int v = 0; v < kVoices; ++v) {
                    if(play_[v].gate)
                        continue;
                    const float level = mla8000_voice_level__ptr_struct_Jp8Voice(voice_[v]);
                    if(level < quietest) { quietest = level; best = v; }
                }
                if(best < 0) {
                    best = 0;
                    for(int v = 1; v < kVoices; ++v)
                        if(play_[v].age < play_[best].age)
                            best = v;
                }
                play(best, pitch, velocity, true, 0.0, 0.0);
                break;
            }
            case kMono:
            case kLegato:
                play(0, pitch, velocity, mode() == kMono || wasEmpty, 0.0, 0.0);
                break;
            case kUnison: {
                const double spread = norm(kUnisonDetuneParam) * 0.5; // semitones either side
                for(int v = 0; v < kVoices; ++v) {
                    const double place = kVoices > 1 ? 2.0 * v / (kVoices - 1) - 1.0 : 0.0;
                    play(v, pitch, velocity, true, place * spread, place * 0.8);
                }
                break;
            }
        }
        lastNote_ = pitch;
    }

    void noteOff(int pitch)
    {
        removeHeld(pitch);
        switch(mode()) {
            case kPoly:
                for(int v = 0; v < kVoices; ++v)
                    if(play_[v].gate && play_[v].note == pitch)
                        release(v);
                break;
            case kMono:
            case kLegato:
            case kUnison: {
                const int count = mode() == kUnison ? kVoices : 1;
                if(heldCount_ == 0) {
                    for(int v = 0; v < count; ++v)
                        if(play_[v].gate)
                            release(v);
                } else if(play_[0].note == pitch) {
                    // Back to the newest key still held, without retriggering.
                    const Held &top = held_[heldCount_ - 1];
                    for(int v = 0; v < count; ++v)
                        play(v, top.pitch, top.velocity, false, play_[v].detune, play_[v].pan);
                    lastNote_ = top.pitch;
                }
                break;
            }
        }
    }

    // --- Control rate ---------------------------------------------------------
    double lfoShape(double phase)
    {
        switch(step(kLfo1WaveParam)) {
            case 1: return 1.0 - 2.0 * phase;                  // saw down
            case 2: return phase < 0.5 ? 1.0 : -1.0;          // square
            case 3: return lfo1Held_;                         // sample and hold
            default: return 1.0 - 4.0 * std::fabs(phase - 0.5); // triangle
        }
    }

    double nextRandom()
    {
        random_ = random_ * 1664525u + 1013904223u;
        return static_cast<double>(random_ >> 8) / 8388608.0 - 1.0;
    }

    // Pitches, cutoff, level and LFO of voice `v` after `dt` seconds.
    void control(int v, double dt)
    {
        PlayVoice &voice = play_[v];
        if(voice.note < 0)
            return;
        const double porta = timeFromNorm(norm(kPortaTimeParam));
        if(glides() && porta > 0.0) {
            // A constant-time glide, like the JP-8000's.
            const double distance = voice.note - voice.from;
            const double move = distance * dt / porta;
            if(std::fabs(voice.note - voice.pitch) <= std::fabs(move)) voice.pitch = voice.note;
            else voice.pitch += move;
        } else {
            voice.pitch = voice.note;
        }
        voice.sinceOn += dt;
        const double fade = timeFromNorm(norm(kLfo1FadeParam));
        const double lfo1 = lfoShape(lfo1Phase_) * (fade > 0.0 ? std::min(1.0, voice.sinceOn / fade) : 1.0);
        const double lfo2 = 1.0 - 4.0 * std::fabs(lfo2Phase_ - 0.5);
        const double wheel = norm(kModWheelParam);
        const double bend = (norm(kPitchBendParam) * 16383.0 - 8192.0) / 8192.0 * step(kBendRangeParam);
        const double vibrato = lfo1 * (norm(kLfo1PitchParam) * 12.0 + wheel * norm(kWheelPitchParam) * 2.0) +
                               lfo2 * bi(kLfo2PitchParam, 1.0);
        const double semis = voice.pitch + voice.detune + bend + vibrato;
        const double hz1 = 440.0 * std::exp2((semis - 69.0) / 12.0);
        const double range = stepValue(kOsc2RangeParam, -24) + bi(kOsc2FineParam, 50.0) / 100.0;
        const double velocity = std::clamp(static_cast<double>(voice.velocity), 0.0, 1.0);
        const double octaves = norm(kKeyFollowParam) * (voice.pitch - 60.0) / 12.0 + lfo1 * bi(kLfo1FilterParam, 4.0) +
                               lfo2 * bi(kLfo2FilterParam, 4.0) + wheel * bi(kWheelCutoffParam, 8.0) -
                               norm(kVelocityFilterParam) * 4.0 * (1.0 - velocity);
        const double cutoff = std::clamp(cutoffFromNorm(norm(kCutoffParam)) * std::exp2(octaves), 10.0, 22000.0);
        double level = norm(kLevelParam) * (1.0 - norm(kVelocityAmpParam) * (1.0 - velocity));
        level *= 1.0 + 0.5 * lfo1 * norm(kLfo1AmpParam) + 0.5 * lfo2 * norm(kLfo2AmpParam);
        // A voice at half of full scale leaves room for chords; Unison's
        // eight voices sound at the level of about two.
        level *= kVoiceGain;
        if(mode() == kUnison)
            level *= 0.35;
        set(v, kDspHz1, hz1);
        set(v, kDspHz2, hz1 * std::exp2(range / 12.0));
        set(v, kDspCutoff, cutoff);
        set(v, kDspLevel, level);
        set(v, kDspLfo, lfo1);
    }

    void controlTick()
    {
        const double dt = kControlFrames / rate_;
        lfo1Phase_ += lfoRateFromNorm(norm(kLfo1RateParam)) * dt;
        if(lfo1Phase_ >= 1.0) {
            lfo1Phase_ -= std::floor(lfo1Phase_);
            lfo1Held_ = nextRandom();
        }
        lfo2Phase_ += lfoRateFromNorm(norm(kLfo2RateParam)) * dt;
        lfo2Phase_ -= std::floor(lfo2Phase_);
        for(int v = 0; v < kVoices; ++v)
            if(play_[v].gate || mla8000_voice_active__ptr_struct_Jp8Voice(voice_[v]))
                control(v, dt);
        // The delay time: synced to the host's tempo, else the Time knob.
        static const double kBeats[] = {0.0, 0.25, 0.5, 0.75, 1.0, 1.5, 2.0};
        const int sync = step(kDelaySyncParam);
        const double ms = sync > 0 && tempo_ > 0.0 ? kBeats[sync] * 60000.0 / tempo_ : delayMsFromNorm(norm(kDelayTimeParam));
        delayTarget_ = std::clamp(ms * rate_ / 1000.0, 1.0, rate_ * 2.5);
        if(delayFrames_ <= 0.0)
            delayFrames_ = delayTarget_;
    }

    // --- Rendering --------------------------------------------------------
    void effects(float &left, float &right)
    {
        left = trebleL_.run(bassL_.run(left));
        right = trebleR_.run(bassR_.run(right));

        const int chorus = step(kChorusTypeParam);
        if(chorus != kChorusOff) {
            chorusL_.push(left);
            chorusR_.push(right);
            const double rate = chorusRateFromNorm(norm(kChorusRateParam)) * (chorus == kChorus2 ? 2.0 : 1.0);
            chorusPhase_ += rate / rate_;
            chorusPhase_ -= std::floor(chorusPhase_);
            const double depth = norm(kChorusDepthParam);
            const bool flanger = chorus == kFlanger;
            const double base = (flanger ? 0.0015 : 0.007) * rate_;
            const double swing = (flanger ? 0.0012 : 0.004) * depth * rate_;
            const auto tap = [&](const Line &line, double offset) {
                return line.read(base + swing * (0.5 + 0.5 * std::sin(2.0 * M_PI * (chorusPhase_ + offset))));
            };
            float wetL = 0.0f, wetR = 0.0f;
            if(chorus == kSuperChorus) {
                // Three taps a side, a third of a cycle apart.
                wetL = (tap(chorusL_, 0.0) + tap(chorusL_, 1.0 / 3.0) + tap(chorusL_, 2.0 / 3.0)) / 3.0f;
                wetR = (tap(chorusR_, 0.5) + tap(chorusR_, 5.0 / 6.0) + tap(chorusR_, 1.0 / 6.0)) / 3.0f;
            } else {
                wetL = tap(chorusL_, 0.0);
                wetR = tap(chorusR_, 0.25);
            }
            if(flanger) {
                // Feed the flanger back for its comb.
                chorusL_.data[static_cast<size_t>((chorusL_.at - 1 + chorusL_.data.size()) % chorusL_.data.size())] += 0.5f * wetL;
                chorusR_.data[static_cast<size_t>((chorusR_.at - 1 + chorusR_.data.size()) % chorusR_.data.size())] += 0.5f * wetR;
            }
            const float mix = static_cast<float>(norm(kChorusLevelParam));
            left = left * (1.0f - 0.5f * mix) + wetL * mix;
            right = right * (1.0f - 0.5f * mix) + wetR * mix;
        }

        const int delay = step(kDelayTypeParam);
        if(delay != kDelayOff) {
            delayFrames_ += (delayTarget_ - delayFrames_) * 0.0005;
            const float feedback = static_cast<float>(norm(kDelayFeedbackParam) * 0.95);
            float echoL = delayL_.read(delayFrames_);
            float echoR = delayR_.read(delayFrames_);
            // A gentle low-pass in the feedback darkens each repeat.
            delayDampL_ += 0.45f * (echoL - delayDampL_);
            delayDampR_ += 0.45f * (echoR - delayDampR_);
            // The repeats saturate, so full feedback holds rather than grows.
            if(delay == kDelayPanning) {
                // Ping-pong: the mono input enters on the left, repeats cross.
                delayL_.push(std::tanh(0.5f * (left + right) + feedback * delayDampR_));
                delayR_.push(std::tanh(feedback * delayDampL_));
            } else {
                delayL_.push(std::tanh(left + feedback * delayDampL_));
                delayR_.push(std::tanh(right + feedback * delayDampR_));
            }
            const float level = static_cast<float>(norm(kDelayLevelParam));
            left += echoL * level;
            right += echoR * level;
        }
    }

    void render(float *outL, float *outR, int32 from, int32 to)
    {
        const double output = gainFromNorm(norm(kOutputParam));
        const bool fx = step(kChorusTypeParam) != kChorusOff || step(kDelayTypeParam) != kDelayOff;
        for(int32 i = from; i < to; ++i) {
            if(untilControl_ <= 0) {
                controlTick();
                untilControl_ = kControlFrames;
            }
            --untilControl_;
            float left = 0.0f, right = 0.0f;
            bool sounding = false;
            for(int v = 0; v < kVoices; ++v) {
                if(!play_[v].gate && !mla8000_voice_active__ptr_struct_Jp8Voice(voice_[v]))
                    continue;
                float r = 0.0f;
                left += mla8000_voice_process__ptr_struct_Jp8Voice_ptr_f32(voice_[v], &r);
                right += r;
                sounding = true;
            }
            if(sounding)
                tail_ = fx ? static_cast<int>(rate_ * 8.0) : static_cast<int>(rate_ * 0.05);
            else if(tail_ > 0)
                --tail_;
            effects(left, right);
            outL[i] = static_cast<float>(left * output);
            outR[i] = static_cast<float>(right * output);
        }
    }
};

} // namespace mla_8000

BEGIN_FACTORY_DEF("MLang", "https://github.com/mattilaa/mlang", "mailto:devnull@example.invalid")
DEF_CLASS2(INLINE_UID_FROM_FUID(mla_8000::kProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass, "Mla 8000",
           0, PlugType::kInstrumentSynth, "0.1.0", kVstVersionString, mla_8000::Processor::createInstance)
END_FACTORY
