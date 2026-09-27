# demos

A scratch area for trying out mlacker features: save sessions, patterns and
presets here while testing, or keep a few songs to reopen later.

Files matching `*.mla*` in this directory and its subdirectories are ignored by
git (see the repository's `.gitignore`). That covers:

- `.mlack` sessions
- `.mlapatt` patterns
- `.mlapre` instrument presets
- `.mlafxpre` effect plugin presets
- `.mlalearn` MIDI learn mappings

Nothing saved here is committed, so it is safe to experiment. Copy anything
worth keeping somewhere else. Only this README is tracked.

Open a session from the repository root with:

```sh
build/cmake/bin/mlacker demos/my-song.mlack
```
