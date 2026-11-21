# Smalltown Boy — Mini bass

A separate four-bar bass sequence for **Bronski Beat — Smalltown Boy**, estimated
from the user-supplied `smalltownboy.m4a` excerpt. It uses one monophonic Mla Mini
instance. The reference audio is not embedded or required for playback.

```sh
# From the repository root:
examples/smalltownboy/build.sh
build/cmake/bin/mlacker examples/smalltownboy.mlaproj
```

Press **M**, then **Space**, for the full C → B♭ → F → E♭ progression.
Space in the pattern editor plays the selected bar. Each of the four patterns
contains one track and eight detached eighth notes, alternating low → high:
C-2/C-3, A#1/A#2, F-2/F-3 and D#2/D#3, respectively. The same Mini instance carries its oscillator phases
between bars.

The attack spacing measures approximately **126.47 BPM**. The project uses
integer **126 BPM**, with small note offsets to follow the excerpt over the
four-bar phrase. The first attack is about 30 ms after the start of the clip.
Gates, velocities and exact octave/oscillator choices are estimates from a
mixed recording. Other instruments in the excerpt are not recreated.

## Estimated preset

The octave movement is in the played eighth notes. The three oscillators feed the
ladder filter and Mini’s built-in stereo chorus. Each eighth note
retriggers both envelopes, with a falling filter sweep and a softer amplitude
decay and release. These are approximation settings from the mixed excerpt,
not recovered original knob positions or a claim about which hardware made
the recording.

| Control | Setting |
| --- | --- |
| OSC 1 | Saw, played pitch, level 0.85 |
| OSC 2 | Triangle/saw, +1 octave, −7 cents, level 0.55 |
| OSC 3 | Triangle, −1 octave, +3 cents, level 0.26 |
| Mixer Noise | 0 |
| Cutoff / Resonance | 385 Hz / 0.11 |
| Filter envelope depth / Key Follow | +4 octaves / 61% |
| Filter A / D / S / R | 2 ms / 205 ms / 8% / 292 ms |
| Amp A / D / S / R | 4 ms / 550 ms / 50% / 298 ms |
| Drive / Feedback | 5.9× / 32% |
| OSC Drive / Analog Output | 3.2× / 84% |
| OSC Drift | ±0.8 cents |
| Glide / Legato | Off / Off (each note retriggers) |
| Velocity → Amp / Filter | Both off |
| Background Hiss / Hiss Level | 2 of 10 / −54 dBFS at full amount |
| Chorus | 85% |
| Output | +3 dB |

The triangle supplies a low fundamental beneath both octaves of the phrase.
**OSC Drive** shapes the oscillators before they hit the ladder. The filter
opens quickly on each attack, then falls back toward 385 Hz. **Analog Output**
adds asymmetric saturation after the amp envelope and before the chorus.

All four bars share one stereo **Mla EQ** in the bass track’s first insert.
A **+4 dB low shelf at 95 Hz** supports the sub, a **+2 dB bell at 145 Hz**
supports the upper octave, and a **+4.5 dB high shelf at 2.9 kHz** brings out
the edge of each note. The EQ output adds 1.6 dB of makeup gain.
In Pattern view press **f**, select the Mla EQ slot
and press **Enter** to adjust the bands or toggle **Bypass**.

Envelope decay/release times are measured to a 60 dB fall, as in the plugin.
The project embeds every parameter, so loading it restores the preset without
changing the plugin's startup defaults or the earlier Mini bass example.

The generator writes **`build/smalltownboy/preview.wav`** through mlacker's
sequencer and VST3 host. It includes the four-bar bass phrase and a short tail.
It requires the existing MLang compiler, fetched VST3 SDK and configured plugin
build from the normal mlacker build.
