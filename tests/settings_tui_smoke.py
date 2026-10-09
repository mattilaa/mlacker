"""Hardware-free PTY coverage for File > Settings and public dropdowns."""
import fcntl
import os
import tempfile
import re
import select
import struct
import subprocess
import sys
import termios
import time

from tui_sync import finish_frame, wait_consumed

# The menu bar opens with F1; Tab cycles panes.
F1 = b"\x1bOP"


def main():
    master, slave = os.openpty()
    before = termios.tcgetattr(slave)
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 36, 80, 0, 0))
    env = dict(os.environ, TERM="xterm-256color", MLANG_TUI_NO_HARDWARE="1")
    # Never the user's own settings file (~/.config/mlacker/settings).
    settings_file = os.path.join(tempfile.mkdtemp(prefix="mlacker-settings-"), "settings")
    env["MLACKER_SETTINGS"] = settings_file
    env.pop("NO_COLOR", None)
    process = subprocess.Popen([sys.argv[1]], stdin=slave, stdout=slave, stderr=slave, env=env)

    # Output read while waiting for the app to take keys; read_for starts with it.
    drained = bytearray()

    def read_for(seconds=0.25):
        data = bytearray(drained)
        drained.clear()
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            ready, _, _ = select.select([master], [], [], min(0.05, max(0, deadline - time.monotonic())))
            if ready:
                data.extend(os.read(master, 65536))
        # A window ending inside a repaint would hide that repaint's last rows.
        finish_frame(master, data)
        return bytes(data)

    def send(keys):
        os.write(master, keys)
        wait_consumed(master, slave, drained)  # the read window starts once the app has the keys
        return read_for()

    def send_until(keys, needle, timeout=10.0):
        # The first Settings open enumerates CoreAudio/CoreMIDI devices and can
        # take longer than a fixed read window.
        os.write(master, keys)
        wait_consumed(master, slave, drained)  # the read window starts once the app has the keys
        data = bytearray(drained)
        drained.clear()
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline and needle not in data:
            data.extend(read_for(0.05))
        return bytes(data) + read_for(0.2)

    try:
        assert b"Settings" in read_for(1)
        # File: New session, New project, Session >, Project >, Settings.
        frame = send_until(b"jjjj\r", b"Audio output (AUHAL)")
        assert b"Audio output (AUHAL)" in frame and b"Audio input (AUHAL)" in frame
        frame = send(b"\r")
        assert b"Disabled" in frame and b"System default" in frame, re.sub(rb"\x1b\[[0-?]*[ -/]*[@-~]", b"", frame).decode()
        # Escape closes only the popup, the next Escape cancels the dialog.
        frame = send(b"\x1b")
        assert b"Audio output (AUHAL)" in frame
        frame = send(b"\x1b")
        assert b"Audio output (AUHAL)" not in frame and b"Patterns" in frame
        # Reopen and walk the fields top to bottom: output, input, sample
        # rate, buffer, headroom, MIDI, MIDI knobs, cores. Disabled output and MIDI apply
        # without device access.
        send(F1 + b"jjjj\r")
        send(b"\rk\r")  # Output: System default -> Disabled
        frame = send(b"\t\r")  # Audio input: Disabled, System default, then the devices
        assert b"AUHAL - System default" in frame
        send(b"\x1b")
        send(b"\t\rjjj\r")  # Device default -> 96 kHz
        frame = send(b"\t\rjj\r")  # 128 -> 512 frames
        assert b"512 frames" in frame
        frame = send(b"\t\r")  # Master headroom, -12 dB by default
        assert b"-12 dB (default)" in frame and b"0 dB (none)" in frame, re.sub(rb"\x1b\[[0-?]*[ -/]*[@-~]", b"", frame).decode()
        send(b"k\r")  # -12 -> -9 dB
        send(b"\t\rk\r")  # MIDI: System default -> Disabled
        frame = send(b"\t\r")  # MIDI knobs: Absolute by default
        assert b"Relative (endless encoders" in frame, re.sub(rb"\x1b\[[0-?]*[ -/]*[@-~]", b"", frame).decode()
        send(b"j\r")  # Absolute -> Relative
        frame = send(b"\t\r")  # CPU cores choices come from the OS
        assert b"1 core (no worker threads)" in frame, re.sub(rb"\x1b\[[0-?]*[ -/]*[@-~]", b"", frame).decode()
        send(b"j\r")  # All cores -> 1 core
        # Applying can span multiple animation/repaint ticks under CI load.
        # Wait for the completed configuration instead of sampling a fixed
        # 250 ms window that may contain only the button press frame.
        frame = send_until(b"\t\r", b"Settings applied. Audio disabled.")
        assert b"Settings applied. Audio disabled." in frame
        # The knob mode is kept between runs, in the settings file.
        with open(settings_file) as saved:
            assert "midi_knobs = relative" in saved.read()
        frame = send(F1 + b"jjjj\r")
        assert b"MIDI input adapter" in frame and b"Disabled" in frame and b"512 frames" in frame and b"96 kHz" in frame
        assert b"-9 dB" in frame and b"1 core (no worker threads)" in frame and b"Relative (endless encoders" in frame
        # Resize with an expanded dropdown, exercising clipping and overlay.
        send(b"\r")
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 10, 30, 0, 0))
        assert read_for()
        send(b"\x1b")
        send(b"\x1b")
        send(b"q")
        deadline = time.monotonic() + 3
        while process.poll() is None and time.monotonic() < deadline:
            read_for(0.1)
        assert process.poll() == 0
        after = termios.tcgetattr(slave)
        pending = getattr(termios, "PENDIN", 0)
        before[3] &= ~pending
        after[3] &= ~pending
        assert after == before
        print("PASS: Settings dropdowns, cancel, apply disabled devices, CPU cores, resize, shutdown")
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        os.close(master)
        os.close(slave)


if __name__ == "__main__":
    main()
