"""Project settings: File > Project > Project settings sets the project's MIDI
knob mode (as in Settings, absolute or relative), which the session keeps.

Usage: project_settings_tui_smoke.py <mlacker>. No audio hardware.
"""
import os
import tempfile
from pathlib import Path

from session_tui_smoke import Terminal, F1

PROJECT_SETTINGS = F1 + b"jjjljjj\r"  # File > Project > Project settings


def expect(frame, *needles):
    for needle in needles:
        assert needle in frame, (needle, frame[-4000:])
    return frame


def main():
    with tempfile.TemporaryDirectory(prefix="mlacker-project-settings-") as folder:
        path = Path(folder) / "knobs.mlack"
        tui = Terminal()
        try:
            tui.read(0.8)
            expect(tui.send(PROJECT_SETTINGS, 0.6), b"Project settings", b"MIDI knobs", b"As in Settings")
            # Enter opens the choices; two down is Relative; Tab to OK.
            expect(tui.send(b"\r", 0.4), b"Relative (endless encoders")
            tui.send(b"jj\r", 0.4)
            expect(tui.send(b"\t\r", 0.8), b"Project MIDI knobs: relative")
            assert b"Saved:" in tui.send(b"\x13" + b"\x15" + os.fsencode(path) + b"\r", 0.8)
        finally:
            tui.close()

        assert b"MIDI_KNOBS" in path.read_bytes()
        tui = Terminal(str(path))
        try:
            expect(tui.read(1.0), b"Opened:")
            expect(tui.send(PROJECT_SETTINGS, 0.6), b"Relative (endless encoders")
            # Escape cancels and keeps the mode.
            tui.send(b"\x1b", 0.4)
            tui.send(b"q", 0.5)
        finally:
            tui.close()
    print("PASS: project settings dialog, knob mode and session round trip")


if __name__ == "__main__":
    main()
