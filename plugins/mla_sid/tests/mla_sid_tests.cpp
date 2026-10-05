// Offline functional tests for the Mla SID bundle.
//
// Loads the built .vst3 through the SDK hosting classes, plays notes and
// checks rendered audio: bus layout, silence, pitch and pitch bend, sample-
// accurate notes, the SID's envelope times and its ADSR bug, waveforms, the
// filter, register writes from MIDI CCs, the four play modes, glide,
// vibrato, sync and ring modulation, the filter and V3 modulation, velocity
// and state round trips.
//
// Usage: mla_sid_tests <path/to/MlaSid.vst3> [wav output directory]
// With a directory, a few example sounds are also written there as WAV files.

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

int failures = 0;
#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if(!(condition)) {                                                                     \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);          \
            ++failures;                                                                        \
        }                                                                                      \
    } while(0)

constexpr double kRate = 48000.0;
constexpr int32 kBlock = 256;

// Parameter IDs (see plugin.cpp).
constexpr ParamID kOutput = 100, kChip = 101, kPlayMode = 103, kLink = 104, kGlide = 105, kBendParam = 107,
                  kVibrato = 108, kFilterEnv = 113, kV3Mod = 115, kV3ModAmount = 116, kHardRestart = 119,
                  kVelocity = 120, kCutoff = 121, kResonance = 122, kFilterMode = 123;
constexpr ParamID voice(int v, int field) { return static_cast<ParamID>(200 + 20 * v + field); }
constexpr int kWave = 0, kPulseWidth = 1, kAttack = 2, kDecay = 3, kSustain = 4, kRelease = 5, kSync = 6,
              kRing = 7, kFilterField_ = 9, kKeys = 10, kTranspose = 11;
constexpr ParamID reg(int r) { return static_cast<ParamID>(300 + r); }

// Normalized values for stepped parameters.
double stepNorm(int value, int steps) { return static_cast<double>(value) / steps; }
double waveNorm(int nibble) { return stepNorm(nibble, 15); }
double nibbleNorm(int value) { return stepNorm(value, 15); }
double transposeNorm(int semitones) { return stepNorm(semitones + 24, 48); }
constexpr int kTri = 1, kSaw = 2, kPulse = 4, kNoise = 8;
constexpr double kOff = 0.0, kOn = 1.0;
// Route all three voices past the filter.
#define BARE {voice(0, kFilterField_), 0.0}, {voice(1, kFilterField_), 0.0}, {voice(2, kFilterField_), 0.0}
constexpr double kPoly = 0.0, kUnison = 1.0 / 3.0, kArp = 2.0 / 3.0, kChannels = 1.0;

double hzOf(int note) { return 440.0 * std::pow(2.0, (note - 69) / 12.0); }

class Application final : public HostApplication {
  public:
    tresult PLUGIN_API getName(String128 name) override
    {
        const char16_t text[] = u"mla_sid_tests";
        std::copy(std::begin(text), std::end(text), name);
        return kResultOk;
    }
};
Application application;

struct Stereo {
    std::vector<float> left, right;
};

struct Instance {
    VST3::Hosting::Module::Ptr module;
    IPtr<PlugProvider> provider;
    IPtr<IComponent> component;
    IPtr<IAudioProcessor> processor;
    HostProcessData data;
    EventList events{512};
    ParameterChanges changes{256};
    ProcessContext context{};
    std::vector<std::string> subCategories;
    int32 lastSilenceFlags = 0;

    bool open(const std::string &path)
    {
        std::string error;
        module = VST3::Hosting::Module::create(path, error);
        if(!module) {
            std::fprintf(stderr, "load failed: %s\n", error.c_str());
            return false;
        }
        const auto &factory = module->getFactory();
        factory.setHostContext(&application);
        for(const auto &info : factory.classInfos()) {
            if(info.category() != kVstAudioEffectClass)
                continue;
            subCategories = info.subCategories();
            provider = owned(new PlugProvider(factory, info, true));
            if(!provider->initialize())
                return false;
            component = provider->getComponentPtr();
            processor = U::cast<IAudioProcessor>(component);
            break;
        }
        if(!component || !processor)
            return false;
        SpeakerArrangement stereo = SpeakerArr::kStereo;
        if(processor->setBusArrangements(nullptr, 0, &stereo, 1) != kResultOk)
            return false;
        component->activateBus(kAudio, kOutput, 0, true);
        component->activateBus(kEvent, kInput, 0, true);
        ProcessSetup setup{kOffline, kSample32, kBlock, kRate};
        if(processor->setupProcessing(setup) != kResultOk || !data.prepare(*component, kBlock, kSample32))
            return false;
        context.sampleRate = kRate;
        data.processContext = &context;
        data.inputEvents = &events;
        data.inputParameterChanges = &changes;
        component->setActive(true);
        processor->setProcessing(true);
        return true;
    }

    ~Instance()
    {
        if(processor)
            processor->setProcessing(false);
        if(component)
            component->setActive(false);
        data.unprepare();
        processor.reset();
        component.reset();
        provider.reset();
    }

    void param(ParamID id, double value, int32 offset = 0)
    {
        int32 index = 0;
        if(auto *queue = changes.addParameterData(id, index))
            queue->addPoint(offset, value, index);
    }

