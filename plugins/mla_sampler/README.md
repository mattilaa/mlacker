# Mla Sampler

A VST3 **sampler instrument** (subcategory `Instrument|Sampler`) with looping
samples, key zones and multiple outputs. Each instance holds up to 16 samples
("slots"), each on the instance's amp ADSR or its own. A slot plays on one key like
[Mla Drum](../mla_drum)'s pads, or across a key zone, pitched from its root
key. Every slot has its own loop, set to off, forward or bidirectional between a
start and an end point, and its own output bus.

The per-sample voice math (playhead, loop wrap and reflection, linear
interpolation, ADSR, constant-power pan) is MLang: `src/mla_sampler_dsp.mla` on
top of the shared [`dsp::envelope`](../../subprojects/mlang/modules/dsp/envelope.mla)
module. The C++ layer (`src/plugin.cpp`) owns the sample buffers, the 32-voice
pool and the output buses. It schedules MIDI sample-accurately, maps parameters
and persists state.

## Slots and keys

Each slot has a **Key Mode**:

- **Pad** (the default): slot 1 plays at **Root Key** (default MIDI note 36),
  slot 2 at 37, and so on up to slot 16, at the sample's recorded pitch.
- **Zone**: the slot plays on every key from **Low Key** to **High Key**. With
  **Key Track** on (the default), **Zone Root** plays the sample at its
  recorded pitch and each key a semitone away shifts it a semitone, so one
  sample covers the keyboard. With Key Track off, every key in the zone plays
  the recorded pitch.

Each slot also has a velocity range, **Vel Low** to **Vel High** (MIDI 1-127,
the whole range by default), in either mode: it plays only notes that hard.
Two slots on the same keys with ranges 1-63 and 64-127 switch samples by how
hard a key is hit.

Zones may overlap: every slot whose key or zone and velocity range hold a note
plays it, so slots layer. Slots in the same **Group** (1-8) take turns
instead: of a group's slots that match a note, only one plays. **Group Mode**
picks it round-robin, each in turn (the default), or at random, never the same
slot twice in a row. Put several recordings of one hit on the same key in a
group for natural repeats; slots without a group still layer on top.

A **Choke** group (1-8) cuts notes off: when a slot in a choke group starts a
note, every sounding note of that choke group fades out over 3 ms, the same
slot's earlier notes included (a closed hi-hat stopping the open one, or a
long sample that should not overlap itself). Slots that one note starts
together, such as layers, do not choke each other. Keys no slot covers, and slots without a sample, are silent.
Global and slot Tune add to the key tracking. Note-off releases the envelope. A looping slot keeps looping through the release until
the envelope ends. A slot with loop off ends with its sample, even while the key
is held.

Mono/stereo samples at any rate from 1 kHz to 384 kHz are resampled to the host
rate. Slots are filled the same ways as Mla Drum's pads:

- **From mlacker**, in the Sampler pane (**Shift+S** in the Pattern view, see
  mlacker's README) or with **Instrument → Drum pads**.
- **From any host**, through the `IConnectionPoint` messages in
  [`stdlib/include/mla_sampler_protocol.h`](../../subprojects/mlang/stdlib/include/mla_sampler_protocol.h),
  where slots are called pads: `mlang.sampler.loadFile`, `mlang.sampler.loadPcm`,
  `mlang.sampler.clear` and `mlang.sampler.info`. The plug-in has no editor.
- **From saved state.** The VST3 component state embeds every slot's audio.

## Loops

| Loop          | Playback |
|---------------|----------|
| Off           | Plays the sample once. |
| Forward       | Plays to the loop end, then jumps back to the loop start. |
| Bidirectional | Plays to the loop end, then back to the loop start and forward again. |

Loop start and end are fractions of the sample length (0..1), so they stay
valid when a slot gets a different sample. A loop shorter than two frames plays
as Off. A slot's **Start** skips the beginning of its sample (silence or an
attack): playback begins there. **Reverse** plays a slot's sample backwards:
its loop points, start and crossfade then count from the sample's end, as if
the sample were stored reversed. It is read when a note starts, so notes
already sounding keep their direction. A start past a forward or bidirectional loop's
end plays on into the loop. Interpolation across the loop end of a forward loop reads the loop
start, so the seam has no jump to the frame after the loop.

