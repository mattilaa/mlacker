"""Load, play and resave the native Last Ninja 2 project with its credits.

Usage: last_ninja_2_tui_smoke.py <mlacker> <MlaSid.vst3>
"""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import time

from session_tui_smoke import Terminal, F1


def main():
    source = Path(__file__).resolve().parents[1] / "examples/last_ninja_2_loading.mlaproj"
    with tempfile.TemporaryDirectory(prefix="mlacker-last-ninja-") as folder:
        project = Path(folder) / source.name
        shutil.copytree(source, project)
        path = project / "Project.mlack"
        document = json.loads(path.read_text())
        # The main bass must be pulse (PW Sweep has no effect on a saw), and
        # all four bass layers need the requested broad duty-cycle movement.
        records = document["records"]
        at = 29
        pulse_rates = set()
        for _ in range(records[28][1]):
            slot, _, count = [record[1] for record in records[at:at + 3]]
            at += 3
            preset = {records[at + i * 2][1]: records[at + i * 2 + 1][1] for i in range(count)}
            at += count * 2
            if 1 <= slot <= 7:
                assert preset[200] == 4 / 15 and preset[111] >= 0.65
                pulse_rates.add(preset[112])
            if slot in (1, 3, 4, 5):
                assert preset[111] >= 0.94
            if slot == 9:
                assert preset[103] == 1 / 3 and preset[104] == 0.0
                assert preset[205] == 0.0, "Snare needs the short SID release"
            if slot == 11:
                assert preset[103] == 2 / 3 and preset[117] == 0.0
                assert preset[200] == 1 / 15 and preset[230] == preset[250] == 0.0
        assert len(pulse_rates) == 7
        assert ["string", "SID intro arp"] in records
        assert ["string", "64 32 . ."] in records, "Snare must change from noise to pulse"
        for record in document["records"]:
            if record[0] == "string" and record[1].endswith("MlaSid.vst3"):
                record[1] = str(Path(sys.argv[2]).resolve())
        path.write_text(json.dumps(document))
        original_strings = [value for kind, value in document["records"] if kind == "string"]
        for repeat in range(2):
            tui = Terminal(str(project))
            try:
                frame = tui.read(1.5)
                for credit in (b"Copyright notice", b"Last Ninja 2", b"Matt Gray", b"1988 System 3", b"Chris Holm"):
                    assert credit in frame, frame[-6000:]
                frame = tui.send(b"\x1b", 0.5)
                assert b"Central Park" in frame, frame[-6000:]
                if repeat == 0:
                    # Source bar 33 starts both busy sixteenth-note basses and
                    # percussion. Exercise this section, not just the intro.
                    tui.send(b"M", 0.3)
                    tui.send(b"j" * 16, 0.3)
                started = time.monotonic()
                frame = tui.send_until(b" ", b"PLAY", timeout=4.0)
                assert b"PLAY" in frame, frame[-6000:]
                assert time.monotonic() - started < 2.0, "Playback start stalled the UI"
                if repeat == 0:
                    started = time.monotonic()
                    frame = tui.send(F1, 0.15)
                    assert b"New session" in frame, frame[-6000:]
                    assert time.monotonic() - started < 1.0, "Menu stalled during dense playback"
                    tui.send(b"\x1b", 0.2)
                    tui.read(4.0)  # Cross a complete two-bar pattern boundary.
                    frame = tui.send(b"M", 0.3)
                    assert b"PLAY" in frame and b"Central Park 035-036" in frame, frame[-6000:]
                    # Space in the editor switches matrix playback to the
                    # selected pattern; stop the matrix from its own view.
                    tui.send(b"M", 0.2)
                started = time.monotonic()
                frame = tui.send_until(b" ", b"STOP", timeout=4.0)
                assert b"STOP" in frame, frame[-6000:]
                assert time.monotonic() - started < 2.0, "Stop stalled during playback"
                if repeat == 0:
                    tui.send(b"M", 0.2)
                frame = tui.send_until(b"\x13", b"Saved project:", timeout=20.0)
                assert b"Saved project:" in frame, frame[-6000:]
            finally:
                tui.close()
            saved = json.loads(path.read_text())
            strings = [value for kind, value in saved["records"] if kind == "string"]
            assert strings == original_strings, "Save changed the score or copyright notice"
    print("PASS: dense Central Park playback stays responsive across patterns; score and credits survive save/reopen")


if __name__ == "__main__":
    main()
