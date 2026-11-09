// Mla Paula - VST3 instrument emulating the Amiga's Paula audio chip.
//
// Paula plays four DMA channels of signed 8-bit samples, each at the rate an
// integer period sets (clock / period: 3546895 Hz PAL, 3579545 Hz NTSC),
// with a 0..64 linear volume. Channels 1 and 4 are hard left, 2 and 3 hard
// right. After the DACs the A500 has a fixed ~4.4 kHz RC low-pass and the
// switchable ~3.3 kHz "LED" filter (the A1200's fixed filter is ~34 kHz).
//
// This instrument holds up to 16 samples in key zones. Every sample is
// reduced to 8-bit mono when it arrives. A note's pitch becomes the nearest
// period, so pitches are quantized the way they are on the machine, and
// periods below 124 (about 28.6 kHz) play at 124. Between sample words the
// channel holds each word (Paula) or interpolates linearly. Volume envelopes
// and vibrato step once per vertical blank (50 Hz PAL, 60 Hz NTSC), as a
// tracker replay routine does. Channel picks one fixed Paula channel per
// instance (one voice, its side of the stereo field) or Auto: four voices
// taking channels 1..4 in turn.
//
// The channel and filter math is MLang (`src/mla_paula_dsp.mla`). This file
// owns the sample memory and the voices, schedules MIDI sample-accurately,
// oversamples the channels and the analog stage, maps VST3 parameters and
// persists state. Samples arrive through the IConnectionPoint messages in
// `mla_sampler_protocol.h` ("pad" = slot), so mlacker fills them as it fills
// Mla Drum's pads.
//
// Register mode: SysEx packets (see registerPacket) write Paula's channel
// registers directly, frame by frame, as a replay routine does on the
// machine. Slot 1's sample is then chip memory, addressed by byte offset.
// A note leaves register mode; a channel without packets for 0.3 s stops.
//
// Threading follows Mla Drum: samples are immutable, published through
// atomics and freed two audio blocks after they are replaced.

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

// --- MLang DSP bridge (compiled from src/mla_paula_dsp.mla) -------------------
struct PaulaVoice;
struct PaulaFilter;
extern "C" int32_t mlapaula_period__f64_f64(double clock, double rate);
extern "C" PaulaVoice *mlapaula_voice_create__f64_f64_f64(double rate, double clock, double tickRate);
extern "C" void mlapaula_voice_destroy__ptr_struct_PaulaVoice(PaulaVoice *voice);
extern "C" void mlapaula_voice_set_clock__ptr_struct_PaulaVoice_f64_f64_i32(PaulaVoice *voice, double clock,
                                                                           double tickRate, int32_t linear);
extern "C" void mlapaula_voice_set_envelope__ptr_struct_PaulaVoice_f64_f64(PaulaVoice *voice, double attackTicks,
                                                                          double releaseTicks);
extern "C" void mlapaula_voice_set_vibrato__ptr_struct_PaulaVoice_i32_i32_i32(PaulaVoice *voice, int32_t speed,
                                                                             int32_t depth, int32_t delayTicks);
extern "C" void mlapaula_voice_start__ptr_struct_PaulaVoice_f64_i32_f64_f64_i32_i32(
    PaulaVoice *voice, double frames, int32_t looping, double loopStart, double loopEnd, int32_t period,
    int32_t volume);
extern "C" void mlapaula_voice_glide__ptr_struct_PaulaVoice_i32_i32(PaulaVoice *voice, int32_t period,
                                                                   int32_t volume);
extern "C" void mlapaula_voice_release__ptr_struct_PaulaVoice(PaulaVoice *voice);
extern "C" void mlapaula_voice_stop__ptr_struct_PaulaVoice(PaulaVoice *voice);
extern "C" int32_t mlapaula_voice_is_active__ptr_struct_PaulaVoice(PaulaVoice *voice);
extern "C" double mlapaula_voice_frame__ptr_struct_PaulaVoice(PaulaVoice *voice);
extern "C" double mlapaula_voice_next_frame__ptr_struct_PaulaVoice(PaulaVoice *voice);
extern "C" float mlapaula_voice_render__ptr_struct_PaulaVoice_f32_f32(PaulaVoice *voice, float word0, float word1);
extern "C" void mlapaula_voice_set_raw__ptr_struct_PaulaVoice_i32(PaulaVoice *voice, int32_t raw);
extern "C" void mlapaula_voice_raw_location__ptr_struct_PaulaVoice_f64(PaulaVoice *voice, double address);
extern "C" void mlapaula_voice_raw_length__ptr_struct_PaulaVoice_i32(PaulaVoice *voice, int32_t words);
extern "C" void mlapaula_voice_raw_period__ptr_struct_PaulaVoice_i32(PaulaVoice *voice, int32_t period);
extern "C" void mlapaula_voice_raw_volume__ptr_struct_PaulaVoice_i32(PaulaVoice *voice, int32_t volume);
extern "C" void mlapaula_voice_raw_dma__ptr_struct_PaulaVoice_i32(PaulaVoice *voice, int32_t on);
extern "C" int64_t mlapaula_filter_size__void();
extern "C" void mlapaula_filter_configure__ptr_struct_PaulaFilter_f64_i32_i32(PaulaFilter *filter, double rate,
                                                                             int32_t model, int32_t led);
