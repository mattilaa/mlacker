// Offline tests load the real VST3 bundle and measure its rendered audio.
#include "../src/parameters.h"
#include "base/source/fstreamer.h"
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

class Application final : public HostApplication {
  public:
    tresult PLUGIN_API getName(String128 name) override
    {
        const char16_t text[] = u"mla_mini_tests";
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
    ParameterChanges changes{256};
    ProcessContext context{};
    std::vector<std::string> subCategories;
    int32 lastSilenceFlags = 0;

    bool open(const std::string &path, double rate = kRate)
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
        ProcessSetup setup{kOffline, kSample32, kBlock, rate};
        if(processor->setupProcessing(setup) != kResultOk || !data.prepare(*component, kBlock, kSample32))
            return false;
        context.sampleRate = rate;
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

    void param(ParamID id, double value, int32 offset = 0)
    {
        int32 index = 0;
        if(auto *queue = changes.addParameterData(id, index))
            queue->addPoint(offset, value, index);
    }

    // A MIDI CC as a host sends it: through the plug-in's MIDI mapping.
    void cc(int number, int value, int32 offset = 0)
    {
        auto mapping = U::cast<IMidiMapping>(provider->getControllerPtr());
        ParamID id = 0;
        if(!mapping || mapping->getMidiControllerAssignment(0, 0, static_cast<CtrlNumber>(number), id) != kResultOk) {
            std::fprintf(stderr, "CC %d is not mapped\n", number);
            ++failures;
            return;
        }
        param(id, value / (number == kPitchBend ? 16383.0 : 127.0), offset);
    }

    void noteOn(int pitch, float velocity = 100.0f / 127.0f, int32 offset = 0, int16 channel = 0)
    {
        Event e{};
        e.type = Event::kNoteOnEvent;
        e.sampleOffset = offset;
        e.noteOn.channel = channel;
        e.noteOn.pitch = static_cast<int16>(pitch);
        e.noteOn.velocity = velocity;
        e.noteOn.noteId = -1;
        events.addEvent(e);
    }

    void noteOff(int pitch, int32 offset = 0, int16 channel = 0)
    {
        Event e{};
        e.type = Event::kNoteOffEvent;
        e.sampleOffset = offset;
        e.noteOff.channel = channel;
        e.noteOff.pitch = static_cast<int16>(pitch);
        e.noteOff.noteId = -1;
        events.addEvent(e);
    }

    // Render about `seconds` (whole blocks).
    Stereo render(double seconds) { return renderFrames(static_cast<int>(kRate * seconds + kBlock - 1) / kBlock * kBlock); }

    Stereo renderFrames(int frames)
    {
        Stereo out;
        for(int done = 0; done < frames; done += kBlock) {
            data.numSamples = kBlock;
            processor->process(data);
            events.clear();
            changes.clearQueue();
            const auto &bus = data.outputs[0];
            lastSilenceFlags = static_cast<int32>(bus.silenceFlags);
            for(int32 i = 0; i < kBlock; ++i) {
                out.left.push_back(bus.channelBuffers32[0][i]);
                out.right.push_back(bus.channelBuffers32[1][i]);
            }
        }
        return out;
    }
};

size_t at(double seconds) { return static_cast<size_t>(seconds * kRate); }

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

// Mean frequency from upward zero crossings in x[from, to).
double zeroCrossingHz(const std::vector<float> &x, size_t from, size_t to)
{
    double first = -1.0, last = -1.0;
    int crossings = 0;
    for(size_t i = from + 1; i < std::min(to, x.size()); ++i) {
        if(x[i - 1] < 0.0f && x[i] >= 0.0f) {
            const double t = static_cast<double>(i - 1) + x[i - 1] / (x[i - 1] - x[i]);
            if(first < 0.0)
                first = t;
            else
                ++crossings;
            last = t;
        }
    }
    return crossings > 0 ? kRate * crossings / (last - first) : 0.0;
}

// Amplitude of the `hz` component in x[from, to) (Hann-windowed DFT bin).
double tone(const std::vector<float> &x, size_t from, size_t to, double hz)
{
    to = std::min(to, x.size());
    double re = 0.0, im = 0.0, weights = 0.0;
    const double n = static_cast<double>(to - from);
    for(size_t i = from; i < to; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * (i - from) / (n - 1.0));
        const double phase = 2.0 * M_PI * hz * static_cast<double>(i) / kRate;
        re += w * x[i] * std::cos(phase);
        im += w * x[i] * std::sin(phase);
        weights += w;
    }
    return 2.0 * std::sqrt(re * re + im * im) / weights;
}

