#!/usr/bin/env python3
"""Import Chris Holm's Central Park MIDI as a small-pattern Mla SID song."""
import argparse
from collections import defaultdict, deque
import hashlib
import json
import math
from pathlib import Path
import struct

BPM = 124
PATTERN_ROWS = 32
SOURCE_SHA256 = "45ec658a2820fd2cbc447cd3b8b4a543acdd5de459dda29f57e81d950d1d212a"
NOTICE = (
    "Last Ninja 2 - Central Park\n"
    "Original music: Matt Gray. Copyright 1988 System 3.\n"
    "Source MIDI arrangement: Chris Holm (level-1.mid).\n"
    "Mla SID adaptation; intro/timbre reference: CoGFV_xxR64.\n"
    "Music rights remain with their respective owners.\n"
    "Music is not covered by mlacker's software license.\n"
    "This project grants no redistribution or commercial-use rights."
)


def read_midi(path):
    data = path.read_bytes()
    if data[:4] != b"MThd":
        raise ValueError("Not a standard MIDI file")
    size, format_, count, ppqn = struct.unpack_from(">IHHH", data, 4)
    if format_ not in (0, 1) or ppqn & 0x8000 or ppqn == 0:
        raise ValueError("Expected synchronous MIDI with quarter-note timing")
    position = 8 + size
    tracks, tempos = [], []
    for _ in range(count):
        if data[position:position + 4] != b"MTrk":
            raise ValueError("Missing MIDI track")
        length = int.from_bytes(data[position + 4:position + 8], "big")
        chunk = data[position + 8:position + 8 + length]
        if len(chunk) != length:
            raise ValueError("Truncated MIDI track")
        position += 8 + length
        at, tick, running = 0, 0, None
        active = defaultdict(deque)
        track = dict(names=[], notes=[], controls=[], bends=[], text=[], end=0)

        def vlq():
            nonlocal at
            value = 0
            for _ in range(4):
                byte = chunk[at]
                at += 1
                value = value * 128 + (byte & 127)
                if byte < 128:
                    return value
            raise ValueError("Invalid MIDI variable-length integer")

        while at < len(chunk):
            tick += vlq()
            status = chunk[at]
            if status & 128:
                at += 1
            elif running is not None:
                status = running
            else:
                raise ValueError("Missing MIDI running status")
            if status == 255:
                kind = chunk[at]
                at += 1
                length = vlq()
                value = chunk[at:at + length]
                at += length
                if kind in (3, 4):
                    track["names"].append(value.decode("latin1"))
                if kind in (1, 2):
                    track["text"].append(value.decode("latin1"))
                if kind == 81:
                    tempos.append((tick, int.from_bytes(value, "big")))
                continue
            if status in (240, 247):
                length = vlq()
                at += length
                running = None
                continue
            if not 128 <= status < 240:
                raise ValueError("Unsupported MIDI status")
            running = status
            kind, channel = status >> 4, status & 15
            length = 1 if kind in (12, 13) else 2
            args = chunk[at:at + length]
            at += length
            if kind == 9 and args[1]:
                active[channel, args[0]].append((tick, args[1]))
            elif kind == 8 or (kind == 9 and not args[1]):
                if active[channel, args[0]]:
                    start, velocity = active[channel, args[0]].popleft()
                    track["notes"].append(dict(start=start, end=tick, pitch=args[0], velocity=velocity))
            elif kind == 11:
                track["controls"].append((tick, args[0], args[1]))
            elif kind == 14:
                track["bends"].append((tick, args[0] + args[1] * 128 - 8192))
        if any(active.values()):
            raise ValueError("MIDI contains unterminated notes")
        track["notes"].sort(key=lambda n: (n["start"], n["pitch"]))
        track["end"] = tick
        tracks.append(track)
    if not tempos or any(abs(60_000_000 / value - BPM) > 0.001 for _, value in tempos):
        raise ValueError("Expected the supplied constant 124 BPM arrangement")
    return dict(ppqn=ppqn, tracks=tracks, sha256=hashlib.sha256(data).hexdigest())