    // A MIDI CC as a host sends it: through the plug-in's MIDI mapping.
    void cc(int number, int value, int32 offset = 0)
    {
        auto mapping = U::cast<IMidiMapping>(provider->getControllerPtr());
        ParamID id = 0;
        if(!mapping || mapping->getMidiControllerAssignment(0, 0, static_cast<CtrlNumber>(number), id) != kResultOk) {
            std::fprintf(stderr, "CC %d is not mapped\n", number);
            ++failures;
            return;
        }
        param(id, value / (number == kPitchBend ? 16383.0 : 127.0), offset);
    }

    void noteOn(int pitch, float velocity = 100.0f / 127.0f, int32 offset = 0, int16 channel = 0)
    {
        Event e{};
        e.type = Event::kNoteOnEvent;
        e.sampleOffset = offset;
        e.noteOn.channel = channel;
        e.noteOn.pitch = static_cast<int16>(pitch);
        e.noteOn.velocity = velocity;
        e.noteOn.noteId = -1;
        events.addEvent(e);
    }

    void noteOff(int pitch, int32 offset = 0, int16 channel = 0)
    {
        Event e{};
        e.type = Event::kNoteOffEvent;
        e.sampleOffset = offset;
        e.noteOff.channel = channel;
        e.noteOff.pitch = static_cast<int16>(pitch);
        e.noteOff.noteId = -1;
        events.addEvent(e);
    }

    // Render about `seconds` (whole blocks).
    Stereo render(double seconds) { return renderFrames(static_cast<int>(kRate * seconds + kBlock - 1) / kBlock * kBlock); }

    Stereo renderFrames(int frames)
    {
        Stereo out;
        for(int done = 0; done < frames; done += kBlock) {
            data.numSamples = kBlock;
            processor->process(data);
            events.clear();
            changes.clearQueue();
            const auto &bus = data.outputs[0];
            lastSilenceFlags = static_cast<int32>(bus.silenceFlags);
            for(int32 i = 0; i < kBlock; ++i) {
                out.left.push_back(bus.channelBuffers32[0][i]);
                out.right.push_back(bus.channelBuffers32[1][i]);
            }
        }
        return out;
    }
};

size_t at(double seconds) { return static_cast<size_t>(seconds * kRate); }

double rms(const std::vector<float> &x, size_t from = 0, size_t to = SIZE_MAX)
{
    to = std::min(to, x.size());
    if(from >= to)
        return 0.0;
    double sum = 0.0;
    for(size_t i = from; i < to; ++i)
        sum += static_cast<double>(x[i]) * x[i];
    return std::sqrt(sum / static_cast<double>(to - from));
}

double peak(const std::vector<float> &x, size_t from = 0, size_t to = SIZE_MAX)
{
    double p = 0.0;
    for(size_t i = from; i < std::min(to, x.size()); ++i)
        p = std::max(p, static_cast<double>(std::fabs(x[i])));
    return p;
}

bool finite(const Stereo &s)
{
    for(size_t i = 0; i < s.left.size(); ++i)
        if(!std::isfinite(s.left[i]) || !std::isfinite(s.right[i]))
            return false;
    return true;
}

// Mean frequency from upward zero crossings in x[from, to).
double zeroCrossingHz(const std::vector<float> &x, size_t from, size_t to)
{
    double first = -1.0, last = -1.0;
    int crossings = 0;
    for(size_t i = from + 1; i < std::min(to, x.size()); ++i) {
        if(x[i - 1] < 0.0f && x[i] >= 0.0f) {
            const double t = static_cast<double>(i - 1) + x[i - 1] / (x[i - 1] - x[i]);
            if(first < 0.0)
                first = t;
            else
                ++crossings;
            last = t;
        }
    }
    return crossings > 0 ? kRate * crossings / (last - first) : 0.0;
}

// Amplitude of the `hz` component in x[from, to) (Hann-windowed DFT bin).
double tone(const std::vector<float> &x, size_t from, size_t to, double hz)
{
    to = std::min(to, x.size());
    double re = 0.0, im = 0.0, weights = 0.0;
    const double n = static_cast<double>(to - from);
    for(size_t i = from; i < to; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * (i - from) / (n - 1.0));
        const double phase = 2.0 * M_PI * hz * static_cast<double>(i) / kRate;
        re += w * x[i] * std::cos(phase);
        im += w * x[i] * std::sin(phase);
        weights += w;
    }
    return 2.0 * std::sqrt(re * re + im * im) / weights;
}

// Share of the energy above `hz` (one-pole split; rough but monotonic).
double highShare(const std::vector<float> &x, size_t from, size_t to, double hz)
{
    const double a = std::exp(-2.0 * M_PI * hz / kRate);
    double low = 0.0, total = 0.0, high = 0.0;
    for(size_t i = from; i < std::min(to, x.size()); ++i) {
        low = (1.0 - a) * x[i] + a * low;
        const double h = x[i] - low;
        high += h * h;
        total += static_cast<double>(x[i]) * x[i];
    }
    return total > 0.0 ? high / total : 0.0;
}

// Seconds from `from` until the 1 ms RMS first reaches `level`; -1 if never.
double onset(const std::vector<float> &x, size_t from, double level)
{
    const size_t window = at(0.001);
    for(size_t i = from; i + window <= x.size(); i += window / 4)
        if(rms(x, i, i + window) >= level)
            return static_cast<double>(i - from) / kRate;
    return -1.0;
}

using Params = std::initializer_list<std::pair<ParamID, double>>;

