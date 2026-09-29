// Mla Sampler - VST3 sampler instrument with looping and multiple outputs.
//
// Sixteen sample slots. A slot in Pad mode plays on one key (Root Key + slot);
// in Zone mode it plays across a key range, pitched from its zone root when
// Key Track is on. A velocity range limits either to notes that hard.
// Overlapping zones layer. Each slot holds one sample with
// its own level, pan, tune, loop (off, forward or bidirectional, between a
// start and an end point) and output bus. A slot uses the instance's amp
// ADSR or its own; note-off releases it, and looping slots keep looping
// through the release. Playback begins at the slot's start point; a reversed
// slot plays its sample backwards. Each voice
// then runs a chain of filter stages (types from an extensible list) moved by
// the instance's or the slot's own filter envelope. Two LFOs modulate its
// pitch, filter cutoff and level, and four mod routes send LFOs, envelopes,
// velocity or key to pitch, cutoff, resonance, level, pan or start.
//
// Outputs: bus 0 "Main" plus seven auxiliary stereo buses "Out 2".."Out 8".
// A slot sent to an aux bus the host has not activated plays on Main, so the
// plug-in works unchanged in hosts that only use the main output.
//
// Loop settings are ordinary VST3 parameters (loop points as a fraction of
// the sample length), so hosts can read, edit, automate and save them without
// a custom protocol; mlacker's Sampler pane (Shift+S) edits them that way.
//
// The per-sample voice math (playhead, loop wrap/reflection, interpolation,
// envelope, pan) lives in MLang (`src/mla_sampler_dsp.mla` on top of
// `modules/dsp/envelope.mla`). This file owns the sample buffers and the voice
// pool, schedules MIDI sample-accurately, routes voices to buses, maps VST3
// parameters and persists state. Samples arrive through the IConnectionPoint
// messages in `mla_sampler_protocol.h`, like Mla Drum's pads.
//
// Threading follows Mla Drum: slots are immutable `Sample` objects owned by
// the control thread and published through atomics; the audio thread picks up
// changes at block start, and replaced samples are freed only after the audio
// thread has finished two further blocks.

#include "mla_sampler_protocol.h"

#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/base/ustring.h"
#include "base/source/fstreamer.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// --- MLang DSP bridge (compiled from src/mla_sampler_dsp.mla) ----------------
struct SamplerVoice;
extern "C" SamplerVoice *mlasampler_voice_create__f32(float sampleRate);
extern "C" void mlasampler_voice_destroy__ptr_struct_SamplerVoice(SamplerVoice *voice);
extern "C" void mlasampler_voice_set_envelope__ptr_struct_SamplerVoice_f32_f32_f32_f32(
    SamplerVoice *voice, float attack, float decay, float sustain, float release);
extern "C" void mlasampler_voice_start__ptr_struct_SamplerVoice_f64_f64_f32_f32_i32_f64_f64(
    SamplerVoice *voice, double frames, double step, float gain, float pan, int32_t loopMode, double loopStart,
    double loopEnd);
extern "C" void mlasampler_voice_release__ptr_struct_SamplerVoice(SamplerVoice *voice);
extern "C" void mlasampler_voice_stop__ptr_struct_SamplerVoice(SamplerVoice *voice);
extern "C" void mlasampler_voice_choke__ptr_struct_SamplerVoice_f32(SamplerVoice *voice, float seconds);
extern "C" void mlasampler_voice_set_lfo__ptr_struct_SamplerVoice_i32_i32_f32_f32_f32_f32_f32(
    SamplerVoice *voice, int32_t index, int32_t shape, float rateHz, float delaySeconds, float pitchSemitones,
    float cutoffOctaves, float levelDepth);
extern "C" void mlasampler_voice_set_lfo_phase__ptr_struct_SamplerVoice_i32_f32(SamplerVoice *voice, int32_t index,
                                                                           float phase);
extern "C" void mlasampler_voice_set_route__ptr_struct_SamplerVoice_i32_i32_i32_f32(SamplerVoice *voice, int32_t route,
                                                                                int32_t source, int32_t target,
                                                                                float amount);
extern "C" void mlasampler_voice_set_note__ptr_struct_SamplerVoice_f32_f32(SamplerVoice *voice, float velocity, float key);
extern "C" void mlasampler_voice_set_glide__ptr_struct_SamplerVoice_f32_f32(SamplerVoice *voice, float semitones,
                                                                        float seconds);
extern "C" void mlasampler_voice_set_chain__ptr_struct_SamplerVoice_i32(SamplerVoice *voice, int32_t chain);
extern "C" void mlasampler_voice_set_beats__ptr_struct_SamplerVoice_f64(SamplerVoice *voice, double timeStep);
extern "C" void mlasampler_voice_segment__ptr_struct_SamplerVoice_f64_f64_bool(SamplerVoice *voice, double from,
                                                                            double until, bool cross);
extern "C" double mlasampler_voice_position__ptr_struct_SamplerVoice(SamplerVoice *voice);
extern "C" void mlasampler_voice_set_stretch__ptr_struct_SamplerVoice_f64_f32(SamplerVoice *voice, double timeStep,
                                                                          float grainSeconds);
extern "C" void mlasampler_voice_set_pitch_envelope__ptr_struct_SamplerVoice_f32_f32_f32(SamplerVoice *voice, float depth,
                                                                                         float attack, float decay);
extern "C" void mlasampler_voice_set_controllers__ptr_struct_SamplerVoice_f32_f32_f32_f32_f32_bool(
    SamplerVoice *voice, float wheel, float aftertouch, float bend, float cc, float bendSemitones, bool snap);
extern "C" int32_t mlasampler_voice_is_active__ptr_struct_SamplerVoice(SamplerVoice *voice);
extern "C" double mlasampler_voice_frame__ptr_struct_SamplerVoice(SamplerVoice *voice);
extern "C" double mlasampler_voice_next_frame__ptr_struct_SamplerVoice(SamplerVoice *voice);
extern "C" void mlasampler_voice_set_step__ptr_struct_SamplerVoice_f64(SamplerVoice *voice, double step);
extern "C" void mlasampler_voice_set_gain__ptr_struct_SamplerVoice_f32_f32(SamplerVoice *voice, float gain, float pan);
extern "C" void mlasampler_voice_set_loop__ptr_struct_SamplerVoice_i32_f64_f64(SamplerVoice *voice, int32_t loopMode,
                                                                           double loopStart, double loopEnd);
extern "C" void mlasampler_voice_set_crossfade__ptr_struct_SamplerVoice_f64(SamplerVoice *voice, double frames);
extern "C" void mlasampler_voice_set_start__ptr_struct_SamplerVoice_f64(SamplerVoice *voice, double frame);
extern "C" void mlasampler_voice_set_filter__ptr_struct_SamplerVoice_i32_i32_f32_f32_f32_f32_f32_f32(
    SamplerVoice *voice, int32_t stage, int32_t kind, float cutoffHz, float resonanceDb, float envOctaves,
    float gainDb, float drive, float modScale);
extern "C" void mlasampler_voice_set_filter_envelope__ptr_struct_SamplerVoice_f32_f32_f32_f32(
    SamplerVoice *voice, float attack, float decay, float sustain, float release);
extern "C" double mlasampler_voice_shadow_frame__ptr_struct_SamplerVoice(SamplerVoice *voice);
extern "C" float mlasampler_voice_render__ptr_struct_SamplerVoice_f32_f32_f32_f32_f32_f32_f32_f32_ptr_f32(
    SamplerVoice *voice, float left0, float right0, float left1, float right1, float shadowLeft0,
    float shadowRight0, float shadowLeft1, float shadowRight1, float *outRight);

