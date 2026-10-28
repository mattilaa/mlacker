# Mla 8000

A VST3 synthesizer instrument in the style of the Roland JP-8000
(subcategory `Instrument|Synth`): eight voices with the JP-8000's Super Saw
and its other oscillator waveforms, a second oscillator with sync, ring and
cross modulation, a 12/24 dB multimode filter, three envelopes, two LFOs,
portamento, Poly, Mono, Legato and Unison key modes, and the JP-8000's
built-in effects: tone, chorus and delay.

It is an emulation built from published analyses, not from Roland's code or
ROMs. The Super Saw follows Adam Szabo's measurements of the real unit
closely. The other waveforms, the filter and the effects reproduce the
JP-8000's controls and their character rather than its exact circuits.

The voice is MLang: `src/mla_8000_dsp.mla` runs one voice's oscillators,
envelopes and filter. The C++ layer (`src/plugin.cpp`) plays the panel. It
allocates the eight voices and runs the LFOs, portamento, pitch bend and the
mod wheel every 16 frames. The effects run after the voices.

## The voice

- **Super Saw** (OSC 1 wave 0). Seven saws at Szabo's measured frequency
  offsets from the centre saw (−11.0 %, −6.3 %, −2.0 %, 0, +2.0 %, +6.2 %,
  +10.7 % at full detune).
  - **Ctrl 1** is the Detune knob, through his fitted 11th-order curve. It
    stays gentle up to about 0.6 and then grows steeply: about ±37 cents on
    the outer saws at 0.7 and ±1.8 semitones at 1.0.
  - **Ctrl 2** is the Mix knob, through his centre-gain and side-gain
    curves.
  - Every saw starts at a random phase on each note.
  - A 12 dB high-pass at the fundamental removes the beating below it, as on
    the JP-8000.
  - The saws are band-limited with polyBLEP.
- **The other OSC 1 waves.** Each uses Ctrl 1 and Ctrl 2:
  - Triangle Mod: a sine-folded triangle. Ctrl 1 sets the fold, Ctrl 2 how
    far LFO 1 moves it.
  - Noise: Ctrl 1 darkens it.
  - Feedback Osc: a self-modulating sine that runs from sine towards a
    bright saw. Ctrl 2 adds the octave above.
  - Square: Ctrl 1 sets the width, Ctrl 2 the PWM from LFO 1.
  - Saw and Triangle.
- **OSC 2:** square (with its own pulse width), saw, triangle or noise.
  - Range is −24 to +24 semitones, Fine ±50 cents.
  - **Sync** restarts OSC 2 with OSC 1.
  - **Ring Mod** multiplies the two oscillators.
  - **Cross Mod** moves OSC 1's frequency by OSC 2 (linear FM).
  - **OSC Balance** crossfades OSC 1 and OSC 2.
- **Pitch envelope:** attack, decay and a depth of ±63 semitones, applied to
  both oscillators.
- **Filter:** LPF, BPF or HPF at −12 or −24 dB/octave.
  - It is built from topology-preserving state-variable filters. At −24 dB
    two run in series, with the resonance on the second.
  - Cutoff runs from 20 Hz to 20 kHz.
  - Key Follow (0–100 %) is measured from C-4.
  - The envelope depth is ±8 octaves.
  - The input saturates gently, and the resonant peak saturates instead of
    growing without bound.
- **Amp:** an ADSR with a linear attack and exponential decay and release
  (times to −60 dB), velocity sensitivity, and LFO tremolo.

## Playing

- **Key Mode**:
  - Poly: 8 voices, stealing the quietest released voice, else the oldest.
  - Mono: retriggers on every key.
  - Legato: retriggers only from silence.
  - Unison: all eight voices on one key, spread across ±Unison Detune and
    panned across the stereo field. Every voice starts its oscillators at
    its own random phases, so the stack sums smoothly, about 3 dB louder
    than one voice, with no spike at the start of each note. The JP-8000
    has no Unison mode; its successor, the JP-8080, added one.
- **Portamento** is a constant-time glide. In Poly mode it glides from the
  last key played; in the other modes from the voice's current pitch.