// A held note in a fresh instance with `params`.
Stereo play(const std::string &path, int note, double seconds, Params params = {}, float velocity = 1.0f)
{
    Instance plugin;
    CHECK(plugin.open(path));
    for(const auto &p : params)
        plugin.param(p.first, p.second);
    plugin.noteOn(note, velocity);
    return plugin.render(seconds);
}

void writeWav(const std::string &file, const std::vector<float> &x)
{
    FILE *f = std::fopen(file.c_str(), "wb");
    if(!f)
        return;
    const uint32_t rate = static_cast<uint32_t>(kRate), bytes = static_cast<uint32_t>(x.size() * 2);
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
    std::fwrite("data", 1, 4, f); u32(bytes);
    for(float v : x) {
        const int16_t s = static_cast<int16_t>(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f));
        std::fwrite(&s, 2, 1, f);
    }
    std::fclose(f);
}

void testLayout(const std::string &path)
{
    Instance plugin;
    CHECK(plugin.open(path));
    const auto &cats = plugin.subCategories;
    CHECK(std::find(cats.begin(), cats.end(), "Instrument") != cats.end());
    CHECK(std::find(cats.begin(), cats.end(), "Synth") != cats.end());
    CHECK(plugin.component->getBusCount(kAudio, kInput) == 0);
    CHECK(plugin.component->getBusCount(kAudio, kOutput) == 1);
    CHECK(plugin.component->getBusCount(kEvent, kInput) == 1);
    SpeakerArrangement mono = SpeakerArr::kMono, stereo = SpeakerArr::kStereo;
    CHECK(plugin.processor->setBusArrangements(nullptr, 0, &stereo, 1) == kResultTrue);
    CHECK(plugin.processor->setBusArrangements(nullptr, 0, &mono, 1) == kResultFalse);
    // Keyboard knobs and every register have a CC.
    auto mapping = U::cast<IMidiMapping>(plugin.provider->getControllerPtr());
    CHECK(mapping);
    ParamID id = 0;
    for(int cc : {1, 5, 7, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, static_cast<int>(kPitchBend)})
        CHECK(mapping->getMidiControllerAssignment(0, 0, static_cast<CtrlNumber>(cc), id) == kResultOk);
    for(int r = 0; r < 25; ++r) {
        CHECK(mapping->getMidiControllerAssignment(0, 3, static_cast<CtrlNumber>(20 + r), id) == kResultOk);
        CHECK(id == reg(r));
    }
    CHECK(mapping->getMidiControllerAssignment(0, 0, 45, id) == kResultFalse);
}

void testSilence(const std::string &path)
{
    // Nothing sounds without notes; a stray note-off changes nothing. The
    // 6581's mixer DC does not thump when the plug-in starts.
    Instance plugin;
    CHECK(plugin.open(path));
    plugin.noteOff(60);
    const Stereo out = plugin.render(0.5);
    CHECK(finite(out));
    CHECK(peak(out.left) < 1e-6 && peak(out.right) < 1e-6);
    // A note rings, then after its release the block is exact silence.
    plugin.noteOn(60);
    const Stereo note = plugin.render(0.3);
    CHECK(peak(note.left) > 0.05);
    CHECK(note.left == note.right);
    plugin.noteOff(60);
    plugin.render(1.5);
    const Stereo after = plugin.render(0.1);
    CHECK(plugin.lastSilenceFlags == 3);
    CHECK(peak(after.left) == 0.0);
}

void testPitch(const std::string &path)
{
    // The triangle plays the note's pitch (A4 = 440 Hz) on the 16-bit FREQ
    // register's grid, across the range.
    for(int note : {45, 57, 69, 81, 93}) {
        const Stereo out = play(path, note, 0.5, {{voice(0, kWave), waveNorm(kTri)}});
        const double hz = zeroCrossingHz(out.left, at(0.1), at(0.5));
        std::printf("note %d: %.2f Hz (expected %.2f)\n", note, hz, hzOf(note));
        CHECK(std::fabs(hz / hzOf(note) - 1.0) < 0.003);
    }
    // Bend up a whole tone (default range 2 semitones), then down.
    Instance plugin;
    CHECK(plugin.open(path));
    plugin.param(voice(0, kWave), waveNorm(kTri));
    plugin.noteOn(69);
    plugin.render(0.1);
    plugin.cc(kPitchBend, 16383);
    const Stereo up = plugin.render(0.4);
    CHECK(std::fabs(zeroCrossingHz(up.left, at(0.05), at(0.4)) / hzOf(71) - 1.0) < 0.003);
    plugin.cc(kPitchBend, 0);
    const Stereo down = plugin.render(0.4);
    CHECK(std::fabs(zeroCrossingHz(down.left, at(0.05), at(0.4)) / hzOf(67) - 1.0) < 0.003);
}

void testStartOffset(const std::string &path)
{
    Instance plugin;
    CHECK(plugin.open(path));
    plugin.noteOn(69, 1.0f, 100);
    const Stereo out = plugin.renderFrames(kBlock * 8);
    CHECK(peak(out.left, 0, 100) < 1e-6);
    CHECK(peak(out.left, 100, 400) > 0.02);
}

