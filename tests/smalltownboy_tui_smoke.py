"""Validate and play the separate Smalltown Boy bass project through mlacker."""
import json
import math
from pathlib import Path
import shutil
import sys
import tempfile

from session_tui_smoke import Terminal


def check_score(records):
    assert records[:4] == [["string", "MLACK"], ["integer", 1], ["integer", 5], ["integer", 126]]
    assert records[28][1] == 1  # One monophonic Mini for the entire bass line.
    count = records[31][1]
    params = {records[32 + i * 2][1]: records[33 + i * 2][1] for i in range(count)}
    # The saved example can carry an edited preset. Validate its complete
    # parameter layout here, then compare every value after save/reopen.
    assert set(params) == set(range(100, 146))
    assert all(0 <= value <= 1 for value in params.values())
    at = 32 + count * 2 + 2
    assert records[at][1] == 4
    at += 1
    onsets = []
    for bar, (name, root) in enumerate(zip(("C", "Bb", "F", "Eb"), ("C-2", "A#1", "F-2", "D#2"))):
        assert records[at + 1][1] == f"Smalltown Boy - {name}"
        at += 2
        assert records[at + 8][1] == 1
        at += 9
        assert records[at + 6][1] == records[at + 7][1] == 1  # Slot and note-line count.
        controls = records[at + 10][1]
        at += 11 + controls * 3
        assert records[at][1] == 0
        at += 1
        assert records[at][1] == 16
        at += 1
        for row in range(16):
            cells = records[at][1]
            values = [v for _, v in records[at + 1:at + 1 + cells]]
            at += 1 + cells
            if row % 2:
                assert values[1] == ""
            else:
                upper = root[:-1] + str(int(root[-1]) + 1)
                expected = root if row % 4 == 0 else upper
                assert values[1:4] == [expected, "105", "1.10"]
                delay = float(values[4])
                assert 0 <= delay < 1
                onsets.append((bar * 16 + row + delay) * 15 / 126)
    assert len(onsets) == 32 and abs(onsets[0] - .03) < .001
    for start, end in zip(onsets, onsets[1:]):
        assert abs(end - start - 30 / 126.4686) < .001
        assert end - start > 1.10 * 15 / 126  # No overlapping held keys.
    assert records[at:at + 5] == [["integer", 4]] + [["integer", n] for n in (1, 2, 3, 4)]
    grid = records.index(["string", "MATRIX_GRID"])
    assert records[grid + 1] == ["integer", 16]
    return params


def main():
    source = Path(__file__).resolve().parents[1] / "examples/smalltownboy.mlaproj"
    with tempfile.TemporaryDirectory(prefix="mlacker-smalltownboy-") as folder:
        project = Path(folder) / source.name
        shutil.copytree(source, project)
        path = project / "Project.mlack"
        doc = json.loads(path.read_text())
        original_params = check_score(doc["records"])
        for record in doc["records"]:
            if record[0] == "string" and record[1].endswith("MlaMini.vst3"):
                record[1] = str(Path(sys.argv[2]).resolve())
        strings = [v for k, v in doc["records"] if k == "string"]
        assert sum(v.endswith("MlaMini.vst3") for v in strings) == 1
        assert not any("/Desktop/" in v or v.endswith(".m4a") for v in strings)
        path.write_text(json.dumps(doc))
        for repeat in range(2):
            tui = Terminal(str(project))
            try:
                frame = tui.read(1)
                assert b"Smalltown Boy" in frame and b"Cannot load" not in frame, frame[-6000:]
                if repeat == 0:
                    tui.send(b"M", .3)
                frame = tui.send_until(b" ", b"PLAY", timeout=4)
                assert b"PLAY" in frame, frame[-6000:]
                if repeat == 0:
                    frame = tui.read(2.1)
                    assert b"PLAY" in frame, frame[-6000:]
                frame = tui.send_until(b" ", b"STOP", timeout=4)
                assert b"STOP" in frame, frame[-6000:]
                if repeat == 0:
                    tui.send(b"M", .2)
                frame = tui.send_until(b"\x13", b"Saved project:", timeout=10)
                assert b"Saved project:" in frame, frame[-6000:]
            finally:
                tui.close()
            saved = json.loads(path.read_text())
            saved_params = check_score(saved["records"])
            changed = {key: (value, saved_params[key]) for key, value in original_params.items()
                       if not math.isclose(saved_params[key], value, rel_tol=0, abs_tol=1e-12)}
            assert not changed, f"Plugin settings changed on save/reopen: {changed}"
            saved_strings = [v for k, v in saved["records"] if k == "string"]
            assert saved_strings[:len(strings)] == strings
    print("PASS: Smalltown Boy notes, preset and mono song survive playback and save/reopen")


if __name__ == "__main__":
    main()
