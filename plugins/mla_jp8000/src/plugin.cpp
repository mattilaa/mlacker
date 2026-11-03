// Two-part JP-8000 performance instrument. Patch DSP is shared with Mla 8000;
// the performance owns routing, eight-voice allocation, controllers and state.
#include "../../../src/parameter_changes.h"
#include "presets.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include <limits>
#include <memory>

using namespace Steinberg;
using namespace Steinberg::Vst;
namespace mla_jp8000
{
static const FUID kUid(0x4D6C614A, 0x50383030, 0xA912E306, 0xD581762B);
constexpr uint32 kMagic = 0x38504A4D, kVersion = 1;

// Keep the patch engine's physical display and conversions on both panels.
class PartParameter final : public Parameter
{
    Parameter* source_;

public:
    PartParameter(Parameter* source, int part)
        : Parameter(source->getInfo()), source_(source)
    {
        info.id = partId(part, info.id);
        char name[128]{}, title[128]{};
        UString(source->getInfo().title, 128).toAscii(name, 128);
        std::snprintf(title, sizeof(title), "%s %s", part ? "Lower" : "Upper",
                      name);
        UString(info.title, 128).fromAscii(title);
    }
    void toString(ParamValue value, String128 text) const override
    {
        source_->toString(value, text);
    }
    bool fromString(const TChar* text, ParamValue& value) const override
    {
        return source_->fromString(text, value);
    }
    ParamValue toPlain(ParamValue value) const override
    {
        return source_->toPlain(value);
    }
    ParamValue toNormalized(ParamValue value) const override
    {
        return source_->toNormalized(value);
    }
};

class Processor final : public SingleComponentEffect, public IMidiMapping
{
    using Patch = mla_8000::Processor;
    std::array<std::unique_ptr<Patch>, 2> patch_;
    std::array<std::array<std::vector<float>, 2>, 2> buffers_;
    std::array<EventList, 2> events_{{EventList{4096}, EventList{4096}}};
    mlacker::ParameterChanges changes_[2];
    std::array<std::atomic<double>, kParamCount> values_{};
    std::atomic<bool> restore_{true};
    std::array<std::array<uint16_t, 128>, 16> held_{};
    std::array<std::array<uint8_t, 128>, 16> routes_{};
    std::array<std::array<uint16_t, 128>, 2> counts_{};
    std::array<std::array<bool, 128>, 2> deferred_{};
    bool ready_ = false;
    bool eventOverflow_ = false;
    bool outputSilent_ = true;
    std::array<double, 2> dcInput_{}, dcOutput_{};
    double dcPole_ = 0;
    int maxBlock_ = 0;
    struct Item
    {
        int32 offset = 0, order = 0;
        bool parameter = false;
        ParamID id = 0;
        double value = 0;
        Event event{};
    };
    std::array<Item, 8192> items_{};

