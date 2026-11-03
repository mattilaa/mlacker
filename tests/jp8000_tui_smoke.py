"""Load the real JP8000 bundle, its complete pad bank, and retained session state."""
import os
import shutil
import sys
import tempfile
from pathlib import Path
from session_tui_smoke import F1, Terminal

LOAD_PRESET = F1 + b"lllllljjjlj\r"
EDIT = F1 + b"llllllj\r"


def main():
    bundle = os.fsencode(os.path.abspath(sys.argv[2]))
    bank = Path(sys.argv[3]).resolve()
    with tempfile.TemporaryDirectory(prefix="mlacker-jp8000-") as folder:
        session = Path(folder) / "pad.mlack"
        tui = Terminal()
        try:
            tui.read(.8)
            assert b"Instrument track created" in tui.send(F1 + b"llljj\r")
            tui.send(F1 + b"llllll\r")
            frame = tui.send(b"\x15" + bundle + b"\r", .8)
            assert b"Instrument loaded:" in frame and b"Mla JP8000" in frame, frame[-5000:]
            for preset in sorted(bank.glob("*.mlapre")):
                assert b"Load plugin preset (.mlapre)" in tui.send(LOAD_PRESET)
                frame = tui.send(b"\x15" + os.fsencode(preset) + b"\r", .5)
                assert b"Loaded plugin preset:" in frame, frame[-5000:]
            frame = tui.send(EDIT)
            assert b"VST3 editor: Mla JP8000" in frame and b"Performance Mode" in frame, frame[-5000:]
            tui.send(b"\x1b")
            tui.send(b"\x13")
            frame = tui.send(b"\x15" + os.fsencode(session) + b"\r", .6)
            assert b"Saved:" in frame, frame[-5000:]
            saved = session.read_bytes()
            assert b"MlaJP8000.vst3" in saved
        finally:
            tui.close()
        tui = Terminal(str(session))
        try:
            frame = tui.read(1)
            assert b"Mla JP8000" in frame, frame[-5000:]
            frame = tui.send(EDIT)
            assert b"VST3 editor: Mla JP8000" in frame, frame[-5000:]
            tui.send(b"\x1b")
            tui.send(b"\x13", .6)
            assert session.read_bytes() == saved, "Performance parameters changed on session round trip"
        finally:
            tui.close()
        if len(sys.argv) > 4:
            project = Path(folder) / "Pad auditions.mlaproj"
            shutil.copytree(sys.argv[4], project)
            tui = Terminal(str(project))
            try:
                frame = tui.read(1)
                assert b"Obsession Atmosphere" in frame and b"Midnight Split" in frame, frame[-5000:]
                frame = tui.send(b"M ", .8)
                assert b"PLAY" in frame, frame[-5000:]
                tui.send(b" ")
            finally:
                tui.close()
    print("PASS: JP8000 bundle load, factory bank, editor, session round trip, audition project playback")


if __name__ == "__main__":
    main()
