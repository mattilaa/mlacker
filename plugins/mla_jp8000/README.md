# Mla JP8000

A pad-focused, two-part JP-8000 performance instrument, written in MLang and
C++. The default performance is **Obsession Atmosphere**: a slowly opening
Super Saw layer over a darker PWM layer, with independent movement, chorus,
and opposite-direction panning delays. Six original performances are included
in [`presets/`](presets).

This is an independent synthesis model. It does not use Roland firmware,
wave ROMs, or samples from the reference song. It is separate from Mla 8000,
with its own VST3 class ID, so existing Mla 8000 sessions keep their parameter
layout and identity.

## Play it

Build with `./build.sh --plugins`, then choose **Instrument > Add instrument**
and select `build/plugins/VST3/Release/MlaJP8000.vst3`. An Instrument track
playing the plugin can play the default pad immediately. Open its editor to
edit the performance and both panels. Use **Instrument > Presets > Load plugin
preset** to load a `.mlapre` from this directory.

The performance controls come first, followed by parameters prefixed **Upper**
and **Lower**. Most continuous sliders use normalized 0–1 values in mlacker's
generic editor; stepped controls show their labels. Useful starting controls:

- **Brightness** opens/closes both filters, independently of their individual
  settings. It is centered at 0.5 and has a ±4 octave range.
- **Filter Slope** selects **12 dB/oct** or **24 dB/oct** for each layer.
  Sequencer **CC22 / CC23** select Upper / Lower slope: `0` is 12 dB/oct,
  `127` is 24 dB/oct.
- **CC20 / CC21** from mlacker sequencer columns set Upper / Lower **Filter
  Key Follow**. Put `127` in both for full tracking, or `0` for fixed cutoff.
  Each note then moves its own filter around C4.
- **Mod Wheel** follows each part's **Wheel > Cutoff** and **Wheel > LFO 1 Pitch**
  depths. The factory pads use it for a gradual lift in brightness.
- **Part Detune**, **Upper Pan**, and **Lower Pan** set the layer spread.
- Each part has its own oscillator mix, filter/envelopes, modulation, tone,
  chorus, and delay. No reverb is built in, matching the hardware's effect types.

## Performance architecture

**Single Upper** and **Single Lower** give the selected part all eight voices.
**Dual** layers the two patches. **Split** plays Lower at/below **Split Point**
and Upper above it. Dual and Split share exactly eight sounding voices;
**Voices Lower / Upper** selects 2/6, 3/5, 4/4, 5/3, or 6/2. A four-note pad
therefore fills the default 4/4 performance. Released voices are stolen first,
then the oldest held voice within the exhausted part.

Each part has Poly, Mono and Legato modes, portamento, transpose, and pan.
There is no JP-8080-style Unison mode in this instrument. Part Detune is a
relative ±50 cent difference between parts, applied symmetrically. **Upper
MIDI Channel** and **Lower MIDI Channel** accept Omni or a numbered channel;
Omni on both is the default for playing layers from a single tracker track.
The performance pedal and common controllers affect both parts.

Changing mode, voice allocation, split point or MIDI-channel assignment clears
held notes to avoid stranded notes under a new routing configuration.
Repeated pitches from different MIDI channels stay held until the last
corresponding key is released. Sustain defers release until the pedal rises.

## Sound engine

Each voice has the seven-wave OSC 1, three-wave OSC 2, ring/sync/cross modulation, pitch
AD, filter ADSR and amp ADSR of the shared Mla 8000 engine. The Super Saw has
seven independent phases, Szabo's measured asymmetric detune offsets and mix
curves, and a high-pass at the fundamental. Its high-order detune fit uses f64
to avoid cancellation near full detune; cross modulation also reaches the
Super Saw's actual phase increments. PWM pulses have their duty-cycle DC
removed before the filter so slow pulse modulation does not bias or saturate
the filter with an inaudible offset. A 5 Hz output DC blocker removes
residual bias from nonlinear processing, before the master gain.

The filter offers 12/24 dB LP/BP/HP responses. Two independent LFOs per part
move pitch, cutoff, amplitude and waveform controls. Layer-specific seeds keep
phases and slow movement independent. Voices and modulation are shared with
[Mla 8000](../mla_8000), whose full control descriptions apply.

