# Mla Paula

A VST3 **instrument** (subcategory `Instrument|Sampler`) emulating the Amiga's
**Paula** audio chip as a tracker drives it: four DMA channels of signed 8-bit
samples, each playing at the rate an integer *period* sets, with a 0..64
volume. The analog stage after the DACs is the A500's (or A1200's).

The channel and filter math is MLang: `src/mla_paula_dsp.mla`. The C++ layer
(`src/plugin.cpp`) owns the sample memory and the voices, schedules MIDI
sample-accurately, maps parameters and persists state.

## How it plays

- **8-bit samples.** Every sample is reduced to signed 8-bit mono when it
  arrives (stereo is averaged), as it would sit in chip memory.
- **Periods.** A note's playback rate becomes the nearest period:
  rate = clock / period, with 3546895 Hz for **PAL** and 3579545 Hz for
  **NTSC**. That quantizes pitch the way the machine does: the higher the note,
  the coarser the steps. Periods below **124** (about 28.6 kHz PAL), which the
  audio DMA cannot fetch, play at 124. A slot's sample plays at its own rate on
  its **Root Key**.
- **Volume.** Paula's register is linear, 0..64: a slot's **Volume** times
  the note's velocity, rounded.
- **Ticks.** Envelopes (Attack, Release) and vibrato step once per vertical
  blank, 50 Hz PAL or 60 Hz NTSC, as a replay routine does. Vibrato is
  ProTracker's effect `4xy`: a 64-step sine, **Speed** steps per tick, the
  period moved by up to 255 × **Depth** / 128.
- **Interpolation.** **Paula** holds each sample word until the next one
  (zero-order hold): the hard, aliased Amiga sound. **Linear** draws a straight
  line between words, as many emulators and players offer: smoother at
  low sample rates while the period quantization, 8-bit words and output
  filter stay.
- **Output stage.** The channels and filters run oversampled (near 192 kHz)
  and are averaged down to the host rate. The **A500** has a first-order RC
  low-pass at about 4.4 kHz; the **A1200**'s is about 34 kHz. The **LED
  Filter** adds the two-pole Butterworth-like low-pass (about 3.3 kHz) that
  the power LED's brightness showed. Coupling capacitors take out DC (about 5 Hz).
- **Stereo.** Channels 1 and 4 are left, 2 and 3 right. **Separation** 100 %
  is the machine's hard panning; lower values blend each side into the other.

## Channels

**Channel** picks how notes take Paula's channels:

| Channel   | Playing |
|-----------|---------|
| Auto      | Four voices: a note takes a free channel (or the oldest one), each on its own side. |
| 1 Left .. 4 Left | One fixed channel: one note at a time. A new note restarts the channel, as a tracker row does. |

With a fixed channel, **Legato** On lets a note played while the previous one
is still held change only the period and volume, without restarting the
sample: a tracker arpeggio, or a slide without the slide. Put four instances
on channels 1..4 to play a module's four tracks.

A note-off releases the note over **Release** (0 = cut at once). A sample
with its loop Off ends by itself.

## Register mode

A replay routine on the Amiga writes Paula's registers once per frame. Mla
Paula can be driven the same way, by non-commercial SysEx packets (without
F0/F7), so a converted module plays exactly as the machine plays it, with
its per-frame vibrato, arpeggios and volume envelopes:

```
7D 'M' 'P' 'A' 'U' 01  channel(0-3)  frames(1-8)  step-us(3 x 7 bits, LE)  flags
[snapshot: byte count, writes]   then per frame: byte count, writes
```

