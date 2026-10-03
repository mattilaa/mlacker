"""Dialogs answer with OK/Cancel-style buttons instead of key hints: the track
rename prompt and the sample editor, driven through a PTY."""
import os
import tempfile
from pathlib import Path

from session_tui_smoke import F1, Terminal

RENAME_TRACK = F1 + b"lll" + b"jjj\r"
ADD_AUDIO = F1 + b"lllll\r"
EDIT_SAMPLE = F1 + b"lllll" + b"j\r"
RIGHT = b"\x1b[C"
FIXTURE = Path(__file__).resolve().parent / "fixtures" / "audio" / "stereo_tone.wav"
HINTS = (b"Enter: save", b"Esc: cancel", b"Esc discard", b"Esc cancel")


def main():
    with tempfile.TemporaryDirectory(prefix="mlacker-buttons-") as directory:
        tui = Terminal(cwd=directory)
        try:
            tui.read(0.8)
            assert b"Track 1" in tui.send(F1 + b"lll\r", 0.6)
            frame = tui.send(RENAME_TRACK)
            assert b"Rename track" in frame and b"OK" in frame and b"Cancel" in frame, frame[-5000:]
            assert not any(hint in frame for hint in HINTS), frame[-5000:]
            # Tab moves to OK; Enter presses it.
            frame = tui.send(b"\x15Lead\t\r", 0.6)
            assert b"Lead [MIDI]" in frame, frame[-5000:]
            # Right selects Cancel: the name stays.
            tui.send(RENAME_TRACK)
            frame = tui.send(b"\x15Bass\t" + RIGHT + b"\r", 0.6)
            assert b"Bass [MIDI]" not in frame, frame[-5000:]
            frame = tui.send(RENAME_TRACK)
            assert b"Lead" in frame, frame[-5000:]
            tui.send(b"\x1b", 0.6)
            tui.send(ADD_AUDIO)
            assert b"stereo_tone" in tui.send(b"\x15" + os.fsencode(FIXTURE) + b"\r", 0.9)
            frame = tui.send(EDIT_SAMPLE, 0.6)
            assert b"Sample editor" in frame and b" stereo_tone.wav | " in frame, frame[-5000:]
            assert not any(hint in frame for hint in HINTS), frame[-5000:]
            # Ctrl+S asks for the new clip's name in a Save/Cancel dialog.
            assert b"Trimmed to the selection" in tui.send(b"vLt")
            frame = tui.send(b"\x13", 0.6)
            assert b"Save clip" in frame and b"Save" in frame and b"Cancel" in frame, frame[-5000:]
            assert b"stereo_tone - Trim 1.wav" in frame, frame[-5000:]
            assert not any(hint in frame for hint in HINTS), frame[-5000:]
            # Tab Tab reaches the buttons; Right selects Cancel.
            frame = tui.send(b"\t\t" + RIGHT + b"\r", 0.6)
            assert b"Sample 2" not in frame and b"[modified]" in frame, frame[-5000:]
            tui.send(b"\x13", 0.6)
            frame = tui.send(b"\t\t\r", 0.8)
            assert b"Saved stereo_tone - Trim 1.wav as sample 2; sample 1 is unchanged" in frame, frame[-5000:]
        finally:
            tui.close()
    print("PASS: rename prompt OK/Cancel buttons, sample editor save dialog buttons, no key hints")


if __name__ == "__main__":
    main()
