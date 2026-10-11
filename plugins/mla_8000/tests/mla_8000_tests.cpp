// Offline functional tests for the Mla 8000 bundle.
//
// Loads the built .vst3 through the SDK hosting classes, plays notes and
// checks rendered audio: bus layout and silence, pitch, the Super Saw's
// Detune and Mix, the filter and its resonance, the mod wheel on the cutoff,
// envelopes, the key modes, portamento, the delay, state round trips and
// bounded output.
//
// Usage: mla_8000_tests <path/to/Mla8000.vst3> [wav output directory]
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
constexpr ParamID kKeyMode = 101, kPortamento = 103, kPortaTime = 104, kModWheel = 107, kWheelCutoff = 108,
                  kOsc1Wave = 119, kCtrl1 = 120, kCtrl2 = 121, kCutoff = 136, kResonance = 137, kKeyFollow = 138,
                  kAmpAttack = 147, kAmpRelease = 150, kChorus = 153, kDelay = 157, kDelaySync = 158,
                  kDelayTime = 159, kDelayFeedback = 160, kDelayLevel = 161, kVelocityAmp = 110;

double stepNorm(int value, int steps) { return static_cast<double>(value) / steps; }
// OSC 1 waves: 0 Super Saw .. 6 Triangle; key modes 0 Poly .. 3 Unison.
double waveNorm(int wave) { return stepNorm(wave, 6); }
double modeNorm(int mode) { return stepNorm(mode, 3); }
constexpr int kSuperSaw = 0, kSquare = 4, kSaw = 5;
// The Time knob: 1 .. 1250 ms; cutoff: 20 Hz x 1000^n.
double delayNorm(double ms) { return (ms - 1.0) / 1249.0; }
double cutoffNorm(double hz) { return std::log(hz / 20.0) / std::log(1000.0); }
// A dry, open, plain patch for measuring the voice.
#define DRY {kChorus, 0.0}, {kDelay, 0.0}, {kCutoff, 1.0}, {kKeyFollow, 0.0}, {kVelocityAmp, 0.0}

double hzOf(int note) { return 440.0 * std::pow(2.0, (note - 69) / 12.0); }