# Source track, name, waveform nibble, ADSR nibbles, PWM depth/rate,
# output dB, cutoff (None = bypass), resonance, optional source/output drum pitch.
PARTS = [
    (1, "Wide PWM bass", 4, (0, 9, 12, 2), 0.94, 0.245, -12, 1500, 4, None),
    (2, "Kanoon pulse", 4, (0, 8, 2, 3), 0.70, 0.60, -11, None, 0, None),
    (3, "Pulse bass L", 4, (0, 4, 9, 2), 0.95, 0.37, -18, 1400, 5, None),
    (4, "Pulse bass R", 4, (0, 5, 9, 2), 0.95, 0.43, -18, 1400, 5, None),
    (5, "PWM bass layer", 4, (0, 9, 11, 3), 0.94, 0.16, -17, 1400, 4, None),
    (6, "Converge", 4, (8, 9, 10, 7), 0.85, 0.45, -23, 1300, 3, None),
    (7, "Ana PWM pad", 4, (4, 8, 10, 6), 0.75, 0.35, -18, 1500, 2, None),
    (8, "SID kick", 1, (0, 5, 0, 0), 0.0, 0.5, -8, None, 0, (35, 43)),
    (8, "SID snare", 8, (0, 6, 0, 1), 0.0, 0.5, -8, None, 0, (38, 83)),
    (8, "SID shaker", 8, (0, 1, 0, 0), 0.0, 0.5, -22, None, 0, (82, 92)),
    (-1, "SID intro arp", 1, (0, 9, 0, 0), 0.0, 0.5, -5, None, 0, None),
]


def parameters(part):
    _, _, wave, adsr, depth, rate, db, cutoff, resonance, drum = part
    p = {key: 0.0 for key in range(100, 126)}
    p.update({key: 0.0 for key in range(300, 325)})
    p.update({100: (db + 60) / 66, 103: 0.0, 104: 1.0,
              106: 1.0 if drum and drum[0] == 35 else 2 / 24,
              107: 8192 / 16383, 109: math.log(6 / 0.5) / math.log(24),
              111: depth, 112: math.log(rate / 0.05) / math.log(200),
              113: 0.5, 114: math.log(0.3 / 0.01) / math.log(500),
              116: 0.5, 117: 1 / 15, 119: 1.0, 120: 0.7,
              121: (cutoff or 0) / 2047, 122: resonance / 15,
              123: 1 / 7 if cutoff else 0.0, 124: 1.0})
    for voice in range(3):
        b = 200 + voice * 20
        p.update({b + field: 0.0 for field in range(14)})
        p.update({b: wave / 15, b + 1: 2048 / 4095,
                  b + 2: adsr[0] / 15, b + 3: adsr[1] / 15,
                  b + 4: adsr[2] / 15, b + 5: adsr[3] / 15,
                  b + 9: float(cutoff is not None), b + 10: float(drum is None or voice == 0),
                  b + 11: 0.5, b + 12: 0.5})
        if drum and voice > 0:
            p[b] = 0.0
    if drum:
        p[104] = 0.0
    if part[0] in (1, 3, 4, 5):
        # Monophonic legato, with the source octave pair voiced by two SID
        # oscillators for the sustained basses. CC5 supplies the slide time.
        p.update({103: 1 / 3, 104: 0.0})
        for voice in (1, 2):
            if part[0] in (1, 5) and voice == 1:
                p[231] = 36 / 48  # +12 semitones.
            else:
                p[200 + voice * 20] = 0.0
                p[210 + voice * 20] = 0.0
    if part[1] == "SID snare":
        # Clock the noise LFSR rapidly, then step its pitch downward using
        # the full +/-24-semitone bend range. Never switch to a tonal wave.
        p[103] = 1 / 3
        p[106] = 1.0
    if part[1] == "SID intro arp":
        # Four octaves, one octave step per PAL player frame. Only one SID
        # oscillator sounds; the held chord is the arpeggiator's input.
        p.update({103: 2 / 3, 104: 0.0, 117: 0.0, 212: 0.85})
        for voice in (1, 2):
            p[200 + voice * 20] = 0.0
            p[210 + voice * 20] = 0.0
    assert len(p) == 93
    return p