    double value(ParamID id) const
    {
        return values_[paramIndex(id)].load(std::memory_order_relaxed);
    }
    int discrete(ParamID id) const
    {
        auto* p = parameters.getParameter(id);
        return int(std::round(value(id) * p->getInfo().stepCount));
    }
    void remember(ParamID id, double v)
    {
        values_[paramIndex(id)].store(v, std::memory_order_relaxed);
    }
    void list(ParamID id, const char* title,
              std::initializer_list<const char*> names)
    {
        UString128 text(title);
        auto* p = new StringListParameter(text, id, nullptr,
                                          ParameterInfo::kCanAutomate |
                                              ParameterInfo::kIsList);
        for(const auto* name : names)
        {
            UString128 n(name);
            p->appendString(n);
        }
        parameters.addParameter(p);
    }
    void range(ParamID id, const char* title, const char* units, double lo,
               double hi, double def, int steps)
    {
        UString128 t(title), u(units);
        parameters.addParameter(new RangeParameter(
            t, id, u, lo, hi, def, steps, ParameterInfo::kCanAutomate));
    }
    void point(int part, ParamID id, double v)
    {
        int32 qi = 0, pi = 0;
        auto* q = changes_[part].addParameterData(id, qi);
        if(q)
            q->addPoint(0, v, pi);
    }
    void patchPoint(int part, ParamID id, double v)
    {
        using namespace mla_8000;
        if(id == kKeyModeParam || id == kOsc2WaveParam)
            v = std::round(v * 2) / 3;
        if(id == kChorusTypeParam)
        {
            const int model = int(std::round(v * 12));
            const int delay =
                int(std::round(value(partId(part, kDelayTypeParam)) * 5));
            patch_[part]->setEffectModels(model, delay);
            v = model == 0 ? 0
                           : double(model >= 5 && model <= 7 ? kFlanger
                                                             : kSuperChorus) /
                                 4;
        }
        if(id == kDelayTypeParam)
        {
            const int model = int(std::round(v * 5));
            const int chorus =
                int(std::round(value(partId(part, kChorusTypeParam)) * 12));
            patch_[part]->setEffectModels(chorus, model);
            v = model == 0 ? 0 : (model >= 4 ? .5 : 1.);
        }
        point(part, id, v);
    }
    void clearRouting()
    {
        for(auto& channel : held_)
            channel.fill(0);
        for(auto& channel : routes_)
            channel.fill(0);
        for(auto& part : counts_)
            part.fill(0);
        for(auto& part : deferred_)
            part.fill(false);
    }
    void configure(bool voices)
    {
        const int mode = discrete(kMode), lower = discrete(kVoiceAssign) + 2;
        if(voices)
        {
            dcInput_.fill(0);
            dcOutput_.fill(0);
            const int limits[2]{
                mode == kSingleLower ? 0
                                     : (mode == kSingleUpper ? 8 : 8 - lower),
                mode == kSingleUpper ? 0 : (mode == kSingleLower ? 8 : lower)};
            for(int p = 0; p < 2; ++p)
            {
                if(patch_[p]->voiceLimit() == limits[p])
                    patch_[p]->panic();
                else
                    patch_[p]->setVoiceLimit(limits[p]);
            }
            clearRouting();
        }
        const double detune = (value(kPartDetune) * 2 - 1) * 50;
        patch_[0]->setPerformance(discrete(kUpperTranspose) - 24, detune * .5,
                                  value(kUpperPan) * 2 - 1,
                                  (value(kBrightness) * 2 - 1) * 4);
        patch_[1]->setPerformance(discrete(kLowerTranspose) - 24, -detune * .5,
                                  value(kLowerPan) * 2 - 1,
                                  (value(kBrightness) * 2 - 1) * 4);
    }
    void pushAll()
    {
        for(int p = 0; p < 2; ++p)
            for(int i = 0; i < kPartParams; ++i)
            {
                const ParamID id = mla_8000::kFirstSlot + i;
                patchPoint(p, id, value(partId(p, id)));
            }
        for(int p = 0; p < 2; ++p)
        {
            point(p, mla_8000::kPitchBendParam, value(kBend));
            point(p, mla_8000::kModWheelParam, value(kWheel));
        }
        configure(true);
    }
    void event(int part, const Event& e)
    {
        Event copy = e;
        copy.sampleOffset = 0;
        copy.busIndex = 0;
        if(events_[part].addEvent(copy) != kResultOk)
            eventOverflow_ = true;
    }
    void release(int part, int pitch)
    {
        Event e{};
        e.type = Event::kNoteOffEvent;
        e.noteOff.pitch = int16(pitch);
        e.noteOff.noteId = -1;
        event(part, e);
        deferred_[part][pitch] = false;
    }
    void notes(const Event& incoming)
    {
        const bool on = incoming.type == Event::kNoteOnEvent &&
                        incoming.noteOn.velocity > 0;
        const int pitch = incoming.type == Event::kNoteOnEvent
                              ? incoming.noteOn.pitch
                              : incoming.noteOff.pitch;
        const int channel = incoming.type == Event::kNoteOnEvent
                                ? incoming.noteOn.channel
                                : incoming.noteOff.channel;
        if(pitch < 0 || pitch > 127 || channel < 0 || channel > 15)
            return;
        if(on)
        {
            if(!std::isfinite(incoming.noteOn.velocity))
                return;
            const int mode = discrete(kMode);
            int mask =
                mode == kSingleUpper ? 1 : (mode == kSingleLower ? 2 : 3);
            if(mode == kSplitMode)
                mask = pitch <= discrete(kSplit) ? 2 : 1;
            for(int p = 0; p < 2; ++p)
            {
                const int accepted =
                    discrete(p ? kLowerChannel : kUpperChannel);
                if(accepted && accepted != channel + 1)
                    mask &= ~(1 << p);
            }
            if(mask == 0)
                return;
            if(held_[channel][pitch] == std::numeric_limits<uint16_t>::max())
                return;
            ++held_[channel][pitch];
            routes_[channel][pitch] = uint8(mask);
            for(int p = 0; p < 2; ++p)
                if(mask & (1 << p))
                {
                    if(deferred_[p][pitch] && counts_[p][pitch] == 0)
                        release(p, pitch);
                    if(counts_[p][pitch] < std::numeric_limits<uint16_t>::max())
                        ++counts_[p][pitch];
                    event(p, incoming);
                }
        }
        else if(held_[channel][pitch])
        {
            --held_[channel][pitch];
            const int mask = routes_[channel][pitch];
            for(int p = 0; p < 2; ++p)
                if(mask & (1 << p))
                {
                    if(counts_[p][pitch])
                        --counts_[p][pitch];
                    if(counts_[p][pitch] == 0)
                    {
                        if(discrete(kSustain))
                            deferred_[p][pitch] = true;
                        else
                            release(p, pitch);
                    }
                }
        }
    }
    void apply(const Item& item)
    {
        if(!item.parameter)
        {
            notes(item.event);
            return;
        }
        const ParamID id = item.id;
        const double v = std::clamp(item.value, 0., 1.);
        const double before = value(id);
        const int beforeStep = discrete(id);
        remember(id, v);
        if(id >= kMaster)
        {
            if(id == kBend || id == kWheel)
                for(int p = 0; p < 2; ++p)
                    point(p,
                          id == kBend ? mla_8000::kPitchBendParam
                                      : mla_8000::kModWheelParam,
                          v);
            if(id == kSustain && before >= .5 && v < .5)
                for(int p = 0; p < 2; ++p)
                    for(int n = 0; n < 128; ++n)
                        if(deferred_[p][n])
                            release(p, n);
            configure((id == kMode || id == kVoiceAssign || id == kSplit ||
                       id == kUpperChannel || id == kLowerChannel) &&
                      beforeStep != discrete(id));
        }
        else
        {
            const int part = id >= kLowerOffset ? 1 : 0;
            patchPoint(part, id - (part ? kLowerOffset : 0), v);
        }
    }
    tresult render(ProcessData& source, int32 from, int32 to)
    {
        const int32 frames = to - from;
        if(eventOverflow_)
        {
            for(auto& patch : patch_)
                patch->panic();
            for(auto& events : events_)
                events.clear();
            clearRouting();
            eventOverflow_ = false;
        }
        for(int part = 0; part < 2; ++part)
        {
            float* channels[]{buffers_[part][0].data(),
                              buffers_[part][1].data()};
            AudioBusBuffers output{};
            output.numChannels = 2;
            output.channelBuffers32 = channels;
            ProcessData data{};
            data.processMode = source.processMode;
            data.symbolicSampleSize = kSample32;
            data.numSamples = frames;
            data.numOutputs = 1;
            data.outputs = &output;
            data.inputEvents = &events_[part];
            data.inputParameterChanges = &changes_[part];
            ProcessContext context{};
            if(source.processContext)
            {
                context = *source.processContext;
                context.projectTimeSamples += from;
                context.continousTimeSamples += from;
                data.processContext = &context;
            }
            if(patch_[part]->process(data) != kResultOk)
                return kResultFalse;
            events_[part].clear();
            changes_[part].clear();
        }
        if(source.numOutputs && source.outputs &&
           source.outputs[0].numChannels == 2 &&
           source.outputs[0].channelBuffers32)
        {
            const double gain = mla_8000::gainFromNorm(value(kMaster));
            auto& output = source.outputs[0];
            for(int32 i = 0; i < frames; ++i)
                for(int c = 0; c < 2; ++c)
                {
                    const double input =
                        double(buffers_[0][c][i]) + buffers_[1][c][i];
                    double coupled =
                        input - dcInput_[c] + dcPole_ * dcOutput_[c];
                    if(std::abs(coupled) < 1e-20)
                        coupled = 0;
                    dcInput_[c] = input;
                    dcOutput_[c] = coupled;
                    const float sample = float(coupled * gain);
                    output.channelBuffers32[c][from + i] = sample;
                    if(sample != 0)
                        outputSilent_ = false;
                }
            output.silenceFlags = outputSilent_ ? 3 : 0;
        }
        return kResultOk;
    }

public:
    DEFINE_INTERFACES
    DEF_INTERFACE(IMidiMapping)
    END_DEFINE_INTERFACES(SingleComponentEffect)
    REFCOUNT_METHODS(SingleComponentEffect)
    static FUnknown* createInstance(void*)
    {
        return static_cast<IComponent*>(new Processor);
    }
    tresult PLUGIN_API initialize(FUnknown* context) override
    {
        auto result = SingleComponentEffect::initialize(context);
        if(result != kResultOk)
            return result;
        addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
        addEventInput(STR16("MIDI In"), 16);
        for(int p = 0; p < 2; ++p)
        {
            patch_[p] = std::make_unique<Patch>();
            if(patch_[p]->initialize(nullptr) != kResultOk)
                return kResultFalse;
            patch_[p]->setPartIdentity(p ? 104729u : 65537u);
        }
        UString128 master("Master");
        parameters.addParameter(new mla_8000::MappedParameter(
            master, kMaster, STR16("dB"), mla_8000::kUnityLevelNorm,
            [](double n, char* text, size_t size)
            { std::snprintf(text, size, "%.1f", -60 + n * 66); }));
        list(kMode, "Performance Mode",
             {"Single Upper", "Single Lower", "Dual", "Split"});
        list(kVoiceAssign, "Voices Lower / Upper",
             {"2 / 6", "3 / 5", "4 / 4", "5 / 3", "6 / 2"});
        range(kSplit, "Split Point", "note", 0, 127, 59, 127);
        range(kPartDetune, "Part Detune", "ct", -50, 50, 0, 0);
        range(kUpperTranspose, "Upper Transpose", "st", -24, 24, 0, 48);
        range(kLowerTranspose, "Lower Transpose", "st", -24, 24, 0, 48);
        range(kUpperPan, "Upper Pan", "", -1, 1, 0, 0);
        range(kLowerPan, "Lower Pan", "", -1, 1, 0, 0);
        range(kBend, "Pitch Bend", "", -8192, 8191, 0, 0);
        range(kWheel, "Mod Wheel", "", 0, 1, 0, 0);
        list(kSustain, "Sustain", {"Off", "On"});
        range(kBrightness, "Brightness", "oct", -4, 4, 0, 0);
        list(kUpperChannel, "Upper MIDI Channel",
             {"Omni", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11",
              "12", "13", "14", "15", "16"});
        list(kLowerChannel, "Lower MIDI Channel",
             {"Omni", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11",
              "12", "13", "14", "15", "16"});
        for(int p = 0; p < 2; ++p)
            for(int i = 0; i < kPartParams; ++i)
            {
                const ParamID id = mla_8000::kFirstSlot + i;
                char name[128]{};
                const char* prefix = p ? "Lower" : "Upper";
                if(id == mla_8000::kChorusTypeParam)
                {
                    std::snprintf(name, sizeof(name), "%s Chorus", prefix);
                    list(partId(p, id), name,
                         {"Off", "Super Chorus Slow", "Super Chorus Mid",
                          "Super Chorus Fast", "Super Chorus Clear",
                          "Flanger Slow", "Flanger Deep", "Flanger Fast",
                          "Deep Phasing Slow", "Jet Phasing", "Twisting",
                          "Freeze Phase 1", "Freeze Phase 2"});
                }
                else if(id == mla_8000::kDelayTypeParam)
                {
                    std::snprintf(name, sizeof(name), "%s Delay", prefix);
                    list(partId(p, id), name,
                         {"Off", "Panning L-R", "Panning R-L", "Panning Short",
                          "Mono Short", "Mono Long"});
                }
                else if(id == mla_8000::kOsc2WaveParam)
                {
                    std::snprintf(name, sizeof(name), "%s OSC 2 Wave", prefix);
                    list(partId(p, id), name, {"Square", "Saw", "Triangle"});
                }
                else if(id == mla_8000::kKeyModeParam)
                {
                    std::snprintf(name, sizeof(name), "%s Key Mode", prefix);
                    list(partId(p, id), name, {"Poly", "Mono", "Legato"});
                }
                else
                    parameters.addParameter(new PartParameter(
                        patch_[p]->getParameterObject(id), p));
            }
        Values defaults{};
        for(int i = 0; i < kParamCount; ++i)
            defaults[i] = parameters.getParameter(paramId(i))
                              ->getInfo()
                              .defaultNormalizedValue;
        defaults = factoryPad(defaults, 0);
        for(int i = 0; i < kParamCount; ++i)
        {
            remember(paramId(i), defaults[i]);
            auto* p = parameters.getParameter(paramId(i));
            p->getInfo().defaultNormalizedValue = defaults[i];
            p->setNormalized(defaults[i]);
        }
        return kResultOk;
    }
    tresult PLUGIN_API terminate() override
    {
        ready_ = false;
        for(auto& patch : patch_)
            if(patch)
            {
                patch->terminate();
                patch.reset();
            }
        return SingleComponentEffect::terminate();
    }
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement* in, int32 ni,
                                          SpeakerArrangement* out,
                                          int32 no) override
    {
        if(ni || no != 1 || !out || out[0] != SpeakerArr::kStereo)
            return kResultFalse;
        return SingleComponentEffect::setBusArrangements(in, ni, out, no);
    }
    tresult PLUGIN_API canProcessSampleSize(int32 size) override
    {
        return size == kSample32 ? kResultTrue : kResultFalse;
    }
    tresult PLUGIN_API setupProcessing(ProcessSetup& setup) override
    {
        if(!std::isfinite(setup.sampleRate) || setup.sampleRate < 8000 ||
           setup.sampleRate > 384000 || setup.maxSamplesPerBlock <= 0 ||
           setup.maxSamplesPerBlock > 1048576)
            return kResultFalse;
        auto r = SingleComponentEffect::setupProcessing(setup);
        if(r != kResultOk)
            return r;
        ready_ = false;
        maxBlock_ = setup.maxSamplesPerBlock;
        dcPole_ = std::exp(-2 * M_PI * 5 / setup.sampleRate);
        dcInput_.fill(0);
        dcOutput_.fill(0);
        for(int p = 0; p < 2; ++p)
        {
            if(patch_[p]->setupProcessing(setup) != kResultOk)
                return kResultFalse;
            for(auto& channel : buffers_[p])
                channel.resize(maxBlock_);
        }
        clearRouting();
        restore_.store(true, std::memory_order_release);
        ready_ = true;
        return kResultOk;
    }
    tresult PLUGIN_API setActive(TBool state) override
    {
        if(ready_)
        {
            dcInput_.fill(0);
            dcOutput_.fill(0);
            for(auto& patch : patch_)
                patch->setActive(state);
            clearRouting();
            restore_.store(true, std::memory_order_release);
        }
        return SingleComponentEffect::setActive(state);
    }
    uint32 PLUGIN_API getTailSamples() override
    {
        return uint32(std::min(384000. * 18., processSetup.sampleRate * 18.));
    }
    tresult PLUGIN_API getMidiControllerAssignment(int32 bus, int16 channel,
                                                   CtrlNumber cc,
                                                   ParamID& id) override
    {
        if(bus || channel < 0 || channel > 15)
            return kResultFalse;
        switch(cc)
        {
        case 1:
            id = kWheel;
            return kResultOk;
        case 7:
            id = kMaster;
            return kResultOk;
        case 20:
            id = partId(0, mla_8000::kKeyFollowParam);
            return kResultOk;
        case 21:
            id = partId(1, mla_8000::kKeyFollowParam);
            return kResultOk;
        case 22:
            id = partId(0, mla_8000::kFilterSlopeParam);
            return kResultOk;
        case 23:
            id = partId(1, mla_8000::kFilterSlopeParam);
            return kResultOk;
        case 64:
            id = kSustain;
            return kResultOk;
        case 74:
            id = kBrightness;
            return kResultOk;
        case kPitchBend:
            id = kBend;
            return kResultOk;
        default:
            break;
        }
        return patch_[0]->getMidiControllerAssignment(bus, channel, cc, id);
    }
    tresult PLUGIN_API process(ProcessData& data) override
    {
        if(!ready_ || data.symbolicSampleSize != kSample32 ||
           data.numSamples < 0 || data.numSamples > maxBlock_)
            return kResultFalse;
        if(data.numOutputs &&
           (!data.outputs || data.outputs[0].numChannels != 2 ||
            !data.outputs[0].channelBuffers32 ||
            !data.outputs[0].channelBuffers32[0] ||
            !data.outputs[0].channelBuffers32[1]))
            return kResultFalse;
        outputSilent_ = true;
        if(restore_.exchange(false, std::memory_order_acquire))
            pushAll();
        int count = 0;
        bool overflow = false;
        if(data.inputParameterChanges)
            for(int32 q = 0;
                q < data.inputParameterChanges->getParameterCount(); ++q)
            {
                auto* queue = data.inputParameterChanges->getParameterData(q);
                if(!queue || paramIndex(queue->getParameterId()) < 0)
                    continue;
                for(int32 p = 0; p < queue->getPointCount(); ++p)
                {
                    int32 offset;
                    double value;
                    if(queue->getPoint(p, offset, value) != kResultOk ||
                       !std::isfinite(value))
                        continue;
                    if(count == int(items_.size()))
                    {
                        overflow = true;
                        break;
                    }
                    auto& item = items_[count];
                    item = Item{};
                    item.offset = std::clamp(offset, 0, data.numSamples);
                    item.order = count++;
                    item.parameter = true;
                    item.id = queue->getParameterId();
                    item.value = value;
                }
            }
        if(data.inputEvents)
            for(int32 i = 0; i < data.inputEvents->getEventCount(); ++i)
            {
                Event e{};
                if(data.inputEvents->getEvent(i, e) != kResultOk ||
                   e.busIndex != 0 ||
                   (e.type != Event::kNoteOnEvent &&
                    e.type != Event::kNoteOffEvent))
                    continue;
                if(count == int(items_.size()))
                {
                    overflow = true;
                    break;
                }
                auto& item = items_[count];
                item = Item{};
                item.offset = std::clamp(e.sampleOffset, 0, data.numSamples);
                item.order = count++;
                item.event = e;
            }
        if(overflow)
        {
            for(auto& p : patch_)
                p->panic();
            clearRouting();
            count = 0;
        }
        std::sort(items_.begin(), items_.begin() + count,
                  [](const Item& a, const Item& b)
                  {
                      if(a.offset != b.offset)
                          return a.offset < b.offset;
                      if(a.parameter != b.parameter)
                          return a.parameter;
                      return a.order < b.order;
                  });
        int32 cursor = 0;
        for(int i = 0; i < count;)
        {
            const int32 at = items_[i].offset;
            if(at > cursor)
            {
                if(render(data, cursor, at) != kResultOk)
                    return kResultFalse;
                cursor = at;
            }
            do
            {
                apply(items_[i++]);
            } while(i < count && items_[i].offset == at);
        }
        return render(data, cursor, data.numSamples);
    }
    tresult PLUGIN_API getState(IBStream* stream) override
    {
        if(!stream)
            return kResultFalse;
        IBStreamer s(stream, kLittleEndian);
        if(!s.writeInt32u(kMagic) || !s.writeInt32u(kVersion) ||
           !s.writeInt32(kParamCount))
            return kResultFalse;
        for(int i = 0; i < kParamCount; ++i)
            if(!s.writeInt32u(paramId(i)) ||
               !s.writeDouble(values_[i].load(std::memory_order_relaxed)))
                return kResultFalse;
        return kResultOk;
    }
    tresult PLUGIN_API setState(IBStream* stream) override
    {
        if(!stream)
            return kResultFalse;
        IBStreamer s(stream, kLittleEndian);
        uint32 magic, version;
        int32 count;
        if(!s.readInt32u(magic) || magic != kMagic || !s.readInt32u(version) ||
           version != kVersion || !s.readInt32(count) || count < 0 ||
           count > 4096)
            return kResultFalse;
        Values next{};
        for(int i = 0; i < kParamCount; ++i)
            next[i] = values_[i].load(std::memory_order_relaxed);
        std::array<bool, kParamCount> seen{};
        for(int i = 0; i < count; ++i)
        {
            uint32 id;
            double v;
            if(!s.readInt32u(id) || !s.readDouble(v) || !std::isfinite(v) ||
               v < 0 || v > 1)
                return kResultFalse;
            const int index = paramIndex(id);
            if(index < 0)
                continue;
            if(seen[index])
                return kResultFalse;
            seen[index] = true;
            next[index] = v;
        }
        for(int i = 0; i < kParamCount; ++i)
        {
            values_[i].store(next[i], std::memory_order_relaxed);
            setParamNormalized(paramId(i), next[i]);
        }
        restore_.store(true, std::memory_order_release);
        return kResultOk;
    }
};
} // namespace mla_jp8000
#ifndef MLA_JP8000_TEST
BEGIN_FACTORY_DEF("MLang", "https://github.com/mattilaa/mlang",
                  "mailto:devnull@example.invalid")
DEF_CLASS2(INLINE_UID_FROM_FUID(mla_jp8000::kUid), PClassInfo::kManyInstances,
           kVstAudioEffectClass, "Mla JP8000", 0, PlugType::kInstrumentSynth,
           "0.1.0", kVstVersionString, mla_jp8000::Processor::createInstance)
END_FACTORY
#endif
