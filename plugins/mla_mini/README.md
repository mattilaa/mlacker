# Mla Mini

A Minimoog-inspired monophonic VST3 instrument for mlacker. The startup patch
is a bright, slightly detuned bass shaped using the supplied `moog.wav` as a
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
- **OSC Drive** soft-clips each oscillator before its mixer level, with
  peak compensation. The default 2.1 adds a little grit; 1 bypasses shaping.
  This control is separate from the existing mixer/filter **Drive**.
- **Analog Output** blends in an asymmetric line-amplifier approximation
  after the VCA. It adds even harmonics and gentle compression before master
  volume, with small-signal gain compensation and DC removal. The nonlinear
  stage runs at 4x sample rate before decimation; default amount is 30%.
- **OSC Drift** adds independent slow tuning wander to all three oscillators.
  The default is bounded to ±1.2 cents; the control ranges from 0 to ±8 cents.
  Random targets arrive at irregular intervals and slew over 200 ms. Each
  instance has its own seed; note gates and transport do not restart the
  oscillator phases or wander. This control introduces no per-note pitch jumps.
- **Background Hiss** is a separate, continuous output-noise floor, audible
  during rests as well as notes. It has **eleven settings, 0–10**, with
  **0 = Off**, **10 = maximum**, and a default of **6**.
  **Hiss Level** fine-trims the full-amount level from −96 to −48 dBFS;
  its default is −54 dBFS. Together the defaults give about −58.4 dBFS RMS
  before master Output and host faders. Amount and level changes
  fade smoothly; master Output also controls the hiss. This is independent
  of the existing **Noise** control, which feeds the filter and VCA.
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
- **Chorus** adds stereo movement after the monophonic voice. Two filtered delay taps sweep in opposite directions at about 0.51 Hz. The control fades from dry at 0% to equal dry and wet at 100%.

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
The startup bass uses a saw at the played pitch with a detuned triangle/saw
blend and triangle one octave below. A 480 Hz cutoff and 4.5-octave envelope
give the attack its edge. The filter closes with a 180 ms release while the
amplitude falls away over 550 ms.
Use **Analog Output** for extra roughness after the amp envelope, and **OSC
Drift** for a little changing oscillator beating. Setting both to zero
recovers the previous output path and stable oscillator tuning. These are
musical approximations of output-stage coloration and VCO drift, rather
than a measured transistor-by-transistor Model D model.

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
| 141 | OSC Drive | 1 .. 4×; 1 = clean bypass |
| 142 | Analog Output | 0 .. 1; default 0.3 |
| 143 | OSC Drift | 0 .. 8 ct maximum deviation per oscillator; default 1.2 |
| 144 | Background Hiss | 0 .. 10; 0 = Off, default 6 |
| 145 | Hiss Level | −96 .. −48 dBFS RMS at amount 10, before master Output; default −54 |
| 146 | Chorus | 0–100% amount, default 0% |

Cutoff, envelope times and Drive use logarithmic normalized mappings. Other
continuous controls are linear. Bend, wheel, pedal and panic return to neutral
when restoring binary VST state; the host can restore performance automation.
Older binary states that lack the new controls restore with Analog Output
and OSC Drift at zero. New states save the control amounts, not the ongoing
random drift trajectory.
Older binary states also load with Background Hiss off. Noise shaping rolls
off low rumble below 80 Hz and gently softens the hiss above 8 kHz. Its
nominal RMS level is normalized across sample rates; this is an analog-style
approximation rather than a measured Minimoog noise specification.
The former On/Off control's saved normalized values remain compatible:
old Off restores as 0 and old On as 10, retaining the saved Hiss Level.

## Example and preview

[The example](../../examples/mini_bass) includes a ready-to-open project,
its generator, and instructions. It needs only Mla Mini.

```sh
examples/mini_bass/build.sh
build/cmake/bin/mlacker examples/mini_bass.mlaproj
```

The generator also writes `build/mini_bass/preview.wav` through mlacker's
actual sequencer and VST3 host. It follows the estimated ten-bar bass sequence
from the reference at 121 BPM. `build/mini_bass/baseline.wav` plays the same
score with Analog Output and OSC Drift disabled for comparison, preserving
the same bright filter, envelopes and oscillator drive.
`build/mini_bass/sixteenths.wav` auditions eight bars of sixteenth notes.
`build/mini_bass/clean_sixteenths.wav` auditions the same notes with the new
output stage and drift disabled.
The comparison renders also disable Background Hiss. The updated project
enables one quiet noise floor on the bass instance, with hiss off on the
other instances so the arrangement does not stack several noise floors.
The project opens on **Free OSC 16ths**; the reference arrangement also
includes the short upper sixteenth-note phrases that begin around four seconds.
The bass and upper line occupy independent patterns in parallel song lanes,
each with its own monophonic instance. Every saved pattern has just one
instrument track and one note column. `bass.wav` and `upper_sixteenths.wav`
render the parts individually.

[Smalltown Boy](../../examples/smalltownboy) is a separate bass example and
estimated preset from a supplied excerpt: alternating low/high eighth notes,
quieter lower oscillators and a darker, plucked filter on one mono Mini. It does not change the
startup patch or the earlier bass example.

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
drive/resonance/feedback, relative harmonic energy from OSC Drive, longer
release tails, and clean oscillator restoration from legacy binary states.
They also check that detached fast notes preserve oscillator phase and that
the brighter low-pass patch has more upper-frequency energy. Simultaneous
keys are checked for a single selected fundamental, and the project test
verifies that the parallel lanes use separate monophonic instrument channels.
Output-stage tests check extra even harmonics, DC removal, master-volume
independence and silent tails. Held-note tests check stable tuning with
Drift at zero and smooth, bounded wander on each oscillator with it enabled.
Hiss tests check the idle noise floor, level adjustment, sample-rate
consistency, silent Off/mute states, saved settings and legacy-state defaults.
The app's `vst3_host` and `mini_bass_tui` tests also
exercise the plugin and example in mlacker.
Steinberg's SDK validator also passes all 47 tests for the built macOS bundle.
