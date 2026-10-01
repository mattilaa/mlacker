"""Record > Bounce selection to sample, from the pattern and the song matrix.

Usage: bounce_tui_smoke.py <mlacker>. With audio hardware the bounce renders
and adds "Bounce N.wav" to the Audio list; without it the status line says
that audio is needed. Either way the command and the matrix mark respond.
"""
from session_tui_smoke import Terminal, F1

BOUNCE = F1 + b"l" * 8 + b"jjj\r"


def expect_any(frame, *needles):
    assert any(needle in frame for needle in needles), (needles, frame[-4000:])
    return frame


def main():
    tui = Terminal()
    try:
        tui.read(0.8)
        tui.send(b"\x1b[108;6u")  # focus the pattern editor
        # No visual selection: the whole pattern.
        frame = tui.send(BOUNCE, 3.0)
        expect_any(frame, b"Bounced rows 1-", b"Bouncing needs audio enabled")
        if b"Bounced rows 1-" in frame:
            assert b"Bounce 1.wav" in frame, frame[-4000:]
        # The matrix: v marks a row range, the bounce takes those rows.
        tui.send(b"M", 0.6)
        frame = tui.send(b"v", 0.5)
        assert b"Marking from matrix row 1" in frame, frame[-4000:]
        frame = tui.send(BOUNCE, 3.0)
        expect_any(frame, b"Bounced matrix row 1", b"Bouncing needs audio enabled")
        frame = tui.send(b"v", 0.5)
        assert b"Mark cleared" in frame, frame[-4000:]
        tui.send(b"q", 0.5)
    finally:
        tui.close()
    print("PASS: bounce from the pattern and from marked matrix rows")


if __name__ == "__main__":
    main()
