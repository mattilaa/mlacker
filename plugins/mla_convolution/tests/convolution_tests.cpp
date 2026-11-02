#define MLA_CONVOLUTION_TEST
#include "../src/plugin.cpp"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include <cstdlib>
#include <iostream>
#include <random>
using namespace mla_convolution;
static void check(bool ok, const char* why)
{
    if(!ok)
    {
        std::cerr << why << '\n';
        std::exit(1);
    }
}
int main()
{
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> random(-.1f, .1f);
    std::vector<float> ir(1502), input(6000);
    for(auto& v : ir)
        v = random(rng);
    for(auto& v : input)
        v = random(rng);
    Convolution engine(ir);
    for(size_t i = 0; i < input.size() / 2; ++i)
    {
        float l, r;
        engine.process(input[i * 2], input[i * 2 + 1], 1, l, r);
        for(int c = 0; c < 2; ++c)
        {
            double ref = 0;
            if(i >= Convolution::block)
                for(size_t k = 0;
                    k < ir.size() / 2 && k <= i - Convolution::block; ++k)
                    ref += double(ir[k * 2 + c]) *
                           input[(i - Convolution::block - k) * 2 + c];
            check(std::abs((c ? r : l) - ref) < 2e-5,
                  "FFT output differs from direct stereo convolution");
        }
    }
    engine.reset();
    for(int i = 0; i < 1200; ++i)
    {
        float l, r;
        engine.process(i == 0 ? 1 : 0, 0, 0, l, r);
        check(std::abs(l - (i == 256 ? 1 : 0)) < 1e-6 && r == 0,
              "dry latency/reset");
    }
    // Tiny PCM16 stereo WAV, then a truncated replacement. Decoder resamples.
    const std::string path = "/tmp/mlacker-convolution-" +
                             std::to_string(std::random_device{}()) + ".wav";
    const unsigned char wav[]{
        'R', 'I', 'F', 'F',  44, 0, 0,    0, 'W',  'A', 'V', 'E',  'f',
        'm', 't', ' ', 16,   0,  0, 0,    1, 0,    2,   0,   0x80, 0xbb,
        0,   0,   0,   0xee, 2,  0, 4,    0, 16,   0,   'd', 'a',  't',
        'a', 8,   0,   0,    0,  0, 0x40, 0, 0x20, 0,   0,   0,    0};
    {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(wav), sizeof(wav));
    }
    auto decoded = loadWav(path.c_str(), 96000);
    check(decoded.size() == 8 && decoded[0] == .5f && decoded[1] == .25f,
          "decode/resample");
    Processor processor;
    check(processor.initialize(nullptr) == kResultOk, "initialize");
    ProcessSetup setup{kRealtime, kSample32, 1024, 48000};
    check(processor.setupProcessing(setup) == kResultOk, "setup");
    HostMessage message;
    message.setMessageID("mla.convolution.load");
    message.getAttributes()->setBinary("path", path.data(),
                                       uint32(path.size()));
    check(processor.notify(&message) == kResultOk, "load message");
    MemoryStream state;
    check(processor.getState(&state) == kResultOk, "save state");
    check(processor.getLatencySamples() == 256 &&
              processor.getTailSamples() == 258,
          "latency/tail");
    // Exercise the VST process path, including a silent input block with a
    // tail.
    std::vector<float> left(512), right(512), outLeft(512), outRight(512);
    left[0] = right[0] = 1;
    float* inputs[]{left.data(), right.data()};
    float* outputs[]{outLeft.data(), outRight.data()};
    AudioBusBuffers in{}, out{};
    in.numChannels = out.numChannels = 2;
    in.channelBuffers32 = inputs;
    out.channelBuffers32 = outputs;
    ProcessData audio{};
    audio.symbolicSampleSize = kSample32;
    audio.numSamples = 128;
    audio.numInputs = audio.numOutputs = 1;
    audio.inputs = &in;
    audio.outputs = &out;
    check(processor.process(audio) == kResultOk, "first audio block");
    in.silenceFlags = 3;
    audio.numSamples = 512;
    check(processor.process(audio) == kResultOk, "silent tail block");
    check(std::abs(outLeft[128] - .5f) < 1e-5 &&
              std::abs(outRight[128] - .25f) < 1e-5,
          "plugin stereo tail on silent input");
    setup.sampleRate = 96000;
    check(processor.setupProcessing(setup) == kResultOk &&
              processor.getTailSamples() == 260,
          "reload at a new sample rate");
    setup.sampleRate = 48000;
    check(processor.setupProcessing(setup) == kResultOk, "restore sample rate");
    state.seek(0, IBStream::kIBSeekSet, nullptr);
    check(processor.setState(&state) == kResultOk, "restore state");
    {
        std::ofstream f(path, std::ios::binary);
        f << "broken";
    }
    check(processor.notify(&message) != kResultOk &&
              processor.getTailSamples() == 258,
          "failed replacement must preserve response");
    std::remove(path.c_str());
    processor.terminate();
    std::cout << "PASS: stereo reference convolution, dry latency, reset, WAV "
                 "resampling, plugin load/state, failed replacement\n";
}
