// Mla Sampler - VST3 sampler instrument with looping and multiple outputs.
//
// Sixteen sample slots. A slot in Pad mode plays on one key (Root Key + slot);
// in Zone mode it plays across a key range, pitched from its zone root when
// Key Track is on. A velocity range limits either to notes that hard.
// Overlapping zones layer. Each slot holds one sample with
// its own level, pan, tune, loop (off, forward or bidirectional, between a
// start and an end point) and output bus. A slot uses the instance's amp
// ADSR or its own; note-off releases it, and looping slots keep looping
// through the release. Playback begins at the slot's start point.
//
// Outputs: bus 0 "Main" plus seven auxiliary stereo buses "Out 2".."Out 8".
// A slot sent to an aux bus the host has not activated plays on Main, so the
// plug-in works unchanged in hosts that only use the main output.
//
// Loop settings are ordinary VST3 parameters (loop points as a fraction of
// the sample length), so hosts can read, edit, automate and save them without
// a custom protocol; mlacker's Sampler pane (Shift+S) edits them that way.
//
// The per-sample voice math (playhead, loop wrap/reflection, interpolation,
// envelope, pan) lives in MLang (`src/mla_sampler_dsp.mla` on top of
// `modules/dsp/envelope.mla`). This file owns the sample buffers and the voice
// pool, schedules MIDI sample-accurately, routes voices to buses, maps VST3
// parameters and persists state. Samples arrive through the IConnectionPoint
// messages in `mla_sampler_protocol.h`, like Mla Drum's pads.
//
// Threading follows Mla Drum: slots are immutable `Sample` objects owned by
// the control thread and published through atomics; the audio thread picks up
// changes at block start, and replaced samples are freed only after the audio
// thread has finished two further blocks.

#include "mla_sampler_protocol.h"

#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/base/ustring.h"
#include "base/source/fstreamer.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// --- MLang DSP bridge (compiled from src/mla_sampler_dsp.mla) ----------------
struct SamplerVoice;
extern "C" SamplerVoice *mlasampler_voice_create__f32(float sampleRate);
extern "C" void mlasampler_voice_destroy__ptr_struct_SamplerVoice(SamplerVoice *voice);
extern "C" void mlasampler_voice_set_envelope__ptr_struct_SamplerVoice_f32_f32_f32_f32(
    SamplerVoice *voice, float attack, float decay, float sustain, float release);
extern "C" void mlasampler_voice_start__ptr_struct_SamplerVoice_f64_f64_f32_f32_i32_f64_f64(
    SamplerVoice *voice, double frames, double step, float gain, float pan, int32_t loopMode, double loopStart,
    double loopEnd);
extern "C" void mlasampler_voice_release__ptr_struct_SamplerVoice(SamplerVoice *voice);
extern "C" void mlasampler_voice_stop__ptr_struct_SamplerVoice(SamplerVoice *voice);
extern "C" int32_t mlasampler_voice_is_active__ptr_struct_SamplerVoice(SamplerVoice *voice);
extern "C" double mlasampler_voice_frame__ptr_struct_SamplerVoice(SamplerVoice *voice);
extern "C" double mlasampler_voice_next_frame__ptr_struct_SamplerVoice(SamplerVoice *voice);
extern "C" void mlasampler_voice_set_crossfade__ptr_struct_SamplerVoice_f64(SamplerVoice *voice, double frames);
extern "C" void mlasampler_voice_set_start__ptr_struct_SamplerVoice_f64(SamplerVoice *voice, double frame);
extern "C" double mlasampler_voice_shadow_frame__ptr_struct_SamplerVoice(SamplerVoice *voice);
extern "C" float mlasampler_voice_render__ptr_struct_SamplerVoice_f32_f32_f32_f32_f32_f32_f32_f32_ptr_f32(
    SamplerVoice *voice, float left0, float right0, float left1, float right1, float shadowLeft0,
    float shadowRight0, float shadowLeft1, float shadowRight1, float *outRight);

