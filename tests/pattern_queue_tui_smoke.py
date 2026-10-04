"""Choosing another pattern while one plays queues it instead of stopping.

Usage: python3 mlacker/tests/pattern_queue_tui_smoke.py mlacker/build/cmake/bin/mlacker
"""
import re
from session_tui_smoke import Terminal, F1


def main():
    tui = Terminal()
    try:
        tui.read(.8)
        tui.send(b"\x1b")
        # Pattern 001 shortened so it ends quickly at 400 BPM, then a second one.
        tui.send(b"\x02\x15400\r")
        tui.send(F1 + b"llll" + b"jjj\r")
        tui.send(b"\x1516\r")
        assert b"002" in tui.send(F1 + b"llll\r")
        # View > Patterns focuses the sidebar: back to 001 and play it.
        tui.send(F1 + b"ll\r")
        tui.send(b"k")
        assert b"PLAY" in tui.send(b" ")
        # Choosing 002 keeps playing and queues it.
        frame = tui.send(b"j", .1)
        assert b"Pattern 2 plays next" in frame, frame[-4000:]
        assert b"STOP" not in frame, frame[-4000:]
        frame = tui.send_until(b"", b"Playing pattern 2")
        assert b"Playing pattern 2" in frame and b"STOP" not in frame, frame[-4000:]
        # Queued again and back before it starts: the playing one plays on.
        tui.send(b"k", .05)
        frame = tui.send(b"j", .05)
        assert b"Playing pattern 2" in frame and b"STOP" not in frame, frame[-4000:]
        # A pattern made on the fly is queued, and working on it keeps playing.
        frame = tui.send(F1 + b"llll\r", .2)
        assert b"Pattern 3 plays next" in frame and b"STOP" not in frame, frame[-4000:]
        frame = tui.send(F1 + b"llll" + b"jj\r" + b"\x15Fill\r", .3)  # Rename pattern
        assert b"Fill" in frame and b"STOP" not in frame, frame[-4000:]
        frame = tui.send(F1 + b"llll" + b"jjj\r" + b"\x1532\r", .3)    # Set length
        assert b"STOP" not in frame, frame[-4000:]
        frame = tui.send(b"\t\r", .3)                                  # edit a cell
        assert b"Editing:" in frame and b"STOP" not in frame, frame[-4000:]
        frame = tui.send(b"\x1b", .3)
        assert b"STOP" not in frame, frame[-4000:]
        assert b"STOP" in tui.send(b" ")
        tui.send(F1 + b"ll\r")                   # View > Patterns
        tui.send(b"kk", .3)                      # back to 001

        # The matrix plays on while the sidebar chooses another pattern.
        assert b"Song matrix" in tui.send(b"\tM", .6)
        tui.send(b"\r", .6)                      # picker for row 1, lane 1
        tui.send(b"j", .3)                       # 001:
        tui.send(b"\r", .6)
        frame = tui.send(b"\x10", .9)
        assert b"Playing the matrix" in frame and b"PLAY" in frame, frame[-4000:]
        tui.send(b"\x1b[Z", .4)                  # sidebar, matrix still shown
        frame = tui.send(b"j", .6)
        assert b"PLAY" in frame and b"STOP" not in frame, frame[-4000:]
        # Back in the matrix, Space stops it.
        tui.send(b"\t", .4)
        assert b"Matrix stopped" in tui.send(b" ", .6)
    finally:
        tui.close()
    matrix_clone_editing()
    cursor_stays_put()
    print("PASS: a pattern change queues the next pattern; the matrix plays on, also while a clone is edited;"
          " moving the cursor during playback keeps it put until Esc")


def matrix_clone_editing():
    """Matrix playing, View > Patterns, Clone pattern and edit the clone."""
    tui = Terminal()
    try:
        tui.read(.8)
        tui.send(b"\x1b")
        tui.send(b"\x02\x15400\r")
        assert b"002" in tui.send(F1 + b"llll\r")
        # Matrix rows 1 and 2 play 001 and 002, so rows change while editing.
        tui.send(F1 + b"ll\r")                   # View > Patterns
        assert b"Song matrix" in tui.send(b"\tM", .6)
        tui.send(b"\r", .6); tui.send(b"j", .3); tui.send(b"\r", .6)     # row 1: 001
        tui.send(b"j", .3)
        tui.send(b"\r", .6); tui.send(b"jj", .3); tui.send(b"\r", .6)    # row 2: 002
        tui.send(b"k", .3)
        frame = tui.send(b"\x10", .9)
        assert b"Playing the matrix" in frame and b"PLAY" in frame, frame[-4000:]
        tui.send(F1 + b"ll\r", .4)               # View > Patterns
        frame = tui.send(F1 + b"llll" + b"j\r", .4)  # Pattern > Clone
        assert b"copy" in frame and b"STOP" not in frame, frame[-4000:]
        frame = tui.send(b"\t\r", .3)            # edit a cell of the clone
        assert b"Editing:" in frame and b"STOP" not in frame, frame[-4000:]
        # Matrix rows change (64 rows at 400 BPM is 2.4 s); the edit and the
        # clone stay in the editor and the matrix keeps playing.
        frame = tui.read(5.5)
        last = frame[frame.rfind(b"BPM 400"):]
        assert b"PLAY" in last and b"STOP" not in frame, frame[-4000:]
        assert b"Editing:" in frame[-6000:] and b"Pattern / 3" in frame[-8000:], frame[-4000:]
        frame = tui.send(b"\x1b", .4)
        assert b"STOP" not in frame and b"Pattern / 3" in frame, frame[-4000:]
    finally:
        tui.close()


# Row labels down the pattern pane's left edge, past the first screen of rows.
LATER_ROWS = re.compile(rb"\xe2\x94\x82(0[3-5]\d) ")


def cursor_stays_put():
    """j/k/h/l while the pattern plays stop the cursor following until Esc."""
    tui = Terminal()
    try:
        tui.read(.8)
        tui.send(b"\x1b")
        tui.send(b"\x02\x15400\r")           # 64 rows take 2.4 s
        assert b"002" in tui.send(F1 + b"llll\r")  # a pattern with tracks to edit
        tui.send(F1 + b"ll\r")                 # View > Patterns
        tui.send(b"\t", .3)                    # the pattern pane, cursor on row 000
        # Following, the editor scrolls down with the playing row.
        tui.send(b" ", .05)
        frame = tui.read(1.6)
        assert b"PLAY" in frame and LATER_ROWS.search(frame), frame[-4000:]
        tui.send(b" ", .4)
        tui.send(b"gg", .3)
        # Moving the cursor while playing leaves it there.
        tui.send(b" ", .05)
        frame = tui.send(b"j", .2)
        assert b"Esc follows playback again" in frame and b"STOP" not in frame, frame[-4000:]
        frame = tui.read(1.6)
        assert b"PLAY" in frame and not LATER_ROWS.search(frame), frame[-4000:]
        # An edit opened and cancelled there keeps the cursor put.
        assert b"Editing:" in tui.send(b"\r", .3)
        frame = tui.send(b"\x1b", .3) + tui.read(1.2)
        assert b"Editing:" not in frame[-3000:] and not LATER_ROWS.search(frame), frame[-4000:]
        # Esc follows playback again.
        frame = tui.send(b"\x1b", .3)
        assert b"Cursor follows playback" in frame, frame[-4000:]
        frame += tui.read(2.6)
        assert b"PLAY" in frame and LATER_ROWS.search(frame), frame[-4000:]
        assert b"STOP" in tui.send(b" ", .4)
    finally:
        tui.close()


if __name__ == "__main__":
    main()
