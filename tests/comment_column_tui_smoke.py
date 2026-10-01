"""Comment (TEXT) columns: Track > Add / remove comment column, typing a
comment, column stages and the session round trip.

A row's comment is what a speech instrument (Mla Speech) says on that row.
Usage: comment_column_tui_smoke.py <mlacker>. No audio hardware.
"""
import os
import tempfile
from pathlib import Path

from session_tui_smoke import Terminal, F1

# Track menu: Add / remove comment column is the fourteenth entry (the last).
TOGGLE_COMMENT = F1 + b"lll" + b"j" * 13 + b"\r"


def expect(frame, *needles):
    for needle in needles:
        assert needle in frame, (needle, frame[-4000:])
    return frame


def main():
    with tempfile.TemporaryDirectory(prefix="mlacker-comments-") as folder:
        path = Path(folder) / "comments.mlack"
        tui = Terminal()
        try:
            tui.read(0.8)
            expect(tui.send(TOGGLE_COMMENT, 0.6), b"Select a MIDI or Instrument track first")
            expect(tui.send(F1 + b"lll" + b"\r"), b"Track 1")
            expect(tui.send(TOGGLE_COMMENT, 0.6), b"Comment column added", b"TEXT")
            tui.send(b"\t")
            # Enter edits the cell; the text may use any letters, spaces and
            # capitals (Shift+S is not the sampler while typing).
            expect(tui.send(b"\r\x15Hello Atari ST, 1987!\r", 0.6), b"Hello Atari ST, 1987!")
            # The column widens to its text and hides with the CC columns:
            # z cycles NOTE/VEL, then LEN/OFF, then everything again.
            tui.send(b"z")
            tui.send(b"z")
            expect(tui.send(b"z", 0.5), b"TEXT", b"Hello Atari ST, 1987!")
            # Collapsing moved the cursor to NOTE: VEL, LEN, OFF, CC1, TEXT.
            expect(tui.send(b"jlllll\r\x15Second row\r", 0.6), b"Second row")
            assert b"Saved:" in tui.send(b"\x13" + b"\x15" + os.fsencode(path) + b"\r", 0.8)
        finally:
            tui.close()

        assert b"Hello Atari ST, 1987!" in path.read_bytes()
        tui = Terminal(str(path))
        try:
            expect(tui.read(1.0), b"Opened:", b"TEXT", b"Hello Atari ST, 1987!", b"Second row")
            # Removing the column drops its texts.
            expect(tui.send(TOGGLE_COMMENT, 0.6), b"Comment column removed")
            tui.send(b"\x13", 0.8)
            assert b"Hello Atari" not in path.read_bytes()
            tui.send(b"q", 0.5)
        finally:
            tui.close()
    print("PASS: comment column, typing, column stages and session round trip")


if __name__ == "__main__":
    main()
