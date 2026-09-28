// Offline functional tests for the Mla Sampler bundle.
//
// Loads the built .vst3 through the SDK hosting classes (as mlacker does),
// fills slots through the mla_sampler_protocol messages and checks rendered
// audio: the bus layout, slot/key mapping, key zones with pitch tracking and
// layering, loop off / forward / bidirectional, per-slot output routing with
// the fallback to Main, and state round trips.
//
// Usage: mla_sampler_tests <path/to/MlaSampler.vst3> <scratch dir>

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
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

int failures = 0;
// A test stops at a failed open; there is nothing to render.
#define OPEN(instance, ...)                                                                    \
    do {                                                                                       \
        if(!(instance).open(__VA_ARGS__)) {                                                    \
            std::fprintf(stderr, "FAIL %s:%d: cannot open the bundle\n", __FILE__, __LINE__); \
            ++failures;                                                                        \
            return;                                                                            \
        }                                                                                      \
    } while(0)
#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if(!(condition)) {                                                                     \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);          \
            ++failures;                                                                        \
        }                                                                                      \
    } while(0)

constexpr double kRate = 48000.0;
constexpr int32 kBlock = 256;
constexpr int kRootKey = 36;
constexpr int kOutputs = 8;

// Parameter IDs (see plugin.cpp).
constexpr ParamID kRelease = 107;
enum SlotParam { kSlotLevel, kSlotPan, kSlotTune, kSlotOutput, kSlotLoop, kSlotLoopStart, kSlotLoopEnd };
constexpr ParamID slotParam(int slot, SlotParam k) { return 200 + slot * 7 + k; }
constexpr double kLoopForward = 0.5, kLoopBidirectional = 1.0;
enum ZoneParam { kZoneMode, kZoneLow, kZoneHigh, kZoneRoot, kZoneTrack };
constexpr ParamID zoneParam(int slot, ZoneParam k) { return 400 + slot * 5 + k; }
constexpr double key(int midi) { return midi / 127.0; }

