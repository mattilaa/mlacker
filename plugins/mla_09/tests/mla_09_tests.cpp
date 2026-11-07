// Offline functional tests for the Mla 09 bundle.
//
// Loads the built .vst3 through the SDK hosting classes, strikes the
// instruments and checks rendered audio against measurements of TR-909
// recordings: bus layout, silence, the key map, sample-accurate hits, each
// voice's length, the bass drum's glide, hold, Attack and Decay, the snare's
// Tune, Tone and Snappy, tom pitch, glide and Decay, the ROM voices (the same
// sample on every hit, the closed hat choking the open one, the hats' Decay,
// the cymbals' Tune running the ROM clock), samples loaded from a folder,
// Drive, accent and velocity dynamics, click-free retriggers, the MIDI CC map
// and state round trips.
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
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

int failures = 0;
// The samples folder the tests run with: an empty one, so the built-in ROMs
// play whatever is installed on the machine (see main).
std::string isolatedSamples;
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
                  kHtTune = 118, kChLevel = 122, kChDecay = 123, kOhLevel = 124, kOhDecay = 125, kCrTune = 127, kRdTune = 129,
                  kDrive = 130, kSdDecay = 131, kCrDecay = 132, kRdDecay = 133;

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
        {"bd", kBD, 0.30, 0.50}, {"sd", kSD, 0.25, 0.55}, {"lt", kLT, 0.50, 0.80}, {"mt", kMT, 0.35, 0.60},
        {"ht", kHT, 0.35, 0.60}, {"rs", kRS, 0.04, 0.20}, {"cp", kCP, 0.25, 0.60}, {"ch", kCH, 0.14, 0.23},
        {"oh", kOH, 0.40, 0.60}, {"cr", kCR, 0.80, 1.20}, {"rd", kRD, 0.75, 1.15},
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

// RMS above 1 kHz in x[from, to).
double highRms(const std::vector<float> &x, size_t from, size_t to)
{
    const double a = std::exp(-2.0 * M_PI * 1000.0 / kRate);
    double low = 0.0, sum = 0.0;
    for(size_t i = 0; i < std::min(to, x.size()); ++i) {
        low = (1.0 - a) * x[i] + a * low;
        if(i >= from)
            sum += (x[i] - low) * (x[i] - low);
    }
    return std::sqrt(sum / static_cast<double>(to - from));
}

// Frequency of each upward zero-crossing cycle after a 1.5 kHz low-pass:
// (start time in seconds, Hz).
std::vector<std::pair<double, double>> cycles(const std::vector<float> &x)
{
    const double a = std::exp(-2.0 * M_PI * 1500.0 / kRate);
    std::vector<double> y(x.size());
    double s1 = 0.0, s2 = 0.0;
    for(size_t i = 0; i < x.size(); ++i) {
        s1 = (1.0 - a) * x[i] + a * s1;
        s2 = (1.0 - a) * s1 + a * s2;
        y[i] = s2;
    }
    std::vector<double> ups;
    for(size_t i = 1; i < y.size(); ++i)
        if(y[i - 1] < 0.0 && y[i] >= 0.0)
            ups.push_back(static_cast<double>(i - 1) + y[i - 1] / (y[i - 1] - y[i]));
    std::vector<std::pair<double, double>> out;
    for(size_t i = 1; i < ups.size(); ++i)
        out.emplace_back(ups[i - 1] / kRate, kRate / (ups[i] - ups[i - 1]));
    return out;
}