void testEnvelope(const std::string &path)
{
    // Attack 9 is the SID's 250 ms: a quarter of the way at 60 ms, full at
    // 250 ms. Attack 0 (2 ms) is loud at once.
    const Stereo slow = play(path, 57, 0.6, {{voice(0, kAttack), nibbleNorm(9)}, {voice(0, kSustain), nibbleNorm(15)}});
    const double full = rms(slow.left, at(0.4), at(0.5));
    const double early = rms(slow.left, at(0.05), at(0.07));
    std::printf("attack 250 ms: %.2f of full at 60 ms, %.2f at 240 ms\n", early / full,
                rms(slow.left, at(0.23), at(0.25)) / full);
    CHECK(early / full > 0.12 && early / full < 0.4);
    CHECK(rms(slow.left, at(0.23), at(0.25)) / full > 0.85);
    const Stereo fast = play(path, 57, 0.3, {{voice(0, kSustain), nibbleNorm(15)}});
    CHECK(rms(fast.left, at(0.005), at(0.015)) / rms(fast.left, at(0.2), at(0.3)) > 0.85);

    // Decay 9 (750 ms) falls to sustain 5 (a third of full).
    const Stereo decay = play(path, 57, 1.5, {{voice(0, kDecay), nibbleNorm(9)}, {voice(0, kSustain), nibbleNorm(5)}});
    const double top = rms(decay.left, at(0.003), at(0.02));
    const double held = rms(decay.left, at(1.2), at(1.4));
    std::printf("sustain 5: %.3f of the peak\n", held / top);
    CHECK(std::fabs(held / top - 5.0 / 15.0) < 0.04);
    CHECK(rms(decay.left, at(0.05), at(0.1)) > held * 1.5);

    // Release 8 (300 ms): halfway down in well under 300 ms (exponential),
    // and gone in about 300 ms.
    Instance plugin;
    CHECK(plugin.open(path));
    plugin.param(voice(0, kSustain), nibbleNorm(15));
    plugin.param(voice(0, kRelease), nibbleNorm(8));
    plugin.noteOn(57);
    const double level = rms(plugin.render(0.3).left, at(0.2), at(0.3));
    plugin.noteOff(57);
    const Stereo tail = plugin.render(0.6);
    std::printf("release 300 ms: %.3f at 50 ms, %.4f at 350 ms\n", rms(tail.left, at(0.04), at(0.06)) / level,
                rms(tail.left, at(0.34), at(0.36)) / level);
    CHECK(rms(tail.left, at(0.04), at(0.06)) / level < 0.6);
    CHECK(rms(tail.left, at(0.34), at(0.36)) / level < 0.01);
}

void testAdsrBug(const std::string &path)
{
    // After a long release, a new note's 2 ms attack waits for the 15-bit
    // rate counter to wrap: the SID's ADSR bug. Hard restart (the default)
    // starts it at once, as C64 players do.
    // The first note decays to zero at once (decay 0, sustain 0); its release
    // (15: a 31251-cycle rate period) then counts the rate counter far past
    // attack 0's 9 cycles before the next note.
    auto attack = [&](double hardRestart) {
        Instance plugin;
        CHECK(plugin.open(path));
        plugin.param(kHardRestart, hardRestart);
        plugin.param(voice(0, kDecay), 0.0);
        plugin.param(voice(0, kSustain), 0.0);
        plugin.param(voice(0, kRelease), nibbleNorm(15));
        plugin.noteOn(57);
        plugin.render(0.1);
        plugin.noteOff(57);
        plugin.render(0.02);
        plugin.param(voice(0, kSustain), nibbleNorm(15));
        plugin.noteOn(57);
        const Stereo out = plugin.render(0.1);
        const double full = rms(out.left, at(0.06), at(0.1));
        return onset(out.left, 0, full * 0.9);
    };
    const double fixedOnset = attack(kOn), buggyOnset = attack(kOff);
    std::printf("attack after release: %.1f ms with hard restart, %.1f ms without\n", fixedOnset * 1000.0,
                buggyOnset * 1000.0);
    CHECK(fixedOnset >= 0.0 && fixedOnset < 0.004);
    CHECK(buggyOnset > fixedOnset + 0.004);
}

