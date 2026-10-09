"""System exclusive columns: Track > Add / remove SysEx columns, typing an ID
and a message, refusing a status byte and the session round trip.

Usage: sysex_columns_tui_smoke.py <mlacker>. No audio hardware.
"""
import os
import tempfile
from pathlib import Path

from session_tui_smoke import Terminal, F1

# Track menu: Add / remove SysEx columns is the fifteenth entry.
TOGGLE_SYSEX = F1 + b"lll" + b"j" * 14 + b"\r"


def expect(frame, *needles):
    for needle in needles:
        assert needle in frame, (needle, frame[-4000:])
    return frame


def main():
    with tempfile.TemporaryDirectory(prefix="mlacker-sysex-") as folder:
        path = Path(folder) / "sysex.mlack"
        tui = Terminal()
        try:
            tui.read(0.8)
            expect(tui.send(TOGGLE_SYSEX, 0.6), b"Select a MIDI or Instrument track first")
            expect(tui.send(F1 + b"lll" + b"\r"), b"Track 1")
            expect(tui.send(TOGGLE_SYSEX, 0.6), b"SysEx columns added", b"ID", b"SYSEX")
            tui.send(b"\t")
            # The cursor is on ID; hex is stored upper case.
            expect(tui.send(b"\r\x1543 1a\r", 0.6), b"43 1A")
            expect(tui.send(b"l\r\x154c 00 00 7e 00\r", 0.6), b"4C 00 00 7E 00")
            # F0 and F7 are added on playback, never typed.
            expect(tui.send(b"j\r\x15F0 43\r", 0.6), b"SYSEX: up to 80 hex bytes")
            tui.send(b"\x1b", 0.3)
            assert b"Saved:" in tui.send(b"\x13" + b"\x15" + os.fsencode(path) + b"\r", 0.8)
        finally:
            tui.close()

        tui = Terminal(str(path))
        try:
            expect(tui.read(1.0), b"Opened:", b"SYSEX", b"43 1A", b"4C 00 00 7E 00")
            # Removing the columns drops their messages.
            expect(tui.send(TOGGLE_SYSEX, 0.6), b"SysEx columns removed")
            tui.send(b"\x13", 0.8)
            assert b"4C 00 00 7E 00" not in path.read_bytes()
            tui.send(b"q", 0.5)
        finally:
            tui.close()
    print("PASS: SysEx columns, typing, validation and session round trip")


if __name__ == "__main__":
    main()
