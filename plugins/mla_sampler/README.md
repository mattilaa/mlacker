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
plays it, so slots layer. Keys no slot covers, and slots without a sample, are silent.
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
as Off. Interpolation across the loop end of a forward loop reads the loop
start, so the seam has no jump to the frame after the loop. Loop settings are
read when a note starts. Notes already sounding keep theirs.

A forward loop whose end and start do not match (a click at the seam) can
**crossfade**: over the loop's last *n* frames, the tail fades out while the
audio the same distance before the loop start fades in (equal-power). The jump
back to the loop start then continues exactly what was fading in. The
**Crossfade** length is a fraction of the sample like the loop points. It is
capped by the loop length and by the frames before the loop start, so a loop
starting at frame 0 cannot crossfade. Off and bidirectional loops ignore it.

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

Parameter IDs are stable: globals are 100-107 and slot `s` (0-based) uses
`200 + 7s` + (0 level, 1 pan, 2 tune, 3 output, 4 loop, 5 loop start, 6 loop
end), `400 + 5s` + (0 key mode, 1 low key, 2 high key, 3 zone root, 4 key
track), `500 + s` for the crossfade and `600 + 2s` + (0 velocity low, 1
velocity high) and `700 + 5s` + (0 envelope, 1 attack, 2 decay, 3 sustain, 4
release). Each later block comes after all earlier parameters, so
presets and states saved before it load with its defaults: every slot in Pad
mode, no crossfade, every velocity, the instance envelope.

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
pads), velocity layers (soft and hard layers, their boundary, a pad limited
to soft notes), per-slot envelopes beside the instance one, a forward loop's seam with and without a crossfade, loop off / forward /
bidirectional, per-slot output routing with the fallback to Main, and state
round trips. In mlacker's plugin tree it is also the CTest test
`sampler_processor` (`./build.sh --test`). The SDK's `validator` passes (47/47).
