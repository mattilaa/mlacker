// Mla SID - VST3 instrument in the style of the Commodore 64's MOS 6581/8580
// SID sound chip.
//
// The chip itself is MLang: `src/mla_sid_dsp.mla` emulates the SID and is
// driven only by register writes. This file is the "player routine" a C64
// music program would be: it turns MIDI notes into FREQ and GATE writes
// (poly, unison, arpeggio or one voice per MIDI channel), adds glide,
// vibrato, pulse-width sweeps and filter sweeps on every video frame, and
// maps VST3 parameters and MIDI CCs onto the SID's registers. CC 20..44 write
// registers $D400..$D418 directly.

#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "base/source/fstreamer.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>

// --- MLang DSP bridge (compiled from src/mla_sid_dsp.mla) --------------------
struct Sid;
extern "C" Sid *mlasid_create__f32(float sampleRate);
extern "C" void mlasid_destroy__ptr_struct_Sid(Sid *dsp);
extern "C" void mlasid_reset__ptr_struct_Sid(Sid *dsp);
extern "C" void mlasid_settle__ptr_struct_Sid(Sid *dsp);
extern "C" void mlasid_poke__ptr_struct_Sid_i32_i32(Sid *dsp, int32_t reg, int32_t value);
extern "C" int32_t mlasid_peek__ptr_struct_Sid_i32(Sid *dsp, int32_t reg);
extern "C" int32_t mlasid_active__ptr_struct_Sid(Sid *dsp);
extern "C" void mlasid_set__ptr_struct_Sid_i32_f32(Sid *dsp, int32_t index, float value);
extern "C" float mlasid_process__ptr_struct_Sid_ptr_f32(Sid *dsp, float *outRight);

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace mla_sid {

// Stable class id. Distinct from the other Mla plug-ins.
static const FUID kProcessorUID(0x4D6C6153, 0x49443634, 0x38313835, 0x38305C64);

constexpr uint32 kStateMagic = 0x4449534D; // "MSID" little-endian
constexpr uint32 kStateVersion = 1;

// Parameter IDs are stable: new parameters are appended.
enum ParamId : ParamID {
    kOutputParam = 100,
    kChipParam,
    kClockParam,
    kPlayModeParam,
    kLinkParam,
    kGlideParam,
    kBendRangeParam,
    kPitchBendParam,
    kVibratoParam,
    kVibratoRateParam,
    kVibratoDelayParam,
    kPwSweepParam,
    kPwSweepRateParam,
    kFilterEnvParam,
    kFilterDecayParam,
    kV3ModParam,
    kV3ModAmountParam,
    kArpSpeedParam,
    kPlayerSpeedParam,
    kHardRestartParam,
    kVelocityParam,
    kCutoffParam,
    kResonanceParam,
    kFilterModeParam,
    kVolumeParam,
    kVoice3OffParam,
    kLastGlobalParam = kVoice3OffParam,
    // Voice v (0..2): kVoiceParamBase + kVoiceStride * v + VoiceField.
    kVoiceParamBase = 200,
    // SID register r ($D400 + r, 0..24): kRegisterParamBase + r. MIDI CC
    // 20 + r drives it.
    kRegisterParamBase = 300,
};

enum VoiceField : int {
    kWaveField = 0,
    kPulseWidthField,
    kAttackField,
    kDecayField,
    kSustainField,
    kReleaseField,
    kSyncField,
    kRingField,
    kTestField,
    kFilterField,
    kKeysField,
    kTransposeField,
    kDetuneField,
    kFreqField,
    kVoiceFields,
};
constexpr int kVoiceStride = 20;
constexpr int kVoices = 3;
constexpr int kRegisters = 25;
constexpr int kFirstRegisterCc = 20;

constexpr ParamID voiceParam(int voice, int field)
{
    return static_cast<ParamID>(kVoiceParamBase + kVoiceStride * voice + field);
}

// Parameter storage covers IDs 100 .. 324; unused IDs stay unregistered.
constexpr ParamID kFirstSlot = 100;
constexpr int kSlots = kRegisterParamBase + kRegisters - kFirstSlot;

static int slotOf(ParamID id)
{
    return id >= kFirstSlot && id < kFirstSlot + kSlots ? static_cast<int>(id - kFirstSlot) : -1;
}

// DSP settings (see mlasid_set in the .mla file).
enum DspIndex : int32_t { kDspOutput = 0, kDspModel, kDspClock, kDspHardRestart, kDspLevel };

enum PlayMode : int { kPoly = 0, kUnison, kArp, kChannels };
enum V3Mod : int { kV3Off = 0, kV3Osc, kV3Env };

constexpr double kPalClock = 985248.0, kNtscClock = 1022727.0;
// Video frames per second: cycles per frame are 63 x 312 (PAL), 65 x 263 (NTSC).
constexpr double kPalFrameHz = kPalClock / 19656.0, kNtscFrameHz = kNtscClock / 17095.0;

static const char *const kRegisterNames[kRegisters] = {
    "V1 Freq Lo", "V1 Freq Hi", "V1 PW Lo", "V1 PW Hi", "V1 Control", "V1 Attack/Decay", "V1 Sustain/Release",
    "V2 Freq Lo", "V2 Freq Hi", "V2 PW Lo", "V2 PW Hi", "V2 Control", "V2 Attack/Decay", "V2 Sustain/Release",
    "V3 Freq Lo", "V3 Freq Hi", "V3 PW Lo", "V3 PW Hi", "V3 Control", "V3 Attack/Decay", "V3 Sustain/Release",
    "FC Lo", "FC Hi", "Res/Filt", "Mode/Vol"};

// The SID's envelope times for each nibble, in milliseconds: attack, and
// decay/release (three times longer).
static const int kAttackMs[16] = {2, 8, 16, 24, 38, 56, 68, 80, 100, 250, 500, 800, 1000, 3000, 5000, 8000};
static const int kDecayMs[16] = {6,   24,  48,   72,   114,  168,  204,  240,
                                 300, 750, 1500, 2400, 3000, 9000, 15000, 24000};

// --- Normalized -> physical mappings ----------------------------------------
constexpr double kUnityLevelNorm = 60.0 / 66.0;
static double gainFromNorm(double norm)
{
    // -60 dB .. +6 dB; the bottom of the range is silence.
    return norm <= 0.0 ? 0.0 : std::pow(10.0, (-60.0 + norm * 66.0) / 20.0);
}
static double glideFromNorm(double n) { return 2.0 * n * n; }                  // 0 .. 2 s
static double vibratoRateFromNorm(double n) { return 0.5 * std::pow(24.0, n); } // 0.5 .. 12 Hz
static double sweepRateFromNorm(double n) { return 0.05 * std::pow(200.0, n); } // 0.05 .. 10 Hz
static double decayFromNorm(double n) { return 0.01 * std::pow(500.0, n); }     // 0.01 .. 5 s
static double bipolar(double n) { return n * 2.0 - 1.0; }
static double detuneFromNorm(double n) { return n * 100.0 - 50.0; } // cents
// Keys-off voice frequency: 0 (stopped), then 0.05 Hz .. ~3.9 kHz.
static double voiceHzFromNorm(double n) { return n <= 0.0 ? 0.0 : 0.05 * std::exp2(n * 16.3); }
static double normFor(double value, double (*map)(double))
{
    // Invert a monotonic mapping by bisection (defaults only).
    double lo = 0.0, hi = 1.0;
    for(int i = 0; i < 60; ++i) {
        const double mid = 0.5 * (lo + hi);
        (map(mid) < value ? lo : hi) = mid;
    }
    return 0.5 * (lo + hi);
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

// --- The player ---------------------------------------------------------------

struct Held {
    int16 pitch;
    int16 channel;
    float velocity;
};

struct PlayVoice {
    int note = -1;       // the note it plays, -1 before the first
    int channel = 0;
    bool gate = false;
    bool retrigger = false; // write GATE 0 then 1 at the next update
    double pitch = 60.0;  // current (gliding) pitch in semitones
    uint64_t age = 0;     // when it last started or stopped
};

class Processor final : public SingleComponentEffect, public IMidiMapping {
  public:
    Processor()
    {
        for(auto &value : norm_)
            value.store(0.0, std::memory_order_relaxed);
        registered_.fill(false);
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

        addMapped(kOutputParam, "Output", "dB", kUnityLevelNorm, [](double n, char *s, size_t z) {
            if(n <= 0.0) std::snprintf(s, z, "-inf");
            else std::snprintf(s, z, "%.1f", -60.0 + n * 66.0);
        });
        addList(kChipParam, "Chip", {"6581", "8580"}, 0);
        addList(kClockParam, "Clock", {"PAL", "NTSC"}, 0);
        addList(kPlayModeParam, "Play Mode", {"Poly", "Unison", "Arp", "Channels"}, kPoly);
        addList(kLinkParam, "Link Voices", {"Off", "On"}, 1);
        addMapped(kGlideParam, "Glide", "s", 0.0,
                  [](double n, char *s, size_t z) { std::snprintf(s, z, "%.3f", glideFromNorm(n)); });
        addRange(kBendRangeParam, "Bend Range", "st", 0, 24, 2);
        // Centre 8192 of the host's 14-bit bend (0 .. 16383).
        addMapped(kPitchBendParam, "Pitch Bend", nullptr, 8192.0 / 16383.0, [](double n, char *s, size_t z) {
            std::snprintf(s, z, "%+.0f", n * 16383.0 - 8192.0);
        });
        addMapped(kVibratoParam, "Vibrato", "st", 0.0,
                  [](double n, char *s, size_t z) { std::snprintf(s, z, "%.2f", n); });
        addMapped(kVibratoRateParam, "Vibrato Rate", "Hz", normFor(6.0, vibratoRateFromNorm),
                  [](double n, char *s, size_t z) { std::snprintf(s, z, "%.2f", vibratoRateFromNorm(n)); });
        addMapped(kVibratoDelayParam, "Vibrato Delay", "s", 0.0,
                  [](double n, char *s, size_t z) { std::snprintf(s, z, "%.2f", 2.0 * n); });
        addMapped(kPwSweepParam, "PW Sweep", "%", 0.0,
                  [](double n, char *s, size_t z) { std::snprintf(s, z, "%.0f", n * 100.0); });
        addMapped(kPwSweepRateParam, "PW Sweep Rate", "Hz", normFor(0.5, sweepRateFromNorm),
                  [](double n, char *s, size_t z) { std::snprintf(s, z, "%.2f", sweepRateFromNorm(n)); });
        addMapped(kFilterEnvParam, "Filter Env", "%", 0.5,
                  [](double n, char *s, size_t z) { std::snprintf(s, z, "%+.0f", bipolar(n) * 100.0); });
        addMapped(kFilterDecayParam, "Filter Decay", "s", normFor(0.3, decayFromNorm),
                  [](double n, char *s, size_t z) { std::snprintf(s, z, "%.3f", decayFromNorm(n)); });
        addList(kV3ModParam, "V3 Mod", {"Off", "OSC3", "ENV3"}, kV3Off);
        addMapped(kV3ModAmountParam, "V3 Mod Amount", "%", 0.5,
                  [](double n, char *s, size_t z) { std::snprintf(s, z, "%+.0f", bipolar(n) * 100.0); });
        addRange(kArpSpeedParam, "Arp Speed", "frames", 1, 16, 2);
        addList(kPlayerSpeedParam, "Player Speed", {"1x", "2x", "4x", "8x"}, 0);
        addList(kHardRestartParam, "Hard Restart", {"Off", "On"}, 1);
        addMapped(kVelocityParam, "Velocity", "%", 0.0,
                  [](double n, char *s, size_t z) { std::snprintf(s, z, "%.0f", n * 100.0); });
        addRange(kCutoffParam, "Cutoff", nullptr, 0, 2047, 1400);
        addRange(kResonanceParam, "Resonance", nullptr, 0, 15, 0);
        addList(kFilterModeParam, "Filter Mode", {"Off", "LP", "BP", "LP+BP", "HP", "LP+HP", "BP+HP", "LP+BP+HP"}, 1);
        addRange(kVolumeParam, "Volume", nullptr, 0, 15, 15);
        addList(kVoice3OffParam, "3 Off", {"Off", "On"}, 0);

        for(int v = 0; v < kVoices; ++v) {
            char prefix[8];
            std::snprintf(prefix, sizeof(prefix), "V%d ", v + 1);
            auto title = [&](const char *name) {
                static thread_local char text[64];
                std::snprintf(text, sizeof(text), "%s%s", prefix, name);
                return text;
            };
            addList(voiceParam(v, kWaveField), title("Waveform"),
                    {"Off", "Tri", "Saw", "Tri+Saw", "Pulse", "Tri+Pulse", "Saw+Pulse", "Tri+Saw+Pulse", "Noise",
                     "Noise+Tri", "Noise+Saw", "Noise+Tri+Saw", "Noise+Pulse", "Noise+Tri+Pulse", "Noise+Saw+Pulse",
                     "All"},
                    4);
            addRange(voiceParam(v, kPulseWidthField), title("Pulse Width"), nullptr, 0, 4095, 2048);
            addEnvelope(voiceParam(v, kAttackField), title("Attack"), kAttackMs, 0);
            addEnvelope(voiceParam(v, kDecayField), title("Decay"), kDecayMs, 9);
            addRange(voiceParam(v, kSustainField), title("Sustain"), nullptr, 0, 15, 10);
            addEnvelope(voiceParam(v, kReleaseField), title("Release"), kDecayMs, 8);
            addList(voiceParam(v, kSyncField), title("Sync"), {"Off", "On"}, 0);
            addList(voiceParam(v, kRingField), title("Ring"), {"Off", "On"}, 0);
            addList(voiceParam(v, kTestField), title("Test"), {"Off", "On"}, 0);
            addList(voiceParam(v, kFilterField), title("Filter"), {"Off", "On"}, 1);
            addList(voiceParam(v, kKeysField), title("Keys"), {"Off", "On"}, 1);
            addRange(voiceParam(v, kTransposeField), title("Transpose"), "st", -24, 24, 0);
            addMapped(voiceParam(v, kDetuneField), title("Detune"), "ct", 0.5,
                      [](double n, char *s, size_t z) { std::snprintf(s, z, "%+.1f", detuneFromNorm(n)); });
            addMapped(voiceParam(v, kFreqField), title("Freq"), "Hz", normFor(440.0, voiceHzFromNorm),
                      [](double n, char *s, size_t z) { std::snprintf(s, z, "%.2f", voiceHzFromNorm(n)); });
        }

        // Raw register writes, one per SID register (see applyRegister).
        for(int r = 0; r < kRegisters; ++r) {
            char text[64];
            std::snprintf(text, sizeof(text), "$D4%02X %s", r, kRegisterNames[r]);
            UString128 title(text);
            const ParamID id = static_cast<ParamID>(kRegisterParamBase + r);
            parameters.addParameter(new RangeParameter(title, id, nullptr, 0, 127, 0, 127,
                                                       ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden));
            registerSlot(id, 0.0, 127);
        }
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
        if(cc >= kFirstRegisterCc && cc < kFirstRegisterCc + kRegisters) {
            id = static_cast<ParamID>(kRegisterParamBase + cc - kFirstRegisterCc);
            return kResultOk;
        }
        // The usual knobs of a MIDI keyboard (GM2 sound controllers).
        switch(cc) {
            case kCtrlModWheel: id = kVibratoParam; return kResultOk;
            case kCtrlPortaTime: id = kGlideParam; return kResultOk;
            case kCtrlVolume: id = kOutputParam; return kResultOk;
            case 70: id = voiceParam(0, kWaveField); return kResultOk;       // Sound variation
            case 71: id = kResonanceParam; return kResultOk;                 // Timbre / resonance
            case 72: id = voiceParam(0, kReleaseField); return kResultOk;    // Release time
            case 73: id = voiceParam(0, kAttackField); return kResultOk;     // Attack time
            case 74: id = kCutoffParam; return kResultOk;                    // Brightness / cutoff
            case 75: id = voiceParam(0, kDecayField); return kResultOk;      // Decay time
            case 76: id = kVibratoRateParam; return kResultOk;               // Vibrato rate
            case 77: id = voiceParam(0, kPulseWidthField); return kResultOk; // Sound controller 8
            case 78: id = kVibratoDelayParam; return kResultOk;              // Vibrato delay
            case 79: id = voiceParam(0, kSustainField); return kResultOk;    // Sound controller 10
            case 80: id = kFilterModeParam; return kResultOk;
            case 81: id = kPwSweepParam; return kResultOk;
            case 82: id = kFilterEnvParam; return kResultOk;
            case 83: id = kPlayModeParam; return kResultOk;
            case kPitchBend: id = kPitchBendParam; return kResultOk;
            default: return kResultFalse;
        }
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
        sampleRate_ = setup.sampleRate > 0 ? setup.sampleRate : 44100.0;
        dsp_ = mlasid_create__f32(static_cast<float>(sampleRate_));
        restart();
        return kResultOk;
    }

    tresult PLUGIN_API setActive(TBool state) SMTG_OVERRIDE
    {
        if(dsp_) {
            mlasid_reset__ptr_struct_Sid(dsp_);
            restart();
        }
        return SingleComponentEffect::setActive(state);
    }

    tresult PLUGIN_API process(ProcessData &data) SMTG_OVERRIDE
    {
        if(dsp_ == nullptr || data.symbolicSampleSize != kSample32)
            return kResultFalse;
        const bool pushed = paramsDirty_.exchange(false, std::memory_order_acquire);
        if(pushed)
            pushAll();

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
                    item.channel = event.noteOn.channel;
                    item.velocity = event.noteOn.velocity;
                } else if(event.type == Event::kNoteOffEvent) {
                    item.kind = Item::kNoteOff;
                    item.pitch = event.noteOff.pitch;
                    item.channel = event.noteOff.channel;
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

        // Silent and nothing to do: exact zeros.
        if(count == 0 && !pushed && !anyGate() && mlasid_active__ptr_struct_Sid(dsp_) == 0) {
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
        int16 channel = 0;
        float velocity = 0;
        int order = 0;
    };
    static constexpr int kMaxItems = 4096;
    static constexpr int kMaxHeld = 128;

    Sid *dsp_ = nullptr;
    double sampleRate_ = 44100.0;
    std::atomic<double> norm_[kSlots];
    std::array<bool, kSlots> registered_{};
    std::array<int, kSlots> steps_{};
    std::atomic<bool> paramsDirty_{false};
    std::array<Item, kMaxItems> items_{};

    // --- Player state (audio thread) -------------------------------------
    // The patch's registers, as the parameters and register CCs wrote them;
    // the player adds notes and modulation on top when it writes the chip.
    std::array<uint8_t, kRegisters> shadow_{};
    std::array<int, kRegisters> written_{};
    std::array<PlayVoice, kVoices> voices_{};
    std::array<Held, kMaxHeld> held_{};
    int heldCount_ = 0;
    uint64_t clock_ = 0;
    double untilFrame_ = 0.0;
    double vibratoPhase_ = 0.0;
    double sweepPhase_ = 0.0;
    double sinceTrigger_ = 0.0;
    double filterEnv_ = 0.0;
    int arpStep_ = 0;
    int arpCount_ = 0;
    int v3Value_ = 0;
    int layout_ = -1;

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

    void addRange(ParamID id, const char *title, const char *units, int low, int high, int defaultValue)
    {
        UString128 t(title), u(units ? units : "");
        const int steps = high - low;
        parameters.addParameter(
            new RangeParameter(t, id, u, low, high, defaultValue, steps, ParameterInfo::kCanAutomate));
        registerSlot(id, static_cast<double>(defaultValue - low) / steps, steps);
    }

    // An envelope nibble whose display is the SID's time for it.
    void addEnvelope(ParamID id, const char *title, const int *times, int defaultValue)
    {
        UString128 t(title);
        auto *list = new StringListParameter(t, id, STR16("ms"),
                                             ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
        for(int i = 0; i < 16; ++i) {
            char text[16];
            std::snprintf(text, sizeof(text), "%d", times[i]);
            UString128 item(text);
            list->appendString(item);
        }
        const double defaultNorm = defaultValue / 15.0;
        list->getInfo().defaultNormalizedValue = defaultNorm;
        list->setNormalized(defaultNorm);
        parameters.addParameter(list);
        registerSlot(id, defaultNorm, 15);
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

    // A stepped parameter's step, as VST3 converts normalized values.
    int step(ParamID id) const
    {
        const int steps = steps_[slotOf(id)];
        return std::min(steps, static_cast<int>(norm(id) * (steps + 1)));
    }

    bool on(ParamID id) const { return step(id) != 0; }
    int voiceStep(int v, int field) const { return step(voiceParam(v, field)); }
    double voiceNorm(int v, int field) const { return norm(voiceParam(v, field)); }

    void destroyDsp()
    {
        if(dsp_)
            mlasid_destroy__ptr_struct_Sid(dsp_);
        dsp_ = nullptr;
    }

    void set(int32_t index, double value) { mlasid_set__ptr_struct_Sid_i32_f32(dsp_, index, static_cast<float>(value)); }

    // A fresh chip (or one just reset): write every register again.
    void restart()
    {
        written_.fill(-1);
        for(auto &voice : voices_)
            voice = PlayVoice{};
        heldCount_ = 0;
        untilFrame_ = 0.0;
        filterEnv_ = 0.0;
        pushAll();
        if(dsp_)
            mlasid_settle__ptr_struct_Sid(dsp_);
    }

    // --- Parameters -> chip ----------------------------------------------
    void setBits(int reg, int mask, int value) { shadow_[reg] = static_cast<uint8_t>((shadow_[reg] & ~mask) | (value & mask)); }

    // Send one parameter to the shadow registers or the DSP; `refresh`
    // writes the chip afterwards.
    void applyParam(ParamID id, bool refresh = true)
    {
        if(dsp_ == nullptr)
            return;
        if(id >= kRegisterParamBase && id < kRegisterParamBase + kRegisters) {
            applyRegister(static_cast<int>(id - kRegisterParamBase), step(id));
        } else if(id >= kVoiceParamBase && id < kVoiceParamBase + kVoiceStride * kVoices) {
            const int v = static_cast<int>(id - kVoiceParamBase) / kVoiceStride;
            const int field = static_cast<int>(id - kVoiceParamBase) % kVoiceStride;
            const int base = 7 * v;
            const int value = field < kDetuneField ? voiceStep(v, field) : 0;
            switch(field) {
                case kWaveField: setBits(base + 4, 0xF0, value << 4); break;
                case kSyncField: setBits(base + 4, 0x02, value << 1); break;
                case kRingField: setBits(base + 4, 0x04, value << 2); break;
                case kTestField: setBits(base + 4, 0x08, value << 3); break;
                case kPulseWidthField:
                    shadow_[base + 2] = static_cast<uint8_t>(value & 0xFF);
                    shadow_[base + 3] = static_cast<uint8_t>(value >> 8);
                    break;
                case kAttackField: setBits(base + 5, 0xF0, value << 4); break;
                case kDecayField: setBits(base + 5, 0x0F, value); break;
                case kSustainField: setBits(base + 6, 0xF0, value << 4); break;
                case kReleaseField: setBits(base + 6, 0x0F, value); break;
                case kFilterField: setBits(23, 1 << v, value << v); break;
                case kKeysField: notesOffOnChange(); break;
                case kFreqField: {
                    const int freq = freqRegister(voiceHzFromNorm(voiceNorm(v, kFreqField)));
                    shadow_[base] = static_cast<uint8_t>(freq & 0xFF);
                    shadow_[base + 1] = static_cast<uint8_t>(freq >> 8);
                    break;
                }
                default: break; // Transpose, detune: read when pitches are written.
            }
        } else {
            switch(id) {
                case kOutputParam: set(kDspOutput, gainFromNorm(norm(id))); break;
                case kChipParam: set(kDspModel, step(id)); break;
                case kClockParam:
                    set(kDspClock, step(id));
                    for(int v = 0; v < kVoices; ++v)
                        applyParam(voiceParam(v, kFreqField), false);
                    break;
                case kHardRestartParam: set(kDspHardRestart, step(id)); break;
                case kPlayModeParam: notesOffOnChange(); break;
                case kCutoffParam: {
                    const int fc = step(id);
                    shadow_[21] = static_cast<uint8_t>(fc & 7);
                    shadow_[22] = static_cast<uint8_t>(fc >> 3);
                    break;
                }
                case kResonanceParam: setBits(23, 0xF0, step(id) << 4); break;
                case kFilterModeParam: setBits(24, 0x70, step(id) << 4); break;
                case kVoice3OffParam: setBits(24, 0x80, step(id) << 7); break;
                case kVolumeParam: setBits(24, 0x0F, step(id)); break;
                default: break; // Read by the player when it runs.
            }
        }
        if(refresh)
            writeChip();
    }

    // A register CC: seven bits for an 8-bit register. Mostly the CC is the
    // register's top seven bits ($D400 + r = CC x 2), and the bit that does
    // not fit is filled in so that CC 0 writes $00 and CC 127 the maximum:
    // - FREQ, PW Lo, Attack/Decay, Sustain/Release, FC Hi: bit 0 copies
    //   bit 7 (CC 127 = $FF).
    // - Control: bit 0 is GATE, which the keys own.
    // - Mode/Vol: the volume's bit 0 copies its bit 3 (CC 127 = $FF).
    // - Res/Filt: resonance x 8 + the FILT3..1 routing bits; FILTEX (there is
    //   no external input) stays 0.
    // - PW Hi (4 bits) and FC Lo (3 bits) span the CC: CC / 8 and CC / 16.
    void applyRegister(int reg, int value)
    {
        int byte = (value << 1) | (value >> 6);
        if(reg < 21) {
            const int field = reg % 7;
            if(field == 3)
                byte = value >> 3;
            else if(field == 4)
                byte = value << 1;
        } else if(reg == 21) {
            byte = value >> 4;
        } else if(reg == 23) {
            byte = ((value >> 3) << 4) | (value & 7);
        } else if(reg == 24) {
            byte = (value << 1) | ((value >> 2) & 1);
        }
        shadow_[reg] = static_cast<uint8_t>(byte);
    }

    void pushAll()
    {
        if(dsp_ == nullptr)
            return;
        // Register CCs are writes, not settings: a restored session starts
        // from the patch parameters.
        for(int slot = 0; slot < kSlots; ++slot) {
            const ParamID id = static_cast<ParamID>(kFirstSlot + slot);
            if(registered_[slot] && id < kRegisterParamBase)
                applyParam(id, false);
        }
        updateFrameRate();
        writeChip();
    }

    void apply(const Item &item)
    {
        if(item.kind == Item::kParam) {
            const int slot = slotOf(item.id);
            if(slot < 0 || !registered_[slot])
                return;
            norm_[slot].store(std::clamp(item.value, 0.0, 1.0), std::memory_order_relaxed);
            if(item.id == kClockParam || item.id == kPlayerSpeedParam)
                updateFrameRate();
            applyParam(item.id);
        } else if(item.kind == Item::kNoteOn) {
            noteOn(item.pitch, item.channel, item.velocity);
        } else {
            noteOff(item.pitch, item.channel);
        }
    }

    // --- Notes -------------------------------------------------------------
    PlayMode mode() const { return static_cast<PlayMode>(step(kPlayModeParam)); }
    bool keys(int v) const { return voiceStep(v, kKeysField) != 0; }

    bool anyGate() const
    {
        for(int v = 0; v < kVoices; ++v)
            if(keys(v) && voices_[v].gate)
                return true;
        return false;
    }

    void allNotesOff()
    {
        heldCount_ = 0;
        for(auto &voice : voices_)
            voice.gate = false;
    }

    // Changing the play mode or which voices the keys play lets go of
    // every note; resending the same values does not.
    void notesOffOnChange()
    {
        int layout = mode();
        for(int v = 0; v < kVoices; ++v)
            layout = layout * 2 + (keys(v) ? 1 : 0);
        if(layout != layout_)
            allNotesOff();
        layout_ = layout;
    }

    void removeHeld(int pitch, int channel)
    {
        int to = 0;
        for(int i = 0; i < heldCount_; ++i)
            if(held_[i].pitch != pitch || held_[i].channel != channel)
                held_[to++] = held_[i];
        heldCount_ = to;
    }

    // The newest held note matching a voice's channel in Channels mode, or
    // any channel (`v` < 0); -1 for none.
    int newestHeld(int v) const
    {
        for(int i = heldCount_ - 1; i >= 0; --i)
            if(v < 0 || held_[i].channel % kVoices == v)
                return i;
        return -1;
    }

    // Start voice `v` on `pitch`; `retrigger` restarts its envelope.
    void play(int v, int pitch, int channel, float velocity, bool retrigger)
    {
        PlayVoice &voice = voices_[v];
        // Glide from the voice's last pitch; the very first note just starts.
        if(voice.note < 0 || glideFromNorm(norm(kGlideParam)) <= 0.0)
            voice.pitch = pitch;
        voice.note = pitch;
        voice.channel = channel;
        if(retrigger) {
            voice.retrigger = voice.gate;
            voice.gate = true;
            voice.age = ++clock_;
            const double amount = norm(kVelocityParam);
            set(kDspLevel + v, 1.0 - amount * (1.0 - std::clamp(static_cast<double>(velocity), 0.0, 1.0)));
            sinceTrigger_ = 0.0;
            filterEnv_ = 1.0;
        }
    }

    void release(int v)
    {
        voices_[v].gate = false;
        voices_[v].age = ++clock_;
    }

    void noteOn(int pitch, int channel, float velocity)
    {
        removeHeld(pitch, channel);
        const bool wasEmpty = heldCount_ == 0;
        if(heldCount_ < kMaxHeld)
            held_[heldCount_++] = Held{static_cast<int16>(pitch), static_cast<int16>(channel), velocity};
        switch(mode()) {
            case kPoly: {
                // A released voice (the longest released first), else the
                // oldest sounding one.
                int best = -1;
                for(int v = 0; v < kVoices; ++v) {
                    if(!keys(v))
                        continue;
                    if(best < 0 || (voices_[v].gate != voices_[best].gate ? !voices_[v].gate
                                                                          : voices_[v].age < voices_[best].age))
                        best = v;
                }
                if(best >= 0)
                    play(best, pitch, channel, velocity, true);
                break;
            }
            case kUnison:
                for(int v = 0; v < kVoices; ++v)
                    if(keys(v))
                        play(v, pitch, channel, velocity, wasEmpty);
                break;
            case kArp:
                if(wasEmpty) {
                    arpStep_ = 0;
                    arpCount_ = 0;
                    for(int v = 0; v < kVoices; ++v)
                        if(keys(v))
                            play(v, pitch, channel, velocity, true);
                }
                break;
            case kChannels: {
                const int v = channel % kVoices;
                if(keys(v))
                    play(v, pitch, channel, velocity, !voices_[v].gate);
                break;
            }
        }
        writeChip();
    }

    void noteOff(int pitch, int channel)
    {
        removeHeld(pitch, channel);
        switch(mode()) {
            case kPoly:
                for(int v = 0; v < kVoices; ++v)
                    if(voices_[v].gate && voices_[v].note == pitch && voices_[v].channel == channel)
                        release(v);
                break;
            case kUnison:
            case kArp: {
                const int top = newestHeld(-1);
                for(int v = 0; v < kVoices; ++v) {
                    if(!keys(v))
                        continue;
                    if(top < 0)
                        release(v);
                    else if(mode() == kUnison)
                        play(v, held_[top].pitch, held_[top].channel, held_[top].velocity, false);
                }
                break;
            }
            case kChannels: {
                const int v = channel % kVoices;
                const int top = newestHeld(v);
                if(top < 0)
                    release(v);
                else if(keys(v))
                    play(v, held_[top].pitch, held_[top].channel, held_[top].velocity, false);
                break;
            }
        }
        writeChip();
    }

    // --- The frame tick ------------------------------------------------------
    double frameHz() const
    {
        const double base = step(kClockParam) == 0 ? kPalFrameHz : kNtscFrameHz;
        return base * static_cast<double>(1 << step(kPlayerSpeedParam));
    }

    void updateFrameRate() { untilFrame_ = std::min(untilFrame_, sampleRate_ / frameHz()); }

    static double triangle(double phase)
    {
        const double f = phase - std::floor(phase);
        return 4.0 * std::fabs(f - 0.5) - 1.0;
    }

    void frameTick()
    {
        const double dt = 1.0 / frameHz();
        const double glide = glideFromNorm(norm(kGlideParam));
        const double approach = glide > 0.0 ? 1.0 - std::exp(-3.0 * dt / glide) : 1.0;
        for(auto &voice : voices_) {
            if(voice.note < 0)
                continue;
            voice.pitch += (voice.note - voice.pitch) * approach;
            if(std::fabs(voice.note - voice.pitch) < 0.001)
                voice.pitch = voice.note;
        }
        vibratoPhase_ += vibratoRateFromNorm(norm(kVibratoRateParam)) * dt;
        vibratoPhase_ -= std::floor(vibratoPhase_);
        sweepPhase_ += sweepRateFromNorm(norm(kPwSweepRateParam)) * dt;
        sweepPhase_ -= std::floor(sweepPhase_);
        sinceTrigger_ += dt;
        // Filter Decay is the time to fall to 10 %.
        filterEnv_ *= std::exp(-dt * 2.302585 / decayFromNorm(norm(kFilterDecayParam)));

        if(mode() == kArp && heldCount_ > 0 && ++arpCount_ >= step(kArpSpeedParam) + 1) {
            arpCount_ = 0;
            // The held notes from lowest to highest.
            std::array<int, kMaxHeld> notes{};
            int n = 0;
            for(int i = 0; i < heldCount_; ++i)
                notes[n++] = held_[i].pitch;
            std::sort(notes.begin(), notes.begin() + n);
            n = static_cast<int>(std::unique(notes.begin(), notes.begin() + n) - notes.begin());
            arpStep_ = (arpStep_ + 1) % n;
            for(int v = 0; v < kVoices; ++v)
                if(keys(v) && voices_[v].gate) {
                    voices_[v].note = notes[arpStep_];
                    voices_[v].pitch = notes[arpStep_];
                }
        }

        const int source = step(kV3ModParam);
        v3Value_ = source == kV3Osc ? mlasid_peek__ptr_struct_Sid_i32(dsp_, 27)
                 : source == kV3Env ? mlasid_peek__ptr_struct_Sid_i32(dsp_, 28)
                                    : 0;
        writeChip();
    }

    int freqRegister(double hz) const
    {
        const double clock = step(kClockParam) == 0 ? kPalClock : kNtscClock;
        return static_cast<int>(std::clamp(std::lround(hz * 16777216.0 / clock), 0L, 65535L));
    }

    // --- Writing the chip -------------------------------------------------
    void poke(int reg, int value)
    {
        if(written_[reg] == value)
            return;
        written_[reg] = value;
        mlasid_poke__ptr_struct_Sid_i32_i32(dsp_, reg, value);
    }

    // Write the registers the patch, the notes and the modulation give now.
    void writeChip()
    {
        if(dsp_ == nullptr)
            return;
        const bool link = on(kLinkParam);
        const bool gated = anyGate();
        const double bend = (norm(kPitchBendParam) * 16383.0 - 8192.0) / 8192.0 * step(kBendRangeParam);
        const double delay = 2.0 * norm(kVibratoDelayParam);
        const double fade = delay > 0.0 ? std::min(1.0, sinceTrigger_ / delay) : 1.0;
        const double vibrato = norm(kVibratoParam) * fade * triangle(vibratoPhase_ + 0.25);
        const double sweep = norm(kPwSweepParam) * 2047.0;

        for(int v = 0; v < kVoices; ++v) {
            const int base = 7 * v;
            const int patch = 7 * (link ? 0 : v);
            const PlayVoice &voice = voices_[v];
            int freq = shadow_[base] | (shadow_[base + 1] << 8);
            if(keys(v) && voice.note >= 0) {
                const double semis = voice.pitch + voiceStep(v, kTransposeField) - 24 +
                                     detuneFromNorm(voiceNorm(v, kDetuneField)) / 100.0 + bend + vibrato;
                freq = freqRegister(440.0 * std::exp2((semis - 69.0) / 12.0));
            }
            int pw = shadow_[patch + 2] | ((shadow_[patch + 3] & 15) << 8);
            pw = std::clamp(static_cast<int>(std::lround(pw + sweep * triangle(sweepPhase_ + v / 3.0))), 0, 4095);
            const bool gate = keys(v) ? voice.gate : gated;
            const int control = (shadow_[patch + 4] & 0xFE) | (gate ? 1 : 0);

            poke(base, freq & 0xFF);
            poke(base + 1, freq >> 8);
            poke(base + 2, pw & 0xFF);
            poke(base + 3, pw >> 8);
            poke(base + 5, shadow_[patch + 5]);
            poke(base + 6, shadow_[patch + 6]);
            if(voice.retrigger) {
                // GATE off and on again restarts the envelope's attack.
                voices_[v].retrigger = false;
                poke(base + 4, control & 0xFE);
            }
            poke(base + 4, control);
        }

        int fc = (shadow_[21] & 7) | (shadow_[22] << 3);
        fc += static_cast<int>(std::lround(bipolar(norm(kFilterEnvParam)) * 2047.0 * filterEnv_));
        fc += static_cast<int>(std::lround(bipolar(norm(kV3ModAmountParam)) * 2047.0 * v3Value_ / 255.0));
        fc = std::clamp(fc, 0, 2047);
        int resFilt = shadow_[23];
        if(link)
            resFilt = (resFilt & 0xF8) | ((resFilt & 1) ? 7 : 0);
        poke(21, fc & 7);
        poke(22, fc >> 3);
        poke(23, resFilt);
        poke(24, shadow_[24]);
    }

    void render(float *outL, float *outR, int32 from, int32 to)
    {
        while(from < to) {
            const int32 chunk = std::min<int32>(to - from, std::max<int32>(1, static_cast<int32>(std::ceil(untilFrame_))));
            for(int32 i = from; i < from + chunk; ++i) {
                float right = 0.0f;
                outL[i] = mlasid_process__ptr_struct_Sid_ptr_f32(dsp_, &right);
                outR[i] = right;
            }
            from += chunk;
            untilFrame_ -= chunk;
            if(untilFrame_ <= 0.0) {
                frameTick();
                untilFrame_ += sampleRate_ / frameHz();
            }
        }
    }
};

} // namespace mla_sid

BEGIN_FACTORY_DEF("MLang", "https://github.com/mattilaa/mlang", "mailto:devnull@example.invalid")
DEF_CLASS2(INLINE_UID_FROM_FUID(mla_sid::kProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass,
           "Mla SID", 0, PlugType::kInstrumentSynth, "0.1.0", kVstVersionString,
           mla_sid::Processor::createInstance)
END_FACTORY
