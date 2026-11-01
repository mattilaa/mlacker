"""Help popup: focused guides, fuzzy prompt, empty results and modal input."""
import fcntl
import re
import struct
import termios

from session_tui_smoke import Terminal

F10 = b"\x1b[21~"
ESC = b"\x1b"


def main():
    tui = Terminal()
    try:
        assert b"[F10]Help" in tui.read(0.8)
        frame = tui.send(F10)
        assert b"Focused pane" in frame and b"Browse / search help" in frame
        frame = tui.send(b"\r")
        assert b"Patterns sidebar" in frame and b"j/k or arrows scroll" in frame
        # q closes either a guide or the topic list, without quitting the app.
        frame = tui.send(b"q")
        assert b"j/k or arrows scroll" not in frame
        assert tui.process.poll() is None
        tui.send(F10 + b"j\r")
        frame = tui.send(b"\n")
        assert b"Preview: Patterns sidebar" in frame
        frame = tui.send(b"\x1b[106;5u")
        assert b"Preview: Audio library" in frame
        frame = tui.send(b"\x0b")
        assert b"Preview: Patterns sidebar" in frame
        frame = tui.send(b"\r")
        assert b"Pattern menu" in frame and b"Preview:" not in frame
        frame = tui.send(b"\x0f")
        assert b"Preview: Patterns sidebar" in frame
        frame = tui.send(b"\x1b[105;5u")
        assert b"Pattern menu" in frame
        frame = tui.send(b"q")
        assert b"Up/Down choose" not in frame
        tui.send(F10 + b"\r")
        # q remains text inside the fuzzy search prompt.
        frame = tui.send(b"/q")
        assert b"Up/Down choose" in frame
        tui.send(b"\x15")
        # Search letters are a subsequence, not a substring; case is ignored.
        frame = tui.send(b"VrtLky")
        assert b"Virtual keyboard" in frame and b"Up/Down choose" in frame
        frame = tui.send(b"\r")
        assert b"computer-keyboard piano" in frame
        # Guide keys cannot edit the underlying pattern or quit the app.
        tui.send(b"jjkk")
        assert tui.process.poll() is None
        tui.send(ESC)
        frame = tui.send(b"/zzzzzzzzzzzzzzzzzzzzzzzz")
        assert b"No matching help" in frame
        assert b"No matching help" in tui.send(b"\r")
        # Ctrl+U clears the prompt. A shortcut search finds guide text.
        frame = tui.send(b"\x15Ctrl+Z")
        assert b"Pattern editor" in frame
        frame = tui.send(b"\r")
        assert b"Move and edit" in frame, frame[-5000:]
        assert re.search(rb"Ctrl\+Z {3,}Cycle vertical detail", frame), frame[-5000:]
        # A small viewport wraps and scrolls the guide.
        fcntl.ioctl(tui.slave, termios.TIOCSWINSZ, struct.pack("HHHH", 16, 65, 0, 0))
        tui.send(b"j" * 40)
        frame = tui.send(b"g")
        assert b"h/j/k/l or arrows" in frame
        tui.send(F10)
        # Reopen to exercise owned search and help text lifetimes.
        tui.send(F10 + b"j\r")
        frame = tui.send(b"/smplr\r")
        assert b"Mla Sampler" in frame
        tui.send(b"q")
        # F10 must reach the Help menu from the virtual keyboard too.
        tui.send(b"P")
        frame = tui.send(F10)
        assert b"Focused pane" in frame
        tui.send(ESC)
        tui.send(b"P")
        # A chooser keeps its state while its help is open.
        tui.send(b"\x1bOPjjl\r")
        frame = tui.send(F10)
        assert b"File browser" in frame and b"Navigate" in frame
        frame = tui.send(b"jjjj")
        assert b"Ctrl+U" in frame
        frame = tui.send(F10)
        assert b"Open session" in frame
        tui.send(ESC)
    finally:
        if tui.process.poll() is None:
            tui.send(F10)
        tui.close()
    print("PASS: Help popup, fuzzy search, modal input, resize and keyboard focus")


if __name__ == "__main__":
    main()
