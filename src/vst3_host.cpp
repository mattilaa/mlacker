#include "mlang_audio_processor.h"
#include "mla_sampler_protocol.h"
#include "parameter_changes.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/base/ustring.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include "public.sdk/source/common/commonstringconvert.h"
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <vector>

namespace {
using namespace Steinberg;
using namespace Steinberg::Vst;

class Application final : public HostApplication {
    tresult PLUGIN_API getName(String128 name) override {
        UString(name, 128).fromAscii("mlacker"); return kResultOk;
    }
};
Application application;

class Processor {
public:
    VST3::Hosting::Module::Ptr module;
    IPtr<PlugProvider> provider;
    IPtr<IComponent> component;
    IPtr<IAudioProcessor> processor;
    HostProcessData data;
    EventList incoming{4096}, outgoing{512};
    ProcessContext context{};
    mlacker::ParameterChanges parameters;
    ParamID midiParameters[16][130];
    struct CachedParameter {
        ParameterInfo info{};
        std::string title;
        std::atomic<double> value{0};
    };
    static_assert(std::atomic<double>::is_always_lock_free);
    std::unique_ptr<CachedParameter[]> cached;
    int32 parameterCount = 0;
    std::string name;
    // Output buses 1..n-1 (multi-output instruments), interleaved stereo after
    // each process call, with their names.
    std::vector<std::vector<float>> aux;
    std::vector<std::string> outputNames;
    // Phrases for text-driven instruments (mlang_audio_processor set_text):
    // a ring of UTF-16 strings written on the control thread and read on the
    // audio thread, which the event queue orders. Allocated on first use.
    static constexpr int32 kPhrases = 256, kPhraseChars = 1024;
    std::unique_ptr<char16_t[]> phrases;
    uint32 phraseLength[kPhrases] = {};
    // System exclusive messages (mlang_audio_processor set_sysex), a ring
    // like the phrases: F0 and F7 are dropped, since VST3 plugins (JUCE's
    // wrapper among them) take the bytes between them. Allocated on first use.
    static constexpr int32 kSysExMessages = 256, kSysExBytes = 1024;
    std::unique_ptr<uint8[]> sysexData;
    uint32 sysexLength[kSysExMessages] = {};
    // The phrase the next note-on on `pendingChannel` carries (-1 = none).
    int32 pendingPhrase = -1, pendingChannel = 0, nextNoteId = 0;
    bool active = false, processing = false, instrument = false, addsOutput = false, overflow = false;
    uint32 transportState = 0;
    int32 maxFrames = 0;

    ~Processor() {
        // Called after AUHAL has stopped, on the owning main thread.
        if(processing) processor->setProcessing(false);
        if(active) component->setActive(false);
        data.unprepare(); processor.reset(); component.reset(); provider.reset();
        module.reset();
    }

