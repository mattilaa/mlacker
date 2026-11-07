#!/usr/bin/env python3
"""Write "Mla 09 - Detroit Drive", a Detroit techno drum track for Mla 09.

An original sequence in the style of Jeff Mills' live TR-909 work: a driving
kick under sixteenth hats, polyrhythmic rim shots and toms, crashes at phrase
starts, a snare roll into the drop, and the panel worked live through CC
columns (hat decays opening, the kick's tune and decay, the ride's tune). It
uses every Mla 09 sound.

Usage: generate.py [--plugin PATH] [--output PATH]
"""
import argparse
import json
from pathlib import Path

BPM = 130
ROWS = 64  # Four bars of sixteenths per pattern.

# The 909's keys in mlacker's note names (key 36 is C-2).
BD, RS, SD, CP = "C-2", "C#2", "D-2", "D#2"
CH, LT, OH, MT, HT = "F#2", "G-2", "A#2", "B-2", "D-3"
CR, RD = "C#3", "D#3"
ACCENT, NORMAL = 127, 100

# Mla 09 CCs (see plugins/mla_09/README.md).
CC_BD_LEVEL, CC_SD_LEVEL, CC_CP_LEVEL, CC_CH_LEVEL, CC_OH_LEVEL = 20, 21, 26, 27, 28
CC_ACCENT, CC_BD_TUNE, CC_BD_ATTACK, CC_BD_DECAY = 102, 103, 104, 105
CC_SD_TUNE, CC_SD_TONE, CC_SD_SNAPPY = 106, 107, 108
CC_LT_TUNE, CC_LT_DECAY, CC_MT_TUNE, CC_MT_DECAY, CC_HT_TUNE, CC_HT_DECAY = 109, 110, 111, 112, 113, 114
# Beyond the 909's panel: the snare's tone decay and the cymbals' decays.
CC_SD_DECAY, CC_CR_DECAY, CC_RD_DECAY = 120, 121, 122
CC_CH_DECAY, CC_OH_DECAY, CC_CR_TUNE, CC_RD_TUNE = 115, 116, 117, 118

# Tracks, all playing the one Mla 09: name, note lines, CC columns.
TRACKS = [
    # Every bass drum and snare knob, in panel order.
    ("BD", 1, [CC_BD_LEVEL, CC_BD_TUNE, CC_BD_ATTACK, CC_BD_DECAY, CC_ACCENT]),
    ("SD CP", 2, [CC_SD_LEVEL, CC_SD_TUNE, CC_SD_TONE, CC_SD_SNAPPY, CC_SD_DECAY, CC_CP_LEVEL]),
    ("RS", 1, []),
    ("Toms", 1, [CC_LT_TUNE, CC_LT_DECAY, CC_MT_TUNE, CC_MT_DECAY, CC_HT_TUNE, CC_HT_DECAY]),
    ("Hats", 1, [CC_CH_LEVEL, CC_CH_DECAY, CC_OH_LEVEL, CC_OH_DECAY]),
    ("Cymbals", 2, [CC_CR_TUNE, CC_CR_DECAY, CC_RD_TUNE, CC_RD_DECAY]),
]

# The panel's starting position: tight kick with plenty of click, crisp snare,
# short closed hat. CC columns move these while the song plays.
# Levels: 115 is about unity (0 dB of -60 .. +6 dB).
KNOBS = {CC_BD_LEVEL: 115, CC_BD_TUNE: 60, CC_BD_ATTACK: 83, CC_BD_DECAY: 92, CC_ACCENT: 72,
         CC_SD_LEVEL: 115, CC_SD_TUNE: 64, CC_SD_TONE: 80, CC_SD_SNAPPY: 84, CC_CP_LEVEL: 115,
         # Toms centred: root, fifth and minor seventh.
         CC_LT_TUNE: 64, CC_LT_DECAY: 64, CC_MT_TUNE: 64, CC_MT_DECAY: 64, CC_HT_TUNE: 64, CC_HT_DECAY: 64,
         CC_CH_LEVEL: 115, CC_CH_DECAY: 40, CC_OH_LEVEL: 117, CC_OH_DECAY: 56, CC_CR_TUNE: 64, CC_RD_TUNE: 64,
         # The snare's tone as the 909's; the cymbals ring for their sample.
         CC_SD_DECAY: 64, CC_CR_DECAY: 127, CC_RD_DECAY: 127}
