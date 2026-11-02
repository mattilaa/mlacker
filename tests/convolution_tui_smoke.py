"""Choose/confirm/cancel an IR, preserve it on errors, and reload its session."""
import os
import sys
import tempfile
import wave
from pathlib import Path
from session_tui_smoke import Terminal, F1


def main():
    bundle = os.fsencode(os.path.abspath(sys.argv[2]))
    choose = F1 + b"lllllll" + b"j" * 7 + b"\r"
    with tempfile.TemporaryDirectory(prefix="mlacker-convolution-") as folder:
        ir = Path(folder) / "room.wav"
        with wave.open(str(ir), "wb") as wav:
            wav.setnchannels(1)
            wav.setsampwidth(2)
            wav.setframerate(44100)
            wav.writeframes(b"\xff\x7f" + b"\0\0" * 4409)
        session = Path(folder) / "reverb.mlack"
        tui = Terminal()
        try:
            tui.read(.8)
            tui.send(F1 + b"lllllll\r")
            assert b"Load VST3 effect" in tui.send(b"\r")
            assert b"Effect loaded" in tui.send(b"\x15" + bundle + b"\r", .8)
            assert b"Choose convolution impulse response WAV" in tui.send(choose)
            frame = tui.send(b"\x15" + os.fsencode(ir) + b"\r")
            assert b"Load convolution impulse response" in frame and b"room.wav" in frame, frame[-5000:]
            tui.send(b"\x1b")  # Cancel confirmation.
            assert b"Choose convolution impulse response WAV" in tui.send(choose)
            tui.send(b"\x15" + os.fsencode(ir) + b"\r")
            frame = tui.send(b"\r", .7)
            assert b"Convolution impulse response loaded" in frame, frame[-5000:]
            # Cancelling the browser must clear the file-selection mode.
            tui.send(choose)
            tui.send(b"\x1b")
            frame = tui.send(b"\x13")
            assert b"Save" in frame, frame.decode("utf-8", "replace")[-2500:]
            frame = tui.send(b"\x15" + os.fsencode(session) + b"\r", .6)
            assert b"Saved:" in frame, frame.decode("utf-8", "replace")[-2500:]
            saved = session.read_bytes()
            assert b"CONVOLUTION_WAV" in saved and os.fsencode(ir) in saved
            bad = Path(folder) / "broken.wav"
            bad.write_bytes(b"broken")
            tui.send(choose)
            tui.send(b"\x15" + os.fsencode(bad) + b"\r")
            frame = tui.send(b"\r", .6)
            assert b"Cannot read WAV" in frame, frame[-5000:]
            tui.send(b"\x13", .6)
            assert session.read_bytes() == saved, "Failed replacement changed the saved response"
        finally:
            tui.close()
        tui = Terminal(str(session))
        try:
            frame = tui.read(1)
            assert b"FX1" in frame, frame[-5000:]
            tui.send(b"\x13", .6)
            assert session.read_bytes() == saved, "Convolution session failed round trip"
            # Changing the controller must reload the external IR before playback.
            assert b"Audio output (AUHAL)" in tui.send_until(F1 + b"jjjj\r", b"Audio output (AUHAL)")
            tui.send(b"\rk\r")
            tui.send(b"\t\t\rjjj\r")  # 96 kHz
            tui.send(b"\t\rjj\r")       # 512 frames
            tui.send(b"\t\t\rk\r")
            frame = tui.send(b"\t\t\t\r", .7)
            assert b"Settings applied. Audio disabled." in frame, frame.decode("utf-8", "replace")[-2500:]
            tui.send(b"\x13", .6)
            assert session.read_bytes() == saved, "Device replacement lost the IR"
            # Project text also carries the response path.
            project = Path(folder) / "Convolution.mlaproj"
            frame = tui.send(F1 + b"jjjlj\r")
            assert b"Save project (.mlaproj)" in frame
            frame = tui.send(b"\x15" + os.fsencode(project) + b"\r", .7)
            project_text = (project / "Project.mlack").read_bytes()
            assert b"CONVOLUTION_WAV" in project_text and os.fsencode(ir) in project_text
        finally:
            tui.close()
        tui = Terminal(str(project))
        try:
            frame = tui.read(1)
            assert b"FX1" in frame, frame.decode("utf-8", "replace")[-2500:]
            tui.send(b"\x13", .6)
            assert (project / "Project.mlack").read_bytes() == project_text
        finally:
            tui.close()
    print("PASS: WAV selection, confirmation, cancellation, failed replacement, session/project round trips, device replacement")


if __name__ == "__main__":
    main()
