// Offline functional tests for the Mla 06 bundle.
//
// Loads the built .vst3 through the SDK hosting classes, strikes the
// instruments and checks rendered audio against measurements of a TR-606:
// bus layout, silence, the key map, sample-accurate hits, each voice's pitch,
// length and spectrum, the closed hat choking the open one, accent and
// velocity dynamics, the per-voice controls and state round trips.
//
// Usage: mla_06_tests <path/to/Mla06.vst3> [wav output directory]
// With a directory, every instrument is also written there as a WAV file.

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

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
constexpr ParamID kOutput = 100, kAccent = 101, kDynamics = 102, kBdTune = 104, kBdDecay = 105,
                  kSdSnappy = 109, kLtTune = 111, kOhDecay = 119, kChLevel = 120, kMetalTune = 122;

// One General MIDI key per instrument.
constexpr int kBD = 36, kSD = 38, kLT = 45, kHT = 50, kCY = 49, kOH = 46, kCH = 42;

class Application final : public HostApplication {
  public:
    tresult PLUGIN_API getName(String128 name) override
    {
        const char16_t text[] = u"mla_06_tests";
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
    ParameterChanges changes{64};
    ProcessContext context{};
    std::vector<std::string> subCategories;

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

    void param(ParamID id, double value)
    {
        int32 index = 0;
        if(auto *queue = changes.addParameterData(id, index))
            queue->addPoint(0, value, index);
    }

    void noteOn(int pitch, float velocity = 100.0f / 127.0f, int32 offset = 0)
    {
        Event e{};
        e.type = Event::kNoteOnEvent;
        e.sampleOffset = offset;
        e.noteOn.pitch = static_cast<int16>(pitch);
        e.noteOn.velocity = velocity;
        e.noteOn.noteId = -1;
        events.addEvent(e);
    }

    void noteOff(int pitch, int32 offset = 0)
    {
        Event e{};
        e.type = Event::kNoteOffEvent;
        e.sampleOffset = offset;
        e.noteOff.pitch = static_cast<int16>(pitch);
        e.noteOff.noteId = -1;
        events.addEvent(e);
    }

    // Render `frames` (a multiple of the block).
    Stereo render(int frames)
    {
        Stereo out;
        for(int done = 0; done < frames; done += kBlock) {
            data.numSamples = kBlock;
            processor->process(data);
            events.clear();
            changes.clearQueue();
            const auto &bus = data.outputs[0];
            for(int32 i = 0; i < kBlock; ++i) {
                out.left.push_back(bus.channelBuffers32[0][i]);
                out.right.push_back(bus.channelBuffers32[1][i]);
            }
        }
        return out;
    }
};

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

// Seconds until the output stays 40 dB below its loudest 5 ms window.
double length40(const std::vector<float> &x)
{
    const size_t window = static_cast<size_t>(kRate / 200.0);
    double loudest = 0.0;
    for(size_t from = 0; from + window <= x.size(); from += window)
        loudest = std::max(loudest, rms(x, from, from + window));
    size_t last = 0;
    for(size_t from = 0; from + window <= x.size(); from += window)
        if(rms(x, from, from + window) > loudest * 0.01)
            last = from + window;
    return static_cast<double>(last) / kRate;
}

// Mean frequency from upward zero crossings in x[from, to).
double zeroCrossingHz(const std::vector<float> &x, size_t from, size_t to)
{
    double first = -1.0, last = -1.0;
    int crossings = 0;
    for(size_t i = from + 1; i < std::min(to, x.size()); ++i) {
        if(x[i - 1] < 0.0f && x[i] >= 0.0f) {
            const double at = static_cast<double>(i - 1) + x[i - 1] / (x[i - 1] - x[i]);
            if(first < 0.0)
                first = at;
            else
                ++crossings;
            last = at;
        }
    }
    return crossings > 0 ? kRate * crossings / (last - first) : 0.0;
}

// Share of the energy above `hz`, from the first difference of a one-pole
// split (a rough but monotonic brightness measure).
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

// One hit of `key` at `velocity` in a fresh instance.
Stereo hit(const std::string &path, int key, float velocity = 100.0f / 127.0f, double seconds = 1.0,
           std::initializer_list<std::pair<ParamID, double>> params = {})
{
    Instance plugin;
    CHECK(plugin.open(path));
    for(const auto &p : params)
        plugin.param(p.first, p.second);
    plugin.noteOn(key, velocity);
    return plugin.render(static_cast<int>(kRate * seconds) / kBlock * kBlock);
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
    CHECK(std::find(cats.begin(), cats.end(), "Drum") != cats.end());
    CHECK(plugin.component->getBusCount(kAudio, kInput) == 0);
    CHECK(plugin.component->getBusCount(kAudio, kOutput) == 1);
    CHECK(plugin.component->getBusCount(kEvent, kInput) == 1);
    SpeakerArrangement mono = SpeakerArr::kMono, stereo = SpeakerArr::kStereo;
    CHECK(plugin.processor->setBusArrangements(nullptr, 0, &stereo, 1) == kResultTrue);
    CHECK(plugin.processor->setBusArrangements(nullptr, 0, &mono, 1) == kResultFalse);
    CHECK(plugin.processor->setBusArrangements(&stereo, 1, &stereo, 1) == kResultFalse);
}

void testSilenceAndKeys(const std::string &path)
{
    // Nothing sounds without notes, on keys outside the map, or on note-offs.
    Instance plugin;
    CHECK(plugin.open(path));
    plugin.noteOn(60);
    plugin.noteOn(34);
    plugin.noteOff(36);
    const Stereo out = plugin.render(24000);
    CHECK(finite(out));
    CHECK(peak(out.left) == 0.0 && peak(out.right) == 0.0);
    // Every General MIDI alias of an instrument plays it.
    for(int key : {35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 55, 57, 59})
        CHECK(peak(hit(path, key, 1.0f, 0.1).left) > 0.05);
}

void testVoices(const std::string &path, const char *wavDir)
{
    // Each voice is finite, mono, bounded, and about as long as on a 606
    // (40 dB down: BD 0.19 s, SD 0.13 s, tom 0.18 s, CH 0.23 s, OH 1.6 s).
    struct Voice {
        const char *name;
        int key;
        double minLength, maxLength;
    } voices[] = {
        {"bd", kBD, 0.15, 0.30}, {"sd", kSD, 0.09, 0.20}, {"lt", kLT, 0.13, 0.30}, {"ht", kHT, 0.10, 0.28},
        {"cy", kCY, 0.8, 3.0},   {"oh", kOH, 1.1, 2.4},   {"ch", kCH, 0.15, 0.32},
    };
    for(const auto &v : voices) {
        const Stereo out = hit(path, v.key, 100.0f / 127.0f, 3.0);
        const double length = length40(out.left), level = peak(out.left);
        std::printf("%s: peak %.3f, 40 dB down at %.3f s\n", v.name, level, length);
        CHECK(finite(out));
        CHECK(level > 0.2 && level < 1.0);
        CHECK(length > v.minLength && length < v.maxLength);
        double difference = 0.0;
        for(size_t i = 0; i < out.left.size(); ++i)
            difference = std::max(difference, static_cast<double>(std::fabs(out.left[i] - out.right[i])));
        CHECK(difference == 0.0);
        if(wavDir)
            writeWav(std::string(wavDir) + "/mla06_" + v.name + ".wav", out.left);
    }
}

void testStartOffset(const std::string &path)
{
    Instance plugin;
    CHECK(plugin.open(path));
    plugin.noteOn(kSD, 1.0f, 100);
    const Stereo out = plugin.render(kBlock * 4);
    CHECK(peak(out.left, 0, 100) == 0.0);
    CHECK(peak(out.left, 100, 400) > 0.05);
}

void testPitches(const std::string &path)
{
    // Bass drum ~58 Hz; Tune moves it by semitones.
    const double bd = zeroCrossingHz(hit(path, kBD).left, 2400, 9600);
    const double bdUp = zeroCrossingHz(hit(path, kBD, 100.0f / 127.0f, 1.0, {{kBdTune, 1.0}}).left, 2400, 9600);
    // Low tom settles at ~132 Hz from a higher start; the high tom is a
    // fifth up. The snare tone settles at ~205 Hz.
    const Stereo lt = hit(path, kLT, 100.0f / 127.0f, 1.0, {{kSdSnappy, 0.0}});
    const double ltStart = zeroCrossingHz(lt.left, 240, 960), ltEnd = zeroCrossingHz(lt.left, 4800, 9600);
    const double ltDown = zeroCrossingHz(hit(path, kLT, 100.0f / 127.0f, 1.0, {{kLtTune, 0.0}}).left, 4800, 9600);
    std::printf("bd %.1f Hz (+12 st %.1f Hz), lt %.1f -> %.1f Hz (-12 st %.1f Hz)\n", bd, bdUp, ltStart,
                ltEnd, ltDown);
    CHECK(std::fabs(bd - 58.0) < 2.0);
    CHECK(std::fabs(bdUp - 116.0) < 4.0);
    CHECK(ltStart > 150.0 && ltEnd > 120.0 && ltEnd < 145.0);
    CHECK(std::fabs(ltDown - ltEnd / 2.0) < 6.0);
}

void testSnappy(const std::string &path)
{
    // Snappy moves the snare between its tone and its noise.
    const Stereo tone = hit(path, kSD, 100.0f / 127.0f, 0.5, {{kSdSnappy, 0.0}});
    const Stereo snappy = hit(path, kSD, 100.0f / 127.0f, 0.5, {{kSdSnappy, 1.0}});
    const double a = highShare(tone.left, 0, 4800, 2000.0), b = highShare(snappy.left, 0, 4800, 2000.0);
    const double toneHz = zeroCrossingHz(tone.left, 1440, 4800);
    std::printf("snare: tone %.1f Hz, energy above 2 kHz %.2f (snappy 0) / %.2f (snappy 1)\n", toneHz, a, b);
    CHECK(std::fabs(toneHz - 205.0) < 12.0);
    CHECK(a < 0.1 && b > 0.5);
}

void testHats(const std::string &path)
{
    // The hats are bright metal (a 606's cross zero at about 7 kHz), and the
    // closed hat chokes the open one.
    const Stereo ch = hit(path, kCH), oh = hit(path, kOH, 100.0f / 127.0f, 2.0);
    const double chHz = zeroCrossingHz(ch.left, 0, 4800), ohHz = zeroCrossingHz(oh.left, 0, 4800);
    std::printf("hats: zero crossings CH %.0f Hz, OH %.0f Hz\n", chHz, ohHz);
    CHECK(chHz > 5500.0 && chHz < 9500.0 && ohHz > 5500.0 && ohHz < 9500.0);
    Instance plugin;
    CHECK(plugin.open(path));
    plugin.noteOn(kOH, 1.0f);
    plugin.render(kBlock * 20);
    plugin.param(kChLevel, 0.0); // the choke alone, without the closed hat's own sound
    plugin.noteOn(kCH, 1.0f);
    const Stereo choked = plugin.render(kBlock * 20);
    const double before = rms(choked.left, 0, 128), after = rms(choked.left, 1920, 4800);
    std::printf("choke: open hat %.4f -> %.6f\n", before, after);
    CHECK(before > 0.02 && after < before * 0.001);
    // Longer OH Decay holds the open hat longer.
    const double shortHat = length40(oh.left);
    const double longHat = length40(hit(path, kOH, 100.0f / 127.0f, 4.0, {{kOhDecay, 0.75}}).left);
    CHECK(longHat > shortHat * 1.6);
    // Metal Tune moves the bank: the hats' sound changes.
    const Stereo tuned = hit(path, kCH, 100.0f / 127.0f, 1.0, {{kMetalTune, 1.0}});
    double difference = 0.0;
    for(size_t i = 0; i < 4800; ++i)
        difference += std::fabs(tuned.left[i] - ch.left[i]);
    CHECK(difference > 1.0);
}

void testDynamics(const std::string &path)
{
    // 606 Accent: velocities up to 100 play at one level; above 100 accents.
    const double soft = peak(hit(path, kBD, 20.0f / 127.0f).left), normal = peak(hit(path, kBD).left);
    const double accented = peak(hit(path, kBD, 1.0f).left);
    const double noAccent = peak(hit(path, kBD, 1.0f, 1.0, {{kAccent, 0.0}}).left);
    std::printf("dynamics: v20 %.3f, v100 %.3f, v127 %.3f, v127 accent 0 %.3f\n", soft, normal, accented, noAccent);
    CHECK(std::fabs(soft - normal) < 1e-6);
    CHECK(accented > normal * 1.6 && accented < normal * 2.4);
    CHECK(std::fabs(noAccent - normal) < 1e-6);
    // Velocity: the level follows velocity.
    const double low = peak(hit(path, kBD, 0.5f, 1.0, {{kDynamics, 1.0}}).left);
    const double high = peak(hit(path, kBD, 1.0f, 1.0, {{kDynamics, 1.0}}).left);
    CHECK(low < high * 0.4);
    // Output gain, and Output 0 is silence.
    CHECK(peak(hit(path, kBD, 100.0f / 127.0f, 1.0, {{kOutput, 0.0}}).left) == 0.0);
}

void testDecayAndRetrigger(const std::string &path)
{
    const double base = length40(hit(path, kBD).left);
    const double longer = length40(hit(path, kBD, 100.0f / 127.0f, 2.0, {{kBdDecay, 1.0}}).left);
    std::printf("bd decay: %.3f s, x4 %.3f s\n", base, longer);
    CHECK(longer > base * 2.5);
    // A fast roll stays bounded and does not click: retriggers continue the
    // ring, so no step is larger than in a single hit (with its click).
    const auto largestStep = [](const std::vector<float> &x) {
        double jump = 0.0;
        for(size_t i = 1; i < x.size(); ++i)
            jump = std::max(jump, static_cast<double>(std::fabs(x[i] - x[i - 1])));
        return jump;
    };
    for(int key : {kBD, kLT, kHT}) {
        const double single = largestStep(hit(path, key, 1.0f).left);
        Instance plugin;
        CHECK(plugin.open(path));
        for(int i = 0; i < 16; ++i)
            plugin.noteOn(key, i % 3 ? 0.5f : 1.0f, i * 15);
        Stereo roll = plugin.render(kBlock * 4);
        for(int i = 0; i < 12; ++i) {
            plugin.noteOn(key, 1.0f, (i * 37) % kBlock);
            Stereo more = plugin.render(kBlock);
            roll.left.insert(roll.left.end(), more.left.begin(), more.left.end());
        }
        const double jump = largestStep(roll.left);
        std::printf("roll on key %d: peak %.3f, largest step %.3f (single hit %.3f)\n", key, peak(roll.left), jump,
                    single);
        CHECK(peak(roll.left) < 1.2);
        CHECK(jump < single * 1.5);
    }
}

void testState(const std::string &path)
{
    Instance a, b;
    CHECK(a.open(path) && b.open(path));
    a.param(kBdTune, 0.8);
    a.param(kDynamics, 1.0);
    a.param(kOutput, 0.5);
    a.render(kBlock);
    MemoryStream stream;
    CHECK(a.component->getState(&stream) == kResultOk);
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    CHECK(b.component->setState(&stream) == kResultOk);
    MemoryStream again;
    CHECK(b.component->getState(&again) == kResultOk);
    CHECK(stream.getSize() == again.getSize());
    CHECK(std::memcmp(stream.getData(), again.getData(), static_cast<size_t>(stream.getSize())) == 0);
    // The restored state sounds the same.
    a.noteOn(kBD, 0.6f);
    b.noteOn(kBD, 0.6f);
    const Stereo x = a.render(kBlock * 8), y = b.render(kBlock * 8);
    CHECK(peak(x.left) > 0.01);
    CHECK(std::equal(x.left.begin(), x.left.end(), y.left.begin()));
    // Rejects foreign data.
    MemoryStream junk;
    int32 zero = 0;
    junk.write(&zero, sizeof(zero), nullptr);
    junk.seek(0, IBStream::kIBSeekSet, nullptr);
    CHECK(b.component->setState(&junk) == kResultFalse);
}

} // namespace

int main(int argc, char **argv)
{
    if(argc < 2) {
        std::fprintf(stderr, "usage: %s <Mla06.vst3> [wav output directory]\n", argv[0]);
        return 2;
    }
    const std::string path = argv[1];
    testLayout(path);
    testSilenceAndKeys(path);
    testVoices(path, argc > 2 ? argv[2] : nullptr);
    testStartOffset(path);
    testPitches(path);
    testSnappy(path);
    testHats(path);
    testDynamics(path);
    testDecayAndRetrigger(path);
    testState(path);
    if(failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all Mla 06 tests passed\n");
    return 0;
}