void testBassDrum(const std::string &path)
{
    // As measured on a TR-909: the pitch glides from about 270 Hz down to
    // about 54 Hz (at centred Tune, f = 54 + 216 exp(-t / 13 ms)); Tune
    // lengthens the glide. The VCO runs freely, so the cycles fall anywhere:
    // check each one in the first 40 ms against the glide at its midpoint.
    const Stereo centre = hit(path, kBD, 100.0f / 127.0f, 1.0);
    int checked = 0;
    for(const auto &cycle : cycles(centre.left)) {
        const double mid = cycle.first + 0.5 / cycle.second;
        if(cycle.first < 0.0015 || mid > 0.04)
            continue;
        const double expected = 54.1 + 216.0 * std::exp(-mid / 0.013);
        CHECK(std::fabs(cycle.second - expected) < expected * 0.1);
        ++checked;
    }
    CHECK(checked >= 3);
    const Stereo flat = hit(path, kBD, 100.0f / 127.0f, 1.0, {{kBdTune, 0.0}});
    const Stereo swept = hit(path, kBD, 100.0f / 127.0f, 1.0, {{kBdTune, 1.0}});
    const double flatEarly = zeroCrossingHz(flat.left, 240, 1920), sweptEarly = zeroCrossingHz(swept.left, 240, 1920);
    const double rest = zeroCrossingHz(centre.left, 9600, 19200);
    // The VCA holds through the glide: still within 2 dB of the peak at 40 ms,
    // then decays (about 8 dB down at 120 ms at centred Decay).
    const double top = peak(centre.left, 0, 1920);
    const double at40 = peak(centre.left, 1680, 2160), at120 = peak(centre.left, 5520, 6000);
    // Decay: 40 dB down after about 0.3 s at the minimum, 0.5 s at the maximum.
    const double shortest = length40(hit(path, kBD, 100.0f / 127.0f, 2.0, {{kBdDecay, 0.0}}).left);
    const double longest = length40(hit(path, kBD, 100.0f / 127.0f, 2.0, {{kBdDecay, 1.0}}).left);
    // Attack adds more of the click: the difference between Attack 1 and 0
    // (the noise is the same in both) is a short, bright burst in the first
    // few milliseconds, gone by 10 ms.
    const Stereo softHit = hit(path, kBD, 100.0f / 127.0f, 0.1, {{kBdAttack, 0.0}});
    const Stereo hardHit = hit(path, kBD, 100.0f / 127.0f, 0.1, {{kBdAttack, 1.0}});
    std::vector<float> click(softHit.left.size());
    for(size_t i = 0; i < click.size(); ++i)
        click[i] = hardHit.left[i] - softHit.left[i];
    const double soft = rms(click, 0, 96), hard = rms(click, 480, 960);
    std::printf("bd: first 40 ms %.0f Hz (tune 0) / %.0f Hz (tune 1), %.1f Hz at 0.2 s; at 40 ms %.1f dB, 120 ms %.1f dB; "
                "40 dB down %.3f .. %.3f s; added click rms %.4f (0-2 ms) / %.5f (10-20 ms)\n",
                flatEarly, sweptEarly, rest, 20 * std::log10(at40 / top), 20 * std::log10(at120 / top), shortest, longest,
                soft, hard);
    CHECK(sweptEarly > flatEarly * 1.8);
    CHECK(rest > 52.5 && rest < 55.5);
    CHECK(at40 > top * 0.79);
    CHECK(at120 < top * 0.5 && at120 > top * 0.28);
    CHECK(shortest > 0.2 && shortest < 0.4);
    CHECK(longest > 0.42 && longest < 0.7);
    CHECK(soft > 0.01 && hard < soft * 0.05 && highShare(click, 0, 96, 1000.0) > 0.5);
}

void testSnare(const std::string &path)
{
    // As on the 909: Tune moves the lower VCO from about 114 Hz to 222 Hz;
    // Snappy adds the noise; Tone sets how long the noise lasts.
    const Stereo lowTune = hit(path, kSD, 100.0f / 127.0f, 0.3, {{kSdTune, 0.0}, {kSdSnappy, 0.0}});
    const Stereo highTune = hit(path, kSD, 100.0f / 127.0f, 0.3, {{kSdTune, 1.0}, {kSdSnappy, 0.0}});
    const auto strongest = [](const std::vector<float> &x) {
        double best = 0.0, at = 0.0;
        for(double hz = 80.0; hz < 500.0; hz += 1.0) {
            const double share = toneShare(x, 480, 4800, hz);
            if(share > best) {
                best = share;
                at = hz;
            }
        }
        return at;
    };
    const double low = strongest(lowTune.left), high = strongest(highTune.left);
    const Stereo dry = hit(path, kSD, 100.0f / 127.0f, 0.5, {{kSdSnappy, 0.0}});
    const Stereo snappy = hit(path, kSD, 100.0f / 127.0f, 0.5, {{kSdSnappy, 1.0}});
    const double a = highShare(dry.left, 0, 4800, 2000.0), b = highShare(snappy.left, 0, 4800, 2000.0);
    const Stereo shortNoise = hit(path, kSD, 100.0f / 127.0f, 0.5, {{kSdTone, 0.0}, {kSdSnappy, 1.0}});
    const Stereo longNoise = hit(path, kSD, 100.0f / 127.0f, 0.5, {{kSdTone, 1.0}, {kSdSnappy, 1.0}});
    const double d = highRms(shortNoise.left, 4800, 9600), e = highRms(longNoise.left, 4800, 9600);
    const double early = highShare(shortNoise.left, 0, 1920, 6000.0), earlyLong = highShare(longNoise.left, 0, 1920, 6000.0);
    std::printf("snare: %.0f .. %.0f Hz (tune); above 2 kHz %.3f (snappy 0) / %.2f (snappy 1); "
                "noise at 0.1-0.2 s %.4f (tone 0) / %.4f (tone 1); colour %.3f / %.3f\n", low, high, a, b, d, e, early,
                earlyLong);
    CHECK(std::fabs(low - 114.0) < 8.0 && std::fabs(high - 222.0) < 12.0);
    CHECK(a < 0.05 && b > 0.15);
    CHECK(e > d * 4.0);
    CHECK(std::fabs(early - earlyLong) < 0.1 * earlyLong);
    // The wire tail needs midrange energy, rather than only a thin hiss.
    // Its bright attack decays separately from the darker wire rattle.
    const double attackAir = highShare(snappy.left, 0, 960, 6000.0);
    const double tailAir = highShare(snappy.left, 4800, 7200, 6000.0);
    std::printf("snare air: attack %.3f, tail %.3f\n", attackAir, tailAir);
    CHECK(tailAir < 0.30 && tailAir > 0.01);
    CHECK(highShare(dry.left, 4800, 7200, 2000.0) < 0.03);
}