    bool open(const char *path, double rate, int32 frames, std::string &error, bool instrumentOnly) {
        module = VST3::Hosting::Module::create(path, error);
        if(!module) return false;
        const auto &factory = module->getFactory();
        factory.setHostContext(&application);
        for(const auto &info : factory.classInfos()) {
            if(info.category() != kVstAudioEffectClass) continue;
            if(instrumentOnly && std::find(info.subCategories().begin(), info.subCategories().end(), "Instrument") == info.subCategories().end()) continue;
            name = info.name();
            provider = owned(new PlugProvider(factory, info, true));
            if(!provider->initialize()) { error = "VST3 component/controller initialization failed"; return false; }
            component = provider->getComponentPtr();
            processor = U::cast<IAudioProcessor>(component);
            break;
        }
        if(!component || !processor) { error = instrumentOnly ? "Bundle contains no VST3 instrument" : "Bundle contains no VST3 audio processor"; return false; }
        if(processor->canProcessSampleSize(kSample32) != kResultOk) {
            error = "VST3 processor does not support 32-bit float audio"; return false;
        }
        const int32 ins = component->getBusCount(kAudio, kInput);
        const int32 outs = component->getBusCount(kAudio, kOutput);
        // Extra output buses (multi-output instruments such as Mla Sampler)
        // keep their own arrangement; mlang routes each as aux_output.
        // Input buses after the first must be auxiliary (sidechains such as
        // Mla Vocoder's carrier input); mlacker leaves them inactive and silent.
        if(ins < 0 || ins > 16 || outs < 1 || outs > 16) {
            error = "Only 0-16 audio input buses and 1-16 output buses are supported"; return false;
        }
        for(int32 bus = 1; bus < ins; ++bus) {
            BusInfo info{};
            if(component->getBusInfo(kAudio, kInput, bus, info) != kResultOk || info.busType != kAux) {
                error = "Only one main audio input bus is supported"; return false;
            }
        }
        BusInfo outInfo{}, inInfo{};
        if(component->getBusInfo(kAudio, kOutput, 0, outInfo) != kResultOk ||
           outInfo.channelCount < 1 || outInfo.channelCount > 2) {
            error = "VST3 output must be mono or stereo"; return false;
        }
        if(ins && (component->getBusInfo(kAudio, kInput, 0, inInfo) != kResultOk ||
                   inInfo.channelCount < 1 || inInfo.channelCount > 2)) {
            error = "VST3 input must be mono or stereo"; return false;
        }
        SpeakerArrangement inputs[16] = {inInfo.channelCount == 1 ? SpeakerArr::kMono : SpeakerArr::kStereo};
        for(int32 bus = 1; bus < ins; ++bus)
            if(processor->getBusArrangement(kInput, bus, inputs[bus]) != kResultOk) inputs[bus] = SpeakerArr::kStereo;
        SpeakerArrangement outputs[16] = {outInfo.channelCount == 1 ? SpeakerArr::kMono : SpeakerArr::kStereo};
        for(int32 bus = 1; bus < outs; ++bus)
            if(processor->getBusArrangement(kOutput, bus, outputs[bus]) != kResultOk) outputs[bus] = SpeakerArr::kStereo;
        if(processor->setBusArrangements(ins ? inputs : nullptr, ins, outputs, outs) != kResultOk) {
            error = "VST3 processor rejected its mono/stereo bus arrangement"; return false;
        }
        if(component->activateBus(kAudio, kOutput, 0, true) != kResultOk ||
           (ins && component->activateBus(kAudio, kInput, 0, true) != kResultOk)) {
            error = "Could not activate VST3 audio buses"; return false;
        }
        for(int32 bus = 1; bus < ins; ++bus) component->activateBus(kAudio, kInput, bus, false);
        outputNames.clear();
        for(int32 bus = 0; bus < outs; ++bus) {
            BusInfo info{};
            outputNames.push_back(component->getBusInfo(kAudio, kOutput, bus, info) == kResultOk
                ? StringConvert::convert(std::u16string(info.name)) : "Out " + std::to_string(bus + 1));
            if(bus > 0) component->activateBus(kAudio, kOutput, bus, info.channelCount >= 1 && info.channelCount <= 2);
        }
        const int32 eventInputs = component->getBusCount(kEvent, kInput);
        if(instrumentOnly && eventInputs < 1) { error = "Instrument requires a MIDI event input"; return false; }
        const int32 eventOutputs = component->getBusCount(kEvent, kOutput);
        if(eventInputs < 0 || eventInputs > 16 || eventOutputs < 0 || eventOutputs > 16) {
            error = "Unsupported VST3 event bus count"; return false;
        }
        for(int32 bus = 0; bus < eventInputs; ++bus)
            if(component->activateBus(kEvent, kInput, bus, bus == 0) != kResultOk && bus == 0) {
                error = "Could not activate VST3 MIDI input"; return false;
            }
        for(int32 bus = 0; bus < eventOutputs; ++bus) component->activateBus(kEvent, kOutput, bus, false);
        ProcessSetup setup{kRealtime, kSample32, frames, rate};
        if(processor->setupProcessing(setup) != kResultOk) { error = "VST3 processing setup failed"; return false; }
        if(!data.prepare(*component, frames, kSample32)) { error = "VST3 buffer allocation failed"; return false; }
        aux.assign(outs - 1, std::vector<float>(static_cast<size_t>(frames) * 2u, 0.f));
        if(data.numOutputs != outs || data.outputs[0].numChannels < 1 || data.outputs[0].numChannels > 2 ||
           data.numInputs != ins || (ins && (data.inputs[0].numChannels < 1 || data.inputs[0].numChannels > 2))) {
            error = "VST3 processor changed to an unsupported bus layout"; return false;
        }
        context.sampleRate = rate; context.tempo = 120;
        data.processContext = &context; data.processMode = kRealtime;
        data.inputEvents = eventInputs ? &incoming : nullptr; data.outputEvents = &outgoing;
        data.inputParameterChanges = &parameters; data.outputParameterChanges = nullptr;
        auto controller = provider->getControllerPtr();
        // Separate controllers may initially expose defaults unrelated to the
        // processor's loaded patch. Synchronize before caching/saving values.
        if(controller) {
            MemoryStream state;
            if(component->getState(&state) == kResultOk && state.getSize() > 0) {
                state.seek(0, IBStream::kIBSeekSet, nullptr);
                controller->setComponentState(&state);
            }
        }
        parameterCount = controller ? controller->getParameterCount() : 0;
        if(parameterCount < 0 || parameterCount > 16384) { error = "Unsupported parameter count"; return false; }
        cached = std::make_unique<CachedParameter[]>(parameterCount);
        for(int32 i = 0; i < parameterCount; ++i) {
            auto &p = cached[i];
            if(controller->getParameterInfo(i, p.info) != kResultOk || p.info.stepCount < 0) {
                error = "Invalid plugin parameter metadata"; return false;
            }
            p.info.title[127] = 0;
            p.title = StringConvert::convert(std::u16string(p.info.title));
            double value = controller->getParamNormalized(p.info.id);
            p.value.store(std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : 0.0);
        }
        auto mapping = U::cast<IMidiMapping>(provider->getControllerPtr());
        for(int ch = 0; ch < 16; ++ch) for(int cc = 0; cc < 130; ++cc) {
            ParamID id = kNoParamId;
            if(mapping && mapping->getMidiControllerAssignment(0, ch, cc, id) != kResultOk) id = kNoParamId;
            midiParameters[ch][cc] = id;
        }
        maxFrames = frames; instrument = instrumentOnly || ins == 0;
        // An instrument with no input renders into a silent buffer and adds
        // to it; one with an input (e.g. a vocoder fed by a track) takes the
        // buffer as its input and replaces it, like an effect.
        addsOutput = ins == 0;
        if(component->setActive(true) != kResultOk) { error = "VST3 activation failed"; return false; }
        active = true;
        const auto started = processor->setProcessing(true);
        // The SDK's base AudioEffect legitimately returns kNotImplemented.
        if(started != kResultOk && started != kNotImplemented) { error = "VST3 processing activation failed"; return false; }
        processing = true; return true;
    }

