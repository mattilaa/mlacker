// Offline functional tests for the Mla Speech bundle.
//
// Loads the built .vst3 through the SDK hosting classes, sends notes with and
// without note-expression text and checks rendered audio: bus layout,
// silence without notes, the default phrase, words that come with a note,
// phrases that outlast their note (and Gate = Note length cutting them),
// longer text speaking longer, words that arrive after their note, pitch
// following the key, phoneme input, the DAC modes and state round trips.
//
// Usage: mla_speech_tests <path/to/MlaSpeech.vst3>

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstnoteexpression.h"

#include <algorithm>
#include <cmath>
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
constexpr ParamID kSpeed = 100, kVoice = 103, kRateParam = 104, kDac = 105, kSmooth = 106, kGate = 107;
constexpr ParamID kIntonation = 102, kOutput = 110;

class Application final : public HostApplication {
  public:
    tresult PLUGIN_API getName(String128 name) override
    {
        const char16_t text[] = u"mla_speech_tests";
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
    // Text events point into these until the block is processed.
    std::vector<std::u16string> texts;
    int64_t clock = 0;

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

    void noteOn(int pitch, int32 noteId = -1, int32 offset = 0, float velocity = 1.0f)
    {
        Event e{};
        e.type = Event::kNoteOnEvent;
        e.sampleOffset = offset;
        e.noteOn.pitch = static_cast<int16>(pitch);
        e.noteOn.velocity = velocity;
        e.noteOn.noteId = noteId;
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

    // Words for note `noteId`, as mlacker's host sends a comment cell.
    void words(const char *utf8, int32 noteId, int32 offset = 0, NoteExpressionTypeID type = kTextTypeID)
    {
        texts.reserve(64);
        texts.emplace_back(utf8, utf8 + std::strlen(utf8));
        Event e{};
        e.type = Event::kNoteExpressionTextEvent;
        e.sampleOffset = offset;
        e.noteExpressionText.typeId = type;
        e.noteExpressionText.noteId = noteId;
        e.noteExpressionText.textLen = static_cast<uint32>(texts.back().size());
        e.noteExpressionText.text = reinterpret_cast<const TChar *>(texts.back().c_str());
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
            texts.clear();
            clock += kBlock;
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

// Seconds from the start until the output stays below -60 dBFS (in 10 ms
// windows), or the whole length if it never does.
double speechLength(const std::vector<float> &x)
{
    const size_t window = static_cast<size_t>(kRate / 100.0);
    size_t last = 0;
    for(size_t from = 0; from + window <= x.size(); from += window)
        if(rms(x, from, from + window) > 1e-3)
            last = from + window;
    return static_cast<double>(last) / kRate;
}

// Fundamental by autocorrelation over x[from, to), searched in 60-600 Hz.
double pitch(const std::vector<float> &x, size_t from, size_t to)
{
    const int lowLag = static_cast<int>(kRate / 600.0), highLag = static_cast<int>(kRate / 60.0);
    // The shortest lag scoring within 10 % of the best: a waveform that
    // repeats exactly scores as high at whole multiples of its period.
    std::vector<double> scores(highLag + 1, 0.0);
    double best = 0.0;
    for(int lag = lowLag; lag <= highLag; ++lag) {
        double sum = 0.0, energy = 0.0;
        for(size_t i = from; i + lag < to; ++i) {
            sum += static_cast<double>(x[i]) * x[i + lag];
            energy += static_cast<double>(x[i + lag]) * x[i + lag];
        }
        scores[lag] = energy > 0.0 ? sum / std::sqrt(energy) : 0.0;
        best = std::max(best, scores[lag]);
    }
    for(int lag = lowLag; lag <= highLag; ++lag)
        if(best > 0.0 && scores[lag] >= 0.9 * best && (lag == highLag || scores[lag] >= scores[lag + 1]))
            return kRate / lag;
    return 0.0;
}

void testLayout(const std::string &path)
{
    Instance plugin;
    CHECK(plugin.open(path));
    const auto &cats = plugin.subCategories;
    CHECK(std::find(cats.begin(), cats.end(), "Instrument") != cats.end());
    CHECK(plugin.component->getBusCount(kAudio, kInput) == 0);
    CHECK(plugin.component->getBusCount(kAudio, kOutput) == 1);
    CHECK(plugin.component->getBusCount(kEvent, kInput) == 1);
    SpeakerArrangement mono = SpeakerArr::kMono, stereo = SpeakerArr::kStereo;
    CHECK(plugin.processor->setBusArrangements(nullptr, 0, &stereo, 1) == kResultTrue);
    CHECK(plugin.processor->setBusArrangements(nullptr, 0, &mono, 1) == kResultFalse);
    CHECK(plugin.processor->setBusArrangements(&stereo, 1, &stereo, 1) == kResultFalse);
}

void testSilence(const std::string &path)
{
    Instance plugin;
    CHECK(plugin.open(path));
    Stereo out = plugin.render(48000 / 2);
    CHECK(finite(out));
    CHECK(peak(out.left) < 1e-5 && peak(out.right) < 1e-5);
}

void testDefaultPhrase(const std::string &path)
{
    // A note without words says the default phrase, to its end, although
    // the note is released at once.
    Instance plugin;
    CHECK(plugin.open(path));
    plugin.noteOn(48);
    plugin.noteOff(48, 64);
    Stereo out = plugin.render(48000 * 4);
    const double length = speechLength(out.left);
    std::printf("default phrase: %.2f s, peak %.3f, rms %.3f\n", length, peak(out.left), rms(out.left, 0, 48000));
    CHECK(finite(out));
    CHECK(peak(out.left) > 0.05 && peak(out.left) < 1.0);
    CHECK(length > 1.0 && length < 3.5);
    CHECK(rms(out.left, 48000 * 7 / 2) < 1e-4);
    // Mono voice, both channels.
    double difference = 0.0;
    for(size_t i = 0; i < out.left.size(); ++i)
        difference = std::max(difference, static_cast<double>(std::fabs(out.left[i] - out.right[i])));
    CHECK(difference < 1e-6);
}

void testWordsWithNote(const std::string &path)
{
    // The host sends the note, then its words at the same offset.
    Instance shortPlugin, longPlugin;
    CHECK(shortPlugin.open(path) && longPlugin.open(path));
    shortPlugin.noteOn(48, 7, 100);
    shortPlugin.words("Hi.", 7, 100);
    longPlugin.noteOn(48, 7, 100);
    longPlugin.words("Hello, this is a much longer sentence for the Atari speech synthesizer.", 7, 100);
    const Stereo a = shortPlugin.render(48000 * 8), b = longPlugin.render(48000 * 8);
    const double shortLength = speechLength(a.left), longLength = speechLength(b.left);
    std::printf("\"Hi.\": %.2f s, long sentence: %.2f s\n", shortLength, longLength);
    CHECK(shortLength > 0.15 && shortLength < 0.8);
    CHECK(longLength > 3.0 && longLength < 7.5);
    // Sample-accurate start: silent before offset 100.
    CHECK(peak(std::vector<float>(a.left.begin(), a.left.begin() + 100)) < 1e-6);
    CHECK(rms(a.left, 100, 4900) > 1e-3 || rms(a.left, 4900, 9700) > 1e-3);
}

void testLaterWordsAndRepeat(const std::string &path)
{
    // Words that arrive in a later block for the speaking note restart it
    // with them; the next note without words repeats them.
    Instance plugin, reference;
    CHECK(plugin.open(path) && reference.open(path));
    plugin.noteOn(50, 3);
    plugin.render(kBlock * 4);
    plugin.words("Go.", 3);
    const Stereo restarted = plugin.render(48000 * 2);
    reference.noteOn(50, 9);
    reference.words("Go.", 9);
    const Stereo expected = reference.render(48000 * 2);
    const double a = speechLength(restarted.left), b = speechLength(expected.left);
    std::printf("restarted with later words: %.2f s (reference %.2f s)\n", a, b);
    CHECK(std::fabs(a - b) < 0.05);
    plugin.noteOn(50);
    const Stereo again = plugin.render(48000 * 2);
    CHECK(std::fabs(speechLength(again.left) - b) < 0.05);
}

void testGate(const std::string &path)
{
    Instance plugin;
    CHECK(plugin.open(path));
    plugin.param(kGate, 1.0);
    plugin.noteOn(48, 1);
    plugin.words("This sentence is cut short by the note off.", 1);
    plugin.noteOff(48, 0);
    plugin.render(kBlock);
    Stereo out = plugin.render(48000);
    // Released in the first block: a 20 ms fade, then silence.
    CHECK(rms(out.left, 4800) < 1e-4);
}

void testPitch(const std::string &path)
{
    // A long phonetic "AA" with intonation off, a clean DAC and full
    // smoothing: the fundamental follows the key.
    double found[2] = {};
    const int keys[2] = {48, 60};
    for(int k = 0; k < 2; ++k) {
        Instance plugin;
        CHECK(plugin.open(path));
        plugin.param(kIntonation, 0.0);
        plugin.param(kDac, 0.0);
        plugin.param(kSpeed, 0.0);
        plugin.noteOn(keys[k], 5);
        plugin.words("[AA AA AA AA]", 5);
        const Stereo out = plugin.render(48000);
        found[k] = pitch(out.left, 4800, 14400);
    }
    std::printf("pitch: key 48 %.1f Hz, key 60 %.1f Hz\n", found[0], found[1]);
    CHECK(std::fabs(found[0] - 130.81) < 6.0);
    CHECK(std::fabs(found[1] - 261.63) < 12.0);
}

void testPhonemesAndModes(const std::string &path)
{
    // kPhonemeTypeID takes phoneme codes; every voice and DAC mode speaks,
    // stays finite and bounded.
    for(int voice = 0; voice < 3; ++voice) {
        for(int dac = 0; dac < 3; ++dac) {
            Instance plugin;
            CHECK(plugin.open(path));
            plugin.param(kVoice, voice / 2.0);
            plugin.param(kDac, dac / 2.0);
            plugin.param(kRateParam, voice == 1 ? 1.0 : 0.0);
            plugin.param(kSmooth, dac == 2 ? 0.0 : 1.0);
            plugin.noteOn(55, 2);
            plugin.words("hEHlOW wER1ld", 2, 0, kPhonemeTypeID);
            const Stereo out = plugin.render(48000 * 2);
            const double length = speechLength(out.left);
            CHECK(finite(out));
            CHECK(peak(out.left) < 1.0);
            CHECK(length > 0.4 && length < 1.6);
            if(length <= 0.4 || length >= 1.6 || peak(out.left) >= 1.0)
                std::printf("voice %d dac %d: %.2f s, peak %.3f\n", voice, dac, length, peak(out.left));
        }
    }
    // The YM DAC changes the sound.
    Instance clean, ym;
    CHECK(clean.open(path) && ym.open(path));
    clean.param(kDac, 0.0);
    ym.param(kDac, 1.0);
    clean.noteOn(48, 1);
    clean.words("Atari", 1);
    ym.noteOn(48, 1);
    ym.words("Atari", 1);
    const Stereo a = clean.render(24000), b = ym.render(24000);
    double difference = 0.0;
    for(size_t i = 0; i < a.left.size(); ++i)
        difference += std::fabs(a.left[i] - b.left[i]);
    CHECK(difference > 1.0);
}

void testNumbersAndRestart(const std::string &path)
{
    // Numbers are read out; a new note restarts the voice.
    Instance plugin;
    CHECK(plugin.open(path));
    plugin.noteOn(48, 1);
    plugin.words("1987", 1);
    const Stereo out = plugin.render(48000 * 3);
    const double length = speechLength(out.left);
    std::printf("\"1987\": %.2f s\n", length);
    CHECK(length > 1.0 && length < 2.8);
    plugin.noteOn(48, 2);
    plugin.words("Ok", 2);
    plugin.render(kBlock);
    plugin.noteOn(48, 3);
    plugin.words("Ok", 3);
    const Stereo second = plugin.render(48000 * 2);
    CHECK(speechLength(second.left) < 1.0);
}

void testState(const std::string &path)
{
    Instance a, b;
    CHECK(a.open(path) && b.open(path));
    a.param(kSpeed, 0.8);
    a.param(kVoice, 1.0);
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
        std::fprintf(stderr, "usage: %s <MlaSpeech.vst3>\n", argv[0]);
        return 2;
    }
    const std::string path = argv[1];
    testLayout(path);
    testSilence(path);
    testDefaultPhrase(path);
    testWordsWithNote(path);
    testLaterWordsAndRepeat(path);
    testGate(path);
    testPitch(path);
    testPhonemesAndModes(path);
    testNumbersAndRestart(path);
    testState(path);
    if(failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all Mla Speech tests passed\n");
    return 0;
}
