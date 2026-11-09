#!/usr/bin/env python3
"""Put the captured Beast register streams on their musical sixteenth-note grid.

The intro's note starts are floor(step * 16 / 3) PAL frames; level 1 uses
step * 7. Repack those steps into four-bar patterns and a one-bar matrix grid.
No samples, instruments, register events, or musical note starts are discarded.
The integer BPM field uses the nearest tempos, 141 and 107 respectively.
"""
import argparse
import copy
from decimal import Decimal
from fractions import Fraction
import json
from pathlib import Path

CONFIG = {"intro": (Fraction(16, 3), 141), "level_1": (Fraction(7), 107)}
MAGIC = bytes((0x7D, 0x4D, 0x50, 0x41, 0x55, 1))


class Project:
    def __init__(self, path):
        self.path = Path(path)
        self.document = json.loads(self.path.read_text(), parse_float=Decimal)
        self.records = self.document["records"]
        self.position = 0
        assert [self.take() for _ in range(3)] == ["MLACK", 1, 5]
        self.view = [self.take() for _ in range(24)]
        self.samples = [[self.take() for _ in range(5)] for _ in range(self.take())]
        self.plugins = []
        for _ in range(self.take()):
            slot, path, count = self.take(), self.take(), self.take()
            self.plugins.append((slot, path, [(self.take(), self.take()) for _ in range(count)]))
        self.id_position = self.position
        self.active, self.next_id, count = self.take(), self.take(), self.take()
        self.patterns = []
        for _ in range(count):
            pattern = dict(id=self.take(), name=self.take(), view=[self.take() for _ in range(8)])
            tracks = []
            for _ in range(self.take()):
                metadata = [self.take() for _ in range(10)]
                automation = [[self.take() for _ in range(3)] for _ in range(self.take())]
                audio = [[self.take() for _ in range(4)] for _ in range(self.take())]
                tracks.append((metadata, automation, audio))
            pattern["tracks"] = tracks
            pattern["rows"] = [[self.take() for _ in range(self.take())] for _ in range(self.take())]
            self.patterns.append(pattern)
        self.song = [self.take() for _ in range(self.take())]
        self.extensions = self.records[self.position:]

    def take(self):
        value = self.records[self.position][1]
        self.position += 1
        return value


def operations(frame):
    cursor = 0
    while cursor < len(frame):
        op = frame[cursor]
        length = {1: 3, 2: 3, 3: 3, 4: 1, 5: 0, 6: 0}[op]
        assert cursor + length < len(frame)
        yield op, frame[cursor + 1:cursor + length + 1]
        cursor += length + 1


def decode_packet(text):
    data = bytes.fromhex(text)
    assert data[:6] == MAGIC and 1 <= data[7] <= 8 and not data[11] & ~3
    cursor, frames, snapshot = 12, [], None
    for index in range(data[7] + bool(data[11] & 2)):
        count = data[cursor]
        frame = data[cursor + 1:cursor + 1 + count]
        assert len(frame) == count
        list(operations(frame))
        cursor += count + 1
        if data[11] & 2 and index == 0:
            snapshot = frame
        else:
            frames.append(frame)
    assert cursor == len(data)
    micros = data[8] | data[9] << 7 | data[10] << 14
    return data[6], micros, snapshot, frames


class Registers:
    def __init__(self):
        self.values = {1: b'\x7f\x7f\x7f', 2: b'\0\0\0', 3: b'\x2c\x03\0', 4: b'\0'}
        self.active = False
        self.latched = None

    def apply(self, frame, compact=False):
        result = bytearray()
        for op, value in operations(frame):
            if op <= 4:
                if not compact or self.values[op] != value:
                    result.extend(bytes((op,)) + value)
                self.values[op] = value
            else:
                result.append(op)
                if op == 5 and not self.active:
                    self.latched = self.values[1], self.values[2]
                self.active = op == 5
        return bytes(result)

    def snapshot(self):
        data = bytearray()
        if self.active:
            # Latch the playing sample before restoring its reload-loop registers.
            data.extend(b'\x01' + self.latched[0] + b'\x02' + self.latched[1] + b'\x05')
        for op in range(1, 5):
            if self.active and op in (1, 2) and self.values[op] == self.latched[op - 1]:
                continue
            data.extend(bytes((op,)) + self.values[op])
        return bytes(data)


