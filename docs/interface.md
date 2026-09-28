# mlacker interface reference

Detailed behaviour of the tracker's views, editing keys, transport and audio
handling. It was written while the tracker was the demo application of MLang's
`tui` widget library, whose reusable widgets are documented in
[MLang's `modules/tui/README.md`](https://github.com/mattilaa/mlang/blob/main/modules/tui/README.md).
Some statements describe earlier stages of the tracker and may be out of date.

## Audio import

Choose **Add → Audio** to load a sample. The file chooser
lists `.wav`, `.aif`, and `.aiff` files case-insensitively and enforces the same
filter on manually entered paths. Directories remain browsable. The current
stdlib decoder supports mono/stereo **16-bit PCM** WAV/AIFF (not every encoding
these containers can hold); decode errors preserve the existing clip.

Loaded samples belong to a session-wide **View → Audio** list in the left pane.
The list shows sample numbers and filenames; j/k, gg, and G navigate without
changing the active pattern or its cursor. Enter inserts the selected sample
into the selected AUDIO track at the Pattern cursor row. Without an audio track
selected, importing only adds the sample to the Audio list and opens that view;
it does not create a track or put audio on a MIDI track.

Importing with an AUDIO track selected also inserts an instance at the selected
row. A track can contain multiple independent, non-overlapping instances.
Overlapping insertions are rejected, while the loaded sample remains available.
With the Pattern pane focused, Backspace over audio removes just that instance,
not its loaded sample, other instances, automation, or pattern rows. Samples can
be inserted again even if the original file is no longer available. Clear track
removes all its instances; deleting a pattern/track does not unload samples.

On an AUDIO track with an instance under the Pattern cursor, `c`, `y`, and `p`
cut, copy, and paste audio at row granularity. `c` **cuts**: the tail of the
instance — from the cursor row to its end — moves to a separate audio clipboard
and the head is truncated to end on the row above (its LEN cell is rewritten);
cutting on an instance's first row removes the whole instance. `y` **copies**
(yanks) that tail into the clipboard without changing the pattern. `p` **pastes**
the clipboard instance at the cursor row, subject to the usual non-overlap rule.
These keys defer to the vim-style row yank/paste elsewhere: `c`/`y` act only when
audio is under the cursor, and `p` pastes audio only on an AUDIO track with a
non-empty audio clipboard, otherwise pasting rows. The audio clipboard is one
clip carried per pattern and is duplicated when a pattern/track is cloned.

Visual selection also drives whole-clip edits. Enter visual mode (`v` or `V`) on
an AUDIO track, extend the row range, then `y`/`d` copy/cut the **first whole
instance** intersecting the selection into the audio clipboard (unlike the
row-granular `c`/`y`, these operate on the entire clip). **Ctrl+J/Ctrl+K** move
the instance under the cursor down/up one row, with pattern-bounds and
non-overlap checks; the cursor follows it. Transpose (`J`/`K`) on an audio LEN
cell that is not an instance's start row is a silent no-op instead of erroring.
The **Audio** menu exposes Copy/Cut/Paste clip, Move clip up/down, Trim to
selection, Slice selection, and Reverse clip (which flips the instance's samples
back-to-front in place). Reverse rebuilds fresh sample/peak data, so it never
mutates other clips sharing the source.

Trim and Slice are **non-destructive**: they never rebuild or copy the sample
buffer. Instead each instance carries a start `offset` (frames into the shared
full clip) plus its LEN, so the decoded audio is untouched and stays shared.
**Trim to selection** (key `t` while the Sample view is open) shrinks the
instance under the cursor to the marked region: its start offset advances to the
region start and its LEN becomes the region length. Trimming again is relative to
the current window, so offsets accumulate. **Slice selection** (key `s` on an
audio track while a Pattern visual row selection is active, or the menu) splits
the covered instance into up to three instances — head, the selected middle, and
tail — that all share one full sample buffer with independent offsets, lengths,
and start rows.

Each instance references decoded samples and carries its own start row.
A read-only waveform column
beside the LEN column draws time downward, with green left-channel bars extending left
from the center line and red right-channel bars extending right. Mono is shown
on both sides. Waveforms scroll with the table, while ROW stays frozen.

Waveforms use eighth-cell edges, a solid mean-absolute-amplitude body, and a
shaded peak envelope. With the Pattern pane focused, press `z` on an audio track
to toggle its waveform between 13 and 25 cells wide (6 or 12 cells per channel).
Zoom is stored per track and preserved by pattern/track cloning. This expands
amplitude detail horizontally, not time: rows and notes remain aligned. Narrow
viewports clip the waveform safely; menus and text editors capture `z` normally.

**View → Sample view** selects **Normal** or **Grainy**, and its **Type** submenu
selects **Filled blocks** or **Wave (osc)**. These settings affect both the Pattern
waveform and the horizontal sample viewer. Grainy Pattern waveforms pack four
successive audio slices into each terminal row using Braille's 2×4 dot grid,
rather than rendering a single row peak with a dotted texture.
Grainy is the default for both sample waveforms and Mixer meters.

Press `s` while an audio clip is under the Pattern cursor to toggle the horizontal
**Sample** view in the lower pane. It replaces (and remembers) the Mixer/Inspector;
moving off the clip closes it. With either the Pattern or Sample pane focused,
**Ctrl+H/L** zooms time out/in by factors of two, anchored around the cursor row.
Zoom ranges from the full clip to one original sample per horizontal pixel (two
pixels per cell in Grainy mode). The display shows its time range and frames per
pixel, with a highlighted cursor column. Stereo uses green L and red R lanes;
mono uses one lane. Filled mode extends to the zero line; Wave draws the signed
sample trace/envelope, revealing individual oscillations at sufficient zoom.
These controls require modifier-aware terminal input; raw Backspace is not zoom.
Menus and editors retain exclusive keyboard ownership, including `s` and Ctrl+H/L.

Decoded samples are shared read-only by pattern/track copies, so zooming neither
reopens the source file nor changes clip timing, notes, or automation.

While the Sample view is open you can mark a sample-accurate region of the clip.
**Ctrl+N/Ctrl+M** move the region's **start** point left/right and
**Shift+N/Shift+M** move its **end** point, each by one visible column (so the
step tracks the current zoom). The selected region is drawn with a **darker
background** than the rest of the clip; a full-clip selection shows no shading.
**Ctrl+Y** lifts the selected region into the audio clipboard as a fresh clip
(new peaks/samples, source untouched), so it can be pasted (`p`) at any row to
rearrange grooves. The selection resets to the whole clip whenever the Sample
view is opened. While a start/end point is being adjusted the view follows that
edge (scrolling so the moving edge stays centred); moving the Pattern cursor to
another row releases the follow and restores the row-anchored view. The Sample
panel title shows the clip's file name (not its full path).

One row currently represents a sixteenth note at the displayed BPM (120 by
default). Longer clips extend the pattern with empty MIDI/automation cells;
Deleting instances does not discard existing rows. Instance ends are limited to
16384 rows. Pattern/track cloning preserves all instance offsets and sample data.
Audio playback, clip trimming, and tempo-change resampling
are not implemented yet; row editing does not trim the source audio.

## Transport and tempo

mlacker's bottom status bar shows BPM (initially 120), sequence elapsed time
(`MM:SS.mmm`), time signature (4/4), PLAY/STOP, and a MIDI-input light. At the
right end, `CPU n%` shows the CPU time of all mlacker threads (UI, audio and
render workers) as a share of every online core. It updates every 300 ms and
turns the accent colour at 80%.
**Ctrl+B** opens the tempo dialog (20–400 integer BPM; Enter saves, Escape cancels,
Tab switches between the field and OK/Cancel). CSI-u Ctrl+B and legacy Ctrl+B
are supported. The input light stays idle until a MIDI-input processor is added.

**Space** starts/stops the current pattern from the selected row and loops at
its end. A monotonic clock with fractional-row phase drives sixteenth-note rows,
the selected-row highlight, automatic scrolling, and sequence time. An open
Sample view follows within the row too, including at single-sample zoom. BPM
changes preserve row phase; menus and the BPM dialog do not pause transport.
Switching patterns or opening a content editor/file/action dialog stops it so
edits cannot accidentally target a moving row. Modal controls capture Space and
Ctrl+B before the transport handles them. Animated repainting uses the configured
meter update rate (60 FPS by default), independently of the sequencer clock.

This is a pattern transport and meter/event preview, not audio-device playback
or MIDI output. Song-order playback is not connected. Imported audio envelopes
remain on their import-time row grid; BPM changes do not yet resample/re-grid them.

## Patterns and Song lists

The left pane defaults to **Patterns**, listing `<pattern_nr> <pattern_name>`.
mlacker starts with `001 Intro`, `002 Verse`, and `003 Chorus`. Selection
immediately loads that pattern into the **Pattern** view and Mixer. Each pattern
has independent rows, tracks, automation settings, mixer state, and cursor/scroll
position; edits are retained when switching away and back.

The **Pattern** menu implements Add pattern, Rename pattern, Remove pattern,
and Clone pattern. Add creates a blank 64-row pattern with three MIDI tracks;
Clone makes an independent copy of the current pattern. Both select the new
pattern and append it to Song order. Rename uses a text dialog with OK and
Cancel buttons (Tab moves to them; Enter and Escape work from the text field). Pattern IDs remain stable after deletion. Remove deletes the
pattern and all its Song occurrences. mlacker retains at least one pattern and
limits the library to 128 patterns. Changes are in memory; there is no undo or
session persistence yet.

View → Show patterns / Show song switches the left pane. Song lists
`<order_position> <pattern_nr> <pattern_name>` in playback order, initially
Intro → Verse → Verse → Chorus. Repeated occurrences reference the same pattern;
renaming or editing it updates all occurrences. In Song view, `dd` removes only
the selected occurrence, not the pattern. An empty Song is allowed and keeps
the current Pattern view available. Song is an order-list preview, not playback.

With Song focused, Ctrl+J/K moves the selected occurrence down/up and selection
follows it. `o` inserts a duplicate above, `O` below, selecting the inserted row.
Shift+J/K changes only that occurrence to the previous/next available pattern,
stopping at either end of the pattern list. Duplicates reference the same pattern;
these shortcuts do nothing in an empty Song. Ctrl+J/K requires modifier-aware
terminal reporting (CSI-u or modifyOtherKeys); legacy LF remains Enter.

## MIDI input, AUHAL output and Settings

mlacker opens the default MIDI input on a dedicated worker thread at startup.
`mlacker_ui::midi_controller` decodes note-on/off (including velocity-zero note-on)
and running status, then posts fixed-size `MidiInputEvent` values through a
1024-entry `std::sync::SpscQueue`. The main loop drains at most 256 events per
iteration, independently of modal keyboard focus. `MidiController.dispatch`
uses `match` to call main-thread note handlers; the current handlers retain the
last note/channel/velocity and flash MIDI IN for 150 ms. This does not record
notes into the pattern or send MIDI output. No input device is a nonfatal state;
connect the device before startup, or reopen File → Settings to refresh devices.

The worker-to-window queue never waits when full: dropped events are counted
atomically, and the consumer clears its last-note display state on loss. Native
input queue drops are counted too. MIDI polling/packet allocation happens on
the worker, not on a real-time audio callback. Shutdown signals and joins the
worker before releasing shared storage. Hardware-independent decoder, threaded
handoff, overflow, and indicator tests live in `tests/midi_tests.mla`.

File → Settings selects the MIDI input adapter and AUHAL master output device.
Both offer System default and Disabled. Enter opens a dropdown, j/k or arrows
browse, Enter commits its choice, and Escape cancels the popup. A second Escape
cancels Settings. Tab or Ctrl+Shift+H/J/K/L moves between fields and the centered
OK/Cancel buttons. Only OK applies choices. Device choices last for this run;
no settings file or automatic hot-plug reconnect is implemented yet. Output
open/start errors keep Settings open and retain the previous output.

mlacker's `AudioSystem` owns `std::audio::controller::AudioController`, an
output-only macOS AUHAL stream requesting 128 frames at the native device rate.
The hardware can reject that buffer request; Settings reports the actual rate
and buffer size after applying. Live MIDI goes directly from the MIDI worker
to a dedicated audio queue, independently of UI dispatch. Sequenced MIDI uses
a separate main-thread queue. The native callback uses a small sine-voice
preview synth by default; no microphone input is opened.
The controller also supports copied PCM sample-play/stop events, but pattern
audio-instance scheduling is not yet connected to this output. Existing
sequencer events are forwarded as they become due in the UI loop; a future
look-ahead sequencer can use absolute frame timestamps supported by the API.
Set `MLANG_TUI_NO_HARDWARE=1` to skip startup device opening, as the PTY tests do.
Set `MLACKER_CPU_METER=0` to turn off the status-bar CPU meter. The PTY tests do
this so that the only repaints are the ones their keystrokes cause.

Add → VST3 master plugin opens the bundle chooser; Add → Unload master VST3
restores the preview synth.

## Pattern table

### Column stages

MIDI tracks start collapsed to NOTE/VEL for every note line. In the Pattern view,
`z` cycles the selected MIDI track through three column stages:

1. NOTE / VEL (default)
2. NOTE / VEL / LEN / OFF
3. All columns, including the CC columns

The next press returns to stage 1. Shift+Z advances every MIDI track's stage
independently. Track → Collapse all selects stage 1; Expand all selects stage 3.
Hidden columns consume no width and are skipped by h/l navigation, but their
values and playback remain unchanged. Collapsing keeps selection in the same
track, returning a hidden LEN/OFF selection to its note line's NOTE column.
Track/pattern copies preserve their stages. Audio retains its existing `z`
waveform-width toggle; MIDI column stages do not change audio tracks.

### Note length and offset

LEN uses decimal sixteenth-note units: `1.00` = one row, `0.50` = half a row,
`3.75` = three and three-quarter rows. Enter edits LEN; Shift+J/K adjusts it by
0.01. MIDI accepts 0.01–16384.00, with new notes defaulting to 1.00. A muted-color
duration rail beside NOTE follows sustained notes using four vertical Braille dots
per terminal cell for partial endings. Its background matches NOTE, including
selection and alternating-row shading. Empty NOTE cells do not cut off a sustain.

Each MIDI note line is NOTE / VEL / LEN / OFF. OFF uses the same scale as LEN,
but is signed: `-0.50` starts half a sixteenth before its row, `+0.50` starts
half a sixteenth after it. New notes default to OFF `0.00`; empty OFF also means
zero. Enter edits it, Shift+J/K adjusts by 0.01, and the accepted range is
−16384.00 through +16384.00. Note-off is always **row time + OFF + LEN**.
Duration rails and MIDI meters follow the shifted start, including notes from
later rows starting early. Partial starts, ends and disjoint fragments use thin
Braille glyphs without a separate background strip. Note-line and pattern duplication preserve OFF.

The scheduler looks ahead across all rows and maintains chronological event
order even when offsets reorder notes. Pattern loops repeat shifted onsets;
starting/seeking skips onsets already in the past (no automatic preroll or
retroactive note-ons). Thus a negative OFF on row 1 first plays just before the
next loop. A live edit refreshes future onsets without altering already-issued
voices' end times. OFF is MIDI-only; audio retains its LEN column.

Audio LEN defaults to the sample duration. Edit it at the instance's starting
row to shorten its gate; it cannot exceed the sample, pattern end or next
instance. Its fractional tail is visible in the waveform and its mixer gate
ends at LEN, leaving the loaded sample unchanged. Clear audio LEN to restore
the available sample duration.

`mlacker_ui::note_playback::NotePlayback` produces timestamped note-on/off events
using the transport's continuous musical ticks (15000 per sixteenth). Note-offs
are scheduled at LEN, independent of row/frame boundaries and visual meter
decay. Equal-pitch overlaps on a track hold until the last voice ends; stopping
flushes active notes. This is application-side scheduling, not a real-time/lock-free
MIDI backend: mlacker still has no connected MIDI-device output. Consumers must
drain `events` after each update; timestamps preserve timing across late frames.

Pattern → Set length opens a row-count dialog prefilled with the current length
(1–16,384 rows). Growth adds empty rows. Shrinking past populated cells or audio
asks “Are you sure, data will be truncated”; Cancel/Escape leaves data intact.
Confirmed truncation trims or removes audio instances without changing loaded
samples. Extending again does not restore discarded data. Playback stops when
opening the length dialog.

### Navigation

The pattern is a `tui::table::Table` with a frozen ROW column and one column
group per track. Focus the sequence pane with Ctrl+Shift+L; `l`/`h`
select the next/previous column and `j`/`k` (or Down/Up) select rows. Navigation
stops at the edges and scrolls the selection into view, with a fixed header.
`G` jumps to the bottom row; consecutive `gg` jumps to the first row. These
bindings operate only in the active sequence pane, not in menus or cell editors.
Any unrelated key or focus change cancels a pending first `g`.
Menus and dialogs retain exclusive keyboard ownership while open.

### Track actions

mlacker's **Track** menu always targets the selected column's track:

- Rename opens a text dialog with OK and Cancel buttons (Enter and Escape also work).
- Create track → MIDI track / AUDIO track appends an empty track of the chosen
  type and selects it. MIDI has NOTE, VEL, LEN, OFF, CC1; AUDIO has LEN.
  Mixed groups have different widths; navigation, frozen ROW, deletion, and
  duplication follow their actual column ranges. Duplicate preserves track type.
- Duplicate appends an independent copy of the pattern, mute flag, and automation
  settings, then selects the copy.
- Delete removes the track after confirmation; at least one track is retained.
- Clear pattern clears only that track's cells after confirmation.
- Mute / unmute toggles the track flag and its `[M]` header indicator.
- Automation → Add / Configure / Remove CC column edits the track's list of
  automation columns (up to 16 on a MIDI track, one per controller).

Each automation slot accepts `cc:N` for MIDI CC 0–127 (values 0–127),
`pitchbend` (signed values -8192–8191), or `name:min:max` for a custom integer
parameter, e.g. `cutoff:0:1000`. Custom names use letters, digits, and underscores
and start with a letter. Limits must fit i32. Existing slot values must fit new
limits or configuration is rejected without changing data. The Inspector shows
the selected track's mute state and its parameter assignments.

New MIDI tracks start with one column, CC1=`cc:1`. There is a 64-track limit.
These actions change in-memory data, not audio/MIDI output; mute is state
for a future playback engine, and custom parameters need an application mapping.

### Mixer

Press `m` outside menus, dialogs, or cell editing to toggle Inspector/Mixer.
`modules/mlacker_ui/mixer.mla` provides six-cell-wide track strips (five content
cells plus a separator). Headers use `M1`, `A2`, etc. for MIDI/audio and their
current track order; full names remain in the sequence and Inspector. Compact
readouts show `V100` for volume (0–100) and `P0` for pan (-100 left to +100 right).
MIDI uses one pre-pan meter; AUDIO uses two meters, left then right.
The selected track shares the sequence selection and has a lighter background.
When tracks exceed the pane width, the visible strip range follows selection.
New tracks, duplicates, names, and mute state are read from the sequence model.
Volume and pan are model readouts in this first preview, not editable controls.

The meter takes all remaining height below its three readout rows, shrinking or
growing on terminal resize. Tiny panes clip readouts and omit meters when no
height remains. Meters use the `tui::meter::VuMeter` thresholds (green, yellow,
orange, red). View → Meter selects Solid bars or Grainy (osc, default).

**View → Meter → Set update rate** opens an FPS dialog. Enter a whole number
from 1 to 240; `60` requests 60 FPS. Enter/OK applies it immediately, and
Escape/Cancel leaves it unchanged. Fractional deadlines avoid rounding 60 FPS
to a 20 ms / 50 FPS polling cadence. Missed frames are skipped rather than
replayed; actual throughput depends on terminal/rendering speed. Input remains
responsive at low rates, and transport timing, smoothing time constants, and
button animations do not change with FPS. The setting is session-local.

The **Mixer** has no synthetic levels. A MIDI note supplies its velocity / 127
as a meter impulse whose target falls to zero before the next row; empty notes and zero
velocity are silent. Audio tracks use the current instance-relative row's
left/right peak amplitudes. Levels use a fast attack (8 ms time constant) and
release (25 ms), bounded toward the next target without overshoot. They continue
updating when the Mixer is hidden, and decay to silence on stop. Track volume,
pan, and mute still apply; V100 is the editable track gain, not a fabricated VU
source. These are sequencer-derived levels, not measurements from an audio device.

### Note lines

MIDI tracks support 1–16 NOTE/VEL/LEN/OFF groups followed by their shared CC columns.
**Track → Add note line** inserts a blank group after the selected group (or appends
it before the CC columns when one is selected). **Duplicate note line**
copies the selected NOTE/VEL/LEN/OFF group for every row and selects the copy. **Remove
note line** removes that group, keeping at least one. Selecting NOTE, VEL, LEN or
OFF identifies the group; Remove/Duplicate require such a selection. These actions
do not apply to audio tracks, and no new shortcuts are assigned yet.

All note lines retain the usual note editing, typed velocity limits, empty values,
column navigation, and horizontal scrolling. Pattern/track clones preserve the
note-line count and contents. Each MIDI track still has one Mixer strip: the highest
velocity among its non-empty notes feeds that strip's smoothed meter; velocities
are not summed, and a velocity without a note produces no meter activity.

### Cell editing

mlacker uses `SequenceTable : Table` in `modules/mlacker_ui/sequence.mla`, outside
the reusable widget library. It owns its cell strings and a compiler-provided
`std::array` (`array<str8, 128>`) containing every MIDI note. Notes use tracker
spelling: MIDI 0 is `C--1`, MIDI 60 is `C-4`, and MIDI 127 is `G-9`; sharps use
`#`, e.g. `C#4`. Manual notes must occur in this array as well as match the
column regex. ROW is read-only, NOTE starts selected, VEL is bounded to 0–127,
and each MIDI track has nullable automation columns with independent limits;
a cell holds one integer or four 1/64-note steps (`12 . 31 40`).

- Shift+J decreases and Shift+K increases the selected value. Notes step by a
  semitone; integer columns step by one. Endpoints stop without wrapping.
- Enter starts an inline `TextField`; Enter validates and commits, while Escape
  discards the draft. Invalid input stays editable with an Inspector error.
- While editing, text including q/h/j/k/l and spaces is literal input. Ctrl+U
  clears the draft. Pane/menu navigation is suspended until commit or cancel.
- Columns can opt into `nullable: true`: an empty string represents no value
  and bypasses the value regex/range checks, but unsupported types still fail.
  NOTE, VEL and the CC columns enable this in mlacker. An empty NOTE means a rest.
  Enter, Ctrl+U, Enter clears a single cell.
- Shift+Backspace clears all editable cells in the selected row, preserving its
  read-only ROW number. `dd` deletes the row, shifts following rows upward, and
  renumbers ROW. Selection and scrolling are clamped, including an empty table.
  The two d keys must be consecutive; another key, menu, or pane switch cancels
  the pending command. Inside the cell editor, `dd` is ordinary text.
- Adjusting an empty note with Shift+J/K initializes it to C-4; an empty integer
  starts at its column minimum (or zero without limits).

Shift+Backspace requires a terminal that reports the modifier (CSI-u or
modifyOtherKeys). If it sends the same byte as plain Backspace, the shortcut
cannot be distinguished; plain Backspace never clears a whole row.
