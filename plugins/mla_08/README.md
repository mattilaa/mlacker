# Mla 08

A VST3 drum machine instrument in the style of the Roland TR-808 Rhythm
Composer (subcategory `Instrument|Drum`). It has the 808's sixteen analog
voices: bass drum, snare drum, low, mid and high tom and conga, rim shot,
claves, hand clap, maracas, cowbell, cymbal, and open and closed hi-hat. They
are synthesized, not sampled. The controls are the 808's panel knobs, plus
Tone and Decay for the hats and Attack for the hats and cymbal.

The synthesis is MLang: `src/mla_08_dsp.mla`. The C++ layer (`src/plugin.cpp`)
declares the buses, maps keys to instruments, schedules hits
sample-accurately, maps parameters and saves state.

The voices follow the 808's circuits. Their constants (pitches, pitch glides,
decay times, filter bands, oscillator frequencies, and what each knob does)
were measured from recordings of a TR-808, several per instrument:

- the bass drum at six Tone and six Decay settings
- the snare at four Tone and four Snappy settings
- each tom at three tunings, and each conga
- the open hat at three Decay settings
- the cymbal at two Decay settings

`tests/mla_08_tests.cpp` checks the rendered audio against those measurements.

## Keys

The kit uses General MIDI drum keys. Note-offs are ignored, as on the 808:
every hit rings out.

| Instrument       | Keys |
|------------------|------|
| Bass drum (BD)   | 35, **36** |
| Rim shot (RS)    | **37** |
| Snare drum (SD)  | **38**, 40 |
| Hand clap (CP)   | **39** |
| Low tom (LT)     | 41, 43, **45** |
| Mid tom (MT)     | **47**, 48 |
| High tom (HT)    | **50** |
| Closed hat (CH)  | **42**, 44 |
| Open hat (OH)    | **46** |
| Cymbal (CY)      | **49**, 51, 52, 55, 57, 59 |
| Cowbell (CB)     | **56** |
| High conga (HC)  | **62** |
| Mid conga (MC)   | **63** |
| Low conga (LC)   | **64** |
| Maracas (MA)     | **70** |
| Claves (CL)      | **75** |

Other keys are silent. In mlacker's note notation, key 36 is C-2 (C-3 is 48).

On the 808 a switch selects between each tom and its conga, the rim shot and
the claves, and the hand clap and the maracas. Here every voice has its own
key, and like on the 808 each pair shares its knobs (the toms' Tuning also
tunes their congas).

## Accent

With **Dynamics** set to *808 Accent* (the default), a hit has two levels, as
on the 808. Velocities up to 100 play at the normal level, and velocities above
100 are accented. mlacker enters notes at velocity 100, so enter 127 for an
accent. **Accent** sets how much louder an accented hit is: 0 to +9 dB, and the
default +4.5 dB. An accented bass drum also rings a little higher, because its
pitch follows its level.

With *Velocity*, velocity scales every hit (by velocity squared), and Accent is
the extra level at full velocity.

## Voices

- **Bass drum.** The 808 rings a bridged-T resonator with a trigger pulse.
  The emulation is a decaying sine at 49 Hz whose pitch follows its
  amplitude: about 53 Hz just after a hit, gliding down to 49 Hz as it fades,
  so longer decays glide longer. The trigger kicks the pitch up to about
  300 Hz for the first 3 ms. The 0.9 ms trigger pulse leaks into the output
  through a low-pass that **Tone** opens from 80 Hz to 1.3 kHz, which sets the
  attack's click. The output then goes through a hard-knee clip, which shapes
  the attack as on the recordings. **Decay** sets the ring's time constant
  from 14 ms to 0.36 s: 40 dB down after 0.07 s at the minimum and 1.6 s at the
  maximum.
- **Snare drum.** Two rings, at 175.5 Hz (time constant 27 ms) and 336 Hz
  (8 ms). **Tone** crossfades from the low one to the high one. **Snappy**
  adds white noise band-passed around 2.5 kHz (time constant 31 ms).
- **Toms and congas.** Rings with a fast pitch kick at the trigger. The low
  tom is at 81 to 103 Hz, the mid at 118 to 146 Hz and the high at 168 to
  210 Hz across **Tuning**. Their noise runs from below the pitch to about
  400 Hz. The congas (177, 248 and 371 Hz at the bottom of Tuning) are pure
  rings.
- **Rim shot and claves.** Rim shot: 456 Hz and 1.83 kHz rings, high-passed
  and driven nearly square, so the hit holds and then stops after about 20 ms.
  Claves: a 2.53 kHz ring.
- **Hand clap and maracas.** Clap: four sharp bursts of noise band-passed
  around 1.3 kHz, at 0, 9.5, 21.5 and 31.5 ms, each decaying in 1.8 ms. The
  last one is the main hit. It adds a short broadband crack and a bright layer
  that fades in about 25 ms, over a tail around 1 kHz that stays noisy up top.
  So the clap starts crisp and darkens as it fades, as on the recording. Maracas: noise high-passed at 6.5 kHz that swells for
  20 ms and then stops.
- **Metal.** Six free-running square-wave oscillators feed the cymbal and both
  hats: 325.2, 417.9, 546.8, 564.8, 793.0 and 849.6 Hz, band-limited. These
  frequencies are fitted to the partials of the recordings. The cowbell takes
  the 564.8 and 849.6 Hz pair through a narrow band-pass at 900 Hz, with a fast
  and a slow decay.
