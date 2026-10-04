// Offline functional tests for the Mla 08 bundle.
//
// Loads the built .vst3 through the SDK hosting classes, strikes the
// instruments and checks rendered audio against measurements of a TR-808:
// bus layout, silence, the key map, sample-accurate hits, each voice's pitch
// and length, the bass drum's Decay and Tone, the snare's Tone and Snappy,
// tom and conga tuning (shared knobs), the closed hat choking the open one,
// the open hat and cymbal decays, accent and velocity dynamics, click-free
// rolls and state round trips.
//
// Usage: mla_08_tests <path/to/Mla08.vst3> [wav output directory]
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
constexpr ParamID kOutput = 100, kAccent = 101, kDynamics = 102, kBdTone = 104, kBdDecay = 105, kSdTone = 107,
                  kSdSnappy = 108, kLtTuning = 110, kCyDecay = 120, kOhDecay = 122, kChLevel = 123;

// One General MIDI key per instrument.
constexpr int kBD = 36, kRS = 37, kSD = 38, kCP = 39, kLT = 45, kMT = 47, kHT = 50, kCH = 42, kOH = 46, kCY = 49,
              kCB = 56, kHC = 62, kMC = 63, kLC = 64, kMA = 70, kCL = 75;

class Application final : public HostApplication {
  public:
    tresult PLUGIN_API getName(String128 name) override
    {
        const char16_t text[] = u"mla_08_tests";
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
    for(int key : {35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 55, 56, 57, 59, 62, 63,
                   64, 70, 75})
        CHECK(peak(hit(path, key, 1.0f, 0.1).left) > 0.05);
}

void testVoices(const std::string &path, const char *wavDir)
{
    // Each voice is finite, mono, bounded, and about as long as on the
    // recorded 808 at the same settings (40 dB down; knobs centred).
    struct Voice {
        const char *name;
        int key;
        double minLength, maxLength;
    } voices[] = {
        {"bd", kBD, 0.45, 0.80}, {"sd", kSD, 0.09, 0.17}, {"lt", kLT, 0.33, 0.48}, {"mt", kMT, 0.20, 0.32},
        {"ht", kHT, 0.17, 0.30}, {"rs", kRS, 0.01, 0.04}, {"cp", kCP, 0.40, 0.75}, {"cb", kCB, 0.25, 0.40},
        {"cy", kCY, 2.5, 5.0},   {"oh", kOH, 0.28, 0.40}, {"ch", kCH, 0.05, 0.10}, {"lc", kLC, 0.28, 0.40},
        {"mc", kMC, 0.14, 0.22}, {"hc", kHC, 0.13, 0.20}, {"cl", kCL, 0.015, 0.035}, {"ma", kMA, 0.02, 0.045},
    };
    for(const auto &v : voices) {
        const Stereo out = hit(path, v.key, 100.0f / 127.0f, 6.0);
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
            writeWav(std::string(wavDir) + "/mla08_" + v.name + ".wav", out.left);
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

void testBassDrum(const std::string &path)
{
    // The 808 bass drum glides down to 49 Hz as it fades; at full Decay it
    // is still above 51 Hz after 0.2 s.
    const Stereo longest = hit(path, kBD, 100.0f / 127.0f, 2.0, {{kBdDecay, 1.0}});
    const double early = zeroCrossingHz(longest.left, 9600, 14400), late = zeroCrossingHz(longest.left, 48000, 72000);
    // Decay: 40 dB down after 0.07 s at the minimum, 1.6 s at the maximum.
    const double shortest = length40(hit(path, kBD, 100.0f / 127.0f, 1.0, {{kBdDecay, 0.0}}).left);
    const double longLength = length40(longest.left);
    // Tone opens the trigger click: more energy above 1 kHz in the first 2 ms.
    const double dark = highShare(hit(path, kBD, 100.0f / 127.0f, 0.1, {{kBdTone, 0.0}}).left, 0, 96, 1000.0);
    const double bright = highShare(hit(path, kBD, 100.0f / 127.0f, 0.1, {{kBdTone, 1.0}}).left, 0, 96, 1000.0);
    std::printf("bd: %.1f Hz at 0.2 s -> %.1f Hz at 1 s; 40 dB down %.3f s (decay 0) .. %.3f s (decay 1); "
                "attack above 1 kHz %.4f (tone 0) / %.4f (tone 1)\n",
                early, late, shortest, longLength, dark, bright);
    CHECK(early > 51.0 && early < 55.0);
    CHECK(std::fabs(late - 49.1) < 1.0);
    CHECK(shortest > 0.04 && shortest < 0.1);
    CHECK(longLength > 1.3 && longLength < 1.9);
    CHECK(bright > dark * 1.5);
}

void testSnare(const std::string &path)
{
    // Tone moves the snare from its 175 Hz ring to its 336 Hz ring; Snappy
    // adds the noise.
    const Stereo low = hit(path, kSD, 100.0f / 127.0f, 0.5, {{kSdTone, 0.0}, {kSdSnappy, 0.0}});
    const Stereo high = hit(path, kSD, 100.0f / 127.0f, 0.5, {{kSdTone, 1.0}, {kSdSnappy, 0.0}});
    const double lowRatio = toneShare(low.left, 0, 2400, 336.0) / toneShare(low.left, 0, 2400, 175.5);
    const double highRatio = toneShare(high.left, 0, 2400, 336.0) / toneShare(high.left, 0, 2400, 175.5);
    const Stereo snappy = hit(path, kSD, 100.0f / 127.0f, 0.5, {{kSdSnappy, 1.0}});
    const double a = highShare(low.left, 0, 4800, 2000.0), b = highShare(snappy.left, 0, 4800, 2000.0);
    std::printf("snare: 336/175 Hz power %.3f (tone 0) / %.1f (tone 1); above 2 kHz %.3f (snappy 0) / %.2f (snappy 1)\n",
                lowRatio, highRatio, a, b);
    CHECK(lowRatio < 0.1 && highRatio > 3.0);
    CHECK(a < 0.05 && b > 0.3);
}

void testTuning(const std::string &path)
{
    // Toms and congas at the recorded pitches; each tom's Tuning also tunes
    // the conga that shares its knobs.
    const double lt = zeroCrossingHz(hit(path, kLT, 100.0f / 127.0f, 0.5, {{kLtTuning, 0.0}}).left, 1440, 9600);
    const double ltUp = zeroCrossingHz(hit(path, kLT, 100.0f / 127.0f, 0.5, {{kLtTuning, 1.0}}).left, 1440, 9600);
    const double lc = zeroCrossingHz(hit(path, kLC, 100.0f / 127.0f, 0.5, {{kLtTuning, 0.0}}).left, 960, 7200);
    const double lcUp = zeroCrossingHz(hit(path, kLC, 100.0f / 127.0f, 0.5, {{kLtTuning, 1.0}}).left, 960, 7200);
    const double mc = zeroCrossingHz(hit(path, kMC).left, 960, 4800), hc = zeroCrossingHz(hit(path, kHC).left, 960, 4800);
    const double cl = zeroCrossingHz(hit(path, kCL).left, 48, 960);
    std::printf("tuning: lt %.1f .. %.1f Hz, lc %.1f .. %.1f Hz, mc %.1f Hz, hc %.1f Hz, claves %.0f Hz\n", lt, ltUp,
                lc, lcUp, mc, hc, cl);
    CHECK(std::fabs(lt - 81.0) < 3.0 && std::fabs(ltUp - 103.0) < 3.0);
    CHECK(std::fabs(lc - 177.4) < 3.0 && std::fabs(lcUp - 221.8) < 4.0);
    CHECK(mc > 250.0 && mc < 275.0 && hc > 375.0 && hc < 410.0);
    CHECK(std::fabs(cl - 2534.0) < 40.0);
}

void testMetal(const std::string &path)
{
    // The cowbell's two squares, at the recorded 565 and 850 Hz.
    const Stereo cb = hit(path, kCB, 100.0f / 127.0f, 0.5);
    std::printf("cowbell: 850 Hz share %.2f, 565 Hz share %.3f\n", toneShare(cb.left, 0, 9600, 849.6),
                toneShare(cb.left, 0, 9600, 564.8));
    CHECK(toneShare(cb.left, 0, 9600, 849.6) > 0.3);
    CHECK(toneShare(cb.left, 0, 9600, 564.8) > 0.01);
    // The hats are bright metal; the closed hat chokes the open one.
    const Stereo ch = hit(path, kCH), oh = hit(path, kOH, 100.0f / 127.0f, 2.0);
    const double chHigh = highShare(ch.left, 0, 2400, 2000.0), ohHigh = highShare(oh.left, 0, 4800, 2000.0);
    const double chHz = zeroCrossingHz(ch.left, 0, 2400), ohHz = zeroCrossingHz(oh.left, 0, 4800);
    std::printf("hats: energy above 2 kHz CH %.2f, OH %.2f; zero crossings CH %.0f Hz, OH %.0f Hz\n", chHigh, ohHigh,
                chHz, ohHz);
    CHECK(chHigh > 0.6 && ohHigh > 0.6);
    CHECK(chHz > 6000.0 && chHz < 11000.0 && ohHz > 6000.0 && ohHz < 11000.0);
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
    // OH Decay: about 0.1 s at the minimum, 0.7 s at the maximum.
    const double ohShort = length40(hit(path, kOH, 100.0f / 127.0f, 2.0, {{kOhDecay, 0.0}}).left);
    const double ohLong = length40(hit(path, kOH, 100.0f / 127.0f, 2.0, {{kOhDecay, 1.0}}).left);
    // CY Decay lengthens the cymbal.
    const double cyShort = length40(hit(path, kCY, 100.0f / 127.0f, 8.0, {{kCyDecay, 0.0}}).left);
    const double cyLong = length40(hit(path, kCY, 100.0f / 127.0f, 8.0, {{kCyDecay, 1.0}}).left);
    std::printf("decays: oh %.3f .. %.3f s, cy %.2f .. %.2f s\n", ohShort, ohLong, cyShort, cyLong);
    CHECK(ohShort > 0.07 && ohShort < 0.15 && ohLong > 0.6 && ohLong < 0.85);
    CHECK(cyLong > cyShort * 2.0);
}

void testDynamics(const std::string &path)
{
    // 808 Accent: velocities up to 100 play at one level; above 100 accents.
    const double soft = peak(hit(path, kLT, 20.0f / 127.0f).left), normal = peak(hit(path, kLT).left);
    const double accented = peak(hit(path, kLT, 1.0f).left);
    const double noAccent = peak(hit(path, kLT, 1.0f, 1.0, {{kAccent, 0.0}}).left);
    std::printf("dynamics: v20 %.3f, v100 %.3f, v127 %.3f, v127 accent 0 %.3f\n", soft, normal, accented, noAccent);
    CHECK(std::fabs(soft - normal) < 1e-6);
    CHECK(accented > normal * 1.5 && accented < normal * 2.0);
    CHECK(std::fabs(noAccent - normal) < 1e-6);
    // An accented bass drum is louder and rings higher.
    const Stereo bd = hit(path, kBD), bdAccent = hit(path, kBD, 1.0f);
    CHECK(peak(bdAccent.left) > peak(bd.left) * 1.3);
    CHECK(zeroCrossingHz(bdAccent.left, 2400, 7200) > zeroCrossingHz(bd.left, 2400, 7200) + 0.5);
    // Velocity: the level follows velocity.
    const double low = peak(hit(path, kLT, 0.5f, 1.0, {{kDynamics, 1.0}}).left);
    const double high = peak(hit(path, kLT, 1.0f, 1.0, {{kDynamics, 1.0}}).left);
    CHECK(low < high * 0.4);
    // Output 0 is silence.
    CHECK(peak(hit(path, kBD, 100.0f / 127.0f, 1.0, {{kOutput, 0.0}}).left) == 0.0);
}

void testRetrigger(const std::string &path)
{
    // A fast roll stays bounded and does not click: retriggers continue the
    // ring, so no step is larger than in a single hit.
    const auto largestStep = [](const std::vector<float> &x) {
        double jump = 0.0;
        for(size_t i = 1; i < x.size(); ++i)
            jump = std::max(jump, static_cast<double>(std::fabs(x[i] - x[i - 1])));
        return jump;
    };
    for(int key : {kLT, kHT, kLC, kHC}) {
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
        CHECK(peak(roll.left) < 1.6);
        CHECK(jump < single * 1.5);
    }
    // A bass drum roll stays bounded too.
    Instance plugin;
    CHECK(plugin.open(path));
    for(int i = 0; i < 16; ++i)
        plugin.noteOn(kBD, 1.0f, i * 15);
    const Stereo roll = plugin.render(kBlock * 16);
    CHECK(finite(roll) && peak(roll.left) < 1.5);
}

void testState(const std::string &path)
{
    Instance a, b;
    CHECK(a.open(path) && b.open(path));
    a.param(kBdDecay, 0.8);
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
        std::fprintf(stderr, "usage: %s <Mla08.vst3> [wav output directory]\n", argv[0]);
        return 2;
    }
    const std::string path = argv[1];
    testLayout(path);
    testSilenceAndKeys(path);
    testVoices(path, argc > 2 ? argv[2] : nullptr);
    testStartOffset(path);
    testBassDrum(path);
    testSnare(path);
    testTuning(path);
    testMetal(path);
    testDynamics(path);
    testRetrigger(path);
    testState(path);
    if(failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all Mla 08 tests passed\n");
    return 0;
}
