"""Open and play all three reference-guided arrangements in the real TUI."""
import shutil
import sys
import tempfile
from pathlib import Path
from session_tui_smoke import Terminal


def main():
    root = Path(sys.argv[2]).resolve()
    with tempfile.TemporaryDirectory(prefix="mlacker-obsession-") as folder:
        for name, first in [("horizon", b"Mist 24"), ("pulse", b"Pulse 24"),
                            ("glass", b"Glass 24")]:
            project = Path(folder) / (name + ".mlaproj")
            shutil.copytree(root / ("jp8000_obsession_" + name + ".mlaproj"), project)
            tui = Terminal(str(project))
            try:
                frame = tui.read(1)
                assert first in frame, frame[-5000:]
                for cc in (b"CC20", b"CC21", b"CC22", b"CC23"):
                    assert cc in frame, frame[-5000:]
                frame = tui.send(b"M ", .8)
                assert b"PLAY" in frame, frame[-5000:]
                tui.send(b" ")
            finally:
                tui.close()
    print("PASS: all three Obsession arrangements load and play")


if __name__ == "__main__":
    main()