def encode_packet(channel, frames, micros, snapshot=None):
    data = bytearray(MAGIC + bytes((channel, len(frames), micros & 127,
                                   micros >> 7 & 127, micros >> 14 & 127, 2 if snapshot is not None else 0)))
    for frame in ([snapshot] if snapshot is not None else []) + frames:
        data.extend(bytes((len(frame),)) + frame)
    # The ID column carries byte 7D separately; SYSEX permits 80 data bytes.
    assert len(data) <= 81, f"channel {channel + 1}: packet needs {len(data) - 1} data bytes"
    return data[1:].hex(' ').upper()


def streams(project):
    lanes = [[] for _ in range(4)]
    initial = [None] * 4
    for pattern in project.patterns:
        assert len(pattern["tracks"]) == 1
        channel = pattern["tracks"][0][0][6] - 1
        assert 0 <= channel < 4
        for row in pattern["rows"]:
            assert len(row) == 8 and not any(row[1:6]) and row[6] == "7D"
            found, micros, snapshot, frames = decode_packet("7D " + row[7])
            assert found == channel and micros == 20000
            if not lanes[channel]:
                assert snapshot is not None
                initial[channel] = snapshot
            lanes[channel].extend(frames)
    assert len({len(lane) for lane in lanes}) == 1
    return lanes, initial


def record(value):
    return ["integer" if isinstance(value, int) else "string", value]


def build_patterns(project, step, bpm):
    lanes, initial = streams(project)
    source_frames = len(lanes[0])
    steps = (source_frames * step.denominator + step.numerator - 1) // step.numerator
    total_rows = (steps + 15) // 16 * 16
    count = (total_rows + 63) // 64
    templates = {pattern["tracks"][0][0][6] - 1: pattern for pattern in project.patterns[:4]}
    rows_by_lane = []
    for channel, source in enumerate(lanes):
        registers = Registers()
        registers.apply(initial[channel])
        rows = []
        for row in range(total_rows):
            start = int(row * step)
            end = int((row + 1) * step)
            raw = [source[index] if index < source_frames else (b'\x06\x04\0' if index == source_frames else b'')
                   for index in range(start, end)]
            snapshot = registers.snapshot() if row % 64 == 0 else None
            compact = [registers.apply(frame, compact=True) for frame in raw]
            if snapshot is not None and {1, 2, 3, 4, 5}.issubset({op for op, _ in operations(raw[0])}):
                # This row fully initializes the voice itself, including a cold start.
                snapshot = None
                compact[0] = raw[0]
            micros = round(Fraction(15_000_000, bpm) / step)
            payload = encode_packet(channel, compact, micros, snapshot)
            rows.append([f"{row % 64 + 1:03}", "", "", "", "", "", "7D", payload])
        rows_by_lane.append(rows)
    patterns = []
    for part in range(count):
        start, end = part * 64, min((part + 1) * 64, total_rows)
        for channel in range(4):
            pattern = copy.deepcopy(templates[channel])
            pattern["id"] = part * 4 + channel + 1
            pattern["name"] = f"Paula {channel + 1} {'L' if channel in (0, 3) else 'R'} bars {start // 16 + 1:03}-{end // 16:03}"
            pattern["view"][1:5] = [0, 1, 0, 0]
            pattern["rows"] = rows_by_lane[channel][start:end]
            patterns.append(pattern)
    return patterns, total_rows // 16, source_frames