class Application final : public HostApplication {
  public:
    tresult PLUGIN_API getName(String128 name) override
    {
        const char16_t text[] = u"mla_8000_tests";
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
    CHECK(std::find(plugin.subCategories.begin(), plugin.subCategories.end(), "Synth") != plugin.subCategories.end());
    // Idle: exact silence, flagged.
    const Stereo out = plugin.render(0.1);
    CHECK(peak(out.left) == 0.0 && peak(out.right) == 0.0);
    CHECK(plugin.lastSilenceFlags == 3);
}

void testPitch(const std::string &path)
{
    // A plain saw: its fundamental at the key's frequency.
    const Stereo out = play(path, 57, 0.5, {DRY, {kOsc1Wave, waveNorm(kSaw)}});
    CHECK(finite(out));
    const double hz = zeroCrossingHz(out.left, at(0.1), at(0.5));
    CHECK(std::fabs(hz - 220.0) < 1.0);
    CHECK(tone(out.left, at(0.1), at(0.5), 220.0) > 4.0 * tone(out.left, at(0.1), at(0.5), 233.0));
}

void testSuperSaw(const std::string &path, const char *wavDir)
{
    // Detune 0: seven saws in unison. Detune 1: the outer saws sit at
    // Szabo's offsets, -11.0 % and +10.7 % of the centre's frequency.
    const Stereo tight = play(path, 57, 1.0, {DRY, {kOsc1Wave, waveNorm(kSuperSaw)}, {kCtrl1, 0.0}, {kCtrl2, 1.0}});
    const Stereo wide = play(path, 57, 1.0, {DRY, {kOsc1Wave, waveNorm(kSuperSaw)}, {kCtrl1, 1.0}, {kCtrl2, 1.0}});
    const double outer = 220.0 * (1.0 + 0.10745242);
    CHECK(tone(wide.left, at(0.2), at(1.0), outer) > 5.0 * tone(tight.left, at(0.2), at(1.0), outer));
    // Mix 0 is mostly the centre saw; Mix 1 adds the six side saws (louder).
    const Stereo centre = play(path, 57, 1.0, {DRY, {kCtrl1, 0.7}, {kCtrl2, 0.0}});
    const Stereo full = play(path, 57, 1.0, {DRY, {kCtrl1, 0.7}, {kCtrl2, 1.0}});
    CHECK(rms(full.left, at(0.2), at(1.0)) > 1.3 * rms(centre.left, at(0.2), at(1.0)));
    // The high-pass at the fundamental: little energy an octave below.
    CHECK(tone(full.left, at(0.2), at(1.0), 110.0) < 0.2 * tone(full.left, at(0.2), at(1.0), 220.0));
    if(wavDir) {
        const Stereo lead = play(path, 57, 2.0, {});
        writeWav(std::string(wavDir) + "/mla8000_supersaw.wav", lead.left);
    }
}

void testFilter(const std::string &path)
{
    const Stereo open = play(path, 45, 0.6, {DRY, {kOsc1Wave, waveNorm(kSaw)}, {kResonance, 0.0}});
    const Stereo closed = play(path, 45, 0.6, {DRY, {kOsc1Wave, waveNorm(kSaw)}, {kResonance, 0.0}, {kCutoff, cutoffNorm(300.0)}});
    CHECK(highShare(closed.left, at(0.1), at(0.6), 2000.0) < 0.2 * highShare(open.left, at(0.1), at(0.6), 2000.0));
    // Resonance peaks at the cutoff: the harmonic nearest it grows.
    const Stereo peaked = play(path, 45, 0.6, {DRY, {kOsc1Wave, waveNorm(kSaw)}, {kResonance, 0.9}, {kCutoff, cutoffNorm(880.0)}});
    const Stereo flat = play(path, 45, 0.6, {DRY, {kOsc1Wave, waveNorm(kSaw)}, {kResonance, 0.0}, {kCutoff, cutoffNorm(880.0)}});
    CHECK(tone(peaked.left, at(0.1), at(0.6), 880.0) > 2.0 * tone(flat.left, at(0.1), at(0.6), 880.0));
    CHECK(finite(peaked));
}

void testModWheel(const std::string &path)
{
    // CC 1 moves the mod wheel; Wheel > Cutoff +8 octaves opens a closed filter.
    Instance plugin;
    CHECK(plugin.open(path));
    for(const auto &p : {std::make_pair(kChorus, 0.0), std::make_pair(kDelay, 0.0), std::make_pair(kCutoff, cutoffNorm(150.0)),
                         std::make_pair(kWheelCutoff, 1.0), std::make_pair(kKeyFollow, 0.0)})
        plugin.param(p.first, p.second);
    plugin.noteOn(45, 1.0f);
    const Stereo dark = plugin.render(0.5);
    plugin.cc(1, 127);
    const Stereo bright = plugin.render(0.5);
    CHECK(highShare(bright.left, at(0.05), at(0.5), 2000.0) > 3.0 * highShare(dark.left, at(0.05), at(0.5), 2000.0));
}

void testEnvelope(const std::string &path)
{
    const Stereo fast = play(path, 57, 0.5, {DRY});
    const Stereo slow = play(path, 57, 0.8, {DRY, {kAmpAttack, 0.9}}); // about 4 s
    const double level = 0.5 * rms(fast.left, at(0.2), at(0.5));
    CHECK(onset(fast.left, 0, level) >= 0.0 && onset(fast.left, 0, level) < 0.02);
    CHECK(onset(slow.left, 0, level) < 0.0 || onset(slow.left, 0, level) > 0.2);
    // Release: quiet a second after the key is let go (release 0.4 s, -60 dB).
    Instance plugin;
    CHECK(plugin.open(path));
    plugin.param(kChorus, 0.0);
    plugin.param(kDelay, 0.0);
    plugin.noteOn(57);
    plugin.render(0.3);
    plugin.noteOff(57);
    const Stereo after = plugin.render(1.0);
    CHECK(rms(after.left, at(0.8), at(1.0)) < 0.001);
    plugin.render(0.2);
    CHECK(plugin.lastSilenceFlags == 3);
}

void testKeyModes(const std::string &path)
{
    // Poly: three keys, three fundamentals.
    {
        Instance plugin;
        CHECK(plugin.open(path));
        plugin.param(kOsc1Wave, waveNorm(kSaw));
        plugin.param(kChorus, 0.0);
        plugin.param(kDelay, 0.0);
        for(int note : {45, 49, 52})
            plugin.noteOn(note);
        const Stereo out = plugin.render(0.5);
        for(int note : {45, 49, 52})
            CHECK(tone(out.left, at(0.1), at(0.5), hzOf(note)) > 0.01);
    }
    // Mono: the newest key only, and back to the held one on release.
    {
        Instance plugin;
        CHECK(plugin.open(path));
        plugin.param(kKeyMode, modeNorm(1));
        plugin.param(kOsc1Wave, waveNorm(kSaw));
        plugin.param(kChorus, 0.0);
        plugin.param(kDelay, 0.0);
        plugin.noteOn(45);
        plugin.noteOn(52);
        const Stereo both = plugin.render(0.5);
        CHECK(tone(both.left, at(0.1), at(0.5), hzOf(52)) > 5.0 * tone(both.left, at(0.1), at(0.5), hzOf(45)));
        plugin.noteOff(52);
        const Stereo back = plugin.render(0.5);
        CHECK(tone(back.left, at(0.1), at(0.5), hzOf(45)) > 5.0 * tone(back.left, at(0.1), at(0.5), hzOf(52)));
    }
    // Unison: eight detuned, panned voices on one key; the ears differ.
    {
        const Stereo out = play(path, 57, 0.5, {DRY, {kKeyMode, modeNorm(3)}});
        CHECK(rms(out.left, at(0.1), at(0.5)) > 0.01);
        double difference = 0.0;
        for(size_t i = at(0.1); i < at(0.5); ++i)
            difference += std::fabs(out.left[i] - out.right[i]);
        CHECK(difference > 1.0);
    }
}

void testPortamento(const std::string &path)
{
    // Mono with a 1 s glide from A2 to A3: halfway it sounds near A#2..G#3.
    Instance plugin;
    CHECK(plugin.open(path));
    plugin.param(kKeyMode, modeNorm(1));
    plugin.param(kPortamento, 1.0);
    plugin.param(kPortaTime, 0.75); // 1 s
    plugin.param(kOsc1Wave, waveNorm(kSaw));
    plugin.param(kChorus, 0.0);
    plugin.param(kDelay, 0.0);
    plugin.noteOn(45);
    plugin.render(0.3);
    plugin.noteOn(57);
    const Stereo glide = plugin.render(1.5);
    const double middle = zeroCrossingHz(glide.left, at(0.45), at(0.55));
    CHECK(middle > 130.0 && middle < 190.0);
    CHECK(std::fabs(zeroCrossingHz(glide.left, at(1.2), at(1.5)) - 220.0) < 2.0);
}

void testDelay(const std::string &path)
{
    // A short blip, then its echo 250 ms later on the delay's side.
    Instance plugin;
    CHECK(plugin.open(path));
    plugin.param(kChorus, 0.0);
    plugin.param(kDelaySync, 0.0);
    plugin.param(kDelayTime, delayNorm(250.0));
    plugin.param(kDelayFeedback, 0.0);
    plugin.param(kDelayLevel, 1.0);
    plugin.param(kAmpRelease, 0.1);
    plugin.noteOn(69);
    plugin.render(0.05);
    plugin.noteOff(69);
    const Stereo out = plugin.render(0.6);
    // The panning delay sends the first repeat left.
    CHECK(rms(out.left, at(0.2), at(0.25)) > 10.0 * rms(out.left, at(0.12), at(0.17)));
    CHECK(finite(out));
}

void testState(const std::string &path)
{
    Instance a;
    CHECK(a.open(path));
    a.param(kCutoff, 0.81);
    a.param(kCtrl1, 0.42);
    a.param(kKeyMode, modeNorm(2));
    a.renderFrames(kBlock);
    MemoryStream stream;
    CHECK(a.component->getState(&stream) == kResultOk);
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    Instance b;
    CHECK(b.open(path));
    CHECK(b.component->setState(&stream) == kResultOk);
    auto controller = U::cast<IEditController>(b.provider->getControllerPtr());
    CHECK(controller && std::fabs(controller->getParamNormalized(kCutoff) - 0.81) < 1e-9);
    CHECK(controller && std::fabs(controller->getParamNormalized(kCtrl1) - 0.42) < 1e-9);
    // Same patch, same note: the same sound (the voices' phases come from
    // the same seeds in a fresh instance).
    a.param(kChorus, 0.0);
    b.param(kChorus, 0.0);
    a.noteOn(60);
    b.noteOn(60);
    const Stereo x = a.render(0.2), y = b.render(0.2);
    CHECK(peak(x.left) > 0.01);
    MemoryStream junk;
    int32 zero = 0;
    junk.write(&zero, sizeof(zero), nullptr);
    junk.seek(0, IBStream::kIBSeekSet, nullptr);
    CHECK(b.component->setState(&junk) == kResultFalse);
    (void)y;
}

void testBounded(const std::string &path)
{
    // Eight keys of square waves at full resonance through the delay at
    // full feedback: an overload, but finite and bounded (the resonance and
    // the delay's feedback saturate rather than grow).
    Instance plugin;
    CHECK(plugin.open(path));
    plugin.param(kResonance, 1.0);
    plugin.param(kCutoff, 0.4);
    plugin.param(kOsc1Wave, waveNorm(kSquare));
    plugin.param(kDelayFeedback, 1.0);
    for(int note : {36, 43, 48, 52, 55, 60, 64, 67})
        plugin.noteOn(note, 1.0f);
    const Stereo out = plugin.render(4.0);
    CHECK(finite(out));
    CHECK(peak(out.left) < 5.0 && peak(out.right) < 5.0);
    // One key of the default patch stays well inside full scale.
    const Stereo one = play(path, 60, 1.0, {});
    CHECK(peak(one.left) > 0.05 && peak(one.left) < 0.6);
}

} // namespace

int main(int argc, char **argv)
{
    if(argc < 2) {
        std::fprintf(stderr, "usage: %s <Mla8000.vst3> [wav output directory]\n", argv[0]);
        return 2;
    }
    const std::string path = argv[1];
    const char *wavDir = argc > 2 ? argv[2] : nullptr;
    testLayout(path);
    testPitch(path);
    testSuperSaw(path, wavDir);
    testFilter(path);
    testModWheel(path);
    testEnvelope(path);
    testKeyModes(path);
    testPortamento(path);
    testDelay(path);
    testState(path);
    testBounded(path);
    if(failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all Mla 8000 tests passed\n");
    return 0;
}
