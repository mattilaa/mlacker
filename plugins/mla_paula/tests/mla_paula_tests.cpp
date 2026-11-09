// Offline functional tests for the Mla Paula bundle.
//
// Loads the built .vst3 through the SDK hosting classes (as mlacker does),
// fills slots through the mla_sampler_protocol messages and checks rendered
// audio: fixed channels and their side of the stereo field, period
// quantization and the 124 period floor, hold vs linear interpolation, the
// vertical-blank attack, legato, and state round trips.
//
// Usage: mla_paula_tests <path/to/MlaPaula.vst3> <scratch dir>

#include "mla_sampler_protocol.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstmessage.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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
constexpr ParamID kChannel = 103, kInterpolation = 104, kModel = 105, kSeparation = 108, kLegato = 109;
constexpr ParamID kAttack = 110;
constexpr ParamID slotParam(int slot, int field) { return 200 + slot * 8 + field; }
constexpr int kLoop = 3;

class Application final : public HostApplication {
  public:
    tresult PLUGIN_API getName(String128 name) override
    {
        const char16_t text[] = u"mla_paula_tests";
        std::copy(std::begin(text), std::end(text), name);
        return kResultOk;
    }
};
Application application;

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

    void note(bool on, int pitch, float velocity = 1.0f, int32 offset = 0)
    {
        Event e{};
        e.type = on ? Event::kNoteOnEvent : Event::kNoteOffEvent;
        e.sampleOffset = offset;
        if(on) {
            e.noteOn.pitch = static_cast<int16>(pitch);
            e.noteOn.velocity = velocity;
            e.noteOn.noteId = -1;
        } else {
            e.noteOff.pitch = static_cast<int16>(pitch);
            e.noteOff.noteId = -1;
        }
        events.addEvent(e);
    }

    std::vector<float> render(int frames)
    {
        std::vector<float> out;
        for(int done = 0; done < frames; done += kBlock) {
            data.numSamples = kBlock;
            processor->process(data);
            events.clear();
            changes.clearQueue();
            const auto &bus = data.outputs[0];
            for(int32 i = 0; i < kBlock; ++i) {
                out.push_back(bus.channelBuffers32[0][i]);
                out.push_back(bus.channelBuffers32[1][i]);
            }
        }
        return out;
    }

    // A register-mode packet (7D 'MPAU' 01 ...), as mlacker sends a SYSEX cell.
    void sysex(const std::vector<uint8> &bytes, int32 offset = 0)
    {
        packets.push_back(bytes);
        Event e{};
        e.type = Event::kDataEvent;
        e.sampleOffset = offset;
        e.data.type = DataEvent::kMidiSysEx;
        e.data.size = static_cast<uint32>(packets.back().size());
        e.data.bytes = packets.back().data();
        events.addEvent(e);
    }
    std::vector<std::vector<uint8>> packets;

    tresult loadPcm(int slot, const std::vector<float> &pcm, double rate)
    {
        auto msg = owned(new HostMessage);
        msg->setMessageID(mla_sampler::kLoadPcmMessage);
        auto *a = msg->getAttributes();
        a->setInt("pad", slot);
        a->setInt("channels", 1);
        a->setInt("frames", static_cast<int64>(pcm.size()));
        a->setFloat("rate", rate);
        a->setBinary("data", pcm.data(), static_cast<uint32>(pcm.size() * sizeof(float)));
        auto connection = U::cast<IConnectionPoint>(component);
        return connection ? connection->notify(msg) : kNoInterface;
    }
};

// A looped square wave: `cycles` cycles of `period` words.
std::vector<float> square(int period, int cycles)
{
    std::vector<float> pcm;
    for(int c = 0; c < cycles; ++c)
        for(int i = 0; i < period; ++i)
            pcm.push_back(i < period / 2 ? 0.8f : -0.8f);
    return pcm;
}

// A looped sine, smooth enough that interpolation changes it audibly.
std::vector<float> sine(int period, int cycles)
{
    std::vector<float> pcm;
    for(int i = 0; i < period * cycles; ++i)
        pcm.push_back(0.8f * static_cast<float>(std::sin(2.0 * M_PI * i / period)));
    return pcm;
}

float peak(const std::vector<float> &stereo, int from, int to, int channel)
{
    float result = 0.0f;
    for(int f = from; f < to; ++f)
        result = std::max(result, std::fabs(stereo[f * 2 + channel]));
    return result;
}

// Frequency from upward zero crossings of the left channel.
double frequency(const std::vector<float> &stereo, int from, int to)
{
    int crossings = 0, first = -1, last = -1;
    for(int f = from + 1; f < to; ++f)
        if(stereo[(f - 1) * 2] < 0.0f && stereo[f * 2] >= 0.0f) {
            if(first < 0)
                first = f;
            last = f;
            ++crossings;
        }
    return crossings > 1 ? (crossings - 1) * kRate / (last - first) : 0.0;
}

