"""File > Save/Open project: a .mlaproj folder with Audio/ and Presets/ that
opens from any location, including as the startup argument."""
import os
import shutil
import sys
import tempfile
from pathlib import Path

from session_tui_smoke import F1, Terminal

SAVE_PROJECT = F1 + b"jjjlj\r"  # File > Project > Save project
OPEN_PROJECT = F1 + b"jjjl\r"  # File > Project > Open project
NEW_PROJECT = F1 + b"j\r"  # File > New project
EDIT_INSTRUMENT = F1 + b"llllll" + b"j\r"
EDIT_EFFECT = F1 + b"lllllll" + b"jj\r"
FIXTURE = Path(__file__).resolve().parent / "fixtures" / "audio" / "stereo_tone.wav"


def main():
    instrument = os.fsencode(os.path.abspath(sys.argv[2]))
    effect = os.fsencode(os.path.abspath(sys.argv[3]))
    with tempfile.TemporaryDirectory(prefix="mlacker-project-") as directory:
        tui = Terminal(cwd=directory)
        try:
            tui.read(0.8)
            assert b"Instrument track created" in tui.send(F1 + b"lll" + b"jj\r")
            tui.send(F1 + b"llllll\r")
            assert b"Instrument loaded:" in tui.send(b"\x15" + instrument + b"\r", 0.9)
            assert b"0.25" in tui.send(EDIT_INSTRUMENT + b"\r\x150.25\r")
            tui.send(b"\x1b")
            assert b"FX1" in tui.send(F1 + b"lllllll\r")
            assert b"Load VST3 effect" in tui.send(b"\r")
            assert b"Effect loaded" in tui.send(b"\x15" + effect + b"\r", 0.9)
            frame = tui.send(b"\r")
            assert b"VST3 editor:" in frame, frame[-5000:]
            assert b"0.75" in tui.send(b"\r\x150.75\r")
            tui.send(b"\x1b")
            tui.send(F1 + b"lllll\r")
            frame = tui.send(b"\x15" + os.fsencode(FIXTURE) + b"\r", 0.9)
            assert b"stereo_tone" in frame, frame[-5000:]
            assert b"Save project (.mlaproj)" in tui.send(SAVE_PROJECT)
            frame = tui.send(b"\x15" + os.fsencode(Path(directory) / "Song") + b"\r", 0.9)
            assert b"Saved project:" in frame, frame[-5000:]
            # Ctrl+S saves the open project in place.
            assert b"Saved project:" in tui.send(b"\x13", 0.9)
        finally:
            tui.close()

        project = Path(directory) / "Song.mlaproj"
        session = (project / "Project.mlack").read_bytes()
        assert (project / "Audio" / "stereo_tone.wav").is_file()
        assert len(list((project / "Presets").glob("Instrument 01 - *.mlapre"))) == 1
        assert len(list((project / "Presets").glob("Effect 1 - *.mlafxpre"))) == 1
        assert not (Path(directory) / "Song.mlaproj.saving").exists()
        assert not (Path(directory) / "Song.mlaproj.previous").exists()
        # Content is referenced relative to the folder, never by absolute path.
        assert os.fsencode(directory) not in session and b"fixtures" not in session
        assert b"Audio/stereo_tone.wav" in session and b"Presets/Instrument 01 - " in session

        moved = Path(directory) / "elsewhere" / "Moved.mlaproj"
        moved.parent.mkdir()
        shutil.move(project, moved)
        tui = Terminal(str(moved), cwd=directory)
        try:
            frame = tui.read(1.2)
            assert b"Opened project:" in frame and b"stereo_tone" in frame, frame[-5000:]
            frame = tui.send(EDIT_INSTRUMENT)
            assert b"VST3 editor: Mlacker Test Instrument" in frame and b"0.25" in frame, frame[-5000:]
            tui.send(b"\x1b")
            assert b"0.75" in tui.send(EDIT_EFFECT)
            tui.send(b"\x1b")
            assert b"Saved project:" in tui.send(b"\x13", 0.9)
            # Open project from the menu; a missing sample is named and the
            # current document stays open.
            (moved / "Audio" / "stereo_tone.wav").unlink()
            assert b"Open project (.mlaproj)" in tui.send(OPEN_PROJECT)
            frame = tui.send(b"\x15" + os.fsencode(moved) + b"\r", 0.9)
            assert b"Missing or unreadable audio file: Audio/stereo_tone.wav" in frame, frame[-5000:]
            # Let the error dialog's button animation finish before sending a
            # function key; otherwise a loaded CI runner can deliver it while
            # the modal still owns input.
            tui.send(b"\r", 1.0)  # dismiss the error
            # File > New project: confirm, then the new empty session is
            # saved as a project straight away.
            assert b"Start a new project?" in tui.send_until(NEW_PROJECT, b"Start a new project?")
            frame = tui.send(b"\r", 0.9)
            assert b"Save project (.mlaproj)" in frame and b"Untitled.mlaproj" in frame, frame[-5000:]
            frame = tui.send(b"\x15" + os.fsencode(Path(directory) / "Fresh") + b"\r", 0.9)
            assert b"Saved project:" in frame, frame[-5000:]
        finally:
            tui.close()
        assert (Path(directory) / "Fresh.mlaproj" / "Project.mlack").is_file()
    print("PASS: project save with Audio/ and Presets/, in-place resave, relocated open, missing-file error, new project")


if __name__ == "__main__":
    main()
