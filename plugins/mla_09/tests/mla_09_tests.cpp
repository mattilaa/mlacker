// Offline functional tests for the Mla 09 bundle.
//
// Loads the built .vst3 through the SDK hosting classes, strikes the
// instruments and checks rendered audio: bus layout, silence, the key map,
// sample-accurate hits, each voice's length, the bass drum's Tune sweep,
// Attack and Decay, the snare's Tune, Tone and Snappy, tom tuning and decay,
// the ROM voices (the same sample on every hit, the closed hat choking the
// open one, the hats' Decay, the cymbals' Tune running the ROM clock),
// accent and velocity dynamics, click-free retriggers and state round trips.
//
// Usage: mla_09_tests <path/to/Mla09.vst3> [wav output directory]
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
constexpr ParamID kOutput = 100, kAccent = 101, kDynamics = 102, kBdTune = 104, kBdAttack = 105, kBdDecay = 106,
                  kSdTune = 108, kSdTone = 109, kSdSnappy = 110, kLtTune = 112, kLtDecay = 113, kMtTune = 115,
                  kHtTune = 118, kChLevel = 122, kChDecay = 123, kOhDecay = 125, kCrTune = 127, kRdTune = 129;

// One key per instrument (the 909's MIDI note numbers).
constexpr int kBD = 36, kRS = 37, kSD = 38, kCP = 39, kLT = 43, kMT = 47, kHT = 50, kCH = 42, kOH = 46, kCR = 49,
              kRD = 51;

