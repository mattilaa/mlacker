"""Pattern visual selection and modal paste errors in a hardware-free PTY."""
from session_tui_smoke import Terminal

# The menu bar opens with F1; Tab cycles panes.
F1 = b"\x1bOP"


class VisualTerminal(Terminal):
    @staticmethod
    def latest(frame):
        return frame.rsplit(b"[F1]File  [F2]Edit ", 1)[-1]

    def read(self, seconds=0.35):
        # Button presses include animation frames before the final repaint.
        return self.latest(super().read(seconds))

    def send_until(self, keys, needle, timeout=10.0):
        return self.latest(super().send_until(keys, needle, timeout))


def main():
    tui = VisualTerminal()
    try:
        tui.read(0.8)
        tui.send(F1 + b"lll\r")  # Track > Create track > MIDI
        frame = tui.send(b"\tK")  # Pattern pane, create a note
        assert b"C-4" in frame and b"100" in frame, frame[-5000:]
        tui.send(b"vly")
        frame = tui.send(b"lp")  # NOTE clipboard cannot start on VEL
        assert b"Cannot paste selection" in frame and b"NOTE to NOTE" in frame, frame[-5000:]
        assert b"OK" in frame and b"Cancel" not in frame, frame[-5000:]
        frame = tui.send(b"jklh")
        assert b"Cannot paste selection" in frame, frame[-5000:]
        # OK closes the modal after its press animation; keys sent before the
        # pattern repaints without it would still go to the modal. A partial
        # repaint lacks the modal text too, so wait for the pattern rows.
        frame = tui.send_until(b"\r", b"Inspector")
        assert b"Cannot paste selection" not in frame and b"ROW NOTE" in frame, frame[-5000:]
        # Under a loaded CI runner the three key events can span more than the
        # default fixed read window. Wait for the pasted row, not an earlier
        # cursor-movement repaint.
        frame = tui.send_until(b"hjp", b"002 C-4")  # matching NOTE in the following row
        assert b"Cannot paste selection" not in frame and frame.count(b"C-4") >= 2, frame[-5000:]
        frame = tui.send(b"vlkd")  # clear both rows, not delete pattern rows
        assert b"C-4" not in frame and b"003" in frame, frame[-5000:]
        frame = tui.send(b"p")
        assert frame.count(b"C-4") >= 2, frame[-5000:]
        frame = tui.send(b"vljK")
        assert frame.count(b"C#4") >= 2 and b"101" in frame, frame[-5000:]
        frame = tui.send(b"\x1b[106;2u")
        assert frame.count(b"C-4") >= 2 and b"100" in frame, frame[-5000:]
        tui.send(b"\x1b")
        tui.send(b"v")
        frame = tui.send(b"\x1b")
        assert b"New session" not in frame, "Visual Escape opened a menu"
        tui.send(b"v")
        assert b"New session" in tui.send(F1)
        assert b"New session" not in tui.send(b"\x1b"), "Visual selection stole Escape from the menu"
        tui.send(b"\x1b")
        frame = tui.send(b"gghoK")
        assert frame.count(b"C-4") == 3 and b"C#4" not in frame, frame[-5000:]
        frame = tui.send(b"OK")
        assert frame.count(b"C-4") == 4 and b"C#4" not in frame, frame[-5000:]
        frame = tui.send(b"ggVjjjd")
        assert b"C-4" not in frame and b"004" in frame, frame[-5000:]
        frame = tui.send(b"p")
        assert frame.count(b"C-4") == 4, frame[-5000:]
        # Whole-pattern and reverse boundary selections retain the anchor.
        tui.send(b"ggvGy")
        frame = tui.send(b"Gvgg\x7f")
        assert b"C-4" not in frame, frame[-5000:]
        frame = tui.send(b"p")
        assert frame.count(b"C-4") == 4, frame[-5000:]
        tui.send(b"jvG\x08")
        frame = tui.send(b"gg")
        assert frame.count(b"C-4") == 1, frame[-5000:]
    finally:
        tui.close()
    print("PASS: visual copy/cut/paste/transpose, matching-column OK modal, new-note velocity, Escape")


if __name__ == "__main__":
    main()
