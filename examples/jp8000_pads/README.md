# JP8000 pad auditions

`../jp8000_pads.mlaproj` plays six original layered/split pad performances on
[Mla JP8000](../../plugins/mla_jp8000). Open it from the repository root, press
**Shift+M**, then **Space**:

```sh
build/cmake/bin/mlacker examples/jp8000_pads.mlaproj
```

The project references the built bundle in this checkout. After building the
plugins, `examples/jp8000_pads/build.sh` regenerates it with the current plugin
path and the shipped factory bank. Existing project saves use the normal
project backup/replace behavior.

The reference is [Matti Laamanen — Obsession (Original mix)](https://www.youtube.com/watch?v=GdwwJXsTSUo).
These are original pad designs; the song has not been copied or transcribed,
and a sound match to its recording has not been verified.

## Sequencer-controlled key follow and fifth chords

```sh
./build/cmake/bin/mlacker examples/jp8000_keyfollow.mlaproj
```

Press **Shift+M**, then **Space**. The six patterns alternate **full** (100%) and
**fixed** (0%) filter key follow for Obsession Atmosphere, Velvet PWM and Silver
Horizon. Each plays C–G power chords rising from C3/G3 through C6/G6. No
third is included, so the root/fifth voicing stays easy to hear.

The tracker columns **CC20** and **CC21** control Upper and Lower Filter Key
Follow. `127` enables full tracking for each chord; `0` holds cutoff fixed.
Edit these sequencer cells to change tracking. Both saved patch amounts start
at zero, so the full-on setting is supplied by the sequencer itself.

For comparison, both filters use an 800 Hz base cutoff, neutral filter
modulation, and shorter releases between chords. At full tracking the C-note
cutoffs rise 400 → 800 → 1600 → 3200 Hz; the G voice tracks its own pitch,
seven semitones higher. At 0% the cutoffs remain at 800 Hz, so high chords get
darker. Oscillators, chorus and delay come from the earlier pad presets.

Regenerate with `examples/jp8000_pads/build.sh keyfollow`. The generator runs
the actual mlacker sequencer into the audio controller and verifies that
CC20/CC21 set both layers to 0% and 100% before saving the project.
Mla 8000's single-layer equivalent uses CC20 alone.

## Sequencer-controlled 24 dB filter demo

```sh
./build/cmake/bin/mlacker examples/jp8000_filter24.mlaproj
```

Press **Shift+M**, then **Space**. The patterns play:

1. Atmosphere 24dB, then Atmosphere 12dB.
2. Velvet 24dB, then Velvet 12dB.
3. Silver 24dB, then Silver 12dB.

Each pair uses the same pad, notes, velocity, 800 Hz base cutoff and full key
follow. The rising C–G fifths make it easy to hear the steeper 24 dB/oct
response remove more harmonics above cutoff. Chorus/delay and amp settings
are the same within each pair.

The sequencer selects the sound on each chord row:

| Column | Target | Value |
| --- | --- | --- |
| CC20 | Upper filter key follow | 127, full |
| CC21 | Lower filter key follow | 127, full |
| CC22 | Upper filter slope | 127 for 24 dB/oct, 0 for 12 dB/oct |
| CC23 | Lower filter slope | 127 for 24 dB/oct, 0 for 12 dB/oct |

Regenerate with `examples/jp8000_pads/build.sh filter24`. The generator starts
from the opposite slope and verifies actual sequencer CC22/CC23 selection
on both layers for all six patterns. Saved slope values start at 12 dB/oct;
the first pattern selects 24 dB/oct through its sequencer columns.

## Obsession studies

Three original 138 BPM projects develop the earlier pads into short arrangements:

| Project | Sound and arrangement | Length, before tail |
| --- | --- | --- |
| `jp8000_obsession_horizon.mlaproj` | Obsession Atmosphere: slow opening, high fifths, return home | 56 seconds |
| `jp8000_obsession_pulse.mlaproj` | Velvet PWM eighth-note pulses, then Silver Horizon an octave higher | 28 seconds |
| `jp8000_obsession_glass.mlaproj` | Glass Tide alternating with Warm Fifths, widening into an upper register | 56 seconds |

From the repository root:

```sh
./build/cmake/bin/mlacker examples/jp8000_obsession_horizon.mlaproj
./build/cmake/bin/mlacker examples/jp8000_obsession_pulse.mlaproj
./build/cmake/bin/mlacker examples/jp8000_obsession_glass.mlaproj
```

Open one project at a time. Press **Shift+M**, then **Space** to play its four
patterns as a song. Space stops playback.

Analysis of the reference recording estimated **138 BPM** and identified a
wider, less bass-heavy passage around **3:20–4:00**. These studies take that
pad space and energy as guidance. They use original F–Db–Ab–Eb root/fifth
voicings, ending Db–Eb–F–F; they are not a transcription. The upload credits
both JP-8000 and JV-1080, and no exact hardware or recording match is claimed.

Every chord uses full Upper/Lower key follow through **CC20/CC21**. The first
two and final sections select **24 dB/oct** through **CC22/CC23**; the third
opens into the upper octave at **12 dB/oct**. **CC74** sweeps brightness,
**CC1** adds movement, and **CC7** shapes level. Cutoff envelopes and effects
come from the earlier pads. These are musical variations; use the separate
filter24 project above for a controlled slope comparison.

Regenerate the projects and optionally render original stereo WAV previews:

```sh
examples/jp8000_pads/build.sh obsession
JP8000_RENDER_DIR="$PWD/build/jp8000/obsession" examples/jp8000_pads/build.sh obsession
```

The generator verifies both layers' key follow, slope and brightness through
the actual sequencer/audio controller in every pattern. Optional WAVs are
`horizon.wav`, `pulse.wav` and `glass.wav` in the render directory, with a
four-second maximum tail. Their levels include mlacker's normal playback gain;
no separate preview normalization is applied. Reference audio is not included.

## Super Saw detune studies

Three more projects focus on oscillator detune, all at 138 BPM:

```sh
./build/cmake/bin/mlacker examples/jp8000_detune_steps.mlaproj
./build/cmake/bin/mlacker examples/jp8000_detune_sweep.mlaproj
./build/cmake/bin/mlacker examples/jp8000_detune_layers.mlaproj
```

Open one, press **Shift+M**, then **Space** to play all four patterns.

| Project | What to listen for |
| --- | --- |
| Steps | The same C4, then C4/G4 fifth at CC16 values 0, 32, 64 and 127 (approximately 0%, 25%, 50% and 100% detune). Single Upper, fixed 8000 Hz cutoff and 24 dB filter, no chorus or delay. |
| Sweep | A held C4/G4 fifth widens 0→127, then narrows 127→0. Next, detune stays at 96 while Super Saw Mix rises 0→127; the final section holds that wide detune and maximum mix. No chorus or delay. |
| Layers | Two Super Saw parts, panned apart, play F–Db–Ab–Eb fifths. Detune is first tight/wide (20/100), then wide/tight (100/20), then both widen 20→100 and return 100→20. A darker 24 dB filter, full saved key follow, slower envelopes and panning delay make a musical pad. Chorus is off. |

These use **OSC 1 Ctrl 1**, the Super Saw detune control, rather than Unison
Detune or Part Detune. Those additional pitch offsets and pitch LFOs are
neutralized. Both parts use Super Saw alone (OSC Balance = 0). The dry
comparisons keep settings and velocities fixed; natural level changes from
interference are retained rather than individually normalized. The detune
control has a nonlinear curve, so halfway on the knob is not halfway to the
maximum frequency spread.

| Sequencer column | Parameter |
| --- | --- |
| CC16 | Upper OSC 1 Ctrl 1: Super Saw detune |
| CC17 | Upper OSC 1 Ctrl 2: Super Saw mix |
| CC24 | Lower OSC 1 Ctrl 1: Super Saw detune |
| CC25 | Lower OSC 1 Ctrl 2: Super Saw mix |

For the other OSC 1 waveforms these same controls retain their waveform-specific
meaning. Detune and mix are saved at zero in these projects; their sequencer
columns supply the values on playback. Continuous examples use four automation
substeps per row, including while notes sustain. Rebuild the plugin for the
new Lower-part CC24/CC25 mappings, then regenerate if the checkout moves:

```sh
./build.sh --plugins
examples/jp8000_pads/build.sh detune
# Optional original WAV previews through the normal playback pipeline:
JP8000_RENDER_DIR="$PWD/build/jp8000/detune" examples/jp8000_pads/build.sh detune
```

The generator verifies both layers' detune and mix through the actual
sequencer/audio controller at the first and last rows of every pattern.
Optional previews are `detune_steps.wav`, `detune_sweep.wav` and
`detune_layers.wav`; Steps lasts about 28 seconds, Sweep and Layers about
56 seconds, before the release/effect tail.

## Trance lead: cutoff, resonance and envelope boost

The clearly named **Out Of The Blue style** project is:

```sh
./build/cmake/bin/mlacker examples/jp8000_out_of_the_blue_style.mlaproj
```

It uses an original 138 BPM trance melody, not the System F melody.
`jp8000_trance_lead.mlaproj` remains available with the same arrangement for
compatibility. Press **Shift+M**, then **Space** to play the four sections.

**Every pattern now opens and closes the filter.** Cutoff starts dark at row
0, reaches its peak at row 64 (about seven seconds), and closes by row 127.
CC74 has four sequencer substeps per row. The notes and velocities repeat
across sections, while resonance and envelope boost develop:

| Section | Cutoff CC74 | Resonance CC71 | Envelope boost CC19 |
| --- | --- | --- | --- |
| Blue opening | 8→80→8 | 20 | 68→76 |
| Blue wide sweep | 8→100→8 | 20 | 76→88 |
| Blue resonance | 8→110→8 | 20→82 | 88 |
| Blue return | 8→100→8 | 82→20 | 88→68 |

CC74 is the plugin's **Brightness**, an octave offset to filter cutoff. The
1000 Hz base cutoff therefore sweeps from about **89 Hz** to **2050, 9850 or
15200 Hz**, before the envelope. Key follow, filter LFOs and velocity-to-filter
are off. The per-note envelope has a **4 ms attack, 300 ms decay, 12% sustain
and 100 ms release**. CC19 controls positive peak boost, ranging from about
+0.57 to +3.09 octaves; this makes the opening brighter on each note.

The single Upper part uses Super Saw, CC16 detune 78, CC17 mix 102 and a
sequencer-selected 24 dB/oct low-pass filter (CC22 = 127). Chorus is off;
a dotted-eighth panning delay fills the gaps.

```sh
examples/jp8000_pads/build.sh lead
JP8000_RENDER_DIR="$PWD/build/jp8000/lead" examples/jp8000_pads/build.sh lead
```

The optional preview is `out_of_the_blue_style.wav` (about 56 seconds plus its
tail). Both project names are regenerated together. The generator checks
sequenced cutoff, resonance, envelope boost, detune, mix and slope at rows
0, 32, 64 and 96 of **every** pattern. Preview rendering also runs an audio
check: the open middle must have substantially more high-frequency energy,
relative to volume, than both closed ends in all four sections.

## Sub oscillator through a resonant 24 dB filter

```sh
./build/cmake/bin/mlacker examples/jp8000_sub_resonance.mlaproj
```

Press **Shift+M**, then **Space**. **OSC 2** supplies the sub: Triangle,
Range **-12 semitones**, Fine **0 cents**. It shares the Upper voice's
24 dB/oct low-pass filter with OSC 1 Super Saw. The original lead phrase is
played one octave lower; chorus and delay are off.

**All four sections open and close the filter**, including the first two
that previously held cutoff fixed. The same sweep in the first pair makes
the effect of the added sub easy to compare. The third now retains Super Saw
harmonics alongside the sub so its filter sweep stays clearly audible.

| Section | OSC Balance CC18 | Cutoff CC74 | Resonance CC71 | Envelope boost CC19 |
| --- | --- | --- | --- | --- |
| Saw sweep 24 | 0: OSC 1 alone | 8→100→8 | 76 | 72 |
| Sub sweep 24 | 40: saw/sub blend | 8→100→8 | 76 | 72 |
| Sub resonance 24 | 64: more sub | 8→110→8 | 76→94 | 72→80 |
| Sub return 24 | 40: saw/sub blend | 8→100→8 | 94→76 | 80→72 |

CC18 crossfades from OSC 1 (0) to OSC 2 (127). Cutoff opens from about 89 Hz
to 9850/15200 Hz at row 64, then closes. The per-note filter envelope adds
about +1.07 to +2.08 octaves at its peak, with the same timing as the lead.
CC22 = 127 selects 24 dB on every phrase; CC16 = 78 and CC17 = 102 retain
Super Saw detune and mix. Key follow and filter LFOs are off.

```sh
examples/jp8000_pads/build.sh sub
JP8000_RENDER_DIR="$PWD/build/jp8000/sub" examples/jp8000_pads/build.sh sub
```

The optional preview is `sub_resonance.wav`. The generator verifies OSC 2's
waveform and tuning, then checks sequenced balance, cutoff, resonance,
envelope depth, detune, mix and slope at four points per section. Preview
rendering checks the actual audio for opening and closing in every section.
Saved balance starts at OSC 1 alone; the sequencer selects when the sub enters.