TOM_TUNES = (CC_LT_TUNE, CC_MT_TUNE, CC_HT_TUNE)

# The mix: each instrument's level against unity (0 dB), in dB, balanced by
# rendering the whole song (kick as the reference). Levels with a CC column
# shift their moves by the same amount; the rest are set once.
MIX = {"bd": 0.0, "sd": -3.1, "toms": -6.9, "rs": -8.0, "cp": 3.9, "hats": -5.0, "cymbals": -2.1}
LEVEL_TRIM = {CC_BD_LEVEL: MIX["bd"], CC_SD_LEVEL: MIX["sd"], CC_CP_LEVEL: MIX["cp"],
              CC_CH_LEVEL: MIX["hats"], CC_OH_LEVEL: MIX["hats"]}
# Mla 09's Output (dB), leaving headroom for the master EQ's boosts.
OUTPUT_DB = -12.8
# The master bus: volume (%) and four peaking bands (Hz, dB, Q).
MASTER_VOLUME = 120
MASTER_BANDS = ((100, 4.0, 0.8), (500, 1.0, 1.0), (2500, 2.0, 1.0), (8000, 3.0, 0.8))


def level(cc, value):
    """A level CC value moved by its instrument's mix trim (1.92 steps a dB)."""
    if cc not in LEVEL_TRIM:
        return value
    return max(0, min(127, round(value + LEVEL_TRIM[cc] * 127 / 66)))
TOM_DECAYS = (CC_LT_DECAY, CC_MT_DECAY, CC_HT_DECAY)


def parameters():
    """Mla 09's 34 parameters, normalized (IDs 100-133, see its README)."""
    unity = 60 / 66
    p = {key: 0.5 for key in range(100, 134)}
    for pid in (100, 103, 107, 111, 114, 117, 120, 121, 122, 124, 126, 128):
        p[pid] = unity
    p[102] = 0.0  # 909 Accent dynamics.
    p[100] = (60 + OUTPUT_DB) / 66
    for pid in (111, 114, 117):
        p[pid] = (60 + MIX["toms"]) / 66
    p[120] = (60 + MIX["rs"]) / 66
    p[126] = (60 + MIX["cymbals"]) / 66
    p[128] = (60 + MIX["cymbals"] - 2.0) / 66  # The ride sits under the crash.
    # Toms centred: root, fifth and minor seventh (94, 140.8, 167.5 Hz).
    p.update({112: KNOBS[CC_LT_TUNE] / 127, 113: KNOBS[CC_LT_DECAY] / 127, 115: KNOBS[CC_MT_TUNE] / 127,
              116: KNOBS[CC_MT_DECAY] / 127, 118: KNOBS[CC_HT_TUNE] / 127, 119: KNOBS[CC_HT_DECAY] / 127,
              121: level(CC_CP_LEVEL, KNOBS[CC_CP_LEVEL]) / 127, 122: level(CC_CH_LEVEL, KNOBS[CC_CH_LEVEL]) / 127,
              124: level(CC_OH_LEVEL, KNOBS[CC_OH_LEVEL]) / 127})
    p.update({101: KNOBS[CC_ACCENT] / 127, 103: level(CC_BD_LEVEL, KNOBS[CC_BD_LEVEL]) / 127, 104: KNOBS[CC_BD_TUNE] / 127,
              105: KNOBS[CC_BD_ATTACK] / 127, 106: KNOBS[CC_BD_DECAY] / 127, 107: level(CC_SD_LEVEL, KNOBS[CC_SD_LEVEL]) / 127,
              108: KNOBS[CC_SD_TUNE] / 127, 109: KNOBS[CC_SD_TONE] / 127, 110: KNOBS[CC_SD_SNAPPY] / 127, 123: KNOBS[CC_CH_DECAY] / 127,
              125: KNOBS[CC_OH_DECAY] / 127, 127: KNOBS[CC_CR_TUNE] / 127, 129: KNOBS[CC_RD_TUNE] / 127,
              130: 0.0,  # Drive off: the clean machine.
              131: KNOBS[CC_SD_DECAY] / 127, 132: KNOBS[CC_CR_DECAY] / 127, 133: KNOBS[CC_RD_DECAY] / 127})
    return p


