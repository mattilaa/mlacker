"""Audio takes: an armed AUDIO track records the audio input (Settings).

Without audio hardware the take cannot start; the status line says why.
Usage: audio_record_tui_smoke.py <mlacker>.
"""
from session_tui_smoke import Terminal, F1


def expect(frame, *needles):
    for needle in needles:
        assert needle in frame, (needle, frame[-4000:])
    return frame


def main():
    tui = Terminal()
    try:
        tui.read(0.8)
        expect(tui.send(F1 + b"lll" + b"j\r"), b"Audio 1 [AUDIO]")
        # Mixer: focus it, arm the selected AUDIO track with Shift+R, then
        # hide it again so the Inspector shows the status message.
        tui.send(b"m\t\tR")
        tui.send(b"\tm")
        frame = tui.send(b" ", 0.6)
        expect(frame, b"is armed: choose an audio input in File > Settings to record it", b"PLAY")
        assert b"REC" not in frame
        tui.send(b" ")
        tui.send(b"q", 0.5)
    finally:
        tui.close()
    print("PASS: an armed AUDIO track explains the missing audio input")


if __name__ == "__main__":
    main()