void testSnareBody(const std::string &path)
{
    // Turn off snappy so noise cannot disguise a missing shell resonance.
    // The upper body should still be audible after the first 30 ms.
    const Stereo dry = hit(path, kSD, 100.0f / 127.0f, 0.5, {{kSdSnappy, 0.0}});
    double lower = 0.0, upper = 0.0;
    for(double hz = 155.0; hz <= 180.0; hz += 1.0)
        lower = std::max(lower, toneShare(dry.left, 1440, 3840, hz));
    for(double hz = 235.0; hz <= 265.0; hz += 1.0)
        upper = std::max(upper, toneShare(dry.left, 1440, 3840, hz));
    const double balance = 10.0 * std::log10(upper / lower);
    std::printf("snare shell: upper/lower %.1f dB at 30-80 ms\n", balance);
    CHECK(balance > -9.0 && balance < -1.0);
}

void testToms(const std::string &path)
{
    // Where each tom settles across Tune: low tom 60 .. 120 Hz (as measured
    // on the 909); centred, the toms are a root, fifth and minor seventh
    // (94, 140.8, 167.5 Hz). Each starts higher and glides down; Decay
    // lengthens it.
    const double lt = zeroCrossingHz(hit(path, kLT, 100.0f / 127.0f, 0.5, {{kLtTune, 0.0}}).left, 9600, 19200);
    const double ltUp = zeroCrossingHz(hit(path, kLT, 100.0f / 127.0f, 0.5, {{kLtTune, 1.0}}).left, 9600, 19200);
    const double mt = zeroCrossingHz(hit(path, kMT, 100.0f / 127.0f, 0.5, {{kMtTune, 0.5}}).left, 9600, 19200);
    const double ht = zeroCrossingHz(hit(path, kHT, 100.0f / 127.0f, 0.5, {{kHtTune, 0.5}}).left, 9600, 19200);
    const auto c = cycles(hit(path, kHT, 100.0f / 127.0f, 0.5).left);
    double early = 0.0;
    for(const auto &cycle : c)
        if(cycle.first > 0.01 && cycle.first < 0.03) {
            early = cycle.second;
            break;
        }
    const double shortTom = length40(hit(path, kLT, 100.0f / 127.0f, 3.0, {{kLtDecay, 0.0}}).left);
    const double longTom = length40(hit(path, kLT, 100.0f / 127.0f, 3.0, {{kLtDecay, 1.0}}).left);
    std::printf("toms: lt %.1f .. %.1f Hz, mt %.1f Hz, ht %.1f Hz (%.0f Hz at 10-30 ms); lt decay %.2f .. %.2f s\n", lt,
                ltUp, mt, ht, early, shortTom, longTom);
    CHECK(std::fabs(lt - 60.0) < 3.0 && std::fabs(ltUp - 120.0) < 5.0);
    const double lc = zeroCrossingHz(hit(path, kLT, 100.0f / 127.0f, 0.5).left, 9600, 19200);
    std::printf("toms centred: %.1f, %.1f, %.1f Hz = %.2f and %.2f semitones above the low tom\n", lc, mt, ht,
                12.0 * std::log2(mt / lc), 12.0 * std::log2(ht / lc));
    CHECK(std::fabs(12.0 * std::log2(mt / lc) - 7.0) < 0.3);   // a fifth
    CHECK(std::fabs(12.0 * std::log2(ht / lc) - 10.0) < 0.3);  // a minor seventh
    CHECK(early > ht * 1.08 && early < ht * 1.3);
    CHECK(longTom > shortTom * 1.3);
    // As on the 909, the tom hits with a strong 2nd harmonic that mellows as
    // it rings: about -8 dB of the pitch in the first 30 ms, -22 dB later.
    const Stereo low = hit(path, kLT, 100.0f / 127.0f, 0.5);
    // Locate body cycles below the noise band. Counting raw crossings now
    // mistakes the stick noise for a higher oscillator pitch.
    std::vector<float> bodyPitch(low.left.size());
    const double pole = std::exp(-2.0 * M_PI * 700.0 / kRate);
    double lp1 = 0.0, lp2 = 0.0;
    for(size_t i = 0; i < low.left.size(); ++i) {
        lp1 += (1.0 - pole) * (low.left[i] - lp1);
        lp2 += (1.0 - pole) * (lp1 - lp2);
        bodyPitch[i] = static_cast<float>(lp2);
    }
    const auto second = [&](size_t from, size_t to) {
        const double f0 = zeroCrossingHz(bodyPitch, from, to);
        return 10.0 * std::log10(toneShare(low.left, from, to, 2.0 * f0) / toneShare(low.left, from, to, f0));
    };
    const double early2 = second(0, 1440), late2 = second(9600, 19200);
    std::printf("tom 2nd harmonic: %.1f dB early, %.1f dB late\n", early2, late2);
    CHECK(early2 > -13.0 && late2 < -17.0 && late2 > -28.0);
    // Each sustained tom should retain a round body without the strong
    // third harmonic produced by the previous heavy waveshaper.
    for(int key : {kLT, kMT, kHT}) {
        const Stereo body = hit(path, key, 100.0f / 127.0f, 0.5);
        const double f0 = zeroCrossingHz(body.left, 9600, 19200);
        const double third = 10.0 * std::log10(toneShare(body.left, 9600, 19200, 3.0 * f0) /
                                              toneShare(body.left, 9600, 19200, f0));
        std::printf("tom key %d: tail third harmonic %.1f dB\n", key, third);
        CHECK(third < -26.0);
    }
}

