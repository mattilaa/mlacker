# Mla Vocoder

A VST3 channel vocoder in the style of the Roland VP-330 Vocoder Plus. It has
its own polyphonic carrier synth, the VP-330's "Human Voice" choir and a
string-machine ensemble (subcategory `Fx|Instrument`, so hosts list it as an
effect and as an instrument).

- **Modulator**: the main audio input, for example a voice, a drum loop or any
  sample on the track.
- **Carrier**: either the built-in synth played over MIDI, an external signal on
  the **sidechain** input (`Carrier`, an aux bus), or both mixed.

The signal processing is MLang: `src/mla_vocoder_dsp.mla`, on top of
[`dsp::filter`](../../subprojects/mlang/modules/dsp/filter.mla)'s ladder
filter. The C++ layer (`src/plugin.cpp`) declares the buses, schedules MIDI
sample-accurately, maps parameters and saves state.

This is a VP-330-*style* design, not a circuit model. The band count, the
divide-down-like free-running oscillators, the Male 8'/Female 4' registers,
the delayed vibrato, the choir formants and the three-phase ensemble follow the
instrument's layout. The band frequencies and filter slopes are this plug-in's
own choices.

## Setups

**Voice or sample in, play chords (internal carrier).** Insert Mla Vocoder on the
audio track that plays the voice or sample. Route MIDI to it (a MIDI track whose
output is the vocoder, or the host's "MIDI to effect" routing), and leave
**Carrier** at *Internal*. The held notes set the pitch, and the audio sets the
spectrum.

**External synth as carrier.** Insert the vocoder on the voice track, route the
synth's track to the vocoder's sidechain, and set **Carrier** to *External*. No
MIDI is needed. *Int + Ext* layers the internal synth on top.

**Host without sidechain routing.** Set **Input** to *Split L/R*. Feed the
modulator on the left channel and the carrier on the right channel of the main
input. *External* (or *Int + Ext*) then takes its carrier from the right channel.

**Choir / string-machine only.** Turn **Vocoder Level** down and **Choir Level**
(formant-filtered "aah") or **Synth Level** (the plain VCO/VCF) up. It plays from
MIDI with no audio input.

### In mlacker

mlacker loads Mla Vocoder both as an instrument and as an insert effect. Its
host keeps the sidechain bus inactive and silent. mlacker cannot yet feed the
vocoder a modulator and notes at the same time:

- An instrument slot gets notes but renders from silence, so only the Choir and
  Synth paths sound there.
- An insert gets its track's audio but no notes, so only *External* or
  *Split L/R* carriers work there, and mlacker has no way to route a second
  signal into them.

Full sample + MIDI vocoding in mlacker needs track MIDI routed to inserts, or
a track's audio routed into an instrument's input.

## Signal flow

```
 MIDI ─► 16 voices: saw / pulse / saw+pulse, 8' + 4' (detuned), ADSR each,
         delayed vibrato + mod wheel, pitch bend
         └─► 24 dB ladder VCF (cutoff, resonance, key track, env depth)
              ├─► Synth Level ───────────────────────────────┐
              ├─► 3 formant band-passes ─► Choir Level ──────┤
              └─► carrier (+ sidechain, + noise) ─┐          │
 main in ─► 80 Hz high-pass ─► 10/16/20 analysis bands ─► followers
                                       carrier bands × followers ─► Vocoder Level ─┤
 main in ─► 5 kHz high-pass ─► Sibilance ─────────────────────────────────────────┤
 main in ─► Dry Level ─────────────────────────────────────────────────────────────┤
                                                   three-phase ensemble ◄─────────┘ ─► Output
```

- **Bands**: log-spaced band centres from 160 Hz to 5.6 kHz. Each is a 4th-order
  band-pass (two TPT state-variable sections) on the modulator and the same
  filter on the carrier. A peak follower (Env Attack / Env Release) scales the
  carrier band. Odd bands lean right and even bands left by **Width**.
- **Sibilance**: the modulator above about 5 kHz, which no band covers, passes
  straight through so "s" and "t" stay intelligible. **Noise** adds noise
  to the carrier for unvoiced sounds. It follows the notes (internal) or the
  sidechain's level (external).
- **Formant** shifts the carrier bands (and the choir formants) against the
  analysis bands: up for a smaller, higher voice, down for a larger one.
- **Ensemble**: one delay line read by three taps 120° apart, each swept by a
  slow (0.63 Hz) and a fast (5.9 Hz) LFO, as in string-machine ensembles.

## Parameters

| ID  | Parameter      | Range | Notes |
|-----|----------------|-------|-------|
| 100 | Carrier        | Internal / External / Int + Ext | External = sidechain, or the right channel in Split mode. |
| 101 | Input          | Stereo / Split L/R | Split: left = modulator, right = carrier. |
| 102 | Bands          | 10 / 16 / 20 | 10 is the VP-330 count. |
| 103 | Formant        | ±12 st | Carrier band shift. |
| 104 | Env Attack     | 0.5 .. 50 ms | Band follower attack. |
| 105 | Env Release    | 5 .. 1000 ms | Band follower release. |
| 106 | Sibilance      | 0..1 | High-passed modulator to the output. |
| 107 | Noise          | 0..1 | Noise in the carrier. |
| 108 | Width          | 0..1 | Odd/even band stereo spread. |
| 109 | Vocoder Level  | -60 .. +6 dB (bottom = off) | |
| 110 | Choir Level    | -60 .. +6 dB | Human Voice ("aah" formants), default off. |
| 111 | Synth Level    | -60 .. +6 dB | The VCO/VCF directly, default off. |
| 112 | Dry Level      | -60 .. +6 dB | The modulator, default off. |
| 113 | Ensemble       | 0..1 | Ensemble wet mix; 0 = off. |
| 114 | Ensemble Depth | 0..1 | Sweep depth. |
| 115 | Output         | -60 .. +6 dB | MIDI CC 7. |
| 200 | Wave           | Saw / Pulse / Saw + Pulse | |
| 201 | Pulse Width    | 5 .. 50 % | |
| 202 | Male 8'        | 0..1 | Register level; the choir formants move from male to female with the 8'/4' balance. |
| 203 | Female 4'      | 0..1 | Register level. |
| 204 | Detune         | 0 .. 25 cents | 4' against 8'. |
| 205 | Cutoff         | 20 Hz .. 20 kHz | Ladder VCF. MIDI CC 74. |
| 206 | Resonance      | 0..1 | MIDI CC 71. |
| 207 | Key Track      | 0..1 | 1 = cutoff follows the last note fully. |
| 208 | Filter Env     | 0..1 | Up to +6 octaves from the loudest voice's envelope. |
| 209 | Attack         | 0 .. 2 s | Voice ADSR. MIDI CC 73. |
| 210 | Decay          | 1 ms .. 10 s | To a 60 dB fall. |
| 211 | Sustain        | 0..1 | |
| 212 | Release        | 1 ms .. 10 s | MIDI CC 72. |
| 213 | Vibrato Rate   | 0.5 .. 10 Hz | |
| 214 | Vibrato Depth  | 0 .. 1 st | |
| 215 | Vibrato Delay  | 0 .. 3 s | Vibrato fades in after the first key of a phrase. |
| 216 | Tune           | ±12 st | |
| 217 | Velocity       | 0..1 | Velocity sensitivity of the voice level. |
| 218 | Bend Range     | 0 .. 12 st | |
| 219 | Pitch Bend     | -1 .. +1 | MIDI pitch bend. |
| 220 | Mod Wheel      | 0..1 | MIDI CC 1: adds up to 0.5 st of vibrato. |

Sixteen voices. A repeated key retriggers its own voice. When all voices are
busy, the quietest releasing voice is taken, or else the oldest. Parameter IDs
are stable, and new parameters are appended to their section.

## Build and test

From the repository root, `./build.sh --plugins` builds every plug-in including
this one (`--install` copies it to the plug-in directory). On its own:

```sh
../../subprojects/mlang/build/mlang pkg --config mlang.toml run build   # -> build/cmake/VST3/Release/MlaVocoder.vst3
../../subprojects/mlang/build/mlang pkg --config mlang.toml run test
```

`tests/mla_vocoder_tests.cpp` loads the built bundle through the SDK hosting
classes and checks rendered audio:

- bus layout and arrangements
- silence without notes or without a modulator
- spectral following: a 300 Hz or 3 kHz modulator puts the output's energy there
- formant shift
- the sidechain carrier, an inactive sidechain, and Split mode
- pitch, pitch bend and tune
- the choir, synth and dry paths
- sample-accurate notes, width/ensemble and 16/20 bands
- an overload stress test and state round trips

The SDK's `validator` passes (47/47).
