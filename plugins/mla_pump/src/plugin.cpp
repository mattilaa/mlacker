// VST3 plumbing for the MLang tempo-locked sidechain pump; no DSP is duplicated here.
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "base/source/fstreamer.h"
#include <algorithm>
#include <array>
#include <cmath>

struct Pump;
extern "C" Pump* mlapump_create__f32(float);
extern "C" void mlapump_destroy__ptr_struct_Pump(Pump*);
extern "C" void mlapump_reset__ptr_struct_Pump(Pump*);
extern "C" void mlapump_set__ptr_struct_Pump_i32_f64(Pump*, int, double);
extern "C" void mlapump_sync__ptr_struct_Pump_f64_f64_i32(Pump*, double, double, int);
extern "C" float mlapump_gain__ptr_struct_Pump(Pump*);
extern "C" float mlapump_process__ptr_struct_Pump_f32_f32_ptr_f32(Pump*, float, float, float*);
using namespace Steinberg;
using namespace Steinberg::Vst;
namespace mla_pump {
static const FUID kProcessorUID(0x4D6C6150, 0x756D7031, 0xB81F4E2A, 0x6C93D205);
// Stable, contiguous IDs are saved by mlacker sessions and presets; append new ones.
enum Index {
    kDepth = 0,
    kRate = 1,
    kAttack = 2,
    kHold = 3,
    kRelease = 4,
    kCurve = 5,
    kTempo = 6,
    kBpm = 7,
    kBypass = 8,
    kCount
};
constexpr ParamID kFirstParam = 100;
enum TempoSource { kHost, kManual };
// Rate list: how often the pump dips, as quarter-note beats.
constexpr int kRateCount = 6;
static const TChar* const kRateNames[kRateCount] = {
    STR16("1/1"), STR16("1/2"), STR16("1/4"), STR16("1/8"), STR16("1/16"), STR16("1/4.")};
static const double kRateBeats[kRateCount] = {4, 2, 1, 0.5, 0.25, 1.5};
// Stepped values are whole numbers from low to high, as VST3 RangeParameter
// and StringListParameter index them.
struct Spec { const TChar* title; const TChar* unit; double low, high, initial; int steps; };
static const Spec specs[kCount] = {
    {STR16("Depth"), STR16(""), 0, 1, 0.7, 0},
    {STR16("Rate"), STR16(""), 0, kRateCount - 1, 2, kRateCount - 1},
    {STR16("Attack"), STR16("ms"), 0, 20, 1, 0},
    {STR16("Hold"), STR16(""), 0, 0.5, 0.05, 0},
    {STR16("Release"), STR16(""), 0.05, 1, 0.55, 0},
    {STR16("Curve"), STR16(""), 0, 1, 0.4, 0},
    {STR16("Tempo Source"), STR16(""), 0, 1, kHost, 1},
    {STR16("BPM"), STR16("BPM"), 20, 400, 120, 0},
    {STR16("Bypass"), STR16(""), 0, 1, 0, 1},
};
static double normalized(int i, double plain) { return (plain - specs[i].low) / (specs[i].high - specs[i].low); }
static double physical(int i, double norm) {
    const double value = specs[i].low + norm * (specs[i].high - specs[i].low);
    return specs[i].steps ? std::round(value) : value;
}
class Processor final : public SingleComponentEffect, public IMidiMapping {
public:
    Processor() : norm_(defaults()) {}
    static std::array<double, kCount> defaults() {
        std::array<double, kCount> values{};
        for(int i = 0; i < kCount; ++i) values[i] = normalized(i, specs[i].initial);
        return values;
    }
    ~Processor() override { destroy(); }
    DEFINE_INTERFACES
        DEF_INTERFACE(IMidiMapping)
    END_DEFINE_INTERFACES(SingleComponentEffect)
    REFCOUNT_METHODS(SingleComponentEffect)
    static FUnknown* createInstance(void*) { return static_cast<IComponent*>(new Processor()); }
    tresult PLUGIN_API initialize(FUnknown* context) override {
        const auto result = SingleComponentEffect::initialize(context);
        if(result != kResultOk) return result;
        addAudioInput(STR16("Stereo In"), SpeakerArr::kStereo);
        addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
        for(int i = 0; i < kCount; ++i) {
            if(i == kRate || i == kTempo) {
                auto* parameter = new StringListParameter(specs[i].title, kFirstParam + i);
                if(i == kRate) for(const TChar* name : kRateNames) parameter->appendString(name);
                if(i == kTempo) { parameter->appendString(STR16("Host")); parameter->appendString(STR16("Manual")); }
                parameter->getInfo().defaultNormalizedValue = norm_[i];
                parameter->setNormalized(norm_[i]); parameters.addParameter(parameter);
            } else {
                int32 flags = ParameterInfo::kCanAutomate;
                if(i == kBypass) flags |= ParameterInfo::kIsBypass;
                parameters.addParameter(new RangeParameter(specs[i].title, kFirstParam + i, specs[i].unit,
                    specs[i].low, specs[i].high, specs[i].initial, specs[i].steps, flags));
            }
        }
        return kResultOk;
    }
    tresult PLUGIN_API terminate() override { destroy(); return SingleComponentEffect::terminate(); }
    // The mod wheel sets Depth.
    tresult PLUGIN_API getMidiControllerAssignment(int32 bus, int16 channel, CtrlNumber cc, ParamID& id) override {
        if(bus != 0 || channel < 0 || channel > 15 || cc != 1) return kResultFalse;
        id = kFirstParam + kDepth; return kResultOk;
    }
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement* inputs, int32 ni, SpeakerArrangement* outputs, int32 no) override {
        if(ni != 1 || no != 1 || !inputs || !outputs || inputs[0] != SpeakerArr::kStereo || outputs[0] != SpeakerArr::kStereo) return kResultFalse;
        return SingleComponentEffect::setBusArrangements(inputs, ni, outputs, no);
    }
    tresult PLUGIN_API canProcessSampleSize(int32 size) override { return size == kSample32 ? kResultTrue : kResultFalse; }
    tresult PLUGIN_API setupProcessing(ProcessSetup& setup) override {
        if(!std::isfinite(setup.sampleRate) || setup.sampleRate < 8000 || setup.sampleRate > 384000 || setup.symbolicSampleSize != kSample32) return kResultFalse;
        const auto result = SingleComponentEffect::setupProcessing(setup);
        if(result != kResultOk) return result;
        destroy();
        dsp_ = mlapump_create__f32(static_cast<float>(setup.sampleRate));
        if(!dsp_) return kOutOfMemory;
        applyAll(); return kResultOk;
    }
    tresult PLUGIN_API setActive(TBool state) override {
        if(!state && dsp_) mlapump_reset__ptr_struct_Pump(dsp_);
        return SingleComponentEffect::setActive(state);
    }
    uint32 PLUGIN_API getTailSamples() override { return 0; }
    tresult PLUGIN_API process(ProcessData& data) override {
        // Match sibling plugins: consume the final value of each block queue.
        if(data.inputParameterChanges) {
            for(int32 q = 0; q < data.inputParameterChanges->getParameterCount(); ++q) {
                auto* queue = data.inputParameterChanges->getParameterData(q);
                if(!queue || queue->getPointCount() <= 0) continue;
                int32 offset = 0; ParamValue value = 0;
                if(queue->getPoint(queue->getPointCount() - 1, offset, value) == kResultOk)
                    applyOne(queue->getParameterId(), value);
            }
        }
        if(data.numSamples <= 0) return kResultOk; // Parameter-only flush.
        if(data.symbolicSampleSize != kSample32) return kResultFalse;
        if(data.numInputs < 1 || data.numOutputs < 1 || !data.inputs || !data.outputs) return kResultOk;
        auto& in = data.inputs[0]; auto& out = data.outputs[0];
        if(in.numChannels != 2 || out.numChannels != 2 || !in.channelBuffers32 || !out.channelBuffers32) return kResultFalse;
        for(int c = 0; c < 2; ++c) if(!in.channelBuffers32[c] || !out.channelBuffers32[c]) return kResultFalse;
        syncTransport(data.processContext);
        bool silent = true;
        const bool bypass = plain(kBypass) != 0;
        for(int32 i = 0; i < data.numSamples; ++i) {
            const float left = (in.silenceFlags & 1) ? 0.f : in.channelBuffers32[0][i];
            const float right = (in.silenceFlags & 2) ? 0.f : in.channelBuffers32[1][i];
            // The grid keeps running under bypass, so switching back lands on the beat.
            float wetRight = right, wetLeft = left;
            if(dsp_) wetLeft = mlapump_process__ptr_struct_Pump_f32_f32_ptr_f32(dsp_, left, right, &wetRight);
            out.channelBuffers32[0][i] = bypass ? left : wetLeft;
            out.channelBuffers32[1][i] = bypass ? right : wetRight;
            if(out.channelBuffers32[0][i] != 0 || out.channelBuffers32[1][i] != 0) silent = false;
        }
        out.silenceFlags = silent ? 3 : 0;
        return kResultOk;
    }
    tresult PLUGIN_API setState(IBStream* state) override {
        if(!state) return kResultFalse;
        IBStreamer stream(state, kLittleEndian);
        std::array<double, kCount> values = defaults();
        for(int i = 0; i < kCount; ++i) {
            double value = 0;
            if(!stream.readDouble(value)) return kResultFalse;
            if(!std::isfinite(value) || value < 0 || value > 1) return kResultFalse;
            values[i] = value;
        }
        norm_ = values;
        for(int i = 0; i < kCount; ++i) setParamNormalized(kFirstParam + i, norm_[i]);
        applyAll(); return kResultOk;
    }
    tresult PLUGIN_API getState(IBStream* state) override {
        if(!state) return kResultFalse;
        IBStreamer stream(state, kLittleEndian);
        for(double value : norm_) if(!stream.writeDouble(value)) return kResultFalse;
        return kResultOk;
    }
    float gain() const { return dsp_ ? mlapump_gain__ptr_struct_Pump(dsp_) : 1.f; }
private:
    Pump* dsp_ = nullptr;
    std::array<double, kCount> norm_{};
    double plain(int i) const { return physical(i, norm_[i]); }
    void destroy() { if(dsp_) { mlapump_destroy__ptr_struct_Pump(dsp_); dsp_ = nullptr; } }
    // Host: follow the host tempo, and its beat position while it plays, so
    // the dips land on the sequencer's kicks. Manual (or no host tempo): the
    // BPM knob and a free-running grid.
    void syncTransport(const ProcessContext* context) {
        if(!dsp_) return;
        const bool host = plain(kTempo) == kHost && context;
        const bool tempoValid = host && (context->state & ProcessContext::kTempoValid) && std::isfinite(context->tempo) && context->tempo > 0;
        const bool follow = tempoValid && (context->state & ProcessContext::kProjectTimeMusicValid) &&
            (context->state & ProcessContext::kPlaying) && std::isfinite(context->projectTimeMusic);
        mlapump_sync__ptr_struct_Pump_f64_f64_i32(dsp_, tempoValid ? context->tempo : plain(kBpm),
            follow ? context->projectTimeMusic : 0, follow ? 1 : 0);
    }
    void pushControl(int i) {
        if(!dsp_) return;
        switch(i) {
            case kDepth: mlapump_set__ptr_struct_Pump_i32_f64(dsp_, 0, plain(kDepth)); break;
            case kRate: mlapump_set__ptr_struct_Pump_i32_f64(dsp_, 1, kRateBeats[static_cast<int>(plain(kRate))]); break;
            case kAttack: mlapump_set__ptr_struct_Pump_i32_f64(dsp_, 2, plain(kAttack)); break;
            case kHold: mlapump_set__ptr_struct_Pump_i32_f64(dsp_, 3, plain(kHold)); break;
            case kRelease: mlapump_set__ptr_struct_Pump_i32_f64(dsp_, 4, plain(kRelease)); break;
            case kCurve: mlapump_set__ptr_struct_Pump_i32_f64(dsp_, 5, plain(kCurve)); break;
        }
    }
    void applyAll() { for(int i = 0; i < kCount; ++i) pushControl(i); }
    void applyOne(ParamID id, double value) {
        if(id < kFirstParam || id >= kFirstParam + kCount || !std::isfinite(value)) return;
        const int i = static_cast<int>(id - kFirstParam);
        norm_[i] = std::clamp(value, 0.0, 1.0);
        pushControl(i);
    }
};
} // namespace mla_pump
#ifndef MLA_PUMP_TEST
BEGIN_FACTORY_DEF("MLang", "https://github.com/mattilaa/mlang", "mailto:devnull@example.invalid")
DEF_CLASS2(INLINE_UID_FROM_FUID(mla_pump::kProcessorUID), PClassInfo::kManyInstances,
    kVstAudioEffectClass, "Mla Pump", 0, PlugType::kFxDynamics, "0.1.0", kVstVersionString,
    mla_pump::Processor::createInstance)
END_FACTORY
#endif
