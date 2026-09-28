"""Mla Sampler pane (Shift+S) in Pattern view: toggling, key capture, loading and
clearing slots, loop mode / loop point / output edits, sharing the pane with the
virtual keyboard, and the loop settings surviving a session round trip.

Usage: sampler_view_tui_smoke.py <mlacker> <MlaSampler.vst3>. No audio hardware.
"""
import struct
import sys
import tempfile
import wave
from pathlib import Path

from session_tui_smoke import Terminal

# The menu bar opens with F1.
F1 = b"\x1bOP"
BACKSPACE = b"\x7f"
SAVE_SESSION = F1 + b"jjjjj\r"


def write_wav(path, value):
    with wave.open(str(path), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(48000)
        out.writeframes(struct.pack("<h", value) * 4800)


def expect(frame, *needles):
    for needle in needles:
        assert needle in frame, (needle, frame[-4000:])
    return frame


def main():
    with tempfile.TemporaryDirectory(prefix="mlacker-sampler-") as directory:
        root = Path(directory)
        pad = root / "pad.wav"
        write_wav(pad, 12000)
        path = root / "sampler.mlack"
        tui = Terminal(cwd=directory)
        try:
            tui.read(0.8)
            # Without an Mla Sampler the pane says what to do.
            expect(tui.send(b"S"), b" Sampler ", b"Select an Instrument track playing Mla Sampler")
            expect(tui.send(b"S"), b"Sampler closed")

            expect(tui.send(F1 + b"lll" + b"jj\r"), b"Instrument track created")
            expect(tui.send(F1 + b"llllll\r"), b"Add VST3 instrument")
            expect(tui.send(b"\x15" + bytes(Path(sys.argv[2]).resolve()) + b"\r", 0.9), b"Instrument loaded: Mla Sampler")

            # Shift+S opens the pane on the selected track's instance: 16 empty
            # slots from the root key C-2, loop off over the whole sample, Main.
            frame = expect(tui.send(b"S"), b"Sampler | Mla Sampler #1", b"C-2", b"(empty)", b"Off", b"Start%", b"0.0 ", b"100.0", b"Main")
            assert frame.count(b"(empty)") >= 4, frame[-4000:]

            # The focused pane keeps its keys: "m" does not toggle the mixer,
            # and "q" does not quit.
            frame = tui.send(b"m")
            assert b" Mixer " not in frame, frame[-4000:]
            tui.send(b"q")
            assert tui.process.poll() is None

            # Enter loads a file into the selected slot (slot 2).
            tui.send(b"j")
            expect(tui.send_until(b"\r", b"Load sample for pad 2"), b"Load sample for pad 2")
            expect(tui.send(b"\x15" + bytes(pad) + b"\r", 0.7), b"Pad 2: pad.wav (added to Audio)", b"pad.wav")

            # Shift+J/K adjust the selected field: loop mode, start, end, output.
            expect(tui.send(b"K"), b"Fwd")
            expect(tui.send(b"K"), b"Bidir")
            frame = tui.send(b"K")  # already the last mode
            assert b"Bidir" in frame, frame[-4000:]
            tui.send(b"l")
            expect(tui.send(b"KKK"), b"3.0 ")
            tui.send(b"l")
            expect(tui.send(b"J"), b"99.0 ")
            tui.send(b"l")
            expect(tui.send(b"K"), b"Out 2")

            # Backspace clears the slot; its loop settings stay.
            expect(tui.send(BACKSPACE, 0.5), b"Pad 2 cleared")
            tui.send(b"k")
            expect(tui.send_until(b"\r", b"Load sample for pad 1"), b"Load sample for pad 1")
            expect(tui.send(b"\x15" + bytes(pad) + b"\r", 0.7), b"Pad 1: pad.wav")

            # The virtual keyboard takes the pane over, and Shift+S takes it back.
            frame = expect(tui.send(b"P"), b"Keyboard")
            assert b"Sampler |" not in frame, frame[-4000:]
            frame = expect(tui.send(b"S"), b"Sampler | Mla Sampler #1")
            assert b" Keyboard " not in frame, frame[-4000:]
            expect(tui.send(b"S"), b"Sampler closed")

            expect(tui.send(SAVE_SESSION), b"Save session (.mlack)")
            expect(tui.send(b"\x15" + bytes(path) + b"\r", 0.7), b"Saved:")
        finally:
            tui.close()

        # Loop settings are plugin parameters: the session restores them.
        tui = Terminal(str(path), cwd=directory)
        try:
            expect(tui.read(1.2), b"Opened:")
            expect(tui.send(b"S"), b"Sampler | Mla Sampler #1", b"pad.wav", b"Bidir", b"3.0 ", b"99.0 ", b"Out 2")
            expect(tui.send(b"S"), b"Sampler closed")  # "q" quits again
        finally:
            tui.close()


if __name__ == "__main__":
    main()