Edits reach notes already sounding: loop mode, loop points and crossfade
(without restarting the playhead), level, pan and tune, the envelope and the
output. A sounding note past a new forward loop's end wraps into the loop, and
a loop turned off plays on to the end of the sample. Key zones, velocity ranges
and the start point decide how a note begins, so they apply from the next
note.

A forward loop whose end and start do not match (a click at the seam) can
**crossfade**: over the loop's last *n* frames, the tail fades out while the
audio the same distance before the loop start fades in (equal-power). The jump
back to the loop start then continues exactly what was fading in. The
**Crossfade** length is a fraction of the sample like the loop points. It is
capped by the loop length and by the frames before the loop start, so a loop
starting at frame 0 cannot crossfade. Off and bidirectional loops ignore it.

## Filters

Each voice runs through a **chain of filter stages** in series, after the
playhead and before the amp envelope. There are two stages today (**Filter 1**
then **Filter 2**). Each has a **Type**, **Cutoff** (20 Hz .. 20 kHz),
**Resonance** (0 .. 36 dB), **Env** (how far the filter envelope moves the
cutoff, -8 .. +8 octaves), **Key Track** (0 .. 100 % of the key's distance
from C-4, so higher notes open the filter) and **Gain** (-24 .. +24 dB, for
peak and shelves). A stage set to Off passes the
signal through.

| Number | Type      | Notes |
|--------|-----------|-------|
| 0      | Off       | |
| 1, 2   | LP 12, LP 24 | Low-pass, 12 or 24 dB/octave (`dsp::multimode`) |
| 3, 4   | HP 12, HP 24 | High-pass |
| 5, 6   | BP 12, BP 24 | Band-pass |
| 7, 8   | Ladder 12, Ladder 24 | Moog-style ladder low-pass |
| 9      | Notch     | Band-reject biquad |
| 10-13  | SVF LP, SVF HP, SVF BP, SVF Notch | Topology-preserving state-variable filter (Cytomic): stays smooth and stable while the filter envelope sweeps it fast |
| 14     | Peak      | Bell EQ: Gain at the cutoff, Resonance narrows it |
| 15, 16 | Low Shelf, High Shelf | Gain below / above the cutoff |
| 17     | Vowel     | Three formant band-passes; the cutoff's place in its range morphs A, E, I, O, U, so the filter envelope sweeps vowels. Resonance narrows the formants |
| 18, 19 | Comb +, Comb - | Feedback comb tuned to the cutoff (one cycle of delay): peaks at its harmonics (+) or odd harmonics (-). Resonance is the feedback; peaks are kept near unity. Key tracking at 100 % makes it play in tune |
| 20     | Flanger   | The input plus itself one cutoff cycle later: notches. Resonance feeds the delay back. Sweep it with the LFO or filter envelope |
| 21     | Phaser    | Four first-order all-passes around the cutoff, mixed with the input: notches. Resonance is feedback |

Resonance is a Q for the state-variable, peak and shelf types: 0 dB is 0.707
(Butterworth), 36 dB about 45. The combs and flanger keep a delay line of
about 50 ms (a 20 Hz cycle) per channel, a `std::ringbuffer` allocated when
the voices are, off the audio thread.

The type list is built to grow. Its parameter always has 64 entries, so a
saved type never changes meaning. New filter models take the next number, and
the ones not yet written show as `(reserved)` and pass the signal through.
The parameter IDs also leave room for up to four stages of up to eight fields
per slot, so more stages in the chain, or more settings per stage (drive,
modulation), only add parameters after the existing ones. In the voice
(`src/mla_sampler_dsp.mla`), a stage is a `SamplerFilter`: a new model adds its
state there and a branch to `configure`/`process`.