class Pattern:
    def __init__(self, name):
        self.name = name
        self.notes = {track[0]: {} for track in TRACKS}  # track -> {(row, line): (note, vel, offset)}
        self.ccs = {track[0]: {} for track in TRACKS}  # track -> {(row, cc): value}
        # Every pattern restores the panel on its first row, so the song can
        # start, loop or jump anywhere.
        for name, _, ccs in TRACKS:
            for cc in ccs:
                self.ccs[name][(0, cc)] = level(cc, KNOBS[cc])

    def hit(self, track, row, note, velocity=NORMAL, line=0, offset=0.0):
        self.notes[track][(row, line)] = (note, velocity, offset)

    def knob(self, track, row, cc, value):
        self.ccs[track][(row, cc)] = level(cc, value)

    def bars(self, track, cc, values):
        """One knob position per bar, set on each bar's first row."""
        for bar, value in enumerate(values):
            self.knob(track, bar * 16, cc, value)

    def ramp(self, track, cc, start, end, first=0, last=ROWS - 1, every=4):
        """Turn a knob from `start` to `end` over rows first..last, one step every `every` rows."""
        for row in range(first, last + 1, every):
            value = start + (end - start) * (row - first) / max(1, last - first)
            self.knob(track, row, cc, round(value))

    def rows(self):
        out = []
        for row in range(ROWS):
            cells = [f"{row:03d}"]
            for name, lines, ccs in TRACKS:
                for line in range(lines):
                    note = self.notes[name].get((row, line))
                    cells += [note[0], str(note[1]), "1.00", f"{note[2]:.2f}"] if note else ["", "", "", ""]
                for cc in ccs:
                    value = self.ccs[name].get((row, cc))
                    cells.append("" if value is None else str(value))
            out.append(cells)
        return out


# --- Building blocks ----------------------------------------------------------

def kick(p, accent_bars=True, skip=()):
    """Four on the floor; beat 1 of every bar accented."""
    for row in range(0, ROWS, 4):
        if row in skip:
            continue
        p.hit("BD", row, BD, ACCENT if accent_bars and row % 16 == 0 else NORMAL)