def extensions(project, patterns, bars):
    """Repeat per-pattern extension records and rebuild the four matrix lanes."""
    groups, current = {}, None
    for item in project.extensions:
        if item[0] == "string" and item[1] in {
            "MIDI_LEARN", "AUX_EFFECTS", "TRACK_INSERTS", "SAMPLER_PADS", "SONG_MATRIX", "TRACK_OUTPUTS",
            "MATRIX_GRID", "MASTER_BUS", "MATRIX_LOOPS", "AUX_OUTPUTS", "PAD_MARKERS", "INSTRUMENT_INPUTS",
            "AUDIO_PROPERTIES", "AUDIO_GROUPS", "PROJECT_OPTIONS", "MIDI_KNOBS", "COPYRIGHT_NOTICE"}:
            current = item[1]
            groups[current] = [item]
        else:
            groups[current].append(item)
    result = []
    for tag, original in groups.items():
        values = [item[1] for item in original[1:]]
        if tag == "AUX_EFFECTS":
            assert values[:-2] == [0] + [1] * len(project.patterns)
            values = [0] + [1] * len(patterns) + values[-2:]
        elif tag == "TRACK_INSERTS":
            assert values == [0] + [1, 0, 0, 0, 0] * len(project.patterns) + [0, -1]
            values = [0] + [1, 0, 0, 0, 0] * len(patterns) + [0, -1]
        elif tag == "SONG_MATRIX":
            assert values[0] == 3
            values = [3]
            for lane in range(1, 4):
                values += [bars] + [(bar // 4) * 4 + lane + 1 if bar % 4 == 0 else 0 for bar in range(bars)]
        elif tag == "TRACK_OUTPUTS" or tag == "INSTRUMENT_INPUTS" or tag == "AUDIO_PROPERTIES" or tag == "AUX_OUTPUTS":
            assert values == [1, 0] * len(project.patterns), f"unsupported customized {tag}"
            values = [1, 0] * len(patterns)
        elif tag == "MATRIX_GRID":
            assert values == [64]
            values = [16]
        else:
            result.extend(original)
            continue
        result += [record(tag)] + [record(value) for value in values]
    return result


def save(project, patterns, bars, bpm):
    records = copy.deepcopy(project.records[:project.id_position])
    records[3][1] = bpm
    # Reset the transport and selection to the start of the aligned arrangement.
    for index in (6, 7, 10, 11):
        records[index][1] = 0
    records += [record(1), record(len(patterns) + 1), record(len(patterns))]
    for pattern in patterns:
        values = [pattern["id"], pattern["name"], *pattern["view"], len(pattern["tracks"])]
        for metadata, automation, audio in pattern["tracks"]:
            values += metadata + [len(automation)]
            for slot in automation:
                values += slot
            values += [len(audio)]
            for instance in audio:
                values += instance
        values += [len(pattern["rows"])]
        for row in pattern["rows"]:
            values += [len(row), *row]
        records += [record(value) for value in values]
    records += [record(bars)] + [record((bar // 4) * 4 + 1 if bar % 4 == 0 else 0) for bar in range(bars)]
    records += extensions(project, patterns, bars)
    def encode(item):
        kind, value = item
        value_text = str(value) if isinstance(value, Decimal) else json.dumps(value, ensure_ascii=False)
        return f'    ["{kind}", {value_text}]'
    text = '{\n  "format": "mlacker-project",\n  "version": 1,\n  "records": [\n' + ',\n'.join(map(encode, records)) + '\n  ]\n}\n'
    project.path.write_text(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--check", action="store_true", help="validate packet sizes without writing")
    args = parser.parse_args()
    for name, (step, bpm) in CONFIG.items():
        project = Project(args.root / f"examples/shadow_of_the_beast_{name}.mlaproj/Project.mlack")
        if project.view[0] == bpm and " bars " in project.patterns[0]["name"]:
            print(f"{name}: already aligned")
            continue
        assert project.view[0] == 125, "expected the original fixed 125 BPM capture"
        patterns, bars, frames = build_patterns(project, step, bpm)
        extensions(project, patterns, bars)
        print(f"{name}: {bpm} BPM, {len(patterns)} patterns, {bars} bars; {frames} captured frames per channel")
        if not args.check:
            save(project, patterns, bars, bpm)


if __name__ == "__main__":
    main()
