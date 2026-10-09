# MLACK session format 1.2

Extension: `.mlack`. A self-contained binary document for the editor's committed
state. No compression, executable code, or live pointers are stored. Plugin paths
refer to external native binaries and must be treated as executable dependencies.

## Primitives

All integers, including counts, IDs, enums and booleans, are **signed little-endian
64-bit** values. Booleans are exactly 0 or 1. Reals are IEEE-754 little-endian f64.
Strings are an i64 UTF-8 byte count followed by those bytes (no trailing NUL).
Lists are a count followed by that many entries, unless a size is implicit below.
The implementation uses the `std::serde` primitive encoding. No trailing bytes
are accepted. Unsupported major or minor versions fail closed.

## Document order

1. String `MLACK`, integer major `1`, integer minor `2`. Minor `0` and `1`
   documents are still read. Minor `0` differs in the track automation record
   below; minor `1` has no OFF cell on AUDIO tracks.
2. View record, in order:
   - BPM, beats per bar, beat unit, transport row, fractional phase (0–14999).
   - Focused pane; sidebar mode (0 Patterns, 1 Song, 2 Audio, 3 Instruments).
   - Pattern/song list selection and scroll; audio selection and scroll;
     instrument selection and scroll.
   - Mixer visible, meter grainy, meter FPS.
   - Sample view visible, grainy, wave style, zoom.
   - Instrument editor visible, slot, selected parameter, horizontal scroll.
3. Sample list. Each sample:
   - Source/display path, channel count, sample rate, frame count, frames per row.
   - `frame_count * channel_count` interleaved PCM values, encoded as f64 and
     restored to f32. PCM is embedded, not reloaded from the source path.
   - Row-peak list: left peak, right peak, left mean, right mean (0–1000).
   - Detail-peak list: left peak, right peak, four entries per row.
4. Plugin list, ascending unique slot order. Each plugin:
   - Slot (0 master, 1–32 instruments), plugin bundle path.
   - Parameter list: stable unsigned-32-bit ID represented as i64, normalized f64
     value (0–1). Read-only values are captured but not forced into the processor.
5. Active pattern ID and next pattern ID.
6. Pattern list. Each pattern:
   - Stable ID and name.
   - Next track ID; selected row/column; horizontal/vertical scroll;
     horizontal/vertical scrollbar flags; alternate-track text flag.
   - Track list. Each track: name, mute, volume, pan, audio flag, instrument flag,
     assigned instrument slot, note-line count, column flags, zoom stage;
     automation slots; audio-instance list. The column flags (0–3, always 0
     on AUDIO tracks) take the place of a retired waveform-zoom flag, which
     older versions wrote as 0: 1 adds a TEXT column, holding free text of up
     to 240 bytes, and 2 adds ID and SYSEX columns. After the track's
     automation columns come ID and SYSEX, then TEXT. An ID cell holds 1–4
     and a SYSEX cell 1–80 hex bytes `00`–`7F` separated by single spaces.
     Older readers reject documents that use flag 2: their flag is 0 or 1
     (the TEXT-only readers), or the row cells do not match their columns.
   - Automation slots: a count (0–16), then per slot its parameter string
     (`cc:N`, `pitchbend`, `aftertouch` or `name:min:max`), minimum and maximum. Each slot is
     one column after the track's note lines. A minor-0 document has no count
     and always stores exactly two slots.
   - An automation cell is empty, one integer played on its row, or four
     space-separated 1/64-note steps where `.` is an empty step (`12 . 31 40`).
     Values must lie within the slot's limits.
   - Each audio instance: zero-based sample-list index, starting row, length ticks
     (0 means natural sample length). 15000 ticks represent one sixteenth note.
   - Row list. Each row is a string list containing all cells, including hidden
     columns and ROW. AUDIO tracks contain LEN and OFF followed by WAVE when the
     track has an instance; minor-0/1 rows omit AUDIO OFF. Column definitions are
     rebuilt from track metadata; cells must match the resulting column count
     and writable-column validation rules.