// --- MLang runtime PCM decoder (libmlang_std.a, std::audio::PcmAudio) --------
struct MlangList {
    int64_t size;
    void *data;
};
extern "C" int64_t __mlang_std_audio_pcm_load(const char *path);
extern "C" int64_t __mlang_std_audio_pcm_file_sample_rate(int64_t handle);
extern "C" int64_t __mlang_std_audio_pcm_file_channels(int64_t handle);
extern "C" int64_t __mlang_std_audio_pcm_file_frame_count(int64_t handle);
extern "C" MlangList __mlang_std_audio_pcm_file_samples(int64_t handle);
extern "C" int32_t __mlang_std_audio_pcm_file_close(int64_t handle);
extern "C" const char *__mlang_std_audio_last_error(void);

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace mla_sampler_plugin {

// Stable class id. Distinct from the other Mla plug-ins and the SDK examples.
static const FUID kProcessorUID(0x4D6C6153, 0x616D706C, 0x9E27C1A4, 0x5B83D06F);

constexpr int kNumSlots = 16;
constexpr int kNumVoices = 32;
constexpr int kNumOutputs = 8; // Main + Out 2..Out 8, all stereo: what a slot's Output picks.
// Then Send A and Send B: every slot adds its sound times its send levels.
// A host routes them to effect inputs (mlacker: its aux effect channels).
constexpr int kNumSends = 2;
constexpr int kNumBuses = kNumOutputs + kNumSends;
constexpr uint32 kStateMagic = 0x4D534C4D; // "MLSM" little-endian
constexpr uint32 kStateVersion = 1;

enum LoopMode : int32_t { kLoopOff = 0, kLoopForward = 1, kLoopBidirectional = 2 };

enum ParamId : ParamID {
    kLevelParam = 100,
    kTuneParam,
    kVelocityParam,
    kRootKeyParam,
    kAttackParam,
    kDecayParam,
    kSustainParam,
    kReleaseParam,
    // MIDI controllers (see getMidiControllerAssignment): the pitch bend
    // range, which CC the Mod CC source reads, and the controllers' values.
    kBendRangeParam = 110,  // 0 .. 24 semitones, stepped
    kModCcParam = 111,      // one of kModCcNumbers, a 16-entry list
    kModWheelParam = 112,   // CC 1
    kAftertouchParam = 113, // channel pressure
    kPitchBendParam = 114,  // centre 8192 / 16383
    kCcValueParamBase = 120, // 120 + i: the value of kModCcNumbers[i]
    kSlotParamBase = 200, // slot s: 200 + 7s, see SlotParam
    kZoneParamBase = 400, // slot s: 400 + 5s, see ZoneParam
    kCrossfadeParamBase = 500, // slot s: 500 + s, loop crossfade (fraction of the sample)
    kVelocityParamBase = 600,  // slot s: 600 + 2s low, + 1 high velocity (MIDI 1..127)
    kEnvelopeParamBase = 700,  // slot s: 700 + 5s, see EnvelopeParam
    kStartParamBase = 800,     // slot s: 800 + s, sample start (fraction of the sample)
    kGroupParamBase = 900,     // slot s: 900 + s, group (Off, 1..8)
    kGroupModeParam = 950,     // how a group picks: Round-robin or Random
    kFilterEnvelopeParamBase = 960,     // instance filter ADSR: 960 attack .. 963 release
    kSlotFilterEnvelopeParamBase = 1100, // slot s: 1100 + 5s, see EnvelopeParam
    kChokeParamBase = 1200,              // slot s: 1200 + s, choke group (Off, 1..8)
    kReverseParamBase = 1300,            // slot s: 1300 + s, Off or On
    kUnisonParamBase = 1400,             // slot s: 1400 + 4s: 0 voices, 1 detune, 2 spread
    kPlayParamBase = 1500,               // slot s: 1500 + 4s: 0 play mode, 1 glide time
    kPitchEnvParamBase = 1600,           // slot s: 1600 + 4s: 0 depth, 1 attack, 2 decay
    kSendParamBase = 1700,               // slot s: 1700 + 4s: 0 Send A, 1 Send B
    kChainParamBase = 1800,              // slot s: 1800 + 4s: 0 filter chain, 1 tempo sync, 2 beats
    kCurveParamBase = 1900,              // slot s: 1900 + 4s: 0 velocity curve, 1 velocity depth, 2 key level
    kFilterParamBase = 2000,             // slot s, stage t: 2000 + 32s + 8t, see FilterField
    kLfoParamBase = 3000,                // slot s, LFO l: 3000 + 32s + 16l, see LfoField
    kRouteParamBase = 4000,              // slot s, route r: 4000 + 32s + 4r, see RouteField
};

// Per-slot LFOs. IDs leave room for 2 LFOs of 16 fields per slot; more fields
// only add parameters after the existing ones.
constexpr int kLfos = 2;
enum LfoField : int {
    kLfoShape = 0, // one of kLfoShapeNames, a 16-entry list
    kLfoRate,      // 0.05 .. 20 Hz (without sync)
    kLfoSync,      // Off: Rate. On: Division of the host tempo
    kLfoDivision,  // one of kLfoDivisionNames, a 16-entry list
    kLfoDelay,     // 0 .. 2 s before full depth
    kLfoPitch,     // -12 .. +12 semitones
    kLfoCutoff,    // -4 .. +4 octaves of filter cutoff
    kLfoLevel,     // 0 .. 100 % tremolo
    kLfoTrigger,   // Free (runs in time) or Retrigger (restarts with each note)
    kLfoFields,
};
constexpr int kLfoSlotStride = 32;
constexpr int kLfoStride = 16;
// As with the filter types, both lists keep a fixed length so saved values
// never move; the voice treats a reserved shape as no modulation.
constexpr int kLfoShapeCount = 16;
static const char *const kLfoShapeNames[] = {"Sine", "Triangle", "Saw Up", "Saw Down", "Square", "S&H"};
constexpr int kLfoShapesKnown = sizeof(kLfoShapeNames) / sizeof(kLfoShapeNames[0]);
constexpr int kLfoDivisionCount = 16;
static const char *const kLfoDivisionNames[] = {"1/1",  "1/2",  "1/4",  "1/8",  "1/16", "1/32", "1/2.",
                                                "1/4.", "1/8.", "1/16.", "1/2T", "1/4T", "1/8T", "1/16T"};
// Beats (quarter notes) per LFO cycle of each division.
static const double kLfoDivisionBeats[] = {4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 3.0, 1.5, 0.75, 0.375,
                                           8.0 / 3.0, 4.0 / 3.0, 2.0 / 3.0, 1.0 / 3.0};
constexpr int kLfoDivisionsKnown = sizeof(kLfoDivisionNames) / sizeof(kLfoDivisionNames[0]);

// Per-slot mod routes: a source sends its value, times an amount (-100 ..
// +100 %), to a target. IDs leave room for 8 routes of 4 fields per slot;
// four run today. Both lists keep 16 entries, the unused ones reserved, and
// their numbers match the voice's (mla_sampler_dsp.mla).
constexpr int kRoutes = 4;
enum RouteField : int { kRouteSource = 0, kRouteTarget, kRouteAmount, kRouteFields };
constexpr int kRouteSlotStride = 32;
constexpr int kRouteStride = 4;
constexpr int kRouteListCount = 16;
static const char *const kRouteSourceNames[] = {"Off",      "LFO 1",      "LFO 2",      "Amp Env",
                                                "Filter Env", "Velocity", "Key",        "Mod Wheel",
                                                "Aftertouch", "Pitch Bend", "Mod CC"};
constexpr int kRouteSourcesKnown = sizeof(kRouteSourceNames) / sizeof(kRouteSourceNames[0]);
static const char *const kRouteTargetNames[] = {"Off", "Pitch", "Cutoff", "Resonance", "Level", "Pan", "Start"};
constexpr int kRouteTargetsKnown = sizeof(kRouteTargetNames) / sizeof(kRouteTargetNames[0]);
enum RouteSource : int {
    kSourceVelocity = 5,
    kSourceKey = 6,
    kSourceModWheel = 7,
    kSourceAftertouch = 8,
    kSourcePitchBend = 9, // -1 .. 1
    kSourceModCc = 10,
};

// The CCs the Mod CC source can read. A VST3 host asks a plugin once which
// parameter each CC drives, so every choice has its own value parameter.
constexpr int kModCcCount = 16;
static const int kModCcNumbers[] = {2, 4, 11, 16, 17, 18, 19, 74};
static const char *const kModCcNames[] = {"CC 2 Breath", "CC 4 Foot", "CC 11 Expression", "CC 16",
                                          "CC 17",       "CC 18",     "CC 19",            "CC 74 Brightness"};
constexpr int kModCcsKnown = sizeof(kModCcNumbers) / sizeof(kModCcNumbers[0]);
constexpr int kBendRangeMax = 24;
constexpr int kControllerParams = 5 + kModCcsKnown;
enum RouteTarget : int { kTargetStart = 6 };
// Full-amount range of each target in its units: semitones, octaves, dB,
// level share, pan, share of the sample.
static const double kRouteTargetRange[] = {0.0, 24.0, 8.0, 36.0, 1.0, 1.0, 1.0};

// The filter chain. IDs leave room for 4 stages of 8 fields per slot; the
// chain runs kFilterStages of kFilterFields today, and more of either only
// add parameters after the existing ones.
constexpr int kFilterStages = 4;
// The first two stages' fields were registered with the first filter block;
// stages 3 and 4 came later and are registered after the sends.
constexpr int kFilterStagesFirst = 2;
constexpr int kFilterFieldsLater = 6; // type .. gain of stages 3 and 4
// How the stages connect (8-entry list, see the voice's set_chain).
constexpr int kChainCount = 8;
static const char *const kChainNames[] = {"Serial", "Parallel", "2 x 2"};
constexpr int kChainsKnown = sizeof(kChainNames) / sizeof(kChainNames[0]);
// Tempo sync (8-entry list): Repitch plays the sample in its length of beats
// by changing its speed and pitch, Stretch by grains at its own pitch, Beats
// by playing each hit (see detectOnsets) whole at its stretched time.
constexpr int kSyncCount = 8;
static const char *const kSyncNames[] = {"Off", "Repitch", "Stretch", "Beats"};
constexpr int kSyncsKnown = sizeof(kSyncNames) / sizeof(kSyncNames[0]);
enum TempoSync : int { kSyncOff = 0, kSyncRepitch = 1, kSyncStretch = 2, kSyncBeats = 3 };
constexpr int kBeatsCount = 16;
static const double kBeats[] = {0.25, 0.5, 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64};
static const char *const kBeatNames[] = {"1/4", "1/2", "1", "2", "3", "4", "6", "8", "12", "16", "24", "32", "48", "64"};
constexpr int kBeatsKnown = sizeof(kBeats) / sizeof(kBeats[0]);
constexpr float kGrainSeconds = 0.04f;
// Velocity curves (8-entry list): Linear, Soft (sqrt: louder soft notes),
// Hard (squared: quieter soft notes), Fixed (every note at full).
constexpr int kCurveCount = 8;
static const char *const kCurveNames[] = {"Linear", "Soft", "Hard", "Fixed"};
constexpr int kCurvesKnown = sizeof(kCurveNames) / sizeof(kCurveNames[0]);
enum FilterField : int {
    kFilterType = 0,  // one of kFilterTypeNames, as a 64-entry list (see there)
    kFilterCutoff,    // 20 Hz .. 20 kHz
    kFilterResonance, // 0 .. 36 dB
    kFilterEnvAmount, // -8 .. +8 octaves of filter envelope
    kFilterKeyTrack,  // 0 .. 100 % of the key's distance from C-4
    kFilterGain,      // -24 .. +24 dB (peak and shelves); registered after the choke groups
    kFilterDrive,     // 0 .. 100 % saturation into the stage; registered after the curves
    kFilterMod,       // -100 .. +100 % of the LFO and route cutoff movement; likewise
    kFilterFields,
};
// The fields registered with the first filter block; later ones follow the
// blocks after it, so no earlier parameter moves.
constexpr int kFilterFieldsFirst = kFilterGain;
constexpr int kFilterSlotStride = 32;
constexpr int kFilterStageStride = 8;

// Filter types, as the voice numbers them (mla_sampler_dsp.mla). The list
// parameter always has 64 entries, so its normalized values never move:
// new filters take the next reserved number and name, older ones keep theirs.
constexpr int kFilterTypeCount = 64;
static const char *const kFilterTypeNames[] = {"Off",       "LP 12",     "LP 24",     "HP 12",     "HP 24",
                                               "BP 12",     "BP 24",     "Ladder 12", "Ladder 24", "Notch",
                                               "SVF LP",    "SVF HP",    "SVF BP",    "SVF Notch", "Peak",
                                               "Low Shelf", "High Shelf", "Vowel",      "Comb +",   "Comb -",
                                               "Flanger",   "Phaser"};
constexpr int kFilterTypesKnown = sizeof(kFilterTypeNames) / sizeof(kFilterTypeNames[0]);

constexpr int kNumGroups = 8;
// Unison: up to 8 voices per note, their list fixed at 16 entries so saved
// values never move. Play modes likewise (8 entries).
constexpr int kUnisonMax = 8;
constexpr int kUnisonListCount = 16;
constexpr int kPlayModeCount = 8;
static const char *const kPlayModeNames[] = {"Poly", "Mono", "Legato"};
constexpr int kPlayModesKnown = sizeof(kPlayModeNames) / sizeof(kPlayModeNames[0]);
enum PlayMode : int { kPlayPoly = 0, kPlayMono = 1, kPlayLegato = 2 };
constexpr int kHeldMax = 16; // keys a mono or legato slot remembers
constexpr int kNumChokeGroups = 8;
constexpr float kChokeSeconds = 0.003f; // short but click-free, as Mla Drum's

// Per-slot envelope offsets from kEnvelopeParamBase + s * kParamsPerEnvelope.
enum EnvelopeParam : int {
    kEnvelopeOwn = 0, // Instance: the global ADSR. Own: the four below.
    kEnvelopeAttack,
    kEnvelopeDecay,
    kEnvelopeSustain,
    kEnvelopeRelease,
    kParamsPerEnvelope,
};

// Per-slot parameter offsets from kSlotParamBase + s * kParamsPerSlot.
enum SlotParam : int {
    kSlotLevel = 0,
    kSlotPan,
    kSlotTune,
    kSlotOutput,
    kSlotLoop,
    kSlotLoopStart,
    kSlotLoopEnd,
    kParamsPerSlot,
};

// Key zone offsets from kZoneParamBase + s * kParamsPerZone. A second block,
// so the parameters above keep their IDs and indexes.
enum ZoneParam : int {
    kZoneMode = 0, // Pad: one key at Root Key + slot. Zone: Low..High.
    kZoneLow,
    kZoneHigh,
    kZoneRoot,     // Key that plays the sample at its recorded pitch.
    kZoneTrack,    // Off: every key in the zone plays the recorded pitch.
    kParamsPerZone,
};

constexpr int kNumGlobalParams = 8;
constexpr int kNumSlotParams = kNumSlots * kParamsPerSlot;
constexpr int kNumZoneParams = kNumSlots * kParamsPerZone;
constexpr int kNumVelocityParams = kNumSlots * 2;
constexpr int kNumEnvelopeParams = kNumSlots * kParamsPerEnvelope;
constexpr int kNumParams = kNumGlobalParams + kNumSlotParams + kNumZoneParams + kNumSlots + kNumVelocityParams +
                            kNumEnvelopeParams + kNumSlots + kNumSlots + 1 + 4 + kNumSlots * kParamsPerEnvelope +
                            kNumSlots * kFilterStagesFirst * kFilterFieldsFirst + kNumSlots + kNumSlots * kFilterStagesFirst +
                            kNumSlots * kLfos * kLfoFields + kNumSlots * kRoutes * kRouteFields + kNumSlots +
                            kNumSlots * 3 + kNumSlots * 2 + kControllerParams + kNumSlots * 3 + kNumSlots * kNumSends +
                            kNumSlots * (kFilterStages - kFilterStagesFirst) * kFilterFieldsLater + kNumSlots * 3 + kNumSlots * 3 +
                            kNumSlots * kFilterStages * 2;
constexpr ParamID kMaxParamId = kRouteParamBase + kNumSlots * kRouteSlotStride;

// Flat index <-> ParamID, in the order parameters are registered: globals,
// slot parameters, key zones, crossfades, velocity ranges, envelopes, sample
// starts, groups, group mode, the instance filter envelope, slot filter
// envelopes, filter stages, choke groups, filter gains, LFO 1, LFO 2, mod
// routes, reverse, unison, play mode, the MIDI controllers, pitch
// envelopes and sends. Each block was appended after the
// ones before it, so saved states keep their meaning.
struct ParamLayout {
    ParamID ids[kNumParams];
    int16_t index[kMaxParamId];
    ParamLayout()
    {
        std::fill(std::begin(index), std::end(index), static_cast<int16_t>(-1));
        int n = 0;
        const auto add = [&](int id) {
            index[id] = static_cast<int16_t>(n);
            ids[n++] = static_cast<ParamID>(id);
        };
        const auto run = [&](int base, int count) {
            for(int i = 0; i < count; ++i)
                add(base + i);
        };
        run(kLevelParam, kNumGlobalParams);
        run(kSlotParamBase, kNumSlotParams);
        run(kZoneParamBase, kNumZoneParams);
        run(kCrossfadeParamBase, kNumSlots);
        run(kVelocityParamBase, kNumVelocityParams);
        run(kEnvelopeParamBase, kNumEnvelopeParams);
        run(kStartParamBase, kNumSlots);
        run(kGroupParamBase, kNumSlots);
        add(kGroupModeParam);
        run(kFilterEnvelopeParamBase, 4);
        run(kSlotFilterEnvelopeParamBase, kNumSlots * kParamsPerEnvelope);
        for(int slot = 0; slot < kNumSlots; ++slot)
            for(int stage = 0; stage < kFilterStagesFirst; ++stage)
                run(kFilterParamBase + slot * kFilterSlotStride + stage * kFilterStageStride, kFilterFieldsFirst);
        run(kChokeParamBase, kNumSlots);
        for(int slot = 0; slot < kNumSlots; ++slot)
            for(int stage = 0; stage < kFilterStagesFirst; ++stage)
                add(kFilterParamBase + slot * kFilterSlotStride + stage * kFilterStageStride + kFilterGain);
        for(int lfo = 0; lfo < kLfos; ++lfo)
            for(int slot = 0; slot < kNumSlots; ++slot)
                run(kLfoParamBase + slot * kLfoSlotStride + lfo * kLfoStride, kLfoFields);
        for(int slot = 0; slot < kNumSlots; ++slot)
            for(int route = 0; route < kRoutes; ++route)
                run(kRouteParamBase + slot * kRouteSlotStride + route * kRouteStride, kRouteFields);
        run(kReverseParamBase, kNumSlots);
        for(int slot = 0; slot < kNumSlots; ++slot)
            run(kUnisonParamBase + slot * 4, 3);
        for(int slot = 0; slot < kNumSlots; ++slot)
            run(kPlayParamBase + slot * 4, 2);
        run(kBendRangeParam, 5);
        run(kCcValueParamBase, kModCcsKnown);
        for(int slot = 0; slot < kNumSlots; ++slot)
            run(kPitchEnvParamBase + slot * 4, 3);
        for(int slot = 0; slot < kNumSlots; ++slot)
            run(kSendParamBase + slot * 4, kNumSends);
        for(int slot = 0; slot < kNumSlots; ++slot)
            for(int stage = kFilterStagesFirst; stage < kFilterStages; ++stage)
                run(kFilterParamBase + slot * kFilterSlotStride + stage * kFilterStageStride, kFilterFieldsLater);
        for(int slot = 0; slot < kNumSlots; ++slot)
            run(kChainParamBase + slot * 4, 3);
        for(int slot = 0; slot < kNumSlots; ++slot)
            run(kCurveParamBase + slot * 4, 3);
        for(int slot = 0; slot < kNumSlots; ++slot)
            for(int stage = 0; stage < kFilterStages; ++stage)
                run(kFilterParamBase + slot * kFilterSlotStride + stage * kFilterStageStride + kFilterDrive, 2);
    }
};
static const ParamLayout kLayout;

static ParamID paramIdAt(int index) { return kLayout.ids[index]; }
static int indexOf(ParamID id) { return id < kMaxParamId ? kLayout.index[id] : -1; }

static ParamID lfoParamId(int slot, int lfo, LfoField field)
{
    return static_cast<ParamID>(kLfoParamBase + slot * kLfoSlotStride + lfo * kLfoStride + field);
}

static ParamID routeParamId(int slot, int route, RouteField field)
{
    return static_cast<ParamID>(kRouteParamBase + slot * kRouteSlotStride + route * kRouteStride + field);
}

static ParamID filterParamId(int slot, int stage, FilterField field)
{
    return static_cast<ParamID>(kFilterParamBase + slot * kFilterSlotStride + stage * kFilterStageStride + field);
}

static ParamID slotParamId(int slot, SlotParam param)
{
    return static_cast<ParamID>(kSlotParamBase + slot * kParamsPerSlot + param);
}

static ParamID envelopeParamId(int slot, EnvelopeParam param)
{
    return static_cast<ParamID>(kEnvelopeParamBase + slot * kParamsPerEnvelope + param);
}

static ParamID zoneParamId(int slot, ZoneParam param)
{
    return static_cast<ParamID>(kZoneParamBase + slot * kParamsPerZone + param);
}

// --- Normalized -> physical mappings ----------------------------------------
constexpr double kUnityLevelNorm = 60.0 / 66.0;
constexpr double kBendCentre = 8192.0 / 16383.0; // Pitch Bend at rest

static float gainFromNorm(double norm)
{
    // -60 dB .. +6 dB; the bottom of the range is silence.
    if(norm <= 0.0)
        return 0.0f;
    const double db = -60.0 + norm * 66.0;
    return static_cast<float>(std::pow(10.0, db / 20.0));
}

static double semitonesFromNorm(double norm) { return norm * 48.0 - 24.0; } // +-2 octaves
static float pitchEnvFromNorm(double norm) { return static_cast<float>(norm * 96.0 - 48.0); } // +-4 octaves
static float panFromNorm(double norm) { return static_cast<float>(norm * 2.0 - 1.0); }
static float attackFromNorm(double norm) { return static_cast<float>(2.0 * norm * norm * norm); } // 0..2 s
static float timeFromNorm(double norm) { return static_cast<float>(0.001 * std::pow(10000.0, norm)); } // 1 ms..10 s
static double normFromTime(double seconds) { return std::log10(seconds / 0.001) / 4.0; }
static int rootKeyFromNorm(double norm) { return static_cast<int>(std::lround(norm * 127.0)); }
static int outputFromNorm(double norm) { return static_cast<int>(std::lround(norm * (kNumOutputs - 1))); }
static int32_t loopFromNorm(double norm) { return static_cast<int32_t>(std::lround(norm * 2.0)); }
static int32_t filterTypeFromNorm(double norm) { return static_cast<int32_t>(std::lround(norm * (kFilterTypeCount - 1))); }
static float cutoffFromNorm(double norm) { return static_cast<float>(20.0 * std::pow(1000.0, norm)); } // 20 Hz..20 kHz
static float resonanceFromNorm(double norm) { return static_cast<float>(norm * 36.0); }             // 0..36 dB
static float envOctavesFromNorm(double norm) { return static_cast<float>((norm * 2.0 - 1.0) * 8.0); } // +-8 octaves
static float filterGainFromNorm(double norm) { return static_cast<float>((norm * 2.0 - 1.0) * 24.0); } // +-24 dB
static float lfoRateFromNorm(double norm) { return static_cast<float>(0.05 * std::pow(400.0, norm)); }  // 0.05..20 Hz
static double normFromLfoRate(double hz) { return std::log(hz / 0.05) / std::log(400.0); }

// Immutable decoded sample: stereo interleaved with one silent guard frame at
// the end, so interpolation may always read the frame after the last one.
struct Sample {
    std::vector<float> stereo;
    int64_t frames = 0;
    double rate = 44100.0;
    std::string name;
    // Where its hits start, in frames, frame 0 first, as detected when it
    // loaded (see detectOnsets). A slot's markers start as these.
    std::vector<double> onsets;
};

// A slot's slice markers: where its hits start (frames, ascending, the first
// 0), the segments Beats tempo sync plays. Detected with the sample, or set
// by the host (mla_sampler_protocol.h kMarkersMessage); published to the
// audio thread like samples.
struct Markers {
    std::vector<double> frames;
};

// A sample's hits: RMS per 256-frame hop, and the strongest rises in it (at
// least 15 % of the strongest, local peaks), taken strongest first at least
// 50 ms apart. Each moves to its onset, the first frame around the rise at
// 30 % of the peak there, then back to the quietest of the 64 frames before.
// Frame 0 always starts the first segment. Runs when a sample loads.
static std::vector<double> detectOnsets(const std::vector<float> &stereo, int64_t frames, double rate)
{
    std::vector<double> onsets{0.0};
    constexpr int64_t hop = 256;
    const int64_t hops = frames / hop;
    if(hops < 3)
        return onsets;
    const auto level = [&](int64_t f) {
        const float v = (stereo[static_cast<size_t>(f) * 2u] + stereo[static_cast<size_t>(f) * 2u + 1u]) * 0.5f;
        return std::fabs(v);
    };
    std::vector<float> rms(static_cast<size_t>(hops)), rise(static_cast<size_t>(hops), 0.0f);
    for(int64_t h = 0; h < hops; ++h) {
        double sum = 0.0;
        for(int64_t f = h * hop; f < (h + 1) * hop; ++f)
            sum += static_cast<double>(level(f)) * level(f);
        rms[h] = static_cast<float>(std::sqrt(sum / hop));
    }
    float strongest = 0.0f;
    for(int64_t h = 1; h < hops; ++h) {
        rise[h] = std::max(0.0f, rms[h] - rms[h - 1]);
        strongest = std::max(strongest, rise[h]);
    }
    if(strongest <= 1e-4f)
        return onsets;
    std::vector<int64_t> candidates;
    for(int64_t h = 1; h < hops; ++h)
        if(rise[h] >= strongest * 0.15f && rise[h] >= rise[h - 1] && (h + 1 >= hops || rise[h] > rise[h + 1]))
            candidates.push_back(h);
    std::sort(candidates.begin(), candidates.end(), [&](int64_t a, int64_t b) { return rise[a] > rise[b]; });
    const int64_t spacing = std::max<int64_t>(1, static_cast<int64_t>(rate * 0.05) / hop);
    std::vector<int64_t> taken;
    for(const int64_t h : candidates) {
        bool near = false;
        for(const int64_t t : taken)
            near = near || std::llabs(t - h) < spacing;
        if(!near)
            taken.push_back(h);
    }
    for(const int64_t h : taken) {
        const int64_t lo = (h - 1) * hop, hi = std::min(frames, (h + 2) * hop);
        float peak = 0.0f;
        for(int64_t f = lo; f < hi; ++f)
            peak = std::max(peak, level(f));
        int64_t onset = h * hop;
        for(int64_t f = lo; f < hi; ++f)
            if(level(f) >= peak * 0.3f) {
                onset = f;
                break;
            }
        int64_t quiet = onset;
        float lowest = 2.0f;
        for(int64_t f = std::max(lo, onset - 64); f <= onset; ++f)
            if(level(f) <= lowest) {
                lowest = level(f);
                quiet = f;
            }
        if(quiet >= 64)
            onsets.push_back(static_cast<double>(quiet));
    }
    std::sort(onsets.begin(), onsets.end());
    return onsets;
}

static std::unique_ptr<Sample> makeSample(const float *data, int64_t frames, int channels, double rate,
                                          std::string name)
{
    auto sample = std::make_unique<Sample>();
    sample->frames = frames;
    sample->rate = rate;
    sample->name = std::move(name);
    sample->stereo.resize(static_cast<size_t>(frames + 1) * 2u, 0.0f);
    for(int64_t f = 0; f < frames; ++f) {
        const float left = data[f * channels];
        const float right = channels == 2 ? data[f * channels + 1] : left;
        sample->stereo[static_cast<size_t>(f) * 2u] = std::isfinite(left) ? left : 0.0f;
        sample->stereo[static_cast<size_t>(f) * 2u + 1u] = std::isfinite(right) ? right : 0.0f;
    }
    sample->onsets = detectOnsets(sample->stereo, frames, rate);
    return sample;
}

static bool validFormat(int64_t frames, int64_t channels, double rate)
{
    return frames >= 1 && frames <= mla_sampler::kMaxFrames && (channels == 1 || channels == 2) &&
           std::isfinite(rate) && rate >= 1000.0 && rate <= 384000.0;
}

static std::string baseName(const std::string &path)
{
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

static std::unique_ptr<Sample> decodeFile(const std::string &path, std::string &error)
{
    const int64_t handle = __mlang_std_audio_pcm_load(path.c_str());
    if(handle == 0) {
        const char *why = __mlang_std_audio_last_error();
        error = (why && *why) ? why : "could not decode audio file";
        return nullptr;
    }
    const int64_t rate = __mlang_std_audio_pcm_file_sample_rate(handle);
    const int64_t channels = __mlang_std_audio_pcm_file_channels(handle);
    const int64_t frames = __mlang_std_audio_pcm_file_frame_count(handle);
    std::unique_ptr<Sample> sample;
    if(!validFormat(frames, channels, static_cast<double>(rate))) {
        error = "unsupported sample length, channel count or rate";
    } else {
        MlangList samples = __mlang_std_audio_pcm_file_samples(handle);
        if(samples.data && samples.size == frames * channels)
            sample = makeSample(static_cast<const float *>(samples.data), frames, static_cast<int>(channels),
                                static_cast<double>(rate), baseName(path));
        else
            error = "could not read decoded samples";
        std::free(samples.data);
    }
    __mlang_std_audio_pcm_file_close(handle);
    return sample;
}

static std::string binaryString(IAttributeList *attributes, IAttributeList::AttrID id)
{
    const void *data = nullptr;
    uint32 size = 0;
    if(attributes->getBinary(id, data, size) != kResultOk || data == nullptr)
        return {};
    return std::string(static_cast<const char *>(data), size);
}

static std::u16string utf16(const std::string &text) { return std::u16string(text.begin(), text.end()); }

class Processor final : public SingleComponentEffect, public IMidiMapping {
  public:
    Processor()
    {
        for(auto &slot : published_)
            slot.store(nullptr, std::memory_order_relaxed);
        for(auto &value : norm_)
            value.store(0.0, std::memory_order_relaxed);
        for(int bus = 0; bus < kNumBuses; ++bus)
            outputActive_[bus].store(bus == 0, std::memory_order_relaxed);
    }

    ~Processor() override
    {
        destroyVoices();
    }

    // SingleComponentEffect hides IConnectionPoint ("no need to expose it to
    // the host"); expose it again so hosts can send the sampler messages.
    DEFINE_INTERFACES
        DEF_INTERFACE(IMidiMapping)
        DEF_INTERFACE(IConnectionPoint)
    END_DEFINE_INTERFACES(SingleComponentEffect)
    REFCOUNT_METHODS(SingleComponentEffect)

    tresult PLUGIN_API connect(IConnectionPoint *other) SMTG_OVERRIDE
    {
        // A host pairing the component with its own controller would connect
        // this object to itself; accept without keeping a self-reference.
        if(other == static_cast<IConnectionPoint *>(this))
            return kResultTrue;
        return SingleComponentEffect::connect(other);
    }

    tresult PLUGIN_API disconnect(IConnectionPoint *other) SMTG_OVERRIDE
    {
        if(other == static_cast<IConnectionPoint *>(this))
            return kResultTrue;
        return SingleComponentEffect::disconnect(other);
    }

    static FUnknown *createInstance(void *) { return static_cast<IComponent *>(new Processor()); }

    tresult PLUGIN_API initialize(FUnknown *context) SMTG_OVERRIDE
    {
        const tresult result = SingleComponentEffect::initialize(context);
        if(result != kResultOk)
            return result;

        addAudioOutput(STR16("Main"), SpeakerArr::kStereo);
        // Aux buses start inactive, as VST3 expects; hosts enable what they route.
        for(int bus = 1; bus < kNumBuses; ++bus)
            addAudioOutput(utf16(outputName(bus)).c_str(), SpeakerArr::kStereo, BusTypes::kAux, 0);
        addEventInput(STR16("MIDI In"), 16);

        addParam(kLevelParam, STR16("Level"), STR16("dB"), kUnityLevelNorm);
        addParam(kTuneParam, STR16("Tune"), STR16("st"), 0.5);
        addParam(kVelocityParam, STR16("Velocity"), nullptr, 1.0);
        parameters.addParameter(STR16("Root Key"), nullptr, 127, 36.0 / 127.0, ParameterInfo::kCanAutomate,
                                kRootKeyParam);
        norm_[indexOf(kRootKeyParam)].store(36.0 / 127.0);
        addParam(kAttackParam, STR16("Attack"), STR16("s"), 0.0);
        addParam(kDecayParam, STR16("Decay"), STR16("s"), normFromTime(0.5));
        addParam(kSustainParam, STR16("Sustain"), nullptr, 1.0);
        addParam(kReleaseParam, STR16("Release"), STR16("s"), normFromTime(0.1));

        for(int slot = 0; slot < kNumSlots; ++slot) {
            addParam(slotParamId(slot, kSlotLevel), slotTitle(slot, "Level").c_str(), STR16("dB"), kUnityLevelNorm);
            addParam(slotParamId(slot, kSlotPan), slotTitle(slot, "Pan").c_str(), nullptr, 0.5);
            addParam(slotParamId(slot, kSlotTune), slotTitle(slot, "Tune").c_str(), STR16("st"), 0.5);

            auto *output = addList(slotParamId(slot, kSlotOutput), slotTitle(slot, "Output"));
            for(int bus = 0; bus < kNumOutputs; ++bus)
                output->appendString(utf16(outputName(bus)).c_str());

            auto *loop = addList(slotParamId(slot, kSlotLoop), slotTitle(slot, "Loop"));
            loop->appendString(STR16("Off"));
            loop->appendString(STR16("Forward"));
            loop->appendString(STR16("Bidirectional"));

            addParam(slotParamId(slot, kSlotLoopStart), slotTitle(slot, "Loop Start").c_str(), nullptr, 0.0);
            addParam(slotParamId(slot, kSlotLoopEnd), slotTitle(slot, "Loop End").c_str(), nullptr, 1.0);
        }
        // Key zones come last, so the parameters above keep their indexes.
        for(int slot = 0; slot < kNumSlots; ++slot) {
            auto *mode = addList(zoneParamId(slot, kZoneMode), slotTitle(slot, "Key Mode"));
            mode->appendString(STR16("Pad"));
            mode->appendString(STR16("Zone"));
            addKey(zoneParamId(slot, kZoneLow), slotTitle(slot, "Low Key"), 0);
            addKey(zoneParamId(slot, kZoneHigh), slotTitle(slot, "High Key"), 127);
            addKey(zoneParamId(slot, kZoneRoot), slotTitle(slot, "Zone Root"), 60);
            auto *track = addList(zoneParamId(slot, kZoneTrack), slotTitle(slot, "Key Track"));
            track->appendString(STR16("Off"));
            track->appendString(STR16("On"));
            setListDefault(track, zoneParamId(slot, kZoneTrack), 1.0);
        }
        // Loop crossfades come after the zones, again keeping indexes.
        for(int slot = 0; slot < kNumSlots; ++slot)
            addParam(static_cast<ParamID>(kCrossfadeParamBase + slot), slotTitle(slot, "Crossfade").c_str(), nullptr, 0.0);
        // Velocity ranges last: a slot plays only notes this hard.
        for(int slot = 0; slot < kNumSlots; ++slot) {
            addKey(static_cast<ParamID>(kVelocityParamBase + slot * 2), slotTitle(slot, "Vel Low"), 1);
            addKey(static_cast<ParamID>(kVelocityParamBase + slot * 2 + 1), slotTitle(slot, "Vel High"), 127);
        }
        // Per-slot envelopes last. Defaults match the instance envelope's.
        for(int slot = 0; slot < kNumSlots; ++slot) {
            auto *own = addList(envelopeParamId(slot, kEnvelopeOwn), slotTitle(slot, "Envelope"));
            own->appendString(STR16("Instance"));
            own->appendString(STR16("Own"));
            addParam(envelopeParamId(slot, kEnvelopeAttack), slotTitle(slot, "Attack").c_str(), STR16("s"), 0.0);
            addParam(envelopeParamId(slot, kEnvelopeDecay), slotTitle(slot, "Decay").c_str(), STR16("s"), normFromTime(0.5));
            addParam(envelopeParamId(slot, kEnvelopeSustain), slotTitle(slot, "Sustain").c_str(), nullptr, 1.0);
            addParam(envelopeParamId(slot, kEnvelopeRelease), slotTitle(slot, "Release").c_str(), STR16("s"), normFromTime(0.1));
        }
        // Sample starts last.
        for(int slot = 0; slot < kNumSlots; ++slot)
            addParam(static_cast<ParamID>(kStartParamBase + slot), slotTitle(slot, "Start").c_str(), nullptr, 0.0);
        // Groups, then the one group mode, last of all.
        for(int slot = 0; slot < kNumSlots; ++slot) {
            auto *group = addList(static_cast<ParamID>(kGroupParamBase + slot), slotTitle(slot, "Group"));
            group->appendString(STR16("Off"));
            for(int g = 1; g <= kNumGroups; ++g)
                group->appendString(utf16(std::to_string(g)).c_str());
        }
        auto *groupMode = addList(kGroupModeParam, u"Group Mode");
        groupMode->appendString(STR16("Round-robin"));
        groupMode->appendString(STR16("Random"));
        // The instance filter envelope, then each slot's own, then the filter
        // chains. A filter envelope defaults to a pluck (no sustain); it moves
        // the cutoff only as far as a stage's Env amount says.
        addParam(kFilterEnvelopeParamBase, STR16("Filter Attack"), STR16("s"), 0.0);
        addParam(kFilterEnvelopeParamBase + 1, STR16("Filter Decay"), STR16("s"), normFromTime(0.5));
        addParam(kFilterEnvelopeParamBase + 2, STR16("Filter Sustain"), nullptr, 0.0);
        addParam(kFilterEnvelopeParamBase + 3, STR16("Filter Release"), STR16("s"), normFromTime(0.1));
        for(int slot = 0; slot < kNumSlots; ++slot) {
            const auto id = [&](EnvelopeParam k) { return static_cast<ParamID>(kSlotFilterEnvelopeParamBase + slot * kParamsPerEnvelope + k); };
            auto *own = addList(id(kEnvelopeOwn), slotTitle(slot, "Filter Envelope"));
            own->appendString(STR16("Instance"));
            own->appendString(STR16("Own"));
            addParam(id(kEnvelopeAttack), slotTitle(slot, "Filter Attack").c_str(), STR16("s"), 0.0);
            addParam(id(kEnvelopeDecay), slotTitle(slot, "Filter Decay").c_str(), STR16("s"), normFromTime(0.5));
            addParam(id(kEnvelopeSustain), slotTitle(slot, "Filter Sustain").c_str(), nullptr, 0.0);
            addParam(id(kEnvelopeRelease), slotTitle(slot, "Filter Release").c_str(), STR16("s"), normFromTime(0.1));
        }
        for(int slot = 0; slot < kNumSlots; ++slot)
            for(int stage = 0; stage < kFilterStagesFirst; ++stage) {
                const std::string name = "Filter " + std::to_string(stage + 1) + " ";
                auto *type = addList(filterParamId(slot, stage, kFilterType), slotTitle(slot, (name + "Type").c_str()));
                for(int t = 0; t < kFilterTypeCount; ++t)
                    type->appendString(utf16(t < kFilterTypesKnown ? kFilterTypeNames[t] : "(reserved)").c_str());
                addParam(filterParamId(slot, stage, kFilterCutoff), slotTitle(slot, (name + "Cutoff").c_str()).c_str(), STR16("Hz"), 1.0);
                addParam(filterParamId(slot, stage, kFilterResonance), slotTitle(slot, (name + "Resonance").c_str()).c_str(), STR16("dB"), 0.0);
                addParam(filterParamId(slot, stage, kFilterEnvAmount), slotTitle(slot, (name + "Env").c_str()).c_str(), STR16("oct"), 0.5);
                addParam(filterParamId(slot, stage, kFilterKeyTrack), slotTitle(slot, (name + "Key Track").c_str()).c_str(), nullptr, 0.0);
            }
        // Choke groups last.
        for(int slot = 0; slot < kNumSlots; ++slot) {
            auto *choke = addList(static_cast<ParamID>(kChokeParamBase + slot), slotTitle(slot, "Choke"));
            choke->appendString(STR16("Off"));
            for(int g = 1; g <= kNumChokeGroups; ++g)
                choke->appendString(utf16(std::to_string(g)).c_str());
        }
        // Filter gains (peak and shelves) last.
        for(int slot = 0; slot < kNumSlots; ++slot)
            for(int stage = 0; stage < kFilterStagesFirst; ++stage)
                addParam(filterParamId(slot, stage, kFilterGain),
                         slotTitle(slot, ("Filter " + std::to_string(stage + 1) + " Gain").c_str()).c_str(), STR16("dB"), 0.5);
        // LFOs last. Depths default to 0, so an LFO does nothing until used.
        for(int lfo = 0; lfo < kLfos; ++lfo)
            for(int slot = 0; slot < kNumSlots; ++slot) {
                const std::string name = "LFO " + std::to_string(lfo + 1) + " ";
                const auto title = [&](const char *what) { return slotTitle(slot, (name + what).c_str()); };
                auto *shape = addList(lfoParamId(slot, lfo, kLfoShape), title("Shape"));
                for(int k = 0; k < kLfoShapeCount; ++k)
                    shape->appendString(utf16(k < kLfoShapesKnown ? kLfoShapeNames[k] : "(reserved)").c_str());
                addParam(lfoParamId(slot, lfo, kLfoRate), title("Rate").c_str(), STR16("Hz"), normFromLfoRate(5.0));
                auto *sync = addList(lfoParamId(slot, lfo, kLfoSync), title("Sync"));
                sync->appendString(STR16("Off"));
                sync->appendString(STR16("On"));
                auto *division = addList(lfoParamId(slot, lfo, kLfoDivision), title("Division"));
                for(int k = 0; k < kLfoDivisionCount; ++k)
                    division->appendString(utf16(k < kLfoDivisionsKnown ? kLfoDivisionNames[k] : "(reserved)").c_str());
                setListDefault(division, lfoParamId(slot, lfo, kLfoDivision), 2.0 / (kLfoDivisionCount - 1)); // 1/4
                addParam(lfoParamId(slot, lfo, kLfoDelay), title("Delay").c_str(), STR16("s"), 0.0);
                addParam(lfoParamId(slot, lfo, kLfoPitch), title("Pitch").c_str(), STR16("st"), 0.5);
                addParam(lfoParamId(slot, lfo, kLfoCutoff), title("Cutoff").c_str(), STR16("oct"), 0.5);
                addParam(lfoParamId(slot, lfo, kLfoLevel), title("Level").c_str(), nullptr, 0.0);
                auto *trigger = addList(lfoParamId(slot, lfo, kLfoTrigger), title("Trigger"));
                trigger->appendString(STR16("Free"));
                trigger->appendString(STR16("Retrigger"));
                setListDefault(trigger, lfoParamId(slot, lfo, kLfoTrigger), 1.0);
            }
        // Mod routes last: off until a source and target are chosen.
        for(int slot = 0; slot < kNumSlots; ++slot)
            for(int route = 0; route < kRoutes; ++route) {
                const std::string name = "Mod " + std::to_string(route + 1) + " ";
                auto *source = addList(routeParamId(slot, route, kRouteSource), slotTitle(slot, (name + "Source").c_str()));
                for(int k = 0; k < kRouteListCount; ++k)
                    source->appendString(utf16(k < kRouteSourcesKnown ? kRouteSourceNames[k] : "(reserved)").c_str());
                auto *target = addList(routeParamId(slot, route, kRouteTarget), slotTitle(slot, (name + "Target").c_str()));
                for(int k = 0; k < kRouteListCount; ++k)
                    target->appendString(utf16(k < kRouteTargetsKnown ? kRouteTargetNames[k] : "(reserved)").c_str());
                addParam(routeParamId(slot, route, kRouteAmount), slotTitle(slot, (name + "Amount").c_str()).c_str(), nullptr, 0.5);
            }
        // Reverse last.
        for(int slot = 0; slot < kNumSlots; ++slot) {
            auto *reverse = addList(static_cast<ParamID>(kReverseParamBase + slot), slotTitle(slot, "Reverse"));
            reverse->appendString(STR16("Off"));
            reverse->appendString(STR16("On"));
        }
        // Unison, then play mode and glide.
        for(int slot = 0; slot < kNumSlots; ++slot) {
            auto *voices = addList(static_cast<ParamID>(kUnisonParamBase + slot * 4), slotTitle(slot, "Unison"));
            for(int k = 0; k < kUnisonListCount; ++k)
                voices->appendString(utf16(k < kUnisonMax ? std::to_string(k + 1) : "(reserved)").c_str());
            addParam(static_cast<ParamID>(kUnisonParamBase + slot * 4 + 1), slotTitle(slot, "Detune").c_str(), STR16("ct"), 0.0);
            addParam(static_cast<ParamID>(kUnisonParamBase + slot * 4 + 2), slotTitle(slot, "Spread").c_str(), nullptr, 0.0);
        }
        for(int slot = 0; slot < kNumSlots; ++slot) {
            auto *mode = addList(static_cast<ParamID>(kPlayParamBase + slot * 4), slotTitle(slot, "Play Mode"));
            for(int k = 0; k < kPlayModeCount; ++k)
                mode->appendString(utf16(k < kPlayModesKnown ? kPlayModeNames[k] : "(reserved)").c_str());
            addParam(static_cast<ParamID>(kPlayParamBase + slot * 4 + 1), slotTitle(slot, "Glide").c_str(), STR16("s"), 0.0);
        }
        // MIDI controllers last: the bend range and Mod CC choice, then the
        // values the host sends (getMidiControllerAssignment).
        parameters.addParameter(STR16("Bend Range"), STR16("st"), kBendRangeMax, 2.0 / kBendRangeMax,
                                ParameterInfo::kCanAutomate, kBendRangeParam);
        norm_[indexOf(kBendRangeParam)].store(2.0 / kBendRangeMax);
        auto *modCc = addList(kModCcParam, u"Mod CC");
        for(int k = 0; k < kModCcCount; ++k)
            modCc->appendString(utf16(k < kModCcsKnown ? kModCcNames[k] : "(reserved)").c_str());
        addParam(kModWheelParam, STR16("Mod Wheel"), nullptr, 0.0);
        addParam(kAftertouchParam, STR16("Aftertouch"), nullptr, 0.0);
        addParam(kPitchBendParam, STR16("Pitch Bend"), nullptr, kBendCentre);
        for(int k = 0; k < kModCcsKnown; ++k)
            addParam(static_cast<ParamID>(kCcValueParamBase + k), utf16(kModCcNames[k]).c_str(), nullptr, 0.0);
        // Pitch envelopes (off at depth 0), then sends, last.
        for(int slot = 0; slot < kNumSlots; ++slot) {
            const auto id = [&](int field) { return static_cast<ParamID>(kPitchEnvParamBase + slot * 4 + field); };
            addParam(id(0), slotTitle(slot, "Pitch Env").c_str(), STR16("st"), 0.5);
            addParam(id(1), slotTitle(slot, "Pitch Attack").c_str(), STR16("s"), 0.0);
            addParam(id(2), slotTitle(slot, "Pitch Decay").c_str(), STR16("s"), normFromTime(0.1));
        }
        for(int slot = 0; slot < kNumSlots; ++slot) {
            addParam(static_cast<ParamID>(kSendParamBase + slot * 4), slotTitle(slot, "Send A").c_str(), nullptr, 0.0);
            addParam(static_cast<ParamID>(kSendParamBase + slot * 4 + 1), slotTitle(slot, "Send B").c_str(), nullptr, 0.0);
        }
        // Filter stages 3 and 4, off like the first two.
        for(int slot = 0; slot < kNumSlots; ++slot)
            for(int stage = kFilterStagesFirst; stage < kFilterStages; ++stage) {
                const std::string name = "Filter " + std::to_string(stage + 1) + " ";
                auto *type = addList(filterParamId(slot, stage, kFilterType), slotTitle(slot, (name + "Type").c_str()));
                for(int t = 0; t < kFilterTypeCount; ++t)
                    type->appendString(utf16(t < kFilterTypesKnown ? kFilterTypeNames[t] : "(reserved)").c_str());
                addParam(filterParamId(slot, stage, kFilterCutoff), slotTitle(slot, (name + "Cutoff").c_str()).c_str(), STR16("Hz"), 1.0);
                addParam(filterParamId(slot, stage, kFilterResonance), slotTitle(slot, (name + "Resonance").c_str()).c_str(), STR16("dB"), 0.0);
                addParam(filterParamId(slot, stage, kFilterEnvAmount), slotTitle(slot, (name + "Env").c_str()).c_str(), STR16("oct"), 0.5);
                addParam(filterParamId(slot, stage, kFilterKeyTrack), slotTitle(slot, (name + "Key Track").c_str()).c_str(), nullptr, 0.0);
                addParam(filterParamId(slot, stage, kFilterGain), slotTitle(slot, (name + "Gain").c_str()).c_str(), STR16("dB"), 0.5);
            }
        // The chain routing and tempo sync, then velocity and key curves.
        for(int slot = 0; slot < kNumSlots; ++slot) {
            auto *chain = addList(static_cast<ParamID>(kChainParamBase + slot * 4), slotTitle(slot, "Filter Chain"));
            for(int k = 0; k < kChainCount; ++k)
                chain->appendString(utf16(k < kChainsKnown ? kChainNames[k] : "(reserved)").c_str());
            auto *sync = addList(static_cast<ParamID>(kChainParamBase + slot * 4 + 1), slotTitle(slot, "Tempo Sync"));
            for(int k = 0; k < kSyncCount; ++k)
                sync->appendString(utf16(k < kSyncsKnown ? kSyncNames[k] : "(reserved)").c_str());
            auto *beats = addList(static_cast<ParamID>(kChainParamBase + slot * 4 + 2), slotTitle(slot, "Beats"));
            for(int k = 0; k < kBeatsCount; ++k)
                beats->appendString(utf16(k < kBeatsKnown ? kBeatNames[k] : "(reserved)").c_str());
            setListDefault(beats, static_cast<ParamID>(kChainParamBase + slot * 4 + 2), 5.0 / (kBeatsCount - 1)); // 4 beats
        }
        for(int slot = 0; slot < kNumSlots; ++slot) {
            auto *curve = addList(static_cast<ParamID>(kCurveParamBase + slot * 4), slotTitle(slot, "Velocity Curve"));
            for(int k = 0; k < kCurveCount; ++k)
                curve->appendString(utf16(k < kCurvesKnown ? kCurveNames[k] : "(reserved)").c_str());
            addParam(static_cast<ParamID>(kCurveParamBase + slot * 4 + 1), slotTitle(slot, "Velocity Depth").c_str(), nullptr, 1.0);
            addParam(static_cast<ParamID>(kCurveParamBase + slot * 4 + 2), slotTitle(slot, "Key Level").c_str(), STR16("dB/oct"), 0.5);
        }
        // Each stage's drive (clean) and mod scale (+100 %: as before).
        for(int slot = 0; slot < kNumSlots; ++slot)
            for(int stage = 0; stage < kFilterStages; ++stage) {
                const std::string name = "Filter " + std::to_string(stage + 1) + " ";
                addParam(filterParamId(slot, stage, kFilterDrive), slotTitle(slot, (name + "Drive").c_str()).c_str(), nullptr, 0.0);
                addParam(filterParamId(slot, stage, kFilterMod), slotTitle(slot, (name + "Mod").c_str()).c_str(), nullptr, 1.0);
            }
        return kResultOk;
    }

    tresult PLUGIN_API terminate() SMTG_OVERRIDE
    {
        destroyVoices();
        {
            std::lock_guard<std::mutex> lock(controlMutex_);
            for(int slot = 0; slot < kNumSlots; ++slot) {
                published_[slot].store(nullptr, std::memory_order_release);
                owned_[slot].reset();
                publishedMarkers_[slot].store(nullptr, std::memory_order_release);
                ownedMarkers_[slot].reset();
            }
            graveyard_.clear();
            markerGraveyard_.clear();
        }
        return SingleComponentEffect::terminate();
    }

    tresult PLUGIN_API getMidiControllerAssignment(int32 bus, int16 channel, CtrlNumber cc,
                                                   ParamID &id) SMTG_OVERRIDE
    {
        if(bus != 0 || channel < 0 || channel > 15)
            return kResultFalse;
        // Raw MIDI CC numbers, matching mlacker's pattern/live CC routing.
        switch(cc) {
            case 7: id = kLevelParam; return kResultOk;    // Channel volume
            case 73: id = kAttackParam; return kResultOk;  // Sound controller 4 (attack)
            case 75: id = kDecayParam; return kResultOk;   // Sound controller 6 (decay)
            case 72: id = kReleaseParam; return kResultOk; // Sound controller 3 (release)
            case 1: id = kModWheelParam; return kResultOk;
            case kAfterTouch: id = kAftertouchParam; return kResultOk;
            case kPitchBend: id = kPitchBendParam; return kResultOk;
        }
        for(int k = 0; k < kModCcsKnown; ++k)
            if(cc == kModCcNumbers[k]) {
                id = static_cast<ParamID>(kCcValueParamBase + k);
                return kResultOk;
            }
        return kResultFalse;
    }

    tresult PLUGIN_API setBusArrangements(SpeakerArrangement *inputs, int32 numIns,
                                          SpeakerArrangement *outputs, int32 numOuts) SMTG_OVERRIDE
    {
        if(numIns != 0 || numOuts < 1 || numOuts > kNumBuses || outputs == nullptr)
            return kResultFalse;
        for(int32 bus = 0; bus < numOuts; ++bus)
            if(outputs[bus] != SpeakerArr::kStereo)
                return kResultFalse;
        return SingleComponentEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
    }

    tresult PLUGIN_API activateBus(MediaType type, BusDirection dir, int32 index, TBool state) SMTG_OVERRIDE
    {
        const tresult result = SingleComponentEffect::activateBus(type, dir, index, state);
        if(result == kResultTrue && type == kAudio && dir == kOutput && index >= 0 && index < kNumBuses)
            outputActive_[index].store(state != 0, std::memory_order_relaxed);
        return result;
    }

    tresult PLUGIN_API canProcessSampleSize(int32 symbolicSampleSize) SMTG_OVERRIDE
    {
        return symbolicSampleSize == kSample32 ? kResultTrue : kResultFalse;
    }

    tresult PLUGIN_API setupProcessing(ProcessSetup &setup) SMTG_OVERRIDE
    {
        const tresult result = SingleComponentEffect::setupProcessing(setup);
        if(result != kResultOk)
            return result;
        // Called while inactive: nothing is rendering, voices may be rebuilt.
        destroyVoices();
        sampleRate_ = setup.sampleRate > 0 ? setup.sampleRate : 44100.0;
        for(auto &voice : voices_)
            voice.dsp = mlasampler_voice_create__f32(static_cast<float>(sampleRate_));
        pushEnvelope();
        return kResultOk;
    }

    tresult PLUGIN_API setActive(TBool state) SMTG_OVERRIDE
    {
        if(!state)
            stopAllVoices();
        return SingleComponentEffect::setActive(state);
    }

    tresult PLUGIN_API process(ProcessData &data) SMTG_OVERRIDE
    {
        syncSlots();
        if(paramsDirty_.exchange(false, std::memory_order_acquire)) {
            pushEnvelope();
            for(int slot = 0; slot < kNumSlots; ++slot)
                applyLive(slot, true, true, true, true);
            for(auto &voice : voices_)
                if(voice.dsp && voice.sample && mlasampler_voice_is_active__ptr_struct_SamplerVoice(voice.dsp))
                    applyControllers(voice.dsp, false);
        }
        handleParameterChanges(data.inputParameterChanges);
        // Synced LFOs follow the host tempo.
        if(data.processContext && (data.processContext->state & ProcessContext::kTempoValid) &&
           data.processContext->tempo > 0.0 && data.processContext->tempo != tempo_) {
            tempo_ = data.processContext->tempo;
            for(int slot = 0; slot < kNumSlots; ++slot)
                applyLive(slot, false, tempoSync(slot) != kSyncOff, false, true);
        }
        framesRendered_ += data.numSamples;

        if(data.symbolicSampleSize != kSample32) {
            flushControls();
            finishBlock();
            return kResultFalse;
        }
        // Buses this block may write: active, stereo and with buffers. Voices
        // on any other bus fall back to Main.
        float *buses[kNumBuses][2] = {};
        const int32 busCount = std::min<int32>(data.numOutputs, kNumBuses);
        for(int32 bus = 0; bus < busCount; ++bus) {
            AudioBusBuffers &out = data.outputs[bus];
            if(out.channelBuffers32 == nullptr || out.numChannels < 2)
                continue;
            for(int32 ch = 0; ch < out.numChannels; ++ch)
                std::fill_n(out.channelBuffers32[ch], data.numSamples, 0.0f);
            out.silenceFlags = 0;
            if(bus == 0 || outputActive_[bus].load(std::memory_order_relaxed)) {
                buses[bus][0] = out.channelBuffers32[0];
                buses[bus][1] = out.channelBuffers32[1];
            }
        }
        if(buses[0][0] == nullptr) {
            flushControls();
            finishBlock();
            return kResultOk;
        }

        // Render between events so note starts land on their exact frame.
        int32 rendered = 0;
        IEventList *events = data.inputEvents;
        const int32 eventCount = events ? events->getEventCount() : 0;
        for(int32 i = 0; i < eventCount; ++i) {
            Event event{};
            if(events->getEvent(i, event) != kResultOk)
                continue;
            const int32 at = std::clamp(event.sampleOffset, rendered, data.numSamples);
            renderTo(buses, rendered, at);
            if(event.type == Event::kNoteOnEvent) {
                if(event.noteOn.velocity <= 0.0f)
                    noteOff(event.noteOn.channel, event.noteOn.pitch);
                else
                    noteOn(event.noteOn.channel, event.noteOn.pitch, event.noteOn.velocity);
            } else if(event.type == Event::kNoteOffEvent) {
                noteOff(event.noteOff.channel, event.noteOff.pitch);
            }
        }
        renderTo(buses, rendered, data.numSamples);
        flushControls();
        finishBlock();
        return kResultOk;
    }

    // --- Sample loading protocol -------------------------------------------
    tresult PLUGIN_API notify(IMessage *message) SMTG_OVERRIDE
    {
        if(message == nullptr || message->getMessageID() == nullptr)
            return kInvalidArgument;
        const char *id = message->getMessageID();
        const bool loadFile = std::strcmp(id, mla_sampler::kLoadFileMessage) == 0;
        const bool loadPcm = std::strcmp(id, mla_sampler::kLoadPcmMessage) == 0;
        const bool clear = std::strcmp(id, mla_sampler::kClearMessage) == 0;
        const bool info = std::strcmp(id, mla_sampler::kInfoMessage) == 0;
        const bool markers = std::strcmp(id, mla_sampler::kMarkersMessage) == 0;
        if(!loadFile && !loadPcm && !clear && !info && !markers)
            return SingleComponentEffect::notify(message);

        IAttributeList *attributes = message->getAttributes();
        if(attributes == nullptr)
            return kInvalidArgument;
        if(info) {
            int64 occupied = 0;
            {
                std::lock_guard<std::mutex> lock(controlMutex_);
                for(int slot = 0; slot < kNumSlots; ++slot)
                    if(owned_[slot])
                        occupied |= int64(1) << slot;
            }
            attributes->setInt("root", rootKeyFromNorm(norm(kRootKeyParam)));
            attributes->setInt("pads", kNumSlots);
            attributes->setInt("occupied", occupied);
            return kResultOk;
        }
        // The protocol calls slots "pads".
        int64 slot = -1;
        if(attributes->getInt("pad", slot) != kResultOk || slot < 0 || slot >= kNumSlots)
            return fail(attributes, "pad must be 0-15");
        if(markers)
            return handleMarkers(static_cast<int>(slot), attributes);

        std::unique_ptr<Sample> sample;
        if(loadFile) {
            const std::string path = binaryString(attributes, "path");
            if(path.empty())
                return fail(attributes, "missing path");
            std::string error;
            sample = decodeFile(path, error);
            if(!sample)
                return fail(attributes, error);
        } else if(loadPcm) {
            int64 channels = 0, frames = 0;
            double rate = 0;
            const void *data = nullptr;
            uint32 size = 0;
            if(attributes->getInt("channels", channels) != kResultOk ||
               attributes->getInt("frames", frames) != kResultOk ||
               attributes->getFloat("rate", rate) != kResultOk ||
               attributes->getBinary("data", data, size) != kResultOk || data == nullptr)
                return fail(attributes, "missing channels, frames, rate or data");
            if(!validFormat(frames, channels, rate))
                return fail(attributes, "unsupported sample length, channel count or rate");
            if(static_cast<uint64_t>(size) != static_cast<uint64_t>(frames * channels) * sizeof(float))
                return fail(attributes, "data size does not match frames * channels");
            // The attribute buffer has no alignment guarantee; copy first.
            std::vector<float> pcm(static_cast<size_t>(frames * channels));
            std::memcpy(pcm.data(), data, size);
            sample = makeSample(pcm.data(), frames, static_cast<int>(channels), rate,
                                binaryString(attributes, "name"));
        }
        replaceSlot(static_cast<int>(slot), std::move(sample));
        return kResultOk;
    }

    // --- State ---------------------------------------------------------------
    tresult PLUGIN_API setState(IBStream *state) SMTG_OVERRIDE
    {
        if(state == nullptr)
            return kResultFalse;
        IBStreamer streamer(state, kLittleEndian);
        uint32 magic = 0, version = 0;
        if(!streamer.readInt32u(magic) || magic != kStateMagic || !streamer.readInt32u(version) ||
           version != kStateVersion)
            return kResultFalse;
        int32 count = 0;
        if(!streamer.readInt32(count) || count < 0 || count > 4096)
            return kResultFalse;
        for(int32 i = 0; i < count; ++i) {
            int32 id = 0;
            double value = 0;
            if(!streamer.readInt32(id) || !streamer.readDouble(value))
                return kResultFalse;
            const int index = indexOf(static_cast<ParamID>(id));
            if(index < 0 || !std::isfinite(value))
                continue; // Unknown parameter from a newer version.
            value = std::clamp(value, 0.0, 1.0);
            norm_[index].store(value, std::memory_order_relaxed);
            setParamNormalized(static_cast<ParamID>(id), value);
        }
        paramsDirty_.store(true, std::memory_order_release);

        std::unique_ptr<Sample> slots[kNumSlots];
        for(int slot = 0; slot < kNumSlots; ++slot) {
            int8 present = 0;
            if(!streamer.readInt8(present))
                return kResultFalse;
            if(!present)
                continue;
            std::string name;
            double rate = 0;
            int64 frames = 0;
            if(!readString(streamer, name) || !streamer.readDouble(rate) || !streamer.readInt64(frames) ||
               !validFormat(frames, 2, rate))
                return kResultFalse;
            std::vector<float> pcm(static_cast<size_t>(frames) * 2u);
            if(!streamer.readFloatArray(pcm.data(), static_cast<int32>(pcm.size())))
                return kResultFalse;
            slots[slot] = makeSample(pcm.data(), frames, 2, rate, std::move(name));
        }
        for(int slot = 0; slot < kNumSlots; ++slot)
            replaceSlot(slot, std::move(slots[slot]));
        return kResultOk;
    }

    tresult PLUGIN_API getState(IBStream *state) SMTG_OVERRIDE
    {
        if(state == nullptr)
            return kResultFalse;
        IBStreamer streamer(state, kLittleEndian);
        streamer.writeInt32u(kStateMagic);
        streamer.writeInt32u(kStateVersion);
        streamer.writeInt32(kNumParams);
        for(int i = 0; i < kNumParams; ++i) {
            streamer.writeInt32(static_cast<int32>(paramIdAt(i)));
            streamer.writeDouble(norm_[i].load(std::memory_order_relaxed));
        }
        std::lock_guard<std::mutex> lock(controlMutex_);
        for(int slot = 0; slot < kNumSlots; ++slot) {
            const Sample *sample = owned_[slot].get();
            streamer.writeInt8(sample ? 1 : 0);
            if(!sample)
                continue;
            writeString(streamer, sample->name);
            streamer.writeDouble(sample->rate);
            streamer.writeInt64(sample->frames);
            // Slots are embedded without the guard frame.
            streamer.writeFloatArray(sample->stereo.data(), static_cast<int32>(sample->frames * 2));
        }
        return kResultOk;
    }

  private:
    struct Voice {
        SamplerVoice *dsp = nullptr;
        const Sample *sample = nullptr;
        int slot = -1;
        int16 channel = -1;
        int16 pitch = -1;
        uint64_t serial = 0;
        // What the note contributed, so live edits recompute pitch and level.
        int keySemitones = 0;
        float velocityGain = 1.0f;
        // Plays its sample backwards (read when the note started).
        bool reversed = false;
        // Its place in a unison stack: pitch and pan offsets, and gain.
        double detune = 0.0;
        float panOffset = 0.0f;
        float unisonGain = 1.0f;
        // Beats tempo sync: on, the next onset to start a segment at, and the
        // playhead last frame (a drop means a loop wrapped).
        bool beats = false;
        size_t nextOnset = 0;
        double lastPosition = 0.0;
        // The slot's markers when the note started (updated when they change).
        const Markers *markers = nullptr;
    };

    struct Retired {
        std::unique_ptr<Sample> sample;
        uint64_t freeAfterBlock = 0;
    };

    Voice voices_[kNumVoices];
    // Audio thread: keys held on each mono or legato slot, newest last, and
    // each slot's last note (-1 for none) and its key tracking, for glides.
    int held_[kNumSlots][kHeldMax] = {};
    int heldCount_[kNumSlots] = {};
    int lastPitch_[kNumSlots] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    int lastKey_[kNumSlots] = {};
    // Audio thread: each group's last pick (random) or next turn (round-robin).
    int groupTurn_[kNumGroups] = {};
    uint32_t random_ = 0x9E3779B9u;
    uint64_t nextSerial_ = 1;
    double sampleRate_ = 44100.0;
    // Audio thread: the host tempo (for synced LFOs) and frames rendered
    // since activation (the time a free LFO runs in).
    double tempo_ = 120.0;
    double framesRendered_ = 0.0;
    std::atomic<double> norm_[kNumParams];
    std::atomic<bool> paramsDirty_{false};
    // This block's controller changes, by frame (see renderTo).
    struct ControlEvent {
        int32 offset;
        int index;
        double value;
    };
    static constexpr int kMaxControlEvents = 512;
    std::array<ControlEvent, kMaxControlEvents> controlEvents_{};
    int controlCount_ = 0;
    int nextControl_ = 0;
    std::atomic<bool> outputActive_[kNumBuses];

    // Audio-thread view of the slots.
    const Sample *active_[kNumSlots] = {};
    std::atomic<const Sample *> published_[kNumSlots];
    std::atomic<uint64_t> blocksDone_{0};

    // Control-thread ownership.
    std::mutex controlMutex_;
    std::unique_ptr<Sample> owned_[kNumSlots];
    std::vector<Retired> graveyard_;
    // Slice markers, handed over like the samples (see publishMarkers).
    struct RetiredMarkers {
        std::unique_ptr<Markers> markers;
        uint64_t freeAfterBlock = 0;
    };
    std::atomic<const Markers *> publishedMarkers_[kNumSlots]{};
    std::unique_ptr<Markers> ownedMarkers_[kNumSlots];
    const Markers *activeMarkers_[kNumSlots] = {};
    std::vector<RetiredMarkers> markerGraveyard_;

    void addParam(ParamID id, const TChar *title, const TChar *units, double defaultNorm)
    {
        parameters.addParameter(title, units, 0, defaultNorm, ParameterInfo::kCanAutomate, id);
        norm_[indexOf(id)].store(defaultNorm, std::memory_order_relaxed);
    }

    void setListDefault(StringListParameter *list, ParamID id, double value)
    {
        list->getInfo().defaultNormalizedValue = value;
        list->setNormalized(value);
        norm_[indexOf(id)].store(value, std::memory_order_relaxed);
    }

    StringListParameter *addList(ParamID id, const std::u16string &title)
    {
        auto *list = new StringListParameter(title.c_str(), id, nullptr,
                                             ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
        parameters.addParameter(list);
        norm_[indexOf(id)].store(0.0, std::memory_order_relaxed);
        return list;
    }

    // A MIDI key 0..127 as a stepped parameter.
    void addKey(ParamID id, const std::u16string &title, int key)
    {
        parameters.addParameter(title.c_str(), nullptr, 127, key / 127.0, ParameterInfo::kCanAutomate, id);
        norm_[indexOf(id)].store(key / 127.0, std::memory_order_relaxed);
    }

    static std::string outputName(int bus)
    {
        if(bus >= kNumOutputs)
            return bus == kNumOutputs ? "Send A" : "Send B";
        return bus == 0 ? "Main" : "Out " + std::to_string(bus + 1);
    }

    static std::u16string slotTitle(int slot, const char *what)
    {
        return utf16("Slot " + std::to_string(slot + 1) + " " + what);
    }

    static tresult fail(IAttributeList *attributes, const std::string &why)
    {
        attributes->setBinary("error", why.data(), static_cast<uint32>(why.size()));
        return kResultFalse;
    }

    static bool readString(IBStreamer &streamer, std::string &out)
    {
        uint32 size = 0;
        if(!streamer.readInt32u(size) || size > 4096)
            return false;
        out.resize(size);
        return size == 0 || streamer.readRaw(out.data(), size) == static_cast<int32>(size);
    }

    static void writeString(IBStreamer &streamer, const std::string &text)
    {
        const uint32 size = static_cast<uint32>(std::min<size_t>(text.size(), 4096));
        streamer.writeInt32u(size);
        if(size)
            streamer.writeRaw(text.data(), size);
    }

    double norm(ParamID id) const { return norm_[indexOf(id)].load(std::memory_order_relaxed); }
    double slotNorm(int slot, SlotParam param) const { return norm(slotParamId(slot, param)); }
    double zoneNorm(int slot, ZoneParam param) const { return norm(zoneParamId(slot, param)); }

    // --- Control thread ------------------------------------------------------
    void replaceSlot(int slot, std::unique_ptr<Sample> sample)
    {
        std::lock_guard<std::mutex> lock(controlMutex_);
        collectGarbage();
        // A new sample starts with the markers detected in it.
        std::unique_ptr<Markers> detected;
        if(sample)
            detected = std::make_unique<Markers>(Markers{sample->onsets});
        publishMarkers(slot, std::move(detected));
        published_[slot].store(sample.get());
        if(owned_[slot])
            graveyard_.push_back({std::move(owned_[slot]), blocksDone_.load() + 2});
        owned_[slot] = std::move(sample);
    }

    // Control thread, under controlMutex_: hand `markers` to the audio thread
    // (syncSlots), retiring the old ones two blocks later.
    void publishMarkers(int slot, std::unique_ptr<Markers> markers)
    {
        publishedMarkers_[slot].store(markers.get());
        if(ownedMarkers_[slot])
            markerGraveyard_.push_back({std::move(ownedMarkers_[slot]), blocksDone_.load() + 2});
        ownedMarkers_[slot] = std::move(markers);
    }

    // kMarkersMessage: write back the slot's markers, after replacing them
    // (set 1: "frames", sorted, inside the sample, 0 first) or detecting
    // them again (set 2).
    tresult handleMarkers(int slot, IAttributeList *attributes)
    {
        std::lock_guard<std::mutex> lock(controlMutex_);
        collectGarbage();
        if(!owned_[slot])
            return fail(attributes, "pad is empty");
        int64 set = 0;
        attributes->getInt("set", set);
        if(set == 1) {
            const void *data = nullptr;
            uint32 size = 0;
            if(attributes->getBinary("frames", data, size) != kResultOk || size % sizeof(double) != 0)
                return fail(attributes, "frames must be float64 values");
            std::vector<double> frames(size / sizeof(double));
            if(!frames.empty())
                std::memcpy(frames.data(), data, size);
            const double end = static_cast<double>(owned_[slot]->frames);
            std::vector<double> clean{0.0};
            for(double frame : frames)
                if(std::isfinite(frame) && frame >= 1.0 && frame < end)
                    clean.push_back(std::floor(frame));
            std::sort(clean.begin(), clean.end());
            clean.erase(std::unique(clean.begin(), clean.end()), clean.end());
            publishMarkers(slot, std::make_unique<Markers>(Markers{std::move(clean)}));
        } else if(set == 2) {
            publishMarkers(slot, std::make_unique<Markers>(Markers{owned_[slot]->onsets}));
        }
        const auto &frames = ownedMarkers_[slot] ? ownedMarkers_[slot]->frames : owned_[slot]->onsets;
        attributes->setBinary("frames", frames.data(), static_cast<uint32>(frames.size() * sizeof(double)));
        return kResultOk;
    }

    // A block that started after the swap has seen the new pointer and
    // stopped every voice on the old one; two completed blocks guarantee that.
    void collectGarbage()
    {
        const uint64_t done = blocksDone_.load();
        graveyard_.erase(std::remove_if(graveyard_.begin(), graveyard_.end(),
                                        [&](const Retired &r) { return done >= r.freeAfterBlock; }),
                         graveyard_.end());
        markerGraveyard_.erase(std::remove_if(markerGraveyard_.begin(), markerGraveyard_.end(),
                                              [&](const RetiredMarkers &r) { return done >= r.freeAfterBlock; }),
                               markerGraveyard_.end());
    }

    void destroyVoices()
    {
        for(auto &voice : voices_) {
            if(voice.dsp)
                mlasampler_voice_destroy__ptr_struct_SamplerVoice(voice.dsp);
            voice = Voice{};
        }
    }

    // --- Audio thread --------------------------------------------------------
    void finishBlock() { blocksDone_.fetch_add(1); }

    // An LFO's rate in Hz: its own, or its division of the host tempo.
    float lfoRate(int slot, int lfo) const
    {
        if(norm(lfoParamId(slot, lfo, kLfoSync)) < 0.5)
            return lfoRateFromNorm(norm(lfoParamId(slot, lfo, kLfoRate)));
        const int division = std::clamp(static_cast<int>(std::lround(norm(lfoParamId(slot, lfo, kLfoDivision)) *
                                                                     (kLfoDivisionCount - 1))),
                                        0, kLfoDivisionsKnown - 1);
        return static_cast<float>(tempo_ / 60.0 / kLfoDivisionBeats[division]);
    }

    // The slot's LFO settings, on a new or sounding voice (phases run on).
    void applyLfo(SamplerVoice *dsp, int slot)
    {
        for(int lfo = 0; lfo < kLfos; ++lfo)
            mlasampler_voice_set_lfo__ptr_struct_SamplerVoice_i32_i32_f32_f32_f32_f32_f32(
                dsp, lfo, static_cast<int32_t>(std::lround(norm(lfoParamId(slot, lfo, kLfoShape)) * (kLfoShapeCount - 1))),
                lfoRate(slot, lfo), static_cast<float>(norm(lfoParamId(slot, lfo, kLfoDelay)) * 2.0),
                static_cast<float>((norm(lfoParamId(slot, lfo, kLfoPitch)) * 2.0 - 1.0) * 12.0),
                static_cast<float>((norm(lfoParamId(slot, lfo, kLfoCutoff)) * 2.0 - 1.0) * 4.0),
                static_cast<float>(norm(lfoParamId(slot, lfo, kLfoLevel))));
    }

    int routeSource(int slot, int route) const
    {
        const int source = static_cast<int>(std::lround(norm(routeParamId(slot, route, kRouteSource)) * (kRouteListCount - 1)));
        return source < kRouteSourcesKnown ? source : 0;
    }
    int routeTarget(int slot, int route) const
    {
        const int target = static_cast<int>(std::lround(norm(routeParamId(slot, route, kRouteTarget)) * (kRouteListCount - 1)));
        return target < kRouteTargetsKnown ? target : 0;
    }
    // A route's full amount, -1 .. 1, times its target's range.
    double routeAmount(int slot, int route) const
    {
        return (norm(routeParamId(slot, route, kRouteAmount)) * 2.0 - 1.0) * kRouteTargetRange[routeTarget(slot, route)];
    }

    // The slot's mod routes, on a new or sounding voice. Start is applied
    // when the note starts, so the voice does not see it.
    void applyRoutes(SamplerVoice *dsp, int slot)
    {
        for(int route = 0; route < kRoutes; ++route) {
            const int target = routeTarget(slot, route);
            mlasampler_voice_set_route__ptr_struct_SamplerVoice_i32_i32_i32_f32(
                dsp, route, routeSource(slot, route), target == kTargetStart ? 0 : target,
                static_cast<float>(routeAmount(slot, route)));
        }
    }

    // The slot's pitch envelope: depth in semitones, attack and decay.
    void applyPitchEnvelope(SamplerVoice *dsp, int slot)
    {
        const auto field = [&](int k) { return norm(static_cast<ParamID>(kPitchEnvParamBase + slot * 4 + k)); };
        mlasampler_voice_set_pitch_envelope__ptr_struct_SamplerVoice_f32_f32_f32(
            dsp, pitchEnvFromNorm(field(0)), attackFromNorm(field(1)), timeFromNorm(field(2)));
    }

    // A controller source's value now: the mod wheel, aftertouch and the
    // chosen CC 0..1, pitch bend -1..1.
    double controllerValue(int source) const
    {
        if(source == kSourceModWheel)
            return norm(kModWheelParam);
        if(source == kSourceAftertouch)
            return norm(kAftertouchParam);
        if(source == kSourcePitchBend)
            return std::clamp((norm(kPitchBendParam) * 16383.0 - 8192.0) / 8192.0, -1.0, 1.0);
        if(source == kSourceModCc) {
            const int choice = static_cast<int>(std::lround(norm(kModCcParam) * (kModCcCount - 1)));
            return choice < kModCcsKnown ? norm(static_cast<ParamID>(kCcValueParamBase + choice)) : 0.0;
        }
        return 0.0;
    }

    // The controllers' values and the pitch bend, on a new or sounding voice.
    // `snap`: a new note starts at the values; a sounding one glides there.
    void applyControllers(SamplerVoice *dsp, bool snap)
    {
        const double range = std::lround(norm(kBendRangeParam) * kBendRangeMax);
        mlasampler_voice_set_controllers__ptr_struct_SamplerVoice_f32_f32_f32_f32_f32_bool(
            dsp, static_cast<float>(controllerValue(kSourceModWheel)), static_cast<float>(controllerValue(kSourceAftertouch)),
            static_cast<float>(controllerValue(kSourcePitchBend)), static_cast<float>(controllerValue(kSourceModCc)),
            static_cast<float>(range), snap);
    }

    void syncSlots()
    {
        for(int slot = 0; slot < kNumSlots; ++slot) {
            const Sample *current = published_[slot].load();
            if(current == active_[slot])
                continue;
            for(auto &voice : voices_)
                if(voice.dsp && voice.sample == active_[slot] && voice.slot == slot) {
                    mlasampler_voice_stop__ptr_struct_SamplerVoice(voice.dsp);
                    voice.sample = nullptr;
                }
            active_[slot] = current;
        }
        // New markers reach the slot's Beats voices at once.
        for(int slot = 0; slot < kNumSlots; ++slot) {
            const Markers *markers = publishedMarkers_[slot].load();
            if(markers == activeMarkers_[slot])
                continue;
            activeMarkers_[slot] = markers;
            for(auto &voice : voices_)
                if(voice.dsp && voice.sample && voice.slot == slot) {
                    voice.markers = markers;
                    if(voice.beats)
                        resyncBeats(voice);
                }
        }
    }

    void stopAllVoices()
    {
        std::fill(std::begin(heldCount_), std::end(heldCount_), 0);
        std::fill(std::begin(lastPitch_), std::end(lastPitch_), -1);
        for(auto &voice : voices_) {
            if(voice.dsp)
                mlasampler_voice_stop__ptr_struct_SamplerVoice(voice.dsp);
            voice.sample = nullptr;
        }
    }

    // The envelope a slot's voices use: its own, or the instance's (slot -1
    // always takes the instance's).
    void applyEnvelope(SamplerVoice *dsp, int slot)
    {
        const bool own = slot >= 0 && norm(envelopeParamId(slot, kEnvelopeOwn)) >= 0.5;
        const double attack = own ? norm(envelopeParamId(slot, kEnvelopeAttack)) : norm(kAttackParam);
        const double decay = own ? norm(envelopeParamId(slot, kEnvelopeDecay)) : norm(kDecayParam);
        const double sustain = own ? norm(envelopeParamId(slot, kEnvelopeSustain)) : norm(kSustainParam);
        const double release = own ? norm(envelopeParamId(slot, kEnvelopeRelease)) : norm(kReleaseParam);
        mlasampler_voice_set_envelope__ptr_struct_SamplerVoice_f32_f32_f32_f32(
            dsp, attackFromNorm(attack), timeFromNorm(decay), static_cast<float>(sustain), timeFromNorm(release));
    }

    // Envelope edits reach sounding voices too.
    void pushEnvelope()
    {
        for(auto &voice : voices_)
            if(voice.dsp)
                applyEnvelope(voice.dsp, voice.sample ? voice.slot : -1);
    }

    // Controller values (mod wheel, aftertouch, pitch bend, the Mod CC
    // choices) change at their exact frame.
    static bool timedController(ParamID id)
    {
        return (id >= kModWheelParam && id <= kPitchBendParam) ||
               (id >= kCcValueParamBase && id < kCcValueParamBase + kModCcsKnown);
    }

    // Render up to frame `at`, applying the controller changes due by then
    // at their frames.
    void renderTo(float *(&buses)[kNumBuses][2], int32 &rendered, int32 at)
    {
        while(nextControl_ < controlCount_ && controlEvents_[nextControl_].offset <= at) {
            const ControlEvent &event = controlEvents_[nextControl_++];
            const int32 when = std::clamp(event.offset, rendered, at);
            render(buses, rendered, when);
            rendered = when;
            applyControl(event);
        }
        render(buses, rendered, at);
        rendered = at;
    }

    void applyControl(const ControlEvent &event)
    {
        norm_[event.index].store(event.value, std::memory_order_relaxed);
        for(auto &voice : voices_)
            if(voice.dsp && voice.sample && mlasampler_voice_is_active__ptr_struct_SamplerVoice(voice.dsp))
                applyControllers(voice.dsp, false);
    }

    // Controller changes left when a block renders nothing.
    void flushControls()
    {
        while(nextControl_ < controlCount_)
            applyControl(controlEvents_[nextControl_++]);
    }

    void handleParameterChanges(IParameterChanges *changes)
    {
        if(changes == nullptr)
            return;
        bool envelopeChanged = false;
        // Slots whose sounding voices take a changed loop or level/pan/tune.
        bool loopChanged[kNumSlots] = {};
        bool soundChanged[kNumSlots] = {};
        bool filterChanged[kNumSlots] = {};
        bool lfoChanged[kNumSlots] = {};
        bool routeChanged[kNumSlots] = {};
        bool controllersChanged = false;
        controlCount_ = 0;
        nextControl_ = 0;
        const int32 count = changes->getParameterCount();
        for(int32 q = 0; q < count; ++q) {
            IParamValueQueue *queue = changes->getParameterData(q);
            if(queue == nullptr)
                continue;
            const int32 points = queue->getPointCount();
            int32 offset = 0;
            ParamValue value = 0;
            if(points <= 0 || queue->getPoint(points - 1, offset, value) != kResultOk)
                continue;
            const ParamID id = queue->getParameterId();
            const int index = indexOf(id);
            if(index < 0 || !std::isfinite(value))
                continue;
            // Controller values take effect at their frames (see process),
            // every point of the block in order; if too many, the last now.
            if(timedController(id) && controlCount_ + points <= kMaxControlEvents) {
                for(int32 p = 0; p < points; ++p) {
                    int32 at = 0;
                    ParamValue point = 0;
                    if(queue->getPoint(p, at, point) == kResultOk && std::isfinite(point))
                        controlEvents_[controlCount_++] = {at, index, std::clamp(point, 0.0, 1.0)};
                }
                continue;
            }
            norm_[index].store(std::clamp(value, 0.0, 1.0), std::memory_order_relaxed);
            if((id >= kAttackParam && id <= kReleaseParam) ||
               (id >= kEnvelopeParamBase && id < kEnvelopeParamBase + kNumEnvelopeParams))
                envelopeChanged = true;
            if(id == kLevelParam || id == kTuneParam)
                std::fill(std::begin(soundChanged), std::end(soundChanged), true);
            if(id >= kSlotParamBase && id < kSlotParamBase + kNumSlotParams) {
                const int slot = static_cast<int>(id - kSlotParamBase) / kParamsPerSlot;
                const int field = static_cast<int>(id - kSlotParamBase) % kParamsPerSlot;
                if(field == kSlotLevel || field == kSlotPan || field == kSlotTune)
                    soundChanged[slot] = true;
                if(field == kSlotLoop || field == kSlotLoopStart || field == kSlotLoopEnd)
                    loopChanged[slot] = true;
            }
            if(id >= kCrossfadeParamBase && id < kCrossfadeParamBase + kNumSlots)
                loopChanged[id - kCrossfadeParamBase] = true;
            if(id >= kFilterEnvelopeParamBase && id < kFilterEnvelopeParamBase + 4)
                std::fill(std::begin(filterChanged), std::end(filterChanged), true);
            if(id >= kSlotFilterEnvelopeParamBase && id < kSlotFilterEnvelopeParamBase + kNumSlots * kParamsPerEnvelope)
                filterChanged[(id - kSlotFilterEnvelopeParamBase) / kParamsPerEnvelope] = true;
            if(id >= kFilterParamBase && id < kFilterParamBase + kNumSlots * kFilterSlotStride)
                filterChanged[(id - kFilterParamBase) / kFilterSlotStride] = true;
            if(id >= kLfoParamBase && id < kLfoParamBase + kNumSlots * kLfoSlotStride)
                lfoChanged[(id - kLfoParamBase) / kLfoSlotStride] = true;
            if(id >= kPitchEnvParamBase && id < kPitchEnvParamBase + kNumSlots * 4)
                lfoChanged[(id - kPitchEnvParamBase) / 4] = true;
            if(id >= kChainParamBase && id < kChainParamBase + kNumSlots * 4) {
                if((id - kChainParamBase) % 4 == 0)
                    filterChanged[(id - kChainParamBase) / 4] = true;
                else
                    soundChanged[(id - kChainParamBase) / 4] = true;
            }
            if(id >= kRouteParamBase && id < kMaxParamId)
                routeChanged[(id - kRouteParamBase) / kRouteSlotStride] = true;
            if((id >= kBendRangeParam && id <= kPitchBendParam) ||
               (id >= kCcValueParamBase && id < kCcValueParamBase + kModCcsKnown))
                controllersChanged = true;
        }
        std::stable_sort(controlEvents_.begin(), controlEvents_.begin() + controlCount_,
                         [](const ControlEvent &a, const ControlEvent &b) { return a.offset < b.offset; });
        if(controllersChanged)
            for(auto &voice : voices_)
                if(voice.dsp && voice.sample && mlasampler_voice_is_active__ptr_struct_SamplerVoice(voice.dsp))
                    applyControllers(voice.dsp, false);
        if(envelopeChanged)
            pushEnvelope();
        for(int slot = 0; slot < kNumSlots; ++slot)
            applyLive(slot, loopChanged[slot], soundChanged[slot], filterChanged[slot], lfoChanged[slot] || routeChanged[slot]);
    }

    // Every slot whose key (Pad) or key range (Zone) and velocity range hold
    // the note plays it, so overlapping zones layer and velocity ranges
    // switch between them. Zone, velocity and start settings are read when a
    // note starts; loop, level, pan, tune and envelope edits reach sounding
    // notes too (applyLive, pushEnvelope).
    //
    // Slots in a group (1-8) take turns instead: of a group's slots that
    // match, one plays, chosen round-robin or at random (Group Mode). A slot
    // in a choke group (1-8) cuts off that group's sounding voices.
    void noteOn(int16 channel, int16 pitch, float velocity)
    {
        const int padSlot = pitch - rootKeyFromNorm(norm(kRootKeyParam));
        const int hardness = std::clamp(static_cast<int>(std::lround(velocity * 127.0f)), 1, 127);
        bool matches[kNumSlots] = {};
        int tracked[kNumSlots] = {};
        for(int slot = 0; slot < kNumSlots; ++slot) {
            if(active_[slot] == nullptr)
                continue;
            const int velocityLow = rootKeyFromNorm(norm(static_cast<ParamID>(kVelocityParamBase + slot * 2)));
            const int velocityHigh = rootKeyFromNorm(norm(static_cast<ParamID>(kVelocityParamBase + slot * 2 + 1)));
            if(hardness < std::min(velocityLow, velocityHigh) || hardness > std::max(velocityLow, velocityHigh))
                continue;
            if(zoneNorm(slot, kZoneMode) < 0.5) {
                matches[slot] = slot == padSlot;
                continue;
            }
            const int low = rootKeyFromNorm(zoneNorm(slot, kZoneLow));
            const int high = rootKeyFromNorm(zoneNorm(slot, kZoneHigh));
            if(pitch < std::min(low, high) || pitch > std::max(low, high))
                continue;
            matches[slot] = true;
            tracked[slot] = zoneNorm(slot, kZoneTrack) >= 0.5 ? pitch - rootKeyFromNorm(zoneNorm(slot, kZoneRoot)) : 0;
        }
        for(int group = 1; group <= kNumGroups; ++group) {
            int members[kNumSlots];
            int count = 0;
            for(int slot = 0; slot < kNumSlots; ++slot)
                if(matches[slot] && groupOf(slot) == group)
                    members[count++] = slot;
            if(count == 0)
                continue;
            const int chosen = members[pickInGroup(group, count)];
            for(int k = 0; k < count; ++k)
                matches[members[k]] = members[k] == chosen;
        }
        // A slot in a choke group cuts off what that group is playing, its
        // own earlier notes included, but not the slots this note starts.
        bool choking[kNumChokeGroups + 1] = {};
        for(int slot = 0; slot < kNumSlots; ++slot)
            if(matches[slot])
                choking[chokeOf(slot)] = true;
        for(auto &voice : voices_)
            if(voice.dsp && voice.sample && voice.slot >= 0 && chokeOf(voice.slot) > 0 && choking[chokeOf(voice.slot)])
                mlasampler_voice_choke__ptr_struct_SamplerVoice_f32(voice.dsp, kChokeSeconds);
        for(int slot = 0; slot < kNumSlots; ++slot) {
            if(!matches[slot])
                continue;
            const int mode = playMode(slot);
            if(mode == kPlayPoly) {
                startVoices(slot, channel, pitch, velocity, tracked[slot], 0.0);
                continue;
            }
            // Mono and legato: one note per slot. Legato while a key is still
            // held moves the sounding note; otherwise the old note gives way
            // (a short fade) to a new one, gliding from the last note's pitch.
            const bool stillHeld = heldCount_[slot] > 0 && slotSounding(slot);
            pushHeld(slot, pitch);
            if(mode == kPlayLegato && stillHeld)
                retarget(slot, pitch, velocity, tracked[slot]);
            else {
                for(auto &voice : voices_)
                    if(voice.dsp && voice.sample && voice.slot == slot)
                        mlasampler_voice_choke__ptr_struct_SamplerVoice_f32(voice.dsp, kChokeSeconds);
                startVoices(slot, channel, pitch, velocity, tracked[slot],
                            lastPitch_[slot] >= 0 ? static_cast<double>(lastKey_[slot] - tracked[slot]) : 0.0);
            }
            lastPitch_[slot] = pitch;
            lastKey_[slot] = tracked[slot];
        }
    }

    int playMode(int slot) const
    {
        const int mode = static_cast<int>(std::lround(norm(static_cast<ParamID>(kPlayParamBase + slot * 4)) * (kPlayModeCount - 1)));
        return mode < kPlayModesKnown ? mode : kPlayPoly;
    }

    float glideSeconds(int slot) const
    {
        const double n = norm(static_cast<ParamID>(kPlayParamBase + slot * 4 + 1));
        return static_cast<float>(2.0 * n * n); // 0 .. 2 s
    }

    bool slotSounding(int slot) const
    {
        for(const auto &voice : voices_)
            if(voice.dsp && voice.sample && voice.slot == slot &&
               mlasampler_voice_is_active__ptr_struct_SamplerVoice(voice.dsp))
                return true;
        return false;
    }

    void pushHeld(int slot, int pitch)
    {
        removeHeld(slot, pitch);
        if(heldCount_[slot] == kHeldMax) {
            std::copy(held_[slot] + 1, held_[slot] + kHeldMax, held_[slot]);
            --heldCount_[slot];
        }
        held_[slot][heldCount_[slot]++] = pitch;
    }

    void removeHeld(int slot, int pitch)
    {
        int kept = 0;
        for(int k = 0; k < heldCount_[slot]; ++k)
            if(held_[slot][k] != pitch)
                held_[slot][kept++] = held_[slot][k];
        heldCount_[slot] = kept;
    }

    // The sounding note of a mono/legato slot moves to `pitch`, gliding from
    // where it was; its envelope carries on.
    void retarget(int slot, int16 pitch, float velocity, int keySemitones)
    {
        for(auto &voice : voices_) {
            if(!voice.dsp || !voice.sample || voice.slot != slot ||
               !mlasampler_voice_is_active__ptr_struct_SamplerVoice(voice.dsp))
                continue;
            const int from = voice.keySemitones;
            voice.keySemitones = keySemitones;
            voice.pitch = pitch;
            mlasampler_voice_set_step__ptr_struct_SamplerVoice_f64(
                voice.dsp, voiceStep(slot, voice.sample, keySemitones + voice.detune));
            mlasampler_voice_set_glide__ptr_struct_SamplerVoice_f32_f32(voice.dsp, static_cast<float>(from - keySemitones),
                                                                    glideSeconds(slot));
            applyFilter(voice.dsp, slot, pitch);
            mlasampler_voice_set_note__ptr_struct_SamplerVoice_f32_f32(voice.dsp, std::clamp(velocity, 0.0f, 1.0f),
                                                                     static_cast<float>(pitch - 60) / 60.0f);
        }
    }

    // A note's unison stack: `voices` voices spread evenly over +-detune in
    // pitch and +-spread in pan, each at 1/sqrt(voices) so the stack keeps
    // its level. `glide` semitones to slide from (mono, legato), or 0.
    void startVoices(int slot, int16 channel, int16 pitch, float velocity, int keySemitones, double glide)
    {
        const int voices = std::clamp(static_cast<int>(std::lround(norm(static_cast<ParamID>(kUnisonParamBase + slot * 4)) *
                                                                   (kUnisonListCount - 1))) + 1, 1, kUnisonMax);
        const double detuneCents = norm(static_cast<ParamID>(kUnisonParamBase + slot * 4 + 1)) * 100.0;
        const double spread = norm(static_cast<ParamID>(kUnisonParamBase + slot * 4 + 2));
        const float gain = 1.0f / std::sqrt(static_cast<float>(voices));
        for(int v = 0; v < voices; ++v) {
            const double position = voices > 1 ? static_cast<double>(v) / (voices - 1) * 2.0 - 1.0 : 0.0;
            startVoice(slot, channel, pitch, velocity, keySemitones, position * detuneCents / 100.0,
                       static_cast<float>(position * spread), gain, glide);
        }
    }

    int chokeOf(int slot) const
    {
        return static_cast<int>(std::lround(norm(static_cast<ParamID>(kChokeParamBase + slot)) * kNumChokeGroups));
    }

    int groupOf(int slot) const
    {
        return static_cast<int>(std::lround(norm(static_cast<ParamID>(kGroupParamBase + slot)) * kNumGroups));
    }

    // Which of a group's `count` matching slots plays: the next in turn
    // (round-robin), or a random one other than the last pick (random).
    int pickInGroup(int group, int count)
    {
        int &turn = groupTurn_[group - 1];
        if(norm(kGroupModeParam) < 0.5) {
            const int pick = turn % count;
            turn = pick + 1;
            return pick;
        }
        // xorshift32: realtime-safe and deterministic per instance.
        random_ ^= random_ << 13;
        random_ ^= random_ >> 17;
        random_ ^= random_ << 5;
        int pick = static_cast<int>(random_ % static_cast<uint32_t>(count));
        if(count > 1 && pick == turn)
            pick = (pick + 1 + static_cast<int>((random_ >> 8) % static_cast<uint32_t>(count - 1))) % count;
        turn = pick;
        return pick;
    }

    // `keySemitones`: pitch offset from key tracking; `detune`, `panOffset`
    // and `unisonGain` place it in a unison stack; `glide` semitones to
    // slide from.
    void startVoice(int slot, int16 channel, int16 pitch, float velocity, int keySemitones, double detune,
                    float panOffset, float unisonGain, double glide)
    {
        const Sample *sample = active_[slot];
        Voice *target = nullptr;
        for(auto &voice : voices_) {
            if(!voice.dsp)
                continue;
            if(!mlasampler_voice_is_active__ptr_struct_SamplerVoice(voice.dsp)) {
                target = &voice;
                break;
            }
            if(target == nullptr || voice.serial < target->serial)
                target = &voice; // Steal the oldest if all are busy.
        }
        if(target == nullptr)
            return;

        const float velocityGain = noteGain(slot, pitch, velocity);
        const double frames = static_cast<double>(sample->frames);

        applyEnvelope(target->dsp, slot);
        mlasampler_voice_start__ptr_struct_SamplerVoice_f64_f64_f32_f32_i32_f64_f64(
            target->dsp, frames, voiceStep(slot, sample, keySemitones + detune), voiceGain(slot, velocityGain) * unisonGain,
            panFromNorm(slotNorm(slot, kSlotPan)) + panOffset, 0, 0.0, 0.0);
        if(glide != 0.0)
            mlasampler_voice_set_glide__ptr_struct_SamplerVoice_f32_f32(target->dsp, static_cast<float>(glide), glideSeconds(slot));
        applyLoop(target->dsp, slot, sample);
        applyFilter(target->dsp, slot, pitch);
        applyLfo(target->dsp, slot);
        applyRoutes(target->dsp, slot);
        applyControllers(target->dsp, true);
        applyPitchEnvelope(target->dsp, slot);
        // Retrigger starts an LFO with the note; Free picks up where its
        // rate has taken it since activation.
        const double elapsed = framesRendered_ / sampleRate_;
        for(int lfo = 0; lfo < kLfos; ++lfo) {
            const bool retrigger = norm(lfoParamId(slot, lfo, kLfoTrigger)) >= 0.5;
            mlasampler_voice_set_lfo_phase__ptr_struct_SamplerVoice_i32_f32(
                target->dsp, lfo, retrigger ? 0.0f : static_cast<float>(std::fmod(elapsed * lfoRate(slot, lfo), 1.0)));
        }
        // Velocity and key as sources: 0..1, and -1..1 around C-4.
        const float velocityValue = std::clamp(velocity, 0.0f, 1.0f);
        const float keyValue = static_cast<float>(pitch - 60) / 60.0f;
        mlasampler_voice_set_note__ptr_struct_SamplerVoice_f32_f32(target->dsp, velocityValue, keyValue);
        // Mod routes to Start move it by velocity or key.
        double startShare = norm(static_cast<ParamID>(kStartParamBase + slot));
        for(int route = 0; route < kRoutes; ++route) {
            if(routeTarget(slot, route) != kTargetStart)
                continue;
            const int source = routeSource(slot, route);
            if(source == kSourceVelocity)
                startShare += routeAmount(slot, route) * std::clamp(velocity, 0.0f, 1.0f);
            if(source == kSourceKey)
                startShare += routeAmount(slot, route) * (pitch - 60) / 60.0;
            if(source >= kSourceModWheel)
                startShare += routeAmount(slot, route) * controllerValue(source);
        }
        mlasampler_voice_set_start__ptr_struct_SamplerVoice_f64(target->dsp,
                                                                std::floor(std::clamp(startShare, 0.0, 1.0) * frames));
        target->keySemitones = keySemitones;
        target->detune = detune;
        target->panOffset = panOffset;
        target->unisonGain = unisonGain;
        target->reversed = norm(static_cast<ParamID>(kReverseParamBase + slot)) >= 0.5;
        target->velocityGain = velocityGain;
        target->sample = sample;
        target->slot = slot;
        target->channel = channel;
        target->pitch = pitch;
        target->serial = nextSerial_++;
        // After the start frame: Beats begins its first segment there.
        target->beats = false;
        target->markers = activeMarkers_[slot];
        applyStretch(*target);
    }

    // Playback rate for a slot's note: sample rate ratio, instance and slot
    // tune, and key tracking.
    double voiceStep(int slot, const Sample *sample, double keySemitones) const
    {
        const double semitones =
            semitonesFromNorm(norm(kTuneParam)) + semitonesFromNorm(slotNorm(slot, kSlotTune)) + keySemitones;
        const double speed = tempoSync(slot) == kSyncRepitch ? tempoFactor(slot, sample) : 1.0;
        return sample->rate / sampleRate_ * std::pow(2.0, semitones / 12.0) * speed;
    }

    int tempoSync(int slot) const
    {
        const int sync = static_cast<int>(std::lround(norm(static_cast<ParamID>(kChainParamBase + slot * 4 + 1)) * (kSyncCount - 1)));
        return sync < kSyncsKnown ? sync : kSyncOff;
    }

    // How much faster than recorded a synced slot plays so its whole sample
    // lasts its Beats at the host tempo.
    double tempoFactor(int slot, const Sample *sample) const
    {
        const int index = static_cast<int>(std::lround(norm(static_cast<ParamID>(kChainParamBase + slot * 4 + 2)) * (kBeatsCount - 1)));
        const double beats = kBeats[std::clamp(index, 0, kBeatsKnown - 1)];
        const double seconds = static_cast<double>(sample->frames) / sample->rate;
        return seconds / (beats * 60.0 / (tempo_ > 0.0 ? tempo_ : 120.0));
    }

    // A Stretch or Beats slot's playhead keeps the tempo: grains keep the
    // pitch (Stretch), or each hit plays whole from its stretched time
    // (Beats). A reversed slot or a bidirectional loop crosses hits
    // backwards, so it stretches by grains instead.
    void applyStretch(Voice &voice)
    {
        const int slot = voice.slot;
        const Sample *sample = voice.sample;
        const int sync = tempoSync(slot);
        const double timeStep = sync == kSyncStretch || sync == kSyncBeats ? sample->rate / sampleRate_ * tempoFactor(slot, sample) : 0.0;
        const bool beats = sync == kSyncBeats && !voice.reversed && loopFromNorm(slotNorm(slot, kSlotLoop)) != kLoopBidirectional;
        if(!beats) {
            voice.beats = false;
            mlasampler_voice_set_stretch__ptr_struct_SamplerVoice_f64_f32(voice.dsp, timeStep, kGrainSeconds);
            return;
        }
        mlasampler_voice_set_beats__ptr_struct_SamplerVoice_f64(voice.dsp, timeStep);
        if(!voice.beats) {
            voice.beats = true;
            beginSegment(voice, false);
        }
    }

    // Beats: where a segment starting at `from` stops: the next onset, or
    // the forward loop's or the sample's end.
    double segmentEnd(const Voice &voice, size_t next) const
    {
        const Sample *sample = voice.sample;
        double limit = static_cast<double>(sample->frames);
        if(loopFromNorm(slotNorm(voice.slot, kSlotLoop)) == kLoopForward)
            limit = std::min(limit, std::floor(slotNorm(voice.slot, kSlotLoopEnd) * sample->frames));
        const auto &onsets = markerFrames(voice);
        return next < onsets.size() ? std::min(limit, onsets[next]) : limit;
    }

    // Beats: the voice's markers, or the sample's own onsets before any.
    const std::vector<double> &markerFrames(const Voice &voice) const
    {
        return voice.markers ? voice.markers->frames : voice.sample->onsets;
    }

    // Beats: markers changed under a sounding voice; its next onset is the
    // first after the playhead, and the sounding segment ends before it.
    void resyncBeats(Voice &voice)
    {
        const double at = mlasampler_voice_position__ptr_struct_SamplerVoice(voice.dsp);
        const auto &onsets = markerFrames(voice);
        size_t next = 0;
        while(next < onsets.size() && onsets[next] <= at)
            ++next;
        voice.nextOnset = next;
    }

    // Beats: a segment from the playhead to the next onset (a note's start,
    // a loop wrapping back).
    void beginSegment(Voice &voice, bool cross)
    {
        const double at = mlasampler_voice_position__ptr_struct_SamplerVoice(voice.dsp);
        const auto &onsets = markerFrames(voice);
        size_t next = 0;
        while(next < onsets.size() && onsets[next] <= at)
            ++next;
        voice.nextOnset = next;
        voice.lastPosition = at;
        mlasampler_voice_segment__ptr_struct_SamplerVoice_f64_f64_bool(voice.dsp, at, segmentEnd(voice, next), cross);
    }

    // Beats: once the playhead reaches the next onset, that hit starts.
    void followBeats(Voice &voice)
    {
        const double at = mlasampler_voice_position__ptr_struct_SamplerVoice(voice.dsp);
        const auto &onsets = markerFrames(voice);
        if(at < voice.lastPosition - 0.5) {
            beginSegment(voice, true);
            return;
        }
        voice.lastPosition = at;
        if(voice.nextOnset < onsets.size() && at >= onsets[voice.nextOnset]) {
            const double from = onsets[voice.nextOnset];
            ++voice.nextOnset;
            mlasampler_voice_segment__ptr_struct_SamplerVoice_f64_f64_bool(voice.dsp, from, segmentEnd(voice, voice.nextOnset), true);
        }
    }

    // A note's gain from its velocity: the slot's curve, then the instance
    // Velocity sensitivity times the slot's depth; and its key: Key Level
    // dB per octave from C-4.
    float noteGain(int slot, int pitch, float velocity) const
    {
        const int curve = static_cast<int>(std::lround(norm(static_cast<ParamID>(kCurveParamBase + slot * 4)) * (kCurveCount - 1)));
        float v = std::clamp(velocity, 0.0f, 1.0f);
        if(curve == 1)
            v = std::sqrt(v);
        else if(curve == 2)
            v = v * v;
        else if(curve == 3)
            v = 1.0f;
        const float sensitivity = static_cast<float>(norm(kVelocityParam) * norm(static_cast<ParamID>(kCurveParamBase + slot * 4 + 1)));
        const double keyDb = (norm(static_cast<ParamID>(kCurveParamBase + slot * 4 + 2)) * 24.0 - 12.0) * (pitch - 60) / 12.0;
        return (1.0f - sensitivity + sensitivity * v) * static_cast<float>(std::pow(10.0, keyDb / 20.0));
    }

    float voiceGain(int slot, float velocityGain) const
    {
        return gainFromNorm(norm(kLevelParam)) * gainFromNorm(slotNorm(slot, kSlotLevel)) * velocityGain;
    }

    // The slot's loop mode, points and crossfade, on a new or sounding voice.
    void applyLoop(SamplerVoice *dsp, int slot, const Sample *sample)
    {
        const double frames = static_cast<double>(sample->frames);
        mlasampler_voice_set_loop__ptr_struct_SamplerVoice_i32_f64_f64(
            dsp, loopFromNorm(slotNorm(slot, kSlotLoop)), std::floor(slotNorm(slot, kSlotLoopStart) * frames),
            std::floor(slotNorm(slot, kSlotLoopEnd) * frames));
        mlasampler_voice_set_crossfade__ptr_struct_SamplerVoice_f64(
            dsp, std::floor(norm(static_cast<ParamID>(kCrossfadeParamBase + slot)) * frames));
    }

    // The slot's filter chain and filter envelope, on a new or sounding
    // voice. Key tracking moves each stage's cutoff by its share of the key's
    // distance from C-4 (MIDI 60).
    void applyFilter(SamplerVoice *dsp, int slot, int pitch)
    {
        for(int stage = 0; stage < kFilterStages; ++stage) {
            const double keyTrack = norm(filterParamId(slot, stage, kFilterKeyTrack));
            const float cutoff = cutoffFromNorm(norm(filterParamId(slot, stage, kFilterCutoff))) *
                                 static_cast<float>(std::pow(2.0, keyTrack * (pitch - 60) / 12.0));
            mlasampler_voice_set_filter__ptr_struct_SamplerVoice_i32_i32_f32_f32_f32_f32_f32_f32(
                dsp, stage, filterTypeFromNorm(norm(filterParamId(slot, stage, kFilterType))), cutoff,
                resonanceFromNorm(norm(filterParamId(slot, stage, kFilterResonance))),
                envOctavesFromNorm(norm(filterParamId(slot, stage, kFilterEnvAmount))),
                filterGainFromNorm(norm(filterParamId(slot, stage, kFilterGain))),
                static_cast<float>(norm(filterParamId(slot, stage, kFilterDrive))),
                static_cast<float>(norm(filterParamId(slot, stage, kFilterMod)) * 2.0 - 1.0));
        }
        const int chain = static_cast<int>(std::lround(norm(static_cast<ParamID>(kChainParamBase + slot * 4)) * (kChainCount - 1)));
        mlasampler_voice_set_chain__ptr_struct_SamplerVoice_i32(dsp, chain < kChainsKnown ? chain : 0);
        const auto own = norm(static_cast<ParamID>(kSlotFilterEnvelopeParamBase + slot * kParamsPerEnvelope)) >= 0.5;
        const auto envelope = [&](int k) {
            return norm(static_cast<ParamID>(own ? kSlotFilterEnvelopeParamBase + slot * kParamsPerEnvelope + 1 + k
                                                 : kFilterEnvelopeParamBase + k));
        };
        mlasampler_voice_set_filter_envelope__ptr_struct_SamplerVoice_f32_f32_f32_f32(
            dsp, attackFromNorm(envelope(0)), timeFromNorm(envelope(1)), static_cast<float>(envelope(2)),
            timeFromNorm(envelope(3)));
    }

    // Live edits: sounding voices of `slot` take its current loop, its level,
    // pan and tune (with the instance's) and/or its filter, keeping their
    // playhead.
    void applyLive(int slot, bool loop, bool sound, bool filter, bool lfo)
    {
        if(!loop && !sound && !filter && !lfo)
            return;
        for(auto &voice : voices_) {
            if(!voice.dsp || !voice.sample || voice.slot != slot ||
               !mlasampler_voice_is_active__ptr_struct_SamplerVoice(voice.dsp))
                continue;
            if(loop)
                applyLoop(voice.dsp, slot, voice.sample);
            if(loop || sound)
                applyStretch(voice);
            if(filter)
                applyFilter(voice.dsp, slot, voice.pitch);
            if(lfo) {
                applyLfo(voice.dsp, slot);
                applyRoutes(voice.dsp, slot);
                applyPitchEnvelope(voice.dsp, slot);
            }
            if(sound) {
                mlasampler_voice_set_step__ptr_struct_SamplerVoice_f64(
                    voice.dsp, voiceStep(slot, voice.sample, voice.keySemitones + voice.detune));
                mlasampler_voice_set_gain__ptr_struct_SamplerVoice_f32_f32(
                    voice.dsp, voiceGain(slot, voice.velocityGain) * voice.unisonGain,
                    panFromNorm(slotNorm(slot, kSlotPan)) + voice.panOffset);
            }
        }
    }

    void noteOff(int16 channel, int16 pitch)
    {
        // A mono or legato slot whose sounding key comes up goes back to the
        // newest key still held, gliding, instead of releasing.
        for(int slot = 0; slot < kNumSlots; ++slot) {
            if(playMode(slot) == kPlayPoly || heldCount_[slot] == 0)
                continue;
            removeHeld(slot, pitch);
            if(heldCount_[slot] == 0 || lastPitch_[slot] != pitch || !slotSounding(slot))
                continue;
            const int back = held_[slot][heldCount_[slot] - 1];
            int keySemitones = 0;
            if(zoneNorm(slot, kZoneMode) >= 0.5 && zoneNorm(slot, kZoneTrack) >= 0.5)
                keySemitones = back - rootKeyFromNorm(zoneNorm(slot, kZoneRoot));
            retarget(slot, static_cast<int16>(back), 1.0f, keySemitones);
            lastPitch_[slot] = back;
            lastKey_[slot] = keySemitones;
        }
        for(auto &voice : voices_)
            if(voice.dsp && voice.channel == channel && voice.pitch == pitch)
                mlasampler_voice_release__ptr_struct_SamplerVoice(voice.dsp);
    }

    // `buses[b]` is null for a bus this block cannot write; Main never is.
    void render(float *(&buses)[kNumBuses][2], int32 from, int32 to)
    {
        if(from >= to)
            return;
        for(auto &voice : voices_) {
            if(!voice.dsp || !voice.sample || !mlasampler_voice_is_active__ptr_struct_SamplerVoice(voice.dsp))
                continue;
            const int bus = outputFromNorm(slotNorm(voice.slot, kSlotOutput));
            const bool routed = bus > 0 && bus < kNumOutputs && buses[bus][0] != nullptr;
            float *outL = routed ? buses[bus][0] : buses[0][0];
            float *outR = routed ? buses[bus][1] : buses[0][1];
            // Sends: the voice's sound again, times the slot's send levels.
            float sendLevel[kNumSends] = {};
            for(int send = 0; send < kNumSends; ++send)
                if(buses[kNumOutputs + send][0] != nullptr)
                    sendLevel[send] = static_cast<float>(norm(static_cast<ParamID>(kSendParamBase + voice.slot * 4 + send)));
            const float *pcm = voice.sample->stereo.data();
            const int64_t guard = voice.sample->frames; // Index of the silent guard frame.
            // A reversed voice reads the sample mirrored: its frame f is the
            // sample's last frame minus f. Loops, the start and the crossfade
            // then work unchanged, in the reversed sample's timeline.
            const bool reversed = voice.reversed;
            const auto at = [pcm, guard, reversed](int64_t f) {
                return pcm + (reversed && f < guard ? guard - 1 - f : f) * 2;
            };
            for(int32 i = from; i < to; ++i) {
                if(voice.beats)
                    followBeats(voice);
                int64_t frame = static_cast<int64_t>(mlasampler_voice_frame__ptr_struct_SamplerVoice(voice.dsp));
                int64_t next = static_cast<int64_t>(mlasampler_voice_next_frame__ptr_struct_SamplerVoice(voice.dsp));
                frame = std::clamp<int64_t>(frame, 0, guard - 1);
                next = std::clamp<int64_t>(next, 0, guard);
                const float *a = at(frame);
                const float *b = at(next);
                // During a crossfade, the frames a loop length back fade in.
                const double shadowFrame = mlasampler_voice_shadow_frame__ptr_struct_SamplerVoice(voice.dsp);
                const int64_t shadow = shadowFrame < 0 ? 0 : std::clamp<int64_t>(static_cast<int64_t>(shadowFrame), 0, guard - 1);
                const float *c = at(shadow);
                const float *d = at(shadow + 1);
                float right = 0.0f;
                const float left = mlasampler_voice_render__ptr_struct_SamplerVoice_f32_f32_f32_f32_f32_f32_f32_f32_ptr_f32(
                    voice.dsp, a[0], a[1], b[0], b[1], c[0], c[1], d[0], d[1], &right);
                outL[i] += left;
                outR[i] += right;
                for(int send = 0; send < kNumSends; ++send)
                    if(sendLevel[send] > 0.0f) {
                        buses[kNumOutputs + send][0][i] += left * sendLevel[send];
                        buses[kNumOutputs + send][1][i] += right * sendLevel[send];
                    }
                if(!mlasampler_voice_is_active__ptr_struct_SamplerVoice(voice.dsp)) {
                    voice.sample = nullptr;
                    break;
                }
            }
        }
    }
};

} // namespace mla_sampler_plugin

BEGIN_FACTORY_DEF("MLang", "https://github.com/mattilaa/mlang", "mailto:devnull@example.invalid")
DEF_CLASS2(INLINE_UID_FROM_FUID(mla_sampler_plugin::kProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass,
           "Mla Sampler", 0, PlugType::kInstrumentSampler, "0.1.0", kVstVersionString,
           mla_sampler_plugin::Processor::createInstance)
END_FACTORY