class Application final : public HostApplication {
  public:
    tresult PLUGIN_API getName(String128 name) override
    {
        const char16_t text[] = u"mla_sampler_tests";
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

    // `auxActive`: also activate bus 1 ("Out 2").
    bool open(const std::string &path, bool auxActive = false)
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
        std::vector<SpeakerArrangement> stereo(kOutputs, SpeakerArr::kStereo);
        if(processor->setBusArrangements(nullptr, 0, stereo.data(), kOutputs) != kResultOk)
            return false;
        component->activateBus(kAudio, kOutput, 0, true);
        if(auxActive)
            component->activateBus(kAudio, kOutput, 1, true);
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

    // Render `frames` (multiple of the block), appending bus `bus`'s
    // interleaved stereo.
    std::vector<float> render(int frames, int bus = 0)
    {
        std::vector<float> out;
        for(int done = 0; done < frames; done += kBlock) {
            data.numSamples = kBlock;
            processor->process(data);
            events.clear();
            changes.clearQueue();
            const auto &buffers = data.outputs[bus];
            for(int32 i = 0; i < kBlock; ++i) {
                out.push_back(buffers.channelBuffers32[0][i]);
                out.push_back(buffers.channelBuffers32[1][i]);
            }
        }
        return out;
    }

    tresult send(IPtr<IMessage> message)
    {
        auto connection = U::cast<IConnectionPoint>(component);
        return connection ? connection->notify(message) : kNoInterface;
    }
};

IPtr<IMessage> message(const char *id)
{
    auto msg = owned(new HostMessage);
    msg->setMessageID(id);
    return msg;
}

tresult loadPcm(Instance &plugin, int slot, const std::vector<float> &mono)
{
    auto msg = message(mla_sampler::kLoadPcmMessage);
    auto *a = msg->getAttributes();
    a->setInt("pad", slot);
    a->setInt("channels", 1);
    a->setInt("frames", static_cast<int64>(mono.size()));
    a->setFloat("rate", kRate);
    a->setBinary("data", mono.data(), static_cast<uint32>(mono.size() * sizeof(float)));
    return plugin.send(msg);
}

// Rising ramp 1/n .. 1 over `frames` frames: a frame's value names its index.
std::vector<float> ramp(int frames)
{
    std::vector<float> pcm(frames);
    for(int i = 0; i < frames; ++i)
        pcm[i] = static_cast<float>(i + 1) / static_cast<float>(frames);
    return pcm;
}

float left(const std::vector<float> &stereo, int frame) { return stereo[static_cast<size_t>(frame) * 2]; }

double energy(const std::vector<float> &stereo, int from, int to)
{
    double sum = 0.0;
    for(int f = from; f < to; ++f)
        sum += std::fabs(left(stereo, f));
    return sum;
}

void testLayout(const std::string &path)
{
    Instance plugin;
    OPEN(plugin, path);
    CHECK(std::find(plugin.subCategories.begin(), plugin.subCategories.end(), "Instrument") !=
          plugin.subCategories.end());
    CHECK(std::find(plugin.subCategories.begin(), plugin.subCategories.end(), "Sampler") !=
          plugin.subCategories.end());
    CHECK(plugin.component->getBusCount(kAudio, kOutput) == kOutputs);
    CHECK(plugin.component->getBusCount(kAudio, kInput) == 0);
    for(int32 bus = 0; bus < kOutputs; ++bus) {
        BusInfo info{};
        CHECK(plugin.component->getBusInfo(kAudio, kOutput, bus, info) == kResultOk);
        CHECK(info.channelCount == 2);
        CHECK(info.busType == (bus == 0 ? kMain : kAux));
        CHECK(((info.flags & BusInfo::kDefaultActive) != 0) == (bus == 0));
    }
    // mla_sampler.info: 16 slots from the root key, occupancy bitmask.
    CHECK(loadPcm(plugin, 3, ramp(64)) == kResultOk);
    auto info = message(mla_sampler::kInfoMessage);
    CHECK(plugin.send(info) == kResultOk);
    int64 root = -1, pads = -1, occupied = -1;
    info->getAttributes()->getInt("root", root);
    info->getAttributes()->getInt("pads", pads);
    info->getAttributes()->getInt("occupied", occupied);
    CHECK(root == kRootKey);
    CHECK(pads == 16);
    CHECK(occupied == (int64(1) << 3));
}

void testLoopOff(const std::string &path)
{
    Instance plugin;
    OPEN(plugin, path);
    CHECK(loadPcm(plugin, 0, std::vector<float>(1000, 0.5f)) == kResultOk);
    plugin.noteOn(kRootKey);
    const auto out = plugin.render(4096);
    CHECK(std::fabs(left(out, 10) - 0.5f) < 1e-3f);
    CHECK(std::fabs(left(out, 990) - 0.5f) < 1e-3f);
    CHECK(energy(out, 1001, 4096) == 0.0); // The sample ends; the key is still held.
}

void testForwardLoop(const std::string &path)
{
    Instance plugin;
    OPEN(plugin, path);
    const int frames = 1000;
    CHECK(loadPcm(plugin, 0, ramp(frames)) == kResultOk);
    plugin.param(slotParam(0, kSlotLoop), kLoopForward);
    plugin.param(slotParam(0, kSlotLoopStart), 0.5); // frames 500..999
    plugin.noteOn(kRootKey);
    const auto out = plugin.render(8192);
    // Frame f of the output is source frame f until the loop end, then
    // 500 + (f - 500) % 500.
    for(int f : {100, 700, 999, 1000, 1250, 4321, 8000}) {
        const int source = f < frames ? f : 500 + (f - 500) % 500;
        CHECK(std::fabs(left(out, f) - static_cast<float>(source + 1) / frames) < 2e-3f);
    }
    // Note-off releases the looping voice; it fades out and stops.
    plugin.param(kRelease, 0.0); // 1 ms
    plugin.noteOff(kRootKey);
    const auto tail = plugin.render(4096);
    CHECK(energy(tail, 2048, 4096) == 0.0);
}

void testBidirectionalLoop(const std::string &path)
{
    Instance plugin;
    OPEN(plugin, path);
    const int frames = 1000;
    CHECK(loadPcm(plugin, 0, ramp(frames)) == kResultOk);
    plugin.param(slotParam(0, kSlotLoop), kLoopBidirectional);
    plugin.param(slotParam(0, kSlotLoopStart), 0.2); // frames 200..999, turning on 999
    plugin.noteOn(kRootKey);
    const auto out = plugin.render(8192);
    // Rises to the last loop frame, falls to the loop start, rises again.
    CHECK(left(out, 998) < left(out, 999));
    CHECK(left(out, 1001) < left(out, 999));
    CHECK(left(out, 1500) < left(out, 1400));
    CHECK(std::fabs(left(out, 999 + 799) - static_cast<float>(201) / frames) < 2e-3f); // Back at the start.
    CHECK(left(out, 2000) > left(out, 1900));
    for(int f = 0; f < 8192; ++f) {
        const float value = left(out, f);
        if(f >= 200 && (value < 0.2f || value > 1.0f + 1e-3f)) {
            CHECK(!"bidirectional playhead left the loop");
            break;
        }
    }
}

// One note on `pitch` in a fresh block; returns the left channel of `frames`.
std::vector<float> play(Instance &plugin, int pitch, int frames = 1024)
{
    plugin.noteOn(pitch);
    return plugin.render(frames);
}

void zone(Instance &plugin, int slot, int low, int high, int root, bool track = true)
{
    plugin.param(zoneParam(slot, kZoneMode), 1.0);
    plugin.param(zoneParam(slot, kZoneLow), key(low));
    plugin.param(zoneParam(slot, kZoneHigh), key(high));
    plugin.param(zoneParam(slot, kZoneRoot), key(root));
    plugin.param(zoneParam(slot, kZoneTrack), track ? 1.0 : 0.0);
}

void testKeyZones(const std::string &path)
{
    const int frames = 4000;
    // Pitch tracking: an octave up plays twice as fast, an octave down half.
    for(const auto &[pitch, step] : {std::pair<int, double>{72, 2.0}, {60, 1.0}, {48, 0.5}}) {
        Instance plugin;
        OPEN(plugin, path);
        CHECK(loadPcm(plugin, 0, ramp(frames)) == kResultOk);
        zone(plugin, 0, 48, 72, 60);
        const auto out = play(plugin, pitch);
        for(int f : {100, 700}) {
            const double source = f * step;
            CHECK(std::fabs(left(out, f) - static_cast<float>((source + 1) / frames)) < 2e-3f);
        }
    }
    // Keys outside the zone are silent, including the slot's old pad key.
    for(int pitch : {kRootKey, 47, 73}) {
        Instance plugin;
        OPEN(plugin, path);
        CHECK(loadPcm(plugin, 0, ramp(frames)) == kResultOk);
        zone(plugin, 0, 48, 72, 60);
        CHECK(energy(play(plugin, pitch), 0, 1024) == 0.0);
    }
    // Key Track off: every key in the zone plays the recorded pitch.
    {
        Instance plugin;
        OPEN(plugin, path);
        CHECK(loadPcm(plugin, 0, ramp(frames)) == kResultOk);
        zone(plugin, 0, 48, 72, 60, false);
        const auto out = play(plugin, 72);
        CHECK(std::fabs(left(out, 100) - 101.0f / frames) < 2e-3f);
    }
    // Overlapping zones layer, and pad slots keep their keys beside them.
    {
        Instance plugin;
        OPEN(plugin, path);
        CHECK(loadPcm(plugin, 0, ramp(frames)) == kResultOk);
        CHECK(loadPcm(plugin, 1, std::vector<float>(frames, 0.25f)) == kResultOk);
        CHECK(loadPcm(plugin, 2, std::vector<float>(frames, 0.5f)) == kResultOk);
        zone(plugin, 0, 48, 72, 60);
        zone(plugin, 1, 55, 65, 60, false);
        plugin.param(kRelease, 0.0); // 1 ms, so the layer is gone before the pad
        const auto layered = play(plugin, 60);
        CHECK(std::fabs(left(layered, 100) - (101.0f / frames + 0.25f)) < 2e-3f);
        plugin.noteOff(60);
        plugin.render(2048);
        const auto pad = play(plugin, kRootKey + 2);
        CHECK(std::fabs(left(pad, 100) - 0.5f) < 1e-3f);
    }
}

void testOutputRouting(const std::string &path)
{
    // Out 2 is inactive: the slot falls back to Main.
    {
        Instance plugin;
        OPEN(plugin, path);
        CHECK(loadPcm(plugin, 0, std::vector<float>(2048, 0.5f)) == kResultOk);
        plugin.param(slotParam(0, kSlotOutput), 1.0 / 7.0);
        plugin.noteOn(kRootKey);
        const auto main = plugin.render(1024, 0);
        CHECK(energy(main, 0, 1024) > 100.0);
    }
    // Out 2 is active: the slot plays there, Main stays silent, and a slot
    // left on Main still plays on Main.
    {
        Instance plugin;
        OPEN(plugin, path, true);
        CHECK(loadPcm(plugin, 0, std::vector<float>(2048, 0.5f)) == kResultOk);
        CHECK(loadPcm(plugin, 1, std::vector<float>(2048, 0.25f)) == kResultOk);
        plugin.param(slotParam(0, kSlotOutput), 1.0 / 7.0);
        plugin.noteOn(kRootKey);
        plugin.render(kBlock);
        const auto &aux = plugin.data.outputs[1];
        const auto &main = plugin.data.outputs[0];
        CHECK(std::fabs(aux.channelBuffers32[0][100] - 0.5f) < 1e-3f);
        CHECK(main.channelBuffers32[0][100] == 0.0f);
        plugin.noteOn(kRootKey + 1);
        plugin.render(kBlock);
        CHECK(std::fabs(aux.channelBuffers32[0][100] - 0.5f) < 1e-3f);
        CHECK(std::fabs(main.channelBuffers32[0][100] - 0.25f) < 1e-3f);
    }
}

void testStateRoundTrip(const std::string &path)
{
    auto state = owned(new MemoryStream);
    {
        Instance plugin;
        OPEN(plugin, path);
        CHECK(loadPcm(plugin, 2, ramp(1000)) == kResultOk);
        plugin.param(slotParam(2, kSlotLoop), kLoopForward);
        plugin.param(slotParam(2, kSlotLoopStart), 0.5);
        plugin.render(kBlock);
        CHECK(plugin.component->getState(state) == kResultOk);
    }
    Instance restored;
    OPEN(restored, path);
    state->seek(0, IBStream::kIBSeekSet, nullptr);
    CHECK(restored.component->setState(state) == kResultOk);
    restored.noteOn(kRootKey + 2);
    const auto out = restored.render(4096);
    CHECK(std::fabs(left(out, 3000) - static_cast<float>(500 + (3000 - 500) % 500 + 1) / 1000.0f) < 2e-3f);
}

} // namespace

int main(int argc, char **argv)
{
    if(argc < 2) {
        std::fprintf(stderr, "usage: %s <MlaSampler.vst3> [scratch dir]\n", argv[0]);
        return 2;
    }
    const std::string path = argv[1];
    testLayout(path);
    testLoopOff(path);
    testForwardLoop(path);
    testBidirectionalLoop(path);
    testKeyZones(path);
    testOutputRouting(path);
    testStateRoundTrip(path);
    if(failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("PASS: layout, key zones, loop off/forward/bidirectional, output routing, state");
    return 0;
}
