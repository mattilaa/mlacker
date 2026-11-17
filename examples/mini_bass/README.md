# Mla Mini bass example

An original F-minor bass riff for [Mla Mini](../../plugins/mla_mini), at
112 BPM. Only the new synth is needed; all sounds are synthesized.

From the repository root:

```sh
examples/mini_bass/build.sh
build/cmake/bin/mlacker examples/mini_bass.mlaproj
```

The script builds the plugin, generates the project with embedded parameter
settings, and renders `build/mini_bass/preview.wav`. It uses the existing
MLang compiler and fetched SDK from the normal mlacker build. No installation
is needed. Plugin paths in the project point into `build/plugins/`, so open
the checked-in project from the repository root. Set `NO_OPEN=0` to open it
automatically after generation.

The song order is **Classic bass → Classic bass → Ladder sweep → Legato
glide**, about 34 seconds. Press Space to play the selected pattern, or open
the song matrix with M to play the arrangement.

- **Classic bass:** detached notes around F-2, C-2 and their upper octaves,
  using the startup bass patch and short, plucky filter articulation.
- **Ladder sweep:** the same riff with four cutoff automation points per
  row (CC74) and stronger resonance (CC71). Watch these columns while playing.
- **Legato glide:** notes overlap by half a row, preserving the envelopes
  and sliding between pitches. A second Mini uses oscillator 3 as a fixed
  LFO; CC1 adds a little pitch and filter movement.

The preview contains the first four-bar pattern, rendered dry by mlacker's
offline bounce. The reference recording is not included, and the score is
an original demonstration rather than a transcription of the recording.
