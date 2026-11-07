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
        for cc in ("cc:103", "cc:105", "cc:108", "cc:115", "cc:118"):
            assert cc in strings, f"{cc} knob automation missing"
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