    void note(int32 on, int32 channel, int32 pitch, int32 velocity, int32 offset) noexcept {
        if(!data.inputEvents) return;
        Event event{}; event.busIndex = 0; event.sampleOffset = offset;
        event.flags = Event::kIsLive;
        if(on) {
            event.type = Event::kNoteOnEvent; event.noteOn.channel = static_cast<int16>(channel);
            event.noteOn.pitch = static_cast<int16>(pitch); event.noteOn.velocity = velocity / 127.f;
            event.noteOn.noteId = -1;
            if(pendingPhrase >= 0 && channel == pendingChannel) {
                // The note carries its words as a note-expression text event.
                const int32 phrase = pendingPhrase; pendingPhrase = -1;
                nextNoteId = nextNoteId >= 0x3fffffff ? 1 : nextNoteId + 1;
                event.noteOn.noteId = nextNoteId;
                if(incoming.addEvent(event) != kResultOk) { overflow = true; return; }
                Event words{}; words.busIndex = 0; words.sampleOffset = offset; words.flags = Event::kIsLive;
                words.type = Event::kNoteExpressionTextEvent;
                words.noteExpressionText.typeId = kTextTypeID;
                words.noteExpressionText.noteId = nextNoteId;
                words.noteExpressionText.textLen = phraseLength[phrase];
                words.noteExpressionText.text = reinterpret_cast<const TChar *>(phrases.get() + phrase * kPhraseChars);
                if(incoming.addEvent(words) != kResultOk) overflow = true;
                return;
            }
        } else {
            event.type = Event::kNoteOffEvent; event.noteOff.channel = static_cast<int16>(channel);
            event.noteOff.pitch = static_cast<int16>(pitch); event.noteOff.velocity = velocity / 127.f;
            event.noteOff.noteId = -1;
        }
        if(incoming.addEvent(event) != kResultOk) overflow = true;
    }