void testTomNoiseAttack(const std::string &path)
{
    for(int key : {kLT, kMT, kHT}) {
        Instance plugin;
        CHECK(plugin.open(path));
        plugin.noteOn(key);
        const Stereo first = plugin.render(kBlock * 600);
        plugin.noteOn(key);
        const Stereo second = plugin.render(kBlock * 40);
        // Fresh tom hits reset the pitched oscillator identically, while
        // the shared noise runs continuously. Subtract hits to isolate the
        // noise rather than count oscillator harmonics as stick attack.
        std::vector<float> noise(second.left.size());
        for(size_t i = 0; i < noise.size(); ++i)
            noise[i] = (second.left[i] - first.left[i]) / std::sqrt(2.0f);
        const double onset = rms(noise, 0, 480);
        const double behind = rms(noise, 720, 2160);
        const double tail = rms(noise, 5760, 8640);
        std::printf("tom noise key %d: onset %.4f, behind body %.4f, tail %.5f\n", key, onset, behind, tail);
        CHECK(onset > 0.03 && onset < 0.2);
        CHECK(behind > 0.012 && behind < onset * 0.7);
        CHECK(tail < behind * 0.12);
    }
}

void testClap(const std::string &path)
{
    // As measured on the 909: four bursts at 0, 10.5, 22.5 and 32.5 ms, each
    // falling fast; the fourth opens the tail, which is darker than the bursts.
    const Stereo cp = hit(path, kCP, 100.0f / 127.0f, 0.6);
    // Each burst's level: the RMS of its first 2 ms (a peak of decaying noise
    // swings by a few dB from hit to hit).
    const auto at = [&](double ms) { return rms(cp.left, static_cast<size_t>(ms * 48.0), static_cast<size_t>((ms + 2.0) * 48.0)); };
    double top = 0.0;
    for(double ms : {0.0, 10.5, 22.5, 32.5})
        top = std::max(top, at(ms));
    std::printf("clap bursts (rms of 2 ms): %.3f %.3f %.3f %.3f\n", at(0.0), at(10.5), at(22.5), at(32.5));
    for(double ms : {0.0, 10.5, 22.5, 32.5}) {
        CHECK(at(ms) > top * 0.45);
        if(ms < 30.0)
            CHECK(at(ms + 8.0) < at(ms) * 0.2);  // each burst falls away before the next
    }
    const double burstBright = highShare(cp.left, 0, 1440, 2000.0), tailBright = highShare(cp.left, 2880, 9600, 2000.0);
    const double early = rms(cp.left, 1920, 2400), at150 = rms(cp.left, 7200, 7680);
    std::printf("clap: burst/tail energy above 2 kHz %.2f / %.2f; tail 150 ms %.1f dB re 40 ms\n", burstBright, tailBright,
                20 * std::log10(at150 / early));
    CHECK(burstBright > tailBright * 1.3);
    // The 909 recordings: about 24 dB down from 40 ms to 150 ms.
    CHECK(at150 < early * 0.15 && at150 > early * 0.03);
    CHECK(peak(cp.left) < 0.8);
}