void testWaveforms(const std::string &path, const char *wavDir)
{
    // Brightness rises from triangle to sawtooth to pulse; noise is noise;
    // "Off" is silent. (Past the filter, so the waveforms are bare.)
    auto shape = [&](int nibble) {
        return play(path, 57, 0.4, {{voice(0, kWave), waveNorm(nibble)}, BARE, {voice(0, kSustain), nibbleNorm(15)}});
    };
    const Stereo tri = shape(kTri), saw = shape(kSaw), pulse = shape(kPulse), noise = shape(kNoise), off = shape(0);
    const double hTri = highShare(tri.left, at(0.1), at(0.4), 2000.0);
    const double hSaw = highShare(saw.left, at(0.1), at(0.4), 2000.0);
    const double hPulse = highShare(pulse.left, at(0.1), at(0.4), 2000.0);
    const double hNoise = highShare(noise.left, at(0.1), at(0.4), 2000.0);
    std::printf("energy above 2 kHz: tri %.3f, saw %.3f, pulse %.3f, noise %.3f\n", hTri, hSaw, hPulse, hNoise);
    // (Noise is clocked by the note: at A3 its band reaches about 3.5 kHz.)
    CHECK(hTri < hSaw && hSaw < hNoise);
    CHECK(hPulse > hTri);
    CHECK(rms(noise.left, at(0.1), at(0.4)) > 0.03);
    CHECK(rms(off.left, at(0.1), at(0.4)) < 1e-3);
    // The saw's partials fall as 1/n.
    const double h1 = tone(saw.left, at(0.1), at(0.4), hzOf(57)), h3 = tone(saw.left, at(0.1), at(0.4), 3 * hzOf(57));
    std::printf("saw: 3rd partial %.3f of the fundamental\n", h3 / h1);
    CHECK(std::fabs(h3 / h1 - 1.0 / 3.0) < 0.06);
    // A 25 % pulse has no 4th partial but a strong 2nd.
    const Stereo narrow = play(path, 57, 0.4, {BARE, {voice(0, kPulseWidth), 1024.0 / 4095.0}, {voice(0, kSustain), nibbleNorm(15)}});
    CHECK(tone(narrow.left, at(0.1), at(0.4), 4 * hzOf(57)) < 0.05 * tone(narrow.left, at(0.1), at(0.4), hzOf(57)));
    CHECK(tone(narrow.left, at(0.1), at(0.4), 2 * hzOf(57)) > 0.3 * tone(narrow.left, at(0.1), at(0.4), hzOf(57)));
    if(wavDir) {
        writeWav(std::string(wavDir) + "/mlasid_tri.wav", tri.left);
        writeWav(std::string(wavDir) + "/mlasid_saw.wav", saw.left);
        writeWav(std::string(wavDir) + "/mlasid_pulse.wav", pulse.left);
        writeWav(std::string(wavDir) + "/mlasid_noise.wav", noise.left);
    }
}

void testFilter(const std::string &path)
{
    // Cutoff (CC 74) darkens a sawtooth on both chips; resonance (CC 71)
    // boosts around the cutoff.
    for(double chip : {0.0, 1.0}) {
        auto saw = [&](int cutoffCc, int resonanceCc) {
            Instance plugin;
            CHECK(plugin.open(path));
            plugin.param(kChip, chip);
            plugin.param(voice(0, kWave), waveNorm(kSaw));
            plugin.param(voice(0, kSustain), nibbleNorm(15));
            plugin.cc(74, cutoffCc);
            plugin.cc(71, resonanceCc);
            plugin.noteOn(45);
            return plugin.render(0.4);
        };
        const Stereo open = saw(127, 0), closed = saw(10, 0), resonant = saw(10, 127);
        const double hOpen = highShare(open.left, at(0.1), at(0.4), 1500.0);
        const double hClosed = highShare(closed.left, at(0.1), at(0.4), 1500.0);
        std::printf("%s: above 1.5 kHz open %.3f, cutoff CC 10 %.3f; resonance %.2fx level\n",
                    chip == 0.0 ? "6581" : "8580", hOpen, hClosed,
                    rms(resonant.left, at(0.1), at(0.4)) / rms(closed.left, at(0.1), at(0.4)));
        CHECK(hClosed < hOpen * 0.5);
        CHECK(rms(resonant.left, at(0.1), at(0.4)) > rms(closed.left, at(0.1), at(0.4)) * 1.05);
    }
    // High-pass removes the fundamental; filter mode Off with the voice
    // routed to the filter is silent, as on the chip.
    const Stereo hp = play(path, 45, 0.4, {{voice(0, kWave), waveNorm(kSaw)}, {kFilterMode, 4.0 / 7.0}, {kCutoff, 0.5}});
    const Stereo lp = play(path, 45, 0.4, {{voice(0, kWave), waveNorm(kSaw)}, {kFilterMode, 1.0 / 7.0}, {kCutoff, 0.5}});
    CHECK(tone(hp.left, at(0.1), at(0.4), hzOf(45)) < 0.2 * tone(lp.left, at(0.1), at(0.4), hzOf(45)));
    const Stereo none = play(path, 45, 0.4, {{kFilterMode, 0.0}, {voice(0, kFilterField_), 1.0}});
    CHECK(rms(none.left, at(0.1), at(0.4)) < 1e-3);
}