    // Control thread: store phrase `index` as UTF-16, cut at kPhraseChars - 1.
    int32_t setText(int32_t index, const char *text) {
        if(index < 0 || index >= kPhrases || !text) return -1;
        if(!phrases) phrases = std::make_unique<char16_t[]>(static_cast<size_t>(kPhrases) * kPhraseChars);
        const std::u16string wide = StringConvert::convert(std::string(text));
        const size_t length = std::min<size_t>(wide.size(), kPhraseChars - 1);
        char16_t *slot = phrases.get() + static_cast<size_t>(index) * kPhraseChars;
        std::copy_n(wide.data(), length, slot); slot[length] = 0;
        phraseLength[index] = static_cast<uint32>(length);
        return 0;
    }
    // Audio thread: the next note-on on `channel` in this block speaks `index`.
    void text(int32_t channel, int32_t index) noexcept {
        if(!phrases || index < 0 || index >= kPhrases) return;
        pendingPhrase = index; pendingChannel = channel;
    }

    // Control thread: store message `index`, a whole F0 ... F7 message.
    int32_t setSysEx(int32_t index, const uint8_t *bytes, int32_t size) {
        if(index < 0 || index >= kSysExMessages || !bytes || size < 3 || size > kSysExBytes) return -1;
        if(bytes[0] != 0xF0 || bytes[size - 1] != 0xF7) return -1;
        if(!sysexData) sysexData = std::make_unique<uint8[]>(static_cast<size_t>(kSysExMessages) * kSysExBytes);
        std::copy_n(bytes + 1, size - 2, sysexData.get() + static_cast<size_t>(index) * kSysExBytes);
        sysexLength[index] = static_cast<uint32>(size - 2);
        return 0;
    }
    // Audio thread: send message `index` as a data event at `offset`.
    void sysex(int32_t index, int32_t offset) noexcept {
        if(!data.inputEvents || !sysexData || index < 0 || index >= kSysExMessages) return;
        Event event{}; event.busIndex = 0; event.sampleOffset = offset; event.flags = Event::kIsLive;
        event.type = Event::kDataEvent; event.data.type = DataEvent::kMidiSysEx;
        event.data.size = sysexLength[index];
        event.data.bytes = sysexData.get() + static_cast<size_t>(index) * kSysExBytes;
        if(incoming.addEvent(event) != kResultOk) overflow = true;
    }

    void begin(bool reset) noexcept {
        incoming.clear(); outgoing.clear(); parameters.clear(); pendingPhrase = -1;
        reset = reset || overflow; overflow = false;
        // Bounded all-notes-off fallback also covers dropped note-offs.
        if(reset)
            for(int32 channel = 0; channel < 16; ++channel)
                for(int32 pitch = 0; pitch < 128; ++pitch) note(0, channel, pitch, 0, 0);
    }

    void control(int32 channel, int32 controller, int32 value, int32 offset) noexcept {
        if(channel < 0 || channel >= 16 || controller < 0 || controller >= 130) return;
        ParamID id = midiParameters[channel][controller];
        if(id == kNoParamId) return;
        int32 index = 0;
        auto *queue = parameters.addParameterData(id, index);
        double normalized = value / (controller == 129 ? 16383.0 : 127.0);
        if(!queue || queue->addPoint(offset, normalized, index) != kResultOk) overflow = true;
        else for(int32 i = 0; i < parameterCount; ++i)
            if(cached[i].info.id == id) cached[i].value.store(normalized, std::memory_order_relaxed);
    }

    double parameterInfo(int32 index, int32 key) const noexcept {
        if(key == 0) return parameterCount;
        if(index < 0 || index >= parameterCount) return -1;
        const auto &p = cached[index];
        if(key == 1) return p.info.stepCount;
        if(key == 2) return p.value.load(std::memory_order_relaxed);
        if(key == 3) return (p.info.flags & ParameterInfo::kIsReadOnly) != 0;
        if(key == 4) return p.info.id;
        return -1;
    }
    void parameter(int32 index, double value, int32 offset) noexcept {
        if(index < 0 || index >= parameterCount || !std::isfinite(value) || value < 0 || value > 1) return;
        auto &p = cached[index];
        if(p.info.flags & ParameterInfo::kIsReadOnly) return;
        int32 point = 0;
        auto *queue = parameters.addParameterData(p.info.id, point);
        if(!queue || queue->addPoint(offset, value, point) != kResultOk) { overflow = true; return; }
        p.value.store(value, std::memory_order_relaxed);
    }