7. Song order: list of stable pattern IDs, allowing repeated occurrences.
   This is lane 1 of the song matrix, so 0 marks an empty row. Files written
   before the matrix never contain 0.
8. Optional MIDI-learn extension: string `MIDI_LEARN`, followed by a list of
   `(channel * 128 + CC, instrument slot, stable parameter ID)` integer triples.
   At most 2048 entries, strictly increasing keys 0–2047, slots 1–32. Every slot
   and parameter ID must exist in the saved plugin list. On restoration the ID
   is resolved to the current parameter index; read-only targets are rejected.
   Files without mappings or aux effects omit this extension, preserving the original 1.0
   layout. This reader accepts both layouts; older readers reject extended files.
   Armed listening is transient and is never saved. Unknown/trailing data is rejected.
9. Optional aux extension, following `MIDI_LEARN` (which may have zero mappings):
   string `AUX_EFFECTS`, then effect count (1–8). Each effect stores its plugin
   path (empty for an unloaded channel), return volume (0–100), and parameter
   list of `(stable parameter ID i64, normalized f64)` pairs. Empty paths require
   zero parameters; IDs must be unique within each effect.
   For each pattern in document order, a track count matching that pattern is
   followed by exactly `effect_count` send levels (0–100) for each track.
   Finally, effect-bank focus (boolean) and selected effect index (0-based) are
   stored. Effects use runtime parameter slots 33–40, separate from the plugin
   list in step 4; the editor view may reference these slots. Older readers
   reject this extension. Files without effects retain the preceding layouts,
   unless track inserts follow (in which case effect count may be zero).
10. Optional `TRACK_INSERTS` extension follows `AUX_EFFECTS`. An instance count
    (0–256) precedes each instance's nonempty path and parameter list of
    `(stable parameter ID i64, normalized f64)` pairs. IDs must be unique within
    an instance. Instance IDs are one-based; parameter slots are 41–296.
    For every pattern, its matching track count precedes four instance IDs per
    track (zero means empty). Finally: insert view visible boolean and selected
    slot index (-1–3). Pattern copies can reference the same instance. Detached
    instances remain available until session close. This extension also requires
    the preceding `MIDI_LEARN` tag, with zero mappings if necessary. When the
    insert view is hidden the selected slot is stored as -1.
11. Optional `SAMPLER_PADS` extension follows `TRACK_INSERTS`: a count (0–4096)
    of `(instrument slot 1–32, pad 0–127, zero-based sample-list index)` integer
    triples in strictly increasing `slot * 128 + pad` order. Every slot must be an
    instrument in the plugin list, and every index must be in the sample list.
    On restoration, after plugins and MIDI learn, each pad's embedded PCM is sent
    to its instrument (see `stdlib/include/mla_sampler_protocol.h`; used by
    Mla Drum). If an instrument refuses a pad, the open is rejected. This
    extension requires the preceding tags, with empty sections if necessary.

12. Optional `SONG_MATRIX` extension follows `SAMPLER_PADS`: a lane count
    (0–15) and, per lane, a row count matching the song length followed by one
    pattern ID per row, where 0 is an empty cell. These are the parallel lanes
    beside the song list of step 7, which is lane 1. Every non-zero ID must
    exist in the pattern list. This extension also requires the preceding tags,
    with empty sections if necessary.

13. Optional `TRACK_OUTPUTS` extension follows `SONG_MATRIX`: per pattern, a
    track count matching that pattern followed by one output channel per track.
    0 is the master bus; any other value is the destination track index + 1
    within the same pattern, and a track may not name itself. It is written only
    when some track leaves master, and it also requires the preceding tags, with
    empty sections if necessary. A route to a track that is no longer an AUDIO
    track is loaded as written and plays to master.

14. Optional `MATRIX_GRID` extension follows `TRACK_OUTPUTS`: the number of
    pattern rows one matrix row holds (1-16384). It is written only when a song
    changed it from the default 64. A session without the tag takes the length
    most of its patterns have (the shortest of those on a tie), so a song of
    16-row patterns plays them one per matrix row. Like the others, it requires the preceding
    tags, with empty sections if necessary.

