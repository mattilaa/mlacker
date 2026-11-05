#!/usr/bin/env python3
"""Recreate the YouTube Central Park in-game performance with Mla SID register frames.

Requires a separately obtained HVSC Last_Ninja_2.sid and py65==1.2.0 only
during generation. The native project has no external music/player dependency.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

SOURCE_SHA256 = "7686b46aada0a107dd9717b47e76209acca9e47b92a6ea10657fcbd63f24588a"
BPM = 188
FRAMES_PER_ROW = 4
PATTERN_ROWS = 48
FRAME_MICROS = round(15 / BPM / FRAMES_PER_ROW * 1_000_000)
PLAYER_FRAMES = 16960
MAGIC = [0x7D, 0x4D, 0x53, 0x49, 0x44, 1]
NOTICE = (
    "Last Ninja 2 - Central Park (in-game)\n"
    "Original music/player: Matt Gray. Release: 1988 System 3.\n"
    "Reference: youtube.com/watch?v=CoGFV_xxR64\n"
    "Original SID voice programs, sequence and mix via Mla SID.\n"
    "Music rights remain with their respective owners.\n"
    "Music is not covered by mlacker's software license.\n"
    "This project grants no redistribution or commercial-use rights."
)


def trace(data):
    from py65.devices.mpu6502 import MPU
    offset, load, init, play = struct.unpack_from(">4H", data, 6)
    payload = data[offset:]
    if load == 0:
        load = int.from_bytes(payload[:2], "little")
        payload = payload[2:]

    class Memory(list):
        def __init__(self):
            super().__init__([0] * 65536)
            self.writes = []

        def __setitem__(self, address, value):
            if isinstance(address, int) and 0xD400 <= address <= 0xD418:
                self.writes.append((address - 0xD400, value))
            super().__setitem__(address, value)

    memory = Memory()
    memory[load:load + len(payload)] = payload
    cpu = MPU(memory=memory)

    def call(address):
        cpu.sp = 0xFF
        cpu.stPushWord(0xFFFE)
        cpu.pc = address
        for _ in range(100000):
            if cpu.pc == 0xFFFF:
                return
            cpu.step()
        raise RuntimeError(f"6502 routine did not return: ${cpu.pc:04x}")

    cpu.a = 1  # HVSC subtune 2: Central Park (in-game), as in the video.
    call(init)
    # Begin with the player itself; video lead-in silence is not score data.
    frames = []
    for _ in range(PLAYER_FRAMES):
        memory.writes = []
        call(play)
        frames.append((memory[0xD400:0xD419], memory.writes))
    quantum = PATTERN_ROWS * FRAMES_PER_ROW
    while len(frames) % quantum:
        frames.append((frames[-1][0].copy(), []))
    return frames


def placements(frames, width):
    """Centre whole drum gestures, including pulse tails between noise frames."""
    sides = [-width, width, -width]
    result = [[round(50 + side * 50) for side in sides] for _ in frames]
    for voice in range(3):
        control = voice * 7 + 4
        starts = [0]
        for index, (registers, writes) in enumerate(frames):
            gate = registers[control] & 1
            was_on = index > 0 and frames[index - 1][0][control] & 1
            if index and gate and (not was_on or any(r == control and not value & 1 for r, value in writes)):
                starts.append(index)
        starts.append(len(frames))
        for begin, end in zip(starts, starts[1:]):
            percussion = any(frame[0][control] & 0x80 for frame in frames[begin:end])
            # Pure tonal SID kicks have no noise frame: identify their rapid,
            # one-way frequency fall, excluding cyclic octave arpeggios.
            if not percussion:
                sample = frames[begin:min(end, begin + 32)]
                frequencies = [f[0][voice * 7] + 256 * f[0][voice * 7 + 1] for f in sample]
                percussion = (len(frequencies) >= 3 and frequencies[0] > 0
                              and frequencies[-1] < frequencies[0] / 2
                              and all(b <= a for a, b in zip(frequencies, frequencies[1:]))
                              and sample[0][0][voice * 7 + 6] >> 4 == 0)
            if percussion:
                for index in range(begin, end):
                    result[index][voice] = 50
    return result


def packet(frames, bank, reset=False, pans=None):
    micros = FRAME_MICROS
    result = MAGIC + [bank, len(frames), micros & 127, (micros >> 7) & 127,
                      (micros >> 14) & 127, int(reset) | (2 if pans is not None and bank < 3 else 0)]
    base = bank * 7
    count = 4 if bank == 3 else 7
    for index, (registers, writes) in enumerate(frames):
        values = registers[base:base + count]
        gate_reset = bank < 3 and bool(values[4] & 1) and any(
            r == base + 4 and not value & 1 for r, value in writes
        )
        mask = sum((value >> 7) << index for index, value in enumerate(values))
        result += [int(gate_reset), mask]
        if pans is not None and bank < 3:
            result += [pans[index][bank]]
        result += [value & 127 for value in values]
    assert len(result) <= 52 and all(0 <= value < 128 for value in result)
    return result


def parameters():
    p = {key: 0.0 for key in range(100, 126)}
    p.update({key: 0.0 for key in range(300, 325)})
    p.update({100: 64 / 66, 106: 2 / 24, 107: 8192 / 16383,
              113: 0.5, 116: 0.5})
    for voice in range(3):
        b = 200 + voice * 20
        p.update({b + field: 0.0 for field in range(14)})
        p.update({b + 11: 0.5, b + 12: 0.5})
    assert len(p) == 93
    return p


def project(frames, plugin, width=0.4):
    records = []
    pans = placements(frames, width) if width else None

    def emit(*values):
        for value in values:
            records.append(["string" if isinstance(value, str) else "real" if isinstance(value, float) else "integer", value])

    groups = len(frames) // (PATTERN_ROWS * FRAMES_PER_ROW)
    patterns = groups * 4
    params = parameters()
    emit("MLACK", 1, 5, BPM, 4, 4, 0, 0, 0, 1, 2, 0, 0, 0, 0, 0,
         0, 1, 60, 0, 1, 0, 0, 0, 0, 0, 0)
    emit(0, 1, 1, plugin, len(params))
    for key, value in sorted(params.items()):
        emit(key, float(value))
    emit(3, patterns + 1, patterns)
    names = ["SID 1 Bass / drums", "SID 2 Lead / modulation",
             "SID 3 Arp / percussion", "SID Volume / filter"]
    guard_names = ["C--1", "C#-1", "D--1", "D#-1", "E--1", "F--1"]
    for pattern in range(patterns):
        group_index, bank = divmod(pattern, 4)
        seconds = int(group_index * PATTERN_ROWS * 15 / BPM)
        segment = frames[group_index * PATTERN_ROWS * 4:(group_index + 1) * PATTERN_ROWS * 4]
        role = ["Bass", "Lead", "Arp", "Chip mix"][bank]
        if bank < 3:
            controls = [f[0][bank * 7 + 4] for f in segment]
            if not any(c & 0xF0 for c in controls):
                role = "Rest"
            elif any(c & 0x80 for c in controls):
                role = "Bass/drums" if bank == 0 else "Percussion"
            elif any(c & 4 for c in controls):
                role = "Ring lead"
        prefix = f"V{bank + 1} " if bank < 3 else ""
        emit(pattern + 1, f"{prefix}{role} {seconds // 60:02d}:{seconds % 60:02d}")
        emit(2, 0, 1, 0, 0, 1, 1, 0, 1)
        emit(names[bank], 0, 100, 0, 0, 1, 1, 1, 2, 1, 0, 0)
        emit(PATTERN_ROWS)
        for row in range(PATTERN_ROWS):
            absolute = group_index * PATTERN_ROWS + row
            group = frames[absolute * 4:absolute * 4 + 4]
            cells = [str(row)]
            message = packet(group, bank, reset=absolute == 0 and bank == 0,
                             pans=pans[absolute * 4:absolute * 4 + 4] if pans is not None else None)
            # Each parallel voice lane has its own overlapping stop guards.
            guard = [guard_names[bank * 2 + absolute % 2], "1", "1.02", "0.00"] if bank < 3 else [""] * 4
            cells += guard + ["7D", " ".join(f"{value:02X}" for value in message[1:])]
            emit(len(cells), *cells)
    emit(groups, *(row * 4 + 1 for row in range(groups)), "MIDI_LEARN", 0, "AUX_EFFECTS", 0)
    emit(*([1] * patterns), 0, 0, "TRACK_INSERTS", 0)
    for _ in range(patterns):
        emit(1, 0, 0, 0, 0)
    emit(0, -1, "SAMPLER_PADS", 0, "SONG_MATRIX", 3)
    for lane in range(1, 4):
        emit(groups, *(row * 4 + lane + 1 for row in range(groups)))
    emit("TRACK_OUTPUTS")
    for _ in range(patterns):
        emit(1, 0)
    emit("MATRIX_GRID", PATTERN_ROWS, "MASTER_BUS", 0, 0, 30, 20, 100,
         100, 0, 100, 500, 0, 100, 2500, 0, 100, 8000, 0, 100, 1,
         "MATRIX_LOOPS", 0, "AUX_OUTPUTS")
    for _ in range(patterns):
        emit(1, 0)
    emit("PAD_MARKERS", 0, "INSTRUMENT_INPUTS")
    for _ in range(patterns):
        emit(1, 0)
    emit("AUDIO_PROPERTIES")
    for _ in range(patterns):
        emit(1, 0)
    emit("AUDIO_GROUPS", 0, 0, "PROJECT_OPTIONS", 0, 1, "MIDI_KNOBS", 0,
         "COPYRIGHT_NOTICE", NOTICE)
    return {"format": "mlacker-project", "version": 1, "records": records}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sid", type=Path)
    parser.add_argument("--plugin", default="~/.local/plugins/VST3/MlaSid.vst3")
    parser.add_argument("--width", type=float, default=0.4, help="Musical voice spread, 0=mono .. 1=hard pan (default 0.4)")
    parser.add_argument("--output", type=Path,
                        default=Path(__file__).resolve().parents[1] / "last_ninja_2_loading.mlaproj/Project.mlack")
    args = parser.parse_args()
    if not 0 <= args.width <= 1:
        parser.error("--width must be between 0 and 1")
    data = args.sid.read_bytes()
    if hashlib.sha256(data).hexdigest() != SOURCE_SHA256:
        parser.error("Expected HVSC Last_Ninja_2.sid (SHA-256 mismatch)")
    frames = trace(data)
    document = project(frames, str(Path(args.plugin).expanduser()), args.width)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    lines = [json.dumps(record, ensure_ascii=False) for record in document["records"]]
    args.output.write_text('{\n  "format": "mlacker-project",\n  "version": 1,\n  "records": [\n    '
                           + ',\n    '.join(lines) + '\n  ]\n}\n')
    print(f"Wrote {args.output}: {len(frames)} register frames, {len(frames) / 4 * 15 / BPM:.3f} seconds")


if __name__ == "__main__":
    main()