    // Control thread. Sampler pads travel as mla_sampler_protocol messages to
    // the component's IConnectionPoint; the plugin owns the audio hand-off.
    int32_t loadPad(int32_t pad, const float *pcm, int64_t frames, int32_t channels, double rate,
                    const char *sampleName, std::string &error) {
        auto connection = U::cast<IConnectionPoint>(component);
        if(!connection) { error = "This instrument does not accept pad samples"; return -1; }
        const bool clear = frames == 0;
        if(!clear && (frames < 1 || frames > mla_sampler::kMaxFrames || channels < 1 || channels > 2 || !pcm)) {
            error = "Pad samples must be mono/stereo with 1-16777216 frames"; return -1;
        }
        auto message = owned(new HostMessage);
        message->setMessageID(clear ? mla_sampler::kClearMessage : mla_sampler::kLoadPcmMessage);
        auto *attributes = message->getAttributes();
        attributes->setInt("pad", pad);
        if(!clear) {
            const std::string label = sampleName ? sampleName : "";
            attributes->setInt("channels", channels);
            attributes->setInt("frames", frames);
            attributes->setFloat("rate", rate);
            attributes->setBinary("data", pcm, static_cast<uint32>(frames * channels * sizeof(float)));
            attributes->setBinary("name", label.data(), static_cast<uint32>(label.size()));
        }
        tresult result = kResultFalse;
        try { result = connection->notify(message); } catch(...) { result = kInternalError; }
        if(result == kResultOk) return 0;
        const void *why = nullptr; uint32 size = 0;
        if(attributes->getBinary("error", why, size) == kResultOk && why && size)
            error.assign(static_cast<const char *>(why), size);
        else
            error = "This instrument does not accept pad samples";
        return -1;
    }

    // Control thread. -1 when the plugin does not answer the sampler query.
    int64_t samplerInfo(int32_t key) {
        auto connection = U::cast<IConnectionPoint>(component);
        if(!connection || key < 0 || key > 2) return -1;
        auto message = owned(new HostMessage);
        message->setMessageID(mla_sampler::kInfoMessage);
        auto *attributes = message->getAttributes();
        try { if(connection->notify(message) != kResultOk) return -1; } catch(...) { return -1; }
        int64 value = -1;
        const char *id = key == 0 ? "root" : (key == 1 ? "pads" : "occupied");
        return attributes->getInt(id, value) == kResultOk ? value : -1;
    }

    // Control thread: a sampler pad's slice markers (mla_sampler_protocol.h).
    // Copies up to `max` into `out`; returns how many there are, -1 if none.
    int32_t padMarkers(int32_t pad, double *out, int32_t max) {
        auto connection = U::cast<IConnectionPoint>(component);
        if(!connection || pad < 0) return -1;
        auto message = owned(new HostMessage);
        message->setMessageID(mla_sampler::kMarkersMessage);
        auto *attributes = message->getAttributes();
        attributes->setInt("pad", pad);
        try { if(connection->notify(message) != kResultOk) return -1; } catch(...) { return -1; }
        const void *data = nullptr; uint32 size = 0;
        if(attributes->getBinary("frames", data, size) != kResultOk || !data || size % sizeof(double)) return -1;
        const int32_t count = static_cast<int32_t>(size / sizeof(double));
        if(out && max > 0) std::memcpy(out, data, static_cast<size_t>(std::min(count, max)) * sizeof(double));
        return count;
    }

    // Control thread: replace a pad's markers, or detect them again (count < 0).
    int32_t setPadMarkers(int32_t pad, const double *frames, int32_t count) {
        auto connection = U::cast<IConnectionPoint>(component);
        if(!connection || pad < 0) return -1;
        auto message = owned(new HostMessage);
        message->setMessageID(mla_sampler::kMarkersMessage);
        auto *attributes = message->getAttributes();
        attributes->setInt("pad", pad);
        attributes->setInt("set", count < 0 ? 2 : 1);
        if(count > 0 && frames) attributes->setBinary("frames", frames, static_cast<uint32>(count * sizeof(double)));
        else if(count == 0) attributes->setBinary("frames", "", 0);
        try { return connection->notify(message) == kResultOk ? 0 : -1; } catch(...) { return -1; }
    }

