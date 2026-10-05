"""Keep PTY smoke tests in step with mlacker on a slow or loaded machine.

A fixed read window started when the keys are written can end before the app
has even read them on a loaded CI runner, and a window can end in the middle
of a repaint. These helpers wait for the app to read the keys first and finish
the repaint in progress, so the windows measure the app's response, not the
runner's scheduling.
"""
import fcntl
import os
import select
import struct
import termios
import time

# A repaint starts at the top-left cell and ends by resetting the colors.
FRAME_START = b"\x1b[1;1H"
FRAME_END = b"\x1b[0m"


def unread_input(slave):
    """Bytes written to the PTY that the app has not read yet."""
    return struct.unpack("i", fcntl.ioctl(slave, termios.FIONREAD, b"\0\0\0\0"))[0]


def wait_consumed(master, slave, data, timeout=5.0):
    """Wait until the app has read everything written to its input.

    Keeps reading the app's output into `data` meanwhile: a full PTY output
    buffer blocks the app's repaint, and with it the reading of its input."""
    deadline = time.monotonic() + timeout
    while unread_input(slave) > 0 and time.monotonic() < deadline:
        if select.select([master], [], [], 0.005)[0]:
            chunk = os.read(master, 65536)
            if not chunk:
                break
            data.extend(chunk)
    return data


def inside_frame(data):
    """True when `data` stops in the middle of a repaint."""
    return data.rfind(FRAME_START) > data.rfind(FRAME_END)


def finish_frame(master, data, timeout=2.0):
    """Read on until `data` does not end inside a repaint."""
    deadline = time.monotonic() + timeout
    while inside_frame(data) and time.monotonic() < deadline:
        if select.select([master], [], [], 0.03)[0]:
            chunk = os.read(master, 65536)
            if not chunk:
                break
            data.extend(chunk)
    return data
