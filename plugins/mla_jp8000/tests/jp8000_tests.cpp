#define MLA_JP8000_TEST
#include "../src/plugin.cpp"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
// Audit C++ allocation while rendering, including the first state push.
static bool auditAllocations = false;
static size_t renderAllocations = 0;
void* operator new(std::size_t size)
{
    if(auditAllocations)
        ++renderAllocations;
    if(void* p = std::malloc(size))
        return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size)
{
    return ::operator new(size);
}
void operator delete(void* p) noexcept
{
    std::free(p);
}
void operator delete[](void* p) noexcept
{
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept
{
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept
{
    std::free(p);
}
using namespace mla_jp8000;
static int checks = 0;
static void check(bool ok, const char* why)
{
    ++checks;
    if(!ok)
    {
        std::cerr << "FAIL: " << why << '\n';
        std::exit(1);
    }
}
struct Audio
{
    std::vector<float> left, right;
};
struct Fixture
{
    Processor processor;
    EventList events{4096};
    Steinberg::Vst::ParameterChanges parameters;
    double rate = 48000;
    uint64 silenceFlags = 0;
    explicit Fixture(double sampleRate = 48000) : rate(sampleRate)
    {
        check(processor.initialize(nullptr) == kResultOk, "initialize");
        ProcessSetup setup{kOffline, kSample32, 4096, rate};
        check(processor.setupProcessing(setup) == kResultOk, "setup");
        check(processor.setActive(true) == kResultOk, "active");
    }
    ~Fixture()
    {
        processor.setActive(false);
        processor.terminate();
    }
    void set(ParamID id, double value, int offset = 0)
    {
        int32 q = 0, p = 0;
        auto* queue = parameters.addParameterData(id, q);
        check(queue && queue->addPoint(offset, value, p) == kResultOk,
              "queue parameter");
    }
    void note(int pitch, bool on = true, int offset = 0, int channel = 0)
    {
        Event e{};
        e.sampleOffset = offset;
        if(on)
        {
            e.type = Event::kNoteOnEvent;
            e.noteOn.pitch = int16(pitch);
            e.noteOn.velocity = .8;
            e.noteOn.channel = int16(channel);
            e.noteOn.noteId = -1;
        }
        else
        {
            e.type = Event::kNoteOffEvent;
            e.noteOff.pitch = int16(pitch);
            e.noteOff.channel = int16(channel);
            e.noteOff.noteId = -1;
        }
        check(events.addEvent(e) == kResultOk, "queue note");
    }
    Audio render(int frames, int block = 256)
    {
        Audio audio;
        audio.left.resize(frames);
        audio.right.resize(frames);
        for(int at = 0; at < frames;)
        {
            const int n = std::min(block, frames - at);
            float* channels[]{audio.left.data() + at, audio.right.data() + at};
            AudioBusBuffers out{};
            out.numChannels = 2;
            out.channelBuffers32 = channels;
            ProcessData data{};
            data.processMode = kOffline;
            data.symbolicSampleSize = kSample32;
            data.numSamples = n;
            data.numOutputs = 1;
            data.outputs = &out;
            data.inputEvents = &events;
            data.inputParameterChanges = &parameters;
            renderAllocations = 0;
            auditAllocations = true;
            const auto result = processor.process(data);
            auditAllocations = false;
            check(result == kResultOk, "render");
            check(renderAllocations == 0, "no C++ allocation on audio thread");
            silenceFlags = out.silenceFlags;
            events.clear();
            parameters.clearQueue();
            at += n;
        }
        return audio;
    }
    void dry(int mode = kDual)
    {
        set(kMode, double(mode) / 3);
        set(kPartDetune, .5);
        set(kUpperPan, .5);
        set(kLowerPan, .5);
        for(int p = 0; p < 2; ++p)
        {
            const auto put = [&](ParamID id, double v)
            { set(partId(p, id), v); };
            using namespace mla_8000;
            put(kChorusTypeParam, 0);
            put(kDelayTypeParam, 0);
            put(kOsc1WaveParam, 5. / 6);
            put(kBalanceParam, 0);
            put(kCutoffParam, 1);
            put(kFilterSlopeParam, 0);
            put(kKeyFollowParam, 0);
            put(kResonanceParam, 0);
            put(kFilterEnvDepthParam, .5);
            put(kLfo1PitchParam, 0);
            put(kLfo2PitchParam, .5);
            put(kLfo1FilterParam, .5);
            put(kLfo2FilterParam, .5);
            put(kLfo1AmpParam, 0);
            put(kLfo2AmpParam, 0);
            put(kAmpAttackParam, 0);
            put(kAmpDecayParam, 0);
            put(kAmpSustainParam, 1);
            put(kAmpReleaseParam, timeNorm(.05));
            put(kFilterReleaseParam, timeNorm(.05));
            put(kVelocityAmpParam, 0);
            put(kTrebleParam, .5);
            put(kBassParam, .5);
        }
    }
};
static double rms(const std::vector<float>& a, int from = 0)
{
    double sum = 0;
    for(size_t i = from; i < a.size(); ++i)
        sum += double(a[i]) * a[i];
    return std::sqrt(sum / std::max(size_t(1), a.size() - from));
}
static double tone(const std::vector<float>& a, double hz, double rate = 48000)
{
    double re = 0, im = 0;
    for(size_t i = a.size() / 2; i < a.size(); ++i)
    {
        const double phase = 2 * M_PI * hz * i / rate;
        re += a[i] * std::cos(phase);
        im += a[i] * std::sin(phase);
    }
    return std::hypot(re, im) * 4 / a.size();
}
static double peak(const std::vector<float>& a)
{
    double result = 0;
    for(float v : a)
    {
        if(!std::isfinite(v))
            check(false, "finite output");
        result = std::max(result, double(std::abs(v)));
    }
    return result;
}
static void u64(std::ostream& f, uint64_t v)
{
    for(int i = 0; i < 8; ++i)
        f.put(char(v >> (8 * i)));
}
static void text(std::ostream& f, const std::string& s)
{
    u64(f, s.size());
    f.write(s.data(), s.size());
}
static void real(std::ostream& f, double v)
{
    uint64_t bits;
    std::memcpy(&bits, &v, 8);
    u64(f, bits);
}
static void wav(const std::string& path, const Audio& audio, int rate = 48000)
{
    std::ofstream f(path, std::ios::binary);
    const auto n32 = [&](uint32_t n)
    {
        for(int i = 0; i < 4; ++i)
            f.put(char(n >> (i * 8)));
    };
    const auto n16 = [&](uint16_t n)
    {
        f.put(char(n));
        f.put(char(n >> 8));
    };
    f.write("RIFF", 4);
    n32(uint32_t(36 + audio.left.size() * 4));
    f.write("WAVEfmt ", 8);
    n32(16);
    n16(1);
    n16(2);
    n32(rate);
    n32(rate * 4);
    n16(4);
    n16(16);
    f.write("data", 4);
    n32(uint32_t(audio.left.size() * 4));
    for(size_t i = 0; i < audio.left.size(); ++i)
    {
        n16(uint16_t(int16_t(std::clamp(audio.left[i], -1.f, 1.f) * 32767)));
        n16(uint16_t(int16_t(std::clamp(audio.right[i], -1.f, 1.f) * 32767)));
    }
}
static void factoryPresets(const std::string& folder,
                           const std::string& renderFolder)
{
    if(!folder.empty())
        std::filesystem::create_directories(folder);
    Fixture defaults;
    Values base{};
    for(int i = 0; i < kParamCount; ++i)
        base[i] = defaults.processor.getParameterObject(paramId(i))
                      ->getInfo()
                      .defaultNormalizedValue;
    for(int p = 0; p < kPadCount; ++p)
    {
        const auto values = factoryPad(base, p);
        std::string name = kPadNames[p];
        std::ofstream f;
        if(!folder.empty())
            f.open(folder + "/" + name + ".mlapre", std::ios::binary);
        if(!folder.empty())
        {
            check(f.is_open(), "open factory preset output");
            text(f, "MLAPRE");
            u64(f, 1);
            u64(f, 0);
            text(f, "Mla JP8000");
            u64(f, kParamCount);
        }
        Fixture voice;
        for(int i = 0; i < kParamCount; ++i)
        {
            if(!folder.empty())
            {
                u64(f, paramId(i));
                real(f, values[i]);
            }
            voice.set(paramId(i), values[i]);
        }
        if(!folder.empty())
        {
            f.flush();
            check(f.good(), "write factory preset output");
        }
        for(int note : {48, 55, 60, 63})
            voice.note(note);
        auto held = voice.render(48000 * 6);
        for(int note : {48, 55, 60, 63})
            voice.note(note, false);
        auto released = voice.render(48000 * 6);
        held.left.insert(held.left.end(), released.left.begin(),
                         released.left.end());
        held.right.insert(held.right.end(), released.right.begin(),
                          released.right.end());
        check(rms(held.left) > .003 && peak(held.left) < 1 &&
                  peak(held.right) < 1,
              "factory pad useful level and headroom");
        double side = 0, mid = 0;
        for(size_t i = 48000; i < 240000; ++i)
        {
            side += std::pow(held.left[i] - held.right[i], 2);
            mid += std::pow(held.left[i] + held.right[i], 2);
        }
        check(side > .002 * mid, "factory pad stereo width");
        for(const auto* channel : {&held.left, &held.right})
        {
            double dc = 0, energy = 0;
            for(size_t i = 48000 * 3; i < 48000 * 5; ++i)
            {
                dc += (*channel)[i];
                energy += double((*channel)[i]) * (*channel)[i];
            }
            dc /= 96000;
            energy = std::sqrt(energy / 96000);
            if(std::abs(dc) >= energy * .03)
                std::cerr << name << " DC=" << dc << " RMS=" << energy << '\n';
            check(std::abs(dc) < energy * .03,
                  "factory pad has no significant DC offset");
        }
        if(!renderFolder.empty())
        {
            std::filesystem::create_directories(renderFolder);
            wav(renderFolder + "/" + name + ".wav", held);
        }
    }
}
int main(int argc, char** argv)
{
    {
        Fixture f;
        check(f.processor.getParameterCount() == kParamCount,
              "complete two-part parameter layout");
        check(f.processor.canProcessSampleSize(kSample64) == kResultFalse,
              "reject doubles");
        Audio idle = f.render(512);
        check(rms(idle.left) == 0 && rms(idle.right) == 0 &&
                  f.silenceFlags == 3,
              "silent idle");
        ParamID id = 0;
        check(f.processor.getMidiControllerAssignment(0, 0, 64, id) ==
                      kResultOk &&
                  id == kSustain,
              "sustain mapping");
    }
    {
        Fixture f;
        f.dry(kDual);
        ParamID upper = 0, lower = 0;
        check(f.processor.getMidiControllerAssignment(0, 0, 20, upper) ==
                      kResultOk &&
                  upper == partId(0, mla_8000::kKeyFollowParam),
              "CC20 maps upper filter key follow");
        check(f.processor.getMidiControllerAssignment(0, 0, 21, lower) ==
                      kResultOk &&
                  lower == partId(1, mla_8000::kKeyFollowParam),
              "CC21 maps lower filter key follow");
        ParamID upperSlope = 0, lowerSlope = 0;
        check(f.processor.getMidiControllerAssignment(0, 0, 22, upperSlope) ==
                      kResultOk &&
                  upperSlope == partId(0, mla_8000::kFilterSlopeParam),
              "CC22 maps upper filter slope");
        check(f.processor.getMidiControllerAssignment(0, 0, 23, lowerSlope) ==
                      kResultOk &&
                  lowerSlope == partId(1, mla_8000::kFilterSlopeParam),
              "CC23 maps lower filter slope");
        for(const auto& mapping : {std::pair<int, ParamID>{16, partId(0, mla_8000::kOsc1Ctrl1Param)},
                                  {17, partId(0, mla_8000::kOsc1Ctrl2Param)},
                                  {24, partId(1, mla_8000::kOsc1Ctrl1Param)},
                                  {25, partId(1, mla_8000::kOsc1Ctrl2Param)}})
        {
            ParamID target = 0;
            check(f.processor.getMidiControllerAssignment(0, 0, mapping.first, target) == kResultOk &&
                      target == mapping.second,
                  "sequencer CC maps Super Saw detune and mix independently per part");
        }
        f.set(upperSlope, 1);
        f.set(lowerSlope, 1);
        f.set(upper, 1);
        f.set(lower, 1);
        f.note(72);
        auto out = f.render(4096);
        MemoryStream state;
        f.processor.getState(&state);
        state.seek(0, IBStream::kIBSeekSet, nullptr);
        IBStreamer saved(&state, kLittleEndian);
        uint32 magic, version, id;
        int32 count;
        double value;
        saved.readInt32u(magic);
        saved.readInt32u(version);
        saved.readInt32(count);
        bool u = false, l = false, us = false, ls = false;
        for(int i = 0; i < count; ++i)
        {
            saved.readInt32u(id);
            saved.readDouble(value);
            if(id == upper)
                u = value == 1;
            if(id == lower)
                l = value == 1;
            if(id == upperSlope)
                us = value == 1;
            if(id == lowerSlope)
                ls = value == 1;
        }
        check(us && ls, "both layers retain 24 dB filter selection");
        check(u && l && rms(out.left) > .02,
              "both layers retain sequencer key follow at 100 percent");
    }
    for(int mode : {kSingleUpper, kSingleLower, kDual})
    {
        Fixture f;
        f.dry(mode);
        f.set(kUpperTranspose, .5);
        f.set(kLowerTranspose, .75);
        f.note(57);
        auto audio = f.render(24000);
        const double a = tone(audio.left, 220), b = tone(audio.left, 440);
        check(rms(audio.left) > .015, "mode sounds");
        if(mode == kSingleUpper)
            check(a > .02, "upper fundamental");
        if(mode == kSingleLower)
            check(b > 20 * a, "lower transpose only");
        if(mode == kDual)
            check(a > .02 && b > .02, "both parts audible");
    }
    {
        Fixture f;
        f.dry(kDual);
        f.set(kVoiceAssign, 0); // Lower two, upper six.
        f.set(kUpperPan, 1);
        f.set(kLowerPan, 0);
        f.note(48);
        f.render(4096);
        f.note(55);
        f.note(60);
        auto dual = f.render(24000);
        const double low = tone(dual.left, 130.81278265),
                     high = tone(dual.right, 130.81278265);
        check(high > .02 && low < high * .12,
              "two/six voice allocation steals only from exhausted part");
        f.set(kMode, double(kSingleLower) / 3);
        f.note(48);
        f.note(55);
        f.note(60);
        auto single = f.render(24000);
        check(tone(single.left, 130.81278265) > .02 && rms(single.right) < 1e-7,
              "single part regains all eight voices");
    }
    {
        Fixture f;
        f.dry(kSplitMode);
        f.set(kUpperPan, 1);
        f.set(kLowerPan, 0);
        f.set(kSplit, 59. / 127);
        f.note(48);
        auto low = f.render(12000);
        check(rms(low.left) > .02 && rms(low.right) < 1e-7, "split lower pan");
        f.note(48, false);
        f.render(12000);
        f.note(72);
        auto high = f.render(12000);
        check(rms(high.right) > .02 && rms(high.left) < 1e-7,
              "split upper pan");
    }
    {
        Fixture f;
        f.dry(kDual);
        f.set(kUpperPan, 1);
        f.set(kLowerPan, 0);
        f.set(kUpperChannel, 1. / 16);
        f.set(kLowerChannel, 2. / 16);
        f.note(60, true, 0, 0);
        auto upper = f.render(12000);
        check(rms(upper.right) > .02 && rms(upper.left) < 1e-7,
              "upper MIDI channel routing");
        f.note(60, false, 0, 0);
        f.render(12000);
        f.note(48, true, 0, 1);
        auto lower = f.render(12000);
        check(rms(lower.left) > .02 && rms(lower.right) < 1e-7,
              "lower MIDI channel routing");
        f.note(48, false, 0, 1);
        f.render(12000);
        f.note(72, true, 0, 2);
        auto ignored = f.render(12000);
        check(rms(ignored.left) < 1e-7 && rms(ignored.right) < 1e-7,
              "other MIDI channels ignored when parts assigned");
    }
    {
        Fixture f;
        f.dry(kSingleUpper);
        f.note(60);
        f.render(4096);
        f.set(kSustain, 1);
        f.note(60, false);
        auto sustained = f.render(12000);
        check(rms(sustained.left) > .02, "sustain holds released note");
        f.set(kSustain, 0);
        auto released = f.render(24000);
        check(rms(released.left, 18000) < 1e-5, "sustain pedal up releases");
    }
    {
        Fixture f;
        f.dry(kSingleUpper);
        f.note(60, true, 0, 0);
        f.note(60, true, 0, 1);
        f.render(4096);
        f.note(60, false, 0, 0);
        auto held = f.render(12000);
        check(rms(held.left) > .02, "overlapping channel note remains held");
        f.note(60, false, 0, 1);
        auto released = f.render(24000);
        check(rms(released.left, 18000) < 1e-5, "last channel releases pitch");
    }
    {
        Fixture f;
        f.dry(kSingleUpper);
        f.set(kMaster, 0, 0);
        f.set(kMaster, mla_8000::kUnityLevelNorm, 128);
        f.note(60);
        auto audio = f.render(512);
        for(int i = 0; i < 128; ++i)
            check(audio.left[i] == 0 && audio.right[i] == 0,
                  "sample accurate master automation");
        check(rms(audio.left, 128) > .02 && f.silenceFlags == 0,
              "automation unmutes remaining samples");
    }
    {
        Fixture a, b;
        a.dry(kDual);
        b.dry(kDual);
        a.note(60, true, 37);
        b.note(60, true, 37);
        auto x = a.render(4096, 256), y = b.render(4096, 4096);
        for(int i = 0; i < 37; ++i)
            check(x.left[i] == 0, "sample accurate note onset");
        double error = 0;
        for(int i = 0; i < 4096; ++i)
            error = std::max(error, double(std::abs(x.left[i] - y.left[i])));
        check(error < 1e-6, "independent of host block size");
    }
    {
        Fixture a, b;
        a.dry(kDual);
        a.render(0);
        a.render(256);
        MemoryStream stream;
        check(a.processor.getState(&stream) == kResultOk,
              "save performance state");
        stream.seek(0, IBStream::kIBSeekSet, nullptr);
        check(b.processor.setState(&stream) == kResultOk,
              "restore performance state");
        for(int i = 0; i < kParamCount; ++i)
        {
            String128 text{};
            double v = 0;
            ParameterInfo info{};
            a.processor.getParameterInfo(i, info);
            a.processor.getParamStringByValue(info.id, .5, text);
            check(text[0] != 0, "parameter display");
        }
        MemoryStream valid;
        check(b.processor.getState(&valid) == kResultOk, "resaved state");
        check(stream.getSize() == valid.getSize() &&
                  std::memcmp(stream.getData(), valid.getData(),
                              valid.getSize()) == 0,
              "state byte round trip");
        MemoryStream truncated;
        truncated.write(stream.getData(), 12, nullptr);
        truncated.seek(0, IBStream::kIBSeekSet, nullptr);
        check(b.processor.setState(&truncated) != kResultOk,
              "truncated state rejected");
        MemoryStream after;
        b.processor.getState(&after);
        check(after.getSize() == valid.getSize() &&
                  std::memcmp(after.getData(), valid.getData(),
                              valid.getSize()) == 0,
              "invalid state preserves patch");
    }
    {
        Fixture a, b;
        a.dry(kSingleUpper);
        b.dry(kSingleUpper);
        a.set(mla_8000::kOsc1WaveParam, 0);
        b.set(mla_8000::kOsc1WaveParam, 0);
        b.set(mla_8000::kCrossModParam, .25);
        a.note(57);
        b.note(57);
        auto plain = a.render(16000), fm = b.render(16000);
        double difference = 0;
        for(int i = 4000; i < 16000; ++i)
            difference += std::pow(plain.left[i] - fm.left[i], 2);
        check(std::sqrt(difference / 12000) > .02,
              "cross modulation actually modulates Super Saw");
    }
    for(int chorus = 0; chorus <= 12; ++chorus)
    {
        Fixture f(44100);
        f.dry(kSingleUpper);
        f.set(partId(0, mla_8000::kChorusTypeParam), chorus / 12.);
        f.set(partId(0, mla_8000::kChorusLevelParam), .9);
        f.note(60);
        auto audio = f.render(8000);
        check(rms(audio.left) > .001 && peak(audio.left) < 2 &&
                  peak(audio.right) < 2,
              "all chorus/phaser models finite and audible");
    }
    for(int delay = 1; delay <= 5; ++delay)
    {
        Fixture f, control;
        f.dry(kSingleUpper);
        control.dry(kSingleUpper);
        f.set(kUpperPan, 0);
        control.set(kUpperPan, 0);
        f.set(partId(0, mla_8000::kDelayTypeParam), delay / 5.);
        f.set(partId(0, mla_8000::kDelaySyncParam), 0);
        f.set(partId(0, mla_8000::kDelayTimeParam), (80. - 1) / 1249.);
        f.set(partId(0, mla_8000::kDelayFeedbackParam), 0);
        f.set(partId(0, mla_8000::kDelayLevelParam), 1);
        f.note(60);
        control.note(60);
        f.render(256);
        control.render(256);
        f.note(60, false);
        control.note(60, false);
        auto tail = f.render(12000);
        const auto dry = control.render(12000);
        check(peak(tail.left) < 2 && peak(tail.right) < 2, "delay bounded");
        // Isolate the repeats from the dry voice and its DC-blocker transient.
        for(size_t i = 0; i < tail.left.size(); ++i)
        {
            tail.left[i] -= dry.left[i];
            tail.right[i] -= dry.right[i];
        }
        const auto energy = [&](const std::vector<float>& v)
        {
            double x = 0;
            for(int i = 3800; i < 7500; ++i)
                x += v[i] * v[i];
            return x;
        };
        if(delay == 1 || delay == 3)
            check(energy(tail.left) > 100 * energy(tail.right),
                  "panning L-R first echo enters left");
        if(delay == 2)
            check(energy(tail.right) > 10 * energy(tail.left),
                  "panning R-L first echo enters right");
        if(delay >= 4)
            check(std::abs(energy(tail.left) - energy(tail.right)) < 1e-5,
                  "mono delay equal returns");
    }
    for(double rate : {44100., 96000.})
    {
        Fixture f(rate);
        f.dry();
        for(int n = 36; n < 72; ++n)
            f.note(n);
        auto audio = f.render(4096);
        check(peak(audio.left) < 3 && peak(audio.right) < 3,
              "bounded voice stealing at multiple rates");
    }
    factoryPresets(argc > 1 ? argv[1] : "", argc > 2 ? argv[2] : "");
    std::cout << "PASS: " << checks << " JP8000 checks\n";
}
