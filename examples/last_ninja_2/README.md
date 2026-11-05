# Last Ninja 2 — Central Park, reference performance

```sh
build/cmake/bin/mlacker examples/last_ninja_2_loading.mlaproj
```

Escape dismisses the credits. **Shift+M**, then **Space**, plays the whole song
from the matrix. Space in the pattern editor loops the selected pattern.
Requires Mla SID built from this branch with register-frame
protocol 1 (`./build.sh --plugins --install`). The earlier project folder name
is retained for existing launch commands.

The source of truth is the supplied
[Central Park in-game recording](https://www.youtube.com/watch?v=CoGFV_xxR64).
The project follows the matching original C64 player, **HVSC subtune 2**, by
**Matt Gray**, released by **System 3 in 1988**. The separate loading tune and
Chris Holm MIDI arrangement are not used to determine this version's notes,
section order, timing or mix.

## Sequence, sound and mix

The original player is traced offline into full eight-bit SID register frames.
These play through **one Mla SID chip**, keeping all three voices coupled:
their oscillator phases, ring modulation, pulse-width movement, glide ramps,
envelopes, waveform changes and shared volume register remain together.
The original voice programs set their relative levels; there are no extra
quiet lead presets, doubled MIDI layers or independently scaled drum patches.
The instrument's overall output is +4 dB with headroom in the checked render.

The snare uses the player's rapid alternation of noise and pulse with changing
frequency and envelope, rather than a generic noise-only hit or a single noise
attack followed by a fixed pulse tail. The actual lead, bass, arpeggio and
percussion programs change roles as the original score plays.

The score runs for **5:40.85**, including a short silent ending. Playback begins
with the original player, without the video's approximately 0.8-second lead-in.
Its original volume fade reaches zero at approximately **5:37.34**. The first bass entry is around **0:03.85**.

The sequencer's **188 BPM is a frame timing grid**, not a musical tempo claim.
Four original PAL frames occupy each row; packets schedule the individual
register frames inside it. The grid is about 0.018% faster than exact PAL,
amounting to roughly 0.06 seconds over the tune. The original driver’s writes
inside a frame are represented by its register state plus gate-reset markers;
CPU-cycle offsets within that frame are not preserved. Mla SID's own chip
model remains an approximation of the hardware. For comparison with the
recording, start at matrix row 1: seeking reconstructs registers, but cannot
reconstruct the previous oscillator phases and envelope history.

The matrix has **89 rows and four parallel lanes**: SID voices 1–3 and the
shared volume/filter registers. Each lane has its own 48-row pattern, giving
356 patterns in total. Names identify the voice, its role in that section and
the start time, for example `V1 Bass 00:03`, `V3 Percussion 00:42` and
`Chip mix 00:03`. All four lanes feed the same coupled chip. There is no audio
backing track, runtime 6502 emulator or external song dependency. The project
is self-contained register data; musical note editing remains available in
the separate MIDI adaptation.

Musical voices sit **40% left/right** (voices 1 and 3 left, voice 2 right).
Complete percussion gestures are centred, including their alternating noise
and pulse tails and falling-pitch kicks. Placement changes smoothly as a voice
changes role. The shared filtered signal stays centred. Averaging left and
right preserves the original mono mix; oscillator coupling and source levels
are unchanged. Pass `--width 0` to generate the mono version.

Each voice lane alternates its own pair of silent stop guards with a short
overlap. Releasing a lane's guards clears its gate and queued writes; releasing
all guards silences the chip. The shared lane carries the original volume
fade. A single voice pattern can also be auditioned in the pattern editor.
Normal keyboard notes above the six reserved guards leave the register player
and restore the instrument patch. The 93 saved parameter IDs
and existing keyboard/CC behavior remain compatible.

## Credits and rights

The notice appears whenever the project opens and survives Save and Save As.
It credits Matt Gray, the original System 3 release and the reference video.
Music rights remain with their respective owners. The music is not covered by
mlacker's software license; attribution grants no redistribution, publication
or commercial-use rights.

The reference SID is obtained separately from
[HVSC](https://hvsc.de/download/C64Music/MUSICIANS/G/Gray_Matt/Last_Ninja_2.sid).
Its [STIL entry](https://hvsc.de/download/C64Music/DOCUMENTS/STIL.txt) names
subtune 2 as Central Park (in-game).

## Reproduce and check

```sh
python3 -m venv build/last_ninja_2/venv
build/last_ninja_2/venv/bin/pip install py65==1.2.0
build/last_ninja_2/venv/bin/python examples/last_ninja_2/generate.py /path/to/Last_Ninja_2.sid
python3 tests/last_ninja_2_tui_smoke.py build/cmake/bin/mlacker ~/.local/plugins/VST3/MlaSid.vst3
```

The source SID SHA-256 is
`7686b46aada0a107dd9717b47e76209acca9e47b92a6ea10657fcbd63f24588a`.
The decoded register/gate-reset program SHA-256 is
`cc924dc044c213ea41a210d967faaf6294f1f26b0913beedcca950d63890d409`,
checked against an independent trace of the original player, without video
lead-in padding. `--plugin` changes the saved bundle path; `--output` changes
the project destination.

The plugin tests cover silent 6581 pattern starts/restarts, timed frames,
complete register bytes, queued-write cancellation on Stop, restart, malformed
packets, stereo placement and mono compatibility, alongside the existing
instrument tests. The project test checks the full program, source landmarks,
parallel matrix alignment, centred percussion, responsive playback, pattern
transitions and saved credits. No changes to
`subprojects/mlang` are required.

## Separate MIDI adaptation

The earlier editable MIDI version is preserved at
`examples/last_ninja_2_midi.mlaproj`, with its generator at
`examples/last_ninja_2/generate_midi.py`. It is a comparison arrangement and is
not the reference performance. See [its notes](MIDI-arrangement.md).