The JP8000 performance has twelve named chorus/flanger/phaser models plus
Off: four Super Chorus variants, three flangers, Deep Phasing Slow, Jet
Phasing, Twisting, and two fixed-phase models. Phasers use six all-pass
stages per side; chorus uses independently modulated stereo taps. Five delay
models cover panning L→R, R→L, short panning, short mono, and long mono. Short
models cap the free delay time at 400 ms. Host tempo sync, feedback and level
remain independent per part. Phaser coefficients update at the control rate.

Processing uses bounded event/parameter queues and allocates no C++ storage
on the audio thread. The performance state validates the complete input
before applying it, and preserves the current performance on truncated or
invalid input. Parameter IDs: Upper 100–162, Lower 1100–1162, performance
2000–2014. No added processing latency is reported.

## Original pad bank and audition project

| Preset | Character |
| --- | --- |
| Obsession Atmosphere | Slow Super Saw/PWM layers, complementary movement and panning echoes |
| Velvet PWM | Two evolving pulse layers with clear chorus |
| Silver Horizon | Brighter Super Saw layers with an upper octave shimmer |
| Glass Tide | Triangle modulation, octave triangle and slow phasing |
| Warm Fifths | Saw pad with a quiet fifth in the lower layer |
| Midnight Split | Lower bass and upper pad divided at B3 |

Generate `examples/jp8000_pads.mlaproj` with
`examples/jp8000_pads/build.sh`, then open it:

```sh
build/cmake/bin/mlacker examples/jp8000_pads.mlaproj
```

Press **Shift+M**, then **Space** to play the six performances in song order.
Each is an isolated chord followed by space for its release and effects.
The generator uses the bundle in this checkout; regenerate the project if the
checkout moves. It does not require plugins installed into your home directory.

`examples/jp8000_keyfollow.mlaproj` compares three of these pads at full and
zero tracking, with ascending root–fifth chords. The sequencer's CC20/CC21
columns drive both filter amounts; the saved patch amounts start at zero.
Run `examples/jp8000_pads/build.sh keyfollow` to regenerate it, then:

```sh
./build/cmake/bin/mlacker examples/jp8000_keyfollow.mlaproj
```

Press **Shift+M**, then **Space**. See the [example guide](../../examples/jp8000_pads/README.md).

For a matching **24 dB versus 12 dB** pad comparison, run:

```sh
./build/cmake/bin/mlacker examples/jp8000_filter24.mlaproj
```

Press **Shift+M**, then **Space**. CC22/CC23 select both filter slopes, and
CC20/CC21 keep key follow full. Regenerate with
`examples/jp8000_pads/build.sh filter24`. The generator verifies the actual
sequencer-to-plugin selection of both slopes on both parts before saving.

## Validation and renders

```sh
ctest --test-dir build/plugins -R 'jp8000_(processor|performance)' --output-on-failure
ctest --test-dir build/cmake -R jp8000_tui --output-on-failure
build/plugins/bin/Release/mla_jp8000_tests plugins/mla_jp8000/presets build/jp8000/auditions
```

The last command regenerates the bank and writes twelve-second stereo WAVs:
six seconds held, then six seconds of release/tail. Tests cover part/channel
routing, voice allocation/stealing, transposition, sustain, overlapping
channels, sample-accurate events, block-size invariance, all effect models,
state rejection/round trips, several sample rates, pad headroom/stereo width,
and absence of C++ allocations during processing. The TUI test loads the real
bundle, imports every factory preset, opens its editor and reopens a session.

## Reference and limits

The requested pad reference is [Matti Laamanen — Obsession (Original mix)](https://www.youtube.com/watch?v=GdwwJXsTSUo).
The original factory bank was designed before reference audio could be retrieved.
The additional [Obsession studies](../../examples/jp8000_pads#obsession-studies)
use analysis of the recording: an estimated 138 BPM and the wider, less
bass-heavy passage around 3:20–4:00. Their notes and arrangements are original;
an exact sound match has **not** been verified. The upload description credits
both a JP-8000 and a JV-1080, so the recording is not a JP-8000-only reference.

Architecture and control names were checked against Roland's
[JP-8000 brochure](https://cdn.roland.com/assets/media/pdf/JP-8000.pdf) and
[owner's manual](https://static.roland.com/assets/media/pdf/JP-8000_OM.pdf).
The measured Super Saw reference is Adam Szabo, *How to Emulate the Super Saw*
(2010), as in the existing Mla 8000 engine. Filters, non-Super-Saw oscillators,
envelope curves and effects remain approximations, without hardware null tests.
Arpeggiator/RPS, Motion Control, ribbon assignment and Roland SysEx patch import
are not implemented. It is not a bit-exact emulation of the complete hardware.