void testRegisterCcs(const std::string &path)
{
    // CC 20 + r writes register $D400 + r, mostly with the CC as its top
    // seven bits.
    Instance plugin;
    CHECK(plugin.open(path));
    plugin.param(kFilterMode, 0.0);
    plugin.param(voice(0, kFilterField_), 0.0);
    // $D406 = $F1 (sustain 15, release 1): the note holds at its peak.
    plugin.cc(26, 0xF0 >> 1);
    plugin.noteOn(57);
    const Stereo held = plugin.render(0.5);
    CHECK(rms(held.left, at(0.3), at(0.5)) > 0.9 * rms(held.left, at(0.005), at(0.02)));
    // $D404 = $10 (triangle): CC 24 = 8; the gate stays the keys'.
    plugin.cc(24, 0x10 >> 1);
    const Stereo tri = plugin.render(0.3);
    CHECK(highShare(tri.left, at(0.05), at(0.3), 2000.0) < 0.02);
    CHECK(std::fabs(zeroCrossingHz(tri.left, at(0.05), at(0.3)) / hzOf(57) - 1.0) < 0.003);
    // $D404 = $80 (noise): CC 24 = 64.
    plugin.cc(24, 0x80 >> 1);
    const Stereo noise = plugin.render(0.3);
    CHECK(highShare(noise.left, at(0.05), at(0.3), 2000.0) > 2.0 * highShare(tri.left, at(0.05), at(0.3), 2000.0));
    // $D418 = $00: volume 0 silences everything.
    plugin.cc(24, 0x20 >> 1);
    plugin.cc(44, 0);
    const Stereo quiet = plugin.render(0.3);
    CHECK(rms(quiet.left, at(0.1), at(0.3)) < 1e-3);
    // $D418 = $1F: LP, volume 15 (CC 15: volume bit 0 copies bit 3).
    plugin.cc(44, 0x1F >> 1);
    plugin.param(voice(0, kFilterField_), 1.0);
    const Stereo loud = plugin.render(0.3);
    std::printf("register CCs: sawtooth at volume 15 through LP, rms %.3f\n", rms(loud.left, at(0.1), at(0.3)));
    CHECK(rms(loud.left, at(0.1), at(0.3)) > 0.05);
    // FC Hi ($D416) low: the filtered voice darkens.
    plugin.cc(42, 4);
    const Stereo dark = plugin.render(0.3);
    CHECK(highShare(dark.left, at(0.1), at(0.3), 1500.0) < highShare(loud.left, at(0.1), at(0.3), 1500.0) * 0.5);
    // Res/Filt ($D417) = resonance x 8 + routing: CC 0 takes voice 1 out of
    // the filter, so it is bright again.
    plugin.cc(43, 0);
    const Stereo direct = plugin.render(0.3);
    CHECK(highShare(direct.left, at(0.1), at(0.3), 1500.0) > highShare(dark.left, at(0.1), at(0.3), 1500.0) * 2.0);
    // Registers write sample-accurately.
    // (On the 8580: the 6581's volume writes click, as they should.)
    Instance timed;
    CHECK(timed.open(path));
    timed.param(kChip, 1.0);
    timed.noteOn(57);
    timed.render(0.2);
    timed.cc(44, 0, 128);
    const Stereo cut = timed.renderFrames(kBlock);
    CHECK(rms(cut.left, 0, 120) > 0.1);
    CHECK(peak(cut.left, 200, kBlock) < 0.05);
}

void testPoly(const std::string &path)
{
    // Three notes take the three voices; a fourth steals the oldest.
    Instance plugin;
    CHECK(plugin.open(path));
    plugin.param(voice(0, kWave), waveNorm(kTri));
    plugin.param(voice(0, kSustain), nibbleNorm(15));
    plugin.noteOn(69);
    plugin.render(0.05);
    plugin.noteOn(73);
    plugin.render(0.05);
    plugin.noteOn(76);
    const Stereo chord = plugin.render(0.4);
    for(int note : {69, 73, 76})
        CHECK(tone(chord.left, at(0.1), at(0.4), hzOf(note)) > 0.03);
    plugin.noteOn(81);
    const Stereo stolen = plugin.render(0.4);
    std::printf("poly: A4 %.3f -> %.3f after the 4th note\n", tone(chord.left, at(0.1), at(0.4), hzOf(69)),
                tone(stolen.left, at(0.1), at(0.4), hzOf(69)));
    CHECK(tone(stolen.left, at(0.1), at(0.4), hzOf(81)) > 0.03);
    CHECK(tone(stolen.left, at(0.1), at(0.4), hzOf(69)) < 0.1 * tone(chord.left, at(0.1), at(0.4), hzOf(69)));
    CHECK(tone(stolen.left, at(0.1), at(0.4), hzOf(76)) > 0.03);
}

void testUnisonAndGlide(const std::string &path)
{
    // Unison: one note plays every voice, each with its transpose.
    const Stereo stack = play(path, 57, 0.4,
                              {{kPlayMode, kUnison}, {kLink, kOff}, {voice(0, kWave), waveNorm(kTri)},
                               {voice(1, kWave), waveNorm(kTri)}, {voice(2, kWave), waveNorm(kTri)},
                               {voice(1, kTranspose), transposeNorm(12)}, {voice(2, kTranspose), transposeNorm(19)}});
    for(double hz : {hzOf(57), hzOf(69), hzOf(76)})
        CHECK(tone(stack.left, at(0.1), at(0.4), hz) > 0.03);

    // Legato with glide: the second note slides over about the glide time.
    Instance plugin;
    CHECK(plugin.open(path));
    plugin.param(kPlayMode, kUnison);
    plugin.param(voice(0, kWave), waveNorm(kTri));
    plugin.param(kGlide, std::sqrt(0.2 / 2.0)); // 0.2 s
    plugin.noteOn(57);
    plugin.render(0.2);
    plugin.noteOn(69);
    const Stereo slide = plugin.render(0.5);
    const double early = zeroCrossingHz(slide.left, at(0.02), at(0.06));
    const double late = zeroCrossingHz(slide.left, at(0.3), at(0.5));
    std::printf("glide: %.1f Hz at 40 ms, %.1f Hz at 0.4 s\n", early, late);
    CHECK(early > hzOf(57) * 1.05 && early < hzOf(69) * 0.95);
    CHECK(std::fabs(late / hzOf(69) - 1.0) < 0.003);
    // Letting go of the top note returns to the held one.
    plugin.noteOff(69);
    const Stereo back = plugin.render(0.6);
    CHECK(std::fabs(zeroCrossingHz(back.left, at(0.4), at(0.6)) / hzOf(57) - 1.0) < 0.003);
}

