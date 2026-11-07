# Mla 09

A VST3 drum machine instrument in the style of the Roland TR-909 Rhythm
Composer (subcategory `Instrument|Drum`). Like the 909, it is a hybrid:

- **Analog voices:** bass drum, snare drum, low, mid and high tom, rim shot and
  hand clap are synthesized, fitted to recordings of a TR-909 made across its
  knobs.
- **Sampled voices:** closed and open hi-hat, crash and ride play samples from
  ROM. They are built in, or your own 909 recordings
  ([Hi-hat and cymbal samples](#hi-hat-cymbal-and-clap-samples)).

The controls are the 909's panel knobs, plus **Drive**, which distorts the
output.

The synthesis and the ROMs are MLang: `src/mla_09_dsp.mla`. The C++ layer
(`src/plugin.cpp`) declares the buses, maps keys to instruments, schedules hits
sample-accurately, maps parameters, loads samples and saves state.

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

The bass drum, snare, toms, rim shot and hand clap were fitted to unprocessed
recordings of a TR-909, made with each knob at panel positions 1, 3, 6, 9 and 11. These
positions are knob values 0, 0.2, 0.5, 0.8 and 1 here. The fit covers pitch
glides, VCA holds and decays, harmonics, noise colour and lengths. Each value
below is a measurement.

- **Bass drum.**
  - **Pitch.** The VCO glides exponentially from about 270 Hz down to its
    resting pitch, 53 Hz at long Decay and 55 Hz at short Decay. **Tune**
    sets the glide's time constant: 6.9 ms at the minimum, 13 ms at the centre
    and 30.5 ms at the maximum. At the centre, the cycles from 2 ms run at
    about 208, 153 and 108 Hz.
  - **Wave.** A nearly pure sine (2nd harmonic -29 dB, 3rd -26.5 dB) that
    starts downwards.
  - **Hold.** The VCA holds the level for 50 ms, through the whole glide. A
    decaying offset meanwhile pushes one side of the wave into the output
    stage's clip, which flattens those half-cycles. That flattening is the
    909's punch.
  - **Decay.** **Decay** sets the time constant after the hold: 17 ms, 33 ms,
    62 ms and 105 ms from the minimum to the maximum, over a quiet tail. The
    drum is 40 dB down after about 0.3 s to 0.5 s.
  - **Click.** **Attack** adds up to about a third more click: band-passed
    noise on top of the step the hit itself makes.
- **Snare drum.**
  - **Tone.** Two VCOs at a 1 : 1.5 ratio, the upper one 12.7 dB down.
    **Tune** sets the lower one from 114 Hz to 222 Hz (166 Hz at the centre).
    Both start 2.4 times higher and drop within a few milliseconds. They decay
    with a 22 ms time constant to about -19 dB, then ring on quietly. The
    lower VCO carries its 2nd harmonic at -20 dB.
  - **Noise.** Band-passed noise centred at 6 to 10 kHz. **Snappy** sets its
    level: about -27 dB against the tone at 0.2, -16 dB at the centre and
    -10.5 dB at the top. **Tone** sets how long the noise holds before it
    fades: 50 ms at the minimum, 120 ms at the centre and 150 ms at the top.
    As on the 909, Tone does not change the noise's colour.
- **Toms.** A soft, triangle-like VCO (2nd and 3rd harmonics about -16 dB)
  that starts 30% above its pitch and glides down with a 40 ms time constant.
  A VCA holds and then decays, and about 20 ms of high noise sits on the
  attack.
  - **Tune.** Sets where each tom settles: low tom 60 to 120 Hz, mid 76 to
    157 Hz, high 90 to 172 Hz.
  - **Decay.** Sets the hold (30 to 110 ms) and the decay that follows (time
    constant 55 to 140 ms).
- **Rim shot.** Partials at 485 Hz and 217 Hz, driven hard into an asymmetric
  clip. The result is a nearly rectangular wave that starts on a narrow
  negative pulse (about 29% of each cycle), with wide positive pulses at about
  half its height. It holds for 10.5 ms, then the drive falls away and it
  decays.
- **Hand clap.** Fitted to recordings like the drums above. White noise is
  struck by four sawtooth bursts at 0, 10.5, 22.5 and 32.5 ms, each falling
  with a 2.3 ms time constant. The bursts are band-passed bright around
  1.1 kHz, with some air above 4 kHz. The fourth burst opens the "reverb"
  tail. The tail starts bright and darkens to a band around 1.05 kHz. It holds
  noisily, falls fast (time constant 17 ms), then rings on quietly (120 ms),
  about 30 dB down at 150 ms and 40 dB down after 0.3 s.

A harder hit on a sounding analog voice rises to its new level over 0.4 ms
instead of jumping mid-cycle, and the VCOs keep their phase. Fast rolls
therefore do not click, except for the bass drum's deliberate attack click.

## Sampled voices

The 909 plays its hi-hats and cymbals from ROM samples. Both hats play the
same ROM, and only their VCAs differ. Playback works like the 909's:

- **On a hit.** Every hit restarts the ROM from its first word. A retrigger
  fades the previous playback out over 2 ms under the new one.
- **Output path.** ROM words are interpolated (the DAC and its reconstruction
  filter), then go through each voice's VCA and a shared coupling high-pass at
  250 Hz.
- **Hi-hats.** **CH Decay** and **OH Decay** set the VCAs. As measured on the
  909, the closed hat lasts 0.11 to 0.2 s (40 dB down) and the open hat 0.28
  to 0.52 s. The hats are one voice, as on the 909, where both play from one ROM: either
  hat cuts the other within a few milliseconds, so they never sound together.
- **Cymbals.** **CR Tune** and **RD Tune** run their ROM clock: the crash from
  0.76x to 1.6x, the ride from 0.78x to 1.52x. A lower tune is darker and also
  longer, because the ROM plays out more slowly: the crash lasts 1.3 s down to
  0.6 s, the ride 1.2 s down to 0.6 s. The cymbals have no VCA decay, as on
  the 909: they ring for the length of their sample.

### Built-in ROMs

Roland's ROM data isn't ours to distribute, so the plug-in builds its own ROMs
when an instance is created, in the 909's format:

- **Recordings.** Each ROM holds a metallic recording: inharmonic partials
  whose decay gets faster with frequency, over filtered noise and a short stick
  transient.
- **Format.** The recordings are stored at a 32 kHz ROM clock and quantized to
  64 levels. The tails stay grainy, as on the 909.
- **Fit.** Their spectra and lengths are fitted to 909 recordings. The hats
  centre around 8 kHz, the crash around 4 to 6 kHz and the ride around 9 kHz.
- **Sizes.** Hi-hat 32 K words (1 s); crash and ride 64 K words each (2 s).

Every instance builds the same ROMs, so each hit plays exactly the same
waveform.

### Hi-hat, cymbal and clap samples

For the original 909 sound, give the plug-in your own 909 recordings. The
clap can play a recording of the 909's own analog clap. When an
instance starts, it looks in `$MLA09_SAMPLES`, or else
`~/.local/share/mlacker/mla09/`, for these files (`.wav`, `.aif` or `.aiff`):

| File        | Plays as |
|-------------|----------|
| `closedhat` (or `hihat`) | both hats, or just the closed hat when `openhat` is there |
| `openhat`   | the open hat |
| `crash`     | the crash |
| `ride`      | the ride |
| `clap`      | the hand clap, in place of the synthesized one |

Each file is mixed to mono and trimmed to the hit. It is resampled to the
32 kHz ROM clock and normalized, up to the ROM's capacity (1 s for each hat,
2 s for each cymbal, 1 s for the clap). Missing files keep their built-in ROM,
or for the clap its synthesized voice. A clap recording plays at the clap's
level, restarting on every hit.

- **Hat recordings** already carry the 909's decay. Record them at full
  Decay. A loaded hat's VCA leaves it as recorded at full Decay, and at lower
  settings shortens it to the 909's measured lengths.
- **Cymbal recordings** are best made at centred Tune, which is where the
  plug-in's Tune plays them unchanged.

Samples are not part of a saved project. Each machine reads its own folder.

## Parameters

| ID  | Name       | Range | Notes |
|-----|------------|-------|-------|
| 100 | Output     | -60 .. +6 dB (bottom = off) | MIDI CC 7. |
| 101 | Accent     | 0..1 | Accented level: 0 to +9 dB (default 0.5 = +4.5 dB). |
| 102 | Dynamics   | 909 Accent / Velocity | |
| 103 | BD Level   | -60 .. +6 dB | |
| 104 | BD Tune    | 0..1 | Pitch glide length. |
| 105 | BD Attack  | 0..1 | Click level. |
| 106 | BD Decay   | 0..1 | |
| 107 | SD Level   | -60 .. +6 dB | |
| 108 | SD Tune    | 0..1 | |
| 109 | SD Tone    | 0..1 | Noise length. |
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
| 127 | CR Tune    | 0..1 | ROM clock 0.76x .. 1.6x. |
| 128 | RD Level   | -60 .. +6 dB | |
| 129 | RD Tune    | 0..1 | ROM clock 0.78x .. 1.52x. |
| 130 | Drive      | 0..1 | Output distortion (default 0, clean). Not on the 909. |

## MIDI controllers

Every level and knob also answers to a MIDI CC, so an Instrument track's CC
columns (or a hardware controller) can turn the panel while a pattern plays:

| CC      | Parameters |
|---------|------------|
| 7       | Output |
| 20-30   | BD, SD, LT, MT, HT, RS, CP, CH, OH, CR, RD Level |
| 102     | Accent |
| 103-105 | BD Tune, Attack, Decay |
| 106-108 | SD Tune, Tone, Snappy |
| 109-114 | LT Tune, LT Decay, MT Tune, MT Decay, HT Tune, HT Decay |
| 115-116 | CH Decay, OH Decay |
| 117-118 | CR Tune, RD Tune |
| 119     | Drive |

A CC value 0-127 sets the parameter's whole range (the levels' -60 to +6 dB, the knobs' 0..1).

## Drive

**Drive** distorts the whole kit's output, like overdriving the 909's mixer.
It is a tanh saturator whose input gain rises with the knob, up to +28 dB.
Its output is levelled so a loud hit keeps about its peak level, while quieter
parts and tails come up. At 0 the output is untouched.

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
- the bass drum against the 909: its glide's cycles, resting pitch, hold,
  Decay range and the Attack click
- the snare's Tune (114 to 222 Hz), Snappy, and Tone setting the noise's
  length but not its colour
- each tom's settling pitch, glide and Decay
- the clap's four bursts and its darker tail
- the ROM voices:
  - the same waveform on every hit and in every instance, unlike the analog
    noise
  - the hats' brightness and their lengths at both ends of Decay
  - the hats choking each other
  - the cymbals' Tune changing both pitch and length, and the ride being
    brighter than the crash
- a recording in the samples folder replacing the crash ROM
- Drive
- accent and velocity dynamics
- click-free rolls
- the MIDI CC map
- state round trips

Given an output directory as a second argument, the test runner also writes
each voice to a WAV file there. The SDK's `validator` passes (47/47).
mlacker's host test (`tests/vst3_tests.cpp`) also loads the bundle and plays a
kick and an open hat from instrument notes.
