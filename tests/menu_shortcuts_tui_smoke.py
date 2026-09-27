"""F1-F9 open the nine menus directly; the bar shows [F1]File  [F2]Edit ..."""
from session_tui_smoke import Terminal

# SS3 spellings for F1-F4, CSI n~ for F5-F9.
KEYS = [b"\x1bOP", b"\x1bOQ", b"\x1bOR", b"\x1bOS", b"\x1b[15~", b"\x1b[17~", b"\x1b[18~", b"\x1b[19~", b"\x1b[20~"]
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
