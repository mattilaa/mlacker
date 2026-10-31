# Turbo OutRun C64 music — native mlacker projects

```sh
build/cmake/bin/mlacker examples/turbo_outrun_loading.mlaproj
build/cmake/bin/mlacker examples/turbo_outrun_intro.mlaproj
```

Each folder contains just **Project.mlack**. It is a native text project holding
all notes, automation, instrument parameters and song order. There is no
compiled song, score generator, separate preset file or recorded backing track.
The installed Mla SID plugin synthesizes the instruments during normal
sequencer playback. Edit the notes and automation in mlacker and save the
project with Ctrl+S.

## Loading music

`turbo_outrun_loading.mlaproj` contains the C64 loading arrangement of
**Magical Sound Shower**: original composition by Hiroshi Kawaguchi / SEGA,
C64 arrangement by Jeroen Tel (1989 US Gold/Probe). It starts at the first
melodic entry; the preceding sampled-only speech introduction is omitted.

The patterns follow the musical sections instead of arbitrary four-bar cuts:

| Pattern | Bars | Sequencer rows |
| --- | ---: | ---: |
| Opening Theme | 12 | 192 |
| Loading Break | 6 | 96 |
| Theme Return | 12 | 192 |
| Short Break | 4 | 64 |
| Rhythm Breakdown | 8 | 128 |
| Final Theme | 8 | 128 |

The arrangement totals 50 bars (96 seconds) at 125 BPM. The final theme includes
the closing two bars of the repeated phrase. Each song-matrix row is one bar
(16 sequencer rows), and long patterns continue across the necessary rows.
**Ctrl+T**, or **Pattern → Follow the row**, follows the local pattern row while
the matrix plays. Turn it off to edit freely without stopping playback.

The original three SID voices remain editable notes with waveform, ADSR,
pulse-width and pitch-bend automation. The sampled bass is converted to a
modulated pulse/square instrument using the original sample player's note
changes and trigger positions. Percussion uses the original trigger positions
with synthesized kick, tonal snare, short hats, toms and crash. Sampled speech
is omitted, and SID percussion approximates the PCM timbres.

## Other C64 title tune

`turbo_outrun_intro.mlaproj` preserves the separate HVSC subtune-2 arrangement:
48 bars at 150 BPM, with the original three SID parts and synthesized percussion.
Its sequence and settings are likewise stored entirely in Project.mlack.

## Credits and rights

The copyright popup appears whenever either project opens and survives saving
or Save As. Music rights remain with the respective owners; project credits do
not grant redistribution, publication or commercial-use rights. The musical
data is not represented as original mlacker music or as covered by its software
license.

References: [requested C64 recording](https://www.youtube.com/watch?v=qG82RMyS07Y),
[gameplay with Loading Music at 0:19 and Title Music at 8:15](https://www.youtube.com/watch?v=UXoelQuqj6o),
[HVSC SID information](https://csdb.dk/sid/?id=28220).
