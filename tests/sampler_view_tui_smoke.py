"""Mla Sampler pane (Shift+S) in Pattern view: toggling, key capture, loading and
clearing slots, key zones, velocity ranges, the Sound page (level, pan, tune,
per-slot envelopes, sample start, groups and group mode), the Filter page (a
two-stage filter chain and filter envelope), loop mode / loop point / output edits, the wave view's
frame-accurate loop and sample start markers, zero-crossing snap and crossfade, auditioning slots, routing an aux output to
an AUDIO track, sharing the pane with the virtual keyboard, and all of it
surviving a session round trip.

Usage: sampler_view_tui_smoke.py <mlacker> <MlaSampler.vst3>. No audio hardware.
"""
import struct
import sys
import tempfile
import wave
from pathlib import Path

from session_tui_smoke import Terminal

# The menu bar opens with F1.
F1 = b"\x1bOP"
BACKSPACE = b"\x7f"
SAVE_SESSION = F1 + b"jjjjj\r"


def write_wav(path, value):
    with wave.open(str(path), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(48000)
        out.writeframes(struct.pack("<h", value) * 4800)


def write_square(path):
    """4800 frames of a square wave: +0.5 for 50 frames, then -0.5, so the
    zero crossings fall on every multiple of 50."""
    with wave.open(str(path), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(48000)
        out.writeframes(b"".join(struct.pack("<h", 16000 if (f // 50) % 2 == 0 else -16000) for f in range(4800)))


def expect(frame, *needles):
    for needle in needles:
        assert needle in frame, (needle, frame[-4000:])
    return frame


def main():
    with tempfile.TemporaryDirectory(prefix="mlacker-sampler-") as directory:
        root = Path(directory)
        pad = root / "pad.wav"
        write_wav(pad, 12000)
        square = root / "square.wav"
        write_square(square)
        path = root / "sampler.mlack"
        tui = Terminal(cwd=directory)
        try:
            tui.read(0.8)
            # Without an Mla Sampler the pane says what to do.
            expect(tui.send(b"S"), b" Sampler ", b"Select an Instrument track playing Mla Sampler")
            expect(tui.send(b"S"), b"Sampler closed")

            # An AUDIO track (A1) to take the sampler's Out 2, then the
            # Instrument track, which stays selected.
            expect(tui.send(F1 + b"lll" + b"j\r"), b"Audio 1 [AUDIO]")
            expect(tui.send(F1 + b"lll" + b"jj\r"), b"Instrument track created")
            expect(tui.send(F1 + b"llllll\r"), b"Add VST3 instrument")
            expect(tui.send(b"\x15" + bytes(Path(sys.argv[2]).resolve()) + b"\r", 0.9), b"Instrument loaded: Mla Sampler")

            # Shift+S opens the pane on the selected track's instance: 16 empty
            # pad slots from the root key C-2, loop off over the whole sample, Main.
            frame = expect(tui.send(b"S"), b"Sampler | Mla Sampler #1 | Groups: Round-robin", b"Mode", b"VLo", b"VHi", b"Pad", b"C-2", b"(empty)", b"Off", b"Start%", b"0.0 ", b"100.0", b"Main")
            assert frame.count(b"(empty)") >= 4, frame[-4000:]

            # The focused pane keeps its keys: "m" does not toggle the mixer,
            # and "q" does not quit.
            frame = tui.send(b"m")
            assert b" Mixer " not in frame, frame[-4000:]
            tui.send(b"q")
            assert tui.process.poll() is None

            # Enter loads a file into the selected slot (slot 2).
            tui.send(b"j")
            expect(tui.send_until(b"\r", b"Load sample for pad 2"), b"Load sample for pad 2")
            expect(tui.send(b"\x15" + bytes(pad) + b"\r", 0.7), b"Pad 2: pad.wav (added to Audio)", b"pad.wav")

            # A pad slot has one key: its zone columns need Zone mode first.
            tui.send(b"l")
            expect(tui.send(b"K"), b"Slot 2 is a pad")
            tui.send(b"h")
            expect(tui.send(b"K"), b"Zone", b"G-9", b"C-4")  # whole keyboard, root C-4
            # [ and ] step keys by an octave: low C-3, root C-5, then Key Track off.
            tui.send(b"l")
            expect(tui.send(b"]]]]"), b"C-3")
            tui.send(b"ll")
            expect(tui.send(b"]"), b"C-5")
            tui.send(b"l")
            expect(tui.send(b"J"), b"Zone")
            # Velocity range: ] steps by 10 (1 -> 21), J lowers VHi to 126.
            tui.send(b"l")
            expect(tui.send(b"]]"), b"21")
            tui.send(b"l")
            expect(tui.send(b"J"), b"126")
            # Shift+J/K adjust the selected column: loop mode, start, end, output.
            tui.send(b"l")
            expect(tui.send(b"K"), b"Fwd")
            expect(tui.send(b"K"), b"Bidir")
            frame = tui.send(b"K")  # already the last mode
            assert b"Bidir" in frame, frame[-4000:]
            tui.send(b"l")
            expect(tui.send(b"KKK"), b"3.0 ")
            tui.send(b"l")
            expect(tui.send(b"J"), b"99.0 ")
            tui.send(b"ll")  # past Xf% to Out
            expect(tui.send(b"K"), b"Out 2")
            # o routes the slot's bus: Out 2 into A1's channel.
            expect(tui.send(b"o"), b"Output destination", b"MAIN with the instrument's output", b"MST  master", b"A1")
            tui.send(b"jj")
            expect(tui.send(b"\r", 0.5), b"Out 2 > A1", b"Out 2>A1")
            # Track > Route plugin outputs lists every aux bus and its route.
            frame = expect(tui.send(F1 + b"lll" + b"j" * 11 + b"\r"), b"Plugin output to route", b"Out 2  > A1", b"Out 8  > MAIN")
            expect(tui.send(b"\x1b", 0.5), b"Cancelled.")

            # p auditions the slot: a zone at its root (C-5), a pad at its key.
            # Without key releases the note ends once p stops repeating.
            expect(tui.send(b"p", 0.6), b"Playing slot 2 at C-5")
            tui.send(b"k")
            expect(tui.send(b"p", 0.6), b"Playing slot 1 at C-2")
            tui.send(b"j")

            # Backspace clears the slot; its loop settings stay.
            expect(tui.send(BACKSPACE, 0.5), b"Pad 2 cleared")
            tui.send(b"k")
            expect(tui.send_until(b"\r", b"Load sample for pad 1"), b"Load sample for pad 1")
            expect(tui.send(b"\x15" + bytes(pad) + b"\r", 0.7), b"Pad 1: pad.wav")

            # Wave view (w) of slot 3: the whole sample, loop start selected.
            tui.send(b"jj")
            expect(tui.send_until(b"\r", b"Load sample for pad 3"), b"Load sample for pad 3")
            expect(tui.send(b"\x15" + bytes(square) + b"\r", 0.7), b"Pad 3: square.wav")
            expect(tui.send(b"w"), b"Slot 3 square.wav", b"Loop Off", b"[Start 0]", b"End 4800", b"Len 4800/4800", b"Zoom 1x")
            # Shift+L moves a frame; z snaps to the nearest zero crossing.
            expect(tui.send(b"L" * 10), b"[Start 10]")
            expect(tui.send(b"z"), b"[Start 50]", b"Len 4750")
            # m picks the end marker: a frame back, then the crossing at 4750.
            expect(tui.send(b"m"), b"[End 4800]")
            expect(tui.send(b"H"), b"[End 4799]")
            expect(tui.send(b"z"), b"[End 4750]", b"Len 4700")
            expect(tui.send(b"="), b"Zoom 2x")
            expect(tui.send(b"-"), b"Zoom 1x", b"Xfade 0")
            # A forward loop can crossfade: [ and ] in the wave view. It is
            # capped by the 50 frames before the loop start.
            expect(tui.send(b"w"), b"Start%", b"Xf%")
            tui.send(b"hhhh")  # Out -> Loop
            expect(tui.send(b"K"), b"Fwd")
            expect(tui.send(b"w"), b"Loop Fwd", b"Xfade 0")
            expect(tui.send(b"]]]"), b"Xfade 50")
            # The third marker is the sample start: frames and snapping as
            # for the loop points.
            expect(tui.send(b"m"), b"[Ofs 0]")
            expect(tui.send(b"LLL"), b"[Ofs 3]")
            expect(tui.send(b"z"), b"[Ofs 50]")
            tui.send(b"m")
            tui.send(b"w")
            tui.send(b"kk")

            # e: the Sound page of slot 1. Level 0 dB, centred, the instance
            # envelope; its own A/D/S/R need Env set to Own first.
            expect(tui.send(b"e"), b"Level", b"Env", b"Sus%", b"Grp", b"Chk", b"0.0", b"C", b"Inst")
            tui.send(b"llll")
            expect(tui.send(b"K"), b"Slot 1 uses the instance envelope")
            tui.send(b"h")
            expect(tui.send(b"K"), b"Own")
            tui.send(b"l")
            expect(tui.send(b"]"), b"2ms")  # attack 10% of its curve
            tui.send(b"ll")
            expect(tui.send(b"["), b"90")   # sustain
            tui.send(b"hhhhhh")
            expect(tui.send(b"J"), b"-1.0")  # level, 1 dB down
            tui.send(b"l")
            expect(tui.send(b"K"), b"R2")    # pan
            tui.send(b"l")
            expect(tui.send(b"]"), b"+12.0")  # tune, an octave up
            tui.send(b"l" * 7)
            expect(tui.send(b"K"), b"1")      # group 1
            expect(tui.send(b"G"), b"Groups: Random")
            tui.send(b"l")
            expect(tui.send(b"KK"), b"2")     # choke group 2
            # e again: the Filter page. Filter 1 to LP24, 10 kHz, 6 dB, +1 octave
            # of filter envelope, 10% key tracking; FEnv needs Own for its ADSR.
            expect(tui.send(b"e"), b"F1", b"F2", b"FEnv", b"Off", b"20.0k", b"Inst")
            expect(tui.send(b"KK"), b"LP24")
            tui.send(b"l")
            expect(tui.send(b"["), b"10.0k")
            tui.send(b"l")
            expect(tui.send(b"]"), b"6")
            tui.send(b"l")
            expect(tui.send(b"]"), b"+1.0")
            tui.send(b"l")
            expect(tui.send(b"]"), b"10")
            tui.send(b"l")
            expect(tui.send(b"]"), b"+6.0")  # gain (for peak and shelves)
            tui.send(b"l" * 8)  # to FEnv's Atk
            expect(tui.send(b"K"), b"Slot 1 uses the instance filter envelope")
            tui.send(b"h")
            expect(tui.send(b"K"), b"Own")
            expect(tui.send(b"e"), b"Mode", b"Loop")

            # The virtual keyboard takes the pane over, and Shift+S takes it back.
            frame = expect(tui.send(b"P"), b"Keyboard")
            assert b"Sampler |" not in frame, frame[-4000:]
            frame = expect(tui.send(b"S"), b"Sampler | Mla Sampler #1")
            assert b" Keyboard " not in frame, frame[-4000:]
            expect(tui.send(b"S"), b"Sampler closed")

            expect(tui.send(SAVE_SESSION), b"Save session (.mlack)")
            expect(tui.send(b"\x15" + bytes(path) + b"\r", 0.7), b"Saved:")
        finally:
            tui.close()

        # Loop settings are plugin parameters: the session restores them.
        tui = Terminal(str(path), cwd=directory)
        try:
            expect(tui.read(1.2), b"Opened:")
            expect(tui.send(b"S"), b"Sampler | Mla Sampler #1", b"pad.wav", b"Zone", b"C-3", b"C-5", b"21", b"126", b"Bidir", b"3.0 ", b"99.0 ", b"Out 2>A1")
            # The Sound page's edits survive it too.
            expect(tui.send(b"e"), b"Own", b"-1.0", b"R2", b"+12.0", b"2ms", b"90", b"Groups: Random")
            expect(tui.send(b"e"), b"LP24", b"10.0k", b"+1.0", b"+6.0", b"Own")
            tui.send(b"e")
            # Frame-accurate loop points survive the round trip.
            tui.send(b"jj")
            expect(tui.send(b"w"), b"Slot 3 square.wav", b"[Start 50]", b"End 4750", b"Xfade 50", b"Ofs 50")
            tui.send(b"w")
            expect(tui.send(b"S"), b"Sampler closed")  # "q" quits again
        finally:
            tui.close()


if __name__ == "__main__":
    main()
