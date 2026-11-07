# Mla 09

A VST3 drum machine instrument in the style of the Roland TR-909 Rhythm
Composer (subcategory `Instrument|Drum`). Like the 909, it is a hybrid:

- **Analog voices:** bass drum, snare drum, low, mid and high tom, rim shot and
  hand clap are synthesized.
- **Sampled voices:** closed and open hi-hat, crash and ride play 6-bit
  samples from ROM.

The controls are the 909's panel knobs.

The synthesis and the ROMs are MLang: `src/mla_09_dsp.mla`. The C++ layer
(`src/plugin.cpp`) declares the buses, maps keys to instruments, schedules hits
sample-accurately, maps parameters and saves state.

## Keys

The kit uses the 909's MIDI note numbers. The cymbals also answer to their
General MIDI aliases. Note-offs are ignored, as on the 909: every hit rings
out.

| Instrument       | Keys |
|------------------|------|
| Bass drum (BD)   | 35, **36** |
| Rim shot (RS)    | **37** |
| Snare drum (SD)  | **38**, 40 |
| Hand clap (CP)   | **39** |
| Low tom (LT)     | 41, **43** |
| Mid tom (MT)     | 45, **47** |
| High tom (HT)    | 48, **50** |
| Closed hat (CH)  | **42**, 44 |
| Open hat (OH)    | **46** |
| Crash (CR)       | **49**, 52, 55, 57 |
| Ride (RD)        | **51**, 53, 59 |

Other keys are silent. In mlacker's note notation, key 36 is C-2 (C-3 is 48).

## Accent