15. Optional `MASTER_BUS` extension follows `MATRIX_GRID`: spectrum analyzer
    visible (boolean), detail (0 blocks, 1 Braille, 2 wide bars) and update rate
    (1–60 FPS); then the master high-pass in Hz (0 = off, otherwise 10–1000),
    the master volume in percent (0–150) and, for each of the four EQ bands, its
    centre (20–20000 Hz), gain in tenths of a dB (−120–120) and Q in hundredths
    (30–1000), then whether the EQ (high-pass and bands) is switched in
    (boolean; a file that ends before it has the EQ in). It is written only
    when one of these differs from its default,
    and it also requires the preceding tags, with empty sections if necessary.
    A session without it opens with the analyzer hidden and the default bus:
    20 Hz high-pass, flat bands at 100/500/2500/8000 Hz with Q 1, and 100%.

16. Optional `MATRIX_LOOPS` extension follows `MASTER_BUS`: a count, then per
    entry the lane (0–15, 0 being the song list of step 7), the row, and a
    kind: 1 for a cell whose pattern loops down its lane, 2 for a split that
    ends a loop or a long pattern. A looping cell must hold a pattern in the
    sections above; a split cell must be empty there, since those sections
    store plain pattern IDs. It is written only when the matrix has a loop or
    a split, and it requires the preceding tags (a default `MASTER_BUS` is
    written when only this extension is needed).
17. Optional `AUX_OUTPUTS` extension follows `MATRIX_LOOPS`: per pattern, a
    track count that must match the pattern, then per track a bus count (0–15)
    and, for each of the track's instrument's aux output buses from Out 2 on,
    its route: -1 to stay with the instrument's main output, 0 for master,
    a destination track index + 1, -2 - n to feed aux effect channel n (0–7)
    as a send, or -10 for nowhere. Readers before the effect routes reject
    -2 .. -10. A track cannot route to itself.
18. Optional `PAD_MARKERS` extension follows `AUX_OUTPUTS`: slice markers set
    by hand for sampler pads. A count (0–4096), then per pad its instrument
    slot (1–32) and pad (0–127), which must be one of the `SAMPLER_PADS`
    pads, a marker count (1–65536) and the markers as frames (0–16777216),
    the first 0 and each after it larger. On load they are sent to the
    instrument after the pads. It is written only when some pad has hand-set
    markers, and it requires the preceding tags (an `AUX_OUTPUTS` extension,
    possibly empty of routes, is written with it). A route to
    a track that is not an AUDIO track plays as -1. It is written only when
    some Instrument track routes an aux bus, and it requires the preceding
    tags.
19. Optional `INSTRUMENT_INPUTS` extension follows `PAD_MARKERS`: per
    pattern, a track count matching that pattern followed by one input per
    track: 0 for none, -1 for the live audio input, otherwise the AUDIO track
    index + 1 feeding the track's instrument (not the track itself). It is
    written when some track takes an input or a later extension follows, and
    it requires the preceding tags.
20. Optional `AUDIO_PROPERTIES` extension follows `INSTRUMENT_INPUTS`: the
    Sample properties of audio placements. Per pattern, a track count matching
    that pattern, then per track a count matching its audio-instance list and
    per instance, in list order: exclusive frame end (0 for none, otherwise
    above the instance's start offset and at most the sample's frame count),
    original BPM (0 for none, otherwise 20–400), sync mode (0 Off, 1 Repitch,
    2 Stretch, 3 Beats, 4 Transients, 5 Transients 2), pitch in semitones
    (−24–24, 0 unless the mode is 2 or above), resampling (0 cubic with
    low-pass, 1 cubic, 2 linear with low-pass, 3 linear) and Transients 2 loop
    mode (0 forward, 1 back-and-forth, 2 off). It is written only when some
    placement has a property set, and it requires the preceding tags. A
    `.mlapatt` pattern file may end with the same tag and that one pattern's
    record; files without properties omit it, so older readers still open
    them.