// --- MLang runtime PCM decoder (libmlang_std.a, std::audio::PcmAudio) --------
struct MlangList {
    int64_t size;
    void *data;
};
extern "C" int64_t __mlang_std_audio_pcm_load(const char *path);
extern "C" int64_t __mlang_std_audio_pcm_file_sample_rate(int64_t handle);
extern "C" int64_t __mlang_std_audio_pcm_file_channels(int64_t handle);
extern "C" int64_t __mlang_std_audio_pcm_file_frame_count(int64_t handle);
extern "C" MlangList __mlang_std_audio_pcm_file_samples(int64_t handle);
extern "C" int32_t __mlang_std_audio_pcm_file_close(int64_t handle);
extern "C" const char *__mlang_std_audio_last_error(void);

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace mla_sampler_plugin {

// Stable class id. Distinct from the other Mla plug-ins and the SDK examples.
static const FUID kProcessorUID(0x4D6C6153, 0x616D706C, 0x9E27C1A4, 0x5B83D06F);

constexpr int kNumSlots = 16;
constexpr int kNumVoices = 32;
constexpr int kNumOutputs = 8; // Main + Out 2..Out 8, all stereo.
constexpr uint32 kStateMagic = 0x4D534C4D; // "MLSM" little-endian
constexpr uint32 kStateVersion = 1;

enum LoopMode : int32_t { kLoopOff = 0, kLoopForward = 1, kLoopBidirectional = 2 };

enum ParamId : ParamID {
    kLevelParam = 100,
    kTuneParam,
    kVelocityParam,
    kRootKeyParam,
    kAttackParam,
    kDecayParam,
    kSustainParam,
    kReleaseParam,
    kSlotParamBase = 200, // slot s: 200 + 7s, see SlotParam
    kZoneParamBase = 400, // slot s: 400 + 5s, see ZoneParam
    kCrossfadeParamBase = 500, // slot s: 500 + s, loop crossfade (fraction of the sample)
    kVelocityParamBase = 600,  // slot s: 600 + 2s low, + 1 high velocity (MIDI 1..127)
    kEnvelopeParamBase = 700,  // slot s: 700 + 5s, see EnvelopeParam
    kStartParamBase = 800,     // slot s: 800 + s, sample start (fraction of the sample)
};

// Per-slot envelope offsets from kEnvelopeParamBase + s * kParamsPerEnvelope.
enum EnvelopeParam : int {
    kEnvelopeOwn = 0, // Instance: the global ADSR. Own: the four below.
    kEnvelopeAttack,
    kEnvelopeDecay,
    kEnvelopeSustain,
    kEnvelopeRelease,
    kParamsPerEnvelope,
};

// Per-slot parameter offsets from kSlotParamBase + s * kParamsPerSlot.
enum SlotParam : int {
    kSlotLevel = 0,
    kSlotPan,
    kSlotTune,
    kSlotOutput,
    kSlotLoop,
    kSlotLoopStart,
    kSlotLoopEnd,
    kParamsPerSlot,
};

// Key zone offsets from kZoneParamBase + s * kParamsPerZone. A second block,
// so the parameters above keep their IDs and indexes.
enum ZoneParam : int {
    kZoneMode = 0, // Pad: one key at Root Key + slot. Zone: Low..High.
    kZoneLow,
    kZoneHigh,
    kZoneRoot,     // Key that plays the sample at its recorded pitch.
    kZoneTrack,    // Off: every key in the zone plays the recorded pitch.
    kParamsPerZone,
};

constexpr int kNumGlobalParams = 8;
constexpr int kNumSlotParams = kNumSlots * kParamsPerSlot;
constexpr int kNumZoneParams = kNumSlots * kParamsPerZone;
constexpr int kNumVelocityParams = kNumSlots * 2;
constexpr int kNumEnvelopeParams = kNumSlots * kParamsPerEnvelope;
constexpr int kNumParams = kNumGlobalParams + kNumSlotParams + kNumZoneParams + kNumSlots + kNumVelocityParams +
                            kNumEnvelopeParams + kNumSlots;

// Flat index <-> ParamID. Globals occupy 0..7, slot parameters follow, then
// the key zones, the loop crossfades, the velocity ranges, the envelopes and
// the sample starts.
static ParamID paramIdAt(int index)
{
    if(index < kNumGlobalParams)
        return static_cast<ParamID>(kLevelParam + index);
    if(index < kNumGlobalParams + kNumSlotParams)
        return static_cast<ParamID>(kSlotParamBase + (index - kNumGlobalParams));
    if(index < kNumGlobalParams + kNumSlotParams + kNumZoneParams)
        return static_cast<ParamID>(kZoneParamBase + (index - kNumGlobalParams - kNumSlotParams));
    const int crossfades = kNumGlobalParams + kNumSlotParams + kNumZoneParams;
    if(index < crossfades + kNumSlots)
        return static_cast<ParamID>(kCrossfadeParamBase + (index - crossfades));
    const int velocities = crossfades + kNumSlots;
    if(index < velocities + kNumVelocityParams)
        return static_cast<ParamID>(kVelocityParamBase + (index - velocities));
    const int envelopes = velocities + kNumVelocityParams;
    if(index < envelopes + kNumEnvelopeParams)
        return static_cast<ParamID>(kEnvelopeParamBase + (index - envelopes));
    return static_cast<ParamID>(kStartParamBase + (index - envelopes - kNumEnvelopeParams));
}

static int indexOf(ParamID id)
{
    if(id >= kLevelParam && id < kLevelParam + kNumGlobalParams)
        return static_cast<int>(id - kLevelParam);
    if(id >= kSlotParamBase && id < kSlotParamBase + kNumSlotParams)
        return kNumGlobalParams + static_cast<int>(id - kSlotParamBase);
    if(id >= kZoneParamBase && id < kZoneParamBase + kNumZoneParams)
        return kNumGlobalParams + kNumSlotParams + static_cast<int>(id - kZoneParamBase);
    if(id >= kCrossfadeParamBase && id < kCrossfadeParamBase + kNumSlots)
        return kNumGlobalParams + kNumSlotParams + kNumZoneParams + static_cast<int>(id - kCrossfadeParamBase);
    if(id >= kVelocityParamBase && id < kVelocityParamBase + kNumVelocityParams)
        return kNumGlobalParams + kNumSlotParams + kNumZoneParams + kNumSlots + static_cast<int>(id - kVelocityParamBase);
    if(id >= kEnvelopeParamBase && id < kEnvelopeParamBase + kNumEnvelopeParams)
        return kNumGlobalParams + kNumSlotParams + kNumZoneParams + kNumSlots + kNumVelocityParams +
               static_cast<int>(id - kEnvelopeParamBase);
    if(id >= kStartParamBase && id < kStartParamBase + kNumSlots)
        return kNumGlobalParams + kNumSlotParams + kNumZoneParams + kNumSlots + kNumVelocityParams +
               kNumEnvelopeParams + static_cast<int>(id - kStartParamBase);
    return -1;
}

static ParamID slotParamId(int slot, SlotParam param)
{
    return static_cast<ParamID>(kSlotParamBase + slot * kParamsPerSlot + param);
}

static ParamID envelopeParamId(int slot, EnvelopeParam param)
{
    return static_cast<ParamID>(kEnvelopeParamBase + slot * kParamsPerEnvelope + param);
}

static ParamID zoneParamId(int slot, ZoneParam param)
{
    return static_cast<ParamID>(kZoneParamBase + slot * kParamsPerZone + param);
}

// --- Normalized -> physical mappings ----------------------------------------
constexpr double kUnityLevelNorm = 60.0 / 66.0;

static float gainFromNorm(double norm)
{
    // -60 dB .. +6 dB; the bottom of the range is silence.
    if(norm <= 0.0)
        return 0.0f;
    const double db = -60.0 + norm * 66.0;
    return static_cast<float>(std::pow(10.0, db / 20.0));
}

static double semitonesFromNorm(double norm) { return norm * 48.0 - 24.0; } // +-2 octaves
static float panFromNorm(double norm) { return static_cast<float>(norm * 2.0 - 1.0); }
static float attackFromNorm(double norm) { return static_cast<float>(2.0 * norm * norm * norm); } // 0..2 s
static float timeFromNorm(double norm) { return static_cast<float>(0.001 * std::pow(10000.0, norm)); } // 1 ms..10 s
static double normFromTime(double seconds) { return std::log10(seconds / 0.001) / 4.0; }
static int rootKeyFromNorm(double norm) { return static_cast<int>(std::lround(norm * 127.0)); }
static int outputFromNorm(double norm) { return static_cast<int>(std::lround(norm * (kNumOutputs - 1))); }
static int32_t loopFromNorm(double norm) { return static_cast<int32_t>(std::lround(norm * 2.0)); }

// Immutable decoded sample: stereo interleaved with one silent guard frame at
// the end, so interpolation may always read the frame after the last one.
struct Sample {
    std::vector<float> stereo;
    int64_t frames = 0;
    double rate = 44100.0;
    std::string name;
};

static std::unique_ptr<Sample> makeSample(const float *data, int64_t frames, int channels, double rate,
                                          std::string name)
{
    auto sample = std::make_unique<Sample>();
    sample->frames = frames;
    sample->rate = rate;
    sample->name = std::move(name);
    sample->stereo.resize(static_cast<size_t>(frames + 1) * 2u, 0.0f);
    for(int64_t f = 0; f < frames; ++f) {
        const float left = data[f * channels];
        const float right = channels == 2 ? data[f * channels + 1] : left;
        sample->stereo[static_cast<size_t>(f) * 2u] = std::isfinite(left) ? left : 0.0f;
        sample->stereo[static_cast<size_t>(f) * 2u + 1u] = std::isfinite(right) ? right : 0.0f;
    }
    return sample;
}

static bool validFormat(int64_t frames, int64_t channels, double rate)
{
    return frames >= 1 && frames <= mla_sampler::kMaxFrames && (channels == 1 || channels == 2) &&
           std::isfinite(rate) && rate >= 1000.0 && rate <= 384000.0;
}

static std::string baseName(const std::string &path)
{
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

static std::unique_ptr<Sample> decodeFile(const std::string &path, std::string &error)
{
    const int64_t handle = __mlang_std_audio_pcm_load(path.c_str());
    if(handle == 0) {
        const char *why = __mlang_std_audio_last_error();
        error = (why && *why) ? why : "could not decode audio file";
        return nullptr;
    }
    const int64_t rate = __mlang_std_audio_pcm_file_sample_rate(handle);
    const int64_t channels = __mlang_std_audio_pcm_file_channels(handle);
    const int64_t frames = __mlang_std_audio_pcm_file_frame_count(handle);
    std::unique_ptr<Sample> sample;
    if(!validFormat(frames, channels, static_cast<double>(rate))) {
        error = "unsupported sample length, channel count or rate";
    } else {
        MlangList samples = __mlang_std_audio_pcm_file_samples(handle);
        if(samples.data && samples.size == frames * channels)
            sample = makeSample(static_cast<const float *>(samples.data), frames, static_cast<int>(channels),
                                static_cast<double>(rate), baseName(path));
        else
            error = "could not read decoded samples";
        std::free(samples.data);
    }
    __mlang_std_audio_pcm_file_close(handle);
    return sample;
}

static std::string binaryString(IAttributeList *attributes, IAttributeList::AttrID id)
{
    const void *data = nullptr;
    uint32 size = 0;
    if(attributes->getBinary(id, data, size) != kResultOk || data == nullptr)
        return {};
    return std::string(static_cast<const char *>(data), size);
}

static std::u16string utf16(const std::string &text) { return std::u16string(text.begin(), text.end()); }

class Processor final : public SingleComponentEffect, public IMidiMapping {
  public:
    Processor()
    {
        for(auto &slot : published_)
            slot.store(nullptr, std::memory_order_relaxed);
        for(auto &value : norm_)
            value.store(0.0, std::memory_order_relaxed);
        for(int bus = 0; bus < kNumOutputs; ++bus)
            outputActive_[bus].store(bus == 0, std::memory_order_relaxed);
    }

    ~Processor() override
    {
        destroyVoices();
    }

    // SingleComponentEffect hides IConnectionPoint ("no need to expose it to
    // the host"); expose it again so hosts can send the sampler messages.
    DEFINE_INTERFACES
        DEF_INTERFACE(IMidiMapping)
        DEF_INTERFACE(IConnectionPoint)
    END_DEFINE_INTERFACES(SingleComponentEffect)
    REFCOUNT_METHODS(SingleComponentEffect)

    tresult PLUGIN_API connect(IConnectionPoint *other) SMTG_OVERRIDE
    {
        // A host pairing the component with its own controller would connect
        // this object to itself; accept without keeping a self-reference.
        if(other == static_cast<IConnectionPoint *>(this))
            return kResultTrue;
        return SingleComponentEffect::connect(other);
    }

    tresult PLUGIN_API disconnect(IConnectionPoint *other) SMTG_OVERRIDE
    {
        if(other == static_cast<IConnectionPoint *>(this))
            return kResultTrue;
        return SingleComponentEffect::disconnect(other);
    }

    static FUnknown *createInstance(void *) { return static_cast<IComponent *>(new Processor()); }

    tresult PLUGIN_API initialize(FUnknown *context) SMTG_OVERRIDE
    {
        const tresult result = SingleComponentEffect::initialize(context);
        if(result != kResultOk)
            return result;

        addAudioOutput(STR16("Main"), SpeakerArr::kStereo);
        // Aux buses start inactive, as VST3 expects; hosts enable what they route.
        for(int bus = 1; bus < kNumOutputs; ++bus)
            addAudioOutput(utf16(outputName(bus)).c_str(), SpeakerArr::kStereo, BusTypes::kAux, 0);
        addEventInput(STR16("MIDI In"), 16);

        addParam(kLevelParam, STR16("Level"), STR16("dB"), kUnityLevelNorm);
        addParam(kTuneParam, STR16("Tune"), STR16("st"), 0.5);
        addParam(kVelocityParam, STR16("Velocity"), nullptr, 1.0);
        parameters.addParameter(STR16("Root Key"), nullptr, 127, 36.0 / 127.0, ParameterInfo::kCanAutomate,
                                kRootKeyParam);
        norm_[indexOf(kRootKeyParam)].store(36.0 / 127.0);
        addParam(kAttackParam, STR16("Attack"), STR16("s"), 0.0);
        addParam(kDecayParam, STR16("Decay"), STR16("s"), normFromTime(0.5));
        addParam(kSustainParam, STR16("Sustain"), nullptr, 1.0);
        addParam(kReleaseParam, STR16("Release"), STR16("s"), normFromTime(0.1));

        for(int slot = 0; slot < kNumSlots; ++slot) {
            addParam(slotParamId(slot, kSlotLevel), slotTitle(slot, "Level").c_str(), STR16("dB"), kUnityLevelNorm);
            addParam(slotParamId(slot, kSlotPan), slotTitle(slot, "Pan").c_str(), nullptr, 0.5);
            addParam(slotParamId(slot, kSlotTune), slotTitle(slot, "Tune").c_str(), STR16("st"), 0.5);

            auto *output = addList(slotParamId(slot, kSlotOutput), slotTitle(slot, "Output"));
            for(int bus = 0; bus < kNumOutputs; ++bus)
                output->appendString(utf16(outputName(bus)).c_str());

            auto *loop = addList(slotParamId(slot, kSlotLoop), slotTitle(slot, "Loop"));
            loop->appendString(STR16("Off"));
            loop->appendString(STR16("Forward"));
            loop->appendString(STR16("Bidirectional"));

            addParam(slotParamId(slot, kSlotLoopStart), slotTitle(slot, "Loop Start").c_str(), nullptr, 0.0);
            addParam(slotParamId(slot, kSlotLoopEnd), slotTitle(slot, "Loop End").c_str(), nullptr, 1.0);
        }
        // Key zones come last, so the parameters above keep their indexes.
        for(int slot = 0; slot < kNumSlots; ++slot) {
            auto *mode = addList(zoneParamId(slot, kZoneMode), slotTitle(slot, "Key Mode"));
            mode->appendString(STR16("Pad"));
            mode->appendString(STR16("Zone"));
            addKey(zoneParamId(slot, kZoneLow), slotTitle(slot, "Low Key"), 0);
            addKey(zoneParamId(slot, kZoneHigh), slotTitle(slot, "High Key"), 127);
            addKey(zoneParamId(slot, kZoneRoot), slotTitle(slot, "Zone Root"), 60);
            auto *track = addList(zoneParamId(slot, kZoneTrack), slotTitle(slot, "Key Track"));
            track->appendString(STR16("Off"));
            track->appendString(STR16("On"));
            track->getInfo().defaultNormalizedValue = 1.0;
            track->setNormalized(1.0);
            norm_[indexOf(zoneParamId(slot, kZoneTrack))].store(1.0, std::memory_order_relaxed);
        }
        // Loop crossfades come after the zones, again keeping indexes.
        for(int slot = 0; slot < kNumSlots; ++slot)
            addParam(static_cast<ParamID>(kCrossfadeParamBase + slot), slotTitle(slot, "Crossfade").c_str(), nullptr, 0.0);
        // Velocity ranges last: a slot plays only notes this hard.
        for(int slot = 0; slot < kNumSlots; ++slot) {
            addKey(static_cast<ParamID>(kVelocityParamBase + slot * 2), slotTitle(slot, "Vel Low"), 1);
            addKey(static_cast<ParamID>(kVelocityParamBase + slot * 2 + 1), slotTitle(slot, "Vel High"), 127);
        }
        // Per-slot envelopes last. Defaults match the instance envelope's.
        for(int slot = 0; slot < kNumSlots; ++slot) {
            auto *own = addList(envelopeParamId(slot, kEnvelopeOwn), slotTitle(slot, "Envelope"));
            own->appendString(STR16("Instance"));
            own->appendString(STR16("Own"));
            addParam(envelopeParamId(slot, kEnvelopeAttack), slotTitle(slot, "Attack").c_str(), STR16("s"), 0.0);
            addParam(envelopeParamId(slot, kEnvelopeDecay), slotTitle(slot, "Decay").c_str(), STR16("s"), normFromTime(0.5));
            addParam(envelopeParamId(slot, kEnvelopeSustain), slotTitle(slot, "Sustain").c_str(), nullptr, 1.0);
            addParam(envelopeParamId(slot, kEnvelopeRelease), slotTitle(slot, "Release").c_str(), STR16("s"), normFromTime(0.1));
        }
        // Sample starts last.
        for(int slot = 0; slot < kNumSlots; ++slot)
            addParam(static_cast<ParamID>(kStartParamBase + slot), slotTitle(slot, "Start").c_str(), nullptr, 0.0);
        return kResultOk;
    }

    tresult PLUGIN_API terminate() SMTG_OVERRIDE
    {
        destroyVoices();
        {
            std::lock_guard<std::mutex> lock(controlMutex_);
            for(int slot = 0; slot < kNumSlots; ++slot) {
                published_[slot].store(nullptr, std::memory_order_release);
                owned_[slot].reset();
            }
            graveyard_.clear();
        }
        return SingleComponentEffect::terminate();
    }

    tresult PLUGIN_API getMidiControllerAssignment(int32 bus, int16 channel, CtrlNumber cc,
                                                   ParamID &id) SMTG_OVERRIDE
    {
        if(bus != 0 || channel < 0 || channel > 15)
            return kResultFalse;
        // Raw MIDI CC numbers, matching mlacker's pattern/live CC routing.
        switch(cc) {
            case 7: id = kLevelParam; return kResultOk;    // Channel volume
            case 73: id = kAttackParam; return kResultOk;  // Sound controller 4 (attack)
            case 75: id = kDecayParam; return kResultOk;   // Sound controller 6 (decay)
            case 72: id = kReleaseParam; return kResultOk; // Sound controller 3 (release)
        }
        return kResultFalse;
    }

    tresult PLUGIN_API setBusArrangements(SpeakerArrangement *inputs, int32 numIns,
                                          SpeakerArrangement *outputs, int32 numOuts) SMTG_OVERRIDE
    {
        if(numIns != 0 || numOuts < 1 || numOuts > kNumOutputs || outputs == nullptr)
            return kResultFalse;
        for(int32 bus = 0; bus < numOuts; ++bus)
            if(outputs[bus] != SpeakerArr::kStereo)
                return kResultFalse;
        return SingleComponentEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
    }

    tresult PLUGIN_API activateBus(MediaType type, BusDirection dir, int32 index, TBool state) SMTG_OVERRIDE
    {
        const tresult result = SingleComponentEffect::activateBus(type, dir, index, state);
        if(result == kResultTrue && type == kAudio && dir == kOutput && index >= 0 && index < kNumOutputs)
            outputActive_[index].store(state != 0, std::memory_order_relaxed);
        return result;
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
        // Called while inactive: nothing is rendering, voices may be rebuilt.
        destroyVoices();
        sampleRate_ = setup.sampleRate > 0 ? setup.sampleRate : 44100.0;
        for(auto &voice : voices_)
            voice.dsp = mlasampler_voice_create__f32(static_cast<float>(sampleRate_));
        pushEnvelope();
        return kResultOk;
    }

    tresult PLUGIN_API setActive(TBool state) SMTG_OVERRIDE
    {
        if(!state)
            stopAllVoices();
        return SingleComponentEffect::setActive(state);
    }

    tresult PLUGIN_API process(ProcessData &data) SMTG_OVERRIDE
    {
        syncSlots();
        if(paramsDirty_.exchange(false, std::memory_order_acquire))
            pushEnvelope();
        handleParameterChanges(data.inputParameterChanges);

        if(data.symbolicSampleSize != kSample32) {
            finishBlock();
            return kResultFalse;
        }
        // Buses this block may write: active, stereo and with buffers. Voices
        // on any other bus fall back to Main.
        float *buses[kNumOutputs][2] = {};
        const int32 busCount = std::min<int32>(data.numOutputs, kNumOutputs);
        for(int32 bus = 0; bus < busCount; ++bus) {
            AudioBusBuffers &out = data.outputs[bus];
            if(out.channelBuffers32 == nullptr || out.numChannels < 2)
                continue;
            for(int32 ch = 0; ch < out.numChannels; ++ch)
                std::fill_n(out.channelBuffers32[ch], data.numSamples, 0.0f);
            out.silenceFlags = 0;
            if(bus == 0 || outputActive_[bus].load(std::memory_order_relaxed)) {
                buses[bus][0] = out.channelBuffers32[0];
                buses[bus][1] = out.channelBuffers32[1];
            }
        }
        if(buses[0][0] == nullptr) {
            finishBlock();
            return kResultOk;
        }

        // Render between events so note starts land on their exact frame.
        int32 rendered = 0;
        IEventList *events = data.inputEvents;
        const int32 eventCount = events ? events->getEventCount() : 0;
        for(int32 i = 0; i < eventCount; ++i) {
            Event event{};
            if(events->getEvent(i, event) != kResultOk)
                continue;
            const int32 at = std::clamp(event.sampleOffset, rendered, data.numSamples);
            render(buses, rendered, at);
            rendered = at;
            if(event.type == Event::kNoteOnEvent) {
                if(event.noteOn.velocity <= 0.0f)
                    noteOff(event.noteOn.channel, event.noteOn.pitch);
                else
                    noteOn(event.noteOn.channel, event.noteOn.pitch, event.noteOn.velocity);
            } else if(event.type == Event::kNoteOffEvent) {
                noteOff(event.noteOff.channel, event.noteOff.pitch);
            }
        }
        render(buses, rendered, data.numSamples);
        finishBlock();
        return kResultOk;
    }

    // --- Sample loading protocol -------------------------------------------
    tresult PLUGIN_API notify(IMessage *message) SMTG_OVERRIDE
    {
        if(message == nullptr || message->getMessageID() == nullptr)
            return kInvalidArgument;
        const char *id = message->getMessageID();
        const bool loadFile = std::strcmp(id, mla_sampler::kLoadFileMessage) == 0;
        const bool loadPcm = std::strcmp(id, mla_sampler::kLoadPcmMessage) == 0;
        const bool clear = std::strcmp(id, mla_sampler::kClearMessage) == 0;
        const bool info = std::strcmp(id, mla_sampler::kInfoMessage) == 0;
        if(!loadFile && !loadPcm && !clear && !info)
            return SingleComponentEffect::notify(message);

        IAttributeList *attributes = message->getAttributes();
        if(attributes == nullptr)
            return kInvalidArgument;
        if(info) {
            int64 occupied = 0;
            {
                std::lock_guard<std::mutex> lock(controlMutex_);
                for(int slot = 0; slot < kNumSlots; ++slot)
                    if(owned_[slot])
                        occupied |= int64(1) << slot;
            }
            attributes->setInt("root", rootKeyFromNorm(norm(kRootKeyParam)));
            attributes->setInt("pads", kNumSlots);
            attributes->setInt("occupied", occupied);
            return kResultOk;
        }
        // The protocol calls slots "pads".
        int64 slot = -1;
        if(attributes->getInt("pad", slot) != kResultOk || slot < 0 || slot >= kNumSlots)
            return fail(attributes, "pad must be 0-15");

        std::unique_ptr<Sample> sample;
        if(loadFile) {
            const std::string path = binaryString(attributes, "path");
            if(path.empty())
                return fail(attributes, "missing path");
            std::string error;
            sample = decodeFile(path, error);
            if(!sample)
                return fail(attributes, error);
        } else if(loadPcm) {
            int64 channels = 0, frames = 0;
            double rate = 0;
            const void *data = nullptr;
            uint32 size = 0;
            if(attributes->getInt("channels", channels) != kResultOk ||
               attributes->getInt("frames", frames) != kResultOk ||
               attributes->getFloat("rate", rate) != kResultOk ||
               attributes->getBinary("data", data, size) != kResultOk || data == nullptr)
                return fail(attributes, "missing channels, frames, rate or data");
            if(!validFormat(frames, channels, rate))
                return fail(attributes, "unsupported sample length, channel count or rate");
            if(static_cast<uint64_t>(size) != static_cast<uint64_t>(frames * channels) * sizeof(float))
                return fail(attributes, "data size does not match frames * channels");
            // The attribute buffer has no alignment guarantee; copy first.
            std::vector<float> pcm(static_cast<size_t>(frames * channels));
            std::memcpy(pcm.data(), data, size);
            sample = makeSample(pcm.data(), frames, static_cast<int>(channels), rate,
                                binaryString(attributes, "name"));
        }
        replaceSlot(static_cast<int>(slot), std::move(sample));
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

        std::unique_ptr<Sample> slots[kNumSlots];
        for(int slot = 0; slot < kNumSlots; ++slot) {
            int8 present = 0;
            if(!streamer.readInt8(present))
                return kResultFalse;
            if(!present)
                continue;
            std::string name;
            double rate = 0;
            int64 frames = 0;
            if(!readString(streamer, name) || !streamer.readDouble(rate) || !streamer.readInt64(frames) ||
               !validFormat(frames, 2, rate))
                return kResultFalse;
            std::vector<float> pcm(static_cast<size_t>(frames) * 2u);
            if(!streamer.readFloatArray(pcm.data(), static_cast<int32>(pcm.size())))
                return kResultFalse;
            slots[slot] = makeSample(pcm.data(), frames, 2, rate, std::move(name));
        }
        for(int slot = 0; slot < kNumSlots; ++slot)
            replaceSlot(slot, std::move(slots[slot]));
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
        std::lock_guard<std::mutex> lock(controlMutex_);
        for(int slot = 0; slot < kNumSlots; ++slot) {
            const Sample *sample = owned_[slot].get();
            streamer.writeInt8(sample ? 1 : 0);
            if(!sample)
                continue;
            writeString(streamer, sample->name);
            streamer.writeDouble(sample->rate);
            streamer.writeInt64(sample->frames);
            // Slots are embedded without the guard frame.
            streamer.writeFloatArray(sample->stereo.data(), static_cast<int32>(sample->frames * 2));
        }
        return kResultOk;
    }

  private:
    struct Voice {
        SamplerVoice *dsp = nullptr;
        const Sample *sample = nullptr;
        int slot = -1;
        int16 channel = -1;
        int16 pitch = -1;
        uint64_t serial = 0;
    };

    struct Retired {
        std::unique_ptr<Sample> sample;
        uint64_t freeAfterBlock = 0;
    };

    Voice voices_[kNumVoices];
    uint64_t nextSerial_ = 1;
    double sampleRate_ = 44100.0;
    std::atomic<double> norm_[kNumParams];
    std::atomic<bool> paramsDirty_{false};
    std::atomic<bool> outputActive_[kNumOutputs];

    // Audio-thread view of the slots.
    const Sample *active_[kNumSlots] = {};
    std::atomic<const Sample *> published_[kNumSlots];
    std::atomic<uint64_t> blocksDone_{0};

    // Control-thread ownership.
    std::mutex controlMutex_;
    std::unique_ptr<Sample> owned_[kNumSlots];
    std::vector<Retired> graveyard_;

    void addParam(ParamID id, const TChar *title, const TChar *units, double defaultNorm)
    {
        parameters.addParameter(title, units, 0, defaultNorm, ParameterInfo::kCanAutomate, id);
        norm_[indexOf(id)].store(defaultNorm, std::memory_order_relaxed);
    }

    StringListParameter *addList(ParamID id, const std::u16string &title)
    {
        auto *list = new StringListParameter(title.c_str(), id, nullptr,
                                             ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
        parameters.addParameter(list);
        norm_[indexOf(id)].store(0.0, std::memory_order_relaxed);
        return list;
    }

    // A MIDI key 0..127 as a stepped parameter.
    void addKey(ParamID id, const std::u16string &title, int key)
    {
        parameters.addParameter(title.c_str(), nullptr, 127, key / 127.0, ParameterInfo::kCanAutomate, id);
        norm_[indexOf(id)].store(key / 127.0, std::memory_order_relaxed);
    }

    static std::string outputName(int bus) { return bus == 0 ? "Main" : "Out " + std::to_string(bus + 1); }

    static std::u16string slotTitle(int slot, const char *what)
    {
        return utf16("Slot " + std::to_string(slot + 1) + " " + what);
    }

    static tresult fail(IAttributeList *attributes, const std::string &why)
    {
        attributes->setBinary("error", why.data(), static_cast<uint32>(why.size()));
        return kResultFalse;
    }

    static bool readString(IBStreamer &streamer, std::string &out)
    {
        uint32 size = 0;
        if(!streamer.readInt32u(size) || size > 4096)
            return false;
        out.resize(size);
        return size == 0 || streamer.readRaw(out.data(), size) == static_cast<int32>(size);
    }

    static void writeString(IBStreamer &streamer, const std::string &text)
    {
        const uint32 size = static_cast<uint32>(std::min<size_t>(text.size(), 4096));
        streamer.writeInt32u(size);
        if(size)
            streamer.writeRaw(text.data(), size);
    }

    double norm(ParamID id) const { return norm_[indexOf(id)].load(std::memory_order_relaxed); }
    double slotNorm(int slot, SlotParam param) const { return norm(slotParamId(slot, param)); }
    double zoneNorm(int slot, ZoneParam param) const { return norm(zoneParamId(slot, param)); }

    // --- Control thread ------------------------------------------------------
    void replaceSlot(int slot, std::unique_ptr<Sample> sample)
    {
        std::lock_guard<std::mutex> lock(controlMutex_);
        collectGarbage();
        published_[slot].store(sample.get());
        if(owned_[slot])
            graveyard_.push_back({std::move(owned_[slot]), blocksDone_.load() + 2});
        owned_[slot] = std::move(sample);
    }

    // A block that started after the swap has seen the new pointer and
    // stopped every voice on the old one; two completed blocks guarantee that.
    void collectGarbage()
    {
        const uint64_t done = blocksDone_.load();
        graveyard_.erase(std::remove_if(graveyard_.begin(), graveyard_.end(),
                                        [&](const Retired &r) { return done >= r.freeAfterBlock; }),
                         graveyard_.end());
    }

    void destroyVoices()
    {
        for(auto &voice : voices_) {
            if(voice.dsp)
                mlasampler_voice_destroy__ptr_struct_SamplerVoice(voice.dsp);
            voice = Voice{};
        }
    }

    // --- Audio thread --------------------------------------------------------
    void finishBlock() { blocksDone_.fetch_add(1); }

    void syncSlots()
    {
        for(int slot = 0; slot < kNumSlots; ++slot) {
            const Sample *current = published_[slot].load();
            if(current == active_[slot])
                continue;
            for(auto &voice : voices_)
                if(voice.dsp && voice.sample == active_[slot] && voice.slot == slot) {
                    mlasampler_voice_stop__ptr_struct_SamplerVoice(voice.dsp);
                    voice.sample = nullptr;
                }
            active_[slot] = current;
        }
    }

    void stopAllVoices()
    {
        for(auto &voice : voices_) {
            if(voice.dsp)
                mlasampler_voice_stop__ptr_struct_SamplerVoice(voice.dsp);
            voice.sample = nullptr;
        }
    }

    // The envelope a slot's voices use: its own, or the instance's (slot -1
    // always takes the instance's).
    void applyEnvelope(SamplerVoice *dsp, int slot)
    {
        const bool own = slot >= 0 && norm(envelopeParamId(slot, kEnvelopeOwn)) >= 0.5;
        const double attack = own ? norm(envelopeParamId(slot, kEnvelopeAttack)) : norm(kAttackParam);
        const double decay = own ? norm(envelopeParamId(slot, kEnvelopeDecay)) : norm(kDecayParam);
        const double sustain = own ? norm(envelopeParamId(slot, kEnvelopeSustain)) : norm(kSustainParam);
        const double release = own ? norm(envelopeParamId(slot, kEnvelopeRelease)) : norm(kReleaseParam);
        mlasampler_voice_set_envelope__ptr_struct_SamplerVoice_f32_f32_f32_f32(
            dsp, attackFromNorm(attack), timeFromNorm(decay), static_cast<float>(sustain), timeFromNorm(release));
    }

    // Envelope edits reach sounding voices too.
    void pushEnvelope()
    {
        for(auto &voice : voices_)
            if(voice.dsp)
                applyEnvelope(voice.dsp, voice.sample ? voice.slot : -1);
    }

    void handleParameterChanges(IParameterChanges *changes)
    {
        if(changes == nullptr)
            return;
        bool envelopeChanged = false;
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
            if((id >= kAttackParam && id <= kReleaseParam) ||
               (id >= kEnvelopeParamBase && id < kEnvelopeParamBase + kNumEnvelopeParams))
                envelopeChanged = true;
        }
        if(envelopeChanged)
            pushEnvelope();
    }

    // Every slot whose key (Pad) or key range (Zone) and velocity range hold
    // the note plays it, so overlapping zones layer and velocity ranges
    // switch between them. Zone and loop settings are read when a note
    // starts; sounding notes keep theirs.
    void noteOn(int16 channel, int16 pitch, float velocity)
    {
        const int padSlot = pitch - rootKeyFromNorm(norm(kRootKeyParam));
        const int hardness = std::clamp(static_cast<int>(std::lround(velocity * 127.0f)), 1, 127);
        for(int slot = 0; slot < kNumSlots; ++slot) {
            if(active_[slot] == nullptr)
                continue;
            const int velocityLow = rootKeyFromNorm(norm(static_cast<ParamID>(kVelocityParamBase + slot * 2)));
            const int velocityHigh = rootKeyFromNorm(norm(static_cast<ParamID>(kVelocityParamBase + slot * 2 + 1)));
            if(hardness < std::min(velocityLow, velocityHigh) || hardness > std::max(velocityLow, velocityHigh))
                continue;
            if(zoneNorm(slot, kZoneMode) < 0.5) {
                if(slot == padSlot)
                    startVoice(slot, channel, pitch, velocity, 0);
                continue;
            }
            const int low = rootKeyFromNorm(zoneNorm(slot, kZoneLow));
            const int high = rootKeyFromNorm(zoneNorm(slot, kZoneHigh));
            if(pitch < std::min(low, high) || pitch > std::max(low, high))
                continue;
            const int tracked = zoneNorm(slot, kZoneTrack) >= 0.5 ? pitch - rootKeyFromNorm(zoneNorm(slot, kZoneRoot)) : 0;
            startVoice(slot, channel, pitch, velocity, tracked);
        }
    }

    // `keySemitones`: pitch offset from key tracking.
    void startVoice(int slot, int16 channel, int16 pitch, float velocity, int keySemitones)
    {
        const Sample *sample = active_[slot];
        Voice *target = nullptr;
        for(auto &voice : voices_) {
            if(!voice.dsp)
                continue;
            if(!mlasampler_voice_is_active__ptr_struct_SamplerVoice(voice.dsp)) {
                target = &voice;
                break;
            }
            if(target == nullptr || voice.serial < target->serial)
                target = &voice; // Steal the oldest if all are busy.
        }
        if(target == nullptr)
            return;

        const double semitones =
            semitonesFromNorm(norm(kTuneParam)) + semitonesFromNorm(slotNorm(slot, kSlotTune)) + keySemitones;
        const double step = sample->rate / sampleRate_ * std::pow(2.0, semitones / 12.0);
        const float sensitivity = static_cast<float>(norm(kVelocityParam));
        const float velocityGain = 1.0f - sensitivity + sensitivity * std::clamp(velocity, 0.0f, 1.0f);
        const float gain = gainFromNorm(norm(kLevelParam)) * gainFromNorm(slotNorm(slot, kSlotLevel)) * velocityGain;
        const float pan = panFromNorm(slotNorm(slot, kSlotPan));
        const double frames = static_cast<double>(sample->frames);
        const double loopStart = std::floor(slotNorm(slot, kSlotLoopStart) * frames);
        const double loopEnd = std::floor(slotNorm(slot, kSlotLoopEnd) * frames);

        applyEnvelope(target->dsp, slot);
        mlasampler_voice_start__ptr_struct_SamplerVoice_f64_f64_f32_f32_i32_f64_f64(
            target->dsp, frames, step, gain, pan, loopFromNorm(slotNorm(slot, kSlotLoop)), loopStart, loopEnd);
        mlasampler_voice_set_crossfade__ptr_struct_SamplerVoice_f64(
            target->dsp, std::floor(norm(static_cast<ParamID>(kCrossfadeParamBase + slot)) * frames));
        mlasampler_voice_set_start__ptr_struct_SamplerVoice_f64(
            target->dsp, std::floor(norm(static_cast<ParamID>(kStartParamBase + slot)) * frames));
        target->sample = sample;
        target->slot = slot;
        target->channel = channel;
        target->pitch = pitch;
        target->serial = nextSerial_++;
    }

    void noteOff(int16 channel, int16 pitch)
    {
        for(auto &voice : voices_)
            if(voice.dsp && voice.channel == channel && voice.pitch == pitch)
                mlasampler_voice_release__ptr_struct_SamplerVoice(voice.dsp);
    }

    // `buses[b]` is null for a bus this block cannot write; Main never is.
    void render(float *(&buses)[kNumOutputs][2], int32 from, int32 to)
    {
        if(from >= to)
            return;
        for(auto &voice : voices_) {
            if(!voice.dsp || !voice.sample || !mlasampler_voice_is_active__ptr_struct_SamplerVoice(voice.dsp))
                continue;
            const int bus = outputFromNorm(slotNorm(voice.slot, kSlotOutput));
            const bool routed = bus > 0 && bus < kNumOutputs && buses[bus][0] != nullptr;
            float *outL = routed ? buses[bus][0] : buses[0][0];
            float *outR = routed ? buses[bus][1] : buses[0][1];
            const float *pcm = voice.sample->stereo.data();
            const int64_t guard = voice.sample->frames; // Index of the silent guard frame.
            for(int32 i = from; i < to; ++i) {
                int64_t frame = static_cast<int64_t>(mlasampler_voice_frame__ptr_struct_SamplerVoice(voice.dsp));
                int64_t next = static_cast<int64_t>(mlasampler_voice_next_frame__ptr_struct_SamplerVoice(voice.dsp));
                frame = std::clamp<int64_t>(frame, 0, guard - 1);
                next = std::clamp<int64_t>(next, 0, guard);
                const float *a = pcm + frame * 2;
                const float *b = pcm + next * 2;
                // During a crossfade, the frames a loop length back fade in.
                const double shadowFrame = mlasampler_voice_shadow_frame__ptr_struct_SamplerVoice(voice.dsp);
                const int64_t shadow = shadowFrame < 0 ? 0 : std::clamp<int64_t>(static_cast<int64_t>(shadowFrame), 0, guard - 1);
                const float *c = pcm + shadow * 2;
                const float *d = pcm + (shadow + 1) * 2;
                float right = 0.0f;
                const float left = mlasampler_voice_render__ptr_struct_SamplerVoice_f32_f32_f32_f32_f32_f32_f32_f32_ptr_f32(
                    voice.dsp, a[0], a[1], b[0], b[1], c[0], c[1], d[0], d[1], &right);
                outL[i] += left;
                outR[i] += right;
                if(!mlasampler_voice_is_active__ptr_struct_SamplerVoice(voice.dsp)) {
                    voice.sample = nullptr;
                    break;
                }
            }
        }
    }
};

} // namespace mla_sampler_plugin

BEGIN_FACTORY_DEF("MLang", "https://github.com/mattilaa/mlang", "mailto:devnull@example.invalid")
DEF_CLASS2(INLINE_UID_FROM_FUID(mla_sampler_plugin::kProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass,
           "Mla Sampler", 0, PlugType::kInstrumentSampler, "0.1.0", kVstVersionString,
           mla_sampler_plugin::Processor::createInstance)
END_FACTORY
