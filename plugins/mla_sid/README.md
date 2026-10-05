# Mla SID

A VST3 synthesizer instrument in the style of the Commodore 64's MOS 6581 and
8580 SID sound chip (subcategory `Instrument|Synth`): three oscillators with
triangle, sawtooth, pulse and noise waveforms, ring modulation and hard sync,
the SID's ADSR envelopes, and its multimode filter. Every SID register can be
written from a MIDI CC, and the usual knobs of a MIDI keyboard play the
waveform, cutoff, resonance and envelope.

The chip is MLang: `src/mla_sid_dsp.mla` emulates the SID and is driven only
by register writes, like the real chip. The C++ layer (`src/plugin.cpp`) is
the "player routine" a C64 music program would be: it turns MIDI notes into
FREQ and GATE writes, runs glide, vibrato, arpeggios, pulse-width and filter
sweeps once per video frame, and maps parameters and CCs onto the registers.

## The chip

- **Oscillators.** 24-bit phase accumulators clocked at the C64's clock
  (985248 Hz PAL or 1022727 Hz NTSC). Note pitches are rounded to the 16-bit
  FREQ register, as on a C64. Noise is the SID's 23-bit LFSR, clocked by the
  oscillator, so its colour follows the note.
- **Combined waveforms** AND their outputs, as early emulators did. Noise
  combined with another waveform writes the AND back into the LFSR, which
  locks up (silent noise) until **Test** is set, as on the chip.
- **Ring modulation** XORs a voice's triangle with its source voice's MSB;
  **hard sync** restarts it when the source's MSB rises. Voice 1's source is
  voice 3, voice 2's is voice 1, voice 3's is voice 2.
- **Envelopes** are the SID's: the 16 attack times from 2 ms to 8 s (decay and
  release three times longer), the exponential decay, sustain at 17 × the
  nibble. Raising the sustain while a note sustains does not raise its
  level, as on the chip. The **ADSR bug** is there: after a slow release a
  fast attack may wait up to 33 ms for the 15-bit rate counter to wrap, and
  a gate toggled at full level freezes the envelope. **Hard Restart** (on
  by default) avoids both, as C64 players do.
- **Filter.** 12 dB/octave, low-, band- and high-pass summed, 11-bit cutoff,
  4-bit resonance. The **6581** follows its measured cutoff curve (flat at the
  bottom, steep in the middle, a drop at FC 1024), its filter input saturates,
  its voices sit on a DC offset that thumps with the envelope, and volume
  writes click (the "$D418 digi" trick works). The **8580** is near linear,
  more resonant and clean.
- The chip runs at about 384 kHz (8× at 44.1/48 kHz) and is decimated
  through an 8th-order low-pass, so the raw waveforms alias little. The
  output is AC-coupled at 16 Hz like the C64's, mono on both channels.

## Playing

**Play Mode** decides which SID voices the keys play. Voices with **Keys**
off ignore notes.

| Mode     | Keys play |
|----------|-----------|
| Poly     | Up to three notes, one voice each. A fourth note takes the voice released longest ago, else the oldest. |
| Unison   | Every voice plays the newest note, each with its own **Transpose** and **Detune** (a fat stacked lead). Overlapping notes are legato and glide. |
| Arp      | The held notes, lowest to highest, one every **Arp Speed** frames on all voices: the C64 chord arpeggio. |
| Channels | MIDI channel 1 plays voice 1, channel 2 voice 2, channel 3 voice 3 (channel n: voice n mod 3), each one note at a time: three tracker tracks, three SID voices. |

**Link Voices** (on by default) makes voices 2 and 3 use voice 1's waveform,
pulse width, envelope and filter routing, so a Poly or Unison patch is set on
voice 1 alone. Turn it off for a different sound on each voice.

A voice with **Keys** off keeps sounding at its **Freq** (or the frequency
its FREQ registers were last written with), gated along with the notes: a
drone, or a sync or ring modulator for another voice. With **3 Off** and
**V3 Mod**, voice 3 becomes an LFO for the filter, as in many C64 tunes.

Velocity does nothing by default, as on the SID; **Velocity** sets how much
it scales a note's level.