def score(midi):
    ppqn = midi["ppqn"]
    # Keep the MIDI's first eight beats and fill them with the reference's
    # missing SID intro instead of trimming them as empty setup time.
    origin = 0
    first_midi_note = min(n["start"] for t in midi["tracks"] for n in t["notes"])
    end = max(t["end"] for t in midi["tracks"])
    rows = math.ceil((end - origin) * 4 / ppqn)
    if rows % PATTERN_ROWS:
        raise ValueError("Expected complete two-bar patterns")
    parts = []
    for definition in PARTS:
        drum = definition[-1]
        if definition[0] == -1:
            bass = midi["tracks"][1]["notes"]
            notes = []
            for beat in range(end // ppqn):
                base = 65
                if beat * ppqn >= first_midi_note:
                    held = [n for n in bass if n["start"] <= beat * ppqn]
                    latest = max((n["start"] for n in held), default=bass[0]["start"])
                    root = min(n["pitch"] for n in (held or bass) if n["start"] == latest)
                    base = 60 + root % 12
                notes.extend([beat * 400, beat * 400 + 396, base + octave * 12, 100]
                             for octave in range(4))
            parts.append(dict(definition=definition, notes=notes, lines=4,
                              automation=[("cc:7", 0, 127), ("pitchbend", -8192, 8191)], width=18))
            continue
        source = midi["tracks"][definition[0]]
        notes = []
        for n in source["notes"]:
            if drum and n["pitch"] != drum[0]:
                continue
            start = round((n["start"] - origin) * 400 / ppqn)
            finish = round((n["end"] - origin) * 400 / ppqn)
            notes.append([start, max(start + 1, finish), drum[1] if drum else n["pitch"], n["velocity"]])
        if definition[0] in (1, 5):
            # These tracks contain root+octave pairs at every onset. Unison
            # supplies the upper octave, so one key can slide both voices.
            grouped = defaultdict(list)
            for note in notes:
                grouped[note[0]].append(note)
            notes = [min(chord, key=lambda n: n[2]) for chord in grouped.values()]
        # Some MIDI octave-bass retriggers overlap by a few ticks. Release the
        # old equal pitch at its retrigger, avoiding a delayed note-off.
        previous = {}
        for note in notes:
            if note[2] in previous:
                old = previous[note[2]]
                old[1] = min(old[1], note[0])
            previous[note[2]] = note
        glide = {}
        if definition[0] in (1, 3, 4, 5):
            for i, note in enumerate(notes):
                time = 0
                if i > 0:
                    old = notes[i - 1]
                    if old[2] != note[2] and old[1] >= note[0] - 2:
                        old[1] = max(old[1], note[0] + 2)
                        time = 28 if definition[0] in (1, 5) else 16
                glide[note[0]] = time
        counts = defaultdict(int)
        for note in notes:
            counts[note[0] // 100] += 1
        lines = max(1, max(counts.values(), default=1))
        automation = []
        if definition[0] in (1, 3, 4, 5):
            automation = [(f"cc:{cc}", 0, 127) for cc in (83, 5, 74, 71, 80)]
        elif drum and drum[0] in (35, 38):
            automation = [("pitchbend", -8192, 8191)]
        parts.append(dict(definition=definition, notes=notes, lines=lines,
                          automation=automation, width=lines * 4 + len(automation), glide=glide))
    return parts, rows, origin


def project(midi, plugin):
    records = []

    def emit(*values):
        for value in values:
            records.append(["string" if isinstance(value, str) else "real" if isinstance(value, float) else "integer", value])

    parts, total_rows, origin = score(midi)
    patterns = total_rows // PATTERN_ROWS
    tracks = len(parts)
    emit("MLACK", 1, 5, BPM, 4, 4, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
         0, 1, 60, 0, 1, 0, 0, 0, 0, 0, 0)
    emit(0, tracks)
    for index, part in enumerate(parts):
        params = parameters(part["definition"])
        emit(index + 1, plugin, len(params))
        for key, value in sorted(params.items()):
            emit(key, float(value))
    emit(1, patterns + 1, patterns)
    names = ["C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"]
    for pattern in range(patterns):
        bar = origin // (midi["ppqn"] * 4) + pattern * 2 + 1
        emit(pattern + 1, f"Central Park {bar:03d}-{bar + 1:03d}")
        emit(tracks + 1, 0, 1, 0, 0, 1, 1, 0, tracks)
        for index, part in enumerate(parts):
            pan = -65 if index == 2 else 65 if index == 3 else 0
            emit(part["definition"][1], 0, 100, pan, 0, 1, index + 1, part["lines"], 0, 1, len(part["automation"]))
            for parameter, low, high in part["automation"]:
                emit(parameter, low, high)
            emit(0)
        rows = [[str(row)] + [""] * sum(p["width"] for p in parts) for row in range(PATTERN_ROWS)]
        first = pattern * PATTERN_ROWS * 100
        last = first + PATTERN_ROWS * 100
        column = 1
        for part in parts:
            automation = column + part["lines"] * 4
            source = part["definition"][0]
            if source in (1, 3, 4, 5):
                definition = part["definition"]
                rows[0][automation:automation + 5] = [
                    "42", "28" if source in (1, 5) else "16",
                    str(round(definition[7] * 127 / 2047)),
                    str(round(definition[8] * 127 / 15)), "18",
                ]
                for row in range(PATTERN_ROWS):
                    now = first + row * 100
                    active = [n for n in part["notes"] if n[0] <= now < n[1]]
                    if not active:
                        continue
                    head = max(active, key=lambda n: n[0])
                    if source in (3, 4):
                        rows[row][automation + 2] = "100 92 84 76"
                    else:
                        age = (now - head[0]) / 100
                        rows[row][automation + 2] = str(round(108 - 32 * min(age / 12, 1)))
            elif source == -1:
                # Bring the continuing arp behind the melody, and remove the
                # intro's +35-cent reference tuning once the MIDI enters.
                rows[0][automation:automation + 2] = ["106" if pattern == 0 else "94",
                                                      "0" if pattern == 0 else "-1434"]
            used = defaultdict(int)
            for start, end, pitch, velocity in part["notes"]:
                if not first <= start < last:
                    continue
                row, offset = divmod(start - first, 100)
                at = column + used[row] * 4
                rows[row][at:at + 4] = [f"{names[pitch % 12]}{pitch // 12 - 1}", str(velocity),
                                      f"{(end - start) / 100:.2f}", f"{offset / 100:.2f}"]
                used[row] += 1
                drum = part["definition"][-1]
                if source in (1, 3, 4, 5):
                    rows[row][automation + 1] = str(part["glide"][start])
                elif drum and drum[0] == 35:
                    rows[row][automation] = "0 -4096 -6144 ."
                elif drum and drum[0] == 38:
                    rows[row][automation] = "4096 0 -4096 -8192"
            column += part["width"]
        emit(PATTERN_ROWS)
        for row in rows:
            emit(len(row), *row)
    emit(patterns, *range(1, patterns + 1), "MIDI_LEARN", 0, "AUX_EFFECTS", 0)
    emit(*([tracks] * patterns), 0, 0, "TRACK_INSERTS", 0)
    for _ in range(patterns):
        emit(tracks, *([0] * (tracks * 4)))
    emit(0, -1, "SAMPLER_PADS", 0, "SONG_MATRIX", 0, "TRACK_OUTPUTS")
    for _ in range(patterns):
        emit(tracks, *([0] * tracks))
    emit("MATRIX_GRID", PATTERN_ROWS, "MASTER_BUS", 0, 0, 30, 20, 100,
         100, 0, 100, 500, 0, 100, 2500, 0, 100, 8000, 0, 100, 1,
         "MATRIX_LOOPS", 0, "AUX_OUTPUTS")
    for _ in range(patterns):
        emit(tracks, *([0] * tracks))
    emit("PAD_MARKERS", 0, "INSTRUMENT_INPUTS")
    for _ in range(patterns):
        emit(tracks, *([0] * tracks))
    emit("AUDIO_PROPERTIES")
    for _ in range(patterns):
        emit(tracks, *([0] * tracks))
    emit("AUDIO_GROUPS", 0, 0, "PROJECT_OPTIONS", 0, 1, "MIDI_KNOBS", 0,
         "COPYRIGHT_NOTICE", NOTICE)
    return {"format": "mlacker-project", "version": 1, "records": records}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("midi", type=Path)
    parser.add_argument("--plugin", default="~/.local/plugins/VST3/MlaSid.vst3")
    parser.add_argument("--output", type=Path,
                        default=Path(__file__).resolve().parents[1] / "last_ninja_2_loading.mlaproj/Project.mlack")
    args = parser.parse_args()
    midi = read_midi(args.midi)
    if midi["sha256"] != SOURCE_SHA256:
        parser.error("Expected the documented Chris Holm level-1.mid (SHA-256 mismatch)")
    document = project(midi, str(Path(args.plugin).expanduser()))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    lines = [json.dumps(record, ensure_ascii=False) for record in document["records"]]
    args.output.write_text('{\n  "format": "mlacker-project",\n  "version": 1,\n  "records": [\n    '
                           + ',\n    '.join(lines) + '\n  ]\n}\n')
    parts, rows, origin = score(midi)
    print(f"Wrote {args.output}: {rows // PATTERN_ROWS} patterns, {len(parts)} parts, {rows * 15 / BPM:.2f} s")
    print(f"Source SHA-256: {midi['sha256']}; retained setup beats for the SID intro")


if __name__ == "__main__":
    main()
