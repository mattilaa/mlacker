"""Load, play and resave the Mla 09 Detroit example project.

Usage: mla09_detroit_tui_smoke.py <mlacker> <Mla09.vst3>
"""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import time

from session_tui_smoke import Terminal

# Every Mla 09 sound: BD RS SD CP CH LT OH MT HT CR RD.
SOUNDS = {"C-2", "C#2", "D-2", "D#2", "F#2", "G-2", "A#2", "B-2", "D-3", "C#3", "D#3"}


# Optional session extensions: the generator writes them all, empty or not;
# mlacker writes only the ones it needs. Neither changes the score.
EXTENSIONS = {"MIDI_LEARN", "AUX_EFFECTS", "TRACK_INSERTS", "SAMPLER_PADS", "SONG_MATRIX", "TRACK_OUTPUTS",
              "MATRIX_GRID", "MASTER_BUS", "MATRIX_LOOPS", "AUX_OUTPUTS", "PAD_MARKERS", "INSTRUMENT_INPUTS",
              "AUDIO_PROPERTIES", "AUDIO_GROUPS", "PROJECT_OPTIONS", "MIDI_KNOBS"}


def score_strings(records):
    """Every string in a session (names, cells, parameters) but the extension tags."""
    return [value for kind, value in records if kind == "string" and value not in EXTENSIONS]


def check_hats(records):
    """Every hi-hat note is on the Hats track's single note line, one per row."""
    v = [record[1] for record in records]
    at = 29
    for _ in range(v[28]):  # plugins: slot, path, parameter count, pairs
        at += 3 + v[at + 2] * 2
    at += 2
    patterns = v[at]
    at += 1
    for _ in range(patterns):
        name = v[at + 1]
        at += 2
        tracks = v[at + 8]
        at += 9
        columns, column = [], 1
        for _ in range(tracks):
            track, lines, controls = v[at], v[at + 7], v[at + 10]
            at += 11 + controls * 3 + 1
            columns.append((track, column, lines))
            column += lines * 4 + controls
        rows = v[at]
        at += 1
        hats = 0
        for _ in range(rows):
            cells = v[at + 1:at + 1 + v[at]]
            at += 1 + v[at]
            here = [(track, line) for track, start, lines in columns for line in range(lines)
                    if cells[start + line * 4] in ("F#2", "A#2")]
            assert all(h == ("Hats", 0) for h in here), f"{name}: a hat outside the Hats track's line"
            assert len(here) <= 1, f"{name}: two hats on one row"
            hats += len(here)
        assert hats > 0, f"{name}: no hats"


def main():
    source = Path(__file__).resolve().parents[1] / "examples/mla09_detroit.mlaproj"
    with tempfile.TemporaryDirectory(prefix="mlacker-mla09-") as folder:
        project = Path(folder) / source.name
        shutil.copytree(source, project)
        path = project / "Project.mlack"
        document = json.loads(path.read_text())
        records = document["records"]
        strings = [value for kind, value in records if kind == "string"]
        assert SOUNDS <= set(strings), "The example must use every Mla 09 sound"
        # CC columns for every bass drum, snare and tom knob, the clap and hat
        # levels, the hats' decays and the cymbals' tunes.
        for cc in ("cc:20", "cc:103", "cc:104", "cc:105", "cc:21", "cc:106", "cc:107", "cc:108", "cc:26",
                   "cc:109", "cc:110", "cc:111", "cc:112", "cc:113", "cc:114", "cc:27", "cc:28", "cc:115",
                   "cc:116", "cc:117", "cc:118"):
            assert cc in strings, f"{cc} knob automation missing"
        check_hats(records)
        for record in records:
            if record[0] == "string" and record[1].endswith("Mla09.vst3"):
                record[1] = str(Path(sys.argv[2]).resolve())
        path.write_text(json.dumps(document))
        original = score_strings(document["records"])
        tui = Terminal(str(project))
        try:
            frame = tui.read(1.5)
            assert b"Ignition" in frame, frame[-6000:]
            started = time.monotonic()
            frame = tui.send_until(b" ", b"PLAY", timeout=4.0)
            assert b"PLAY" in frame, frame[-6000:]
            assert time.monotonic() - started < 2.0, "Playback start stalled the UI"
            tui.read(2.0)
            frame = tui.send_until(b" ", b"STOP", timeout=4.0)
            assert b"STOP" in frame, frame[-6000:]
            frame = tui.send_until(b"\x13", b"Saved project:", timeout=20.0)
            assert b"Saved project:" in frame, frame[-6000:]
        finally:
            tui.close()
        saved = json.loads(path.read_text())
        assert score_strings(saved["records"]) == original, "Save changed the score"
    print("PASS: the Mla 09 Detroit project loads, plays and resaves unchanged")


if __name__ == "__main__":
    main()