The "player" works once per video frame (50 Hz PAL, 60 Hz NTSC), as C64
music players do: glide, vibrato, the pulse-width sweep, the filter envelope,
V3 Mod and arpeggios step at that rate. **Player Speed** runs it 2, 4 or 8
times per frame (the C64's "multispeed" tunes) for smoother modulation.
Notes start and end sample-accurately.

## MIDI controllers

| CC | Controls |
|----|----------|
| 1 (mod wheel) | Vibrato depth |
| 5 | Glide |
| 7 | Output |
| 70 | V1 Waveform |
| 71 | Resonance |
| 72 | V1 Release |
| 73 | V1 Attack |
| 74 | Cutoff |
| 75 | V1 Decay |
| 76 | Vibrato Rate |
| 77 | V1 Pulse Width |
| 78 | Vibrato Delay |
| 79 | V1 Sustain |
| 80 | Filter Mode |
| 81 | PW Sweep |
| 82 | Filter Env |
| 83 | Play Mode |
| Pitch bend | ± Bend Range semitones |
| 20 .. 44 | SID registers $D400 .. $D418 (below) |

With Link Voices on, the V1 controls are the whole patch's.

### Register CCs

CC 20 + *r* writes SID register $D400 + *r*. A CC has seven bits, so the CC
value is mostly the register's top seven bits (CC = register / 2), and the
bit that does not fit is filled in so that CC 0 writes $00 and CC 127 the
maximum:

| CC | Register | CC value |
|----|----------|----------|
| 20, 27, 34 | Voice 1/2/3 FREQ Lo | register / 2; bit 0 copies bit 7 |
| 21, 28, 35 | Voice 1/2/3 FREQ Hi | register / 2; bit 0 copies bit 7 |
| 22, 29, 36 | Voice 1/2/3 PW Lo | register / 2; bit 0 copies bit 7 |
| 23, 30, 37 | Voice 1/2/3 PW Hi | 0..127 spans 0..15 (CC / 8) |
| 24, 31, 38 | Voice 1/2/3 Control | register / 2: Noise 64, Pulse 32, Saw 16, Triangle 8, Test 4, Ring 2, Sync 1. GATE is the keys'. |
| 25, 32, 39 | Voice 1/2/3 Attack/Decay | register / 2; bit 0 copies bit 7 |
| 26, 33, 40 | Voice 1/2/3 Sustain/Release | register / 2; bit 0 copies bit 7 |
| 41 | FC Lo | 0..127 spans 0..7 (CC / 16) |
| 42 | FC Hi | register / 2; bit 0 copies bit 7 |
| 43 | Res/Filt | resonance × 8 + routing (Filt 3: 4, Filt 2: 2, Filt 1: 1); FILTEX stays 0 |
| 44 | Mode/Vol | register / 2: 3 Off 64, HP 32, BP 16, LP 8, + volume / 2; volume bit 0 copies bit 3 |

So `$41` (pulse, gated) is CC 24 = 32, `$81` (noise) is 64, `$11`
(triangle) is 8; `$F8` in Sustain/Release is 124; `$1F` in Mode/Vol is 15.
A register CC sounds at once and sample-accurately, and lasts until the next
write to that register or parameter: the plug-in's parameters are the patch,
register CCs play over it (the parameter editor keeps showing the patch).
Notes own pitch and gate: FREQ writes reach voices with Keys off, and the
Control register's GATE bit always comes from the keys. With Link Voices
on, voice 2 and 3 registers other than FREQ follow voice 1's.

Register CCs make tracker-style SID programming possible: a CC 24 column
switching `64` (noise) on the first row of a note and `32` (pulse) on the
next is the classic C64 drum or "noise attack" lead.

## Parameters

| ID  | Name | Range | Notes |
|-----|------|-------|-------|
| 100 | Output | -60 .. +6 dB (bottom = off) | CC 7. |
| 101 | Chip | 6581 / 8580 | |
| 102 | Clock | PAL / NTSC | Pitch grid, frame rate. |
| 103 | Play Mode | Poly / Unison / Arp / Channels | |
| 104 | Link Voices | Off / On | Default On. |
| 105 | Glide | 0 .. 2 s | |
| 106 | Bend Range | 0 .. 24 st | Default 2. |
| 107 | Pitch Bend | | The pitch bend wheel. |
| 108 | Vibrato | 0 .. 1 st | Mod wheel. Triangle. |
| 109 | Vibrato Rate | 0.5 .. 12 Hz | Default 6 Hz. |
| 110 | Vibrato Delay | 0 .. 2 s | Fades vibrato in after a note starts. |
| 111 | PW Sweep | 0 .. 100 % | Pulse-width LFO depth (100 % = ±2047). Each voice's sweep is a third of a cycle apart. |
| 112 | PW Sweep Rate | 0.05 .. 10 Hz | |
| 113 | Filter Env | -100 .. +100 % | Cutoff jump (±2047) on each note, falling back. |
| 114 | Filter Decay | 0.01 .. 5 s | Time to fall to 10 %. |
| 115 | V3 Mod | Off / OSC3 / ENV3 | Reads voice 3's waveform ($D41B) or envelope ($D41C) each frame... |
| 116 | V3 Mod Amount | -100 .. +100 % | ...and adds it to the cutoff (100 % = 2047 at 255). |
| 117 | Arp Speed | 1 .. 16 frames | Default 2. |
| 118 | Player Speed | 1x / 2x / 4x / 8x | Frame ticks per video frame. |
| 119 | Hard Restart | Off / On | Default On. |
| 120 | Velocity | 0 .. 100 % | Velocity to level. |
| 121 | Cutoff | 0 .. 2047 | FC; default 1400. CC 74. |
| 122 | Resonance | 0 .. 15 | CC 71. |
| 123 | Filter Mode | Off, LP, BP, LP+BP, HP, LP+HP, BP+HP, LP+BP+HP | Default LP. Off silences the voices routed to the filter, as on the chip. |
| 124 | Volume | 0 .. 15 | Default 15. |
| 125 | 3 Off | Off / On | Mutes voice 3 unless it goes through the filter. |

Each voice *v* (0..2) has the parameters 200 + 20 *v* + field:

| Field | Name | Range | Notes |
|-------|------|-------|-------|
| 0 | Waveform | Off, Tri, Saw, Tri+Saw, Pulse, ..., Noise, ..., All | The Control register's top nibble. Default Pulse. |
| 1 | Pulse Width | 0 .. 4095 | Default 2048 (50 %). |
| 2 | Attack | 2 ms .. 8 s (16 steps) | Default 2 ms. |
| 3 | Decay | 6 ms .. 24 s (16 steps) | Default 750 ms. |
| 4 | Sustain | 0 .. 15 | Default 10. |
| 5 | Release | 6 ms .. 24 s (16 steps) | Default 300 ms. |
| 6 | Sync | Off / On | |
| 7 | Ring | Off / On | Needs a triangle. |
| 8 | Test | Off / On | Holds the oscillator; unlocks noise. |
| 9 | Filter | Off / On | Routes the voice through the filter. Default On. |
| 10 | Keys | Off / On | Default On. |
| 11 | Transpose | ±24 st | |
| 12 | Detune | ±50 cents | |
| 13 | Freq | 0, 0.05 .. 3900 Hz | The frequency with Keys off. Default 440 Hz. |

The register CC targets are hidden parameters 300 .. 324. Parameter IDs are
stable, and new parameters are appended.

## In mlacker

1. **Track → Create Instrument track**, then **Instrument → Add instrument**
   and choose `MlaSid.vst3`.
2. Enter notes. Add CC columns (**Track → Automation → Add CC column**) for
   `cc:74` (cutoff), `cc:70` (waveform), or any register, `cc:20` .. `cc:44`.
3. For three independent SID voices, like a C64 tracker, set **Play Mode** to
   Channels and assign the same instance to three adjacent Instrument
   tracks. mlacker gives each track that shares an instance its own MIDI
   channel (track index mod 16), and channel *n* plays voice *n* mod 3, so the
   three tracks play the three voices.

Two example projects use it. They expect the plug-in in
`~/.local/plugins/VST3` (`./build.sh --plugins --install`).

- `examples/mlasidtest.mlaproj` is a short C64-style tune in the pattern list
  (A minor, 125 BPM): nine patterns, each holding all the parts. Instrument 1
  (a 6581 in Channels mode) plays the Lead, Bass and Drums tracks on its
  three voices; instrument 2 (an 8580 in Arp mode) arpeggiates the chords.
- `examples/mlasidmatrix.mlaproj` is made for the song matrix (**Shift+M**).
  Each lane is one part with its own Mla SID: **L1** Drums, **L2** Bass,
  **L3** Arpeggio, **L4** Lead. Every pattern is the same four bars (Am9,
  Fmaj7, Cadd9, Gadd9: stacked fifths with added tones; the bass plays root,
  fifth and octave), so any variation fits any row, and every pattern sets its instrument's
  whole sound with CCs on its first row (waveform, pulse width, envelope,
  filter, vibrato, PW sweep, glide). Swapping one cell changes that part's
  notes and sound while the other lanes play on:

  | Lane | Variations |
  |------|------------|
  | L1 Drums | Hats Intro, Beat A, Beat B, Beat Half, Beat Electro, Beat End |
  | L2 Bass | Bass Saw (octaves), Bass Pulse (syncopated, resonant), Bass Long (whole notes, slow resonant saw), Bass Walk (walking pulse) |
  | L3 Arpeggio | Arp Soft (triangle), Arp Pulse (cutoff sweep), Arp Sweep (resonant band-pass), Arp High (sawtooth an octave up, high-pass) |
  | L4 Lead | Lead A (pulse with vibrato and PW sweep), Lead B Saw (sawtooth with glide), Lead C Noise (16th arpeggios, noise attacks), Lead D Flute (triangle, slow attack, deep vibrato) |

- `examples/mlasidturbo.mlaproj` is a racing-game title intro in the style of
  late-80s C64 arcade conversions (an original tune, E minor, 140 BPM), also
  for the matrix. Everything is Mla SID, the drums included. Lanes: **L1**
  Drums (Start, Drive, Drive Fill with toms, Half Time, Finish), **L2** Bass
  (Run: 16th-note octaves, Fifths, Hold), **L3** Arpeggio (one-frame chords
  in stacked fifths: Fast, Bright, Soft), **L4** Lead (Theme, Answer, Hold,
  and Sync Theme on its own instrument: a sawtooth hard-synced 16 semitones
  above its source voice) and **L5** FX (Engine Riser: noise gliding up over
  four bars; Pass By: a car flashing past).

The examples show off register CCs. The drums are one SID voice whose waveform and
decay are written per hit: a kick is noise for one 1/64 step, then triangle
(`64 8 . .` in the voice's Control register CC), and the hats are noise with
a short or long decay (Attack/Decay register CC). Noise attacks on lead
notes work the same way (`64 32 . .`), and CC 74 sweeps the filters.

## Build and test

From the repository root, `./build.sh --plugins` builds every plug-in,
including this one (`--install` copies it to the plug-in directory). On its
own:

```sh
../../subprojects/mlang/build/mlang pkg --config mlang.toml run build   # -> build/cmake/VST3/Release/MlaSid.vst3
../../subprojects/mlang/build/mlang pkg --config mlang.toml run test
```

`tests/mla_sid_tests.cpp` loads the built bundle through the SDK hosting
classes and checks rendered audio:

- bus layout and the CC map
- silence, no thump at start-up, exact zeros after a release
- pitch on the FREQ grid (A4 = 440 Hz) and pitch bend
- sample-accurate notes and register writes
- the envelope times, sustain level and release, and the ADSR bug with and
  without hard restart
- the waveforms (the sawtooth's 1/n partials, a 25 % pulse's missing 4th
  partial, noise), the filter on both chips, high-pass, filter mode Off
- register CCs: Control, Sustain/Release, Mode/Vol, FC Hi, Res/Filt
- Poly voice stealing, Unison transposes, glide and legato, the arpeggio,
  Channels
- hard sync, ring modulation, vibrato, the filter envelope, V3 Mod, velocity
- state round trips and bounded output

Given an output directory as a second argument, the test runner also writes
example sounds there as WAV files. The SDK's `validator` passes (47/47).
mlacker's host test (`tests/vst3_tests.cpp`) also loads the bundle, plays a
note and writes $D418 from an instrument CC.