// Share of the energy above `hz` (one-pole split; rough but monotonic).
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

void set(Instance& i, int index, double value, int offset = 0) { i.param(100 + index, mla_mini::normalized(index, value), offset); }
void plainPatch(Instance& i) {
    using namespace mla_mini;
    set(i, Wave1, 0); set(i, Level1, 1); set(i, Level2, 0); set(i, Level3, 0);
    set(i, Range2, 0); // Pitch tests use concert pitch when they isolate OSC 2.
    set(i, Cutoff, 15000); set(i, EnvDepth, 0); set(i, Resonance, 0); set(i, Feedback, 0);
    set(i, Drive, 1); set(i, KeyFollow, 0); set(i, VelocityAmp, 0); set(i, Glide, 0);
    set(i, OscDrive, 1);
    set(i, AnalogOutput, 0); set(i, OscDrift, 0);
    set(i, BackgroundHiss, 0);
    set(i, AmpAttack, .001); set(i, AmpSustain, 1); set(i, AmpRelease, .05);
}
double pitch(const Stereo& s) { return zeroCrossingHz(s.left, at(.2), s.left.size()); }
void writeWav(const std::string& path, const std::vector<float>& audio) {
    FILE* f = std::fopen(path.c_str(), "wb"); CHECK(f != nullptr); if(!f) return;
    auto u16 = [&](uint16_t x) { std::fputc(x & 255, f); std::fputc(x >> 8, f); };
    auto u32 = [&](uint32_t x) { u16(x & 65535); u16(x >> 16); };
    std::fwrite("RIFF", 1, 4, f); u32(36 + audio.size() * 4); std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(2); u32(kRate); u32(kRate * 4); u16(4); u16(16);
    std::fwrite("data", 1, 4, f); u32(audio.size() * 4);
    for(float x : audio) { const auto v = static_cast<int16_t>(std::clamp(x, -1.f, 1.f) * 32767); u16(v); u16(v); }
    std::fclose(f);
}
} // namespace

