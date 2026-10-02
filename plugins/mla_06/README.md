# Mla 06

A VST3 drum machine instrument in the style of the Roland TR-606 Drumatix
(subcategory `Instrument|Drum`). It has the 606's seven analog voices: bass
drum, snare drum, low and high tom, cymbal, and open and closed hi-hat. They
are synthesized, not sampled.

The synthesis is MLang: `src/mla_06_dsp.mla`. The C++ layer (`src/plugin.cpp`)
declares the buses, maps keys to instruments, schedules hits
sample-accurately, maps parameters and saves state.

The voices follow the 606's circuits. Their constants (pitches, sweeps, decay
times, filter bands) were measured from recordings of a TR-606, and
`tests/mla_06_tests.cpp` checks the rendered audio against those measurements.

## Keys

The kit uses General MIDI drum keys. Note-offs are ignored, as on the 606:
every hit rings out.

| Instrument      | Keys |
|-----------------|------|
| Bass drum (BD)  | 35, **36** |
| Snare drum (SD) | 37, **38**, 39, 40 |
| Low tom (LT)    | 41, 43, **45** |
| High tom (HT)   | 47, **48**, 50 |
| Cymbal (CY)     | **49**, 51, 52, 53, 55, 57, 59 |
| Open hat (OH)   | **46** |
| Closed hat (CH) | **42**, 44 |

Other keys are silent. In mlacker's note notation, key 36 is C-2 (C-3 is 48).

## Accent

With **Dynamics** set to *606 Accent* (the default), a hit has two levels, as
on the 606. Velocities up to 100 play at the normal level, and velocities above
100 are accented. mlacker enters notes at velocity 100, so enter 127 for an
accent. **Accent** sets how much louder an accented hit is: 0 to +9 dB, and the
default +4.5 dB. An accented hit also rings up to 15 % longer, like the 606's
accent bus driving its triggers harder.

With *Velocity*, velocity scales every hit (by velocity squared), and Accent is
the extra level at full velocity.

## Voices

- **Bass drum.** The 606 rings a bridged-T resonator with a trigger pulse. The
  emulation uses a decaying sine whose amplitude and pitch can change while it
  rings: a near-pure 58 Hz tone with a short trigger click. Like the 606's
  envelopes, the decay speeds up at its end (40 dB down after about 0.19 s).
- **Snare drum.** A tone that falls from 315 Hz to 205 Hz in about 10 ms, plus
  white noise high-passed at 1.6 kHz and rolled off above 6 kHz. **SD Snappy**
  moves the balance from all tone to all noise.
- **Toms.** The low tom falls from 180 Hz to 132 Hz with a second mode a fifth
  up, a trigger click and slight saturation. The high tom is a fifth above
  the low tom.
- **Metal.** Six free-running square-wave oscillators at 292, 373, 437, 509, 636
  and 755 Hz (band-limited) feed the cymbal and both hats. These frequencies
  are fitted to the partials of the 606's hats. The closed hat high-passes the
  bank at 5 kHz (4th order) and the open hat at 5 kHz (2nd order). Each then
  mixes a narrow band-pass at 7 kHz with part of the high-passed signal. The
  cymbal uses a lower, wider band.
- **Hi-hats.** The closed hat rises in 6 ms and decays with a time constant of
  50 ms. It also chokes the open hat. The open hat drops fast, then rings on
  for about 1.5 s.

Retriggering a ringing drum continues its waveform from where it is, so fast
rolls do not click. A softer hit on a louder ring keeps the ring's level.

## Parameters

| ID  | Name        | Range | Notes |
|-----|-------------|-------|-------|
| 100 | Output      | -60 .. +6 dB (bottom = off) | MIDI CC 7. |
| 101 | Accent      | 0..1 | Accented level: 0 to +9 dB (default 0.5 = +4.5 dB). |
| 102 | Dynamics    | 606 Accent / Velocity | |
| 103 | BD Level    | -60 .. +6 dB | |
| 104 | BD Tune     | ±12 st | |
| 105 | BD Decay    | 0.25x .. 4x | 1x (centre) is the 606's decay. |
| 106 | SD Level    | -60 .. +6 dB | |
| 107 | SD Tune     | ±12 st | |
| 108 | SD Decay    | 0.25x .. 4x | |
| 109 | SD Snappy   | 0..1 | Tone vs noise. |
| 110 | LT Level    | -60 .. +6 dB | |
| 111 | LT Tune     | ±12 st | |
| 112 | LT Decay    | 0.25x .. 4x | |
| 113 | HT Level    | -60 .. +6 dB | |
| 114 | HT Tune     | ±12 st | |
| 115 | HT Decay    | 0.25x .. 4x | |
| 116 | CY Level    | -60 .. +6 dB | |
| 117 | CY Decay    | 0.25x .. 4x | |
| 118 | OH Level    | -60 .. +6 dB | |
| 119 | OH Decay    | 0.25x .. 4x | |
| 120 | CH Level    | -60 .. +6 dB | |
| 121 | CH Decay    | 0.25x .. 4x | |
| 122 | Metal Tune  | ±12 st | Retunes the six oscillators and the hat and cymbal filters together. |

Parameter IDs are stable, and new parameters are appended. The output is mono,
on both channels.

## In mlacker

1. **Track → Create Instrument track**, then **Instrument → Add instrument**
   and choose `Mla06.vst3`.
2. Enter notes on the keys above: C-2 is the bass drum, D-2 the snare, F#2 the
   closed hat, A#2 the open hat, and so on. Use velocity 127 for accents.

## Build and test

From the repository root, `./build.sh --plugins` builds every plug-in,
including this one (`--install` copies it to the plug-in directory). On its
own:

```sh
../../subprojects/mlang/build/mlang pkg --config mlang.toml run build   # -> build/cmake/VST3/Release/Mla06.vst3
../../subprojects/mlang/build/mlang pkg --config mlang.toml run test
```

`tests/mla_06_tests.cpp` loads the built bundle through the SDK hosting
classes and checks rendered audio:

- bus layout
- silence, the key map, and ignored note-offs
- sample-accurate hits
- each voice's level, length and pitch against the 606 measurements (BD 58 Hz;
  the low tom settling at 132 Hz; the snare tone at 205 Hz; hats crossing zero
  at about 7 kHz)
- the Snappy balance, the closed hat choking the open hat, and Metal Tune
- accent and velocity dynamics
- decay scaling
- click-free fast rolls
- state round trips

Given an output directory as a second argument, the test runner also writes
each voice to a WAV file there. The SDK's `validator` passes (47/47).
mlacker's host test (`tests/vst3_tests.cpp`) also loads the bundle and plays a
kick from instrument notes.
