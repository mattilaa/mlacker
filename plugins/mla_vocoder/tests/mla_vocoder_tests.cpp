// Offline functional tests for the Mla Vocoder bundle.
//
// Loads the built .vst3 through the SDK hosting classes, feeds the modulator
// (main input), the sidechain carrier and MIDI, and checks rendered audio:
// bus layout, silence without a modulator or notes, spectral following (the
// output's energy sits where the modulator's is), formant shift, external and
// split-input carriers, the choir and direct synth paths, pitch and pitch
// bend, sample-accurate notes and state round trips.
//
// Usage: mla_vocoder_tests <path/to/MlaVocoder.vst3>

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
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
constexpr double kPi = 3.14159265358979323846;

// Parameter IDs (see plugin.cpp).
constexpr ParamID kCarrier = 100, kInputMode = 101, kBands = 102, kFormant = 103, kSibilance = 106, kNoise = 107;
constexpr ParamID kWidth = 108, kVocoderLevel = 109, kChoirLevel = 110, kSynthLevel = 111, kDryLevel = 112;
constexpr ParamID kEnsemble = 113, kOutput = 115;
constexpr ParamID kMale = 202, kFemale = 203, kCutoff = 205, kVibDepth = 214, kTune = 216, kBendRange = 218;
constexpr ParamID kPitchBend = 219;
constexpr double kUnity = 60.0 / 66.0;

class Application final : public HostApplication {
  public:
    tresult PLUGIN_API getName(String128 name) override
    {
        const char16_t text[] = u"mla_vocoder_tests";
        std::copy(std::begin(text), std::end(text), name);
        return kResultOk;
    }
};
Application application;

// Generator: sample index -> value.
using Signal = std::function<float(int64_t)>;