The **filter envelope** mirrors the amp envelope: the instance has one
(**Filter Attack/Decay/Sustain/Release**, a pluck by default: no sustain), and
each slot uses it or its own (**Slot N Filter Envelope**: Instance or Own). It
starts and releases with the note. Coefficients follow it every 16 frames.
Filter and filter envelope edits reach notes already sounding.

## LFOs

Each slot has two LFOs, **LFO 1** and **LFO 2**, alike. Each modulates its **Pitch** (-12 .. +12 semitones,
vibrato), its filter **Cutoff** (-4 .. +4 octaves on every stage, wobble) and
its **Level** (0 .. 100 %, tremolo: full level at the top of the cycle, down by
the depth at the bottom). Its **Shape** is Sine, Triangle, Saw Up, Saw Down,
Square or S&H (a random value held for each cycle). **Rate** runs 0.05 .. 20
Hz, or with **Sync** on, a **Division** of the host tempo: 1/1 .. 1/32, dotted
and triplet. **Delay** (0 .. 2 s) fades its depth in after the note starts.
**Trigger** Retrigger (the default) starts each note's LFO from the top of its
cycle; Free keeps it running in time, so notes join it wherever it is. Depths
default to 0, so the LFO does nothing until one is set. Pitch and cutoff
follow it every 16 frames and level every frame, and edits reach sounding
notes.

As with the filter types, the shape and division lists keep a fixed length
(16 entries, the unused ones reserved), and each LFO has room for 16 settings.

## Mod matrix

Each slot has four **mod routes**. A route sends a **Source** (LFO 1, LFO 2,
Amp Env, Filter Env, Velocity, Key: -1 .. +1 over five octaves either side
of C-4, or a MIDI controller: Mod Wheel, Aftertouch, Pitch Bend -1 .. +1, or
Mod CC) to a **Target**, scaled by its **Amount** (-100 .. +100 %). At 100 % a
source at full swing moves:

| Target    | By |
|-----------|----|
| Pitch     | 24 semitones |
| Cutoff    | 8 octaves, every filter stage |
| Resonance | 36 dB |
| Level     | the whole level (1 + amount x source, 0 .. 2) |
| Pan       | across the stereo field |
| Start     | the whole sample, when the note starts (Velocity, Key and the controllers' values then) |

Routes add to each other and to the LFOs' direct depths. The voice evaluates
them every 16 frames, with the filter; level from routes glides across those
frames, so it does not step. Routes reach sounding notes. Both lists keep 16
entries (the unused ones reserved), and the IDs leave room for 8 routes per
slot, so new sources, targets and routes only add to the end.

While a note plays, a jump in the cutoff's envelope or LFO movement (a square
LFO) glides over about 1.5 ms: a biquad holding signal rings far out of range
when its cutoff leaps several octaves at once. A note's first cutoff is not
smoothed, so an instant filter attack still opens at once.

## MIDI controllers

Mla Sampler tells the host (through VST3 `IMidiMapping`) which parameter each
controller drives, so the controllers arrive as parameter changes:

| Controller | Parameter |
|------------|-----------|
| CC 1 (mod wheel) | Mod Wheel |
| Channel pressure | Aftertouch |
| Pitch bend | Pitch Bend |
| CC 2, 4, 11, 16-19, 74 | CC 2 Breath .. CC 74 Brightness |
| CC 7, 72, 73, 75 | Level, Release, Attack, Decay |

**Pitch Bend** always bends every sounding voice by up to **Bend Range**
semitones (0 .. 24, default 2) either way, and is also a mod source. **Mod CC**
chooses which of the eight CC parameters the Mod CC source reads. A host asks
for the mapping once, so each choosable CC has a parameter of its own rather
than one "any CC" parameter. The controller values reach sounding notes at the
next 16-frame step and new notes from their start. Controllers apply to the
whole instance: every channel, every slot.

## Unison and play modes

**Unison** stacks up to 8 voices per note on a slot (a 16-entry list; 1 is
off). **Detune** (0 .. 100 cents) spreads their pitch evenly between minus and
plus that amount, and **Spread** (0 .. 100 %) pans them across the stereo field
the same way. The stack is scaled by 1/sqrt(voices), so adding voices keeps
the loudness roughly even. Each voice counts against the 32-voice pool.

