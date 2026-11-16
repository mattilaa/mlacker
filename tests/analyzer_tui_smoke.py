"""Large analyzer: FFT controls, pane focus, song transport and view switches."""
from session_tui_smoke import Terminal, F1

OPEN = b"\x1b[97;6u"  # Ctrl+Shift+A; O and Ctrl+O retain row/cell insertion.
PATTERNS = b"\x1b[1;6P"


def expect(frame, *labels):
    for label in labels:
        assert label in frame, (label, frame[-6000:])
    return frame


def main():
    tui = Terminal()
    try:
        tui.read(0.8)
        expect(tui.send(F1 + b"lll\r"), b"Track 1")
        tui.send(b"\tK")
        expect(tui.send(F1 + b"llllj\r"), b"Pattern / 2")
        expect(tui.send(b"M"), b"Song matrix")
        frame = expect(tui.send(OPEN), b"Spectrum analyzer / Oscilloscope", b"FFT on", b"Scope on", b"Music")
        assert b"ROW  L1" not in frame, frame[-6000:]
        expect(tui.send(b"f"), b"Flat")
        expect(tui.send(b"m"), b"Music")
        expect(tui.send(b"x"), b"Dot glyphs")
        expect(tui.send(b"d"), b"Wide bars")
        expect(tui.send(b"D"), b"shade off")
        expect(tui.send(b"v+"), b"flash on 72")
        expect(tui.send(b"-"), b"flash on 64")
        expect(tui.send(b"p"), b"palette 1")
        expect(tui.send(b"bsc"), b"FFT off", b"Scope off")
        expect(tui.send(b"sc"), b"FFT on", b"Scope on")
        expect(tui.send(b"?"), b"FFT / Oscilloscope controls", b"h / l")
        tui.send(b"?")
        expect(tui.send(b"e"), b"EQ1", b"EQ4", b"Master")
        expect(tui.send(b"\x1b[A\x1b[A"), b"+0.2")
        tui.send(b"e")
        # Tab leaves the view in place; returning restores its keyboard owner.
        expect(tui.send(b"\t"), b"Spectrum analyzer / Oscilloscope")
        expect(tui.send(b"\x1b[Z"), b"Spectrum analyzer / Oscilloscope")
        expect(tui.send_until(b" ", b"PLAY"), b"PLAY")
        expect(tui.send(b"l", 0.3), b"Time 00:05", b"PLAY")
        expect(tui.send(b"h", 0.3), b"Time 00:00", b"PLAY")
        expect(tui.send_until(b" ", b"STOP"), b"STOP")
        frame = expect(tui.send(b"M"), b"Song matrix", b"L1")
        assert b"Spectrum analyzer / Oscilloscope" not in frame, frame[-6000:]
        expect(tui.send(OPEN), b"Spectrum analyzer / Oscilloscope")
        frame = expect(tui.send(b"q"), b"Song matrix")
        assert tui.process.poll() is None, "q closed mlacker instead of the view"
        tui.send(OPEN)
        frame = expect(tui.send(PATTERNS), b"Pattern /", b"Patterns")
        assert b"Spectrum analyzer / Oscilloscope" not in frame, frame[-6000:]
        tui.send(OPEN)
        tui.send(b"\x1a")  # Ctrl+Z belongs to the hidden editor, not this view.
        expect(tui.send(PATTERNS), b"Pattern /", b"1/16")
    finally:
        tui.send(PATTERNS, 0.1)
        tui.close()


if __name__ == "__main__":
    main()
