# Mla Mini bass example

A synthesis reconstruction of the bass and short upper phrases in `moog.wav` for
[Mla Mini](../../plugins/mla_mini), at approximately **121 BPM**. Only the
new synth is needed; all sounds are synthesized.

From the repository root:

```sh
examples/mini_bass/build.sh
build/cmake/bin/mlacker examples/mini_bass.mlaproj
```

The script builds the plugin, generates the project with embedded parameter
settings, and renders `build/mini_bass/sixteenths.wav` (the free-oscillator
audition), `build/mini_bass/preview.wav` (the reference arrangement), and
`build/mini_bass/baseline.wav` (the previous bright tone with Analog Output,
OSC Drift and Background Hiss disabled). `clean_sixteenths.wav` offers the same comparison
for the fast bass audition. It also writes
`bass.wav` and `upper_sixteenths.wav` as separate solo renders of the two
parallel parts. It uses the existing
MLang compiler and fetched SDK from the normal mlacker build. No installation
is needed. Plugin paths in the project point into `build/plugins/`, so open
the checked-in project from the repository root. Set `NO_OPEN=0` to open it
automatically after generation.

The project opens on **Free OSC 16ths**. Press **Space** to play it: the
first bar repeats F-2 on all sixteen rows; the second has octave and pitch
changes, again with a note on every row. The 60% gates retrigger the
envelopes while the oscillator phases keep moving through the gaps.
The separate `sixteenths.wav` repeats this two-bar pattern four times, so
you can hear the detuned oscillators beating over a longer stretch.

Open the song matrix with **M**, then press **Space** for the reference
arrangement: **eight bars of F octave pulses, one bar of C♯ pulses, and
one bar of D♯ pulses**, about 20 seconds, including the short upper
sixteenth-note phrases starting around four seconds and recurring every
two bars. The two matrix lanes contain independent patterns and channels:

| Matrix lane | Patterns | Instrument channel |
| --- | --- | --- |
| 1 | Bass - F / Bass - ending | Mini bass, instance 1 |
| 2 | Upper - pickup / 16ths A / 16ths B / ending | Mini upper 16ths, instance 3 |

Each pattern has one track with one note column, and each instance is
monophonic. The two parts sound together only when the parallel lanes play.
Select either pattern to hear or edit that part alone. The upper part's last
sixteenth in the preceding pattern supplies each pickup.
The main oscillator alternates F-2/F-3 (about 87/175 Hz), then C#2/C#3 and
D#2/D#3; oscillator 3 supplies the sub-octave.

Timing and pitch were estimated from short-window spectra and attack
spacing in the recording. Bass notes fall every eighth note, starting about
112 ms into the cropped reference. The saved score uses a 0.90-row offset
and short gates, with release tails continuing toward the next pulse.
The fast phrases and their octave placements are estimates from the mixed
recording, as are gate lengths and velocities. Drums are not included.

The brighter startup preset has **600 Hz Cutoff**, **3 octaves Filter Env
Depth**, **35% Key Follow**, **480 ms Amp Release**, **450 ms Filter
Release**, and **OSC Drive 1.4**, with **30% Analog Output** and **1.2-cent
OSC Drift**. The output coloration adds mild roughness after the amp
envelope; the oscillators wander independently without restarting phase
on each note. The envelope can open the filter to about 4.8 kHz before
keyboard tracking. The baseline previews use the same bright filter,
notes, releases and oscillator drive, with output coloration, drift and
background hiss disabled. The separate upper instance uses a 1 kHz cutoff
and a two-octave envelope.

The bass instance has **Background Hiss 6** with **Hiss Level −54 dBFS**,
giving a more audible noise floor during rests. In the VST parameters,
**Background Hiss** now has eleven settings: **0 = Off**, then **1–10**
for increasing amounts. **Hiss Level** provides fine adjustment. The default
is about 20 dB louder than the initial very quiet hiss setting.
Hiss is off on the other two instances to avoid stacking background noise.
The existing **Noise** mixer knob remains independent. The noisy previews
include a short hiss-only tail after the last note; the render tail limit
keeps these exports finite.

The original demonstration patterns remain available separately:

- **Classic bass:** detached notes around F-2, C-2 and their upper octaves,
  using the startup bass patch and short, plucky filter articulation.
- **Ladder sweep:** the same riff with four cutoff automation points per
  row (CC74) and stronger resonance (CC71). Watch these columns while playing.
- **Legato glide:** notes overlap by half a row, preserving the envelopes
  and sliding between pitches. A second Mini uses oscillator 3 as a fixed
  LFO; CC1 adds a little pitch and filter movement.

The reference previews contain the full ten-bar parallel arrangement, rendered dry by
mlacker's offline bounce. The reference recording is not included.