- **LFO 1** has Triangle, Saw, Square or S&H shapes, a rate of 0.05–30 Hz,
  and a fade-in after each note. It modulates pitch, the filter, the amp,
  PWM and the triangle fold.
- **LFO 2** is a triangle that modulates pitch, the filter and the amp.
- **Mod Wheel** (CC 1) moves the cutoff by **Wheel > Cutoff** (±8 octaves)
  and adds vibrato from LFO 1 by **Wheel > LFO 1 Pitch**. With Wheel >
  Cutoff turned up, a CC 1 column in mlacker sweeps the filter.

## Effects

- **Tone:** Bass (a 200 Hz shelf) and Treble (a 4 kHz shelf), ±12 dB each.
- **Chorus:**
  - Chorus 1: one slow tap per side.
  - Chorus 2: twice the rate.
  - Super Chorus: three taps a side, a third of a cycle apart.
  - Flanger: a short tap with feedback.
  - Rate, Depth and Level control it.
- **Delay:** Stereo, or Panning (ping-pong: repeats alternate left and
  right).
  - Time runs from 1 to 1250 ms. Delay Sync locks it to the host tempo, from
    1/16 to 1/2 (the default is a dotted eighth).
  - Each repeat is darkened. The feedback saturates, so full feedback holds
    rather than grows.
- **Drive** saturates the input of the voice filter.

## MIDI controllers

| CC | Parameter |
|----|-----------|
| 1 | Mod Wheel |
| 5 | Portamento Time |
| 7 | Output |
| 16, 17 | OSC 1 Ctrl 1, Ctrl 2 (Super Saw Detune, Mix) |
| 18 | OSC Balance |
| 19 | Filter Env Depth |
| 71 | Resonance |
| 72, 73, 75 | Amp Release, Attack, Decay |
| 74 | Cutoff |
| 76 | LFO 1 Rate |
| 91 | Delay Level |
| 93 | Chorus Level |
| Pitch bend | Pitch Bend (Bend Range 0–24 semitones) |

## Parameters

Parameter IDs start at 100 and are stable: new parameters are appended. The
full list in ID order:

- **Global:** Output, Key Mode, Unison Detune, Portamento, Portamento Time,
  Bend Range, Pitch Bend, Mod Wheel, Wheel > Cutoff, Wheel > LFO 1 Pitch,
  Velocity > Amp, Velocity > Filter.
- **LFOs:** LFO 1 Wave, LFO 1 Rate, LFO 1 Fade, LFO 2 Rate, LFO 2 Pitch,
  LFO 2 Filter, LFO 2 Amp.
- **Oscillators:** OSC 1 Wave, OSC 1 Ctrl 1, OSC 1 Ctrl 2, OSC 2 Wave,
  OSC 2 Range, OSC 2 Fine, OSC 2 PW, OSC Sync, Ring Mod, Cross Mod,
  OSC Balance, LFO 1 Pitch, Pitch Env Depth, Pitch Env Attack,
  Pitch Env Decay.
- **Filter:** Filter Type, Filter Slope, Cutoff, Resonance, Key Follow,
  LFO 1 Filter, Filter Env Depth, Filter Attack, Filter Decay,
  Filter Sustain, Filter Release.
- **Amp:** Level, LFO 1 Amp, Amp Attack, Amp Decay, Amp Sustain, Amp Release.
- **Effects:** Tone Bass, Tone Treble, Chorus, Chorus Rate, Chorus Depth,
  Chorus Level, Delay, Delay Sync, Delay Time, Delay Feedback, Delay Level,
  Drive.

**Value mappings:**

- Envelope and portamento times are 0 at the bottom of the knob, then 1 ms
  to 10 s, exponential. The normalized value is log10(t / 1 ms) / 4.
- Cutoff is 20 Hz × 1000^n.
- Bipolar knobs (the depths, Fine, Tone) are centred at 0.5.

**Defaults:** the classic trance lead. That is a Super Saw at Detune 0.7 and
Mix 0.8, the 24 dB low-pass at 8 kHz, Chorus 1, and a dotted-eighth Panning
delay.

## In mlacker

1. **Track → Create Instrument track**, then **Instrument → Add instrument**
   and choose `Mla8000.vst3`.
