"""Load and play the JP8000 lead with sequenced filter and envelope controls."""
import shutil
import sys
import tempfile
from pathlib import Path
from session_tui_smoke import Terminal


def main():
    with tempfile.TemporaryDirectory(prefix="mlacker-trance-lead-") as folder:
        project = Path(folder) / "lead.mlaproj"
        shutil.copytree(Path(sys.argv[2]).resolve(), project)
        tui = Terminal(str(project))
        try:
            frame = tui.read(1)
            for label in (b"Blue opening", b"CC74", b"CC71", b"CC19"):
                assert label in frame, frame[-5000:]
            frame = tui.send(b"M ", .8)
            assert b"PLAY" in frame, frame[-5000:]
            tui.send(b" ")
        finally:
            tui.close()
    print("PASS: JP8000 lead, cutoff, resonance and envelope boost project playback")


if __name__ == "__main__":
    main()