int main(int argc, char** argv) {
    using namespace mla_mini;
    if(argc < 2) return 2;
    const std::string path = argv[1];
    {
        Instance i; if(!i.open(path)) return 1;
        CHECK(i.component->getBusCount(kAudio, kInput) == 0);
        CHECK(i.component->getBusCount(kAudio, kOutput) == 1);
        CHECK(i.processor->canProcessSampleSize(kSample64) == kResultFalse);
        set(i, BackgroundHiss, 0);
        auto silence = i.render(.1); CHECK(peak(silence.left) == 0); CHECK(i.lastSilenceFlags == 3);
        i.noteOn(36, 1, 100);
        auto s = i.render(.5); CHECK(peak(s.left, 0, 100) == 0); CHECK(rms(s.left) > .03);
        CHECK(s.left == s.right); CHECK(finite(s)); CHECK(peak(s.left) <= 1);
        i.noteOff(36); i.render(.8); CHECK(peak(i.render(.1).left) == 0);
    }
    // Hardware-style hiss continues without notes, with a calibrated quiet
    // level. Switching it off fades to exact zeros and a silent output flag.
    {
        Instance i; CHECK(i.open(path));
        ParameterInfo info{};
        CHECK(i.provider->getControllerPtr()->getParameterInfo(BackgroundHiss, info) == kResultOk);
        CHECK(info.stepCount == 10);
        auto initial = i.render(.5);
        const double initialDb = 20 * std::log10(rms(initial.left, at(.1)));
        CHECK(initialDb > -59.5 && initialDb < -57.5);
        set(i, BackgroundHiss, 10); set(i, HissLevel, -78);
        auto hiss = i.render(.8);
        const double db = 20 * std::log10(rms(hiss.left, at(.1)));
        CHECK(db > -79 && db < -77); CHECK(i.lastSilenceFlags == 0);
        CHECK(hiss.left == hiss.right); CHECK(finite(hiss));
        CHECK(i.processor->getTailSamples() == kInfiniteTail);
        set(i, HissLevel, -66); auto louder = i.render(.5);
        const double ratio = rms(louder.left, at(.1)) / rms(hiss.left, at(.1));
        CHECK(ratio > 3.7 && ratio < 4.3);
        double prior = 0;
        for(int amount = 1; amount <= 10; ++amount) {
            set(i, BackgroundHiss, amount); auto adjusted = i.render(.3);
            const double level = rms(adjusted.left, at(.1));
            const double expected = std::pow(10., -66. / 20) * amount / 10.;
            CHECK(std::abs(20 * std::log10(level / expected)) < .8);
            CHECK(level > prior); prior = level;
        }
        set(i, BackgroundHiss, 0); auto faded = i.render(.2);
        CHECK(peak(faded.left, at(.1)) == 0); CHECK(i.lastSilenceFlags == 3);
        CHECK(i.processor->getTailSamples() != kInfiniteTail);
        set(i, BackgroundHiss, 10); CHECK(rms(i.render(.2).left, at(.1)) > .0003);
        set(i, Output, -60); CHECK(peak(i.render(.4).left, at(.3)) == 0);
    }
    // Hiss level is comparable at common sample rates, not tied to 48 kHz.
    for(double rate : {44100., 96000., 192000.}) {
        Instance i; CHECK(i.open(path, rate)); set(i, BackgroundHiss, 10); set(i, HissLevel, -78);
        auto s = i.renderFrames(static_cast<int>(rate / kBlock) * kBlock);
        const double db = 20 * std::log10(rms(s.left, static_cast<size_t>(rate * .1)));
        CHECK(db > -79 && db < -77);
    }
    // Pitch and octave switches for every oscillator, independent mixer levels.
    for(int osc = 0; osc < 3; ++osc) for(int octave = -1; octave <= 1; ++octave) {
        Instance i; CHECK(i.open(path)); plainPatch(i);
        set(i, Level1, osc == 0); set(i, Level2, osc == 1); set(i, Level3, osc == 2);
        const int waveId[] = {Wave1, Wave2, Wave3}, rangeId[] = {Range1, Range2, Range3};
        set(i, waveId[osc], 0); set(i, rangeId[osc], octave); set(i, Fine2, 0); set(i, Fine3, 0);
        i.noteOn(45, 1); auto s = i.render(.7);
        const double expected = 110 * std::pow(2., octave);
        CHECK(std::abs(pitch(s) - expected) < expected * .005);
    }
    // Stereo movement comes from delayed copies of one voice. Automating
    // the amount back to zero restores sample-identical left and right.
    {
        Instance i; CHECK(i.open(path)); plainPatch(i); i.noteOn(45, 1);
        set(i, Chorus, 50); auto wet = i.render(.6);
        double side = 0;
        for(size_t f = at(.05); f < wet.left.size(); ++f)
            side += std::abs(wet.left[f] - wet.right[f]);
        CHECK(finite(wet)); CHECK(side / wet.left.size() > .005);
        set(i, Chorus, 0); i.render(.03); auto dry = i.render(.1);
        CHECK(dry.left == dry.right); CHECK(rms(dry.left) > .01);
    }
    // Six wave shapes should all sound; detune changes pitch in cents.
    for(int wave = 0; wave < 6; ++wave) {
        Instance i; CHECK(i.open(path)); plainPatch(i); set(i, Wave1, wave); i.noteOn(45, 1);
        CHECK(rms(i.render(.3).left) > .03);
    }
    {
        Instance i; CHECK(i.open(path)); plainPatch(i); set(i, Level1, 0); set(i, Level2, 1);
        set(i, Wave2, 0); set(i, Fine2, 25); i.noteOn(45, 1);
        CHECK(std::abs(pitch(i.render(.7)) - 110 * std::pow(2., 25./1200)) < .5);
    }
    // Filter cutoff/resonance/envelope have audible spectral effects.
    double brightness[2]{};
    for(int open = 0; open < 2; ++open) {
        Instance i; CHECK(i.open(path)); plainPatch(i); set(i, Wave1, 2); set(i, Cutoff, open ? 6000 : 150);
        i.noteOn(36, 1); auto s = i.render(.6); brightness[open] = highShare(s.left, at(.2), s.left.size(), 1000);
    }
    CHECK(brightness[1] > brightness[0] * 2);
    {
        Instance i; CHECK(i.open(path)); plainPatch(i); set(i, Wave1, 2); set(i, Cutoff, 100);
        set(i, EnvDepth, 6); set(i, FilterDecay, .7); set(i, FilterSustain, 0); i.noteOn(36, 1);
        auto s = i.render(.8);
        CHECK(highShare(s.left, at(.01), at(.06), 1000) > highShare(s.left, at(.5), at(.7), 1000) * 2);
    }
    // Last/low/high priority, fallback when the selected note is released.
    for(int priority = 0; priority < 3; ++priority) {
        Instance i; CHECK(i.open(path)); plainPatch(i); set(i, Priority, priority);
        i.noteOn(45, 1); i.noteOn(57, 1); auto s = i.render(.7);
        CHECK(std::abs(pitch(s) - (priority == 1 ? 110 : 220)) < 1);
        i.noteOff(57); CHECK(std::abs(pitch(i.render(.7)) - 110) < 1);
        i.noteOff(45); CHECK(peak(i.render(.3).left, at(.2)) == 0);
    }
    {
        Instance i; CHECK(i.open(path)); plainPatch(i); i.noteOn(45, 1, 0, 1); i.render(.1);
        i.noteOff(45, 0, 0); CHECK(rms(i.render(.2).left) > .03); // Other channel cannot release it.
        i.noteOff(45, 0, 1); CHECK(peak(i.render(.3).left, at(.2)) == 0);
        i.noteOn(45, 1); i.noteOn(45, 1); i.noteOff(45); CHECK(rms(i.render(.2).left) > .03);
        i.noteOn(45, 0); CHECK(peak(i.render(.3).left, at(.2)) == 0); // Velocity zero is note-off.
    }
    // Sustain, pitch bend, sample-accurate panic (including CC123 value zero).
    {
        Instance i; CHECK(i.open(path)); plainPatch(i); i.noteOn(45, 1); i.render(.1);
        i.cc(64, 127); i.noteOff(45); CHECK(rms(i.render(.2).left) > .03);
        i.cc(64, 0); CHECK(peak(i.render(.3).left, at(.2)) == 0);
        i.noteOn(45, 1); set(i, BendRange, 12); i.cc(kPitchBend, 16383);
        CHECK(std::abs(pitch(i.render(.7)) - 220) < 1);
        i.cc(123, 0, 113); auto s = i.renderFrames(kBlock);
        CHECK(rms(s.left, 0, 100) > .03); CHECK(peak(s.left, 113) == 0);
    }
    // Automation points must not mute earlier samples in a block.
    {
        Instance i; CHECK(i.open(path)); plainPatch(i); i.noteOn(45, 1); i.render(.1);
        set(i, Output, -60, 128); auto s = i.renderFrames(kBlock);
        CHECK(rms(s.left, 0, 100) > .03);
        CHECK(peak(i.render(.4).left, at(.3)) < 1e-8);
    }
    // Portamento takes time and legato preserves a decayed envelope.
    {
        Instance i; CHECK(i.open(path)); plainPatch(i); set(i, Glide, .5); i.noteOn(45, 1); i.render(.3);
        i.noteOn(57, 1); auto s = i.render(.7);
        const double early = zeroCrossingHz(s.left, at(.01), at(.06));
        CHECK(early > 110 && early < 210); CHECK(std::abs(pitch(s) - 220) < 2);
    }
    // State reload: physical controls survive, malformed states do not partially apply.
    double legatoLevel[2]{};
    for(int legato = 0; legato < 2; ++legato) {
        Instance i; CHECK(i.open(path)); plainPatch(i); set(i, Legato, legato);
        set(i, AmpDecay, .4); set(i, AmpSustain, .1);
        i.noteOn(45, 1); i.render(.8); i.noteOn(57, 1);
        legatoLevel[legato] = rms(i.render(.05).left);
    }
    CHECK(legatoLevel[0] > legatoLevel[1] * 3);
    // The revised release keeps the tail audible after the old patch has faded.
    double releaseLevel[2]{};
    for(int longTail = 0; longTail < 2; ++longTail) {
        Instance i; CHECK(i.open(path)); plainPatch(i); set(i, AmpRelease, longTail ? .48 : .12);
        i.noteOn(45, 1); i.render(.3); i.noteOff(45); auto s = i.render(.8);
        releaseLevel[longTail] = rms(s.left, at(.10), at(.12));
        CHECK(peak(s.left, at(.7)) == 0);
    }
    CHECK(releaseLevel[1] > releaseLevel[0] * 20);
    // Oscillator saturation adds upper harmonics before the mixer/filter.
    // Compare relative harmonic energy so a gain change cannot pass the test.
    double distortion[2]{};
    for(int driven = 0; driven < 2; ++driven) {
        Instance i; CHECK(i.open(path)); plainPatch(i); set(i, OscDrive, driven ? 4 : 1);
        i.noteOn(45, 1); auto s = i.render(.7);
        const double fundamental = tone(s.left, at(.2), at(.6), 110);
        double harmonics = 0;
        for(int h = 3; h <= 11; h += 2) {
            const double amplitude = tone(s.left, at(.2), at(.6), h * 110);
            harmonics += amplitude * amplitude;
        }
        distortion[driven] = harmonics / (fundamental * fundamental);
    }
    CHECK(distortion[1] > distortion[0] * 2);
    // Output coloration adds even harmonics to a symmetric triangle. Compare
    // harmonic/fundamental ratios so a loudness change cannot pass the test.
    double evenHarmonic[3]{};
    for(int mode = 0; mode < 3; ++mode) {
        Instance i; CHECK(i.open(path)); plainPatch(i);
        set(i, AnalogOutput, mode == 0 ? 0 : 1);
        if(mode == 2) set(i, Output, -12);
        i.noteOn(45, 1); auto s = i.render(.7);
        evenHarmonic[mode] = tone(s.left, at(.2), at(.6), 220) / tone(s.left, at(.2), at(.6), 110);
        double dc = 0;
        for(size_t f = at(.2); f < at(.6); ++f) dc += s.left[f];
        CHECK(std::abs(dc / at(.4)) < .002);
        CHECK(finite(s));
        i.noteOff(45); CHECK(peak(i.render(.3).left, at(.2)) == 0);
    }
    CHECK(evenHarmonic[1] > .002);
    CHECK(evenHarmonic[1] > evenHarmonic[0] * 10);
    CHECK(std::abs(evenHarmonic[1] - evenHarmonic[2]) < .001);
    // Each oscillator wanders gently within its cents limit. With Drift at
    // zero the same held pitch stays stable; random targets never jump phase.
    for(int oscillator = -1; oscillator < 3; ++oscillator) {
        Instance i; CHECK(i.open(path)); plainPatch(i);
        const int selected = std::max(0, oscillator);
        set(i, Level1, selected == 0); set(i, Level2, selected == 1); set(i, Level3, selected == 2);
        set(i, Wave2, 0); set(i, Wave3, 0); set(i, Range3, 0); set(i, Fine2, 0); set(i, Fine3, 0);
        set(i, OscDrift, oscillator < 0 ? 0 : 8); i.noteOn(45, 1); auto s = i.render(8);
        double lowest = 1e6, highest = 0;
        for(int window = 0; window < 16; ++window) {
            const auto from = at(.3 + window * .45), to = from + at(.3);
            const double hz = zeroCrossingHz(s.left, from, to);
            CHECK(hz > 110 * std::pow(2., -8.2 / 1200));
            CHECK(hz < 110 * std::pow(2., 8.2 / 1200));
            lowest = std::min(lowest, hz); highest = std::max(highest, hz);
        }
        const double span = 1200 * std::log2(highest / lowest);
        CHECK(oscillator < 0 ? span < .05 : span > .5);
        for(size_t f = at(.2) + 1; f < s.left.size(); ++f)
            CHECK(std::abs(s.left[f] - s.left[f - 1]) < .03);
    }
    // Repeated detached notes around sixteenth-note speed do not restart
    // oscillator phase. The same key/velocity has a different waveform on
    // the next gate, even after the preceding amp envelope reached silence.
    {
        Instance i; CHECK(i.open(path)); plainPatch(i); set(i, AmpRelease, .005);
        i.noteOn(41, 1); auto first = i.renderFrames(4096);
        i.noteOff(41); auto gap = i.renderFrames(2048);
        CHECK(peak(gap.left, 1024) == 0);
        i.noteOn(41, 1); auto second = i.renderFrames(4096);
        double difference = 0, energy = 0;
        for(size_t n = at(.04); n < at(.075); ++n) {
            double delta = first.left[n] - second.left[n];
            difference += delta * delta; energy += first.left[n] * first.left[n];
        }
        CHECK(difference > energy * .25);
    }
    // The revised bass restores upper harmonics compared with the dark patch.
    double patchBrightness[2]{};
    for(int old = 0; old < 2; ++old) {
        Instance i; CHECK(i.open(path));
        if(old) { set(i, Cutoff, 130); set(i, EnvDepth, 1.8); set(i, KeyFollow, .2); }
        i.noteOn(41, 1); auto s = i.render(.3);
        patchBrightness[old] = highShare(s.left, at(.01), at(.12), 600);
    }
    CHECK(patchBrightness[0] > patchBrightness[1] * 2);
    // Simultaneous keys still produce one voice: only the selected key's
    // fundamental remains. Parallel musical lines need separate instances.
    {
        Instance i; CHECK(i.open(path)); plainPatch(i);
        i.noteOn(45, 1); i.noteOn(57, 1); auto s = i.render(.6);
        const double selected = tone(s.left, at(.2), at(.5), 220);
        const double other = tone(s.left, at(.2), at(.5), 110);
        CHECK(selected > .03); CHECK(other < selected * .01);
    }
    // Sample rate changes preserve pitch and produce finite audio.
    for(double rate : {44100., 96000., 192000.}) {
        Instance i; CHECK(i.open(path, rate)); plainPatch(i); i.noteOn(45, 1);
        auto s = i.renderFrames(static_cast<int>(rate / kBlock) * kBlock);
        CHECK(finite(s)); CHECK(std::abs(pitch(s) * rate / kRate - 110) < 1);
    }
    {
        Instance a, b; CHECK(a.open(path)); CHECK(b.open(path));
        set(a, Chorus, 42); set(a, Cutoff, 720); set(a, Wave2, 4); set(a, BackgroundHiss, 0); set(a, HissLevel, -70); a.render(.01);
        MemoryStream state; CHECK(a.component->getState(&state) == kResultOk);
        state.seek(0, IBStream::kIBSeekSet, nullptr); CHECK(b.component->setState(&state) == kResultOk);
        MemoryStream again; CHECK(b.component->getState(&again) == kResultOk);
        CHECK(state.getSize() == again.getSize()); CHECK(std::memcmp(state.getData(), again.getData(), state.getSize()) == 0);
        CHECK(b.provider->getControllerPtr()->getParamNormalized(144) == 0);
        CHECK(std::abs(b.provider->getControllerPtr()->getParamNormalized(146) - .42) < 1e-6);
        CHECK(std::abs(physical(HissLevel, b.provider->getControllerPtr()->getParamNormalized(145)) + 70) < .001);
        MemoryStream bad; IBStreamer s(&bad, kLittleEndian); s.writeInt32u(0x494E494D); s.writeInt32u(1); s.writeInt32(1); s.writeInt32(113);
        s.writeDouble(NAN); bad.seek(0, IBStream::kIBSeekSet, nullptr);
        CHECK(b.component->setState(&bad) != kResultOk);
        a.noteOn(36, 1); b.noteOn(36, 1); auto x = a.render(.4), y = b.render(.4);
        CHECK(std::abs(rms(x.left) - rms(y.left)) < .03);
    }
    // Extreme drive/feedback/resonance stays finite at low and high notes.
    {
        Instance i; CHECK(i.open(path));
        MemoryStream legacy; IBStreamer s(&legacy, kLittleEndian);
        s.writeInt32u(0x494E494D); s.writeInt32u(1); s.writeInt32(41);
        for(int p = 0; p < 41; ++p) { s.writeInt32(100 + p); s.writeDouble(normalized(p, specs[p].initial)); }
        legacy.seek(0, IBStream::kIBSeekSet, nullptr);
        CHECK(i.component->setState(&legacy) == kResultOk);
        CHECK(i.provider->getControllerPtr()->getParamNormalized(141) == 0);
        CHECK(i.provider->getControllerPtr()->getParamNormalized(142) == 0);
        CHECK(i.provider->getControllerPtr()->getParamNormalized(143) == 0);
        CHECK(i.provider->getControllerPtr()->getParamNormalized(144) == 0);
        MemoryStream previous; IBStreamer previousWriter(&previous, kLittleEndian);
        previousWriter.writeInt32u(0x494E494D); previousWriter.writeInt32u(1); previousWriter.writeInt32(42);
        for(int p = 0; p < 42; ++p) { previousWriter.writeInt32(100 + p); previousWriter.writeDouble(normalized(p, specs[p].initial)); }
        previous.seek(0, IBStream::kIBSeekSet, nullptr);
        CHECK(i.component->setState(&previous) == kResultOk);
        CHECK(i.provider->getControllerPtr()->getParamNormalized(142) == 0);
        CHECK(i.provider->getControllerPtr()->getParamNormalized(143) == 0);
        CHECK(i.provider->getControllerPtr()->getParamNormalized(144) == 0);
        MemoryStream withAnalog; IBStreamer analogWriter(&withAnalog, kLittleEndian);
        analogWriter.writeInt32u(0x494E494D); analogWriter.writeInt32u(1); analogWriter.writeInt32(44);
        for(int p = 0; p < 44; ++p) { analogWriter.writeInt32(100 + p); analogWriter.writeDouble(normalized(p, specs[p].initial)); }
        withAnalog.seek(0, IBStream::kIBSeekSet, nullptr);
        CHECK(i.component->setState(&withAnalog) == kResultOk);
        CHECK(i.provider->getControllerPtr()->getParamNormalized(144) == 0);
        // A saved On from the old binary switch is normalized 1. It now
        // means amount 10, retaining its saved -78 dB noise floor exactly.
        MemoryStream oldToggle; IBStreamer toggleWriter(&oldToggle, kLittleEndian);
        toggleWriter.writeInt32u(0x494E494D); toggleWriter.writeInt32u(1); toggleWriter.writeInt32(46);
        for(int p = 0; p < 46; ++p) {
            double value = normalized(p, specs[p].initial);
            if(p == BackgroundHiss) value = 1;
            if(p == HissLevel) value = normalized(HissLevel, -78);
            toggleWriter.writeInt32(100 + p); toggleWriter.writeDouble(value);
        }
        oldToggle.seek(0, IBStream::kIBSeekSet, nullptr);
        CHECK(i.component->setState(&oldToggle) == kResultOk);
        CHECK(physical(BackgroundHiss, i.provider->getControllerPtr()->getParamNormalized(144)) == 10);
        auto restoredHiss = i.render(.8);
        const double restoredDb = 20 * std::log10(rms(restoredHiss.left, at(.1)));
        CHECK(restoredDb > -79 && restoredDb < -77);
    }
    for(int note : {0, 36, 96, 127}) {
        Instance i; CHECK(i.open(path)); set(i, Drive, 8); set(i, Feedback, 1); set(i, Resonance, 1); set(i, Cutoff, 20000);
        set(i, BackgroundHiss, 0);
        set(i, Noise, 1); set(i, Output, 0); set(i, OscDrive, 4);
        set(i, AnalogOutput, 1); set(i, OscDrift, 8); i.noteOn(note, 1); auto s = i.render(.5);
        CHECK(finite(s)); CHECK(peak(s.left) <= 1.001);
        i.noteOff(note); CHECK(peak(i.render(.9).left, at(.8)) == 0);
    }
    if(argc > 2) {
        Instance i; CHECK(i.open(path)); std::vector<float> song;
        const int notes[] = {41, 41, 48, 41, 44, 41, 39, 36, 41, 53, 48, 44, 39, 36, 39, 40};
        for(int repeat = 0; repeat < 4; ++repeat) for(int n = 0; n < 16; ++n) {
            i.noteOn(notes[n], n % 4 == 0 ? 1.f : .8f); auto a = i.render(.13);
            i.noteOff(notes[n]); auto b = i.render(.12);
            song.insert(song.end(), a.left.begin(), a.left.end()); song.insert(song.end(), b.left.begin(), b.left.end());
        }
        writeWav(argv[2], song);
    }
    std::printf("Mla Mini: %d failures\n", failures); return failures ? 1 : 0;
}

