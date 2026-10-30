# Turbo OutRun C64 music — Mla SID

Two separate projects are available. The startup/loading music is in
`examples/turbo_outrun_loading.mlaproj`; the existing subtune-2 project is kept
in `examples/turbo_outrun_intro.mlaproj`.

Open the loading project from the repository root:

```sh
build/cmake/bin/mlacker examples/turbo_outrun_loading.mlaproj
```

## Loading music (HVSC subtune 1)

The C64 arrangement of **Magical Sound Shower**, with the original C64 notes,
rather than a transcription of the arcade or Mega Drive arrangement. This
project contains 48 bars, about 92 seconds at 125 BPM, across twelve patterns.
It begins at the first melodic SID entry: the preceding 23-second section,
which relies on digital samples and the spoken count-in, is omitted. That
makes the melody playable immediately.

The melody, upper pulse accompaniment, chord changes, waveform changes, envelopes and pulse widths
were transcribed from subtune 1 of the original PAL SID. Pitch-bend automation
preserves the arpeggios and vibrato that continue during the release of a note.
Nine Mla SID instruments play the arrangement: the original three synth
voices, kick, snare, closed hi-hat, toms, PWM bass and crash. The bass notes
and rhythm come from samples 5/6 in the original digital player's event
stream, with pitch decoded from its CIA playback timer. A modulated square
wave replaces the sampled bass: broad pulse width, per-note width accents,
a moving filter and a fast slap-like attack.

Percussion uses the original sample trigger positions, including the
combined kick/bass attacks and the sparse closed-hat, snare and tom fills.
Closed hats are short filtered ticks; the snare has a tonal triangle body.
The original synth voices use their LP+BP filter routing, which colours
noise effects as well. No sampled recordings are embedded, and the sampled
speech is omitted. This recreates the sequence with SID synthesis; it is
not intended to reproduce each PCM drum's timbre exactly.

[This gameplay recording](https://www.youtube.com/watch?v=UXoelQuqj6o) labels
Loading Music at **0:19** and Title Music at **8:15**, distinguishing the two.
The copyright popup credits Hiroshi Kawaguchi / SEGA for the composition and
Jeroen Tel for the C64 arrangement, with the original US Gold/Probe credits.

## Existing title project (HVSC subtune 2)

Open the other project from the repository root:

```sh
build/cmake/bin/mlacker examples/turbo_outrun_intro.mlaproj
```

This project transcribes C64 SID subtune 2, composed by **Jeroen Tel**. The project
transcribes the opening 48 bars (about 77 seconds) at 150 BPM, including the
opening riff, low synthesizer breaks and the following title melody.
Twelve four-bar patterns play in their original order.

The original three SID voices were transcribed from the PAL player in
HVSC's `MUSICIANS/T/Tel_Jeroen/Turbo_Outrun.sid`, **subtune 2**. Notes and
gates use its 50 Hz frame timings, with the original frequency table's
approximately +34-cent tuning. Rapid arpeggios and pitch runs remain editable
notes, and the waveform and envelope changes appear in CC24/25/26 columns.
Register automation is quantized to mlacker's four 1/64 steps per row.

Six Mla SID instruments play the project: C64 Lead, C64 Bass, C64 Arp, SID
Kick, SID Snare and SID Hi-hat. The percussion is a synthesized replacement
for the original digital drum track; no original recording, digital drum
samples or voice samples are embedded. The project recreates the melody
and articulation, rather than the exact sound of the original recording.
The separate `mlasidturbo.mlaproj` remains an original racing tune.

Mla SID must be installed (`./build.sh --plugins --install`). The project
stores its patches in `Presets/`. Press Enter or click OK to dismiss the
copyright notice, then Space to play. The notice appears on every open,
including the command line and Open project menu, and survives Save As.

To regenerate either project after building mlacker:

```sh
examples/turbo_outrun/build.sh loading
examples/turbo_outrun/build.sh title
```

 Set `MLACKER_SID_PLUGIN` to use a different MlaSid.vst3 location.
`examples/turbo_outrun/loading_song.mla` contains the loading tune's editable
score and patch settings; `song.mla` contains the existing subtune-2 project.

## Credits and rights

- Music: **Jeroen Tel**.
- Original SID credits: **1989 US Gold/Probe**.
- Reference: [Turbo Outrun (C64) Title Theme](https://www.youtube.com/watch?v=qG82RMyS07Y).
- Source identification: [HVSC/CSDb SID information](https://csdb.dk/sid/?id=28220).

The music remains subject to its owners' copyright. Including credits does
not grant permission to redistribute, publish, or commercially use it. This
transcription and its musical data are not represented as original mlacker
music or as covered by mlacker's software license.

Projects and standalone sessions can carry an optional `COPYRIGHT_NOTICE`
extension in `Project.mlack` / `.mlack`, written from
`SessionView.copyright_notice`. Documents without this metadata open normally.
Older mlacker versions may reject documents containing this new extension.
