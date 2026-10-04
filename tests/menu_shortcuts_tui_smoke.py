"""F1-F9 open the nine menus directly; the bar shows [F1]File  [F2]Edit ..."""
import re

from session_tui_smoke import Terminal

# SS3 spellings for F1-F4, CSI n~ for F5-F9.
KEYS = [b"\x1bOP", b"\x1bOQ", b"\x1bOR", b"\x1bOS", b"\x1b[15~", b"\x1b[17~", b"\x1b[18~", b"\x1b[19~", b"\x1b[20~"]
CTRL_SHIFT_F = [b"\x1b[1;6P", b"\x1b[1;6Q", b"\x1b[13;6~"]
# Each menu's first item.
FIRST = [b"New session", b"Undo", b"Patterns", b"Create MIDI track", b"Add pattern", b"Add audio",
         b"Add instrument", b"Add effect channel", b"Play metronome"]


def main():
    tui = Terminal()
    try:
        frame = tui.read(0.8)
        assert b"[F1]File  [F2]Edit  [F3]View  [F4]Track  [F5]Pattern  [F6]Audio  [F7]Instrument  [F8]Effect  [F9]Record" in frame, frame[-3000:]
        for key, item in zip(KEYS, FIRST):
            frame = tui.send(key)
            assert item in frame, (key, frame[-3000:])
        # Items with a keymap shortcut show it right-aligned, at least three
        # cells after the menu's longest label.
        tui.send(KEYS[0])
        frame = tui.send(b"jjl")  # File > Session
        assert re.search(rb"Save session {3,}<C-s>", frame), frame[-3000:]
        tui.send(b"\x1b")
        frame = tui.send(KEYS[2])
        for label, key in ((rb"Patterns", rb"<C-S-F1>"), (rb"Audio", rb"<C-S-F2>"), (rb"Instruments", rb"<C-S-F3>"),
                           (rb"Song matrix", rb"<S-m>"), (rb"Show spectrum analyzer", rb"<C-S-m>"),
                           (rb"Show virtual keyboard", rb"<S-p>")):
            assert re.search(label + rb" {3,}" + key, frame), (label, frame[-3000:])
        tui.send(KEYS[2])
        # Ctrl+Shift+F1-F3 switch the left view instead of opening a menu.
        frame = tui.send(CTRL_SHIFT_F[1])
        assert b" Audio " in frame and b"New session" not in frame and b"Undo" not in frame, frame[-3000:]
        frame = tui.send(CTRL_SHIFT_F[2])
        assert b" Instruments " in frame and b"Patterns" not in frame.split(b" Instruments ")[-1][:40], frame[-3000:]
        frame = tui.send(CTRL_SHIFT_F[0])
        assert b" Patterns " in frame and b"New session" not in frame, frame[-3000:]
        # The open menu's key closes it; another key switches menus.
        tui.send(KEYS[8])
        frame = tui.send(KEYS[1])
        assert b"Undo" in frame, frame[-3000:]
        frame = tui.send(KEYS[3])
        assert b"Create MIDI track" in frame, frame[-3000:]
        # F4 again closes Track; Enter then does not run a menu command.
        tui.send(KEYS[3])
        assert b"Track 1" not in tui.send(b"\r")
        # Menu navigation after a function key still works.
        assert b"Track 1" in tui.send(KEYS[3] + b"\r", 0.6)
    finally:
        tui.close()
    print("PASS: F1-F9 open, switch and close menus; shortcut labels in the menu bar")


if __name__ == "__main__":
    main()
