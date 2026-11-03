"""Open and play all three Super Saw detune studies in the real TUI."""
import shutil
import sys
import tempfile
from pathlib import Path
from session_tui_smoke import Terminal


def main():
    root = Path(sys.argv[2]).resolve()
    with tempfile.TemporaryDirectory(prefix="mlacker-detune-") as folder:
        for name, first in [("detune_steps", b"Dry zero"), ("detune_sweep", b"Slow widening"),
                            ("detune_layers", b"Tight and wide")]:
            project = Path(folder) / (name + ".mlaproj")
            shutil.copytree(root / ("jp8000_" + name + ".mlaproj"), project)
            tui = Terminal(str(project))
            try:
                frame = tui.read(1)
                assert first in frame, frame[-5000:]
                for cc in (b"CC16", b"CC17", b"CC24", b"CC25"):
                    assert cc in frame, frame[-5000:]
                frame = tui.send(b"M ", .8)
                assert b"PLAY" in frame, frame[-5000:]
                tui.send(b" ")
            finally:
                tui.close()
    print("PASS: all three Super Saw detune studies load and play")


if __name__ == "__main__":
    main()