extern "C" void mlapaula_filter_reset__ptr_struct_PaulaFilter(PaulaFilter *filter);
extern "C" float mlapaula_filter_process__ptr_struct_PaulaFilter_f32_f32_ptr_f32(PaulaFilter *filter, float left,
                                                                                 float right, float *outRight);

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

namespace mla_paula {

// Stable class id, distinct from the other Mla plugins.
static const FUID kProcessorUID(0x4D6C6150, 0x61756C61, 0x0A500F5E, 0x3546895D);

constexpr int kNumSlots = 16;
constexpr int kNumChannels = 4;
constexpr uint32 kStateMagic = 0x41504C4D; // "MLPA" little-endian
constexpr uint32 kStateVersion = 1;
constexpr double kPalClock = 3546895.0;
constexpr double kNtscClock = 3579545.0;
// Channels 1 and 4 are left, 2 and 3 right.
constexpr int kChannelSide[kNumChannels] = {-1, 1, 1, -1};

enum ParamId : ParamID {
    kLevelParam = 100,
    kTuneParam,
    kVelocityParam,
    kChannelParam,
    kInterpolationParam,
    kModelParam,
    kLedParam,
    kClockParam,
    kSeparationParam,
    kLegatoParam,
    kAttackParam,
    kReleaseParam,
    kVibratoSpeedParam,
    kVibratoDepthParam,
    kVibratoDelayParam,
    kSlotParamBase = 200, // slot s: 200 + 8s + field
};

enum SlotField {
    kSlotLowKey,
    kSlotHighKey,
    kSlotRootKey,
    kSlotLoop,
    kSlotLoopStart,
    kSlotLoopEnd,
    kSlotVolume,
    kSlotFinetune,
    kFieldsPerSlot
};

constexpr int kNumGlobalParams = 15;
constexpr int kNumParams = kNumGlobalParams + kNumSlots * kFieldsPerSlot;

static ParamID paramIdAt(int index)
{
    if(index < kNumGlobalParams)
        return static_cast<ParamID>(kLevelParam + index);
    return static_cast<ParamID>(kSlotParamBase + (index - kNumGlobalParams));
}

static int indexOf(ParamID id)
{
    if(id >= kLevelParam && id < kLevelParam + kNumGlobalParams)
        return static_cast<int>(id - kLevelParam);
    if(id >= kSlotParamBase && id < kSlotParamBase + kNumSlots * kFieldsPerSlot)
        return kNumGlobalParams + static_cast<int>(id - kSlotParamBase);
    return -1;
}

static ParamID slotParamId(int slot, int field) { return static_cast<ParamID>(kSlotParamBase + slot * kFieldsPerSlot + field); }

// --- Normalized -> physical mappings ----------------------------------------
constexpr double kUnityLevelNorm = 60.0 / 66.0;

static float gainFromNorm(double norm)
{
    if(norm <= 0.0)
        return 0.0f;
    return static_cast<float>(std::pow(10.0, (-60.0 + norm * 66.0) / 20.0));
}
static double semitonesFromNorm(double norm) { return norm * 48.0 - 24.0; }
static double secondsFromNorm(double norm) { return 2.0 * norm * norm * norm; } // 0..2 s
static int stepsFromNorm(double norm, int steps) { return static_cast<int>(std::lround(norm * steps)); }

// Immutable sample: 8-bit words as floats (-128..127 / 128), one silent guard
// word past the end so the next word can always be read.
struct Sample {
    std::vector<float> words;
    int64_t frames = 0;
    double rate = 8287.0;
    std::string name;
};

static float toWord(float value)
{
    if(!std::isfinite(value))
        return 0.0f;
    const float q = std::clamp(std::round(value * 128.0f), -128.0f, 127.0f);
    return q / 128.0f;
}

static std::unique_ptr<Sample> makeSample(const float *data, int64_t frames, int channels, double rate,
                                          std::string name)
{
    auto sample = std::make_unique<Sample>();
    sample->frames = frames;
    sample->rate = rate;
    sample->name = std::move(name);
    sample->words.resize(static_cast<size_t>(frames + 1), 0.0f);
    for(int64_t f = 0; f < frames; ++f) {
        float value = data[f * channels];
        if(channels == 2)
            value = 0.5f * (value + data[f * channels + 1]);
        sample->words[static_cast<size_t>(f)] = toWord(value);
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
    }

    ~Processor() override { destroyDsp(); }

    DEFINE_INTERFACES
        DEF_INTERFACE(IMidiMapping)
        DEF_INTERFACE(IConnectionPoint)
    END_DEFINE_INTERFACES(SingleComponentEffect)
    REFCOUNT_METHODS(SingleComponentEffect)

    tresult PLUGIN_API connect(IConnectionPoint *other) SMTG_OVERRIDE
    {
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

        addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
        addEventInput(STR16("MIDI In"), 16);

        addParam(kLevelParam, STR16("Level"), STR16("dB"), kUnityLevelNorm);
        addParam(kTuneParam, STR16("Tune"), STR16("st"), 0.5);
        addParam(kVelocityParam, STR16("Velocity"), nullptr, 1.0);
        addList(kChannelParam, "Channel", {"Auto", "1 Left", "2 Right", "3 Right", "4 Left"}, 0);
        addList(kInterpolationParam, "Interpolation", {"Paula", "Linear"}, 0);
        addList(kModelParam, "Model", {"A500", "A1200"}, 0);
        addList(kLedParam, "LED Filter", {"Off", "On"}, 0);
        addList(kClockParam, "Clock", {"PAL", "NTSC"}, 0);
        addParam(kSeparationParam, STR16("Separation"), STR16("%"), 1.0);
        addList(kLegatoParam, "Legato", {"Off", "On"}, 0);
        addParam(kAttackParam, STR16("Attack"), STR16("s"), 0.0);
        addParam(kReleaseParam, STR16("Release"), STR16("s"), 0.0);
        addStepped(kVibratoSpeedParam, "Vibrato Speed", 15, 0);
        addStepped(kVibratoDepthParam, "Vibrato Depth", 15, 0);
        addParam(kVibratoDelayParam, STR16("Vibrato Delay"), STR16("s"), 0.0);

        std::vector<std::string> finetunes;
        for(int f = -8; f <= 7; ++f)
            finetunes.push_back(f > 0 ? "+" + std::to_string(f) : std::to_string(f));
        for(int slot = 0; slot < kNumSlots; ++slot) {
            const std::string prefix = "Slot " + std::to_string(slot + 1) + " ";
            addStepped(slotParamId(slot, kSlotLowKey), prefix + "Low Key", 127, 0);
            addStepped(slotParamId(slot, kSlotHighKey), prefix + "High Key", 127, 127);
            addStepped(slotParamId(slot, kSlotRootKey), prefix + "Root Key", 127, 60);
            addList(slotParamId(slot, kSlotLoop), prefix + "Loop", {"Off", "Forward"}, 0);
            addParam(slotParamId(slot, kSlotLoopStart), utf16(prefix + "Loop Start").c_str(), nullptr, 0.0);
            addParam(slotParamId(slot, kSlotLoopEnd), utf16(prefix + "Loop End").c_str(), nullptr, 1.0);
            addStepped(slotParamId(slot, kSlotVolume), prefix + "Volume", 64, 64);
            addList(slotParamId(slot, kSlotFinetune), prefix + "Finetune", finetunes, 8);
        }
        return kResultOk;
    }

    tresult PLUGIN_API terminate() SMTG_OVERRIDE
    {
        destroyDsp();
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
        if(cc == 7) {
            id = kLevelParam;
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
        destroyDsp();
        sampleRate_ = setup.sampleRate > 0 ? setup.sampleRate : 44100.0;
        // Run the channels and the analog stage near 192 kHz.
        oversample_ = std::clamp(static_cast<int>(std::lround(192000.0 / sampleRate_)), 1, 4);
        const double rate = sampleRate_ * oversample_;
        for(auto &voice : voices_)
            voice.dsp = mlapaula_voice_create__f64_f64_f64(rate, kPalClock, 50.0);
        filter_ = static_cast<PaulaFilter *>(std::calloc(1, static_cast<size_t>(mlapaula_filter_size__void())));
        pushParameters();
        return kResultOk;
    }

    tresult PLUGIN_API setActive(TBool state) SMTG_OVERRIDE
    {
        if(!state) {
            leaveRegisterMode();
            stopAllVoices();
            if(filter_)
                mlapaula_filter_reset__ptr_struct_PaulaFilter(filter_);
        }
        return SingleComponentEffect::setActive(state);
    }

    tresult PLUGIN_API process(ProcessData &data) SMTG_OVERRIDE
    {
        syncSlots();
        if(paramsDirty_.exchange(false, std::memory_order_acquire))
            pushParameters();
        handleParameterChanges(data.inputParameterChanges);
        expireChannels();

        if(data.symbolicSampleSize != kSample32) {
            finishBlock();
            return kResultFalse;
        }
        if(data.numOutputs < 1 || data.outputs[0].numChannels < 2 || data.outputs[0].channelBuffers32 == nullptr) {
            finishBlock();
            return kResultOk;
        }
        AudioBusBuffers &out = data.outputs[0];
        float *outL = out.channelBuffers32[0];
        float *outR = out.channelBuffers32[1];

        int32 rendered = 0;
        IEventList *events = data.inputEvents;
        const int32 eventCount = events ? events->getEventCount() : 0;
        for(int32 i = 0; i < eventCount; ++i) {
            Event event{};
            if(events->getEvent(i, event) != kResultOk)
                continue;
            const int32 at = std::clamp(event.sampleOffset, rendered, data.numSamples);
            render(outL, outR, rendered, at);
            rendered = at;
            if(event.type == Event::kDataEvent && event.data.type == DataEvent::kMidiSysEx) {
                registerPacket(event.data.bytes, event.data.size, sampleClock_ + at);
            } else if(event.type == Event::kNoteOnEvent) {
                if(event.noteOn.velocity <= 0.0f)
                    noteOff(event.noteOn.pitch);
                else
                    noteOn(event.noteOn.pitch, event.noteOn.velocity);
            } else if(event.type == Event::kNoteOffEvent) {
                noteOff(event.noteOff.pitch);
            }
        }
        render(outL, outR, rendered, data.numSamples);
        sampleClock_ += data.numSamples;
        out.silenceFlags = 0;
        finishBlock();
        return kResultOk;
    }

    // --- Sample loading protocol ("pad" = slot) ---------------------------
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
            attributes->setInt("root", stepsFromNorm(norm(slotParamId(0, kSlotRootKey)), 127));
            attributes->setInt("pads", kNumSlots);
            attributes->setInt("occupied", occupied);
            return kResultOk;
        }
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
                continue;
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
               !validFormat(frames, 1, rate))
                return kResultFalse;
            std::vector<int8> bytes(static_cast<size_t>(frames));
            if(streamer.readRaw(bytes.data(), static_cast<int32>(frames)) != static_cast<int32>(frames))
                return kResultFalse;
            std::vector<float> pcm(bytes.size());
            for(size_t f = 0; f < bytes.size(); ++f)
                pcm[f] = static_cast<float>(bytes[f]) / 128.0f;
            slots[slot] = makeSample(pcm.data(), frames, 1, rate, std::move(name));
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
            // The 8-bit words, as Paula's chip memory holds them.
            std::vector<int8> bytes(static_cast<size_t>(sample->frames));
            for(size_t f = 0; f < bytes.size(); ++f)
                bytes[f] = static_cast<int8>(std::lround(sample->words[f] * 128.0f));
            streamer.writeRaw(bytes.data(), static_cast<int32>(bytes.size()));
        }
        return kResultOk;
    }

  private:
    struct Voice {
        PaulaVoice *dsp = nullptr;
        const Sample *sample = nullptr;
        int slot = -1;
        int16 pitch = -1;
        bool held = false;
        uint64_t serial = 0;
    };

    struct Retired {
        std::unique_ptr<Sample> sample;
        uint64_t freeAfterBlock = 0;
    };

    // One register frame: the ordered writes of one channel at one moment.
    struct RawFrame {
        int64_t at = 0;
        int channel = 0;
        bool snapshot = false; // applied only to a channel starting cold
        uint8 count = 0;
        uint8 ops[80] = {};
    };
    static constexpr int kMaxRawFrames = 256;

    Voice voices_[kNumChannels];
    PaulaFilter *filter_ = nullptr;
    int64_t sampleClock_ = 0; // host frames rendered
    bool registerMode_ = false;
    RawFrame raw_[kMaxRawFrames];
    int rawCount_ = 0;
    int64_t lastPacket_[kNumChannels] = {};
    bool cold_[kNumChannels] = {true, true, true, true};
    uint64_t nextSerial_ = 1;
    double sampleRate_ = 44100.0;
    int oversample_ = 4;
    double clock_ = kPalClock;
    double tickRate_ = 50.0;
    std::atomic<double> norm_[kNumParams];
    std::atomic<bool> paramsDirty_{false};

    const Sample *active_[kNumSlots] = {};
    std::atomic<const Sample *> published_[kNumSlots];
    std::atomic<uint64_t> blocksDone_{0};

    std::mutex controlMutex_;
    std::unique_ptr<Sample> owned_[kNumSlots];
    std::vector<Retired> graveyard_;

    void addParam(ParamID id, const TChar *title, const TChar *units, double defaultNorm)
    {
        parameters.addParameter(title, units, 0, defaultNorm, ParameterInfo::kCanAutomate, id);
        norm_[indexOf(id)].store(defaultNorm, std::memory_order_relaxed);
    }

    void addStepped(ParamID id, const std::string &title, int steps, int defaultStep)
    {
        const double value = static_cast<double>(defaultStep) / steps;
        parameters.addParameter(utf16(title).c_str(), nullptr, steps, value, ParameterInfo::kCanAutomate, id);
        norm_[indexOf(id)].store(value, std::memory_order_relaxed);
    }

    void addList(ParamID id, const std::string &title, const std::vector<std::string> &items, int defaultItem)
    {
        auto *list = new StringListParameter(utf16(title).c_str(), id, nullptr,
                                             ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
        for(const auto &item : items)
            list->appendString(utf16(item).c_str());
        const double value = items.size() > 1 ? static_cast<double>(defaultItem) / (items.size() - 1) : 0.0;
        list->setNormalized(value);
        list->getInfo().defaultNormalizedValue = value;
        parameters.addParameter(list);
        norm_[indexOf(id)].store(value, std::memory_order_relaxed);
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
    int slotSteps(int slot, int field, int steps) const { return stepsFromNorm(norm(slotParamId(slot, field)), steps); }

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

    void collectGarbage()
    {
        const uint64_t done = blocksDone_.load();
        graveyard_.erase(std::remove_if(graveyard_.begin(), graveyard_.end(),
                                        [&](const Retired &r) { return done >= r.freeAfterBlock; }),
                         graveyard_.end());
    }

    void destroyDsp()
    {
        for(auto &voice : voices_) {
            if(voice.dsp)
                mlapaula_voice_destroy__ptr_struct_PaulaVoice(voice.dsp);
            voice = Voice{};
        }
        std::free(filter_);
        filter_ = nullptr;
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
                if(voice.dsp && voice.slot == slot && voice.sample == active_[slot]) {
                    mlapaula_voice_stop__ptr_struct_PaulaVoice(voice.dsp);
                    voice.sample = nullptr;
                }
            active_[slot] = current;
        }
    }

    void stopAllVoices()
    {
        for(auto &voice : voices_) {
            if(voice.dsp)
                mlapaula_voice_stop__ptr_struct_PaulaVoice(voice.dsp);
            voice.sample = nullptr;
            voice.held = false;
        }
    }

    void pushParameters()
    {
        const bool ntsc = norm(kClockParam) >= 0.5;
        clock_ = ntsc ? kNtscClock : kPalClock;
        tickRate_ = ntsc ? 60.0 : 50.0;
        const int32_t linear = norm(kInterpolationParam) >= 0.5 ? 1 : 0;
        const double attack = secondsFromNorm(norm(kAttackParam)) * tickRate_;
        const double release = secondsFromNorm(norm(kReleaseParam)) * tickRate_;
        const int32_t speed = stepsFromNorm(norm(kVibratoSpeedParam), 15);
        const int32_t depth = stepsFromNorm(norm(kVibratoDepthParam), 15);
        const int32_t delay = static_cast<int32_t>(std::lround(norm(kVibratoDelayParam) * 2.0 * tickRate_));
        for(auto &voice : voices_) {
            if(!voice.dsp)
                continue;
            mlapaula_voice_set_clock__ptr_struct_PaulaVoice_f64_f64_i32(voice.dsp, clock_, tickRate_, linear);
            mlapaula_voice_set_envelope__ptr_struct_PaulaVoice_f64_f64(voice.dsp, attack, release);
            mlapaula_voice_set_vibrato__ptr_struct_PaulaVoice_i32_i32_i32(voice.dsp, speed, depth, delay);
        }
        if(filter_)
            mlapaula_filter_configure__ptr_struct_PaulaFilter_f64_i32_i32(
                filter_, sampleRate_ * oversample_, norm(kModelParam) >= 0.5 ? 1 : 0, norm(kLedParam) >= 0.5 ? 1 : 0);
    }

    void handleParameterChanges(IParameterChanges *changes)
    {
        if(changes == nullptr)
            return;
        bool changed = false;
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
            const int index = indexOf(queue->getParameterId());
            if(index < 0 || !std::isfinite(value))
                continue;
            norm_[index].store(std::clamp(value, 0.0, 1.0), std::memory_order_relaxed);
            changed = true;
        }
        if(changed)
            pushParameters();
    }

    // The lowest slot with a sample whose zone holds the key.
    int slotFor(int pitch) const
    {
        for(int slot = 0; slot < kNumSlots; ++slot)
            if(active_[slot] && pitch >= slotSteps(slot, kSlotLowKey, 127) && pitch <= slotSteps(slot, kSlotHighKey, 127))
                return slot;
        return -1;
    }

    int fixedChannel() const { return stepsFromNorm(norm(kChannelParam), 4) - 1; } // -1 = Auto

    void noteOn(int16 pitch, float velocity)
    {
        if(registerMode_)
            leaveRegisterMode();
        const int slot = slotFor(pitch);
        if(slot < 0)
            return;
        const Sample *sample = active_[slot];

        const double semitones = pitch - slotSteps(slot, kSlotRootKey, 127) + semitonesFromNorm(norm(kTuneParam)) +
                                 (slotSteps(slot, kSlotFinetune, 15) - 8) / 8.0;
        const int32_t period = mlapaula_period__f64_f64(clock_, sample->rate * std::pow(2.0, semitones / 12.0));
        const double sensitivity = norm(kVelocityParam);
        const double velocityGain = 1.0 - sensitivity + sensitivity * std::clamp<double>(velocity, 0.0, 1.0);
        const int32_t volume = static_cast<int32_t>(std::lround(slotSteps(slot, kSlotVolume, 64) * velocityGain));

        const int fixed = fixedChannel();
        Voice *target = nullptr;
        if(fixed >= 0) {
            target = &voices_[fixed];
        } else {
            for(auto &voice : voices_) {
                if(!voice.dsp)
                    continue;
                if(!mlapaula_voice_is_active__ptr_struct_PaulaVoice(voice.dsp)) {
                    target = &voice;
                    break;
                }
                if(target == nullptr || voice.serial < target->serial)
                    target = &voice;
            }
        }
        if(target == nullptr || target->dsp == nullptr)
            return;

        const bool legato = fixed >= 0 && norm(kLegatoParam) >= 0.5 && target->held && target->slot == slot &&
                            target->sample == sample && mlapaula_voice_is_active__ptr_struct_PaulaVoice(target->dsp);
        if(legato) {
            mlapaula_voice_glide__ptr_struct_PaulaVoice_i32_i32(target->dsp, period, volume);
        } else {
            const bool looping = slotSteps(slot, kSlotLoop, 1) == 1;
            const double frames = static_cast<double>(sample->frames);
            mlapaula_voice_start__ptr_struct_PaulaVoice_f64_i32_f64_f64_i32_i32(
                target->dsp, frames, looping ? 1 : 0, norm(slotParamId(slot, kSlotLoopStart)) * frames,
                norm(slotParamId(slot, kSlotLoopEnd)) * frames, period, volume);
        }
        target->sample = sample;
        target->slot = slot;
        target->pitch = pitch;
        target->held = true;
        target->serial = nextSerial_++;
    }

    void noteOff(int16 pitch)
    {
        for(auto &voice : voices_)
            if(voice.dsp && voice.held && voice.pitch == pitch) {
                voice.held = false;
                mlapaula_voice_release__ptr_struct_PaulaVoice(voice.dsp);
            }
    }

    // --- Register mode ---------------------------------------------------
    // Non-commercial SysEx, without F0/F7:
    //   7D 'MPAU' 01, channel 0-3, frame count 1-8, frame step in us (three
    //   little-endian 7-bit bytes), flags (1 reset: stop all channels first;
    //   2 a state snapshot follows),
    //   [snapshot: byte count, writes], then per frame: byte count, writes.
    // Writes, in order: 01 a b c LC (byte offset in slot 1's sample, 21
    // bits; 1FFFFF = outside it), 02 a b c LEN (words), 03 a b c PER,
    // 04 v VOL (0-64), 05 DMA on, 06 DMA off. The snapshot (the channel's
    // registers and DMA at the packet's start) only applies to a channel
    // starting cold, so playback can begin on any row.
    static bool validOps(const uint8 *p, int n)
    {
        for(int i = 0; i < n;) {
            const int op = p[i];
            const int size = op == 1 || op == 2 || op == 3 ? 4 : op == 4 ? 2 : op == 5 || op == 6 ? 1 : 0;
            if(size == 0 || i + size > n)
                return false;
            i += size;
        }
        return true;
    }

    void registerPacket(const uint8 *p, uint32 size, int64_t at)
    {
        static constexpr uint8 magic[] = {0x7D, 'M', 'P', 'A', 'U', 1};
        if(p == nullptr || size < 13 || size > 128 || std::memcmp(p, magic, sizeof(magic)) != 0)
            return;
        for(uint32 i = 0; i < size; ++i)
            if(p[i] > 127)
                return;
        const int channel = p[6], count = p[7], flags = p[11];
        const int micros = p[8] | (p[9] << 7) | (p[10] << 14);
        if(channel >= kNumChannels || count < 1 || count > 8 || micros < 1000 || micros > 100000 || (flags & ~3))
            return;
        RawFrame frames[9];
        int decoded = 0;
        uint32 i = 12;
        const int total = count + ((flags & 2) ? 1 : 0);
        for(int f = 0; f < total; ++f) {
            if(i >= size)
                return;
            const int n = p[i++];
            if(i + n > size || n > static_cast<int>(sizeof(RawFrame::ops)) || !validOps(p + i, n))
                return;
            RawFrame &frame = frames[decoded++];
            frame.channel = channel;
            frame.snapshot = (flags & 2) && f == 0;
            const int index = (flags & 2) ? f - 1 : f;
            frame.at = at + (frame.snapshot ? 0 : static_cast<int64_t>(std::llround(index * sampleRate_ * micros / 1e6)));
            frame.count = static_cast<uint8>(n);
            std::memcpy(frame.ops, p + i, static_cast<size_t>(n));
            i += static_cast<uint32>(n);
        }
        if(i != size)
            return;
        if(!registerMode_)
            enterRegisterMode();
        if(flags & 1) {
            rawCount_ = 0;
            for(int c = 0; c < kNumChannels; ++c)
                if(voices_[c].dsp)
                    mlapaula_voice_raw_dma__ptr_struct_PaulaVoice_i32(voices_[c].dsp, 0);
        }
        lastPacket_[channel] = at;
        for(int f = 0; f < decoded; ++f)
            pushFrame(frames[f]);
    }

    void pushFrame(const RawFrame &frame)
    {
        if(rawCount_ >= kMaxRawFrames)
            return;
        int position = rawCount_++;
        // Keep time order; equal times keep arrival order (snapshot first).
        while(position > 0 && raw_[position - 1].at > frame.at) {
            raw_[position] = raw_[position - 1];
            --position;
        }
        raw_[position] = frame;
    }

    RawFrame popFrame()
    {
        RawFrame frame = raw_[0];
        for(int k = 1; k < rawCount_; ++k)
            raw_[k - 1] = raw_[k];
        --rawCount_;
        return frame;
    }

    void enterRegisterMode()
    {
        registerMode_ = true;
        rawCount_ = 0;
        for(int c = 0; c < kNumChannels; ++c) {
            cold_[c] = true;
            if(voices_[c].dsp)
                mlapaula_voice_set_raw__ptr_struct_PaulaVoice_i32(voices_[c].dsp, 1);
            voices_[c].sample = nullptr;
            voices_[c].held = false;
        }
    }

    void leaveRegisterMode()
    {
        if(!registerMode_)
            return;
        registerMode_ = false;
        rawCount_ = 0;
        for(auto &voice : voices_) {
            if(voice.dsp)
                mlapaula_voice_set_raw__ptr_struct_PaulaVoice_i32(voice.dsp, 0);
            voice.sample = nullptr;
        }
    }

    // A channel whose packets stopped (transport stopped, lane muted) stops.
    void expireChannels()
    {
        if(!registerMode_)
            return;
        const int64_t timeout = static_cast<int64_t>(0.3 * sampleRate_);
        for(int c = 0; c < kNumChannels; ++c) {
            if(cold_[c] || sampleClock_ - lastPacket_[c] < timeout)
                continue;
            bool pending = false;
            for(int f = 0; f < rawCount_; ++f)
                pending = pending || raw_[f].channel == c;
            if(pending)
                continue;
            cold_[c] = true;
            if(voices_[c].dsp)
                mlapaula_voice_raw_dma__ptr_struct_PaulaVoice_i32(voices_[c].dsp, 0);
        }
    }

    void applyFrame(const RawFrame &frame)
    {
        Voice &voice = voices_[frame.channel];
        if(!registerMode_ || voice.dsp == nullptr)
            return;
        if(frame.snapshot && !cold_[frame.channel])
            return;
        cold_[frame.channel] = false;
        const Sample *chip = active_[0];
        if(chip == nullptr)
            return;
        voice.sample = chip;
        const uint8 *p = frame.ops;
        for(int i = 0; i < frame.count;) {
            const int op = p[i];
            const int value = op <= 3 ? (p[i + 1] | (p[i + 2] << 7) | (p[i + 3] << 14)) : op == 4 ? p[i + 1] : 0;
            switch(op) {
                case 1:
                    mlapaula_voice_raw_location__ptr_struct_PaulaVoice_f64(
                        voice.dsp, value == 0x1FFFFF ? -1e9 : static_cast<double>(value));
                    break;
                case 2: mlapaula_voice_raw_length__ptr_struct_PaulaVoice_i32(voice.dsp, value & 0xFFFF); break;
                case 3: mlapaula_voice_raw_period__ptr_struct_PaulaVoice_i32(voice.dsp, value & 0xFFFF); break;
                case 4: mlapaula_voice_raw_volume__ptr_struct_PaulaVoice_i32(voice.dsp, value); break;
                case 5: mlapaula_voice_raw_dma__ptr_struct_PaulaVoice_i32(voice.dsp, 1); break;
                case 6: mlapaula_voice_raw_dma__ptr_struct_PaulaVoice_i32(voice.dsp, 0); break;
            }
            i += op <= 3 ? 4 : op == 4 ? 2 : 1;
        }
    }

    void render(float *outL, float *outR, int32 from, int32 to)
    {
        if(from >= to || filter_ == nullptr)
            return;
        const float separation = static_cast<float>(norm(kSeparationParam));
        const float level = gainFromNorm(norm(kLevelParam)) / static_cast<float>(oversample_);
        // Each channel at half scale: two channels on one side reach full scale.
        float toLeft[kNumChannels], toRight[kNumChannels];
        for(int c = 0; c < kNumChannels; ++c) {
            const float side = static_cast<float>(kChannelSide[c]) * separation;
            toLeft[c] = 0.5f * (1.0f - side) * 0.5f;
            toRight[c] = 0.5f * (1.0f + side) * 0.5f;
        }
        for(int32 i = from; i < to; ++i) {
            while(rawCount_ > 0 && raw_[0].at <= sampleClock_ + i)
                applyFrame(popFrame());
            float sumL = 0.0f, sumR = 0.0f;
            for(int k = 0; k < oversample_; ++k) {
                float left = 0.0f, right = 0.0f;
                for(int c = 0; c < kNumChannels; ++c) {
                    Voice &voice = voices_[c];
                    if(!voice.dsp || !voice.sample)
                        continue;
                    if(!mlapaula_voice_is_active__ptr_struct_PaulaVoice(voice.dsp)) {
                        voice.sample = nullptr;
                        continue;
                    }
                    const int64_t last = voice.sample->frames; // the guard word
                    // Outside the sample (chip memory) reads silence.
                    const auto at = [&](double frame) {
                        const int64_t index = static_cast<int64_t>(frame);
                        return index >= 0 && index < last ? voice.sample->words[static_cast<size_t>(index)] : 0.0f;
                    };
                    const float word0 = at(mlapaula_voice_frame__ptr_struct_PaulaVoice(voice.dsp));
                    const float word1 = at(mlapaula_voice_next_frame__ptr_struct_PaulaVoice(voice.dsp));
                    const float value = mlapaula_voice_render__ptr_struct_PaulaVoice_f32_f32(voice.dsp, word0, word1);
                    left += value * toLeft[c];
                    right += value * toRight[c];
                }
                float filteredR = 0.0f;
                sumL += mlapaula_filter_process__ptr_struct_PaulaFilter_f32_f32_ptr_f32(filter_, left, right, &filteredR);
                sumR += filteredR;
            }
            outL[i] = sumL * level;
            outR[i] = sumR * level;
        }
    }
};

} // namespace mla_paula

BEGIN_FACTORY_DEF("MLang", "https://github.com/mattilaa/mlang", "mailto:devnull@example.invalid")
DEF_CLASS2(INLINE_UID_FROM_FUID(mla_paula::kProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass,
           "Mla Paula", 0, PlugType::kInstrumentSampler, "0.1.0", kVstVersionString,
           mla_paula::Processor::createInstance)
END_FACTORY
