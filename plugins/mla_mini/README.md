# Mla Mini

A Minimoog-inspired monophonic VST3 instrument for mlacker. The startup patch
is a dark, slightly detuned bass shaped using the supplied `moog.wav` as a
tonal reference. It synthesizes new notes; the recording is not embedded or
played back. The recording alone cannot reveal the original oscillator mix,
knob settings, processing or circuit response, so this is an approximation,
not an exact Model D circuit recreation or a verified identical match.

## Sound engine

- Three free-running oscillators, independent wave, octave and mixer level.
  Waves: triangle, triangle/saw blend, saw, square, 30% pulse and 15% pulse.
  Oscillators 2 and 3 have ±25-cent fine tuning. Ranges span ±2 octaves.
- PolyBLEP saws and pulses, and four internal samples per output sample.
  A two-pole low-pass plus averaging precedes decimation.
- A 24 dB/octave TPT ladder. A Newton solve places input saturation inside
  its instantaneous resonance loop. Resonance reduces bass gain, with a
  little compensation. Drive saturates the mixer/filter input; Feedback
  returns the filter output to its input for thicker, dirtier patches.
- Independent filter and amplitude ADSR envelopes. Attack is linear;
  decay/release are exponential, with displayed times measured to a 60 dB
  fall. Retrigger starts from the current envelope level.
- Filter envelope amount, keyboard tracking, noise, velocity sensitivity,
  pitch bend, sustain and last/low/high note priority.
- Mono legato with a held-note stack and fallback to the remaining key.
  Glide operates between overlapping notes; detached notes start at pitch.
  Turn Legato off to retrigger both envelopes on each selected new note.
- Oscillator 3 can become a low-frequency modulation source. Turn off its
  keyboard tracking for a fixed rate. The wheel controls its pitch/filter
  modulation depths. In Low mode it is removed from the audio mixer.
- Mono sound on both stereo output channels; no chorus or reverb.

The DSP is in `src/mla_mini_dsp.mla`; C++ supplies VST3 parameters, MIDI,
sample-offset scheduling and versioned preset state. No audio-thread allocation.
Parameters appear in mlacker's instrument parameter panel; there is no custom
graphical editor. Only 32-bit float processing is supported.

## Use in mlacker

Build with `./build.sh --plugins`, then load
`build/plugins/VST3/Release/MlaMini.vst3` on an instrument track.
`./build.sh --plugins --install` installs it alongside the other Mla plugins.
Play around C-2 .. C-3; F-2 is MIDI note 41, approximately 87.3 Hz.

Start with the default patch. Raise **Cutoff** or **Filter Env Depth** for
more bite, **Resonance** for a stronger filter peak, and **Drive** or
**Feedback** for more growl. **OSC 2 Fine** controls the beating; raise
**OSC 3 Level** for more weight an octave below. **Filter Decay** controls
the initial brightness fall; **Amp Release** controls how quickly notes stop.

MIDI CCs: 1 wheel, 5 glide, 7 output, 64 sustain, 71 resonance, 74 cutoff,
73 amp attack, 72 amp release, 120/123 immediate all-notes-off. MIDI pitch
bend uses **Bend Range**. CC mapping is global to this mono instrument.

## Parameters

Stable IDs are 100 plus the index in `src/parameters.h`:

| IDs | Controls | Physical range |
| --- | --- | --- |
| 100 | Output | −60 .. +6 dB; minimum mutes |
| 101–103 | OSC 1 Wave / Range / Level | 6 waves / −2 .. +2 oct / 0 .. 1 |
| 104–107 | OSC 2 Wave / Range / Fine / Level | 6 waves / −2 .. +2 oct / ±25 ct / 0 .. 1 |
| 108–111 | OSC 3 Wave / Range / Fine / Level | as OSC 2 |
| 112 | Noise | 0 .. 1 |
| 113–116 | Cutoff / Resonance / Filter Env Depth / Key Follow | 20 Hz .. 20 kHz / 0 .. 1 / 0 .. 8 oct / 0 .. 1 |
| 117–120 | Filter Attack / Decay / Sustain / Release | 1 ms .. 10 s / 10 ms .. 10 s / 0 .. 1 / 5 ms .. 10 s |
| 121–124 | Amp Attack / Decay / Sustain / Release | as filter envelope |
| 125–127 | Drive / Feedback / Glide | 1 .. 8× / 0 .. 1 / 0 .. 1 s |
| 128–129 | Legato / Key Priority | off/on / Last, Low, High |
| 130–134 | Pitch Bend / Bend Range / Mod Wheel / Mod > Pitch / Mod > Filter | ±1 / 0 .. 24 st / 0 .. 1 / 0 .. 12 st / 0 .. 6 oct |
| 135–138 | OSC 3 Low / OSC 3 Keyboard / Velocity > Amp / Velocity > Filter | off/on / off/on / 0 .. 1 / 0 .. 1 |
| 139–140 | Sustain Pedal / All Notes Off | MIDI performance controls |

Cutoff, envelope times and Drive use logarithmic normalized mappings. Other
continuous controls are linear. Bend, wheel, pedal and panic return to neutral
when restoring binary VST state; the host can restore performance automation.

## Example and preview

[The example](../../examples/mini_bass) includes a ready-to-open project,
its generator, and instructions. It needs only Mla Mini.

```sh
examples/mini_bass/build.sh
build/cmake/bin/mlacker examples/mini_bass.mlaproj
```

The generator also writes `build/mini_bass/preview.wav` through mlacker's
actual sequencer and VST3 host.

## Verification

```sh
cmake --build build/plugins --target mla_mini_tests -j4
ctest --test-dir build/plugins -R '^mini_processor$' --output-on-failure
```

The tests load the real bundle and check oscillator pitch/range/fine tuning,
all six wave shapes, filter cutoff/envelope spectra, release, legato versus
retrigger, glide, key priority/fallback, channels, repeated keys, velocity-zero
note-offs, sustain, bend, automation offsets, panic, state round trips,
malformed-state rejection, sample rates, and finite output under maximum
drive/resonance/feedback. The app's `vst3_host` and `mini_bass_tui` tests also
exercise the plugin and example in mlacker.
Steinberg's SDK validator also passes all 47 tests for the built macOS bundle.
