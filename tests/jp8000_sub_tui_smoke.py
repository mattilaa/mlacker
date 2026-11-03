"""Load and play the JP8000 sub oscillator with sequenced balance and resonant 24 dB filter."""
import shutil
import sys
import tempfile
from pathlib import Path
from session_tui_smoke import Terminal


def main():
    with tempfile.TemporaryDirectory(prefix="mlacker-sub-") as folder:
        project = Path(folder) / "lead.mlaproj"
        shutil.copytree(Path(sys.argv[2]).resolve(), project)
        tui = Terminal(str(project))
        try:
            frame = tui.read(1)
            for label in (b"Saw sweep 24", b"CC74", b"CC71", b"CC22"):
                assert label in frame, frame[-5000:]
            frame = tui.send(b"M ", .8)
            assert b"PLAY" in frame, frame[-5000:]
            tui.send(b" ")
        finally:
            tui.close()
    print("PASS: JP8000 sub oscillator and resonant 24 dB filter project playback")


if __name__ == "__main__":
    main()
