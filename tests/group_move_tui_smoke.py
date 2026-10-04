"""Audio > Move to group: samples move into, between and out of groups. In a
saved project a notice says the project is saved with the move (its "Don't
ask this again" is kept in the project), and the files move on disk."""
import os
import tempfile
from pathlib import Path

from session_tui_smoke import F1, Terminal

AUDIO = F1 + b"lllll"
ADD_GROUP = AUDIO + b"j" * 8 + b"\r"
MOVE_TO_GROUP = AUDIO + b"j" * 10 + b"\r"
TOGGLE_FULLSCREEN = AUDIO + b"j" * 11 + b"\r"
VIEW_AUDIO = F1 + b"lljj\r"  # focuses the Audio list
SAVE_PROJECT = F1 + b"jjjlj\r"
FIXTURE = Path(__file__).resolve().parent / "fixtures" / "audio" / "stereo_tone.wav"


def expect(frame, *needles):
    for needle in needles:
        assert needle in frame, (needle, frame[-5000:])
    return frame


def main():
    with tempfile.TemporaryDirectory(prefix="mlacker-group-move-") as directory:
        project = Path(directory) / "Song.mlaproj"
        tui = Terminal(cwd=directory)
        try:
            tui.read(0.8)
            tui.send(AUDIO + b"\r")
            expect(tui.send(b"\x15" + os.fsencode(FIXTURE) + b"\r", 0.9), b"1 stereo_tone.wav")
            # Without groups there is nowhere to move to.
            expect(tui.send(VIEW_AUDIO + MOVE_TO_GROUP), b"No groups yet")
            expect(tui.send(SAVE_PROJECT + b"\x15" + os.fsencode(Path(directory) / "Song") + b"\r", 0.9), b"Saved project:")
            tui.send(ADD_GROUP)
            expect(tui.send(b"Drums\r"), b"Added group Drums", b"> Drums")
            tui.send(ADD_GROUP)
            expect(tui.send(b"Kicks\r"), b"Added group Drums/Kicks", b"  > Kicks")
            # The picker lists no group, then the groups as a tree.
            tui.send(VIEW_AUDIO + b"G")
            expect(tui.send(MOVE_TO_GROUP), b"Move to group", b"(No group)", b"Drums", b"  Kicks")
            # Cancel leaves everything in place.
            expect(tui.send(b"\x1b", 0.6), b"Cancelled.")
            assert (project / "Audio" / "stereo_tone.wav").is_file()
            # Into Drums/Kicks: the notice asks first; Cancel there moves nothing.
            tui.send(MOVE_TO_GROUP)
            expect(tui.send(b"jj\r"), b"so the project must be", b"[ ] Don't ask this again")
            expect(tui.send(b"\x1b", 0.6), b"Cancelled.")
            assert (project / "Audio" / "stereo_tone.wav").is_file()
            tui.send(MOVE_TO_GROUP)
            tui.send(b"jj\r")
            expect(tui.send(b"\x1b[A "), b"[x] Don't ask this again")  # Shift+Up to the check box, Space
            expect(tui.send(b"\r", 1.0), b"Moved 1 sample(s) to Drums/Kicks; Saved project:")
            assert (project / "Audio" / "Drums" / "Kicks" / "stereo_tone.wav").is_file()
            assert not (project / "Audio" / "stereo_tone.wav").exists()
            # Between groups, without the notice now.
            tui.send(VIEW_AUDIO + b"G")
            frame = tui.send(MOVE_TO_GROUP)
            expect(tui.send(b"k\r", 1.0), b"Moved 1 sample(s) to Drums; Saved project:")
            assert (project / "Audio" / "Drums" / "stereo_tone.wav").is_file()
            assert not (project / "Audio" / "Drums" / "Kicks" / "stereo_tone.wav").exists()
        finally:
            tui.close()

        # The project reopens with the sample in Drums and keeps "Don't ask".
        tui = Terminal(str(project), cwd=directory)
        try:
            expect(tui.read(1.2), b"Opened project:")
            tui.send(VIEW_AUDIO + b"G\r")  # open the Drums group
            tui.send(b"G")
            frame = tui.send(MOVE_TO_GROUP)
            expect(frame, b"(No group)")
            expect(tui.send(b"k\r", 1.0), b"Moved 1 sample(s) out of their groups; Saved project:")
            # The sample editor opens full screen by default, hiding the
            # lower pane; the Audio menu turns that off for the project.
            expect(tui.send(AUDIO), b"[x] Always open sample editor full screen")
            tui.send(b"\x1b")
            frame = expect(tui.send(VIEW_AUDIO + b"s", 0.6), b"Sample editor")
            assert b"Inspector" not in frame, frame[-5000:]
            tui.send(b"\x1b", 0.6)
            expect(tui.send(TOGGLE_FULLSCREEN), b"opens in the Pattern pane")
            expect(tui.send(VIEW_AUDIO + b"s", 0.6), b"Sample editor", b"Inspector")
            tui.send(b"\x1b", 0.6)
            expect(tui.send(b"\x13", 0.9), b"Saved project:")
            assert (project / "Audio" / "stereo_tone.wav").is_file()
            assert not (project / "Audio" / "Drums" / "stereo_tone.wav").exists()
            assert (project / "Audio" / "Drums" / "Kicks").is_dir()  # empty groups keep their folders
        finally:
            tui.close()

        # The project keeps the editor setting.
        tui = Terminal(str(project), cwd=directory)
        try:
            expect(tui.read(1.2), b"Opened project:")
            expect(tui.send(AUDIO), b"[ ] Always open sample editor full screen")
            tui.send(b"\x1b")
        finally:
            tui.close()
    print("PASS: move to group: picker, notice with Don't ask (kept in the project), files moved on save")


if __name__ == "__main__":
    main()
