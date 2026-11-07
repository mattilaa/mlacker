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
CC_BD_LEVEL, CC_SD_LEVEL = 20, 21
CC_ACCENT, CC_BD_TUNE, CC_BD_ATTACK, CC_BD_DECAY = 102, 103, 104, 105
CC_SD_TUNE, CC_SD_TONE, CC_SD_SNAPPY = 106, 107, 108
CC_CH_DECAY, CC_OH_DECAY, CC_CR_TUNE, CC_RD_TUNE = 115, 116, 117, 118

# Tracks, all playing the one Mla 09: name, note lines, CC columns.
TRACKS = [
    # Every bass drum and snare knob, in panel order.
    ("BD", 1, [CC_BD_LEVEL, CC_BD_TUNE, CC_BD_ATTACK, CC_BD_DECAY, CC_ACCENT]),
    ("SD CP", 2, [CC_SD_LEVEL, CC_SD_TUNE, CC_SD_TONE, CC_SD_SNAPPY]),
    ("RS", 1, []),
    ("Toms", 1, []),
    ("Hats", 1, [CC_CH_DECAY, CC_OH_DECAY]),
    ("Cymbals", 2, [CC_CR_TUNE, CC_RD_TUNE]),
]

# The panel's starting position: tight kick with plenty of click, crisp snare,
# short closed hat. CC columns move these while the song plays.
# Levels: 115 is about unity (0 dB of -60 .. +6 dB).
KNOBS = {CC_BD_LEVEL: 115, CC_BD_TUNE: 60, CC_BD_ATTACK: 83, CC_BD_DECAY: 52, CC_ACCENT: 72,
         CC_SD_LEVEL: 115, CC_SD_TUNE: 64, CC_SD_TONE: 80, CC_SD_SNAPPY: 84,
         CC_CH_DECAY: 40, CC_OH_DECAY: 56, CC_CR_TUNE: 64, CC_RD_TUNE: 64}


def parameters():
    """Mla 09's 31 parameters, normalized (IDs 100-130, see its README)."""
    unity = 60 / 66
    p = {key: 0.5 for key in range(100, 131)}
    for level in (100, 103, 107, 111, 114, 117, 120, 121, 122, 124, 126, 128):
        p[level] = unity
    p[102] = 0.0  # 909 Accent dynamics.
    p[100] = (60 - 3) / 66  # Output -3 dB: accented kicks and crashes keep headroom.
    p[126] = (60 - 2) / 66  # CR Level.
    p[128] = (60 - 4) / 66  # RD Level.
    p[124] = (60 - 1) / 66  # OH Level.
    # Toms tuned up a little, as Detroit tom grooves often are.
    p[112], p[115], p[118] = 0.6, 0.6, 0.62
    p.update({101: KNOBS[CC_ACCENT] / 127, 103: KNOBS[CC_BD_LEVEL] / 127, 104: KNOBS[CC_BD_TUNE] / 127,
              105: KNOBS[CC_BD_ATTACK] / 127, 106: KNOBS[CC_BD_DECAY] / 127, 107: KNOBS[CC_SD_LEVEL] / 127,
              108: KNOBS[CC_SD_TUNE] / 127, 109: KNOBS[CC_SD_TONE] / 127, 110: KNOBS[CC_SD_SNAPPY] / 127, 123: KNOBS[CC_CH_DECAY] / 127,
              125: KNOBS[CC_OH_DECAY] / 127, 127: KNOBS[CC_CR_TUNE] / 127, 129: KNOBS[CC_RD_TUNE] / 127,
              130: 0.0})  # Drive off: the clean machine.
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
                self.ccs[name][(0, cc)] = KNOBS[cc]

    def hit(self, track, row, note, velocity=NORMAL, line=0, offset=0.0):
        self.notes[track][(row, line)] = (note, velocity, offset)

    def knob(self, track, row, cc, value):
        self.ccs[track][(row, cc)] = value

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
    p.ramp("BD", CC_BD_DECAY, 30, 52, every=8)
    crash(p)
    return p


def drive():
    p = Pattern("Drive")
    kick(p)
    hats(p)
    rim_threes(p)
    return p


def clap_section():
    p = Pattern("Clap")
    kick(p)
    hats(p)
    clap(p)
    rim_threes(p, start=1)
    ride_offbeats(p, 32, ROWS)
    tom_fill(p)
    return p


def tom_section():
    p = Pattern("Tom Grid")
    kick(p)
    hats(p, open_hats=False)
    clap(p)
    tom_cycle(p)
    crash(p)
    # Pull the kick back a little so the toms come through.
    p.knob("BD", 0, CC_BD_LEVEL, 106)
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
    # The kick comes back only on the last beat, under the toms.
    p.hit("BD", 60, BD, ACCENT)
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
    # The snare drops lower for the second half.
    p.knob("SD CP", 32, CC_SD_TUNE, 30)
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
    p.knob("BD", 0, CC_BD_DECAY, 70)
    return p


def outro():
    """Strip down to kick and hats; the kick's decay closes, a last crash."""
    p = Pattern("Outro")
    kick(p, skip=(60,))
    hats(p, open_hats=True, density=8)
    for row in range(0, 32):
        if row % 16 in (4, 12):
            p.hit("SD CP", row, CP, NORMAL)
    p.ramp("BD", CC_BD_DECAY, 90, 25, every=4)
    p.ramp("Cymbals", CC_CR_TUNE, 64, 20, 48, ROWS - 1, every=4)
    crash(p, 48)
    return p


PATTERNS = [ignition, drive, clap_section, tom_section, roll, exhibition, jack, waveform, outro]
# Pattern numbers (1-based, in PATTERNS order) in song order.
SONG = [1, 2, 2, 3, 4, 6, 7, 5, 6, 8, 7, 9]


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
    emit("MATRIX_GRID", ROWS, "MASTER_BUS", 0, 0, 30, 20, 100,
         100, 0, 100, 500, 0, 100, 2500, 0, 100, 8000, 0, 100, 1,
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
