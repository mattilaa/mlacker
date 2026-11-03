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