With **Dynamics** set to *909 Accent* (the default), a hit has two levels, as
on the 909. Velocities up to 100 play at the normal level. Velocities above
100 are accented. mlacker enters notes at velocity 100, so enter 127 for an
accent. **Accent** (the 909's Total Accent) sets how much louder an accented
hit is: 0 to +9 dB, default +4.5 dB.

With *Velocity*, velocity scales every hit (by velocity squared), and Accent is
the extra level at full velocity.

## Analog voices

- **Bass drum.** A VCO shaped into a rounded sine with flattened tops. A pitch
  envelope sweeps it down to about 50 Hz. **Tune** sets how far and how long
  the sweep runs: from about 2x the resting pitch, short, up to 5x, longer.
  Tune also raises the resting pitch a little (47 to 56 Hz). **Decay** sets the
  VCA's time constant after a short hold, so the drum is 40 dB down after
  0.2 s at the minimum and 1.3 s at the maximum. **Attack** mixes in the click:
  a 0.6 ms pulse through a 4 kHz low-pass, plus a 2 ms burst of band-passed
  noise.
- **Snare drum.** Two VCOs, about 180 and 330 Hz, that drop in pitch over the
  first few milliseconds, plus white noise high-passed at 1.1 kHz. **Tune**
  moves both VCOs over about half an octave. **Tone** opens the noise's
  low-pass from 4 to 14 kHz and lengthens the noise. **Snappy** sets the
  noise level.
- **Toms.** A VCO that starts about 60% above its pitch and bends down, plus a
  little low-passed noise on the attack. **Tune** moves each tom over about a
  fifth: low tom 72 to 112 Hz, mid 105 to 162 Hz, high 150 to 230 Hz.
  **Decay** sets the VCA's time constant from about 0.05 s to 0.4 s.
- **Rim shot.** Three short rings at 495 Hz, 1.72 kHz and 3.35 kHz,
  high-passed and driven.
- **Hand clap.** Noise band-passed around 1.15 kHz, struck by three sawtooth
  bursts 10 ms apart. A fourth burst at 30 ms opens a tail around 1 kHz, about
  0.35 s long.

A harder hit on a sounding analog voice rises to its new level over 0.4 ms
instead of jumping mid-cycle, and the VCOs keep their phase. Fast rolls
therefore do not click, except for the bass drum's deliberate attack click.

## Sampled voices

The 909 plays its hi-hats and cymbals from 6-bit ROM samples. Roland's ROM data
isn't ours to distribute, so the plug-in builds its own four ROMs when an
instance is created, in the same format:

- **Recordings.** The ROMs hold metallic recordings: inharmonic partials whose
  decay gets faster with frequency, over high-passed noise and a short stick
  transient. Some of the crash's partials swell in after the hit. The ride's
  partials are lower, longer and less noisy, so the ride pings where the crash
  washes.
- **Format.** Each ROM is stored at a 32 kHz ROM clock, normalized to full
  scale and quantized to 64 levels (6 bits). The quantization stays in: as a
  sample fades, its tail gets grainy, as on the 909. Each cymbal recording
  fades out over its last quarter, so playback never stops on a step.
- **Lengths.** Closed hat 8 K words (0.26 s), open hat 32 K words (1 s),
  crash and ride 64 K words each (2 s at the nominal clock).

Every instance builds the same ROMs, so each hit of a sampled voice plays
exactly the same waveform. Playback works like the 909's:

- **On a hit.** Every hit restarts the ROM from its first word. A retrigger
  fades the previous playback out over 2 ms under the new one.
- **Output path.** ROM words are interpolated (the DAC and its reconstruction
  filter), then go through each voice's VCA and a shared coupling high-pass at
  250 Hz.
- **Hi-hats.** **CH Decay** sets the closed hat's VCA time constant from 6 to
  40 ms, after a 2 ms hold. **OH Decay** sets the open hat's from 40 to
  450 ms, after a 5 ms hold. The closed hat chokes the open hat within a few
  milliseconds.
- **Cymbals.** **CR Tune** and **RD Tune** run their ROM clock from 0.71x to
  1.41x. A lower tune is darker and also longer, because the ROM plays out
  more slowly. The cymbals have no VCA decay, as on the 909: they ring for
  the length of their sample.

## Parameters

| ID  | Name       | Range | Notes |
|-----|------------|-------|-------|
| 100 | Output     | -60 .. +6 dB (bottom = off) | MIDI CC 7. |
| 101 | Accent     | 0..1 | Accented level: 0 to +9 dB (default 0.5 = +4.5 dB). |
| 102 | Dynamics   | 909 Accent / Velocity | |
| 103 | BD Level   | -60 .. +6 dB | |
| 104 | BD Tune    | 0..1 | Pitch sweep depth and length. |
| 105 | BD Attack  | 0..1 | Click level. |
| 106 | BD Decay   | 0..1 | |
| 107 | SD Level   | -60 .. +6 dB | |
| 108 | SD Tune    | 0..1 | |
| 109 | SD Tone    | 0..1 | Noise brightness and length. |
| 110 | SD Snappy  | 0..1 | Noise level. |
| 111 | LT Level   | -60 .. +6 dB | |
| 112 | LT Tune    | 0..1 | |
| 113 | LT Decay   | 0..1 | |
| 114 | MT Level   | -60 .. +6 dB | |
| 115 | MT Tune    | 0..1 | |
| 116 | MT Decay   | 0..1 | |
| 117 | HT Level   | -60 .. +6 dB | |
| 118 | HT Tune    | 0..1 | |
| 119 | HT Decay   | 0..1 | |
| 120 | RS Level   | -60 .. +6 dB | |
| 121 | CP Level   | -60 .. +6 dB | |
| 122 | CH Level   | -60 .. +6 dB | |
| 123 | CH Decay   | 0..1 | |
| 124 | OH Level   | -60 .. +6 dB | |
| 125 | OH Decay   | 0..1 | |
| 126 | CR Level   | -60 .. +6 dB | |
| 127 | CR Tune    | 0..1 | ROM clock 0.71x .. 1.41x. |
| 128 | RD Level   | -60 .. +6 dB | |
| 129 | RD Tune    | 0..1 | ROM clock 0.71x .. 1.41x. |

The knobs are 0..1 and centred by default. Parameter IDs are stable, and new
parameters are appended. A parameter's DSP index (`mla09_set`) is its ID minus
100. The output is mono, on both channels.

## In mlacker

1. **Track → Create Instrument track**, then **Instrument → Add instrument**
   and choose `Mla09.vst3`.
2. Enter notes on the keys above, for example:
   - C-2: bass drum
   - D-2: snare
   - D#2: clap
   - F#2: closed hat
   - A#2: open hat
   - C#3: crash
   - D#3: ride

   Use velocity 127 for accents.

## Build and test

From the repository root, `./build.sh --plugins` builds every plug-in,
including this one (`--install` copies it to the plug-in directory). On its
own:

```sh
../../subprojects/mlang/build/mlang pkg --config mlang.toml run build   # -> build/cmake/VST3/Release/Mla09.vst3
../../subprojects/mlang/build/mlang pkg --config mlang.toml run test
```

`tests/mla_09_tests.cpp` loads the built bundle through the SDK hosting
classes and checks rendered audio:

- bus layout
- silence, the key map and ignored note-offs
- sample-accurate hits
- each voice's level and length
- the bass drum's Tune sweep, resting pitch, Decay range and Attack click
- the snare's Tune, Tone and Snappy
- tom tuning, pitch bend and Decay
- the ROM voices:
  - the same waveform on every hit and in every instance, unlike the analog
    noise
  - the hats' brightness and Decay knobs
  - the closed hat choking the open hat
  - the cymbals' Tune changing both pitch and length
- accent and velocity dynamics
- click-free rolls
- state round trips

Given an output directory as a second argument, the test runner also writes
each voice to a WAV file there. The SDK's `validator` passes (47/47).
mlacker's host test (`tests/vst3_tests.cpp`) also loads the bundle and plays a
kick and an open hat from instrument notes.