class Application final : public HostApplication {
  public:
    tresult PLUGIN_API getName(String128 name) override
    {
        const char16_t text[] = u"mla_09_tests";
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

// Power of x[from, to) at `hz` (Goertzel), relative to the segment's power.
double toneShare(const std::vector<float> &x, size_t from, size_t to, double hz)
{
    const double w = 2.0 * M_PI * hz / kRate, k = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0, total = 0.0;
    to = std::min(to, x.size());
    for(size_t i = from; i < to; ++i) {
        const double s = x[i] + k * s1 - s2;
        s2 = s1;
        s1 = s;
        total += static_cast<double>(x[i]) * x[i];
    }
    const double power = s1 * s1 + s2 * s2 - k * s1 * s2;
    return total > 0.0 ? 2.0 * power / (static_cast<double>(to - from) * total) : 0.0;
}

double largestStep(const std::vector<float> &x)
{
    double jump = 0.0;
    for(size_t i = 1; i < x.size(); ++i)
        jump = std::max(jump, static_cast<double>(std::fabs(x[i] - x[i - 1])));
    return jump;
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
    plugin.noteOn(56);
    plugin.noteOff(36);
    const Stereo out = plugin.render(24000);
    CHECK(finite(out));
    CHECK(peak(out.left) == 0.0 && peak(out.right) == 0.0);
    // Every key of an instrument plays it.
    for(int key : {35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 55, 57, 59})
        CHECK(peak(hit(path, key, 1.0f, 0.1).left) > 0.05);
}

void testVoices(const std::string &path, const char *wavDir)
{
    // Each voice is finite, mono, bounded and about as long as a 909's at
    // centred knobs (40 dB down).
    struct Voice {
        const char *name;
        int key;
        double minLength, maxLength;
    } voices[] = {
        {"bd", kBD, 0.35, 0.80}, {"sd", kSD, 0.15, 0.40}, {"lt", kLT, 0.50, 1.10}, {"mt", kMT, 0.45, 1.0},
        {"ht", kHT, 0.40, 0.90}, {"rs", kRS, 0.01, 0.06}, {"cp", kCP, 0.25, 0.60}, {"ch", kCH, 0.05, 0.16},
        {"oh", kOH, 0.40, 0.90}, {"cr", kCR, 1.2, 2.1},   {"rd", kRD, 1.2, 2.1},
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
            writeWav(std::string(wavDir) + "/mla09_" + v.name + ".wav", out.left);
    }
}

void testStartOffset(const std::string &path)
{
    for(int key : {kSD, kCH}) {
        Instance plugin;
        CHECK(plugin.open(path));
        plugin.noteOn(key, 1.0f, 100);
        const Stereo out = plugin.render(kBlock * 4);
        CHECK(peak(out.left, 0, 100) == 0.0);
        CHECK(peak(out.left, 100, 400) > 0.05);
    }
}

void testBassDrum(const std::string &path)
{
    // The pitch sweeps down to about 50 Hz; Tune deepens and lengthens the
    // sweep.
    const Stereo flat = hit(path, kBD, 100.0f / 127.0f, 1.0, {{kBdTune, 0.0}});
    const Stereo swept = hit(path, kBD, 100.0f / 127.0f, 1.0, {{kBdTune, 1.0}});
    const double flatEarly = zeroCrossingHz(flat.left, 240, 1920), sweptEarly = zeroCrossingHz(swept.left, 240, 1920);
    const double rest = zeroCrossingHz(hit(path, kBD).left, 9600, 19200);
    // Decay: 40 dB down after about 0.2 s at the minimum and over 1 s at the maximum.
    const double shortest = length40(hit(path, kBD, 100.0f / 127.0f, 2.0, {{kBdDecay, 0.0}}).left);
    const double longest = length40(hit(path, kBD, 100.0f / 127.0f, 2.0, {{kBdDecay, 1.0}}).left);
    // Attack adds the click: more energy above 2 kHz in the first 3 ms.
    const double soft = highShare(hit(path, kBD, 100.0f / 127.0f, 0.1, {{kBdAttack, 0.0}}).left, 0, 144, 2000.0);
    const double hard = highShare(hit(path, kBD, 100.0f / 127.0f, 0.1, {{kBdAttack, 1.0}}).left, 0, 144, 2000.0);
    std::printf("bd: first 40 ms %.0f Hz (tune 0) / %.0f Hz (tune 1), %.1f Hz at 0.2 s; 40 dB down %.3f .. %.3f s; "
                "attack above 2 kHz %.4f (0) / %.4f (1)\n",
                flatEarly, sweptEarly, rest, shortest, longest, soft, hard);
    CHECK(sweptEarly > flatEarly * 1.3);
    CHECK(rest > 44.0 && rest < 60.0);
    CHECK(shortest > 0.1 && shortest < 0.35);
    CHECK(longest > 1.0 && longest < 1.9);
    CHECK(hard > soft * 2.5);
}

void testSnare(const std::string &path)
{
    // Tune moves the VCOs; Tone brightens the noise; Snappy adds it.
    const double low = zeroCrossingHz(hit(path, kSD, 100.0f / 127.0f, 0.3, {{kSdTune, 0.0}, {kSdSnappy, 0.0}}).left, 960, 4800);
    const double high = zeroCrossingHz(hit(path, kSD, 100.0f / 127.0f, 0.3, {{kSdTune, 1.0}, {kSdSnappy, 0.0}}).left, 960, 4800);
    const Stereo dry = hit(path, kSD, 100.0f / 127.0f, 0.5, {{kSdSnappy, 0.0}});
    const Stereo snappy = hit(path, kSD, 100.0f / 127.0f, 0.5, {{kSdSnappy, 1.0}});
    const double a = highShare(dry.left, 0, 4800, 2000.0), b = highShare(snappy.left, 0, 4800, 2000.0);
    const Stereo dark = hit(path, kSD, 100.0f / 127.0f, 0.5, {{kSdTone, 0.0}, {kSdSnappy, 1.0}});
    const Stereo bright = hit(path, kSD, 100.0f / 127.0f, 0.5, {{kSdTone, 1.0}, {kSdSnappy, 1.0}});
    const double d = highShare(dark.left, 0, 4800, 6000.0), e = highShare(bright.left, 0, 4800, 6000.0);
    std::printf("snare: %.0f .. %.0f Hz (tune); above 2 kHz %.3f (snappy 0) / %.2f (snappy 1); "
                "above 6 kHz %.3f (tone 0) / %.3f (tone 1)\n", low, high, a, b, d, e);
    CHECK(high > low * 1.3);
    CHECK(a < 0.05 && b > 0.3);
    CHECK(e > d * 1.5);
}

void testToms(const std::string &path)
{
    // Each tom settles to its pitch; Tune moves it over about a fifth and
    // Decay lengthens it.
    const double lt = zeroCrossingHz(hit(path, kLT, 100.0f / 127.0f, 0.5, {{kLtTune, 0.0}}).left, 7200, 19200);
    const double ltUp = zeroCrossingHz(hit(path, kLT, 100.0f / 127.0f, 0.5, {{kLtTune, 1.0}}).left, 7200, 19200);
    const double mt = zeroCrossingHz(hit(path, kMT, 100.0f / 127.0f, 0.5, {{kMtTune, 0.5}}).left, 7200, 19200);
    const double ht = zeroCrossingHz(hit(path, kHT, 100.0f / 127.0f, 0.5, {{kHtTune, 0.5}}).left, 7200, 19200);
    // The bend: the first 10 ms are well above the settled pitch.
    const double ltStart = zeroCrossingHz(hit(path, kLT, 100.0f / 127.0f, 0.5, {{kLtTune, 0.0}}).left, 0, 960);
    const double shortTom = length40(hit(path, kLT, 100.0f / 127.0f, 3.0, {{kLtDecay, 0.0}}).left);
    const double longTom = length40(hit(path, kLT, 100.0f / 127.0f, 3.0, {{kLtDecay, 1.0}}).left);
    std::printf("toms: lt %.1f .. %.1f Hz (starting %.0f Hz), mt %.1f Hz, ht %.1f Hz; lt decay %.2f .. %.2f s\n", lt,
                ltUp, ltStart, mt, ht, shortTom, longTom);
    CHECK(std::fabs(lt - 72.0) < 4.0 && std::fabs(ltUp - 112.0) < 5.0);
    CHECK(std::fabs(mt - 130.0) < 5.0 && std::fabs(ht - 185.0) < 7.0);
    CHECK(ltStart > lt * 1.2);
    CHECK(longTom > shortTom * 4.0);
}

void testSamples(const std::string &path)
{
    // The ROM voices replay the same samples on every hit (unlike the analog
    // noise voices), and from one instance to the next.
    for(int key : {kCH, kOH, kCR, kRD}) {
        const Stereo a = hit(path, key, 100.0f / 127.0f, 0.2), b = hit(path, key, 100.0f / 127.0f, 0.2);
        CHECK(std::equal(a.left.begin(), a.left.end(), b.left.begin()));
        Instance plugin;
        CHECK(plugin.open(path));
        plugin.noteOn(key);
        const Stereo first = plugin.render(kBlock * 10);
        plugin.render(static_cast<int>(kRate * 3) / kBlock * kBlock);
        plugin.noteOn(key);
        const Stereo again = plugin.render(kBlock * 10);
        double diff = 0.0;
        for(size_t i = 0; i < first.left.size(); ++i)
            diff = std::max(diff, static_cast<double>(std::fabs(first.left[i] - again.left[i])));
        std::printf("rom key %d: repeat differs by %.2e\n", key, diff);
        CHECK(diff < 1e-4 && peak(first.left) > 0.05);
    }
    // The analog snare's noise does differ between hits.
    Instance plugin;
    CHECK(plugin.open(path));
    plugin.noteOn(kSD);
    const Stereo first = plugin.render(kBlock * 10);
    plugin.render(kBlock * 200);
    plugin.noteOn(kSD);
    const Stereo again = plugin.render(kBlock * 10);
    CHECK(!std::equal(first.left.begin(), first.left.end(), again.left.begin()));
}

void testHats(const std::string &path)
{
    // The hats are bright.
    const Stereo ch = hit(path, kCH), oh = hit(path, kOH, 100.0f / 127.0f, 2.0);
    const double chHigh = highShare(ch.left, 0, 2400, 2000.0), ohHigh = highShare(oh.left, 0, 4800, 2000.0);
    std::printf("hats: energy above 2 kHz CH %.2f, OH %.2f\n", chHigh, ohHigh);
    CHECK(chHigh > 0.7 && ohHigh > 0.7);
    // The closed hat chokes the open one.
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
    // Decay knobs.
    const double chShort = length40(hit(path, kCH, 100.0f / 127.0f, 1.0, {{kChDecay, 0.0}}).left);
    const double chLong = length40(hit(path, kCH, 100.0f / 127.0f, 1.0, {{kChDecay, 1.0}}).left);
    const double ohShort = length40(hit(path, kOH, 100.0f / 127.0f, 2.0, {{kOhDecay, 0.0}}).left);
    const double ohLong = length40(hit(path, kOH, 100.0f / 127.0f, 2.0, {{kOhDecay, 1.0}}).left);
    std::printf("hat decays: ch %.3f .. %.3f s, oh %.3f .. %.3f s\n", chShort, chLong, ohShort, ohLong);
    CHECK(chLong > chShort * 2.5);
    CHECK(ohLong > ohShort * 3.0);
}

void testCymbals(const std::string &path)
{
    // Tune runs the ROM clock: lower is darker and longer.
    for(const auto &c : {std::pair<int, ParamID>{kCR, kCrTune}, {kRD, kRdTune}}) {
        const Stereo low = hit(path, c.first, 100.0f / 127.0f, 3.0, {{c.second, 0.0}});
        const Stereo high = hit(path, c.first, 100.0f / 127.0f, 3.0, {{c.second, 1.0}});
        const double lowHz = zeroCrossingHz(low.left, 0, 9600), highHz = zeroCrossingHz(high.left, 0, 9600);
        const double lowLength = length40(low.left), highLength = length40(high.left);
        std::printf("cymbal key %d: %.0f .. %.0f Hz zero crossings, %.2f .. %.2f s\n", c.first, lowHz, highHz, lowLength,
                    highLength);
        CHECK(highHz > lowHz * 1.4);
        CHECK(lowLength > highLength * 1.4);
    }
    // The crash is brighter than the ride.
    const double crHigh = highShare(hit(path, kCR, 100.0f / 127.0f, 0.5).left, 0, 9600, 6000.0);
    const double rdHigh = highShare(hit(path, kRD, 100.0f / 127.0f, 0.5).left, 0, 9600, 6000.0);
    std::printf("cymbals: above 6 kHz crash %.2f, ride %.2f\n", crHigh, rdHigh);
    CHECK(crHigh > rdHigh);
}

void testDynamics(const std::string &path)
{
    // 909 Accent: velocities up to 100 play at one level; above 100 accents.
    for(int key : {kLT, kCH}) {
        const double soft = peak(hit(path, key, 20.0f / 127.0f).left), normal = peak(hit(path, key).left);
        const double accented = peak(hit(path, key, 1.0f).left);
        const double noAccent = peak(hit(path, key, 1.0f, 1.0, {{kAccent, 0.0}}).left);
        std::printf("dynamics key %d: v20 %.3f, v100 %.3f, v127 %.3f, v127 accent 0 %.3f\n", key, soft, normal,
                    accented, noAccent);
        CHECK(std::fabs(soft - normal) < 1e-6);
        CHECK(accented > normal * 1.5 && accented < normal * 2.0);
        CHECK(std::fabs(noAccent - normal) < 1e-6);
    }
    // Velocity: the level follows velocity.
    const double low = peak(hit(path, kLT, 0.5f, 1.0, {{kDynamics, 1.0}}).left);
    const double high = peak(hit(path, kLT, 1.0f, 1.0, {{kDynamics, 1.0}}).left);
    CHECK(low < high * 0.4);
    // Output 0 is silence.
    CHECK(peak(hit(path, kBD, 100.0f / 127.0f, 1.0, {{kOutput, 0.0}}).left) == 0.0);
}

void testRetrigger(const std::string &path)
{
    // A fast roll stays bounded and does not click: analog voices continue
    // their waveform, ROM voices fade the old playback out under the new.
    for(int key : {kBD, kLT, kHT, kOH, kCR}) {
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
            roll.right.insert(roll.right.end(), more.right.begin(), more.right.end());
        }
        const double jump = largestStep(roll.left);
        std::printf("roll on key %d: peak %.3f, largest step %.3f (single hit %.3f)\n", key, peak(roll.left), jump,
                    single);
        CHECK(finite(roll));
        // The bass drum's click sounds on every hit, as on the 909, and sums
        // with the ring that is already sounding.
        CHECK(peak(roll.left) < (key == kBD ? 2.2 : 1.6));
        if(key != kBD)
            CHECK(jump < single * 1.6);
    }
}

void testState(const std::string &path)
{
    Instance a, b;
    CHECK(a.open(path) && b.open(path));
    a.param(kBdDecay, 0.8);
    a.param(kCrTune, 0.2);
    a.param(kDynamics, 1.0);
    a.param(kOutput, 0.8);
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
    a.noteOn(kCR, 0.6f);
    b.noteOn(kCR, 0.6f);
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
        std::fprintf(stderr, "usage: %s <Mla09.vst3> [wav output directory]\n", argv[0]);
        return 2;
    }
    const std::string path = argv[1];
    testLayout(path);
    testSilenceAndKeys(path);
    testVoices(path, argc > 2 ? argv[2] : nullptr);
    testStartOffset(path);
    testBassDrum(path);
    testSnare(path);
    testToms(path);
    testSamples(path);
    testHats(path);
    testCymbals(path);
    testDynamics(path);
    testRetrigger(path);
    testState(path);
    if(failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all Mla 09 tests passed\n");
    return 0;
}
