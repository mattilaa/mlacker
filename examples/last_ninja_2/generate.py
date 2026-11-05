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
LEAD_IN_FRAMES = 40
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
    frames = [([0] * 25, []) for _ in range(LEAD_IN_FRAMES)]
    for _ in range(PLAYER_FRAMES):
        memory.writes = []
        call(play)
        frames.append((memory[0xD400:0xD419], memory.writes))
    quantum = PATTERN_ROWS * FRAMES_PER_ROW
    while len(frames) % quantum:
        frames.append((frames[-1][0].copy(), []))
    return frames


def packet(frames, bank, reset=False):
    micros = FRAME_MICROS
    result = MAGIC + [bank, len(frames), micros & 127, (micros >> 7) & 127,
                      (micros >> 14) & 127, int(reset)]
    base = bank * 7
    count = 4 if bank == 3 else 7
    for registers, writes in frames:
        values = registers[base:base + count]
        gate_reset = bank < 3 and bool(values[4] & 1) and any(
            r == base + 4 and not value & 1 for r, value in writes
        )
        mask = sum((value >> 7) << index for index, value in enumerate(values))
        result += [int(gate_reset), mask] + [value & 127 for value in values]
    assert len(result) <= 48 and all(0 <= value < 128 for value in result)
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


def project(frames, plugin):
    records = []

    def emit(*values):
        for value in values:
            records.append(["string" if isinstance(value, str) else "real" if isinstance(value, float) else "integer", value])

    patterns = len(frames) // (PATTERN_ROWS * FRAMES_PER_ROW)
    params = parameters()
    emit("MLACK", 1, 5, BPM, 4, 4, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
         0, 1, 60, 0, 1, 0, 0, 0, 0, 0, 0)
    emit(0, 1, 1, plugin, len(params))
    for key, value in sorted(params.items()):
        emit(key, float(value))
    emit(1, patterns + 1, patterns)
    names = ["SID 1 Bass / drums", "SID 2 Lead / modulation",
             "SID 3 Arp / percussion", "SID Volume / filter"]
    for pattern in range(patterns):
        seconds = int(pattern * PATTERN_ROWS * 15 / BPM)
        emit(pattern + 1, f"Central Park {seconds // 60:02d}:{seconds % 60:02d}")
        emit(5, 0, 1, 0, 0, 1, 1, 0, 4)
        for name in names:
            emit(name, 0, 100, 0, 0, 1, 1, 1, 2, 1, 0, 0)
        emit(PATTERN_ROWS)
        for row in range(PATTERN_ROWS):
            absolute = pattern * PATTERN_ROWS + row
            group = frames[absolute * 4:absolute * 4 + 4]
            cells = [str(row)]
            for bank in range(4):
                message = packet(group, bank, reset=absolute == 0 and bank == 0)
                # Alternating overlapping stop guards survive pattern changes;
                # releasing the last one on Stop clears queued register writes.
                guard = ["C--1" if absolute % 2 == 0 else "C#-1", "1", "1.02", "0.00"] if bank == 0 else [""] * 4
                cells += guard + ["7D", " ".join(f"{value:02X}" for value in message[1:])]
            emit(len(cells), *cells)
    emit(patterns, *range(1, patterns + 1), "MIDI_LEARN", 0, "AUX_EFFECTS", 0)
    emit(*([4] * patterns), 0, 0, "TRACK_INSERTS", 0)
    for _ in range(patterns):
        emit(4, *([0] * 16))
    emit(0, -1, "SAMPLER_PADS", 0, "SONG_MATRIX", 0, "TRACK_OUTPUTS")
    for _ in range(patterns):
        emit(4, 0, 0, 0, 0)
    emit("MATRIX_GRID", PATTERN_ROWS, "MASTER_BUS", 0, 0, 30, 20, 100,
         100, 0, 100, 500, 0, 100, 2500, 0, 100, 8000, 0, 100, 1,
         "MATRIX_LOOPS", 0, "AUX_OUTPUTS")
    for _ in range(patterns):
        emit(4, 0, 0, 0, 0)
    emit("PAD_MARKERS", 0, "INSTRUMENT_INPUTS")
    for _ in range(patterns):
        emit(4, 0, 0, 0, 0)
    emit("AUDIO_PROPERTIES")
    for _ in range(patterns):
        emit(4, 0, 0, 0, 0)
    emit("AUDIO_GROUPS", 0, 0, "PROJECT_OPTIONS", 0, 1, "MIDI_KNOBS", 0,
         "COPYRIGHT_NOTICE", NOTICE)
    return {"format": "mlacker-project", "version": 1, "records": records}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sid", type=Path)
    parser.add_argument("--plugin", default="~/.local/plugins/VST3/MlaSid.vst3")
    parser.add_argument("--output", type=Path,
                        default=Path(__file__).resolve().parents[1] / "last_ninja_2_loading.mlaproj/Project.mlack")
    args = parser.parse_args()
    data = args.sid.read_bytes()
    if hashlib.sha256(data).hexdigest() != SOURCE_SHA256:
        parser.error("Expected HVSC Last_Ninja_2.sid (SHA-256 mismatch)")
    frames = trace(data)
    document = project(frames, str(Path(args.plugin).expanduser()))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    lines = [json.dumps(record, ensure_ascii=False) for record in document["records"]]
    args.output.write_text('{\n  "format": "mlacker-project",\n  "version": 1,\n  "records": [\n    '
                           + ',\n    '.join(lines) + '\n  ]\n}\n')
    print(f"Wrote {args.output}: {len(frames)} register frames, {len(frames) / 4 * 15 / BPM:.3f} seconds")


if __name__ == "__main__":
    main()