21. Optional `AUDIO_GROUPS` extension follows `AUDIO_PROPERTIES`: the Audio-list
    groups. A group count (0–4096), then each group's path: names joined by
    `/` (`Drums`, `Drums/Kicks`), none empty, `.` or `..`, each group's parent
    listed before it. Then a count matching the sample list and each sample's
    group path, empty for none, otherwise one of the listed paths. It is
    written when the list has any group, and it requires the preceding tags
    (`AUDIO_PROPERTIES` is then written even without properties). Without it,
    a plain session puts generated slices (`kick.wav/kick.wav - 1`) in a group
    named after their original, and a project takes each sample's group from
    its folder.
22. Optional `PROJECT_OPTIONS` extension follows `AUDIO_GROUPS`: a flag
    (0 or 1) set when Audio > Move to group saves the project without asking
    first ("Don't ask this again"), then a flag set when the sample editor
    opens full screen (Audio > Always open sample editor full screen; a file
    ending before it opens it full screen). A project session writes it when
    the first flag is set or the second is clear; any session writes it,
    with the current flags, when `MIDI_KNOBS` follows. It requires the
    preceding tags (`AUDIO_GROUPS` is then written even without groups).
23. Optional `MIDI_KNOBS` extension follows `PROJECT_OPTIONS`: the project's
    MIDI knob mode (File > Project > Project settings), 1 absolute or 2
    relative (endless encoders stepping learned parameters). It is written
    only when the project overrides the application setting; a session
    without it takes the knob mode from File > Settings.

The active pattern is serialized from the live editor, not its older library
snapshot. Audio placements reference the embedded sample list; plugin assignments
reference stable slots, including holes left by removed instruments.

## Validation and restoration

Maximum file size is 256 MiB; strings are limited to 4096 bytes; at most 256
samples, 33 plugins (including master) plus 8 aux effects and 256 inserts,
16384 parameters per plugin, 1024 patterns,
64 tracks per pattern, 16384 rows per pattern, 8 million cells per document, and
65536 song entries and 15 parallel song lanes are accepted. Each sample has at most 16777216 frames and one
or two channels. Invalid counts, non-finite/out-of-range numeric data, malformed
cell values, duplicate pattern IDs, and dangling song/instrument/sample references
are rejected. Application editing limits may be stricter than archive limits.

Loading first builds a separate document and stopped audio controller. Plugins are
loaded and parameter layouts checked before the running session is replaced.
Parameters are restored on the owning thread with audio stopped, outside the
real-time callback. Values already present in the new instance are not replayed:
some plugins expose command-like MIDI parameters whose default values must not
be resent as commands. Hardware settings are kept from the current application.
Meters/MIDI activity are transient and start silent; playback never auto-starts.

Version 1.0 does not capture opaque VST3 component/controller state blobs or
plugin-internal sample libraries. It preserves the exposed parameter state edited
by mlacker. Sampler pads filled by mlacker are the exception: they are rebuilt
from the embedded sample list (`SAMPLER_PADS`). Future incompatible additions require a new version.

## Project folders (`.mlaproj`, session 1.3)

A project is a directory whose name ends in `.mlaproj`:

- `Project.mlack`: the session, minor version **3**.
- `Audio/`: one IEEE float32 WAV per sample-list entry, named after the source
  file (`kick.wav`, then `kick 2.wav`, … for repeated names).
- `Presets/`: one parameter preset per loaded plugin: `.mlapre` (1.0) for
  instruments, `.mlafxpre` for the master, aux effect and insert plugins.

A project 1.3 session is the portable 1.2 document with two differences:

- A sample is its relative path string (`Audio/<name>`), channel count, sample
  rate, frame count and frames per row. The file must still have that format and
  frame count; row and detail peaks are rebuilt from its PCM when loading.
- A plugin's parameter list is replaced by the relative path of its preset
  (`Presets/<name>`). An aux effect channel without a plugin stores an empty
  path. Kit presets are accepted, but their pads are ignored: sampler pads
  still come from `SAMPLER_PADS`.