2. Turn up **Wheel > Cutoff** and lower **Cutoff**. The track's CC1 column
   then sweeps the filter. `cc:74` moves the Cutoff knob itself.

`examples/mla8000_demo.mlaproj` is an original epic-trance song played
entirely on Mla 8000, with Mla 08 drums. It is at 138 BPM in B-flat minor, in
the style of the late-90s string-theme remixes. It has four patches:

- a mono saw-and-square bass
- a gated Super Saw stab
- a slow Super Saw pad
- a Super Saw lead

The CC1 columns sweep their filters: the pad opens through the intro and the
breakdown, and the gate through the build. To open it, press **Shift+M**,
then **Space**:

```sh
build/cmake/bin/mlacker examples/mla8000_demo.mlaproj
```

`examples/jv_pads/build.sh mla8000` regenerates the project from
`examples/jv_pads/mla8000_song.mla`.

`examples/mla8000_keyfollow.mlaproj` demonstrates **Key Follow**. Three Mla
8000s play the same patch: a resonant saw through the 24 dB low-pass at
800 Hz. They differ only in Key Follow: 0 %, 50 % and 100 %. Each of the
first three patterns plays an Am7 arpeggio that climbs four octaves from A-1
to G-5 and comes back down, on one of them.

- At 0 % the cutoff stays put. Low notes buzz, and high notes lose their
  harmonics and go dull.
- At 100 % the cutoff moves an octave for every octave played, so every note
  keeps the same tone.

The "Groove" pattern then uses the 100 % patch over a bass that is also
key-followed, with Mla 08 drums. `examples/jv_pads/build.sh keyfollow`
regenerates it.

`examples/mla8000_supersaw_arp.mlaproj` puts Key Follow to work on a Super
Saw arpeggio lead. It is an original trance song at 138 BPM in B-flat minor,
in the same late-90s style. The arpeggio rolls in 16ths over two and a half
octaves, on two tracks with the same patch.

The patch is made fat the way Arturia's Jup-8000 V trance presets are (Big
En-Trance, King of Saws):

- all eight voices stacked in Unison, each a Super Saw
- OSC 2's saw an octave up underneath
- the 12 dB low-pass at 1.26 kHz with little resonance
- the treble shelf boosted
- a little drive, Super Chorus and the panning delay

Measured against renders of those presets, its stereo width and presence
sit between the two. The two tracks differ only in Key Follow:

- In the intro it plays at Key Follow 0 %, so its top notes are muffled.
- At the drop, Key Follow 100 % takes over and the top notes ring as
  brightly as the bottom ones.

The CC1 column then opens it further through the breakdown and the build.

The mix is balanced against a mastered trance track (Matti Laamanen's
"Flakes"):

- a deep 909 kick and a bass with a triangle sub an octave down
- an MlaLimiter on every track, driving each 6–14 dB into its ceiling, for
  the density of a mastered track
- after the limiters, [Mla Pump](../mla_pump) on the bass, the pad and the
  arpeggios: a sidechain pump locked to the beat. The pad and the arpeggios
  dip 9 dB under each kick and swell back over most of the beat. The bass
  dips less and is back by its offbeat note. The breakdown, without a kick,
  leaves the pump out.

`examples/jv_pads/build.sh supersaw_arp` regenerates it.

## Build and test

From the repository root, `./build.sh --plugins` builds every plug-in,
including this one. Add `--install` to copy them to the plug-in directory.
To build and test this plug-in on its own:

```sh
../../subprojects/mlang/build/mlang pkg --config mlang.toml run build   # -> build/cmake/VST3/Release/Mla8000.vst3
../../subprojects/mlang/build/mlang pkg --config mlang.toml run test
```

`tests/mla_8000_tests.cpp` loads the built bundle through the SDK hosting
classes and checks rendered audio:

- the bus layout and exact silence when idle
- pitch
- the Super Saw's outer saws at Szabo's offsets, Mix adding the side saws,
  and the high-pass below the fundamental
- the filter and its resonance, and CC 1 opening the filter
- attack and release
- the Poly, Mono and Unison modes
- the portamento glide
- the panning delay's first repeat
- state round trips
- bounded output under overload

With a second argument (a directory), it also writes an example WAV there.