void testAnalogDrift(const std::string &path)
{
    // Each analog hit drifts a little in tuning and gain. Two hits in one
    // instance differ, with their levels within about 1 dB.
    for(int key : {kBD, kSD}) {
        Instance plugin;
        CHECK(plugin.open(path));
        plugin.noteOn(key);
        const Stereo first = plugin.render(kBlock * 40);
        plugin.noteOn(key);
        const Stereo second = plugin.render(kBlock * 40);
        const double a = rms(first.left, 0, 4800), b = rms(second.left, 0, 4800);
        std::printf("drift on key %d: rms %.4f / %.4f\n", key, a, b);
        CHECK(!std::equal(first.left.begin(), first.left.begin() + 4800, second.left.begin()));
        CHECK(std::fabs(20.0 * std::log10(b / a)) < 1.0);
    }
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
    CHECK(chHigh > 0.6 && ohHigh > 0.6);
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
    // ... and the open hat chokes the closed one: the hats never sound together.
    {
        Instance mono;
        CHECK(mono.open(path));
        mono.param(kChDecay, 1.0);
        mono.noteOn(kCH, 1.0f);
        mono.render(kBlock * 4);
        mono.param(kOhLevel, 0.0); // the choke alone, without the open hat's own sound
        mono.noteOn(kOH, 1.0f);
        const Stereo cut = mono.render(kBlock * 8);
        const double was = rms(cut.left, 0, 64), now = rms(cut.left, 960, 1920);
        std::printf("choke: closed hat %.4f -> %.6f\n", was, now);
        CHECK(was > 0.01 && now < was * 0.001);
    }
    // Decay knobs.
    const double chShort = length40(hit(path, kCH, 100.0f / 127.0f, 1.0, {{kChDecay, 0.0}}).left);
    const double chLong = length40(hit(path, kCH, 100.0f / 127.0f, 1.0, {{kChDecay, 1.0}}).left);
    const double ohShort = length40(hit(path, kOH, 100.0f / 127.0f, 2.0, {{kOhDecay, 0.0}}).left);
    const double ohLong = length40(hit(path, kOH, 100.0f / 127.0f, 2.0, {{kOhDecay, 1.0}}).left);
    std::printf("hat decays: ch %.3f .. %.3f s, oh %.3f .. %.3f s\n", chShort, chLong, ohShort, ohLong);
    // As measured on the 909: CH 0.11 .. 0.2 s, OH 0.28 .. 0.52 s (40 dB down).
    CHECK(chShort > 0.09 && chShort < 0.13 && chLong > 0.17 && chLong < 0.23);
    CHECK(ohShort > 0.24 && ohShort < 0.32 && ohLong > 0.45 && ohLong < 0.6);
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
    // On the 909 the ride is brighter than the crash.
    const double crHigh = highShare(hit(path, kCR, 100.0f / 127.0f, 0.5).left, 0, 9600, 6000.0);
    const double rdHigh = highShare(hit(path, kRD, 100.0f / 127.0f, 0.5).left, 0, 9600, 6000.0);
    std::printf("cymbals: above 6 kHz crash %.2f, ride %.2f\n", crHigh, rdHigh);
    CHECK(rdHigh > crHigh);
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
    for(int key : {kBD, kSD, kLT, kMT, kHT, kOH, kCR}) {
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
        // Kick and tom noise attacks sum with a body already ringing.
        // Allow transient headroom while still bounding rapid rolls.
        const bool noisyAttack = key == kBD || key == kLT || key == kMT || key == kHT;
        CHECK(peak(roll.left) < (noisyAttack ? 2.2 : 1.6));
        if(key != kBD)
            CHECK(jump < single * 1.6);
    }
}

