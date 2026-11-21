# Mla Mini bass example

A synthesis reconstruction of the bass and short upper phrases in the supplied excerpt of Tobias Bernstrup’s *27 (Laser Mix)* for
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
`build/mini_bass/baseline.wav` (the current patch with Analog Output,
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

The bass pairs a saw at the played pitch with a triangle/saw blend and a
triangle one octave below. Their slight detuning changes the low harmonics
across repeated notes. The filter starts at **480 Hz**, opens by up to **4.5
octaves**, and closes quickly after key-off (**180 ms filter release**).
The **550 ms amp release** lets the low end continue as the brighter edge
fades. Oscillator drive is **2.1×**. The low eighth notes have shorter gates
(about 69 ms) and stronger accents than the upper answers (about 86 ms).

The separate upper voice doubles the pickup an octave up. Both sixteenth
phrases answer F with G♯ and A♯, then land on C or E♭. The first pickup is
about four seconds into the preview.

The demo uses Mini’s built-in **Chorus** at **20% on the bass** and **50% on
the upper sixteenths**. In the mlacker parameter editor the corresponding
values are 0.20 and 0.50. Compare `preview.wav` with `chorus_off.wav` in
`build/mini_bass/` to hear the same arrangement with and without the effect.

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