- **Cymbal.** Three bands (2.2 to 5.5 kHz, 5.5 to 12 kHz, 9 to 14 kHz), each
  with its own envelope fitted to the recordings. The high bands start bright
  and drop within about 0.1 s. The low band swells in over about 0.1 s and
  rings on (time constant 1.1 s at the recorded setting). The high bands use
  steep (6th and 8th order) high-passes, so the squares' low partials stay
  out. **Decay** scales the bands' ring. **Tone** balances the low band
  against the two high ones.
- **Hi-hats.** A 4th-order high-pass and a band at 7.3 kHz. The closed hat
  decays in about 70 ms and chokes the open hat. The open hat's **Decay**
  sets how long it rings before it closes: about 0.1 to 0.7 s.
- **Beyond the 808's panel.** **CH Decay** scales the closed hat's decay
  (0.25x to 4x; 20 ms to 0.3 s, 40 dB down). **CH Tone** and **OH Tone** move
  each hat's filters by up to 3/4 octave either way. **CH Attack**, **OH
  Attack** and **CY Attack** set the rise from 0.05 ms to 10 ms; the centre,
  0.7 ms, is the recordings' rise. The metal filters run all the time, as on
  the hardware, so a hit opens an envelope instead of striking a resting
  filter with the squares' edges.

Retriggering a ringing drum continues its waveform from where it is, so fast
rolls do not click. A softer hit on a louder ring keeps the ring's level.

## Parameters

| ID  | Name         | Range | Notes |
|-----|--------------|-------|-------|
| 100 | Output       | -60 .. +6 dB (bottom = off) | MIDI CC 7. |
| 101 | Accent       | 0..1 | Accented level: 0 to +9 dB (default 0.5 = +4.5 dB). |
| 102 | Dynamics     | 808 Accent / Velocity | |
| 103 | BD Level     | -60 .. +6 dB | |
| 104 | BD Tone      | 0..1 | Attack click. |
| 105 | BD Decay     | 0..1 | |
| 106 | SD Level     | -60 .. +6 dB | |
| 107 | SD Tone      | 0..1 | Low ring vs high ring. |
| 108 | SD Snappy    | 0..1 | Noise. |
| 109 | LT/LC Level  | -60 .. +6 dB | Low tom and low conga. |
| 110 | LT/LC Tuning | 0..1 | |
| 111 | MT/MC Level  | -60 .. +6 dB | Mid tom and mid conga. |
| 112 | MT/MC Tuning | 0..1 | |
| 113 | HT/HC Level  | -60 .. +6 dB | High tom and high conga. |
| 114 | HT/HC Tuning | 0..1 | |
| 115 | RS/CL Level  | -60 .. +6 dB | Rim shot and claves. |
| 116 | CP/MA Level  | -60 .. +6 dB | Hand clap and maracas. |
| 117 | CB Level     | -60 .. +6 dB | |
| 118 | CY Level     | -60 .. +6 dB | |
| 119 | CY Tone      | 0..1 | Low band vs high bands. |
| 120 | CY Decay     | 0..1 | |
| 121 | OH Level     | -60 .. +6 dB | |
| 122 | OH Decay     | 0..1 | |
| 123 | CH Level     | -60 .. +6 dB | |
| 124 | CH Decay     | 0..1 | 0.25x .. 4x. Not on the 808. |
| 125 | CH Tone      | 0..1 | Filters down/up 3/4 octave. Not on the 808. |
| 126 | OH Tone      | 0..1 | Filters down/up 3/4 octave. Not on the 808. |
| 127 | CH Attack    | 0..1 | 0.05 .. 10 ms (0.7 ms centred). Not on the 808. |
| 128 | OH Attack    | 0..1 | As CH Attack. Not on the 808. |
| 129 | CY Attack    | 0..1 | As CH Attack. Not on the 808. |

The knobs are 0..1, centred by default. Parameter IDs are stable, and new
parameters are appended. The output is mono, on both channels.

## In mlacker

1. **Track → Create Instrument track**, then **Instrument → Add instrument**
   and choose `Mla08.vst3`.
2. Enter notes on the keys above: C-2 is the bass drum, D-2 the snare, D#2 the
   clap, F#2 the closed hat, A#2 the open hat, and so on. Use velocity 127 for
   accents.

## Build and test

From the repository root, `./build.sh --plugins` builds every plug-in,
including this one (`--install` copies it to the plug-in directory). On its
own:

```sh
../../subprojects/mlang/build/mlang pkg --config mlang.toml run build   # -> build/cmake/VST3/Release/Mla08.vst3
../../subprojects/mlang/build/mlang pkg --config mlang.toml run test
```

`tests/mla_08_tests.cpp` loads the built bundle through the SDK hosting
classes and checks rendered audio:

- bus layout
- silence, the key map, and ignored note-offs
- sample-accurate hits
- each voice's level and length against the 808 recordings
- the bass drum's pitch glide (to 49 Hz), Decay range and Tone
- the snare's Tone (175 vs 336 Hz) and Snappy
- tom and conga tuning, with the shared knobs; the claves' pitch
- the cowbell's oscillator pair; the hats' brightness, the closed hat
  choking the open hat, and the open hat and cymbal decays
- CH Decay and the hats' Tone; the hats' and cymbal's attack (no step at the
  onset) and the Attack knobs
- accent and velocity dynamics
- click-free fast rolls
- state round trips

Given an output directory as a second argument, the test runner also writes
each voice to a WAV file there. The SDK's `validator` passes (47/47).
mlacker's host test (`tests/vst3_tests.cpp`) also loads the bundle and plays a
kick from instrument notes.
