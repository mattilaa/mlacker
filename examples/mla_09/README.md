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
  (high, mid, high, low) roll across the bar lines against the 4/4. The toms
  are tuned to a root, its fifth and its minor seventh.
- **Punctuation.** Crashes mark phrase starts. Flammed snares sit behind the
  claps, a pushed kick lands before beat 3, and tom fills run down the kit.
- **The roll.** A breakdown drops the kick while the snare speeds up from
  eighths to sixteenths to 32nds, with Snappy and Tone opening as it climbs.
- **Hands on the panel.** CC columns turn Mla 09's knobs while the patterns
  play: the closed hat's decay opens over the drop, the kick's tune sweeps up,
  the ride is retuned through the Waveform section, light output Drive changes
  with the sections, and the kick's decay closes
  in the outro. The kick's Attack opens through the drop, and the snare's
  Tune climbs with the roll and drops in Jack.

## Song

Thirteen four-bar patterns, played in this order (118 s). Every pattern works the
panel live through CC columns. The kick, snare, claps, hats and toms change
their settings as it plays:

| # | Pattern  | What happens | Knobs moving |
|---|----------|--------------|--------------|
| 1 | Ignition | Kick and eighth hats; crash. | Kick decay opens; closed hat fades in and opens. |
| 2 | Drive    | Sixteenth hats with open offbeats; rim shot threes. (Played twice.) | Kick tune, extended decay, light Drive and open-hat decay change every bar. |
| 3 | Clap     | Claps on 2 and 4; ride from bar 3; tom fill. | Claps build; kick attack and closed-hat decay change every bar; the fill rings long. |
| 4 | Tom Grid | The six-step tom figure under kick, hats and claps. | Kick and hats pulled back; kick decay changes every bar; the toms dive in pitch and change decay every bar; claps lift in the last bar. |
| 5 | Drop     | Everything in. | Closed hat opens; kick tune sweeps up and its attack opens; snare tone changes every bar and gets snappier; claps and open hats swell. |
| 6 | Jack     | Pushed kicks, flammed snares, toms, tom fill. | Kick decay pumps; snare snap changes every bar and its tune drops; claps alternate loud and soft; toms go up for the second half; open hat opens. |
| 7 | Roll     | Breakdown: the snare roll into the drop. | Snare tune, level, tone and snap climb; closed hat dips and opens; long low toms at the end. |
| 8 | Drop     | Again. | |
| 9 | Waveform | Eighth rides; rim and tom polyrhythms. | Ride retuned across the pattern; kick tune rises; closed-hat level alternates every bar; short toms climbing in pitch. |
| 10 | Kick Decays | Kick, hats and claps. | Two bars of short kicks, then half-time long kicks and a final ring-out; BD Tune and light Drive move each bar. |
| 11 | Tom Decays | A rolling tom figure over kick and hats. | Each tom's Decay steps short to long over the bars (the high tom long to short) while the toms are retuned. |
| 12 | Snare Decays | Backbeat snares and ghost notes. | SD Decay and Tone change on every hit; SD Tune each bar. |
| 13 | Cymbal Decays | A crash on every bar, eighth rides. | Each crash has its own CR Decay and Tune; RD Decay changes every beat as RD Tune climbs. |
| 14 | Jack    | Again. | |
| 15 | Outro   | Kick and hats; a last, retuned crash. | Kick decay closes; claps and hats fade; open hat tightens. |

The four decay studies (10 to 13) play every tunable sound at different
decays. The snare's tone decay (SD Decay) and the cymbals' decays (CR and RD
Decay) are Mla 09 additions beyond the 909's panel.

## Tracks

Six Instrument tracks all play the one Mla 09:

- **BD.** Every bass drum knob: CC 20 BD Level, CC 103 BD Tune, CC 104 BD
  Attack, CC 105 BD Decay, CC 102 Accent, plus global CC 119 Drive.
- **SD CP.** Two note lines, for flams and 32nds. Every snare knob: CC 21 SD
  Level, CC 106 SD Tune, CC 107 SD Tone, CC 108 SD Snappy, CC 120 SD Decay;
  and the claps' CC 26 CP Level.
- **RS.** Rim shot.
- **Toms.** Low, mid and high tom. CC 109 to 114: LT Tune and Decay, MT Tune
  and Decay, HT Tune and Decay.
- **Hats.** One note line for both hats. CC 27 CH Level, CC 115 CH Decay,
  CC 28 OH Level, CC 116 OH Decay.
- **Cymbals.** Two note lines, for the ride and the crash. CC 117 CR Tune,
  CC 121 CR Decay, CC 118 RD Tune, CC 122 RD Decay.

Every pattern sets all the knobs on its first row, so the song can start or
loop anywhere.

## Mix

The levels were set by rendering the whole song through Mla 09 with the
samples installed and measuring each part. Against the kick's peak, the
snare sits at about -3 dB, the clap -5, the toms -6, the hats -9, the
cymbals -10 and the rim shot -11. The snare has more wire noise, the kick
has more attack noise, and light output Drive adds grit. Drive is reset at every pattern start and varies
from 6 to 20 out of 127 across the song.

Drive, Drop, Jack and Waveform use the extended upper fifth of BD Decay.
The Kick Decays study leaves half-time gaps and a final ring-out so the
longest setting (about 1.05 s to fall 40 dB) can be heard. Lower settings
keep their earlier decay times.

The master bus boosts the mix: 120% volume, and EQ bands at 100 Hz (+4 dB),
500 Hz (+1 dB), 2.5 kHz (+2 dB) and 8 kHz (+3 dB). Mla 09's Output (-12.8 dB)
leaves headroom for those boosts, overlapping long kicks and noisy attacks.
An offline render with the locally installed hat, cymbal and clap samples
peaks at about -4.4 dBFS; other samples can change that balance. `generate.py`
has the levels (`MIX`, `OUTPUT_DB`) and the master bus (`MASTER_VOLUME`,
`MASTER_BANDS`). Velocity 127 is the 909's accent; everything else plays at the
normal level.

## Regenerate

`generate.py` writes the project. Edit the sections there and run:

```sh
python3 examples/mla_09/generate.py [--plugin PATH]
```

`tests/mla09_detroit_tui_smoke.py` opens it in mlacker, plays it, saves it and
checks the score survives.