void setupLoop(Instance &plugin, int channel)
{
    plugin.param(kChannel, channel / 4.0);
    plugin.param(slotParam(0, kLoop), 1.0);
}

void testLayoutAndSilence(const std::string &bundle)
{
    Instance plugin;
    CHECK(plugin.open(bundle));
    CHECK(std::find(plugin.subCategories.begin(), plugin.subCategories.end(), "Instrument") !=
          plugin.subCategories.end());
    CHECK(plugin.component->getBusCount(kAudio, kInput) == 0);
    plugin.note(true, 60);
    const auto out = plugin.render(4096);
    CHECK(peak(out, 0, 4096, 0) == 0.0f && peak(out, 0, 4096, 1) == 0.0f);
}

void testChannelSides(const std::string &bundle)
{
    for(int channel = 1; channel <= 4; ++channel) {
        Instance plugin;
        CHECK(plugin.open(bundle));
        CHECK(plugin.loadPcm(0, square(32, 64), 8287.0) == kResultOk);
        setupLoop(plugin, channel);
        plugin.note(true, 60);
        const auto out = plugin.render(9600);
        const bool left = channel == 1 || channel == 4;
        CHECK(peak(out, 2400, 9600, left ? 0 : 1) > 0.1f);
        CHECK(peak(out, 2400, 9600, left ? 1 : 0) < 1e-4f);
    }
}

void testPeriods(const std::string &bundle)
{
    // 8287 Hz at the root is period 428: 3546895 / 428 / 32 words = 258.97 Hz.
    // An octave up is period 214; far above, the period stops at 124.
    const int notes[] = {60, 72, 96};
    const int periods[] = {428, 214, 124};
    for(int n = 0; n < 3; ++n) {
        Instance plugin;
        CHECK(plugin.open(bundle));
        CHECK(plugin.loadPcm(0, square(32, 64), 8287.0) == kResultOk);
        setupLoop(plugin, 1);
        plugin.param(kModel, 1.0); // A1200: keep the square's edges for counting.
        plugin.note(true, notes[n]);
        const auto out = plugin.render(48000);
        const double expected = 3546895.0 / periods[n] / 32.0;
        const double measured = frequency(out, 4800, 48000);
        if(std::fabs(measured - expected) > expected * 0.005)
            std::fprintf(stderr, "note %d: %.2f Hz, expected %.2f\n", notes[n], measured, expected);
        CHECK(std::fabs(measured - expected) <= expected * 0.005);
    }
}

std::vector<float> renderSine(const std::string &bundle, double interpolation)
{
    Instance plugin;
    if(!plugin.open(bundle) || plugin.loadPcm(0, sine(16, 128), 8287.0) != kResultOk)
        return {};
    setupLoop(plugin, 1);
    plugin.param(kModel, 1.0);
    plugin.param(kInterpolation, interpolation);
    plugin.note(true, 72);
    return plugin.render(9600);
}

void testInterpolation(const std::string &bundle)
{
    const auto hold = renderSine(bundle, 0.0);
    const auto linear = renderSine(bundle, 1.0);
    CHECK(hold.size() == linear.size() && !hold.empty());
    double difference = 0.0, energy = 0.0;
    for(size_t i = 4800; i < hold.size(); i += 2) {
        difference += std::fabs(hold[i] - linear[i]);
        energy += std::fabs(linear[i]);
    }
    // Same tone, but the held steps differ from the interpolated line.
    CHECK(energy > 0.0 && difference > energy * 0.02);
}

void testAttackTicks(const std::string &bundle)
{
    Instance plugin;
    CHECK(plugin.open(bundle));
    CHECK(plugin.loadPcm(0, square(32, 64), 8287.0) == kResultOk);
    setupLoop(plugin, 1);
    plugin.param(kAttack, std::cbrt(0.25)); // 0.5 s: 25 ticks
    plugin.note(true, 60);
    const auto out = plugin.render(48000);
    CHECK(peak(out, 0, 2400, 0) < peak(out, 36000, 48000, 0) * 0.3f);
}

void testLegato(const std::string &bundle)
{
    // Overlapping notes on one fixed channel: the second (gliding with
    // legato, restarting without) keeps sounding after the first's note-off.
    for(int legato = 0; legato <= 1; ++legato) {
        Instance plugin;
        CHECK(plugin.open(bundle));
        CHECK(plugin.loadPcm(0, square(32, 64), 8287.0) == kResultOk);
        setupLoop(plugin, 1);
        plugin.param(kLegato, legato);
        plugin.note(true, 60);
        auto out = plugin.render(4096);
        plugin.note(true, 67);
        plugin.note(false, 60);
        out = plugin.render(9600);
        CHECK(peak(out, 4800, 9600, 0) > 0.1f);
    }
}

