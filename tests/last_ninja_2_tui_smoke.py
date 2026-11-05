"""Verify the reference SID program, responsive playback, Stop, and saved credits.

Usage: last_ninja_2_tui_smoke.py <mlacker> <MlaSid.vst3>
"""
import hashlib
import json
from pathlib import Path
import shutil
import sys
import tempfile
import time

from session_tui_smoke import Terminal, F1


def inspect_score(document):
    records = document["records"]
    assert records[3] == ["integer", 188]
    assert records[27:29] == [["integer", 0], ["integer", 1]]
    assert records[31] == ["integer", 93]
    params = {records[32 + i * 2][1]: records[33 + i * 2][1] for i in range(93)}
    assert params[101] == params[102] == params[119] == 0.0
    at = 32 + 93 * 2
    assert records[at:at + 3] == [["integer", 3], ["integer", 357], ["integer", 356]]
    at += 3
    decoded = [[0] * 25 for _ in range(17088)]
    resets = [0] * 17088
    pans = [[50] * 3 for _ in decoded]
    guards = ["C--1", "C#-1", "D--1", "D#-1", "E--1", "F--1"]
    for pattern in range(356):
        group, bank = divmod(pattern, 4)
        assert records[at][1] == pattern + 1
        assert records[at + 1][1].startswith(f"V{bank + 1} " if bank < 3 else "Chip mix ")
        at += 2
        assert records[at + 8] == ["integer", 1]
        at += 9
        assert [r[1] for r in records[at + 6:at + 12]] == [1, 1, 2, 1, 0, 0]
        at += 12
        assert records[at] == ["integer", 48]
        at += 1
        for row in range(48):
            assert records[at] == ["integer", 7]
            at += 1
            cells = [r[1] for r in records[at:at + 7]]
            at += 7
            absolute = group * 48 + row
            assert cells[1:5] == ([guards[2 * bank + absolute % 2], "1", "1.02", "0.00"] if bank < 3 else [""] * 4)
            assert cells[5] == "7D"
            message = [0x7D] + [int(value, 16) for value in cells[6].split()]
            n = 4 if bank == 3 else 7
            stride = n + 2 + int(bank < 3)
            assert message[:8] == [0x7D, 0x4D, 0x53, 0x49, 0x44, 1, bank, 4]
            assert len(message) == 12 + 4 * stride
            assert message[8] | message[9] << 7 | message[10] << 14 == 19947
            assert message[11] == int(absolute == 0 and bank == 0) | (2 if bank < 3 else 0)
            for f in range(4):
                q = message[12 + f * stride:12 + (f + 1) * stride]
                index = absolute * 4 + f
                assert q[0] in (0, 1) and q[1] < 1 << n
                resets[index] |= q[0] << bank
                offset = 2
                if bank < 3:
                    assert q[2] in (30, 50, 70)
                    pans[index][bank] = q[2]
                    offset += 1
                for register in range(n):
                    decoded[index][bank * 7 + register] = q[offset + register] | ((q[1] >> register & 1) << 7)
    assert records[at:at + 90] == [["integer", 89]] + [["integer", row * 4 + 1] for row in range(89)]
    matrix = records.index(["string", "SONG_MATRIX"]) + 1
    assert records[matrix] == ["integer", 3]
    matrix += 1
    for lane in range(1, 4):
        assert records[matrix:matrix + 90] == [["integer", 89]] + [["integer", row * 4 + lane + 1] for row in range(89)]
        matrix += 90
    assert pans[193][:2] == [30, 70]
    assert pans[1][2] == 30
    for frame, pan in zip(decoded, pans):
        for voice in range(3):
            if frame[voice * 7 + 4] & 0x80:
                assert pan[voice] == 50
    canonical = b"".join(bytes(frame) + bytes([reset]) for frame, reset in zip(decoded, resets))
    assert decoded[0][24] == 15  # No artificial video lead-in.
    assert decoded[1][18] & 1  # Intro gate within the first row.
    assert len(decoded) == 17088
    assert decoded[1][14:16] == [0x6B, 0x2F]  # Reference's first F5 octave band.
    assert decoded[193][0:2] == [0xED, 0x05]  # First in-game bass entry at 3.85 s.
    assert decoded[193][7:9] == [0xF6, 0x02]
    assert decoded[16911][24] == 1 and decoded[16912][24] == 0
    assert any(frame[11] == 0x15 or frame[18] == 0x15 for frame in decoded)  # Coupled ring modulation.
    return hashlib.sha256(canonical).hexdigest()


def main():
    source = Path(__file__).resolve().parents[1] / "examples/last_ninja_2_loading.mlaproj"
    with tempfile.TemporaryDirectory(prefix="mlacker-last-ninja-reference-") as folder:
        project = Path(folder) / source.name
        shutil.copytree(source, project)
        path = project / "Project.mlack"
        document = json.loads(path.read_text())
        fingerprint = inspect_score(document)
        assert fingerprint == "cc924dc044c213ea41a210d967faaf6294f1f26b0913beedcca950d63890d409"
        print("Reference program SHA-256:", fingerprint, flush=True)
        for record in document["records"]:
            if record[0] == "string" and record[1].endswith("MlaSid.vst3"):
                record[1] = str(Path(sys.argv[2]).resolve())
        path.write_text(json.dumps(document))
        original_strings = [value for kind, value in document["records"] if kind == "string"]
        for repeat in range(2):
            tui = Terminal(str(project))
            try:
                frame = tui.read(2.0)
                for credit in (b"Copyright notice", b"Central Park (in-game)", b"Matt Gray", b"1988 System 3", b"CoGFV_xxR64"):
                    assert credit in frame, frame[-6000:]
                tui.send(b"\x1b", 0.3)
                if repeat == 0:
                    tui.send(b"M", 0.3)
                    tui.send(b"j" * 20, 0.3)
                started = time.monotonic()
                frame = tui.send_until(b" ", b"PLAY", timeout=4.0)
                assert b"PLAY" in frame and time.monotonic() - started < 2.0
                if repeat == 0:
                    started = time.monotonic()
                    frame = tui.send(F1, 0.15)
                    assert b"New session" in frame and time.monotonic() - started < 1.0
                    tui.send(b"\x1b", 0.2)
                    tui.read(4.0)
                    frame = tui.send(b"M", 0.3)
                    assert b"PLAY" in frame and b"01:20" in frame, frame[-6000:]
                    tui.send(b"M", 0.2)
                started = time.monotonic()
                frame = tui.send_until(b" ", b"STOP", timeout=4.0)
                assert b"STOP" in frame and time.monotonic() - started < 2.0
                if repeat == 0:
                    tui.send(b"M", 0.2)
                frame = tui.send_until(b"\x13", b"Saved project:", timeout=20.0)
                assert b"Saved project:" in frame, frame[-6000:]
            finally:
                tui.close()
            saved = json.loads(path.read_text())
            assert [value for kind, value in saved["records"] if kind == "string"] == original_strings
    print("PASS: reference program, responsive pattern transitions, stop and copyright save/reopen")


if __name__ == "__main__":
    main()