void testArp(const std::string &path, const char *wavDir)
{
    // Arp: a held chord cycles every 2 frames (40 ms at PAL).
    Instance plugin;
    CHECK(plugin.open(path));
    plugin.param(kPlayMode, kArp);
    plugin.param(voice(0, kSustain), nibbleNorm(15));
    plugin.param(voice(0, kWave), waveNorm(kTri));
    plugin.noteOn(57);
    plugin.noteOn(64);
    plugin.noteOn(69);
    const Stereo out = plugin.render(1.0);
    int counts[3] = {0, 0, 0};
    const int notes[3] = {57, 64, 69};
    const size_t window = at(0.02);
    for(size_t from = at(0.1); from + window <= out.left.size(); from += window) {
        double best = 0.0;
        int which = 0;
        for(int k = 0; k < 3; ++k) {
            const double a = tone(out.left, from, from + window, hzOf(notes[k]));
            if(a > best)
                best = a, which = k;
        }
        ++counts[which];
    }
    std::printf("arp: %d/%d/%d windows on each note\n", counts[0], counts[1], counts[2]);
    for(int c : counts)
        CHECK(c >= 8);
    if(wavDir)
        writeWav(std::string(wavDir) + "/mlasid_arp.wav", out.left);
}

void testChannels(const std::string &path)
{
    // Channels: MIDI channel 1 plays voice 1, channel 2 voice 2; each voice
    // is mono.
    Instance plugin;
    CHECK(plugin.open(path));
    plugin.param(kPlayMode, kChannels);
    plugin.param(voice(0, kWave), waveNorm(kTri));
    plugin.param(voice(0, kSustain), nibbleNorm(15));
    plugin.noteOn(57, 1.0f, 0, 0);
    plugin.noteOn(64, 1.0f, 0, 1);
    const Stereo both = plugin.render(0.4);
    CHECK(tone(both.left, at(0.1), at(0.4), hzOf(57)) > 0.03);
    CHECK(tone(both.left, at(0.1), at(0.4), hzOf(64)) > 0.03);
    plugin.noteOn(60, 1.0f, 0, 0);
    const Stereo moved = plugin.render(0.4);
    CHECK(tone(moved.left, at(0.1), at(0.4), hzOf(57)) < 0.01);
    CHECK(tone(moved.left, at(0.1), at(0.4), hzOf(60)) > 0.03);
    CHECK(tone(moved.left, at(0.1), at(0.4), hzOf(64)) > 0.03);
}

void testSyncAndRing(const std::string &path, const char *wavDir)
{
    // Hard sync: voice 2's sawtooth, a fourth up, restarts with voice 1, so
    // the sound has voice 1's period. Voice 1 is silent (waveform off).
    auto synced = [&](double sync) {
        return play(path, 57, 0.5,
                    {{kPlayMode, kUnison}, {kLink, kOff}, BARE, {voice(0, kWave), 0.0},
                     {voice(1, kWave), waveNorm(kSaw)}, {voice(1, kSync), sync}, {voice(1, kTranspose), transposeNorm(5)},
                     {voice(1, kSustain), nibbleNorm(15)}, {voice(2, kKeys), kOff}, {voice(2, kWave), 0.0}});
    };
    const Stereo free = synced(kOff), hard = synced(kOn);
    const double fourth = hzOf(62);
    std::printf("sync: the 4th's partial %.3f free, %.3f synced\n", tone(free.left, at(0.1), at(0.5), fourth),
                tone(hard.left, at(0.1), at(0.5), fourth));
    CHECK(tone(free.left, at(0.1), at(0.5), fourth) > 0.05);
    CHECK(tone(hard.left, at(0.1), at(0.5), fourth) < 0.2 * tone(free.left, at(0.1), at(0.5), fourth));
    CHECK(tone(hard.left, at(0.1), at(0.5), hzOf(57)) > 0.03);

    // Ring modulation: voice 1's triangle by voice 3 (a fifth up, silent)
    // makes sum and difference tones that neither voice plays.
    auto ringed = [&](double ring) {
        return play(path, 57, 0.5,
                    {{kPlayMode, kUnison}, {kLink, kOff}, BARE, {voice(0, kWave), waveNorm(kTri)},
                     {voice(0, kRing), ring}, {voice(0, kSustain), nibbleNorm(15)}, {voice(1, kKeys), kOff},
                     {voice(1, kWave), 0.0}, {voice(2, kWave), 0.0}, {voice(2, kTranspose), transposeNorm(7)}});
    };
    const Stereo plain = ringed(kOff), ring = ringed(kOn);
    const double sum = hzOf(57) + hzOf(64);
    std::printf("ring: sum tone %.3f plain, %.3f ringed\n", tone(plain.left, at(0.1), at(0.5), sum),
                tone(ring.left, at(0.1), at(0.5), sum));
    CHECK(tone(ring.left, at(0.1), at(0.5), sum) > 5.0 * tone(plain.left, at(0.1), at(0.5), sum));
    if(wavDir) {
        writeWav(std::string(wavDir) + "/mlasid_sync.wav", hard.left);
        writeWav(std::string(wavDir) + "/mlasid_ring.wav", ring.left);
    }
}