// Register writes: 01 LC, 02 LEN, 03 PER (three 7-bit bytes), 04 VOL, 05 DMA on.
std::vector<uint8> packet(int channel, const std::vector<std::vector<uint8>> &frames, int flags = 0)
{
    std::vector<uint8> p = {0x7D, 'M', 'P', 'A', 'U', 1, static_cast<uint8>(channel),
                            static_cast<uint8>(frames.size()), 20000 & 127, (20000 >> 7) & 127, 20000 >> 14,
                            static_cast<uint8>(flags)};
    for(const auto &f : frames) {
        p.push_back(static_cast<uint8>(f.size()));
        p.insert(p.end(), f.begin(), f.end());
    }
    return p;
}

std::vector<uint8> write3(int op, int value)
{
    return {static_cast<uint8>(op), static_cast<uint8>(value & 127), static_cast<uint8>((value >> 7) & 127),
            static_cast<uint8>(value >> 14)};
}

void testRegisterMode(const std::string &bundle)
{
    Instance plugin;
    CHECK(plugin.open(bundle));
    // Chip memory: 64 bytes of silence, then a 32-byte square cycle (16 words).
    std::vector<float> chip(64, 0.0f);
    auto cycle = square(32, 1);
    chip.insert(chip.end(), cycle.begin(), cycle.end());
    CHECK(plugin.loadPcm(0, chip, 8287.0) == kResultOk);
    plugin.param(kModel, 1.0);
    std::vector<uint8> start;
    for(auto part : {write3(1, 64), write3(2, 16), write3(3, 428)})
        start.insert(start.end(), part.begin(), part.end());
    start.insert(start.end(), {4, 64, 5});
    // Frame 2 (20 ms later): an octave up.
    plugin.sysex(packet(1, {start, write3(3, 214)}, 1));
    auto out = plugin.render(48000);
    // Channel 2 is right; the second frame's period holds from 20 ms on.
    CHECK(peak(out, 4800, 14400, 1) > 0.1f);
    CHECK(peak(out, 4800, 14400, 0) < 1e-4f);
    // The packets stopped: silent after the 0.3 s timeout.
    CHECK(peak(out, 24000, 48000, 1) < 1e-3f);
    plugin.sysex(packet(1, {start}, 1));
    out = plugin.render(9600);
    std::vector<float> mono;
    for(size_t f = 0; f < out.size() / 2; ++f) {
        mono.push_back(out[f * 2 + 1]);
        mono.push_back(0.0f);
    }
    const double expected = 3546895.0 / 428 / 32;
    const double measured = frequency(mono, 960, 9600);
    if(std::fabs(measured - expected) > expected * 0.01)
        std::fprintf(stderr, "register mode: %.2f Hz, expected %.2f\n", measured, expected);
    CHECK(std::fabs(measured - expected) <= expected * 0.01);
    // A note leaves register mode: the note-mode slot plays instead.
    plugin.param(slotParam(0, kLoop), 1.0);
    plugin.param(kChannel, 0.25);
    plugin.note(true, 60);
    out = plugin.render(9600);
    CHECK(peak(out, 4800, 9600, 0) > 0.1f);
}

void testStateRoundTrip(const std::string &bundle)
{
    MemoryStream stream;
    std::vector<float> reference;
    {
        Instance plugin;
        CHECK(plugin.open(bundle));
        CHECK(plugin.loadPcm(0, sine(16, 128), 8287.0) == kResultOk);
        setupLoop(plugin, 2);
        plugin.param(kSeparation, 0.5);
        plugin.render(kBlock);
        plugin.note(true, 64);
        reference = plugin.render(4096);
        CHECK(plugin.component->getState(&stream) == kResultOk);
    }
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    Instance restored;
    CHECK(restored.open(bundle));
    CHECK(restored.component->setState(&stream) == kResultOk);
    restored.render(kBlock);
    restored.note(true, 64);
    const auto again = restored.render(4096);
    CHECK(again.size() == reference.size());
    float worst = 0.0f;
    for(size_t i = 0; i < again.size() && i < reference.size(); ++i)
        worst = std::max(worst, std::fabs(again[i] - reference[i]));
    CHECK(worst < 1e-6f);
}

} // namespace

int main(int argc, char **argv)
{
    if(argc < 2) {
        std::fprintf(stderr, "usage: %s <MlaPaula.vst3> [scratch dir]\n", argv[0]);
        return 2;
    }
    const std::string bundle = argv[1];
    testLayoutAndSilence(bundle);
    testChannelSides(bundle);
    testPeriods(bundle);
    testInterpolation(bundle);
    testAttackTicks(bundle);
    testLegato(bundle);
    testStateRoundTrip(bundle);
    testRegisterMode(bundle);
    if(failures == 0)
        std::printf("mla_paula_tests: all passed\n");
    return failures == 0 ? 0 : 1;
}