def hats(p, open_hats=True, density=16):
    """Sixteenth closed hats with uneven accents, open hats on the offbeats
    (the open hat is choked by the next closed one, as on the 909)."""
    accents = (3, 7, 11, 13)
    for row in range(ROWS):
        step = row % 16
        if open_hats and step % 4 == 2:
            p.hit("Hats", row, OH, NORMAL)
        elif density == 16 or step % (16 // density) == 0:
            p.hit("Hats", row, CH, ACCENT if step in accents else NORMAL)


def clap(p, snare=False):
    for row in range(ROWS):
        if row % 16 in (4, 12):
            p.hit("SD CP", row, CP, NORMAL)
            if snare:
                p.hit("SD CP", row, SD, NORMAL, line=1, offset=0.25)  # Flam behind the clap.


def rim_threes(p, start=2):
    """Rim shots every three sixteenths: a 3-against-4 cycle across the bar lines."""
    for row in range(start, ROWS, 3):
        p.hit("RS", row, RS, ACCENT if row % 16 in (2, 10) else NORMAL)


def tom_cycle(p, first=0, last=ROWS):
    """A six-step tom figure (high, mid, low) rolling over the 4/4 grid."""
    figure = {0: (HT, ACCENT), 2: (MT, NORMAL), 3: (HT, NORMAL), 4: (LT, NORMAL)}
    for row in range(first, last):
        step = (row - first) % 6
        if step in figure:
            note, velocity = figure[step]
            p.hit("Toms", row, note, velocity)


def ride_offbeats(p, first=0, last=ROWS):
    for row in range(first, last):
        if row % 4 == 2:
            p.hit("Cymbals", row, RD, ACCENT if row % 16 == 14 else NORMAL)


def crash(p, row=0):
    p.hit("Cymbals", row, CR, ACCENT, line=1)


def tom_fill(p, bar=3):
    """Last-bar fill down the toms, sixteenths then accented."""
    base = bar * 16 + 8
    for i, note in enumerate((HT, HT, MT, MT, LT, LT, MT, LT)):
        p.hit("Toms", base + i, note, ACCENT if i in (0, 4, 7) else NORMAL)


# --- Sections -----------------------------------------------------------------

def ignition():
    p = Pattern("Ignition")
    kick(p)
    hats(p, open_hats=False, density=8)
    p.ramp("BD", CC_BD_DECAY, 64, 92, every=8)
    # The hats fade in and open up.
    p.ramp("Hats", CC_CH_LEVEL, 85, 115, every=4)
    p.ramp("Hats", CC_CH_DECAY, 25, 45, every=8)
    crash(p)
    return p


def drive():
    p = Pattern("Drive")
    kick(p)
    hats(p)
    rim_threes(p)
    # The kick's pitch and the open hat's length change every bar.
    p.bars("BD", CC_BD_TUNE, (60, 72, 56, 80))
    p.bars("Hats", CC_OH_DECAY, (40, 70, 50, 90))
    return p


def clap_section():
    p = Pattern("Clap")
    kick(p)
    hats(p)
    clap(p)
    rim_threes(p, start=1)
    ride_offbeats(p, 32, ROWS)
    tom_fill(p)
    # The claps build, the kick's click and the closed hat change every bar,
    # and the fill rings long.
    p.bars("SD CP", CC_CP_LEVEL, (100, 108, 115, 121))
    p.bars("BD", CC_BD_ATTACK, (83, 100, 70, 110))
    p.bars("Hats", CC_CH_DECAY, (30, 50, 35, 60))
    for cc in TOM_DECAYS:
        p.knob("Toms", 48, cc, 90)
    return p


def tom_section():
    p = Pattern("Tom Grid")
    kick(p)
    hats(p, open_hats=False)
    clap(p)
    tom_cycle(p)
    crash(p)
    # Pull the kick and hats back a little so the toms come through.
    p.knob("BD", 0, CC_BD_LEVEL, 106)
    p.knob("Hats", 0, CC_CH_LEVEL, 105)
    p.bars("BD", CC_BD_DECAY, (92, 76, 104, 84))
    # The toms dive in pitch together and ring longer or shorter every bar;
    # the claps lift for the last bar.
    for cc in TOM_TUNES:
        p.ramp("Toms", cc, 64, 44, every=16)
    for cc in TOM_DECAYS:
        p.bars("Toms", cc, (64, 50, 80, 64))
    p.knob("SD CP", 48, CC_CP_LEVEL, 121)
    return p


def roll():
    """Breakdown: no kick; the snare rolls from eighths to 32nds while Snappy
    and Tone open, the hats thin out, and a crash lands on the last step."""
    p = Pattern("Roll")
    hats(p, open_hats=False, density=4)
    ride_offbeats(p, 0, 32)
    for row in range(ROWS):
        bar, step = divmod(row, 16)
        velocity = ACCENT if step % 4 == 0 and bar >= 2 else NORMAL
        if bar == 0 and step % 4 == 0:
            p.hit("SD CP", row, SD, velocity)
        elif bar == 1 and step % 2 == 0:
            p.hit("SD CP", row, SD, velocity)
        elif bar == 2:
            p.hit("SD CP", row, SD, velocity)
        elif bar == 3:
            # 32nds: a second line half a row later.
            p.hit("SD CP", row, SD, velocity)
            p.hit("SD CP", row, SD, NORMAL, line=1, offset=0.5)
    p.ramp("SD CP", CC_SD_SNAPPY, 40, 127, every=4)
    p.ramp("SD CP", CC_SD_TONE, 50, 120, every=8)
    # The roll climbs in pitch and swells into the drop.
    p.ramp("SD CP", CC_SD_TUNE, 30, 110, every=4)
    p.ramp("SD CP", CC_SD_LEVEL, 96, 121, every=4)
    # The closed hat dips and opens under the roll.
    p.ramp("Hats", CC_CH_LEVEL, 115, 95, every=8)
    p.ramp("Hats", CC_CH_DECAY, 30, 80, every=8)
    # The kick comes back only on the last beat, under long low toms.
    p.hit("BD", 60, BD, ACCENT)
    p.knob("Toms", 56, CC_LT_DECAY, 100)
    p.hit("Toms", 60, LT, ACCENT)
    p.hit("Toms", 62, LT, ACCENT)
    return p


def exhibition():
    """The drop: everything in, the closed hat opening up over four bars and
    the kick's tune swept up."""
    p = Pattern("Drop")
    crash(p)
    kick(p)
    hats(p)
    clap(p, snare=True)
    rim_threes(p)
    ride_offbeats(p)
    p.ramp("Hats", CC_CH_DECAY, 20, 110, every=4)
    p.ramp("BD", CC_BD_TUNE, 40, 100, every=8)
    # More click as the drop builds.
    p.ramp("BD", CC_BD_ATTACK, 60, 127, every=8)
    # The flammed snares change colour every bar and get snappier; claps and
    # open hats swell.
    p.bars("SD CP", CC_SD_TONE, (80, 60, 100, 70))
    p.ramp("SD CP", CC_SD_SNAPPY, 70, 110, every=8)
    p.ramp("SD CP", CC_CP_LEVEL, 105, 121, every=8)
    p.ramp("Hats", CC_OH_LEVEL, 100, 121, every=8)
    return p


def jack():
    """Variation: a pushed kick before beat 3, flammed snares, tom fill."""
    p = Pattern("Jack")
    kick(p)
    for bar in range(4):
        p.hit("BD", bar * 16 + 7, BD, NORMAL)
        if bar % 2:
            p.hit("BD", bar * 16 + 15, BD, NORMAL)
    hats(p)
    clap(p, snare=True)
    tom_cycle(p, 0, 32)
    tom_fill(p)
    p.knob("Hats", 0, CC_OH_DECAY, 40)
    p.knob("Hats", 32, CC_OH_DECAY, 80)
    # The snare drops lower for the second half and its snap changes every
    # bar; the kick's decay pumps; claps alternate loud and soft.
    p.knob("SD CP", 32, CC_SD_TUNE, 30)
    p.bars("SD CP", CC_SD_SNAPPY, (84, 110, 70, 120))
    p.bars("BD", CC_BD_DECAY, (76, 104, 80, 112))
    p.bars("SD CP", CC_CP_LEVEL, (121, 104, 121, 104))
    # The toms go up for the second half and the fill rings long.
    for cc in TOM_TUNES:
        p.knob("Toms", 32, cc, 80)
    for cc in TOM_DECAYS:
        p.knob("Toms", 48, cc, 90)
    return p


def waveform():
    """Ride section: the ride retuned across the pattern, long open hats, rim
    and tom polyrhythms together."""
    p = Pattern("Waveform")
    crash(p)
    kick(p)
    hats(p)
    rim_threes(p)
    tom_cycle(p, 32, ROWS)
    for row in range(0, ROWS, 2):
        p.hit("Cymbals", row, RD, ACCENT if row % 8 == 2 else NORMAL)
    p.ramp("Cymbals", CC_RD_TUNE, 30, 110, every=4)
    p.knob("Hats", 0, CC_OH_DECAY, 100)
    p.knob("BD", 0, CC_BD_DECAY, 104)
    p.ramp("BD", CC_BD_TUNE, 60, 90, every=8)
    p.bars("Hats", CC_CH_LEVEL, (115, 100, 115, 100))
    # Short toms climbing in pitch.
    for cc in TOM_TUNES:
        p.ramp("Toms", cc, 50, 80, 32, ROWS - 1, every=4)
    for cc in TOM_DECAYS:
        p.knob("Toms", 32, cc, 40)
    return p


def outro():
    """Strip down to kick and hats; the kick's decay closes, a last crash."""
    p = Pattern("Outro")
    kick(p, skip=(60,))
    hats(p, open_hats=True, density=8)
    for row in range(0, 32):
        if row % 16 in (4, 12):
            p.hit("SD CP", row, CP, NORMAL)
    p.ramp("BD", CC_BD_DECAY, 112, 56, every=4)
    p.ramp("Cymbals", CC_CR_TUNE, 64, 20, 48, ROWS - 1, every=4)
    # The claps fade, the hats fade and the open hat tightens.
    p.ramp("SD CP", CC_CP_LEVEL, 115, 70, 0, 31, every=4)
    p.ramp("Hats", CC_CH_LEVEL, 115, 75, every=4)
    p.ramp("Hats", CC_OH_DECAY, 80, 30, every=8)
    crash(p, 48)
    return p


# --- Decay studies: every tunable sound at different decays ------------------

def kick_decays():
    """Every beat a different BD Decay, short to long; BD Tune moves each bar,
    so each tuning is heard across the decays."""
    p = Pattern("Kick Decays")
    kick(p)
    hats(p, open_hats=False)
    clap(p)
    for row in range(0, ROWS, 4):
        p.knob("BD", row, CC_BD_DECAY, (20, 60, 100, 127)[row // 4 % 4])
    p.bars("BD", CC_BD_TUNE, (30, 60, 90, 120))
    return p


def tom_decays():
    """A rolling tom figure; each tom's Decay steps from short to long over
    the bars (high tom the other way) while the toms are retuned."""
    p = Pattern("Tom Decays")
    kick(p)
    hats(p, open_hats=False, density=8)
    figure = {0: (LT, ACCENT), 3: (MT, NORMAL), 6: (HT, NORMAL), 8: (MT, ACCENT), 11: (LT, NORMAL), 14: (HT, NORMAL)}
    for row in range(ROWS):
        if row % 16 in figure:
            note, velocity = figure[row % 16]
            p.hit("Toms", row, note, velocity)
    p.bars("Toms", CC_LT_DECAY, (10, 45, 85, 127))
    p.bars("Toms", CC_MT_DECAY, (10, 45, 85, 127))
    p.bars("Toms", CC_HT_DECAY, (127, 85, 45, 10))
    for cc, tunes in ((CC_LT_TUNE, (64, 44, 84, 64)), (CC_MT_TUNE, (64, 44, 84, 64)), (CC_HT_TUNE, (64, 44, 84, 64))):
        p.bars("Toms", cc, tunes)
    return p


def snare_decays():
    """Backbeat snares and ghost notes; SD Decay and Tone change on every hit,
    SD Tune each bar."""
    p = Pattern("Snare Decays")
    kick(p)
    hats(p)
    decays, tones, n = (16, 64, 112, 127), (30, 80, 120, 60), 0
    for row in range(ROWS):
        step = row % 16
        if step in (4, 12) or step in (7, 10, 15):
            p.hit("SD CP", row, SD, ACCENT if step in (4, 12) else NORMAL)
            p.knob("SD CP", row, CC_SD_DECAY, decays[n % 4])
            p.knob("SD CP", row, CC_SD_TONE, tones[n % 4])
            n += 1
    p.bars("SD CP", CC_SD_TUNE, (40, 64, 96, 64))
    return p


def cymbal_decays():
    """A crash on each bar with its own CR Decay and Tune; eighth rides whose
    RD Decay changes every beat as RD Tune climbs."""
    p = Pattern("Cymbal Decays")
    kick(p)
    hats(p, open_hats=False, density=4)
    clap(p)
    for bar in range(4):
        crash(p, bar * 16)
    p.bars("Cymbals", CC_CR_DECAY, (20, 50, 90, 127))
    p.bars("Cymbals", CC_CR_TUNE, (100, 64, 40, 64))
    for row in range(0, ROWS, 2):
        p.hit("Cymbals", row, RD, ACCENT if row % 8 == 2 else NORMAL)
    for row in range(0, ROWS, 4):
        p.knob("Cymbals", row, CC_RD_DECAY, (30, 60, 95, 127)[row // 4 % 4])
    p.ramp("Cymbals", CC_RD_TUNE, 40, 100, every=8)
    return p


PATTERNS = [ignition, drive, clap_section, tom_section, roll, exhibition, jack, waveform, outro,
            kick_decays, tom_decays, snare_decays, cymbal_decays]
# Pattern numbers (1-based, in PATTERNS order) in song order: the decay
# studies come after Waveform.
SONG = [1, 2, 2, 3, 4, 6, 7, 5, 6, 8, 10, 11, 12, 13, 7, 9]


def project(plugin):
    records = []

    def emit(*values):
        for value in values:
            kind = "string" if isinstance(value, str) else "real" if isinstance(value, float) else "integer"
            records.append([kind, value])

    patterns = [make() for make in PATTERNS]
    tracks = len(TRACKS)
    count = len(patterns)
    emit("MLACK", 1, 5, BPM, 4, 4, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
         0, 1, 60, 0, 1, 0, 0, 0, 0, 0, 0)
    params = parameters()
    emit(0, 1, 1, plugin, len(params))
    for key, value in sorted(params.items()):
        emit(key, float(value))
    emit(1, count + 1, count)
    for index, pattern in enumerate(patterns):
        emit(index + 1, pattern.name)
        emit(tracks + 1, 0, 1, 0, 0, 1, 1, 0, tracks)
        for name, lines, ccs in TRACKS:
            emit(name, 0, 100, 0, 0, 1, 1, lines, 0, 1, len(ccs))
            for cc in ccs:
                emit(f"cc:{cc}", 0, 127)
            emit(0)
        emit(ROWS)
        for row in pattern.rows():
            emit(len(row), *row)
    emit(len(SONG), *SONG, "MIDI_LEARN", 0, "AUX_EFFECTS", 0)
    emit(*([tracks] * count), 0, 0, "TRACK_INSERTS", 0)
    for _ in range(count):
        emit(tracks, *([0] * (tracks * 4)))
    emit(0, -1, "SAMPLER_PADS", 0, "SONG_MATRIX", 0, "TRACK_OUTPUTS")
    for _ in range(count):
        emit(tracks, *([0] * tracks))
    emit("MATRIX_GRID", ROWS, "MASTER_BUS", 0, 0, 30, 20, MASTER_VOLUME)
    for hz, db, q in MASTER_BANDS:
        emit(hz, round(db * 10), round(q * 100))
    emit(1,
         "MATRIX_LOOPS", 0, "AUX_OUTPUTS")
    for _ in range(count):
        emit(tracks, *([0] * tracks))
    emit("PAD_MARKERS", 0, "INSTRUMENT_INPUTS")
    for _ in range(count):
        emit(tracks, *([0] * tracks))
    emit("AUDIO_PROPERTIES")
    for _ in range(count):
        emit(tracks, *([0] * tracks))
    emit("AUDIO_GROUPS", 0, 0, "PROJECT_OPTIONS", 0, 1, "MIDI_KNOBS", 0)
    return records, patterns


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plugin", default="~/.local/plugins/VST3/Mla09.vst3")
    parser.add_argument("--output", type=Path,
                        default=Path(__file__).resolve().parents[1] / "mla09_detroit.mlaproj/Project.mlack")
    args = parser.parse_args()
    records, patterns = project(str(Path(args.plugin).expanduser()))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    lines = [json.dumps(record, ensure_ascii=False) for record in records]
    args.output.write_text('{\n  "format": "mlacker-project",\n  "version": 1,\n  "records": [\n    '
                           + ',\n    '.join(lines) + '\n  ]\n}\n')
    used = sorted({n[0] for p in patterns for t in p.notes.values() for n in t.values()})
    seconds = len(SONG) * ROWS * 15 / BPM
    print(f"Wrote {args.output}: {len(patterns)} patterns, song of {len(SONG)} ({seconds:.0f} s at {BPM} BPM); "
          f"sounds {' '.join(used)}")


if __name__ == "__main__":
    main()
