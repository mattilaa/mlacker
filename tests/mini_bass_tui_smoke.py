"""Load, play and resave the generated Mla Mini example through mlacker."""
import json
import math
from pathlib import Path
import shutil
import sys
import tempfile

from session_tui_smoke import Terminal


def check_reference(records):
    """Check the actual saved pitches/timing, presets and arrangement."""
    assert records[3] == ["integer", 121]
    at = 29
    for slot in range(3):
        count = records[at + 2][1]
        params = {records[at + 3 + i * 2][1]: records[at + 4 + i * 2][1] for i in range(count)}
        assert math.isclose(.005 * 2000 ** params[124], .28 if slot == 2 else .55, rel_tol=1e-5)
        assert math.isclose(.005 * 2000 ** params[120], .18, rel_tol=1e-5)
        assert math.isclose(4 ** params[141], 2.1, rel_tol=1e-5)
        assert math.isclose(20 * 1000 ** params[113], 1000 if slot == 2 else 480, rel_tol=1e-5)
        assert math.isclose(params[115] * 8, 2 if slot == 2 else 4.5, rel_tol=1e-5)
        assert math.isclose(params[142], .3, rel_tol=1e-5)
        assert math.isclose(params[143] * 8, 1.2, rel_tol=1e-5)
        assert math.isclose(params[144], .6 if slot == 0 else 0, rel_tol=1e-5)
        assert math.isclose(-96 + params[145] * 48, -54, rel_tol=1e-5)
        assert params[105] == (.75 if slot == 2 else .25)
        at += 3 + count * 2
    assert 1 <= records[at][1] <= 10
    at += 2
    assert records[at][1] == 10
    at += 1
    patterns, slots = {}, {}
    for _ in range(10):
        name = records[at + 1][1]
        at += 2
        tracks = records[at + 8][1]
        assert tracks == 1, "Each pattern owns exactly one mono instrument channel"
        at += 9
        columns, column = [], 1
        for _ in range(tracks):
            slots[name] = records[at + 6][1]
            lines = records[at + 7][1]
            assert lines == 1
            controls = records[at + 10][1]
            columns.append(column)
            column += lines * 4 + controls
            at += 11 + controls * 3
            assert records[at][1] == 0
            at += 1
        rows = records[at][1]
        at += 1
        score = []
        for _ in range(rows):
            cells = records[at][1]
            values = [v for _, v in records[at + 1:at + 1 + cells]]
            at += 1 + cells
            score.append([values[c:c + 4] for c in columns])
        patterns[name] = score
    for name in ("Bass - F", "Bass - ending"):
        score = patterns[name]
        assert slots[name] == 1 and len(score) == 32
        for row, tracks in enumerate(score):
            if row % 2:
                assert tracks[0][0] == ""
            else:
                high = row % 4 == 2
                root = ("C#" if row < 16 else "D#") if name.endswith("ending") else "F-"
                assert tracks[0][0] == root + ("3" if high else "2")
                assert tracks[0][2:] == ["0.69" if high else "0.56", "0.90"]
    for name in ("Upper - pickup", "Upper - 16ths A", "Upper - 16ths B", "Upper - ending"):
        assert slots[name] == 3 and len(patterns[name]) == 32
    phrase = patterns["Upper - 16ths A"]
    assert [r[0][0] for r in phrase[:5]] == ["G#4", "F-4", "A#4", "F-4", "C-5"]
    assert patterns["Upper - pickup"][31][0][0] == "F-4"
    fast = patterns["Free OSC 16ths"]
    assert slots["Free OSC 16ths"] == 1 and len(fast) == 32
    assert all(r[0][0] == "F-2" for r in fast[:16])
    assert all(r[0][0] and r[0][2:] == ["0.60", "0.00"] for r in fast)
    assert slots["Legato glide"] == 2
    assert records[at:at + 6] == [["integer", 5]] + [["integer", i] for i in (1, 1, 1, 1, 2)]
    lane = records.index(["string", "SONG_MATRIX"])
    assert records[lane + 1:lane + 8] == [["integer", 1], ["integer", 5]] + [["integer", i] for i in (3, 4, 5, 4, 6)]
    grid = records.index(["string", "MATRIX_GRID"])
    assert records[grid + 1] == ["integer", 32]


def main():
    source = Path(__file__).resolve().parents[1] / "examples/mini_bass.mlaproj"
    with tempfile.TemporaryDirectory(prefix="mlacker-mini-") as folder:
        project = Path(folder) / source.name
        shutil.copytree(source, project)
        path = project / "Project.mlack"
        doc = json.loads(path.read_text())
        check_reference(doc["records"])
        for record in doc["records"]:
            if record[0] == "string" and record[1].endswith("MlaMini.vst3"):
                record[1] = str(Path(sys.argv[2]).resolve())
        strings = [v for k, v in doc["records"] if k == "string"]
        for expected in ("Bass - F", "Bass - ending", "Upper - 16ths A", "Upper - 16ths B", "Free OSC 16ths", "Classic bass", "Ladder sweep", "Legato glide", "cc:74", "cc:71", "4.50"):
            assert expected in strings, expected
        assert sum(v.endswith("MlaMini.vst3") for v in strings) == 3
        path.write_text(json.dumps(doc))
        for repeat in range(2):
            tui = Terminal(str(project))
            try:
                frame = tui.read(1)
                assert b"Free OSC 16ths" in frame or b"Bass -" in frame, frame[-6000:]
                assert b"Cannot load" not in frame, frame[-6000:]
                frame = tui.send_until(b" ", b"PLAY", timeout=4)
                assert b"PLAY" in frame, frame[-6000:]
                frame = tui.send_until(b" ", b"STOP", timeout=4)
                assert b"STOP" in frame, frame[-6000:]
                if repeat == 0:
                    frame = tui.send(b"M", .4)
                    assert b"Bass -" in frame and b"Upper -" in frame, frame[-6000:]
                    frame = tui.send_until(b" ", b"PLAY", timeout=4)
                    assert b"PLAY" in frame, frame[-6000:]
                    frame = tui.send_until(b" ", b"STOP", timeout=4)
                    assert b"STOP" in frame, frame[-6000:]
                    tui.send(b"M", .2)
                frame = tui.send_until(b"\x13", b"Saved project:", timeout=10)
                assert b"Saved project:" in frame, frame[-6000:]
            finally:
                tui.close()
            saved = json.loads(path.read_text())
            check_reference(saved["records"])
            saved_strings = [v for k, v in saved["records"] if k == "string"]
            assert saved_strings[:len(strings)] == strings
            # The app can append empty optional sections to a generated
            # project. Their tags follow the unchanged notes and song order.
            optional = {"MIDI_LEARN", "AUX_EFFECTS", "TRACK_INSERTS", "SAMPLER_PADS",
                        "SONG_MATRIX", "TRACK_OUTPUTS", "MATRIX_GRID"}
            assert all(s in optional for s in saved_strings[len(strings):])
    print("PASS: Mla Mini bass example plays and survives save/reopen")


if __name__ == "__main__":
    main()