    // Audio thread, before render: the sequencer's tempo and beat position.
    void transport(double tempo, double beat, int32_t playing) noexcept {
        transportState = 0;
        if(tempo > 0) { context.tempo = tempo; transportState |= ProcessContext::kTempoValid; }
        if(tempo > 0 && std::isfinite(beat)) {
            context.projectTimeMusic = beat; transportState |= ProcessContext::kProjectTimeMusicValid;
            if(playing) transportState |= ProcessContext::kPlaying;
        }
    }

    int32 render(float *stereo, int32 frames, uint64_t clock) noexcept {
        if(frames < 0 || frames > maxFrames || overflow) return -1;
        data.numSamples = frames; context.projectTimeSamples = static_cast<TSamples>(clock);
        context.continousTimeSamples = static_cast<TSamples>(clock);
        context.state = ProcessContext::kContTimeValid | transportState;
        for(int32 aux = 1; aux < data.numInputs; ++aux) {
            auto &bus = data.inputs[aux];
            for(int32 ch = 0; ch < bus.numChannels; ++ch)
                if(bus.channelBuffers32) std::fill_n(bus.channelBuffers32[ch], frames, 0.f);
            bus.silenceFlags = (uint64)-1;
        }
        if(data.numInputs) {
            auto &bus = data.inputs[0]; bus.silenceFlags = 0;
            for(int32 f = 0; f < frames; ++f) {
                if(bus.numChannels == 1) bus.channelBuffers32[0][f] = (stereo[2*f] + stereo[2*f+1]) * 0.5f;
                else { bus.channelBuffers32[0][f] = stereo[2*f]; bus.channelBuffers32[1][f] = stereo[2*f+1]; }
            }
        }
        auto &output = data.outputs[0]; output.silenceFlags = 0;
        for(int32 ch = 0; ch < output.numChannels; ++ch) std::fill_n(output.channelBuffers32[ch], frames, 0.f);
        try {
            if(processor->process(data) != kResultOk) return -1;
        } catch(...) { return -1; }
        for(size_t bus = 0; bus < aux.size(); ++bus) {
            const auto &buffers = data.outputs[bus + 1];
            float *interleaved = aux[bus].data();
            if(!buffers.channelBuffers32 || buffers.numChannels < 1 || buffers.numChannels > 2) {
                std::fill_n(interleaved, static_cast<size_t>(frames) * 2u, 0.f); continue;
            }
            for(int32 f = 0; f < frames; ++f) {
                interleaved[2*f] = buffers.channelBuffers32[0][f];
                interleaved[2*f+1] = buffers.channelBuffers32[buffers.numChannels == 1 ? 0 : 1][f];
            }
        }
        for(int32 f = 0; f < frames; ++f) {
            float l = output.channelBuffers32[0][f];
            float r = output.channelBuffers32[output.numChannels == 1 ? 0 : 1][f];
            if(addsOutput) { stereo[2*f] += l; stereo[2*f+1] += r; }
            else { stereo[2*f] = l; stereo[2*f+1] = r; }
        }
        return 0;
    }
};

template<bool InstrumentOnly = false>
int32_t load(const char *path, double rate, int32_t frames,
    mlang_audio_processor *out, char *error, int32_t errorSize) {
    try {
        auto plugin = std::make_unique<Processor>();
        std::string why;
        if(!plugin->open(path, rate, frames, why, InstrumentOnly)) {
            std::snprintf(error, errorSize, "%s", why.c_str()); return -1;
        }
        *out = {};
        out->context = plugin.get(); out->instrument = plugin->instrument;
        out->begin = [](void *p, int32_t reset) { static_cast<Processor*>(p)->begin(reset != 0); };
        out->note = [](void *p, int32_t on, int32_t ch, int32_t note, int32_t vel, int32_t offset) { static_cast<Processor*>(p)->note(on, ch, note, vel, offset); };
        out->process = [](void *p, float *buffer, int32_t frames, uint64_t clock) { return static_cast<Processor*>(p)->render(buffer, frames, clock); };
        out->transport = [](void *p, double tempo, double beat, int32_t playing) { static_cast<Processor*>(p)->transport(tempo, beat, playing); };
        out->destroy = [](void *p) { delete static_cast<Processor*>(p); };
        out->name = [](void *p) { return static_cast<Processor*>(p)->name.c_str(); };
        out->control = [](void *p, int32_t ch, int32_t cc, int32_t value, int32_t offset) { static_cast<Processor*>(p)->control(ch, cc, value, offset); };
        out->parameter_info = [](void *p, int32_t i, int32_t k) { return static_cast<Processor*>(p)->parameterInfo(i, k); };
        out->parameter_name = [](void *p, int32_t i) -> const char * {
            auto *host = static_cast<Processor*>(p);
            return i >= 0 && i < host->parameterCount ? host->cached[i].title.c_str() : "";
        };
        out->parameter = [](void *p, int32_t i, double v, int32_t offset) { static_cast<Processor*>(p)->parameter(i, v, offset); };
        out->parameter_edited = [](void *p, int32_t i, double value) {
            auto *host = static_cast<Processor*>(p);
            if(i < 0 || i >= host->parameterCount) return;
            auto &parameter = host->cached[i];
            parameter.value.store(value, std::memory_order_relaxed);
            if(auto controller = host->provider->getControllerPtr()) controller->setParamNormalized(parameter.info.id, value);
        };
        out->load_pad = [](void *p, int32_t pad, const float *pcm, int64_t frames, int32_t channels, double rate,
                           const char *sampleName, char *error, int32_t errorSize) -> int32_t {
            std::string why;
            try {
                if(static_cast<Processor*>(p)->loadPad(pad, pcm, frames, channels, rate, sampleName, why) == 0) return 0;
            } catch(...) { why = "Pad sample load failed"; }
            std::snprintf(error, errorSize, "%s", why.c_str()); return -1;
        };
        out->set_text = [](void *p, int32_t index, const char *text) -> int32_t {
            try { return static_cast<Processor*>(p)->setText(index, text); } catch(...) { return -1; }
        };
        out->text = [](void *p, int32_t channel, int32_t index, int32_t) { static_cast<Processor*>(p)->text(channel, index); };
        out->set_sysex = [](void *p, int32_t index, const uint8_t *bytes, int32_t size) -> int32_t {
            try { return static_cast<Processor*>(p)->setSysEx(index, bytes, size); } catch(...) { return -1; }
        };
        out->sysex = [](void *p, int32_t index, int32_t offset) { static_cast<Processor*>(p)->sysex(index, offset); };
        out->output_count = [](void *p) -> int32_t { return static_cast<int32_t>(static_cast<Processor*>(p)->outputNames.size()); };
        out->output_name = [](void *p, int32_t bus) -> const char * {
            auto *host = static_cast<Processor*>(p);
            return bus >= 0 && bus < static_cast<int32_t>(host->outputNames.size()) ? host->outputNames[bus].c_str() : "";
        };
        out->aux_output = [](void *p, int32_t bus) -> const float * {
            auto *host = static_cast<Processor*>(p);
            return bus >= 1 && bus <= static_cast<int32_t>(host->aux.size()) ? host->aux[bus - 1].data() : nullptr;
        };
        out->sampler_info = [](void *p, int32_t key) -> int64_t {
            try { return static_cast<Processor*>(p)->samplerInfo(key); } catch(...) { return -1; }
        };
        out->pad_markers = [](void *p, int32_t pad, double *frames, int32_t max) -> int32_t {
            try { return static_cast<Processor*>(p)->padMarkers(pad, frames, max); } catch(...) { return -1; }
        };
        out->set_pad_markers = [](void *p, int32_t pad, const double *frames, int32_t count) -> int32_t {
            try { return static_cast<Processor*>(p)->setPadMarkers(pad, frames, count); } catch(...) { return -1; }
        };
        plugin.release(); return 0;
    } catch(const std::exception &e) { std::snprintf(error, errorSize, "VST3 load failed: %s", e.what()); }
    catch(...) { std::snprintf(error, errorSize, "VST3 load failed with an unknown exception"); }
    return -1;
}
} // namespace

// mlacker's version for `mlacker --version`, as an owned (malloc'd) str8.
extern "C" char* mlacker_version() { return strdup(MLACKER_VERSION); }

extern "C" void mlacker_install_vst3_host() {
    PluginContextFactory::instance().setPluginContext(&application);
    // SDK diagnostics must not corrupt the terminal screen.
    PlugProvider::setErrorStream(nullptr);
    mlang_audio_register_processor_factory(load<false>);
    mlang_audio_register_instrument_factory(load<true>);
}
