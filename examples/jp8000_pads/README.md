# JP8000 pad auditions

`../jp8000_pads.mlaproj` plays six original layered/split pad performances on
[Mla JP8000](../../plugins/mla_jp8000). Open it from the repository root, press
**Shift+M**, then **Space**:

```sh
build/cmake/bin/mlacker examples/jp8000_pads.mlaproj
```

The project references the built bundle in this checkout. After building the
plugins, `examples/jp8000_pads/build.sh` regenerates it with the current plugin
path and the shipped factory bank. Existing project saves use the normal
project backup/replace behavior.

The reference is [Matti Laamanen — Obsession (Original mix)](https://www.youtube.com/watch?v=GdwwJXsTSUo).
These are original pad designs; the song has not been copied or transcribed,
and a sound match to its recording has not been verified.