Content paths are one file name below `Presets/`, or below `Audio/` or one of
its group folders (`Audio/Drums/Kicks/kick.wav`: the sample is in group
`Drums/Kicks`, see `AUDIO_GROUPS`, which also keeps empty group folders and
whose names win over the folders'; a folder drops an audio extension from its
group's name, so group `illusion.wav` is the folder `Audio/illusion`); any
other path, including absolute paths and `..`, is rejected, so a project only
reads its own files and opens from any location. Plugin bundle paths remain
absolute. Project sessions are only read as part of a project. The reader also
accepts legacy project 1.2 sessions, adding an empty AUDIO OFF cell to each row.
A failed load names the missing or invalid content file.

Saving writes `<folder>.saving`, then renames an existing project to
`<folder>.previous`, moves the new folder into place and removes the old one.
An existing target that is not a directory holding `Project.mlack` is refused.

## Portable MIDI learn files (`.mlalearn`, version 1.0)

Uses the same little-endian integer and length-prefixed UTF-8 string primitives:
string `MLALEARN`, major 1, minor 0, plugin display-name string, mapping count,
then `(channel * 128 + CC, stable parameter ID)` integer pairs. Keys are strictly
increasing, 0–2047; IDs are unsigned 32-bit values stored as i64. Maximum size is
64 KiB, maximum count 2048, maximum string length 4096 bytes. Trailing data is
invalid. No session slot, parameter values, or armed-listening state is stored.

Import requires the same plugin display name and writable parameter IDs, and
replaces mappings belonging to the selected loaded instrument. Keys currently
owned by another instrument reject the import. The entire file is validated
before changing mappings. Empty mapping lists clear that instrument's bindings.
Exports use atomic file replacement. Ordinary `.mlack` persistence is unchanged.

## Parameter preset files (`.mlapre`, version 1.0 and 1.1)

String `MLAPRE`, i64 major 1 and minor 0 or 1, plugin display-name string, i64 parameter
count, then `(i64 stable parameter ID, f64 normalized value)` pairs. Uses the same
little-endian primitives. Maximum 256 MiB, 16384 parameters, and 4096-byte strings.

Minor 1 is a **kit preset**, written for sampler instruments (such as Mla Drum)
that have pads loaded from the session. After the parameters: string
`SAMPLER_PADS`, a count (0–64), then per pad its number (0–127, strictly
increasing) and the sample in the session's sample-list encoding (path, format,
PCM, row and detail peaks). Loading a kit replaces every pad of the instrument:
listed pads receive their samples, and all other pads are cleared. Kit samples
join the Audio list, reusing an identical existing sample. Minor 0 presets leave
pads unchanged. Readers that only know 1.0 reject 1.1 files.
The plugin name and complete parameter-ID set must match; duplicate/missing IDs,
non-finite values, values outside 0–1, and trailing/truncated data are rejected
before applying. Read-only parameters are recorded but not written on restore.
Unchanged values are not resent, preserving the session restoration safeguards.
MIDI bindings and opaque VST3 component/controller state are not part of a preset.

## Sampler slot preset files (`.mlaslot`, version 1.0)

One Mla Sampler slot, loadable into any slot. String `MLASLOT`, i64 major 1
and minor 0, plugin display-name string (`Mla Sampler`), i64 field count
(0–4096), then `(i64 key, f64 normalized value)` pairs, then an i64 flag (0 or
1) and, when 1, the slot's sample in the session's sample-list encoding. Same
little-endian primitives and limits as `.mlapre`.

A key names a slot field independently of the slot: `base * 64 + field`,
where the slot's parameter ID is `base + stride * slot + field` for one of
Mla Sampler's per-slot ID blocks (see its README: `200 + 7s`, `400 + 5s`, …,
`4000 + 32s`). The plugin name must match; duplicate keys, values outside 0–1
and trailing/truncated data are rejected before anything is applied. Keys the
plugin lacks are skipped, and slot fields the file lacks keep their values.
Without a sample, loading empties the slot.