Writes, applied in order at their frame (frame *k* sounds *k* × step after the
packet arrives): `01 a b c` AUDxLC, a byte offset into **slot 1's sample**,
which is chip memory here (21 bits; `1FFFFF` is outside it and reads
silence); `02 a b c` AUDxLEN in words; `03 a b c` AUDxPER; `04 v` AUDxVOL;
`05` DMA on; `06` DMA off. Switched on, DMA latches LC/LEN and plays that
block, then reloads whatever LC/LEN hold at its end, as Paula does. Flag 1
stops all channels first (a song's start); flag 2 adds a snapshot of the
channel's registers that only applies to a channel starting cold, so playback
can begin on any pattern. In mlacker a packet is a row's SYSEX cell (ID `7D`)
on a track playing the instrument.

A note leaves register mode. A channel that receives no packet for 0.3 s
stops (the transport stopped, a track muted). Interpolation, the output
filters and separation still apply.

## Slots

Up to 16 samples ("slots"), filled through the `IConnectionPoint` messages in
[`stdlib/include/mla_sampler_protocol.h`](../../subprojects/mlang/stdlib/include/mla_sampler_protocol.h)
(where slots are called pads), as mlacker fills Mla Drum's pads. Each slot
plays the keys from **Low Key** to **High Key**; of several slots holding a
key, the lowest numbered with a sample plays it. **Loop** Forward plays the
sample once, then repeats **Loop Start** .. **Loop End** (fractions of its
length), as an Amiga instrument's repeat. **Finetune** is ProTracker's,
-8 .. +7 eighths of a semitone.

## Parameters

| Parameter          | Range                         | Notes |
|--------------------|-------------------------------|-------|
| Level              | -60 .. +6 dB (bottom = off)   | MIDI CC 7. Each channel is at half scale, so two on one side reach full scale. |
| Tune               | ±24 semitones                 | |
| Velocity           | 0..1                          | 0 = every note at the slot's volume. |
| Channel            | Auto, 1 Left, 2 Right, 3 Right, 4 Left | |
| Interpolation      | Paula, Linear                 | Default Paula (hold). |
| Model              | A500, A1200                   | Fixed output filter. |
| LED Filter         | Off, On                       | |
| Clock              | PAL, NTSC                     | Period clock and tick rate. |
| Separation         | 0 .. 100 %                    | Default 100 % (hard). |
| Legato             | Off, On                       | Fixed channels only. |
| Attack             | 0 .. 2 s                      | Volume rise, in ticks. |
| Release            | 0 .. 2 s                      | Volume fall after note-off; 0 cuts. |
| Vibrato Speed      | 0 .. 15                       | |
| Vibrato Depth      | 0 .. 15                       | 0 = off. |
| Vibrato Delay      | 0 .. 2 s                      | Before the vibrato starts. |
| Slot N Low Key     | MIDI 0..127                   | Default 0. |
| Slot N High Key    | MIDI 0..127                   | Default 127. |
| Slot N Root Key    | MIDI 0..127                   | Key at the sample's own rate. Default 60 (C-4). |
| Slot N Loop        | Off, Forward                  | |
| Slot N Loop Start  | 0..1 of the sample            | Default 0. |
| Slot N Loop End    | 0..1 of the sample            | Default 1. |
| Slot N Volume      | 0 .. 64                       | Default 64. |
| Slot N Finetune    | -8 .. +7                      | Default 0. |

Parameter IDs: the globals 100..114 in the order above, then slot *s* (0-15)
at `200 + 8s` + field (Low Key, High Key, Root Key, Loop, Loop Start, Loop End,
Volume, Finetune).

## State

The VST3 state holds every parameter and each slot's 8-bit sample (name, rate,
words). mlacker resends sampler pads from its session when a project opens.

## Tests

`tests/mla_paula_tests.cpp` loads the built bundle and checks the fixed
channels' sides, period quantization (428 and 214 for an 8287 Hz sample at its
root and an octave up, and the 124 floor), hold vs linear interpolation, the
tick attack, legato, a state round trip, and register mode (a packet's period, side and frame timing, the timeout, a note leaving it):

```sh
mlang pkg --config plugins/mla_paula/mlang.toml run test
```

## Examples

- [`examples/shadow_of_the_beast_intro.mlaproj`](../../examples/shadow_of_the_beast_intro.mlaproj):
  David Whittaker's music for *Shadow of the Beast* (Psygnosis, 1989) that
  plays while disk 1 loads the game's intro, in register mode, with the
  game's own samples and sequence. The replay routine the boot loader
  starts was traced frame by frame into Paula register writes. The project
  holds its sample memory (`Audio/Beast chip memory.wav`, slot 1), each of
  its 16 instruments as its own WAV, and a song matrix of 62 rows (7:56: a
  65 s opening, then the 6:50 section the game loops) in four parallel
  lanes, Paula's channels 1-4, each played by its own Mla Paula instance
  (instrument *n* is channel *n*, on its own mixer strip; all four hold the
  same chip memory). Each lane has its own one-track pattern per matrix row
  (`Paula 1 L 0m00s` .. `Paula 4 L 7m48s`, 248 in all), whose SYSEX cells
  carry six 50 Hz frames per row at 125 BPM. Muting a lane silences its
  channel; playback can start on any matrix row. Raw Paula output: samples held
  without interpolation, A1200 model (no audible filter), LED filter off,
  75 % separation. A credits and rights notice shows on opening; the music
  is not covered by mlacker's license.
- [`examples/shadow_of_the_beast_level_1.mlaproj`](../../examples/shadow_of_the_beast_level_1.mlaproj):
  the music when a game starts, on the first level (disk 2's music bank
  `00007340`, song 2), built the same way: four lanes, each played by its
  own Mla Paula instance, 19 matrix rows (2:26: 35.40 s, then the 103.04 s
  section the game loops, from matrix row 5, row 39), 6 instruments, raw
  Paula output. The tune runs at speed 7 (seven frames a beat step); rows
  stay six frames, so every register write keeps its exact frame.
- [`examples/amiga_beast_intro.mlaproj`](../../examples/amiga_beast_intro.mlaproj):
  an original tune in that style on four instances in note mode, one per
  channel, with synthesized 8-bit samples.

Open either and press **Shift+M**, then **Space**, to play the song.