void testModulation(const std::string &path)
{
    // Vibrato from the mod wheel: the pitch moves; without it, it does not.
    auto spread = [&](int wheel) {
        Instance plugin;
        CHECK(plugin.open(path));
        plugin.param(voice(0, kWave), waveNorm(kTri));
        plugin.cc(1, wheel);
        plugin.noteOn(69);
        const Stereo out = plugin.render(1.0);
        double low = 1e9, high = 0.0;
        for(size_t from = at(0.2); from + at(0.04) <= out.left.size(); from += at(0.04)) {
            const double hz = zeroCrossingHz(out.left, from, from + at(0.04));
            low = std::min(low, hz);
            high = std::max(high, hz);
        }
        return high / low;
    };
    std::printf("vibrato: pitch spread %.4f without, %.4f with the wheel up\n", spread(0), spread(127));
    CHECK(spread(0) < 1.003);
    CHECK(spread(127) > 1.04);

    // Filter envelope: a closed filter opens on the note and closes again.
    const Stereo env = play(path, 45, 0.8, {{voice(0, kWave), waveNorm(kSaw)}, {voice(0, kSustain), nibbleNorm(15)},
                                            {kCutoff, 0.0}, {kFilterEnv, 1.0}});
    CHECK(highShare(env.left, at(0.0), at(0.05), 1500.0) > 2.0 * highShare(env.left, at(0.6), at(0.8), 1500.0));

    // V3 Mod: ENV3 opens the filter like a C64 player reading $D41C (in
    // Unison, so voice 3 plays the note too).
    const Stereo flat = play(path, 45, 0.4, {{kPlayMode, kUnison}, {voice(0, kWave), waveNorm(kSaw)}, {kCutoff, 0.0},
                                             {voice(0, kSustain), nibbleNorm(15)}});
    const Stereo v3 = play(path, 45, 0.4, {{kPlayMode, kUnison}, {voice(0, kWave), waveNorm(kSaw)}, {kCutoff, 0.0},
                                           {voice(0, kSustain), nibbleNorm(15)}, {kV3Mod, 1.0}, {kV3ModAmount, 1.0}});
    CHECK(highShare(v3.left, at(0.1), at(0.4), 1500.0) > 2.0 * highShare(flat.left, at(0.1), at(0.4), 1500.0));

    // Velocity: at 100 %, a soft note is quieter; at 0 % (the SID's way)
    // every note is alike.
    const Stereo soft = play(path, 57, 0.3, {{kVelocity, 1.0}}, 0.25f);
    const Stereo hard = play(path, 57, 0.3, {{kVelocity, 1.0}}, 1.0f);
    const Stereo flatSoft = play(path, 57, 0.3, {}, 0.25f);
    CHECK(rms(soft.left, at(0.1), at(0.3)) < 0.4 * rms(hard.left, at(0.1), at(0.3)));
    CHECK(std::fabs(rms(flatSoft.left, at(0.1), at(0.3)) / rms(hard.left, at(0.1), at(0.3)) - 1.0) < 0.05);
}

void testState(const std::string &path)
{
    Instance a, b;
    CHECK(a.open(path) && b.open(path));
    a.param(voice(0, kWave), waveNorm(kSaw));
    a.param(kCutoff, 0.3);
    a.param(kResonance, 0.8);
    a.param(kChip, 1.0);
    a.param(kOutput, 0.5);
    a.render(0.01);
    MemoryStream stream;
    CHECK(a.component->getState(&stream) == kResultOk);
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    CHECK(b.component->setState(&stream) == kResultOk);
    b.render(0.01); // The oscillators run freely: keep the two in step.
    MemoryStream again;
    CHECK(b.component->getState(&again) == kResultOk);
    CHECK(stream.getSize() == again.getSize());
    CHECK(std::memcmp(stream.getData(), again.getData(), static_cast<size_t>(stream.getSize())) == 0);
    // The restored state sounds the same.
    a.noteOn(57);
    b.noteOn(57);
    const Stereo x = a.render(0.2), y = b.render(0.2);
    CHECK(peak(x.left) > 0.01);
    CHECK(x.left == y.left);
    // Rejects foreign data.
    MemoryStream junk;
    int32 zero = 0;
    junk.write(&zero, sizeof(zero), nullptr);
    junk.seek(0, IBStream::kIBSeekSet, nullptr);
    CHECK(b.component->setState(&junk) == kResultFalse);
}

void testBounded(const std::string &path)
{
    // Everything at once stays finite and below clipping.
    Instance plugin;
    CHECK(plugin.open(path));
    plugin.param(kResonance, 1.0);
    plugin.param(kCutoff, 0.3);
    plugin.param(voice(0, kWave), waveNorm(kSaw | kPulse));
    for(int note : {36, 48, 55})
        plugin.noteOn(note, 1.0f);
    const Stereo out = plugin.render(1.0);
    CHECK(finite(out));
    CHECK(peak(out.left) < 1.0);
}

} // namespace

int main(int argc, char **argv)
{
    if(argc < 2) {
        std::fprintf(stderr, "usage: %s <MlaSid.vst3> [wav output directory]\n", argv[0]);
        return 2;
    }
    const std::string path = argv[1];
    const char *wavDir = argc > 2 ? argv[2] : nullptr;
    testLayout(path);
    testSilence(path);
    testPitch(path);
    testStartOffset(path);
    testEnvelope(path);
    testAdsrBug(path);
    testWaveforms(path, wavDir);
    testFilter(path);
    testRegisterCcs(path);
    testPoly(path);
    testUnisonAndGlide(path);
    testArp(path, wavDir);
    testChannels(path);
    testSyncAndRing(path, wavDir);
    testModulation(path);
    testState(path);
    testBounded(path);
    if(failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all Mla SID tests passed\n");
    return 0;
}
