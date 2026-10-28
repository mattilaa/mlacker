// Exercise the actual VST3 processor and compiled MLang bridge together.
#define MLA_PUMP_TEST
#include "../src/plugin.cpp"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include <cstdlib>
#include <iostream>
#include <vector>
using namespace mla_pump;
static void check(bool ok, const char* message) {
    if(!ok) { std::cerr << message << '\n'; std::exit(1); }
}
// 8000 Hz at 120 BPM is 4000 samples per beat.
struct Fixture {
    Processor processor;
    Fixture() {
        check(processor.initialize(nullptr) == kResultOk, "initialize");
        ProcessSetup setup{kRealtime, kSample32, 8192, 8000};
        check(processor.setupProcessing(setup) == kResultOk, "setup");
        check(processor.setActive(true) == kResultOk, "activate");
    }
    ~Fixture() { processor.setActive(false); processor.terminate(); }
    void set(int i, double plain) {
        ParameterChanges changes;
        int32 qi = 0, pi = 0;
        auto* q = changes.addParameterData(kFirstParam + i, qi);
        q->addPoint(0, normalized(i, plain), pi);
        ProcessData data{}; data.inputParameterChanges = &changes;
        check(processor.process(data) == kResultOk, "parameter flush");
    }
    // A constant 1 / -1 input, so the output is the gain curve itself.
    std::array<std::vector<float>, 2> render(int count, ProcessContext* context = nullptr) {
        std::vector<float> left(count, 1.f), right(count, -1.f);
        std::array<std::vector<float>, 2> out{std::vector<float>(count), std::vector<float>(count)};
        float* inputs[]{left.data(), right.data()}; float* outputs[]{out[0].data(), out[1].data()};
        AudioBusBuffers in{}, output{}; in.numChannels = output.numChannels = 2;
        in.channelBuffers32 = inputs; output.channelBuffers32 = outputs;
        ProcessData data{}; data.numSamples = count; data.symbolicSampleSize = kSample32;
        data.numInputs = data.numOutputs = 1; data.inputs = &in; data.outputs = &output; data.processContext = context;
        check(processor.process(data) == kResultOk, "render");
        for(const auto& channel : out) for(float v : channel) check(std::isfinite(v), "non-finite output");
        return out;
    }
};
static ProcessContext playing(double tempo, double beat) {
    ProcessContext context{};
    context.sampleRate = 8000; context.tempo = tempo; context.projectTimeMusic = beat;
    context.state = ProcessContext::kPlaying | ProcessContext::kTempoValid | ProcessContext::kProjectTimeMusicValid;
    return context;
}
int main() {
    Fixture a;
    check(a.processor.getParameterCount() == kCount, "parameter count");
    for(int i = 0; i < kCount; ++i) {
        ParameterInfo info{};
        check(a.processor.getParameterInfo(i, info) == kResultOk, "parameter metadata");
        check(info.id == kFirstParam + i, "stable ID");
        check(std::abs(a.processor.getParamNormalized(info.id) - normalized(i, specs[i].initial)) < 1e-9, "controller default");
    }

    // Manual 120 BPM, a 1/4 pump: 4000 samples a period. Attack 0, no hold,
    // a straight release over the whole period: the gain ramps 0.3 -> 1.
    a.set(kTempo, kManual); a.set(kBpm, 120); a.set(kAttack, 0); a.set(kHold, 0);
    a.set(kRelease, 1); a.set(kCurve, 0); a.set(kDepth, 0.7);
    auto ramp = a.render(8000);
    for(int i = 0; i < 8000; ++i) {
        const float want = 0.3f + 0.7f * (i % 4000) / 4000.f;
        check(std::abs(ramp[0][i] - want) < 1e-3f && std::abs(ramp[1][i] + want) < 1e-3f, "linear release from 1 - depth");
    }

    // Curve bends the release: halfway through, x^4 is far below linear.
    Fixture curved; curved.set(kTempo, kManual); curved.set(kBpm, 120); curved.set(kAttack, 0);
    curved.set(kHold, 0); curved.set(kRelease, 1); curved.set(kCurve, 1); curved.set(kDepth, 1);
    auto bent = curved.render(4000);
    check(std::abs(bent[0][2000] - 0.0625f) < 1e-3f, "curve 1 is x^4");

    // Hold keeps the gain down, then the release finishes within the period.
    Fixture held; held.set(kTempo, kManual); held.set(kBpm, 120); held.set(kAttack, 0);
    held.set(kHold, 0.25); held.set(kRelease, 0.5); held.set(kCurve, 0); held.set(kDepth, 0.5);
    auto h = held.render(4000);
    check(std::abs(h[0][500] - 0.5f) < 1e-4f && std::abs(h[0][999] - 0.5f) < 1e-4f, "hold stays down");
    check(std::abs(h[0][2000] - 0.75f) < 1e-3f, "halfway through the release");
    check(h[0][3000] == 1.f && h[0][3999] == 1.f, "recovered before the next beat");

    // Attack: 10 ms (80 samples) from full gain down to 1 - depth.
    Fixture attack; attack.set(kTempo, kManual); attack.set(kBpm, 120); attack.set(kAttack, 10); attack.set(kDepth, 0.8);
    auto at = attack.render(200);
    check(at[0][0] > 0.9999f && std::abs(at[0][40] - 0.6f) < 1e-3f && std::abs(at[0][81] - 0.2f) < 1e-3f, "attack ramps down");

    // Host: the dip follows the host beat. At 138 BPM, beat 2.5 is halfway
    // through a period; a block starting on beat 3 dips at once.
    Fixture host; host.set(kAttack, 0); host.set(kHold, 0); host.set(kRelease, 1); host.set(kCurve, 0); host.set(kDepth, 1);
    auto mid = playing(138, 2.5);
    check(std::abs(host.render(64, &mid)[0][0] - 0.5f) < 1e-3f, "follows the host beat");
    auto on = playing(138, 3.0);
    check(host.render(64, &on)[0][0] < 1e-3f, "dips on the host beat");
    // The 1/2 rate dips every other beat.
    host.set(kRate, 1);
    auto odd = playing(138, 3.0);
    check(std::abs(host.render(64, &odd)[0][0] - 0.5f) < 1e-3f, "1/2 rate");

    // Depth 0 and Bypass are transparent.
    Fixture flat; flat.set(kDepth, 0);
    auto level = flat.render(4000);
    for(float v : level[0]) check(v == 1.f, "depth 0 is transparent");
    Fixture bypass; bypass.set(kBypass, 1);
    auto through = bypass.render(4000);
    for(float v : through[0]) check(v == 1.f, "bypass is transparent");

    // State round trip.
    Fixture saved; saved.set(kDepth, 0.42); saved.set(kRate, 3);
    MemoryStream stream;
    check(saved.processor.getState(&stream) == kResultOk, "get state");
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    Fixture restored;
    check(restored.processor.setState(&stream) == kResultOk, "set state");
    check(std::abs(restored.processor.getParamNormalized(kFirstParam + kDepth) - 0.42) < 1e-9, "depth restored");
    check(std::abs(restored.processor.getParamNormalized(kFirstParam + kRate) - normalized(kRate, 3)) < 1e-9, "rate restored");
    std::cout << "mla_pump tests passed\n";
    return 0;
}