**Play Mode** (an 8-entry list):

| Mode   | Playing |
|--------|---------|
| Poly   | Every note plays its own voices. |
| Mono   | One note at a time: a new note cuts the slot's sounding one and restarts. |
| Legato | As Mono, but a note played while another is held keeps the playhead and envelopes, and only changes pitch. |

In Mono and Legato a slot remembers the keys held, so releasing the newest
returns to the previous one still held. **Glide** (0 .. 2 s, quadratic) slides
from the previous note's pitch to the new one in Mono and Legato; in Poly it
does nothing.

## Outputs

Bus 0 is **Main**. Buses 1-7 are the auxiliary stereo outputs **Out 2**..**Out 8**,
inactive by default as VST3 expects. Each slot's **Output** parameter picks
its bus. A slot sent to an output the host has not activated plays on Main, so
nothing goes silent in hosts that only use the main output. mlacker activates
every bus: they play with Main until routed to their own mixer channels (see
mlacker's README, "Plugin outputs").

## Parameters

| Parameter          | Range                         | Notes |
|--------------------|-------------------------------|-------|
| Level              | -60 .. +6 dB (bottom = off)   | Instance output level. MIDI CC 7. |
| Tune               | ±24 semitones                 | Added to each slot's tune. |
| Velocity           | 0..1                          | 0 = every note at full level, 1 = fully velocity-scaled. |
| Root Key           | MIDI 0..127                   | Key of slot 1. |
| Attack             | 0 .. 2 s                      | Linear. MIDI CC 73. |
| Decay              | 1 ms .. 10 s                  | Exponential. MIDI CC 75. |
| Sustain            | 0..1                          | |
| Release            | 1 ms .. 10 s                  | Exponential, from note-off. MIDI CC 72. |
| Slot N Level       | -60 .. +6 dB                  | |
| Slot N Pan         | L .. R                        | Constant-power; centre is unity in both channels. |
| Slot N Tune        | ±24 semitones                 | |
| Slot N Output      | Main, Out 2 .. Out 8          | |
| Slot N Loop        | Off, Forward, Bidirectional   | |
| Slot N Loop Start  | 0..1 of the sample            | Default 0. |
| Slot N Loop End    | 0..1 of the sample            | Default 1 (the whole sample). |
| Slot N Key Mode    | Pad, Zone                     | Default Pad. |
| Slot N Low Key     | MIDI 0..127                   | Zone's lowest key. Default 0. |
| Slot N High Key    | MIDI 0..127                   | Zone's highest key. Default 127. |
| Slot N Zone Root   | MIDI 0..127                   | Key at the recorded pitch. Default 60 (C-4). |
| Slot N Key Track   | Off, On                       | Default On. |
| Slot N Crossfade   | 0..1 of the sample            | Forward-loop crossfade length. Default 0 (none). |
| Slot N Vel Low     | MIDI velocity 1..127          | Softest note the slot plays. Default 1. |
| Slot N Vel High    | MIDI velocity 1..127          | Hardest note the slot plays. Default 127. |
| Slot N Envelope    | Instance, Own                 | Instance (the default) uses Attack..Release above. |
| Slot N Attack      | 0 .. 2 s                      | With Own. Ranges as the instance's. |
| Slot N Decay       | 1 ms .. 10 s                  | With Own. |
| Slot N Sustain     | 0..1                          | With Own. |
| Slot N Release     | 1 ms .. 10 s                  | With Own. |
| Slot N Start       | 0..1 of the sample            | Where playback begins. Default 0. |
| Slot N Group       | Off, 1 .. 8                   | Slots in one group take turns. Default Off (layers). |
| Group Mode         | Round-robin, Random           | How a group picks its slot. Default Round-robin. |
| Filter Attack .. Release | as the amp envelope's   | The instance filter envelope. Sustain defaults to 0. |
| Slot N Filter Envelope | Instance, Own             | Own uses the four below. |
| Slot N Filter Attack .. Release | as above         | With Own. |
| Slot N Filter T Type | 64-entry list (see Filters) | Stage T = 1, 2. Default Off. |
| Slot N Filter T Cutoff | 20 Hz .. 20 kHz           | Logarithmic. Default 20 kHz. |
| Slot N Filter T Resonance | 0 .. 36 dB             | Default 0. |
| Slot N Filter T Env | -8 .. +8 octaves             | Filter envelope depth. Default 0. |
| Slot N Filter T Key Track | 0 .. 100 %             | Default 0. |
| Slot N Choke       | Off, 1 .. 8                   | Choke group. Default Off. |
| Slot N Reverse     | Off, On                       | Plays the sample backwards. Default Off. |
| Slot N Filter T Gain | -24 .. +24 dB               | Peak and shelves. Default 0. |
| Slot N LFO 1 Shape | Sine, Triangle, Saw Up, Saw Down, Square, S&H | 16-entry list. Default Sine. |
| Slot N LFO 1 Rate  | 0.05 .. 20 Hz                 | Without sync. Default 5 Hz. |
| Slot N LFO 1 Sync  | Off, On                       | Default Off. |
| Slot N LFO 1 Division | 1/1 .. 1/32, dotted, triplet | With sync. Default 1/4. |
| Slot N LFO 1 Delay | 0 .. 2 s                      | Depth fade-in. Default 0. |
| Slot N LFO 1 Pitch | -12 .. +12 semitones          | Default 0. |
| Slot N LFO 1 Cutoff | -4 .. +4 octaves             | Default 0. |
| Slot N LFO 1 Level | 0 .. 100 %                    | Tremolo. Default 0. |
| Slot N LFO 1 Trigger | Free, Retrigger             | Default Retrigger. |
| Slot N LFO 2 ...   | as LFO 1                      | |
| Slot N Mod R Source | Off, LFO 1, LFO 2, Amp Env, Filter Env, Velocity, Key, Mod Wheel, Aftertouch, Pitch Bend, Mod CC | Route R = 1 .. 4. 16-entry list. Default Off. |
| Slot N Mod R Target | Off, Pitch, Cutoff, Resonance, Level, Pan, Start | 16-entry list. Default Off. |
| Slot N Mod R Amount | -100 .. +100 %               | Default 0. |
| Slot N Unison      | 1 .. 8 voices                 | 16-entry list. Default 1. |
| Slot N Detune      | 0 .. 100 cents                | Unison pitch spread. Default 0. |
| Slot N Spread      | 0 .. 100 %                    | Unison stereo spread. Default 0. |
| Slot N Play Mode   | Poly, Mono, Legato            | 8-entry list. Default Poly. |
| Slot N Glide       | 0 .. 2 s                      | Mono and Legato. Default 0. |
| Bend Range         | 0 .. 24 semitones             | Pitch bend either way. Default 2. |
| Mod CC             | CC 2, 4, 11, 16, 17, 18, 19, 74 | Which CC the Mod CC source reads. 16-entry list. Default CC 2. |
| Mod Wheel, Aftertouch | 0 .. 1                     | Set by CC 1 and channel pressure. Default 0. |
| Pitch Bend         | 0 .. 1 (centre 8192/16383)    | Set by pitch bend. Default centre. |
| CC 2 Breath .. CC 74 Brightness | 0 .. 1           | Set by their CCs. Default 0. |

Parameter IDs are stable. Globals are 100-107; slot `s` (0-based) uses:

- `200 + 7s` + 0 level, 1 pan, 2 tune, 3 output, 4 loop, 5 loop start, 6 loop end
- `400 + 5s` + 0 key mode, 1 low key, 2 high key, 3 zone root, 4 key track
- `500 + s`: crossfade
- `600 + 2s` + 0 velocity low, 1 velocity high
- `700 + 5s` + 0 envelope, 1 attack, 2 decay, 3 sustain, 4 release
- `800 + s`: start
- `900 + s`: group
- `1100 + 5s` + 0 filter envelope, 1 attack, 2 decay, 3 sustain, 4 release
- `2000 + 32s + 8t` + 0 type, 1 cutoff, 2 resonance, 3 env, 4 key track, 5
  gain, for filter stage `t` (0-based; room for 4 stages of 8 fields). The
  gains were added later and are registered after the choke groups.
- `1200 + s`: choke group
- `1300 + s`: reverse
- `3000 + 32s + 16l` + 0 shape, 1 rate, 2 sync, 3 division, 4 delay, 5 pitch,
  6 cutoff, 7 level, 8 trigger, for LFO `l` (0-based: LFO 1 and LFO 2, 16
  fields each; LFO 2 was added later and is registered after LFO 1's block)
- `4000 + 32s + 4r` + 0 source, 1 target, 2 amount, for mod route `r`
  (0-based; room for 8 routes of 4 fields)
- `1400 + 4s` + 0 unison voices, 1 detune, 2 spread
- `1500 + 4s` + 0 play mode, 1 glide

`950` is the Group Mode and `960`-`963` the instance filter envelope. The MIDI
controllers are `110` Bend Range, `111` Mod CC, `112` Mod Wheel, `113`
Aftertouch, `114` Pitch Bend and `120`-`127` the CC values, registered last.

Each later block comes after all earlier parameters, so presets and states
saved before it load with its defaults: every slot in Pad mode, no crossfade,
every velocity, the instance envelope, starting at frame 0, no group, filters
off, no choke group, LFO depths at 0, no mod routes, forwards, one voice, Poly,
a 2-semitone bend range.

A slot set to its own envelope keeps it for every note, so a short one-shot pad
and a sustained, looping zone can share one instance. Envelope edits reach
notes already sounding, as the instance envelope's do. With all 32 voices busy, the oldest is stolen.

## Build

mlacker's `./build.sh --plugins` builds it with the other plugins. On its own,
from this directory:

```sh
../../subprojects/mlang/build/mlang pkg --config mlang.toml build   # -> build/cmake/VST3/Release/MlaSampler.vst3
../../subprojects/mlang/build/mlang pkg --config mlang.toml run test
```

`test` builds and runs `tests/mla_sampler_tests.cpp`, an offline host that
checks rendered audio: the bus layout, slot/key mapping, key zones (pitch
tracking up and down, keys outside a zone, Key Track off, layering beside
pads), filters (low-, high-pass and notch, two stages in series, a reserved type
passing through, the filter envelope and a slot's own, key tracking; the
state-variable types, a full-resonance sweep staying bounded, peak and shelf
gains, vowel morphing; combs, flanger and phaser, a comb at full feedback swept fast
staying bounded), the LFO (tremolo, vibrato, a filter wobble that stays in
range, tempo sync, delay, retrigger and free, sample & hold), LFO 2 and the mod
matrix (LFO 2 to pan, velocity to level and start, filter envelope to pitch,
LFO 1 to resonance staying bounded, a route edited mid-note), reverse (backwards, with a start and a forward loop
in the reversed timeline), unison (the stack's level, stereo spread and
detune), Mono replacing a note, Legato keeping the envelope, glide and
returning to a held key), MIDI controllers (the host mapping, pitch bend and
its range on new and sounding notes, the mod wheel, aftertouch and chosen CC as
sources, pitch bend as a source), choke
groups (cutting another slot and a retrigger, layers left alone), groups
(round-robin turns, random picks without repeats, an ungrouped slot
layering on top), live edits on a sounding note (a loop turned on or off, including a
bidirectional loop on its way back, tune and level), velocity layers (soft and hard layers, their boundary, a pad limited
to soft notes), per-slot envelopes beside the instance one, the sample start
(to the exact frame, and past a loop's end), a forward loop's seam with and without a crossfade, loop off / forward /
bidirectional, per-slot output routing with the fallback to Main, and state
round trips. In mlacker's plugin tree it is also the CTest test
`sampler_processor` (`./build.sh --test`). The SDK's `validator` passes (47/47).
