"""Instrument inputs: Track > Set instrument input and saving the route.

An Instrument track's plugin (e.g. Mla Vocoder) can take an AUDIO track's
clips as its audio input. Usage: instrument_input_tui_smoke.py <mlacker>
<instrument.vst3>. No audio hardware.
"""
import os
import sys
import tempfile
from pathlib import Path

from session_tui_smoke import Terminal

F1 = b"\x1bOP"
# Track menu: Set output channel is the seventh entry, Set instrument input
# the thirteenth (the last).
SET_OUTPUT = F1 + b"lll" + b"j" * 6 + b"\r"
SET_INPUT = F1 + b"lll" + b"j" * 12 + b"\r"


def expect(frame, *needles):
    for needle in needles:
        assert needle in frame, (needle, frame[-4000:])
    return frame


def main():
    instrument = os.fsencode(os.path.abspath(sys.argv[2]))
    with tempfile.TemporaryDirectory(prefix="mlacker-inputs-") as folder:
        path = Path(folder) / "inputs.mlack"
        tui = Terminal()
        try:
            tui.read(0.8)
            expect(tui.send(F1 + b"lll" + b"j\r"), b"Audio 1 [AUDIO]")
            expect(tui.send(F1 + b"lll" + b"\r"), b"Track 2")
            expect(tui.send(F1 + b"llllll\r"), b"Add VST3 instrument")
            expect(tui.send(b"\x15" + instrument + b"\r", 1.0), b"Instrument loaded:")
            # A MIDI track has no instrument to feed yet.
            expect(tui.send(SET_INPUT, 0.8), b"Select an Instrument track playing a plugin")
            expect(tui.send(SET_OUTPUT, 0.8), b"MIDI output", b"I1")
            expect(tui.send(b"j\r", 0.8), b"MIDI output: Mlacker Test Instrument #1")
            # The picker lists "none" and every AUDIO track.
            expect(tui.send(SET_INPUT, 0.8), b"Instrument input", b"none", b"A1")
            expect(tui.send(b"j\r", 0.8), b"Mlacker Test Instrument input: A1")
            assert b"Saved:" in tui.send(b"\x13" + b"\x15" + os.fsencode(path) + b"\r", 0.8)
        finally:
            tui.close()

        saved = path.read_bytes()
        assert b"INSTRUMENT_INPUTS" in saved
        tui = Terminal(str(path))
        try:
            expect(tui.read(1.0), b"Opened:")
            # Re-saving an unchanged session rewrites the same bytes.
            tui.send(b"\x13", 0.8)
            assert path.read_bytes() == saved
            # The picker opens on the current input; k steps back to none.
            expect(tui.send(SET_INPUT, 0.8), b"Instrument input")
            expect(tui.send(b"k\r", 0.8), b"Mlacker Test Instrument input: none")
            tui.send(b"\x13", 0.8)
            assert b"INSTRUMENT_INPUTS" not in path.read_bytes()
            tui.send(b"q", 0.5)
        finally:
            tui.close()
    print("PASS: instrument input picker, routing and session round trip")


if __name__ == "__main__":
    main()
