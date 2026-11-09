"""CLI information, invalid arguments, and startup audio settings in a real PTY."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from session_tui_smoke import Terminal

F1 = b"\x1bOP"
SETTINGS = F1 + b"jjjj\r"


def command(*args):
    return subprocess.run([sys.argv[1], *args], input=b"", stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, timeout=15)


def main():
    help_result = command("--help")
    assert help_result.returncode == 0
    for option in (b"--list-outputs", b"--output", b"--latency", b"--buffer-size", b"--sample-rate"):
        assert option in help_result.stdout, help_result.stdout
    assert b"\x1b" not in help_result.stdout
    assert command("-h").stdout == help_result.stdout
    version = command("--version")
    assert version.returncode == 0 and version.stdout.startswith(b"mlacker ")
    devices = command("--list-outputs")
    assert devices.returncode == 0 and b"Audio outputs" in devices.stdout
    assert b"System default" in devices.stdout and b"Disabled" in devices.stdout
    assert b"\x1b" not in devices.stdout
    for args in (("--unknown",), ("--output",), ("--latency", "0"), ("--sample-rate", "22050"),
                 ("--output", "999999"), ("a.mlack", "b.mlack")):
        result = command(*args)
        assert result.returncode == 2, (args, result.stdout, result.stderr)
        assert b"\x1b" not in result.stdout

    with tempfile.TemporaryDirectory(prefix="mlacker-cli-") as directory:
        session = Path(directory) / "startup options.mlack"
        tui = Terminal("--output", "disabled", "--latency", "512", "--sample-rate", "96000")
        try:
            frame = tui.read(1)
            assert b"Untitled" in frame and b"Audio output (AUHAL)" not in frame, frame[-4000:]
            frame = tui.send_until(SETTINGS, b"Audio output (AUHAL)")
            assert b"Disabled" in frame and b"512 frames" in frame and b"96 kHz" in frame, frame[-4000:]
            tui.send(b"\x1b")
            tui.send(b"\x13")
            frame = tui.send(b"\x15" + os.fsencode(session) + b"\r", 0.7)
            assert b"Saved:" in frame, frame[-4000:]
        finally:
            tui.close()
        # A path with spaces, options after the path, and the --name=value syntax.
        tui = Terminal("--output=disabled", str(session), "--buffer-size=256", "--sample-rate=44100")
        try:
            frame = tui.read(1.2)
            assert b"startup options.mlack" in frame, frame[-4000:]
            assert b"Audio output (AUHAL)" not in frame
            frame = tui.send_until(SETTINGS, b"Audio output (AUHAL)")
            assert b"Disabled" in frame and b"256 frames" in frame and b"44.1 kHz" in frame, frame[-4000:]
            tui.send(b"\x1b")
        finally:
            tui.close()
    print("PASS: CLI help, device list, validation, and audio options retained when opening a session")


if __name__ == "__main__":
    main()