void testMidiMapping(const std::string &path)
{
    // CC 7 is Output, CC 20-30 the levels, CC 102-118 Accent and the knobs,
    // CC 119 Drive, CC 120-122 SD, CR and RD Decay.
    Instance plugin;
    CHECK(plugin.open(path));
    auto mapping = U::cast<IMidiMapping>(plugin.component);
    CHECK(mapping);
    if(!mapping)
        return;
    const auto map = [&](int cc) {
        ParamID id = 0;
        return mapping->getMidiControllerAssignment(0, 0, static_cast<CtrlNumber>(cc), id) == kResultOk
                   ? static_cast<int>(id)
                   : -1;
    };
    CHECK(map(7) == 100);
    CHECK(map(20) == 103 && map(21) == 107 && map(27) == 122 && map(30) == 128);
    CHECK(map(102) == kAccent && map(103) == kBdTune && map(105) == kBdDecay && map(108) == kSdSnappy);
    CHECK(map(115) == kChDecay && map(116) == kOhDecay && map(117) == kCrTune && map(118) == kRdTune);
    CHECK(map(119) == kDrive && map(120) == kSdDecay && map(121) == kCrDecay && map(122) == kRdDecay);
    CHECK(map(19) == -1 && map(31) == -1 && map(101) == -1 && map(123) == -1 && map(1) == -1);
}

void testExtraDecays(const std::string &path)
{
    // SD Decay stretches the snare's tone body (centred = the 909's).
    const double shortBody = length40(hit(path, kSD, 100.0f / 127.0f, 1.5, {{kSdDecay, 0.0}, {kSdSnappy, 0.0}}).left);
    const double longBody = length40(hit(path, kSD, 100.0f / 127.0f, 1.5, {{kSdDecay, 1.0}, {kSdSnappy, 0.0}}).left);
    // CR and RD Decay shorten the cymbals; at the top (the default) they ring
    // for their whole sample, as on the 909.
    const double crShort = length40(hit(path, kCR, 100.0f / 127.0f, 3.0, {{kCrDecay, 0.0}}).left);
    const double crFull = length40(hit(path, kCR, 100.0f / 127.0f, 3.0).left);
    const double rdShort = length40(hit(path, kRD, 100.0f / 127.0f, 3.0, {{kRdDecay, 0.3}}).left);
    const double rdFull = length40(hit(path, kRD, 100.0f / 127.0f, 3.0, {{kRdDecay, 1.0}}).left);
    std::printf("extra decays: snare body %.2f .. %.2f s; crash %.2f / %.2f s; ride %.2f / %.2f s\n", shortBody, longBody,
                crShort, crFull, rdShort, rdFull);
    CHECK(longBody > shortBody * 2.5);
    CHECK(crShort < 0.3 && crFull > 0.8);
    CHECK(rdShort < rdFull * 0.7 && rdFull > 0.75);
}

void testDrive(const std::string &path)
{
    // Drive saturates the output: the tail comes up against the peak (lower
    // crest factor) and the peak stays bounded. 0 is clean.
    const Stereo clean = hit(path, kBD, 100.0f / 127.0f, 0.6);
    const Stereo driven = hit(path, kBD, 100.0f / 127.0f, 0.6, {{kDrive, 1.0}});
    const double cleanCrest = peak(clean.left) / rms(clean.left, 0, 14400);
    const double drivenCrest = peak(driven.left) / rms(driven.left, 0, 14400);
    std::printf("drive: crest %.2f clean / %.2f driven; peak %.3f / %.3f\n", cleanCrest, drivenCrest, peak(clean.left),
                peak(driven.left));
    CHECK(finite(driven));
    CHECK(drivenCrest < cleanCrest * 0.8);
    CHECK(peak(driven.left) < 1.0);
    CHECK(std::equal(clean.left.begin(), clean.left.end(), hit(path, kBD, 100.0f / 127.0f, 0.6, {{kDrive, 0.0}}).left.begin()));
}

