# Last Ninja 2 — Central Park, Mla SID MIDI adaptation

```sh
build/cmake/bin/mlacker examples/last_ninja_2_loading.mlaproj
```

Escape dismisses the credits. Press **Shift+M**, then **Space** to play the
whole song from the song matrix. Space in the pattern editor plays the selected
two-bar pattern. Mla SID must be installed (`./build.sh --plugins --install`).
The project retains its earlier folder name for existing launch commands.

This version uses the supplied **level-1.mid**, a 124 BPM Yamaha XG arrangement
of Central Park credited to **Chris Holm**, with original music by **Matt Gray**
and the MIDI copyright credit **1988 System 3**. The XG instrument and effect
settings are adapted to SID synthesizer patches. The supplied
[Central Park in-game recording](https://www.youtube.com/watch?v=CoGFV_xxR64)
guides the added intro and the bass/snare timbres. The MIDI's melodic pitches
and arrangement are retained, so its key, phrasing and duration still differ
from that recording; this is not cycle-exact replay of the C64 player.

The project preserves all **7,637 note starts and velocities**, the original
melodic pitches and song structure. The MIDI's eight silent setup beats now
contain a synthesized SID intro. Note starts and ends are
rounded to the sequencer's 0.01-row precision (at most about 0.6 ms per endpoint).
Equal-pitch bass retriggers with overlapping gates release the old note at the
new note's start. The resulting song lasts **4:38.71**, with **72 two-bar
patterns** of 32 rows. Small patterns keep scheduling and editing responsive;
sustained notes carry across matrix rows.

Everything is synthesized by Mla SID during playback: no recorded backing
track, original SID binary or external MIDI player is required. The native
`Project.mlack` contains eleven parts: wide PWM bass, Kanoon pulse, left/right
pulse basses, a PWM bass layer, Converge, Ana PWM pad, SID kick, snare and shaker,
and the SID intro arpeggio.
The source percussion triggers (35, 38 and 82) drive synthesized percussion at
their original times; their pitches and envelopes are adapted for the SID.

## Pulse-width modulation

Seven parts use Mla SID's **PW Sweep** player modulation, stepped at the PAL frame
rate, without streaming thousands of per-frame CC events through the editor.
All start from pulse width 2048. Each chip's voices have staggered sweep phases.

| Part | PW Sweep | Rate |
| --- | ---: | ---: |
| Wide PWM bass | 94% | 0.245 Hz |
| Kanoon pulse | 70% | 0.60 Hz |
| Pulse bass L | 95% | 0.37 Hz |
| Pulse bass R | 95% | 0.43 Hz |
| PWM bass layer | 94% | 0.16 Hz |
| Converge | 85% | 0.45 Hz |
| Ana PWM pad | 75% | 0.35 Hz |

The main bass sweeps approximately 124–3972 on the SID's 0–4095 pulse-width
range; the left/right basses reach approximately 103–3993. The original saw
patch is replaced with a pulse wave so width modulation affects its sound.
All four bass layers bypass the filter to retain the moving pulse harmonics.
The patches are editable in the Mla SID parameter editor. Hard Restart is
enabled for reliable note attacks, and the kick uses a short pitch-bend drop.

## Intro and snare

The intro holds F4, F5, F6 and F7, tuned 35 cents sharp to match the reference's
octave bands. One triangle oscillator arpeggiates at one step per PAL frame,
with a new envelope attack on each beat. The built-in player's ascending order
approximates the recording's four-octave effect; it does not reproduce the
original descending register sequence. Its 32 chord-input notes are additional
to the MIDI's original notes.

Each snare hit starts with a brief high-pitched noise strike, then switches to
a narrow pulse at G2 after one 1/64 step (about 30 ms). Overlapping notes in
Unison mode retune the same oscillator without restarting its envelope. Pulse
width rises from roughly 240 to 480 during the tail, and a short release cuts
the hit cleanly. This adds one tonal-body note per source snare hit. Voice
linking is disabled for percussion so unused oscillators stay silent.

## Credits and rights

The notice appears whenever the project opens and survives Save and Save As.
It credits Matt Gray, the original System 3 release and Chris Holm's MIDI
arrangement. Music rights remain with their respective owners. The music is
not covered by mlacker's software license; attribution grants no redistribution,
publication or commercial-use rights.

The supplied MIDI, rather than the earlier HVSC transcription, is the source
for this version. The separately suggested
[Commodore 64 SID Tribute MIDI announcement](https://www.facebook.com/commodore64sidtribute/posts/the-last-ninja-2-soundtrack-is-now-available-in-midi-format-on-my-discord-channe/935092915106044/)
is reserved for a subsequent comparison; those MIDI files are not included here.

## Reproduce and check

No Python dependencies are needed. The source MIDI is not bundled.

```sh
python3 examples/last_ninja_2/generate.py /path/to/level-1.mid
python3 tests/last_ninja_2_tui_smoke.py build/cmake/bin/mlacker ~/.local/plugins/VST3/MlaSid.vst3
```

The generator checks source SHA-256
`45ec658a2820fd2cbc447cd3b8b4a543acdd5de459dda29f57e81d950d1d212a`.
`--plugin /path/to/MlaSid.vst3` overrides the installation path saved in the
project; `--output /path/to/Project.mlack` changes the destination.

The integration check starts the dense bass/percussion section, opens a menu
during playback, crosses a pattern boundary, and checks prompt start/stop
responses. It also verifies credits on reopening and that saving preserves all
score text. CTest runs it when the built Mla SID bundle is available. No changes
to `subprojects/mlang` are required.
