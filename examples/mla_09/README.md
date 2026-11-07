# Mla 09 — Detroit Drive

```sh
build/cmake/bin/mlacker examples/mla09_detroit.mlaproj
```

**Shift+M**, then **Space**, plays the whole song from the matrix. Space in the
pattern editor loops the selected pattern. Requires
[Mla 09](../../plugins/mla_09) installed (`./build.sh --plugins --install`).
For the original 909 hats and cymbals, put your 909 recordings in its samples
folder (see its README). The
project looks for `~/.local/plugins/VST3/Mla09.vst3`; regenerate it with
`--plugin` for another location.

An original drum track in the style of Jeff Mills' live TR-909 work in Detroit
techno: no melody, just the machine played hard and the panel worked by hand.
It uses every Mla 09 sound. It is not a copy of any of his records.

## The style

- **The engine.** A four-on-the-floor kick at 130 BPM that almost never stops,
  accented on every bar's downbeat.
- **Hats.** Sixteenth closed hats with uneven accents, and open hats on the
  offbeats. Each open hat is cut by the next closed hat, as on the 909.
- **Polyrhythm.** Rim shots every three sixteenths and a six-step tom figure
  (high, mid, high, low) roll across the bar lines against the 4/4.
- **Punctuation.** Crashes mark phrase starts. Flammed snares sit behind the
  claps, a pushed kick lands before beat 3, and tom fills run down the kit.
- **The roll.** A breakdown drops the kick while the snare speeds up from
  eighths to sixteenths to 32nds, with Snappy and Tone opening as it climbs.
- **Hands on the panel.** CC columns turn Mla 09's knobs while the patterns
  play: the closed hat's decay opens over the drop, the kick's tune sweeps up,
  the ride is retuned through the Waveform section, and the kick's decay closes
  in the outro. The kick's Attack opens through the drop, and the snare's
  Tune climbs with the roll and drops in Jack.

## Song

Nine four-bar patterns, played in this order (89 s):

| # | Pattern  | What happens |
|---|----------|--------------|
| 1 | Ignition | Kick and eighth hats; the kick's decay opens; crash. |
| 2 | Drive    | Sixteenth hats with open offbeats; rim shot threes. (Played twice.) |
| 3 | Clap     | Claps on 2 and 4; ride from bar 3; tom fill. |
| 4 | Tom Grid | The six-step tom figure under kick, hats and claps; the kick pulled back. |
| 5 | Drop     | Everything in. CH Decay opens, BD Tune sweeps up, BD Attack opens. |
| 6 | Jack     | Pushed kicks, flammed snares, toms, OH Decay longer, SD Tune drops. |
| 7 | Roll     | Breakdown: the snare roll into the drop, rising in pitch and level. |
| 8 | Drop     | Again. |
| 9 | Waveform | Eighth rides retuned across the pattern; rim and tom polyrhythms. |
| 10 | Jack    | Again. |
| 11 | Outro   | Kick and hats; the kick's decay closes; a last, retuned crash. |

## Tracks

Six Instrument tracks all play the one Mla 09:

- **BD.** Every bass drum knob: CC 20 BD Level, CC 103 BD Tune, CC 104 BD
  Attack, CC 105 BD Decay, plus CC 102 Accent.
- **SD CP.** Two note lines, for flams and 32nds. Every snare knob: CC 21 SD
  Level, CC 106 SD Tune, CC 107 SD Tone, CC 108 SD Snappy.
- **RS.** Rim shot.
- **Toms.** Low, mid and high tom.
- **Hats.** CC 115 CH Decay, CC 116 OH Decay.
- **Cymbals.** Two note lines, for the ride and the crash. CC 117 CR Tune, CC 118 RD Tune.

Every pattern sets all the knobs on its first row, so the song can start or
loop anywhere. Velocity 127 is the 909's accent; everything else plays at the
normal level.

## Regenerate

`generate.py` writes the project. Edit the sections there and run:

```sh
python3 examples/mla_09/generate.py [--plugin PATH]
```

`tests/mla09_detroit_tui_smoke.py` opens it in mlacker, plays it, saves it and
checks the score survives.
