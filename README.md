[![Build and Test](https://github.com/mattilaa/mlacker/actions/workflows/build.yml/badge.svg)](https://github.com/mattilaa/mlacker/actions/workflows/build.yml)
# mlacker

Terminal tracker built with MLang, the MLang `tui` widget library, macOS AUHAL,
and a native VST3 host. The tracker's UI and model live in `modules/mlacker_ui/`
(imported as `mlacker_ui::*`); the VST3 effects, the Mla Drum and Mla Sampler
instruments, the [Mla Vocoder](plugins/mla_vocoder) (VP-330 style, with its
own carrier synth), [Mla Speech](plugins/mla_speech) (an Atari ST style
speech synthesizer that says a pattern's comment texts) and
[Mla 06](plugins/mla_06) (a synthesized TR-606 style drum machine) are under `plugins/`. [docs/interface.md](docs/interface.md) describes the views,
editing keys, transport and audio handling in detail.

## Build and run

Prerequisites: macOS, Xcode command-line tools, CMake, Git, Python 3, and what
MLang itself needs to build (LLVM, flex, bison, OpenSSL, zstd, z3, rapidjson).

```sh
./bootstrap.sh                 # choose the MLang toolchain and install dirs, then build
./build.sh                     # build everything configured (same as --all)
build/cmake/bin/mlacker        # run
```

`bootstrap.sh` asks where MLang comes from and writes the answers to the
ignored `mlacker.conf`, which `build.sh` reads. Re-run it to change them; the
current values are the defaults.

- **subproject**: clones MLang's development repository into
  `subprojects/mlang` (ignored by this repository). Make compiler/stdlib changes
  that mlacker needs there and commit them to the MLang repository (see
  [Developing MLang alongside mlacker](#developing-mlang-alongside-mlacker)).
- **external**: uses your own MLang checkout (default `../mlang`); MLang changes
  are made in that checkout as usual.

In subproject mode it also asks whether `./build.sh --all` should build the
full MLang toolchain from `subprojects/mlang` (compiler, runtime, `mlang-config`,
`mlangd-mla`, `mlang-format`, `mlang-frontend`, `mlangpkg`) and, with `--install`,
install it under an MLang prefix (default `~/.local`, tools in `~/.local/bin`).
That keeps the development MLang you commit to in sync with the installed one.

It also offers to write notes for AI coding agents (default yes in subproject
mode): `CLAUDE.local.md` for Claude Code and `AGENTS.override.md` for Codex, both
ignored by git. They tell agents to make every MLang change (compiler, stdlib,
`tui`/`dsp` modules, headers) in the configured checkout, for example
`subprojects/mlang`, and to commit it there rather than in mlacker or another
MLang checkout. Only a marked block in those files is generated; anything else
you write in them is kept, and `--no-agent-notes` removes just the block.

It then asks for the VST3 plugin install directory (default
`~/.local/plugins/VST3`) and the mlacker binary directory (default `~/.local/bin`),
and offers to build (and install) right away. Everything can be given as flags
instead (`--mlang-subproject`, `--mlang-dir DIR`, `--mlang-toolchain`/
`--no-mlang-toolchain`, `--mlang-prefix DIR`, `--agent-notes`/`--no-agent-notes`,
`--plugin-dir DIR`, `--bin-dir DIR`,
`--build`/`--no-build`, `--install`/`--no-install`, `-y`); see
`./bootstrap.sh --help`.

`build.sh` always builds the MLang compiler and runtime (`mlang`,
`libmlang_std.a`) that mlacker links against in the chosen checkout, then the
selected targets. Targets can be combined; none means `--all`:

```sh
./build.sh --all               # mlacker + plugins, + full MLang if MLANG_TOOLCHAIN="yes"
./build.sh --app               # mlacker only
./build.sh --plugins           # plugins only
./build.sh --mlang             # full MLang toolchain only (MLang's own build.sh)
./build.sh --mlang --app       # MLang toolchain and mlacker
./build.sh --install --all     # build and install everything configured
./build.sh --install --plugins # build and install only the plugins
./build.sh --install --mlang   # build and install MLang under MLANG_PREFIX
./build.sh --update            # pull the latest MLang into subprojects/mlang
./build.sh --update --all      # pull the latest MLang, then build everything
./build.sh --test              # build everything, then run all tests
```

`--update` fetches MLang's upstream into `subprojects/mlang` and fast-forwards
the checked-out branch. It never merges: uncommitted changes are kept (git
refuses only if the update would overwrite them), and when the branch has local
commits that are not upstream yet it stops and asks you to rebase or merge in
`subprojects/mlang`. In external mode it leaves your own checkout alone. Given
alone it only updates; with targets it updates and then builds.

`--mlang` works with an external checkout too, but there you would normally
build MLang yourself. When the checkout has no `build/mlang-config.conf` yet,
`build.sh` writes one for the MLang prefix so MLang's build.sh runs without
prompting; an existing one keeps its settings apart from the prefix.

For tests see [Tests](#tests).

`mlang.toml` pins VST3 SDK 3.8.1 at commit
`3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96`; `mlang.lock` records the resolved
revision. Generated SDK sources and binaries stay under ignored `build/`.
SDK license and usage notices remain in `build/deps/vst3sdk/LICENSE.txt` and
`VST3_Usage_Guidelines.pdf`; preserve applicable notices when distributing.

## Developing MLang alongside mlacker

mlacker needs matching MLang changes now and then: a `tui` widget, a stdlib
function, a compiler fix. Make those changes in the MLang checkout that
mlacker builds against (`subprojects/mlang` in subproject mode), and commit
them to MLang, never to this repository.

### In `subprojects/mlang` (subproject mode)

1. Work on a branch in the subproject:

   ```sh
   git -C subprojects/mlang switch -c my-change
   ```

2. Edit MLang and mlacker together. `./build.sh --app` always rebuilds the
   MLang compiler and runtime (`mlang`, `libmlang_std.a`) first, and the `tui`
   and `dsp` modules are compiled from the checkout, so every build sees your
   MLang edits.
3. Test both sides:

   ```sh
   ./build.sh --app --test                                  # mlacker unit tests + CTest
   (cd subprojects/mlang && build/mlang --tests tests/tui_tests.mla)   # MLang tests you touched
   (cd subprojects/mlang && build/mlang test)               # all MLang .mla tests
   ```

4. Commit in each repository: the MLang part in `subprojects/mlang`, the
   mlacker part here. Mention the MLang dependency in the mlacker commit.
5. Merge and **push MLang first**, then mlacker. CI builds mlacker against the
   MLang on GitHub, so an mlacker push that needs an unpushed MLang change
   fails there.

   ```sh
   git -C subprojects/mlang switch main && git -C subprojects/mlang merge my-change
   git -C subprojects/mlang push
   git push
   ```

`./build.sh --update` fast-forwards the subproject later on. It keeps local
commits and uncommitted work, and stops instead of merging if your branch and
upstream have diverged.

### In a separate MLang checkout

If you already develop MLang elsewhere (for example `../mlang`), you have two
options:

- **Build mlacker against that checkout.** Run
  `./bootstrap.sh --mlang-dir ../mlang` (external mode). mlacker then builds with
  whatever is in that checkout, and you commit and push there as usual. Run
  `./bootstrap.sh --mlang-subproject` to switch back.
- **Keep subproject mode and copy the change over to test it.** Apply the
  uncommitted diff to the subproject, build and test mlacker, then commit in
  `../mlang`:

  ```sh
  git -C ../mlang diff -- modules/tui | git -C subprojects/mlang apply
  ./build.sh --app --test
  ```

  After the MLang commit is pushed, drop the copy and pull the real commit. The
  copy must go first, or `--update` refuses to overwrite it:

  ```sh
  git -C subprojects/mlang restore --staged --worktree modules/tui
  ./build.sh --update --app
  git -C subprojects/mlang status --short   # empty: the subproject matches upstream
  ```

  Then push mlacker.

## Install

```sh
./build.sh --install --all      # MLang -> MLANG_PREFIX (if MLANG_TOOLCHAIN="yes"),
                                # mlacker -> BIN_DIR, Mla*.vst3 -> PLUGIN_DIR
./build.sh --install --app      # mlacker only
./build.sh --install --plugins  # plugins only
./build.sh --install --mlang    # MLang toolchain only
```

Installing builds first and ad-hoc signs the copies; existing bundles of the
same name are replaced. The destinations come from `mlacker.conf`. When running
`mlang pkg` directly, pass `--option mlang_root=DIR`, `--option bin_dir=DIR`,
`--option plugin_dir=DIR` or `--option plugins="mla_verb mla_eq"` (names under
`plugins/`).

## Sessions (.mlack 1.2)

Launching mlacker without a filename starts one empty, 64-row **Untitled**
pattern, with no tracks, song entries, instruments, or samples. Menus stay closed.
F1 opens the menu bar, and Tab / Shift+Tab cycle the panes. To open an existing
session at startup:

```sh
build/cmake/bin/mlacker "my song.mlack"
```

**File → Save session** (or Ctrl+S) saves to the current filename; the first save
asks for a `.mlack` path. **Save session as** chooses another path. **Open session**
loads a `.mlack` file. **New session** confirms before resetting to an empty editor.
Saving stops the sequencer; loading always restores a stopped transport.

## Projects (.mlaproj)

A project is a folder, like a `.vst3` bundle, that keeps a song's content as
ordinary files:

```
My song.mlaproj/
  Project.mlack      session (format 1.4)
  Audio/             ordinary Audio-list samples, as float32 WAV
    SampleAudio.aif/ destructive slices grouped by their original filename
  Presets/           each plugin's parameters: Instrument 01 - <name>.mlapre,
                     Master / Effect N / Insert N - <name>.mlafxpre
```

**File → Save project** consolidates the samples and plugin presets into the
folder (the first save asks for a name; `.mlaproj` is appended when missing),
and **Save project as** picks another folder. While a project is open, Ctrl+S
and **Save session** save the project in place. **Open project** opens a
`.mlaproj` folder, and so does passing one on the command line:

```sh
build/cmake/bin/mlacker "/path/to/My song.mlaproj"
```

The session refers to `Audio/` and `Presets/` files by paths relative to the
folder, so a project can be moved, copied or opened from any location. VST3
plugins stay where they are installed and are referenced by their bundle path.
A save writes the whole folder beside the target and swaps it in only when it
is complete; the previous folder, including any files added to it by hand, is
replaced. A folder that is not an mlacker project is never overwritten.

## Menus

Each menu opens with its function key, shown before its title in the menu bar:
**F1** File, **F2** Edit, **F3** View, **F4** Track, **F5** Pattern, **F6** Audio,
**F7** Instrument, **F8** Effect and **F9** Record. While a menu is open, another
function key switches to that menu and the open menu's own key closes it. Each
menu starts with what it creates, then what it changes, then what it removes;
related entries live in submenus.

Enter runs the highlighted entry and closes the menu. On an option, an entry
shown as `[x]` or `[ ]` (such as **Record → Play metronome** or **Audio →
Bounce tail**), **Space** toggles it and keeps the menu open, so you can set
several options in a row. Space does nothing on other entries.

Items that have a keyboard shortcut show it right-aligned in the menu, in the
same color as the function keys, written like Vim key notation: `<C-s>` is
Ctrl+S, `<S-m>` Shift+M, `<C-S-m>` Ctrl+Shift+M and `<C-S-F1>` Ctrl+Shift+F1. A
shortcut of several presses in a row is written `<C-a><C-m>`.

| Shortcut | Action |
|----------|--------|
| `<C-S-F1>` / `<C-S-F2>` / `<C-S-F3>` | Left view: Patterns / Audio / Instruments |
| `<C-s>` | Save the session or project |
| `<C-f>` | Toggle the focused content pane fullscreen |
| `<C-g>` | Show or hide the View sidebar |
| `<S-m>` | Show or hide the song matrix |
| `<C-S-m>` | Show or hide the spectrum analyzer |
| `<S-p>` | Show or hide the virtual keyboard |
| `<S-s>` | Show or hide the Sampler pane (Mla Sampler) |

These use Ctrl+Shift because macOS keeps Ctrl+F1–F3 for keyboard focus. The
terminal must pass modified function keys on; the View menu works everywhere.

For developers: every command is an `Action` in
`modules/mlacker_ui/actions.mla`, and shortcuts are bound to actions in
`modules/mlacker_ui/keymap.mla` (`default_keymap()`) as one or more key chords
(`key(115, true, false)` is `<C-s>`, `fkey(1, true, true)` is `<C-S-F1>`). The key
handling and the menu labels both read the keymap, so changing a binding there
changes the key and what the menu shows. A binding of several chords waits for
the rest of its presses; a `global` binding (like `<C-s>`) also works while a
dialog or text field has the keyboard.

| Menu | Contents |
|------|----------|
| File | New / Open session / Open project / Recent sessions ▸ / Save session / Save session as / Save project / Save project as / Settings / Quit |
| Edit | Undo, Redo, Copy/Cut/Paste clip |
| View | Patterns, Song matrix, Audio, Instruments, Sample view ▸, Meter ▸, Show spectrum analyzer, Spectrum analyzer ▸, Show virtual keyboard, Show sampler, Reset layout, Show details |
| Track | Create MIDI/AUDIO/Instrument track, Rename, Duplicate, Mute, Set output channel, Note lines ▸, Automation ▸, Clear pattern, Delete, Route plugin outputs, Set instrument input |
| Pattern | Add, Clone, Rename, Set length, Follow matrix patterns, Set matrix row length, Remove, Save pattern, Load pattern |
| Audio | Add audio, Edit sample (destructive), Clip ▸, Remove audio, Bounce tail, Bounce tail length ▸, Bounce second pass, Make track clips destructive |
| Instrument | Add instrument, Open VST3 editor, Drum pads ▸, Presets ▸, MIDI learn ▸, Remove instrument |
| Effect | Add effect channel, Load/Edit effect plugin, Set track send, Master ▸, Remove effect plugin |
| Record | Play metronome, Extend pattern when playing, Metronome ▸, Bounce selection to sample |

## Song matrix

**Shift+M**, or **View → Song matrix**, shows the song matrix in the pattern
pane. It replaces the old Song sidebar: rows are song steps, columns are
parallel lanes, and every pattern on a row plays together. The sidebar keeps the
pattern list.

| Key | Action |
|-----|--------|
| `h/j/k/l` or arrows | Move the cursor |
| `Enter` | Choose the cell's pattern from a list of `<no>:<name>`, or "(empty)" |
| `Backspace` | Empty the cell |
| `y` / `p` | Copy the cell's pattern / paste it into another cell |
| `o` / `O` | Insert an empty row below / above, across every lane |
| `Ctrl+O` / `Ctrl+Shift+O` | Insert a cell below / above in this lane only, leaving the other lanes where they are |
| `dd` | Remove the whole row |
| `r` | Loop the cell's pattern down its lane, or stop it looping |
| `s` | Split: end a loop (or a long pattern) at this row |
| `Shift+R` | Arm the cell's pattern for recording, or disarm it |
| `v` | Mark rows from here to the cursor for **Record → Bounce selection**, or clear the mark |
| `Space` / `Ctrl+P` | Play the matrix from the cursor row, or stop |
| `Shift+M` | Close the matrix |

**Space** or **Ctrl+P** in the matrix plays the song from the cursor row: every
pattern in a row plays together, then the transport moves to the next row
holding patterns, wrapping at the end. The playing row is drawn brighter.
Pressing either again stops. Outside the matrix, Space plays the selected
pattern alone, as before.

The next row is merged and scheduled while the current one plays, at a moment
when no note is due for 40 ms (or two pattern rows before the change at the
latest), so the change itself does no work on the downbeat. Notes still
sounding when a row ends keep their length and get their note-offs in the next
row. Editing anything while the matrix plays prepares the next row again.

On an audio device, sequencer notes and CCs go to the audio engine 60 ms ahead
of the transport, each stamped with the exact audio frame it sounds on, so
every track keeps sample-accurate time with the others however busy the UI is
(merging a row with long patterns can take tens of milliseconds). The next row
sends its first notes ahead the same way before the change, and once it has, it
plays even if you edit in the meantime; a row never sends anything past its
own end. Audio clips and the metronome are still started when the UI
reaches them.

### Recording in the matrix

**Shift+R** on a cell arms its pattern: its lane header shows a red **R** and
the cell turns red. **Ctrl+P** (or Space) then plays the matrix and records
MIDI notes and controllers into that pattern while the other lanes play along.
Playback starts on the armed pattern's row, or on the cursor row when that lies
inside the pattern. The pattern opens in the editor and grows row by row for as
long as the take runs, across matrix rows and past the end of the song, which
wraps around. Its own notes are not played back during the take; you hear what
you play. Only **Ctrl+P** (or Space) stops the take. The pattern then ends at
the nearest bar line (moving on a bar when that would cut off a recorded note),
the pattern is disarmed, and as after any take the track can be named.

The take goes to the pattern's armed MIDI track, else the MIDI track under its
cursor, else its first MIDI track; a track armed only for the take is disarmed
again afterwards. The pattern must hold a MIDI track. A matrix take has no
count-in. A longer pattern takes as many matrix rows as it needs, up to the next
pattern placed below it in its lane.

### Looping patterns

**r** on a cell switches looping for the pattern playing there (on the cell
where it starts, or on any of its rows). A looping pattern keeps repeating down
its lane after its own rows, and the rows it repeats into show a darker copy
of it, `» Name`; the cell it starts on is marked `Name »`. A 16-row pattern
looping on a 16-row grid beside a 64-row one fills the three rows next to the
long pattern with repeats:

```
ROW  L1              L2
001  Beat A »        Verse ┬
002  » Beat A              │
003  » Beat A              │
004  » Beat A              ┴
```

The loop runs until the next pattern placed in its lane, which takes over from
its row, or until a split. **s** on a repeat row splits the loop there: the
cell shows `╳ end` and the lane is silent from that row. **s** on a later row
of a long pattern cuts it short the same way. Backspace on a split removes it
and lets the loop run on; Backspace on a repeat row splits there rather than
removing the pattern it repeats. Rows inserted or removed, and lanes shifted
with Ctrl+O, carry loops and splits with their cells, and sessions save them.
A pattern that does not loop plays its own rows only, and one shorter than the
matrix row still repeats to fill that row, as before.

### Row length and long patterns

One matrix row holds a fixed stretch of pattern time. A new song uses 64 rows;
a song opened from a file that predates the setting takes the length most of its
patterns have, so a song written from 16-row patterns plays them one per matrix
row instead of repeating each of them four times. **Pattern → Set matrix row
length** changes it, and from then on the song keeps what you chose. A pattern longer than
that occupies as many matrix rows as it needs and plays a different stretch of
itself in each, so the lanes beside it move on to their own patterns instead of
waiting for it. A 128-row pattern beside two 64-row ones looks like this:

```
ROW  L1              L2
001  Verse       ┬   DrumsA
002          ┴       DrumsB
003  Chorus          DrumsA
```

With a 16-row grid, four 16-row patterns in a lane fill exactly the time one
64-row pattern takes beside them:

```
ROW  L1              L2
002  Beat A          Untitled ┬
003  Beat B                 │
004  Beat A                 │
005  Snarefill              ┴
```

The rows a pattern covers belong to it: placing another pattern there is refused
with the row it started on, and Backspace on such a row clears the pattern that
covers it. A pattern shorter than the row length repeats to fill its row, as
before, and a song that never changes the length keeps playing exactly as it did.

Within a row, every lane plays its own stretch, and a lane holding nothing is
silent. Every lane brings its own tracks with their clips,
faders, sends, inserts and output channels, and the mixer the engine follows is
the row that is playing, not whatever pattern the sidebar has selected. A row
that starts the song with a pattern carrying no effects therefore no longer
takes the next row's routing with it.

The matrix replaces the pattern editor in that pane while it is open, and menus
open above it. A session with no song yet starts the matrix on one empty row.

**Pattern → Follow matrix patterns** (on by default, shown as `[x]` in the menu)
keeps the rest of the app on the pattern the matrix points at:

- *Browsing*: moving the cursor over a cell selects that cell's pattern, so the
  editor behind the matrix, the sidebar and the mixer show its channels, faders,
  sends, inserts and output routing. The status line names it. Empty cells keep
  the pattern already shown.
- *Playing*: each matrix row moves the editor and sidebar to the pattern the row
  starts from, and the mixer shows the whole row: every lane's channels, with a
  channel that several parallel patterns share drawn once (same kind, name and
  instrument). Those strips are the song's, not one pattern's, so they are a view
  only; stop the matrix or turn the option off to mix.

With the option off, the editor and mixer stay on the selected pattern. The
setting lasts for the session and is not saved in `.mlack`.

Cells may be empty, including in lane 1: an empty row is a silent step. An empty
lane is always available past the last used one, up to 16 lanes. Pasting a
pattern warns about clashes just like choosing one does.

### Parallel patterns that clash

Two patterns on the same row can drive one instrument with the same note at the
same step, e.g. two patterns hitting the same drum pad. Placing such a pattern
warns first, counting the clashing notes:

- **OK** places it anyway, leaving the notes as they are.
- **Cancel** leaves the cell alone.
- **Split** places it and gives each clashing track its own note line, so the
  parallel patterns stop sharing one. A split track is renamed with the
  `Track 1 - 2`, `Track 1 - 3` convention, counting the patterns that share the
  instrument in that row. Splitting again replaces the suffix rather than
  stacking it.

Only real collisions warn: the same instrument playing different notes, or the
same note at different steps, is left alone.

## Removing library items

**Shift+Backspace** in the library pane removes the selected item: a sample when
the Audio list is shown, an instrument instance when the Instruments list is.
**Audio → Remove audio** and **Instrument → Remove instrument** do the same from
the menu.

An item still in use asks first:

- A sample used by clips: **Remove clips** deletes the sample and every clip of
  it, or **Cancel** keeps both. Clips cannot outlive their sample, because a
  session stores each clip as an index into the sample list.
- An instrument played by tracks: **Remove tracks** deletes those tracks, or
  **Keep tracks** keeps their notes and only clears the assignment, ready for
  another plugin.

Removing a sample also empties any drum pad loaded from it and renumbers the
list.

## Keys and panes

**F1**–**F9** open and close the menus (F1 File … F9 Record). **Tab** and **Shift+Tab** cycle forwards and
backwards through every pane of the main view: the sidebar, the pattern editor,
the inspector/mixer, the FX bus (when the mixer shows effect channels) and the
VST3 editor (while it is open). The pattern pane is skipped while the VST3 editor
covers it, and the editor keeps its own keys only while it holds focus, so Tab
moves out of it without closing it. `Ctrl+Shift+H/J/K/L` still moves between
panes by direction.

**Ctrl+F** toggles the focused Pattern or lower pane fullscreen. The View
sidebar remains at the left when it is shown, and an open virtual piano remains
at the bottom. **Ctrl+G** shows or hides the View sidebar; hiding it moves focus
to the Pattern pane, and showing it focuses the sidebar. **View → Reset layout**
restores the normal split and the sidebar.

**Space** starts and stops the sequencer from every pane, including the mixer,
the FX bus and the VST3 editor. Text entry, modal dialogs and an open menu keep
Space for themselves.

Inside a dialog, Tab keeps cycling that dialog's own panes.

## File browser

Every chooser (sessions, samples, VST3 bundles, MIDI-learn and preset files)
remembers the directory it last browsed for the rest of the run, so reopening it
returns there. The kinds are remembered separately: plugins, samples, presets and
sessions each keep their own directory, and plugin browsing starts at
`/Library/Audio/Plug-Ins/VST3` until you browse elsewhere. The memory is not
saved in `.mlack`, so it resets when mlacker restarts.

In the **Files** pane, Space marks the file under the cursor and moves to the
next one; Space again unmarks it. Marked rows are drawn a step brighter than the
rest, and Enter (or OK) opens every marked file instead of the one under the
cursor. Marks survive moving between directories and are cleared when the
chooser reopens. Multi-select is only offered where opening several files makes
sense: **Audio → Add audio**, **Instrument → Add instrument** (each bundle becomes its own
instance) and pad samples (which fill consecutive pads from the chosen key).
Session, preset, MIDI-learn, effect/insert and every save chooser stay
single-file, and Space does nothing there.

In the audio choosers (**Audio → Add audio** and pad samples), **Ctrl+P** plays
the file under the Files cursor on the master output, at full level and outside
every track. Pressing Ctrl+P again stops it. Moving to another file, or closing
the chooser, stops it too. It needs an audio output. With audio disabled, the
chooser's status line says so.

**View → Audio** works the same way. With the sidebar focused, **Ctrl+P** plays
the selected sample on the master output, and again stops it. Moving to another
sample, leaving the sidebar or the Audio view, or opening a dialog stops it too.
While the sidebar shows Audio, its Ctrl+P previews instead of playing the song
matrix. Ctrl+P from the pattern or the matrix still plays the matrix.

Browsing starts focused on the **Directories** pane on the left, where `j/k`
moves, Enter or `l` expands, and `h` collapses. Tab cycles Path → Directories →
Files → buttons, and Ctrl+Shift+H/J/K/L moves between them. Typing `/` or `~`
jumps to the path field and starts a fresh absolute path; Ctrl+U clears the field
and focuses it. Save choosers open in the path field instead, since they start
from a suggested filename. Relative paths typed into the field resolve against
the directory being browsed.

Version 1.2 stores all patterns and song order, track types and assignments,
NOTE/VEL/LEN/OFF/automation data, audio LEN/OFF and placements, loaded samples, loaded
instrument/master-plugin paths and normalized parameter states, BPM/time signature,
playhead/cursors, pane focus, track zoom, scroll positions, sidebar mode, mixer and
sample styles, meter update rate, and the parameter editor's selection/visibility.
Ctrl+S works with the parameter editor open. Uncommitted text-entry drafts are not
saved.

Decoded audio and waveform data are embedded: the original WAV/AIFF files are not
needed to reopen the session. VST3 binaries are **not** embedded and must remain
installed at the saved paths. Only open trusted sessions: opening one can load
native plugin code. The current audio/MIDI device selection is retained, rather
than reopening machine-specific device IDs from another computer.

Saves use a flushed, same-directory temporary file and atomic replacement. Parse,
version, missing-plugin, and parameter-restore failures retain the current session.
There is no autosave or unsaved-change prompt on Open/Quit yet. Native plugin
opaque presets, internal sample banks, and non-parameter controller state are not
stored in 1.0; exposed parameter values are restored by ID. Device changes still
reload plugins with defaults. See [the binary format](FORMAT.md) for the schema
and limits. Sessions and `.mlapatt` files saved as 1.0 still open: their tracks
keep both of their old CC columns.

**Pattern → Save pattern / Load pattern** uses `.mlapatt` files. Saving includes
the active pattern’s notes, track settings and embedded audio clips. Loading adds
and selects a new pattern without changing the song matrix. Instrument assignments
and effect routing are cleared so you can assign plugins in the current session.
The save dialog appends `.mlapatt` when needed.

In the Pattern pane, **Ctrl-Z** cycles vertical detail through **1/16 → 1/32 →
1/64 → 1/16**. Only original sixteenth-note rows have row numbers; intermediate
lines show `.` and use alternating darker backgrounds. Up/Down (or `j`/`k`)
visits each visible subdivision: the 1/64 view has three editable positions
between numbered rows. At these zoom levels, `dd` deletes one visible note-time
slot, and `o`/`O` inserts a blank slot below/above the cursor. Later notes shift by
one 1/32 or 1/64 step; adjacent subdivisions remain intact. The pattern grows if
needed to keep notes at the end. Main-row automation and audio keep their anchors. Enter edits a note, velocity, length or offset; Shift-J/K
adjusts values and Backspace clears the selected field. MIDI step entry on an
armed track also uses the selected subdivision. Notes entered between
rows are stored with timing offsets and play at every zoom level, including
1/16. They survive session and pattern save/load without increasing the pattern's
main row count. Chords at each visible position share horizontal note lanes.
Numbered rows become slightly brighter when finer notes are hidden at the current
zoom. Visual copy/cut/paste includes the selected subdivision positions and keeps
their timing spacing even when pasted at a different zoom or into another pattern.
The 1/32 notes are slightly darker and 1/64 notes darker again; note-length lines
expand with the view.

## Pattern visual selection

In normal Pattern mode, `o` inserts a blank row below the cursor and `O` inserts
one above it, selecting the new row while keeping the current column. Later rows
and audio clip starts shift down; clips already spanning that point keep their
duration. The maximum pattern length remains 16384 rows.

In the Pattern pane, `v` starts a rectangular selection at the current cell.
Use `h/j/k/l` to extend it; selected cells have a brighter background. `y` copies
the selection, `d` cuts/clears its cells without removing rows, and `p` pastes at
the current cell. `gg` and `G` extend an active selection to the first or last
visible position, including subdivisions. Use `gg v G y` to copy from top to
bottom, or `v gg` / `v G` to select toward either end from the cursor.
Backspace clears the selected cells without removing rows or replacing the
clipboard; `d` still cuts them. `Esc` or `v` cancels selection. The internal clipboard survives
pattern switches; it is not the operating-system clipboard or session data.

`Shift+V` selects whole rows across every track; `j/k` extends the row range.
`y` copies and `d` clears/cuts all writable cells, including hidden LEN/OFF and
automation fields. Row numbers and pattern length stay unchanged. To paste with
`p`, place the cursor at the first writable column of the destination row.
`Shift+V` again or `Esc` leaves whole-row selection.

Column-mode selection includes only visible, writable columns. Paste must fit the pattern and match
the destination column roles throughout (NOTE to NOTE, VEL to VEL, etc.). Invalid
pastes show an OK-only dialog and leave the pattern unchanged. Pasting does not
insert rows. New notes default to VEL 100, LEN 1.00 and OFF 0.00; explicitly copied
values override these defaults when those columns are included.

While selecting, `Shift+J/K` decreases/increases every nonempty selected value:
notes move by one semitone, VEL/automation by one, and LEN/OFF by 0.01 sixteenth
notes. If any value would exceed its limits, the entire step does nothing. Empty,
hidden and read-only cells stay unchanged; selection remains active for repeats.

## Instrument tracks

- **Instrument → Add instrument** browses `.vst3` bundles on disk. On selection, the host
  validates the first class marked `Instrument`, its MIDI input and supported
  audio buses. Effects and incompatible/broken bundles report an error without
  adding an entry. Only open trusted plugins.
- Each Instrument track shows its instance under the track name in the pattern
  view, numbered as in the Instruments list (e.g. `001 Mla Drum`), or
  `- unassigned -` until one is assigned.
- **View → Instruments** shows session-wide loaded instances, numbered by ID.
  `j/k`, `gg`, and `G` navigate; Enter assigns the selected instance to the
  current Instrument track. If another track already plays that instance,
  mlacker asks first. **New instance** loads another copy of the same plugin for
  this track, with its own pads, fader, meters and empty insert slots. **Share**
  links the track to the existing instance. Loading while an Instrument track is selected also
  assigns the new instance automatically.
- **Track → Create Instrument track** creates a note/velocity/LEN/OFF
  track using the selected library instance, unless another track in the pattern
  already uses that instance. The new track then starts unassigned, so it never
  silently shares another track's pads, fader and meters. An unassigned track
  stays silent until assigned through **Instrument → Add instrument** (a new instance) or
  Enter in the Instruments list, which asks whether to share or load a new
  instance.
- Up to 32 instances may be loaded, independently routed and summed before the
  master processor. Loading the same bundle again creates another instance.
  Assigning the same entry to multiple tracks shares plugin state and its 16 MIDI
  channels (track index modulo 16); use separate instances for isolated voices.
  Track/pattern duplication preserves the instrument ID rather than loading a
  new instance. Removing a track does not unload the library entry.
- Audio-device changes reload all instances at the new sample rate. Disabling
  output keeps them loaded in an offline controller.
- Live MIDI plays sample-accurately: each note and controller change sounds on
  the frame it arrived on, one audio block later (a fixed ~2.7 ms at 128 frames
  and 48 kHz, instead of up to a block of jitter).
- Live MIDI follows the selected Pattern-view track, even while another pane or
  menu has keyboard focus. Instrument tracks address their VST3 instance; MIDI
  tracks use the preview/master path. Audio, muted, and unassigned instrument
  tracks reject new notes. Live CC 0–127 and 14-bit pitch bend use the same
  selected destination and VST3 MIDI mapping as pattern controls. Input MIDI
  channels and note velocities (0–127) are preserved; velocity zero is note-off.
  A patch must have velocity sensitivity/modulation enabled to respond audibly.
  Held keys retain
  their original destination for note-off when selection or assignment changes.
- Every instrument's stereo PCM is summed before the master processor, gain,
  and output clipping. The rightmost **Master** mixer strip stays pinned while
  track strips scroll. Its L/R meters show measured output peaks with smooth
  decay, using the selected meter style and update rate—not MIDI velocity.

### Drum sampler pads

[Mla Drum](plugins/mla_drum) (and any instrument implementing
`stdlib/include/mla_sampler_protocol.h`) takes samples into numbered pads. Select
the loaded instance in **View → Instruments**, then:

- **Instrument → Drum pads → Send audio sample to pad** sends the sample selected in
  **View → Audio** to a key you pick. Pads go to the selected Instrument track's
  own instance (the picker title shows e.g. `Mla Drum #2`). For other tracks they
  go to the instance selected in the Instruments list.
- **Instrument → Drum pads → Load pad sample from file** picks the key first, then a WAV/AIFF.
  The file is added to the Audio list too.

Both open a piano keyboard. It spans the instrument's pads: pad 1 is the plugin's
Root Key (default MIDI 36, `C-2`), pad 2 one key higher, and so on. Keys outside
the pads are dimmed. The selected key is dark gray, and a red dot marks pads that
already hold a sample. The line under the keyboard names the key, pad and current
sample. `h/l` (or Left/Right) moves one key and `j/k` one octave. The picker opens
on the first empty pad. The **Insert sample** / **Choose file** button (or Enter)
uses the key, replacing any sample already on that pad. **Clear pad** (or
Backspace/Delete) clears a loaded pad, and **Cancel** (or Esc) closes the picker.
Tab moves between the keyboard and the buttons.

Pads can be loaded while audio is playing. For per-drum effects, load
one instance per drum family, give each its own Instrument track, and add
inserts to those tracks. `.mlack` saves which Audio sample each pad uses and
reloads the pads on open and after audio-device changes. Removing an instrument
forgets its pads. **Save plugin preset** on a sampler writes a kit preset that
embeds its pad samples. Loading it restores all pads and clears pads the kit
does not use.

### Sampler pane

**Shift+S**, or **View → Show sampler**, shows the Sampler pane in the lowest
pane of the Pattern view and focuses it. It edits the
[Mla Sampler](plugins/mla_sampler) instance on the selected Instrument track;
other tracks show a hint instead. Shift+S again hides it. The Sampler pane and
the virtual keyboard share the pane, so opening one closes the other.

Each of the 16 slots is one row with these columns:

- **Mode**: `Pad` or `Zone`. A pad plays one key, shown under **Low** (slot 1
  at the Root Key, default `C-2`, then one key up per slot).
- **Low** / **High**: a zone's key range.
- **Root**: the key that plays the sample at its recorded pitch.
- **Trk**: key tracking, `On` or `Off`. On pitches each key from Root; off
  plays every key at the recorded pitch.
- **VLo** / **VHi**: the velocity range (1-127) the slot plays, for pads and
  zones alike. Two slots on the same keys with ranges 1-63 and 64-127 switch
  samples by how hard a key is hit.
- The sample.
- **Loop**: `Off`, `Fwd` forward, `Bidir` bidirectional.
- **Start%** / **End%**: the loop points, in percent of the sample.
- **Xf%**: a forward loop's crossfade length, in percent of the sample. It
  smooths a clicking seam (see the Mla Sampler README).
- **Out**: the output bus (`Main`, `Out 2`..`Out 8`).

Overlapping zones layer.

| Keys | Action |
|------|--------|
| `j` / `k` (Down / Up) | Next / previous slot |
| `h` / `l` (Left / Right) | Previous / next column |
| `J` / `K` | Decrease / increase the column: choices and velocities by one, keys by a semitone, loop points by 1% |
| `[` / `]` | Keys by an octave, velocities by 10, loop points by 10% |
| Enter | Load a WAV/AIFF into the slot (added to the Audio list); several on a zone make a round-robin set |
| Backspace | Clear the slot |
| `o` | Route the slot's output bus (see Plugin outputs below) |
| `w` | Wave view of the slot (again returns to the table) |
| `v` | Key map of all slots (again returns to the table) |
| `e` | Next page: keys & loops, Sound, Filter, Filter 3/4, Filter Mod, LFO 1, LFO 2, Mod, Play, Vel/Tempo |
| `G` | Group mode: round-robin or random |
| `p` | Play the slot: a pad at its key, a zone at its root |
| `E` | Edit the slot's sample (see below) |
| `W` / `R` | Save the slot as a slot preset / load one into it (see below) |
| `C` | Slice the slot's sample into the slots after it (see below) |
| `A` | Auto-map note-named samples as key zones from the slot (see below) |

The **Filter page** (`e` from the Sound page) shows each slot's filter chain,
its first two stages: per stage its type (**F1**/**F2**: `Off`, `LP12`,
`LP24`, `HP12`, `HP24`, `BP12`, `BP24`, `Ldr12`, `Ldr24`, `Notch`, the
state-variable `SvLP`, `SvHP`, `SvBP`, `SvNt`, `Peak`, `LoShf`, `HiShf`,
`Vowel`, `Comb+`, `Comb-`, `Flang` and `Phasr`), **Cut** (20 Hz .. 20k), **Res** (dB), **Env** (octaves of filter
envelope), **Key** (key tracking, %) and **Gain** (dB, for Peak and the
shelves; dim otherwise). Then **FEnv** (`Inst` uses the instance's filter envelope,
`Own` the slot's) and its **Atk**, **Dec**, **Sus%** and **Rel**, editable
once FEnv is `Own`. `J`/`K` step the cutoff by about a semitone, resonance and
gain by 1 dB and Env by a tenth of an octave; `[`/`]` by an octave, 6 dB and an
octave. The instance filter envelope is edited like its amp envelope, in the
instrument's parameter editor. See the Mla Sampler README for the filter types
and how the list grows.

The **Filter 3/4 page** (`e` from the Filter page) shows **Chain**, how the
four stages connect (`Serial`; `Parall`: all side by side, summed; `2 x 2`:
stages 1-2 beside 3-4), then filter stages 3 and 4 (**F3**, **F4**) with the
same columns and keys as the first two.

The **Filter Mod page** (`e` from Filter 3/4) shows each stage's **Drv**
(drive, %: saturation into the stage) and **Mod** (-100 .. +100 % of the LFO
and mod-route cutoff movement it takes; +100 by default), dim while the stage
is Off. Opposite Mod values sweep two stages apart.

The **LFO 1** and **LFO 2** pages (`e` from the Filter Mod page, then again) set
each slot's two LFOs alike: **Shape**
(`Sine`, `Tri`, `SawUp`, `SawDn`, `Sqr`, `S&H`), **Rate** (0.05-20 Hz, dim
while synced), **Sync** and **Div** (a division of the tempo: `1/1`..`1/32`,
dotted `1/4.`, triplet `1/8T`), **Delay** (depth fade-in), **Pitch**
(semitones, vibrato), **Cut** (octaves of filter cutoff) and **Lvl%**
(tremolo), and **Trig** (`Retrg` restarts it with each note, `Free` keeps it
running). `J`/`K` step Pitch by a tenth of a semitone and Cut by a tenth of an
octave; `[`/`]` by a semitone and an octave. Depths start at 0, so an LFO does
nothing until one is set.

The **Mod page** (`e` from LFO 2) shows each slot's four mod routes: per
route its source (**Src**: `LFO1`, `LFO2`, `AmpEn`, `FltEn`, `Veloc`, `Key`,
and the MIDI controllers `ModWh`, `AftT`, `Bend` and `ModCC`; the instance's
Bend Range and which CC `ModCC` reads are in its parameter editor),
target (**Tgt**: `Pitch`, `Cut`, `Reso`, `Level`, `Pan`, `Start`) and amount
(**Amt**, -100 .. +100 %). A route's target and amount are dim until it has
both a source and a target. `J`/`K` step the amount by 1 %, `[`/`]` by 10 %.
See the Mla Sampler README for what each target moves at 100 %.

The **Play page** (`e` from Mod) sets how each slot plays notes: **Mode**
(`Poly`, `Mono`, or `Legato`, which keeps the playhead and envelopes when a
note is played over a held one), **Glide** (the slide from the previous note in
Mono and Legato; dim in Poly), **Uni** (1-8 unison voices per note), **Det**
(their detune, cents) and **Spr%** (their stereo spread). Det and Spr% are dim
with one voice. Then the pitch envelope, **PEnv** (semitones at its peak; `J`/`K`
a semitone, `[`/`]` an octave), **PAtk** and **PDec** (dim at depth 0), and the
sends, **SnA%** and **SnB%**: how much of the slot goes to the instance's Send A
and Send B buses as well, which feed aux effect channels 1 and 2 (see Plugin
outputs), so one instance can send its snare to a reverb and not its kick.

The **Vel/Tempo page** (`e` from Play) sets how velocity and key shape a
note's level, **VCurve** (`Linear`, `Soft`, `Hard`, `Fixed`), **Vel%** (its
depth; dim for Fixed) and **KeyL** (dB per octave from C-4), and tempo sync:
**Sync** (`Off`, `Repitch` changes speed and pitch, `Stretch` keeps the pitch,
`Beats` keeps the pitch and every hit's attack: for drums)
plays the whole sample in **Beats** (dim while Sync is Off) at the tempo.

The **key map** (`v`) draws every slot over the 128 MIDI keys, instead of the
page's table: an octave ruler, a **Lyr** row counting the loaded slots under
each key (blank where no slot plays: a gap; a dot for one; the count, in the
accent color, where slots layer), then a row per slot with its keys (`━`), root
(`◆`, or `●` for a pad) and velocity range. The header describes the selected
slot. The page's columns still edit, so moving a zone's Low or High on the keys
& loops page shows at once. Empty slots are dim.

**Slicing** (`C`) cuts the selected slot's sample into slices and puts
slice 1 on that slot, slice 2 on the next, and so on, each a one-shot pad
(Pad mode, loop off, whole slice, forwards), so the slices play on consecutive
keys, Akai-style. `C` shows the wave view with the cuts marked; `J`/`K` (`[`/`]`
by 4) set how many slices (2 up to the slots left), `t` switches between equal
slices, slicing at transients and slicing at the cuts (one slice per cut, see
Wave view), Enter slices, and `C` or Backspace cancels.
At transients the strongest rises in level are cut (at least 30 ms apart), just
before each onset; a sample with fewer transients gets fewer slices. The slices
join the Audio list as `name slice N.wav`; other settings of the slots they
land on stay.

**Auto-map** (`A`) opens the file dialog (Space marks several files) and turns
the samples whose names end in a note into key zones on the slots from the
selected one, lowest note first: `Piano_C4.wav`, `Strings F#2.aif`,
`PianoEb3.wav` (C4 = MIDI 60). Each zone has its sample's note as root and
reaches up to the next sample's note; the lowest reaches down to key 0 and the
highest up to 127. Files on the same note (`Snare_D2_rr1.wav`,
`Snare_D2_rr2.wav`) share that zone and a group of their own: a round-robin
set. Files without a note name and files past slot 16 are skipped, and the
status line counts them.

**Round-robin per zone**: slots in one group (the Sound page's **Grp**) take
turns on each note they share, round-robin or random (`G`), so repeated notes
do not sound machine-gunned. Enter on a Zone slot with several files marked
(Space in the file dialog) loads them into that slot and the ones after it,
gives each the zone's keys and velocity range, and puts them all in the
zone's group (or the first free one).

**Slot presets** keep one slot, its sample and every setting on all pages, in a
`.mlaslot` file. `W` saves the selected slot (the file name suggested is the
sample's); `R` loads a preset into the selected slot, whichever slot it was
saved from. The sample joins the Audio list; a preset of an empty slot empties
the slot. Settings a newer Mla Sampler added that the preset lacks keep their
values, and settings this Mla Sampler lacks are skipped.

The **Sound page** (`e`) lists each slot's **Level** (dB, `off` at the
bottom), **Pan** (`L50`, `C`, `R20`), **Tune** (semitones), **Env** (`Inst`
uses the instance's envelope, `Own` the slot's) and the slot's own **Atk**,
**Dec**, **Sus%** and **Rel**, **Ofs%**, where playback starts in the
sample, **Grp**, the slot's group (`-` or 1-8), and **Chk**, its choke group
(`-` or 1-8): a slot in a choke group cuts off that group's sounding notes,
its own included, like a closed hi-hat stopping the open one. **Rev** plays the
slot's sample backwards; its loop points and start then count from the end,
and the wave view shows the waveform reversed, as it plays. Slots in one group take
turns on a note instead of layering: round-robin or random, as the pane title
shows (`Groups: Round-robin`). `G` switches between the two. The four envelope columns are dimmed, and cannot be edited,
until Env is `Own`. `J`/`K` step level by 1 dB, tune by a semitone and the
rest by 1%. `[`/`]` step by 6 dB, an octave and 10%. A slot on its own
envelope keeps it, so a short pad and a sustained, looping zone can share one
instance.

`E` opens the slot's sample in the sample editor (see Sample editing), with
the slot's loop already selected, or nothing selected when the loop is off. A
reversed slot's loop is selected where it lies in the stored sample, with the
cursor at its end. `t` trims the sample to it. Saving a trim to exactly the loop destructively (Ctrl+S, then
Destructive) resets the slot's loop to span the whole new sample and its start
to the beginning, so it plays as before, without the audio outside the loop. The
edit changes the session sample, so every placement and pad using it updates;
other slots' loop points stay as they were.

**Wave view** shows the selected slot's waveform across the pane, with the
loop region highlighted, the crossfade region shaded, its start and end as `│`
markers and the sample start (where playback begins) as a dashed `┆` marker. The line above
names the slot and shows the loop mode, the loop start and end in frames (the
one being edited in brackets), the loop length, the crossfade and the zoom. Frames are exact:
a loop point set here plays from that frame.

| Keys | Action |
|------|--------|
| `m` | Edit the loop start, the loop end, the sample start or the cuts, in turn |
| `h` / `l` (Left / Right) | Move the marker one dot of the waveform |
| `H` / `L` | Move the marker one frame |
| `z` | Snap the marker to the nearest zero crossing (within 48000 frames) |
| `[` / `]` | Shorten / lengthen the loop crossfade by one dot (forward loops) |
| `=` / `-` | Zoom in / out, centred on the marker |
| `j` / `k` | Next / previous slot |
| `p` | Play the slot |
| `,` / `.` | Cuts: select the previous / next cut |
| `n` / `x` | Cuts: add a cut half way to the next / delete the selected one |
| `u` | Cuts: detect them again |

The **cuts** are the slot's slice markers, shown as dotted `┊` lines: where
its hits start, as Mla Sampler detects them when a sample loads. Beats tempo
sync plays from one cut to the next, and `C` slicing can slice at them. With
the Cut marker (`m` past the sample start) `h`/`l`, `H`/`L` and `z` move the
selected cut between its neighbours (the first stays at frame 0). Cuts set by
hand are kept with the session until the pad gets another sample, and `u`
brings back the detected ones. A reversed slot's cuts are not drawn.

`p` plays through the Instrument track like a key of the virtual keyboard, at
velocity 100 or the nearest velocity in the slot's range, so
you hear loop and zone edits without leaving the pane. Loop, level, pan, tune
and envelope edits change a held note as it plays. A zone plays at its root,
or at the nearest key of the zone when the root lies outside it. Other slots
whose key or zone holds that key sound too. On terminals that report key
releases (the kitty keyboard protocol, see Virtual keyboard) the note lasts
while `p` is held; elsewhere it ends once `p` stops repeating.

The loop keeps at least two frames. The waveform comes from the slot's sample
in the Audio list, so a slot filled only by a plugin preset has none. Enter
loads one.

While focused, the pane keeps every printable key, `q` included, plus Enter,
Backspace and the arrows. Tab, Space, the function keys, Escape, Ctrl
shortcuts and the Shift+M / Shift+P / Shift+S toggles still work. A pad slot's
Low, High, Root and Trk need Zone mode first. Zones, loop settings and outputs
are ordinary plugin parameters, so sessions, plugin presets and
automation keep them. Slots are saved like drum pads (see above).

#### Plugin outputs

A multi-output instrument such as Mla Sampler brings its extra output buses
(`Out 2`..`Out 8`) into mlacker. By default each one plays with the instrument's
main output, through the Instrument track's inserts, fader and sends. To give a
bus its own mixer channel, create an AUDIO track and route the bus there:
**Track → Route plugin outputs** on the Instrument track lists each bus with its
route (`MAIN`, `MST` or `A3`). Pick one, then its destination. In the Sampler
pane, `o` does the same for the selected slot's bus. The Out column then shows
the route, e.g. `Out 2>A3`. A routed bus skips the instrument's own inserts,
fader and sends and takes the destination track's instead; routed to `MST` it
goes straight to the master bus. Routed to an aux effect channel (`FX1`..`FX8`)
it feeds only that effect's input, as a send (nothing plays while the channel
has no effect), and `OFF` silences it. A bus named `Send ...`, such as Mla
Sampler's Send A and Send B, is a send: it defaults to aux effect channel 1,
2, and so on instead of joining the main output (choosing `MAIN` for it picks
that default again). Routes belong to the Instrument track in each
pattern, like its output channel, and `.mlack` saves them.

#### Instrument inputs

An instrument with an audio input, such as [Mla Vocoder](plugins/mla_vocoder),
can take an AUDIO track's clips as that input. Put the voice or sample on an
AUDIO track, load the plugin on an Instrument track, then use **Track → Set
instrument input** on the Instrument track and pick the AUDIO track (or `none`).
The Instrument track's notes play the plugin while the clips feed it. For
example, Mla Vocoder vocodes the voice with the chords you write. Choose
`Audio input` instead to feed it the live input from **File → Settings → Audio
input**, for example a microphone into Mla Vocoder played from a MIDI keyboard.

The input is taken from the AUDIO track's clips before that track's inserts
and fader, and the track still plays on its own channel. Pull its fader down
(or mute it) to hear only the instrument. An instrument with an input replaces
what it is given with its output, so the input is not heard twice. Tracks
sharing one instance share its input. The route belongs to the Instrument
track in each pattern, and `.mlack` saves it (the `INSTRUMENT_INPUTS`
extension).

### Sample editing

**Audio → Edit sample (destructive)**, or `e` in the Audio list, opens the selected
sample in an editor over the Pattern view. Edits work on a copy with 16 undo
steps; nothing changes in the session until you save.

The editor uses the same keys as the Sample pane (`s` over a clip in the
Pattern view; see docs/interface.md). A cursor moves over the waveform and
stays in the middle of the view while you zoom. A status bar at the bottom shows
the sample's name, the cursor position, the selection and whether transient
mode is on. `v` starts a selection at the
cursor, and the selection then follows the cursor until `Esc` unselects.

| Key | Action |
|-----|--------|
| `h` / `l` | Move the cursor one column of the view |
| `H` / `L` | Move an eighth of the view |
| `Ctrl+H` / `Ctrl+L` | Jump to the previous / next transient |
| `Shift+T` | Transient mode: `h`/`l` step between transients, `H`/`L` between strong ones (like bass drums); marked ▼ strong, ▿ others |
| `Shift+B` / `Shift+E` | Jump to the beginning / end of the sample |
| `v` | Start a selection at the cursor |
| `Esc` | Unselect; with nothing selected, close the editor (asks once if an edit is unsaved) |
| `t` | Trim: keep only the selection |
| `x` | Delete the selection, closing the gap |
| `u` | Undo |
| `+` / `-` | Zoom in / out around the cursor |
| `=` | Fit the whole sample in the pane |
| `Ctrl+P` | Play the selection, or the whole sample without one; again stops |
| `Ctrl+S` | Save as a new clip |

**Ctrl+S** always saves the result as a new clip in the Audio list, named
`SampleName - Trim 1.wav` (the first free number; a trim of a trim numbers on
from the original name). The dialog lets you change the name and pick a mode:

- **Non-destructive** adds the new clip and leaves the original sample, its
  pattern placements and drum pads as they were.
- **Destructive** puts the new clip in place of the original: it takes the
  original's place in the Audio list and in every pattern placement, and every
  drum pad that used the original is re-sent at once. Placements whose length
  changed return to the natural length.

The new clip is saved with the session (in a project, as a WAV in `Audio/`).
The source WAV/AIFF on disk is never rewritten.

### Mixer faders and MIDI recording

Press `m` to show Mixer, then focus it with Tab (or `Ctrl+Shift+J`) from Pattern
view.
`h/l` or Left/Right selects tracks. Each strip has a vertical volume fader beside
its VU meter, with the 0–100 value underneath. `Shift+J/K` lowers/raises volume;
these keys still edit cells when Pattern view has focus. Gain affects preview,
PCM voices and VST instrument output. Tracks sharing a loaded VST instance share
its output gain; changing one linked fader updates the others in the pattern.

Instrument strips also show `M LR`: MIDI velocity beside measured left/right
plugin PCM peaks, after the instrument fader and before the master chain. PCM
meters share the meter style, update rate, and smooth decay. Tracks using the
same loaded instance display the same cached stereo output readings.

`Shift+R` in Mixer arms the selected track (red `R`). MIDI and Instrument tracks
record MIDI, and AUDIO tracks record the audio input (see
[Recording audio](#recording-audio)). Press
Space to record from the selected row; press Space again to stop and enter the
track name (Enter accepts, Esc keeps the old name). Each take targets one synth
track, with up to 64 automatically added note lines. Notes on the same row are
inserted in ascending pitch order with their velocities and timing intact.
LEN captures key-hold duration. OFF captures early/late timing relative to the
nearest sixteenth-note row, to 0.01 row precision. All recorded cells remain editable.

The **Record** menu has two options, enabled by default:

- **Play metronome**: clicks at the current tempo and time signature, with a higher
  first beat and lower remaining beats. An armed recording starts after one full
  bar of count-in; pattern playback and note capture wait until it finishes.
  Turning this off starts recording immediately. **Metronome → On recording**
  (default) limits clicks to recording and its count-in; **Always** also clicks
  during normal pattern and song playback. These modes are mutually exclusive.
- **Extend pattern when playing**: grows the pattern while recording (up to the
  pattern limit of 16,384 rows). With it off, recording loops at the existing
  length, replacing the armed track's notes as each row is reached. Other tracks
  remain intact. The choice takes effect at the start of a take.

Press Space to stop, including during the count-in.
The live MIDI path monitors the synth during recording.

While stopped, incoming notes still provide single-cell step entry on the armed
track. Audio tracks do not record MIDI. Opening a modal editor stops a timed take.
Volume is saved in `.mlack`; record-arm is transient and starts off on load.

#### Bouncing

**Record → Bounce selection to sample** renders part of the song offline,
faster than real time, through the whole mix: instruments, inserts, sends,
aux effects and the master chain. The result is added to the Audio list as
`Bounce N.wav`, ready to place on an AUDIO track or to edit.

- **In Pattern view**, a visual selection sets the rows, and a block selection
  (`v`) also limits the bounce to the tracks it spans; whole-row selection
  (`V`) keeps every track. Without a selection, the whole pattern is
  bounced.
- **In the song matrix** (when it has focus), `v` marks rows from the cursor.
  Move to extend the mark, then bounce. Without a mark, the cursor row is
  bounced. Each marked row plays the way song playback plays it, one after
  another without gaps, and rows without patterns are skipped.

Notes and clips start on their exact frames. At the end of the selection,
held notes are released and clips stop. The audio device pauses while the
bounce renders and resumes afterwards. Stop playback before bouncing.

The **Audio** menu sets how a bounce ends and starts:

- **Bounce tail** (on by default): keep rendering after the selection, with
  no new notes or clips, until the mix stays below about −90 dBFS for half a
  second, or for at most **Bounce tail length** (5, 10 or 20 s). Release,
  delay and reverb tails ring out into the clip, past the selection's last
  row, and the silent end is trimmed. Turned off, the clip ends exactly at
  the selection's end.
- **Bounce second pass (seamless loop)** (off by default): render the
  selection once to warm up delays, reverbs, choruses, vocoder envelopes and
  so on, then render it again without resetting anything, and keep only the
  second pass. Its start then carries the first pass's tail, so the clip
  loops (or sits mid-song) without effects starting from silence. Each pass
  restarts the plugins' transport beat at the selection's start, so
  tempo-synced effects line up. For matrix rows, the warm-up is the same
  rows, not what comes before them in the song. With **Bounce tail** on as
  well, the clip also gets a tail at the end.

#### Recording audio

An armed AUDIO track (`Shift+R` in Mixer) records the audio input chosen in
**File → Settings → Audio input**. Press Space to play the pattern from the
selected row. After the count-in (with **Play metronome** on), the take
starts at that row. Press Space again to stop. The take becomes a sample,
`Recording N.wav`, in the Audio list, placed on the armed track at the row
where it started. If it would overlap a clip already there, it stays in the
Audio list only. Sessions save it like any other sample.

The take is shifted by the latency CoreAudio reports for the output and input
devices, plus mlacker's input buffering, so it lines up with what you heard
while playing along. The pattern keeps looping during a long take (audio takes
do not extend it), and the take runs on across the loops. Only the first armed
AUDIO track records, and a MIDI take on an armed MIDI or Instrument track can
run at the same time. mlacker does not monitor the input itself. Use your
interface's direct monitoring, or route the input into an instrument (see
[Instrument inputs](#instrument-inputs)) to hear it processed. If Space reports
that the armed track needs an audio input, choose one in Settings first.

### Output channels

The bottom row of every mixer strip names where the channel goes, under the
`0–100` fader value:

| Row | Meaning |
|-----|---------|
| `OUT:MST` | Straight to Master (the default on every channel) |
| `OUT:A3` | Into audio track 3, which then applies its own inserts, fader and sends |
| `OUT:I4` | An instrument track: its notes play instrument instance 4, whose audio goes to Master |
| `I4>A3` | Instrument 4, routed on into audio track 3 |

**Track → Set output channel** lists the destinations for the selected track and
routes it with **OK** or Enter (`j/k` selects, **Cancel** or Esc closes it). What it offers depends on the
kind of track:

- A **MIDI track** picks the instrument its notes play: `MST` keeps the built-in
  preview tone, `I1`, `I2`, … assign a loaded instance, exactly like Enter in the
  Instruments view. Picking an instance another track already plays shares it
  (same pads, fader and inserts), which the status line reports.
- An **instrument or audio track** picks the audio channel it feeds: `MST` or any
  other audio track. Master is always the default.

A track cannot feed itself, only audio tracks take other channels, and a route
that would loop back is refused. Tracks sharing one instrument instance share its
output channel, as they already share its inserts and sends. Deleting a track
clears the routes into it and shifts the ones past it. Routed audio arrives before
the destination's inserts, so the destination's chain, fader and sends all apply
on top of the source's own. Sends stay post-fader on the source itself.

A channel that something is routed into meters what it actually puts out: its
`L`/`R` bars show the measured post-fader output of the whole channel, its own
clips and every source routed into it, instead of the levels derived from its
pattern. The same measured meter appears on channels with inserts. Channels that
mix straight into Master with neither keep their pattern-derived meters, and an
instrument strip's `M LR` pair still shows that instance alone, before its
destination's chain.

Output channels are saved in `.mlack` per pattern track.

### Virtual keyboard

**Shift+P**, or **View → Show virtual keyboard**, shows a piano in the lowest
pane (over the inspector, mixer or spectrum analyzer) and focuses it, so you can
play without a MIDI keyboard. Notes go where a MIDI keyboard's would: the armed
track, else the selected one. MIDI tracks play the preview synth and Instrument
tracks play their instrument. Audio and muted tracks do not play. The pane title
shows the octave, the playable range and the target track.

Keys use the tracker layout, and each playable key shows the computer key that
plays it:

| Keys | Notes |
|------|-------|
| `z s x d c v g b h n j m` | C to B of the current octave |
| `, l . ; /` | C to E one octave up |
| `q 2 w 3 e r 5 t 6 y 7 u` | C to B one octave up |
| `i 9 o 0 p` | C to E two octaves up |
| `Left` / `-` | Octave down (0–8, default 3: `z` plays C-3) |
| `Right` / `=` / `+` | Octave up |

`C-2`, `C-3`, `C-4` … label each octave under the keys. Keys outside the
playable range are dimmed, and a sounding key is darkened like the selected key
in the drum-pad picker. Each key plays from key-down to key-up, so chords and
overlapping notes work. While the keyboard shows, mlacker asks the terminal to
report key releases through the kitty keyboard protocol. kitty, Ghostty,
WezTerm (with `enable_kitty_keyboard`), foot, Alacritty and recent iTerm2
support this. Terminals without it, such as macOS Terminal.app, send only
presses. There a note sounds while its key auto-repeats and ends about 400 ms
after the last repeat. Only the most recently pressed key repeats, so chords
fade. The notes join the MIDI input. An armed track records them while
playing, and step-enters them at the cursor while stopped. **Space** still
starts and stops playback. A take played from the keyboard leaves the keyboard
focused.

While the keyboard has focus, it keeps printable keys for itself, including `q`
(quit with `Ctrl+C`, or leave the pane first). Tab, Shift+Tab,
`Ctrl+Shift+H/J/K/L`, F1, `Shift+M` and control shortcuts keep working as usual.
The widget behind it is `tui::piano::PianoKeyboard`, which the drum-pad picker
uses too.

### Spectrum analyzer and master bus

`Ctrl+Shift+M` (from the sidebar, Pattern view or Mixer), or **View → Show
spectrum analyzer**, shows the analyzer in the Mixer's pane, together with the
master bus: a high-pass, a four-band EQ and the master volume. They are the last
stage before the output device, after aux returns and the master plugin, and the
analyzer shows exactly what leaves it (the same signal as the master meter).

The graph spans 20 Hz–20 kHz on a log axis, labelled underneath (`100Hz`,
`300Hz`, `1kHz`, …). Levels are dBFS, tilted +3 dB/octave around 1 kHz so music
reads evenly; bars rise at once and fall at 40 dB/s, with a fading `▒`/`░` trail
up to their recent peak. **View → Spectrum analyzer → Change details** cycles:

- **Blocks** (default): one bar per column, with eighth-block tops for detail
  finer than a cell.
- **Braille (fine)**: two bars per column and four dots per cell, with a
  floating peak dot.
- **Wide bars**: two-column bars with gaps.

**Update rate** sets how often it analyses (1–60 FPS, default 30), independent
of the meter rate.

To the right are the controls: **HP** (a 24 dB/octave high-pass, 20 Hz by
default so inaudible rumble never reaches the output), **EQ1–EQ4** (peaking
bands at 100 Hz, 500 Hz, 2.5 kHz and 8 kHz, ±12 dB, each with its own Q) and
**Master** with the volume slider (0–150%, 100% unity) beside the master L/R
meters. With the pane focused:

| Key | Action |
|---|---|
| Left / Right | Select HP, EQ1–EQ4 or Master |
| Enter | On an EQ band, cycle gain → frequency → Q (the selected value is highlighted) |
| Up / Down or `k` / `j` | Fine step: 0.1 dB, 1/6 octave, 1/12-octave Q, 1% |
| `K` / `J` | Coarse step: 1 dB, an octave, half-octave Q, 6% |
| `0` | Reset the selected value |
| `Shift+E` | Switch the EQ (high-pass and bands) in or out |

Stepping the high-pass below 10 Hz switches it off; stepping up turns it back on.
`Shift+E` bypasses the whole EQ without losing its settings: the `EQ` badge at
the top right of the graph is lit white on red while the EQ is in, like MIDI
learn's `L`, and its strips are dimmed while it is out. The switch crossfades
over about 20 ms; the master volume applies either way, and the analyzer always
shows the output after the EQ stage.
Changes glide over about 30 ms, so adjusting them while playing does not click.
Wider bands have a lower Q. The analyzer view, its detail and rate, and the
master bus are saved in `.mlack` (`MASTER_BUS`).

### Track inserts

With Pattern view focused, `f` toggles four insert slots between each track name
and its NOTE/VEL column headers. While shown, `h/l` selects a track and `j/k`
selects one of its four slots. Enter browses for a VST3 audio effect in an empty
slot, or opens the existing effect's parameter editor. **Effect → Edit effect
plugin** also edits the selected insert. Backspace removes the slot's assignment;
`f` returns to normal pattern navigation. Loaded plugin names appear in the slots.
`Ctrl+J` / `Ctrl+K` move the selected effect one slot down or up the chain. If
that slot already holds an effect, the two swap places. The selection follows
the moved effect, and the audio chain changes at once, including while playing.

Inserts process serially from top to bottom, before channel volume and aux sends.
They work on sample tracks, MIDI preview audio, and VST instrument outputs.
Tracks assigned to the same VST instrument share its output and insert chain.
Pattern copies retain their insert assignments; a duplicated non-instrument track
starts with empty inserts so it does not reuse another audio stream's processor.
`.mlack` preserves assignments, exposed parameters, and the active insert view;
audio-device changes restore parameters as well. Up to 256 insert instances are
retained per session. Detached instances are reused on the next load only when no
pattern references them, so removing an insert does not alter another pattern.

### Aux effect channels

Choose **Effect → Add effect channel** to create an aux return (up to eight).
Effect strips occupy a separate bank before the pinned Master strip; ordinary
tracks and effects scroll independently when they do not fit. In Mixer, `l`
from the last ordinary track enters the effect bank; `h` from the first effect
returns to the tracks. `Shift+J/K` adjusts the selected return's volume.

Press Enter on an empty effect strip to browse for a VST3 effect, or on a loaded
strip to edit its parameters. The same actions are available in the Effect menu.
**Remove effect plugin** empties the selected channel without removing its strip.

Select a source track in Pattern view, then select the desired effect strip and
choose **Effect → Set track send** (0–100%). In Mixer, `z` expands/collapses the
selected track's FX sliders beside its volume and meters. `h/l` moves between
volume and the `FX1`, `FX2`, … sliders; moving past the last slider selects the
next channel. `Shift+J/K` adjusts the focused slider. Wide strips scroll their
FX controls to keep the selected slider visible. Expansion is per track and
transient; switching patterns resets it, while send values remain saved.

Sends are post-fader: 0% is dry, 100% fully wet through the loaded effect.
With multiple FX, each receives its own send amount, while the dry gain is
`1 - max(loaded FX sends)`. All effect returns are summed into Master.
Empty FX slots do not attenuate the dry path.
For reverb/delay aux use, set the plugin's wet mix to 100%; mlacker does not force
plugin-specific wet/dry parameters. Effects continue processing silence for tails.
Effect-to-effect sends are not supported.

Send levels belong to each pattern. Tracks sharing a VST instrument instance
share its PCM send levels, just as they share output gain. `.mlack` saves effect
paths, exposed parameters, return levels, sends, and effect-bank selection.

## Use a master plugin

### Instrument parameter editor

Select an entry in **View → Instruments**, then choose **Instrument → Open VST3
editor**. This opens a terminal parameter editor over the Pattern pane (not the
plugin's native graphical window). Parameter names, step counts, read-only flags,
and initial values are copied into memory when the plugin loads.

- Arrow keys or `h/j/k/l` browse sliders. Moving between columns scrolls horizontally to keep
  the selected control visible; the bottom bar shows the horizontal position.
- `Shift+L` arms MIDI learn, displaying a white **L** on red. Move a synth CC control
  to bind its channel/CC to this parameter; capture ends listening. Press `Shift+L`
  again, move to another parameter, or close the editor to cancel listening.
  Read-only parameters cannot learn. Learned CCs control the assigned instrument
  independently of track focus and replace ordinary selected-track CC routing
  for that channel/CC. Values span the parameter range, rounded for discrete controls.
  Bindings belong to the session, survive output changes, and save in `.mlack`.
  New sessions start unmapped; removing an instrument removes its bindings.
- **Instrument → MIDI learn → Save / Load MIDI learn** exports/imports mappings
  for the selected entry in Instruments as a `.mlalearn` file. Saving suggests
  `<plugin name> - `: type a suffix, or Ctrl+U to replace the whole filename/path.
  The extension is appended automatically when omitted. Loading replaces that
  instrument's mappings, matching stable parameter IDs rather than session slots.
  Different plugin names, missing/read-only parameters, malformed files, and CCs
  owned by another instrument are rejected without changing existing mappings.
  Plugin parameter values are not included; `.mlack` still saves session mappings.
- **Instrument → Presets → Save / Load plugin preset** saves/restores exposed
  parameter values in `.mlapre` files for the selected instrument. The suggested
  `<plugin name> - ` prefix is editable (Ctrl+U replaces it); omitted extensions
  are appended. Loading validates the complete file, matches stable parameter
  IDs, and pauses audio/MIDI input while applying values. MIDI-learn mappings
  remain unchanged. These are parameter presets, not opaque VST3 state: internal
  sample libraries and other non-parameter plugin state are not included. The
  exception is sampler pads loaded from mlacker (Mla Drum): the preset becomes a
  kit that embeds those samples, and loading it restores every pad (see
  **Drum sampler pads**).
- `Shift+J` decreases and `Shift+K` increases the selected value.
- Enter opens manual entry; Ctrl+U clears the field and Enter validates/commits.
  Invalid input stays in the field. Esc cancels entry; Esc outside entry closes
  the editor and restores the previous pane focus.
- Continuous parameters use VST3's normalized `f64` range 0–1, with 0.01 steps;
  discrete controls use `i32` indices 0–stepCount. These are not physical Hz/dB
  units. Read-only parameters cannot be edited. The public `tui::slider::Slider`
  widget checks numeric type and min/max bounds and supports i32/u32/f32/f64.
- Accepted edits use the existing preallocated audio-event/parameter queues.
  The editor captures navigation, so the pattern and library do not move.

**Instrument → Remove instrument** stops playback, joins the MIDI worker, stops
audio, unloads the selected instance, and clears its assignments in every
pattern. Other instrument IDs remain unchanged; vacant slots can be reused.
Saved `.mlack` sessions restore exposed parameter edits across application
restarts and audio-device changes. Dynamic parameter-list changes and plugin-originated
parameter notifications are not handled yet; reopen to refresh cached values.

### Pattern CC columns

Each MIDI track has its own list of automation columns after its note lines, one
per controller, headed by what it plays: `CC1`, `CC74`, `PB` for pitch bend,
`AT` for aftertouch (channel pressure).
A new track has only `CC1` (modulation wheel). **Track → Automation → Add CC
column** adds one for `cc:N` (0–127; values 0–127), `pitchbend` (values
-8192–8191), `aftertouch` (values 0–127) or a custom `name:min:max`, up to 16 per track and one per
controller; **Configure CC column** changes the selected one (the first one
when the cursor is elsewhere) and **Remove CC column** deletes the selected
one with its values after confirmation. AUDIO tracks have no CC columns.
Columns belong to the track in its pattern, so every pattern can automate
different controllers.

A cell holds one value, sent on its row, or four 1/64-note steps written as
`12 . 31 40` (`.` is an empty step). At 1/64 vertical detail (Ctrl-Z) each line
of a row is one step: Enter and Shift-J/K edit the step under the cursor. At
1/16 detail Enter edits the whole cell text and Shift-J/K moves every step; a
value followed by `+` has more steps between the lines shown, and its row
number is highlighted. Values are sent even without a note or when the columns
are collapsed. Instrument tracks target their assigned instance (Track → Set
output channel chooses it); MIDI tracks target the master plugin. Muted tracks
do not send these events. Empty cells leave the current parameter value unchanged.

### Comment (TEXT) columns

**Track → Add / remove comment column** adds a `TEXT` column after the
selected MIDI or Instrument track's CC columns, or removes it with its texts.
Press Enter on a cell to type free text (up to 240 characters). The column
widens to its longest text, up to 48 cells, and is shown in the third column
stage with the CC columns (`z`).

On an Instrument track that plays [Mla Speech](plugins/mla_speech), a row's
text is spoken: with the row's first note, at its pitch and velocity, or at C-3
when the row has no note. The phrase plays to its end, however long the row
is. The host stores the text in the instrument's phrase ring
(`instrument_text`) and posts an `InstrumentText` event just before the
note-on. The VST3 host sends it as a note-expression text event
(`kTextTypeID`) of that note, so any VST3 instrument that reads note text
receives it. On other tracks, the column is only a comment.

**Curves between two values.** Put the cursor on a CC value and press
**Ctrl+V** to mark it (amber), then mark a second value in the same column
(Ctrl+V on a marked value unmarks it; unzoomed, a cell's first value is
marked, at 1/64 zoom the step under the cursor). **Ctrl+A** opens the curve
dialog: **Linear** (the default), **Logarithmic** (rises fast, then settles),
**Exponential** (starts slow, then speeds up), **S-curve** (eases in and out)
or **Inverse S** (fast at both ends, flat in the middle), with a preview and
OK/Cancel. OK ramps from the first value to the second through every 1/64
step between them, writing a step only where the value changes, so a slow
ramp leaves the steps in between empty and replaces what was there.

While recording, every controller and pitch-bend message from the MIDI input is
written to the armed track at the nearest 1/64 note, in the column for that
controller. A controller the track has no column for gets a new one, shown at
once: turning the filter cutoff knob (CC74) during a take adds a `CC74` column
holding the movement. Later messages in the same 1/64 step replace earlier ones.

A CC that is MIDI-learned to a parameter of the track's instrument drives that
parameter during playback, exactly as the knob did while recording; the learn
binding of the track's own MIDI channel wins, else the first channel binding
that CC to that instrument. Other controllers go to the plugin as MIDI.

The host translates those controllers using the plugin's `IMidiMapping` assignments
for event bus 0 and the track's MIDI channel. Assignments are cached at load time;
unmapped controllers and custom `name:min:max` slots are not sent. Plugins without
MIDI mappings cannot receive these controls yet. Parameter queues are bounded and
preallocated, with sample offsets preserved by the native audio event queue.

### Master slot

1. Select MIDI input, master output, buffer size, and sample rate in **File → Settings**.
   Buffer choices are 32–4096 frames (samples per channel), default 128.
   Smaller buffers reduce latency; larger buffers allow more processing time.
   Sample rates are Device default, 44.1, 48, and 96 kHz. The explicit rate sets
   audio-engine and plugin processing; CoreAudio converts to the device rate if
   needed. Device default keeps the native device rate.
   Both selections survive device changes and session opens during this run.
   After applying, the status shows the actual device buffer size and sample rate.
   **CPU cores for audio** limits how many threads render plugins. It lists every
   core the OS reports, and on Apple silicon the label also gives the performance
   and efficiency core counts. The default is All cores. Instruments (with their
   inserts), tracks at the same routing depth, and aux returns render in
   parallel. They are summed in slot order, so the mix sounds the same with any
   setting. The limit applies to running audio at once, without restarting the
   device.
   **Audio input (AUHAL)** opens an input device (Disabled by default, the
   system default, or a named device) that instruments can take as their input
   (see [Instrument inputs](#instrument-inputs)). It runs on its own device
   clock and is read about one buffer behind, resampled to the engine rate, so
   it may be a different device from the output. It needs audio enabled.
   macOS asks for microphone access for your terminal app the first time. If
   it was denied, allow the terminal under System Settings → Privacy &
   Security → Microphone, or the input stays silent.
2. Choose **Effect → Master → Load master VST3**.
3. Select a `.vst3` bundle, or type its full path into the dialog and press Enter.
   The chooser starts in `/Library/Audio/Plug-Ins/VST3`; user plugins are commonly
   under `~/Library/Audio/Plug-Ins/VST3`. Matching bundles are selectable items,
   not directories to navigate into.
4. Play MIDI input or press Space to play pattern MIDI through the plugin.
5. **Effect → Master → Unload master VST3** restores the reference sine preview instrument.

The first audio-processor class in a bundle is loaded into one master slot.
Zero-audio-input plugins act as instruments and replace the reference sine
voices. Mono/stereo effects process the master mix instead. Notes retain MIDI
channel, pitch, velocity, and sample offset. Sequencer tracks use channels modulo
16; live MIDI retains its input channel. Audio can be disabled while loading a
plugin for inspection; its name appears in the Inspector, with no hardware open.
Failed replacement keeps the previous plugin. Audio-device changes reload the
selected plugin for the new sample rate and restores its exposed parameter values.
Opaque, non-parameter plugin state is not preserved by device changes yet.

Only load plugins you trust. Plugins execute in-process; they are not sandboxed.
A plugin can display its own authorization UI, block, or crash the tracker.

## Audio path and lifecycle

The main/sequencer and MIDI-worker producers keep separate preallocated SPSC
queues. The AUHAL consumer drains a bounded batch and converts MIDI events to
VST3 events with offsets inside the current block. The host preallocates audio
and event buffers and does not load modules, allocate, log, or take locks in its
render path. Third-party plugins must independently satisfy realtime constraints.
Panic/queue overflow sends bounded note-offs covering every channel and pitch.
Failed processing silences the block, increments an atomic error counter, and
reports the failure to the UI without logging from the callback.

Module loading, component/controller initialization, bus negotiation, activation,
and teardown run on the main thread with audio stopped. The module remains loaded
until its component, controller, processor and connections have been released.
The VST3 SDK is linked only into mlacker, not into the TUI library or stdlib.
`stdlib/include/mlang_audio_processor.h` is the SDK-independent native bridge.

Current scope:

- macOS, native-architecture VST3 bundles; float32 mono/stereo, at most one audio
  input bus and one output bus, and the first MIDI input bus.
- One master slot plus 32 instrument slots, eight aux returns, and four inserts
  per track; no native plugin editor windows, opaque preset
  persistence, arbitrary named-parameter automation, sidechains, or
  latency compensation yet.
- Every plugin's `ProcessContext` carries the sequencer tempo and quarter-note
  position (`projectTimeMusic`), with `kPlaying` while the transport runs (after
  any count-in), so tempo-synced effects such as Mla Delay and
  [Mla Stutter](plugins/mla_stutter/README.md) follow the BPM. The position
  is sent on start, stop and tempo changes; the audio thread advances it
  sample-accurately in between. Playback starts at the starting row's beat
  (four rows per beat) and keeps counting across pattern loops and matrix rows. There is no time
  signature, bar position or loop range yet.
- Existing UI-loop sequencer timing is retained. The audio API supports absolute
  frame scheduling, but a look-ahead sequencer is still future work. Because the
  UI loop drives it, a note can start up to one frame late, never a loop late:
  each pattern is scheduled from the row it starts on, so the row that starts
  playback, and row 1 of a pattern the matrix moves to, play their own notes even
  though the update that schedules them arrives a frame after the transport did.
  A pattern keeps its own grid, so matrix rows of different lengths stay in step.
  Audio clips follow the same rule: a matrix row change silences the previous
  row's clips and re-arms the new pattern's from the row it starts on. Arming a
  pattern never stops the audio device: clip PCM is registered while it renders
  and cached per clip for the session, so effect tails and instrument voices
  survive starting playback and every matrix row change. Only a full sample
  table (256 clips) reloads the hard way.
- Pattern audio samples feed Master directly, respecting track mute/volume, start
  row and LEN. Starting inside a sample seeks into its embedded PCM; stopping
  playback releases its voices. FX sliders blend the dry path with parallel
  effect returns. PCM is copied while audio is stopped before playback
  (up to 256 placements per pattern); the callback receives lock-free events only.
- AUHAL's selected buffer-size request is not a measured end-to-end latency guarantee.

## Tests

```sh
./build.sh --test              # build, then test mlacker and every plugin
./build.sh --app --test        # mlacker only: MLang unit tests, then CTest
```

CI (`.github/workflows/build.yml`) runs the same on macOS for every push and
pull request, building against MLang's `main` from GitHub; a manual run can pick
another MLang branch. For mlacker's tests, `--test` runs `mlang pkg run test`,
which runs the MLang unit tests in `tests/*.mla`, then builds two local test bundles (never installed in system plugin folders),
loads them through the real module loader, and checks instrument/effect output,
frame-accurate event offsets, live MIDI lane independence, panic, failed
replacement, repeated unload/reload, instrument-only validation and independent
instrument-slot mixing. Four PTY tests cover Settings, plugin selection/load/unload,
Instrument track creation/assignment, Instruments view, existing widgets and
playback. They open no audio devices.
The session PTY test separately checks real empty startup, command-line opening,
parameter/editor-state round trips and rejected files. The spectrum PTY test toggles the
analyzer, edits the master bus and checks it survives a session round trip. The
virtual keyboard PTY test toggles the keyboard, changes octave, step-enters a note
and checks that Space and `q` behave. The Sampler pane PTY test (run when
`MlaSampler.vst3` is built) loads and clears slots, edits loops, outputs and
every page, saves a slot preset and loads it into another slot, checks that
the edits survive a session round trip, then slices a slot, auto-maps a
note-named sample and shows the key map; it also edits cuts and checks
that they survive the round trip. Legacy widget tests opt in
to seeded demo data with `MLACKER_DEMO=1`; normal mlacker startup does not.

For a hardware-free manual run:

```sh
MLANG_TUI_NO_HARDWARE=1 build/cmake/bin/mlacker
```

Run that command from this directory. The `mlacker_ui` unit tests need no SDK;
run them alone with `../mlang/build/mlang --tests tests` (or the `unit-test`
task with the same `--option mlang_root=...`).

### Effect plugin presets

**Effect → Presets → Load plugin preset / Save plugin preset** uses `.mlafxpre`
files. Select an effect channel in Mixer, a loaded insert in the track's insert
rows, or open the desired effect's parameter editor and move focus out with Tab.
An open effect editor takes precedence. Saving adds `.mlafxpre` when
needed; loading filters for that extension, case-insensitively.

FX presets save exposed plugin parameters by stable ID and check the plugin name
and parameter layout before loading. Invalid or incompatible files leave the
parameters unchanged. Instrument presets continue to use `.mlapre` and may
include sampler pads; the two formats are deliberately distinct.

In the Audio, Instruments and Patterns sidebar, `Ctrl+N` toggles a `*`
mark on the cursor item without activating it. Mark several items, then use
`y` to copy and `p` to duplicate them, or `Backspace` to remove them.
Without marks, these commands use the cursor item. Used audio/instruments
show the existing removal confirmation; instrument notes can be kept for
a replacement plugin. Audio duplicates are saved beside the original as
`Sample - N.wav`, using float32 WAV to preserve the loaded samples and
skipping existing filenames. Removing audio from the list leaves its disk file.
Instrument copies restore exposed parameters and sampler pad assignments.
