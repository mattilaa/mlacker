// VST3 transport, MIDI note stack and state. All sound synthesis is MLang.
#include "parameters.h"
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "base/source/fstreamer.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>

struct Mini;
extern "C" Mini* mlamini_create__f32(float);
extern "C" void mlamini_destroy__ptr_struct_Mini(Mini*);
extern "C" void mlamini_reset__ptr_struct_Mini(Mini*);
extern "C" void mlamini_set__ptr_struct_Mini_i32_f32(Mini*, int32_t, float);
extern "C" void mlamini_note__ptr_struct_Mini_f32_f32_i32_i32(Mini*, float, float, int32_t, int32_t);
extern "C" void mlamini_off__ptr_struct_Mini(Mini*);
extern "C" int32_t mlamini_active__ptr_struct_Mini(Mini*);
extern "C" float mlamini_process__ptr_struct_Mini(Mini*);

using namespace Steinberg;
using namespace Steinberg::Vst;
namespace mla_mini {
static const FUID kProcessorUID(0x4D6C614D, 0x696E6950, 0xB23F6419, 0x8D9170A5);
constexpr uint32 kMagic = 0x494E494D, kVersion = 1;

class MappedParameter final : public RangeParameter {
public:
    explicit MappedParameter(int i) : RangeParameter(STR16(""), 100 + i, STR16(""),
        specs[i].lo, specs[i].hi, specs[i].initial, specs[i].steps, ParameterInfo::kCanAutomate), index_(i) {
        UString(info.title, 128).fromAscii(specs[i].name);
        UString(info.units, 128).fromAscii(specs[i].unit);
        info.defaultNormalizedValue = valueNormalized = normalized(i, specs[i].initial);
    }
    ParamValue toPlain(ParamValue n) const override { return physical(index_, n); }
    ParamValue toNormalized(ParamValue p) const override {
        return normalized(index_, std::clamp(p, specs[index_].lo, specs[index_].hi));
    }
private:
    int index_;
};

class Processor final : public SingleComponentEffect, public IMidiMapping {
public:
    Processor() { for(int i = 0; i < Count; ++i) norm_[i].store(normalized(i, specs[i].initial)); }
    ~Processor() override { destroy(); }
    DEFINE_INTERFACES
        DEF_INTERFACE(IMidiMapping)
    END_DEFINE_INTERFACES(SingleComponentEffect)
    REFCOUNT_METHODS(SingleComponentEffect)
    static FUnknown* createInstance(void*) { return static_cast<IComponent*>(new Processor); }
    tresult PLUGIN_API initialize(FUnknown* context) override {
        auto result = SingleComponentEffect::initialize(context);
        if(result != kResultOk) return result;
        addEventInput(STR16("MIDI In"), 16);
        addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
        for(int i = 0; i < Count; ++i) {
            if(i == Wave1 || i == Wave2 || i == Wave3 || i == Priority) {
                String128 title{}; UString(title, 128).fromAscii(specs[i].name);
                auto* p = new StringListParameter(title, 100 + i);
                if(i == Priority) {
                    for(auto* text : {STR16("Last"), STR16("Low"), STR16("High")}) p->appendString(text);
                } else {
                    for(auto* text : {STR16("Triangle"), STR16("Triangle-Saw"), STR16("Saw"), STR16("Square"), STR16("Pulse 30%"), STR16("Pulse 15%")}) p->appendString(text);
                }
                const double n = norm_[i].load(); p->getInfo().defaultNormalizedValue = n; p->setNormalized(n);
                parameters.addParameter(p);
            } else parameters.addParameter(new MappedParameter(i));
        }
        return kResultOk;
    }
    tresult PLUGIN_API terminate() override { destroy(); return SingleComponentEffect::terminate(); }
    tresult PLUGIN_API canProcessSampleSize(int32 size) override { return size == kSample32 ? kResultTrue : kResultFalse; }
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement*, int32 ni, SpeakerArrangement* outputs, int32 no) override {
        return ni == 0 && no == 1 && outputs && outputs[0] == SpeakerArr::kStereo ? kResultOk : kResultFalse;
    }
    tresult PLUGIN_API setupProcessing(ProcessSetup& setup) override {
        if(!std::isfinite(setup.sampleRate) || setup.sampleRate < 8000 || setup.sampleRate > 384000 || setup.symbolicSampleSize != kSample32) return kResultFalse;
        auto result = SingleComponentEffect::setupProcessing(setup);
        if(result != kResultOk) return result;
        destroy(); rate_ = setup.sampleRate;
        dsp_ = mlamini_create__f32(static_cast<float>(rate_));
        if(!dsp_) return kOutOfMemory;
        clearNotes(); pushAll(); return kResultOk;
    }
    tresult PLUGIN_API setActive(TBool state) override {
        clearNotes(); if(dsp_) mlamini_reset__ptr_struct_Mini(dsp_);
        return SingleComponentEffect::setActive(state);
    }
    uint32 PLUGIN_API getTailSamples() override { return static_cast<uint32>(rate_ * 14); }
    tresult PLUGIN_API getMidiControllerAssignment(int32 bus, int16 channel, CtrlNumber cc, ParamID& id) override {
        if(bus != 0 || channel < 0 || channel > 15) return kResultFalse;
        int i;
        switch(cc) {
            case 1: i = Wheel; break; case 5: i = Glide; break; case 7: i = Output; break;
            case 64: i = Sustain; break; case 71: i = Resonance; break; case 74: i = Cutoff; break;
            case 73: i = AmpAttack; break; case 72: i = AmpRelease; break;
            case 120: case 123: i = Panic; break;
            case kPitchBend: i = Bend; break; default: return kResultFalse;
        }
        id = 100 + i; return kResultOk;
    }
    tresult PLUGIN_API process(ProcessData& data) override {
        if(dirty_.exchange(false, std::memory_order_acquire)) {
            clearNotes(); if(dsp_) mlamini_reset__ptr_struct_Mini(dsp_); pushAll();
        }
        // Fixed queue cursors: no allocation, and parameter points and notes
        // both take effect at their sample offsets. Flushes have no audio bus.
        struct Cursor { IParamValueQueue* q = nullptr; int32 index = 0, at = 0; double value = 0; bool valid = false; };
        std::array<Cursor, Count> cursors{};
        int count = 0;
        if(data.inputParameterChanges) {
            for(int q = 0; q < data.inputParameterChanges->getParameterCount() && count < Count; ++q) {
                auto* queue = data.inputParameterChanges->getParameterData(q);
                if(!queue || queue->getPointCount() == 0 || queue->getParameterId() < 100 || queue->getParameterId() >= 100 + Count) continue;
                auto& c = cursors[count++]; c.q = queue;
                c.valid = queue->getPoint(0, c.at, c.value) == kResultOk;
            }
        }
        if(data.numSamples <= 0) {
            for(int q = 0; q < count; ++q) {
                auto& c = cursors[q];
                if(c.q->getPoint(c.q->getPointCount() - 1, c.at, c.value) == kResultOk) apply(c.q->getParameterId(), c.value);
            }
            return kResultOk;
        }
        if(data.symbolicSampleSize != kSample32) return kResultFalse;
        if(!dsp_ || data.numOutputs < 1 || !data.outputs) return kResultOk;
        auto& out = data.outputs[0];
        if(out.numChannels != 2 || !out.channelBuffers32 || !out.channelBuffers32[0] || !out.channelBuffers32[1]) return kResultFalse;
        const int ne = data.inputEvents ? data.inputEvents->getEventCount() : 0;
        int ei = 0; Event event{};
        bool have = ne > 0 && data.inputEvents->getEvent(0, event) == kResultOk;
        bool silent = true;
        for(int frame = 0; frame < data.numSamples; ++frame) {
            for(int q = 0; q < count; ++q) {
                auto& c = cursors[q];
                while(c.valid && c.at <= frame) {
                    apply(c.q->getParameterId(), c.value);
                    c.valid = ++c.index < c.q->getPointCount() && c.q->getPoint(c.index, c.at, c.value) == kResultOk;
                }
            }
            while(have && event.sampleOffset <= frame) {
                handle(event);
                have = ++ei < ne && data.inputEvents->getEvent(ei, event) == kResultOk;
            }
            const float sample = mlamini_process__ptr_struct_Mini(dsp_);
            out.channelBuffers32[0][frame] = out.channelBuffers32[1][frame] = sample;
            silent = silent && sample == 0;
        }
        out.silenceFlags = silent ? 3 : 0;
        return kResultOk;
    }
    tresult PLUGIN_API getState(IBStream* state) override {
        if(!state) return kResultFalse;
        IBStreamer s(state, kLittleEndian);
        if(!s.writeInt32u(kMagic) || !s.writeInt32u(kVersion) || !s.writeInt32(Count)) return kResultFalse;
        for(int i = 0; i < Count; ++i) {
            const double n = i == Sustain || i == Panic || i == Bend || i == Wheel ? normalized(i, specs[i].initial) : norm_[i].load();
            if(!s.writeInt32(100 + i) || !s.writeDouble(n)) return kResultFalse;
        }
        return kResultOk;
    }
    tresult PLUGIN_API setState(IBStream* state) override {
        if(!state) return kResultFalse;
        IBStreamer s(state, kLittleEndian); uint32 magic, version; int32 count;
        if(!s.readInt32u(magic) || magic != kMagic || !s.readInt32u(version) || version != kVersion || !s.readInt32(count) || count < 0 || count > 4096) return kResultFalse;
        std::array<double, Count> values{};
        for(int i = 0; i < Count; ++i) values[i] = normalized(i, specs[i].initial);
        // States predating OSC Drive retain their original clean oscillators.
        values[OscDrive] = 0;
        for(int n = 0; n < count; ++n) {
            int32 id; double value;
            if(!s.readInt32(id) || !s.readDouble(value) || !std::isfinite(value) || value < 0 || value > 1) return kResultFalse;
            if(id >= 100 && id < 100 + Count) values[id - 100] = value;
        }
        for(int i = 0; i < Count; ++i) {
            if(i == Sustain || i == Panic || i == Bend || i == Wheel) values[i] = normalized(i, specs[i].initial);
            norm_[i].store(values[i]); setParamNormalized(100 + i, values[i]);
        }
        dirty_.store(true, std::memory_order_release); return kResultOk;
    }
private:
    struct Note { bool used = false, down = false; int pitch = 0, channel = 0, id = -1; float velocity = 1; uint64_t serial = 0; };
    std::array<Note, 256> notes_{};
    std::array<std::atomic<double>, Count> norm_{};
    std::atomic<bool> dirty_{false};
    Mini* dsp_ = nullptr; double rate_ = 44100; int current_ = -1; uint64_t serial_ = 0;
    double plain(int i) const { return physical(i, norm_[i].load(std::memory_order_relaxed)); }
    void destroy() { if(dsp_) mlamini_destroy__ptr_struct_Mini(dsp_); dsp_ = nullptr; }
    void clearNotes() { for(auto& n : notes_) n = Note{}; current_ = -1; serial_ = 0; }
    void pushAll() { if(dsp_) for(int i = 0; i < Count; ++i) mlamini_set__ptr_struct_Mini_i32_f32(dsp_, i, static_cast<float>(plain(i))); }
    void apply(ParamID id, double value) {
        if(id < 100 || id >= 100 + Count || !std::isfinite(value)) return;
        int i = id - 100; norm_[i].store(std::clamp(value, 0.0, 1.0), std::memory_order_relaxed);
        if(!dsp_) return;
        mlamini_set__ptr_struct_Mini_i32_f32(dsp_, i, static_cast<float>(plain(i)));
        if(i == Panic) { clearNotes(); mlamini_reset__ptr_struct_Mini(dsp_); }
        if(i == Sustain && plain(Sustain) == 0) {
            for(auto& n : notes_) if(!n.down) n.used = false;
            select(false);
        }
        if(i == Priority) select(false);
    }
    void select(bool newNote) {
        int best = -1; const int priority = static_cast<int>(plain(Priority));
        for(int i = 0; i < static_cast<int>(notes_.size()); ++i) if(notes_[i].used) {
            if(best < 0 || (priority == 0 ? notes_[i].serial > notes_[best].serial :
                (priority == 1 ? notes_[i].pitch < notes_[best].pitch : notes_[i].pitch > notes_[best].pitch))) best = i;
        }
        if(best == current_) return;
        if(best < 0) { if(current_ >= 0) mlamini_off__ptr_struct_Mini(dsp_); current_ = -1; return; }
        const bool first = current_ < 0;
        const auto& n = notes_[best];
        mlamini_note__ptr_struct_Mini_f32_f32_i32_i32(dsp_, static_cast<float>(n.pitch), n.velocity,
            first || (newNote && plain(Legato) == 0), first);
        current_ = best;
    }
    void handle(const Event& e) {
        if(e.type == Event::kNoteOnEvent && e.noteOn.velocity > 0 && std::isfinite(e.noteOn.velocity)) {
            if(e.noteOn.pitch < 0 || e.noteOn.pitch > 127 || e.noteOn.channel < 0 || e.noteOn.channel > 15) return;
            int slot = -1;
            for(int i = 0; i < static_cast<int>(notes_.size()); ++i) if(!notes_[i].used) { slot = i; break; }
            if(slot < 0) { slot = 0; for(int i = 1; i < static_cast<int>(notes_.size()); ++i) if(notes_[i].serial < notes_[slot].serial) slot = i; }
            notes_[slot] = {true, true, e.noteOn.pitch, e.noteOn.channel, e.noteOn.noteId, std::clamp(e.noteOn.velocity, 0.f, 1.f), ++serial_};
            select(true);
        } else if(e.type == Event::kNoteOffEvent || (e.type == Event::kNoteOnEvent && e.noteOn.velocity <= 0)) {
            const bool off = e.type == Event::kNoteOffEvent;
            const int pitch = off ? e.noteOff.pitch : e.noteOn.pitch;
            const int channel = off ? e.noteOff.channel : e.noteOn.channel;
            const int id = off ? e.noteOff.noteId : e.noteOn.noteId;
            // Oldest matching key wins for hosts that do not assign note IDs.
            int found = -1;
            for(int i = 0; i < static_cast<int>(notes_.size()); ++i) {
                const auto& n = notes_[i];
                if(n.used && n.down && n.channel == channel && (id >= 0 ? n.id == id : n.pitch == pitch) &&
                    (found < 0 || n.serial < notes_[found].serial)) found = i;
            }
            if(found >= 0) { notes_[found].down = false; if(plain(Sustain) == 0) notes_[found].used = false; select(false); }
        }
    }
};
} // namespace mla_mini
BEGIN_FACTORY_DEF("MLang", "https://github.com/mattilaa/mlang", "mailto:devnull@example.invalid")
DEF_CLASS2(INLINE_UID_FROM_FUID(mla_mini::kProcessorUID), PClassInfo::kManyInstances,
    kVstAudioEffectClass, "Mla Mini", 0, PlugType::kInstrumentSynth, "0.1.0", kVstVersionString,
    mla_mini::Processor::createInstance)
END_FACTORY