void testSampleFolder(const std::string &path)
{
    // Recordings in $MLA09_SAMPLES replace the built-in ROMs: a 1 kHz tone as
    // crash.wav plays as the crash; the ride keeps its built-in ROM.
    char dir[] = "/tmp/mla09-samples-XXXXXX";
    if(!mkdtemp(dir)) {
        CHECK(false);
        return;
    }
    std::vector<float> tone(static_cast<size_t>(kRate * 0.5));
    for(size_t i = 0; i < tone.size(); ++i)
        tone[i] = static_cast<float>(0.5 * std::sin(2.0 * M_PI * 1000.0 * i / kRate) * std::exp(-static_cast<double>(i) / kRate / 0.2));
    const std::string file = std::string(dir) + "/crash.wav";
    writeWav(file, tone);
    // ... and a 2 kHz tone as clap.wav plays as the clap.
    std::vector<float> clapTone(tone.size());
    for(size_t i = 0; i < clapTone.size(); ++i)
        clapTone[i] = static_cast<float>(0.5 * std::sin(2.0 * M_PI * 2000.0 * i / kRate) * std::exp(-static_cast<double>(i) / kRate / 0.1));
    const std::string clapFile = std::string(dir) + "/clap.wav";
    writeWav(clapFile, clapTone);
    const Stereo clapSynth = hit(path, kCP, 100.0f / 127.0f, 0.3);
    const Stereo builtIn = hit(path, kCR, 100.0f / 127.0f, 0.3);
    setenv("MLA09_SAMPLES", dir, 1);
    const Stereo loaded = hit(path, kCR, 100.0f / 127.0f, 0.3);
    const Stereo ride = hit(path, kRD, 100.0f / 127.0f, 0.3);
    const Stereo clapLoaded = hit(path, kCP, 100.0f / 127.0f, 0.3);
    setenv("MLA09_SAMPLES", isolatedSamples.c_str(), 1);
    std::remove(clapFile.c_str());
    const double clapBefore = toneShare(clapSynth.left, 480, 4800, 2000.0);
    const double clapAfter = toneShare(clapLoaded.left, 480, 4800, 2000.0);
    std::printf("sample folder: 2 kHz share of the clap %.3f synthesized / %.3f loaded\n", clapBefore, clapAfter);
    CHECK(clapAfter > 0.5 && clapBefore < 0.1);
    const Stereo rideBuiltIn = hit(path, kRD, 100.0f / 127.0f, 0.3);
    std::remove(file.c_str());
    rmdir(dir);
    const double before = toneShare(builtIn.left, 480, 4800, 1000.0), after = toneShare(loaded.left, 480, 4800, 1000.0);
    std::printf("sample folder: 1 kHz share of the crash %.3f built in / %.3f loaded\n", before, after);
    CHECK(after > 0.5 && before < 0.05);
    CHECK(std::equal(ride.left.begin(), ride.left.end(), rideBuiltIn.left.begin()));
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
    // Run against the built-in ROMs, not samples installed on this machine,
    // unless MLA09_TEST_KEEP_SAMPLES is set (to render with them).
    char empty[] = "/tmp/mla09-no-samples-XXXXXX";
    if(!std::getenv("MLA09_TEST_KEEP_SAMPLES") && mkdtemp(empty)) {
        isolatedSamples = empty;
        setenv("MLA09_SAMPLES", empty, 1);
    }
    testLayout(path);
    testSilenceAndKeys(path);
    testVoices(path, argc > 2 ? argv[2] : nullptr);
    testStartOffset(path);
    testBassDrum(path);
    testSnare(path);
    testSnareBody(path);
    testToms(path);
    testTomNoiseAttack(path);
    testClap(path);
    testAnalogDrift(path);
    testSamples(path);
    testHats(path);
    testCymbals(path);
    testDynamics(path);
    testRetrigger(path);
    testMidiMapping(path);
    testExtraDecays(path);
    testDrive(path);
    testSampleFolder(path);
    testState(path);
    if(!isolatedSamples.empty())
        rmdir(isolatedSamples.c_str());
    if(failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all Mla 09 tests passed\n");
    return 0;
}