Signal silence() { return [](int64_t) { return 0.0f; }; }
Signal sine(double hz, double amplitude = 0.5)
{
    return [=](int64_t n) { return static_cast<float>(amplitude * std::sin(2.0 * kPi * hz * n / kRate)); };
}
Signal saw(double hz, double amplitude = 0.5)
{
    return [=](int64_t n) {
        const double t = std::fmod(hz * n / kRate, 1.0);
        return static_cast<float>(amplitude * (2.0 * t - 1.0));
    };
}

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
    int64_t clock = 0;

    bool open(const std::string &path, bool sidechain = true)
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
        SpeakerArrangement ins[2] = {SpeakerArr::kStereo, SpeakerArr::kStereo};
        SpeakerArrangement stereo = SpeakerArr::kStereo;
        if(processor->setBusArrangements(ins, 2, &stereo, 1) != kResultOk)
            return false;
        component->activateBus(kAudio, kInput, 0, true);
        component->activateBus(kAudio, kInput, 1, sidechain);
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

    void noteOn(int pitch, float velocity = 1.0f, int32 offset = 0)
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

    // Render `frames` (a multiple of the block). Main input channels get
    // `left`/`right` (right defaults to left), the sidechain gets `side`.
    Stereo render(int frames, const Signal &left, const Signal &side = silence(), const Signal &right = nullptr)
    {
        Stereo out;
        for(int done = 0; done < frames; done += kBlock) {
            data.numSamples = kBlock;
            for(int32 i = 0; i < kBlock; ++i) {
                const int64_t n = clock + i;
                const float l = left(n);
                data.inputs[0].channelBuffers32[0][i] = l;
                data.inputs[0].channelBuffers32[1][i] = right ? right(n) : l;
                data.inputs[1].channelBuffers32[0][i] = side(n);
                data.inputs[1].channelBuffers32[1][i] = side(n);
            }
            processor->process(data);
            events.clear();
            changes.clearQueue();
            clock += kBlock;
            const auto &bus = data.outputs[0];
            for(int32 i = 0; i < kBlock; ++i) {
                out.left.push_back(bus.channelBuffers32[0][i]);
                out.right.push_back(bus.channelBuffers32[1][i]);
            }
        }
        return out;
    }

    // A plain setup for spectral checks: no ensemble, no stereo spread, no
    // sibilance or noise, so only the vocoder bands reach the output.
    void plain()
    {
        param(kEnsemble, 0.0);
        param(kWidth, 0.0);
        param(kSibilance, 0.0);
        param(kNoise, 0.0);
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

double peak(const std::vector<float> &x)
{
    double p = 0.0;
    for(float v : x)
        p = std::max(p, static_cast<double>(std::fabs(v)));
    return p;
}

bool finite(const Stereo &s)
{
    for(size_t i = 0; i < s.left.size(); ++i)
        if(!std::isfinite(s.left[i]) || !std::isfinite(s.right[i]))
            return false;
    return true;
}

// Energy of `x[from..]` in a band around `hz` (Goertzel over a few bins).
double bandEnergy(const std::vector<float> &x, size_t from, double low, double high)
{
    const size_t n = x.size() - from;
    double total = 0.0;
    const double step = kRate / static_cast<double>(n);
    for(double hz = low; hz <= high; hz += step) {
        const double w = 2.0 * kPi * hz / kRate;
        const double c = 2.0 * std::cos(w);
        double s1 = 0.0, s2 = 0.0;
        for(size_t i = from; i < x.size(); ++i) {
            const double s = x[i] + c * s1 - s2;
            s2 = s1;
            s1 = s;
        }
        total += s1 * s1 + s2 * s2 - c * s1 * s2;
    }
    return total;
}

// Frequency from rising zero crossings over `x[from..]`.
double frequency(const std::vector<float> &x, size_t from)
{
    int crossings = 0;
    double first = -1.0, last = -1.0;
    for(size_t i = from + 1; i < x.size(); ++i)
        if(x[i - 1] < 0.0f && x[i] >= 0.0f) {
            const double t = static_cast<double>(i - 1) + x[i - 1] / (x[i - 1] - x[i]);
            if(first < 0.0)
                first = t;
            last = t;
            ++crossings;
        }
    return crossings > 1 ? kRate * (crossings - 1) / (last - first) : 0.0;
}

void testLayout(const std::string &path)
{
    Instance plugin;
    CHECK(plugin.open(path));
    const auto &cats = plugin.subCategories;
    CHECK(std::find(cats.begin(), cats.end(), "Fx") != cats.end());
    CHECK(std::find(cats.begin(), cats.end(), "Instrument") != cats.end());
    CHECK(plugin.component->getBusCount(kAudio, kInput) == 2);
    CHECK(plugin.component->getBusCount(kAudio, kOutput) == 1);
    CHECK(plugin.component->getBusCount(kEvent, kInput) == 1);
    BusInfo info{};
    CHECK(plugin.component->getBusInfo(kAudio, kInput, 0, info) == kResultOk && info.busType == kMain);
    CHECK(plugin.component->getBusInfo(kAudio, kInput, 1, info) == kResultOk && info.busType == kAux);
    SpeakerArrangement mono = SpeakerArr::kMono, stereo = SpeakerArr::kStereo;
    SpeakerArrangement surround = SpeakerArr::k51;
    CHECK(plugin.processor->setBusArrangements(&mono, 1, &stereo, 1) == kResultTrue);
    CHECK(plugin.processor->setBusArrangements(&surround, 1, &stereo, 1) == kResultFalse);
}

void testSilence(const std::string &path)
{
    // No notes: the internal carrier is silent, so a modulator alone gives
    // nothing (sibilance and dry are off).
    Instance plugin;
    CHECK(plugin.open(path));
    plugin.plain();
    Stereo out = plugin.render(48000 / 2, saw(150.0));
    CHECK(finite(out));
    CHECK(peak(out.left) < 1e-4 && peak(out.right) < 1e-4);

    // A held note without a modulator is silent too (choir and synth are off)
    // once the band envelopes have released what the saw left in them.
    plugin.noteOn(48);
    out = plugin.render(48000, silence());
    std::printf("held note, no modulator: rms %.3g over 0.25-0.5 s, %.3g over 0.75-1 s\n", rms(out.left, 12000, 24000),
                rms(out.left, 36000));
    CHECK(rms(out.left, 36000) < 1e-4 && rms(out.right, 36000) < 1e-4);
}

void testVocodes(const std::string &path)
{
    Instance plugin;
    CHECK(plugin.open(path));
    plugin.plain();
    plugin.noteOn(48);
    Stereo out = plugin.render(48000, saw(150.0));
    CHECK(finite(out));
    const double level = rms(out.left, 24000);
    std::printf("vocoder output rms (saw modulator 0.5, one note): %.4f\n", level);
    CHECK(level > 0.02 && level < 1.0);

    // Modulator stops: the bands' envelopes release and the output dies.
    out = plugin.render(48000, silence());
    CHECK(rms(out.left, 36000) < 1e-4);

    // Note off: release (0.3 s) then silence even with the modulator back.
    plugin.noteOff(48);
    plugin.render(48000 * 2, silence());
    out = plugin.render(48000 / 2, saw(150.0));
    CHECK(peak(out.left) < 1e-4);
}

void testSpectralFollowing(const std::string &path)
{
    // A broadband carrier (a low note, both registers) shaped by a pure
    // modulator tone: the output's energy must sit near that tone.
    auto run = [&](double modHz, double formant) {
        Instance plugin;
        CHECK(plugin.open(path));
        plugin.plain();
        plugin.param(kFormant, formant);
        plugin.param(kVibDepth, 0.0);
        plugin.noteOn(36);
        return plugin.render(48000, sine(modHz)).left;
    };
    const std::vector<float> low = run(300.0, 0.5);
    const std::vector<float> high = run(3000.0, 0.5);
    const double lowLow = bandEnergy(low, 24000, 250.0, 350.0), lowHigh = bandEnergy(low, 24000, 2500.0, 3500.0);
    const double highLow = bandEnergy(high, 24000, 250.0, 350.0), highHigh = bandEnergy(high, 24000, 2500.0, 3500.0);
    std::printf("spectral: 300 Hz mod -> low/high %.3g / %.3g, 3 kHz mod -> low/high %.3g / %.3g\n", lowLow, lowHigh,
                highLow, highHigh);
    CHECK(lowLow > 10.0 * lowHigh);
    CHECK(highHigh > 10.0 * highLow);

    // Formant +12 st moves the carrier bands an octave up: the output of a
    // 1 kHz modulator moves from around 1 kHz to around 2 kHz.
    auto tilt = [&](const std::vector<float> &x) {
        return bandEnergy(x, 24000, 1500.0, 3000.0) / bandEnergy(x, 24000, 600.0, 1400.0);
    };
    const double straight = tilt(run(1000.0, 0.5)), shifted = tilt(run(1000.0, 1.0));
    std::printf("formant: 2k/1k energy ratio %.3g at 0 st, %.3g at +12 st\n", straight, shifted);
    CHECK(shifted > 8.0 * straight);
}

void testExternalCarrier(const std::string &path)
{
    {
        // Sidechain carrier, no MIDI at all.
        Instance plugin;
        CHECK(plugin.open(path));
        plugin.plain();
        plugin.param(kCarrier, 0.5); // External
        Stereo out = plugin.render(48000, saw(150.0), saw(110.0));
        CHECK(finite(out));
        std::printf("external carrier rms: %.4f\n", rms(out.left, 24000));
        CHECK(rms(out.left, 24000) > 0.02);
        // Notes do nothing in External mode without a sidechain signal.
        plugin.noteOn(48);
        out = plugin.render(48000 / 2, saw(150.0), silence());
        CHECK(rms(out.left, 12000) < 1e-3);
    }
    {
        // An inactive sidechain bus is ignored even if its buffer has audio.
        Instance plugin;
        CHECK(plugin.open(path, false));
        plugin.plain();
        plugin.param(kCarrier, 0.5);
        Stereo out = plugin.render(48000 / 2, saw(150.0), saw(110.0));
        CHECK(peak(out.left) < 1e-4);
    }
    {
        // Split input: left channel modulator, right channel carrier.
        Instance plugin;
        CHECK(plugin.open(path, false));
        plugin.plain();
        plugin.param(kCarrier, 0.5);
        plugin.param(kInputMode, 1.0);
        Stereo out = plugin.render(48000, saw(150.0), silence(), saw(110.0));
        CHECK(rms(out.left, 24000) > 0.02);
        // Same audio on both channels but no carrier on the right: silence.
        out = plugin.render(48000 / 2, saw(150.0), silence(), silence());
        CHECK(rms(out.left, 12000) < 1e-3);
    }
}

void testDirectPaths(const std::string &path)
{
    // Synth Level alone (vocoder off, Male 8' only): the VCO at the note's pitch.
    Instance plugin;
    CHECK(plugin.open(path));
    plugin.plain();
    plugin.param(kVocoderLevel, 0.0);
    plugin.param(kSynthLevel, kUnity);
    plugin.param(kFemale, 0.0);
    plugin.param(kVibDepth, 0.0);
    plugin.param(kCutoff, 0.6); // ~1.26 kHz: mostly the fundamental
    plugin.noteOn(69);
    Stereo out = plugin.render(48000, silence());
    const double a4 = frequency(out.left, 12000);
    std::printf("synth A4: %.2f Hz\n", a4);
    CHECK(std::fabs(a4 - 440.0) < 1.0);

    // Pitch bend fully up with a 2-semitone range.
    plugin.param(kBendRange, 2.0 / 12.0);
    plugin.param(kPitchBend, 1.0);
    out = plugin.render(48000, silence());
    const double bent = frequency(out.left, 12000);
    std::printf("synth A4 bent +2: %.2f Hz\n", bent);
    CHECK(std::fabs(bent - 493.88) < 1.5);

    // Tune -12 st.
    plugin.param(kPitchBend, 0.5);
    plugin.param(kTune, 0.0);
    out = plugin.render(48000, silence());
    CHECK(std::fabs(frequency(out.left, 12000) - 220.0) < 1.0);

    // Choir alone (the Human Voice section) sounds without a modulator.
    Instance choir;
    CHECK(choir.open(path));
    choir.plain();
    choir.param(kVocoderLevel, 0.0);
    choir.param(kChoirLevel, kUnity);
    choir.noteOn(57);
    choir.noteOn(60);
    choir.noteOn(64);
    out = choir.render(48000, silence());
    std::printf("choir chord rms: %.4f\n", rms(out.left, 24000));
    CHECK(finite(out));
    CHECK(rms(out.left, 24000) > 0.01 && peak(out.left) < 2.0);

    // Dry level passes the modulator.
    Instance dry;
    CHECK(dry.open(path));
    dry.plain();
    dry.param(kDryLevel, kUnity);
    out = dry.render(48000 / 2, sine(440.0));
    CHECK(std::fabs(rms(out.left, 12000) - 0.5 / std::sqrt(2.0)) < 0.02);
}

void testSampleAccurateNote(const std::string &path)
{
    Instance plugin;
    CHECK(plugin.open(path));
    plugin.plain();
    plugin.param(kVocoderLevel, 0.0);
    plugin.param(kSynthLevel, kUnity);
    plugin.noteOn(60, 1.0f, 100);
    Stereo out = plugin.render(kBlock, silence());
    double before = 0.0;
    for(int i = 0; i < 100; ++i)
        before = std::max(before, static_cast<double>(std::fabs(out.left[i])));
    CHECK(before < 1e-6);
    CHECK(peak(out.left) > 1e-3);
}

void testStereoAndEnsemble(const std::string &path)
{
    // Width spreads odd/even bands; the ensemble makes the sides differ.
    Instance plugin;
    CHECK(plugin.open(path));
    plugin.param(kSibilance, 0.0);
    plugin.param(kNoise, 0.0);
    plugin.param(kWidth, 1.0);
    plugin.noteOn(48);
    plugin.noteOn(55);
    Stereo out = plugin.render(48000, saw(150.0));
    CHECK(finite(out));
    double diff = 0.0;
    for(size_t i = 24000; i < out.left.size(); ++i)
        diff += std::fabs(out.left[i] - out.right[i]);
    CHECK(diff / 24000.0 > 1e-3);

    // 16 and 20 bands render too.
    plugin.param(kBands, 0.5);
    out = plugin.render(48000 / 2, saw(150.0));
    CHECK(finite(out) && rms(out.left, 12000) > 0.01);
    plugin.param(kBands, 1.0);
    out = plugin.render(48000 / 2, saw(150.0));
    CHECK(finite(out) && rms(out.left, 12000) > 0.01);
}

void testStress(const std::string &path)
{
    // Hard settings stay finite: 20 more notes than voices, full resonance
    // and noise, every level at maximum, loud modulator and carrier.
    Instance plugin;
    CHECK(plugin.open(path));
    for(ParamID id : {kSibilance, kNoise, kVocoderLevel, kChoirLevel, kSynthLevel, kDryLevel, kOutput})
        plugin.param(id, 1.0);
    plugin.param(206, 1.0); // Resonance
    plugin.param(kCarrier, 1.0);
    for(int note = 30; note < 66; ++note)
        plugin.noteOn(note);
    Stereo out = plugin.render(48000, saw(97.0, 1.0), saw(61.0, 1.0));
    CHECK(finite(out));
}

void testState(const std::string &path)
{
    Instance a;
    CHECK(a.open(path));
    a.param(kFormant, 0.75);
    a.param(kBands, 1.0);
    a.param(kCutoff, 0.3);
    a.render(kBlock, silence());
    MemoryStream stream;
    CHECK(a.component->getState(&stream) == kResultOk);

    Instance b;
    CHECK(b.open(path));
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    CHECK(b.component->setState(&stream) == kResultOk);
    MemoryStream again;
    CHECK(b.component->getState(&again) == kResultOk);
    int64 sizeA = 0, sizeB = 0;
    stream.seek(0, IBStream::kIBSeekEnd, &sizeA);
    again.seek(0, IBStream::kIBSeekEnd, &sizeB);
    CHECK(sizeA == sizeB && sizeA > 0);
    CHECK(std::memcmp(stream.getData(), again.getData(), static_cast<size_t>(sizeA)) == 0);

    // Garbage is rejected.
    MemoryStream junk;
    const char text[] = "not a vocoder state";
    int32 written = 0;
    junk.write(const_cast<char *>(text), sizeof(text), &written);
    junk.seek(0, IBStream::kIBSeekSet, nullptr);
    CHECK(b.component->setState(&junk) == kResultFalse);
}

} // namespace

int main(int argc, char **argv)
{
    if(argc < 2) {
        std::fprintf(stderr, "usage: %s <MlaVocoder.vst3>\n", argv[0]);
        return 2;
    }
    const std::string path = argv[1];
    testLayout(path);
    testSilence(path);
    testVocodes(path);
    testSpectralFollowing(path);
    testExternalCarrier(path);
    testDirectPaths(path);
    testSampleAccurateNote(path);
    testStereoAndEnsemble(path);
    testStress(path);
    testState(path);
    if(failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all Mla Vocoder tests passed\n");
    return 0;
}
