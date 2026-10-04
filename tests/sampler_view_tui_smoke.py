"""Mla Sampler pane (Shift+S) in Pattern view: toggling, key capture, loading and
clearing slots, key zones, velocity ranges, the Sound page (level, pan, tune,
per-slot envelopes, sample start, groups and group mode), the Filter page (a
two-stage filter chain and filter envelope), the LFO pages, the Mod page, the Play page (mode, glide, unison), loop mode / loop point / output edits, the wave view's
frame-accurate loop and sample start markers, zero-crossing snap and crossfade,
editing a slot's sample (crop to its loop), auditioning slots, routing an aux output to
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
SAVE_SESSION = F1 + b"jjljjj\r"  # File > Session > Save session as


def write_wav(path, value):
    with wave.open(str(path), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(48000)
        out.writeframes(struct.pack("<h", value) * 4800)


def write_float_aifc(path, value):
    """A mono 48 kHz float32 AIFF-C ("fl32") of 4800 frames at `value`."""
    comm = struct.pack(">hIh", 1, 4800, 32) + bytes.fromhex("400ebb80000000000000") + b"fl32" + b"\x00\x00"
    samples = struct.pack(">f", value) * 4800
    chunks = b"COMM" + struct.pack(">I", len(comm)) + comm
    chunks += b"SSND" + struct.pack(">I", len(samples) + 8) + bytes(8) + samples
    with open(path, "wb") as out:
        out.write(b"FORM" + struct.pack(">I", len(chunks) + 4) + b"AIFC" + chunks)


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
        # A float32 AIFF-C: auto-map (and the plugins) decode it too.
        tone = root / "Tone_G4.aif"
        write_float_aifc(tone, 0.25)
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
            frame = expect(tui.send(F1 + b"lll" + b"j" * 11 + b"\r"), b"Plugin output to route", b"Out 2  > A1", b"Out 8  > MAIN",
                           b"Send A  > FX1", b"Send B  > FX2")
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
            tui.send(b"mm")  # past the Cut marker, back to the loop start
            tui.send(b"w")
            # E edits the slot's sample with its loop (frames 50-4750)
            # selected; a destructive save of the trim to it, under the
            # original name, makes the loop span the whole sample.
            expect(tui.send_until(b"E", b"Sample editor"), b"Sample editor", b"Sel 0.001-0.098 s")
            expect(tui.send(b"t"), b"Trimmed to the selection")
            expect(tui.send(b"\x13", 0.6), b"square - Trim 1.wav")
            tui.send(b"\x15square.wav\t ")
            expect(tui.send_until(b"\r", b"the slot's loop now spans it"), b"over sample 3")
            expect(tui.send(b"w"), b"Start 0", b"End 4700", b"Len 4700/4700", b"Ofs 0")
            tui.send(b"w")
            tui.send(b"kk")
            # The Cut marker (m after Ofs) edits slot 1's slice markers: a
            # constant sample has only the one at 0; n adds one half way, L
            # moves it a frame. The session keeps them.
            expect(tui.send(b"w"), b"Slot 1 pad.wav")
            expect(tui.send(b"mmm"), b"[Cut 1/1 at 0]", b"1 cuts")
            expect(tui.send(b"n"), b"Slot 1: 2 cuts", b"[Cut 2/2 at 2400]")
            expect(tui.send(b"L"), b"[Cut 2/2 at 2401]")
            expect(tui.send(b"u"), b"Slot 1: 1 cuts detected", b"[Cut 1/1 at 0]")
            tui.send(b"n")
            expect(tui.send(b"L"), b"[Cut 2/2 at 2401]")
            tui.send(b"m")
            tui.send(b"w")

            # e: the Sound page of slot 1. Level 0 dB, centred, the instance
            # envelope; its own A/D/S/R need Env set to Own first.
            expect(tui.send(b"e"), b"Level", b"Env", b"Sus%", b"Grp", b"Chk", b"Rev", b"0.0", b"C", b"Inst")
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
            tui.send(b"l")
            expect(tui.send(b"K"), b"On")     # reverse
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
            # e again: the LFO page. A synced square at 1/8 with 50% tremolo;
            # Rate dims once Sync is on.
            # e: filter stages 3 and 4 and how the four connect.
            expect(tui.send(b"e"), b"Filter 3/4 |", b"Chain", b"F3", b"F4", b"Serial")
            expect(tui.send(b"K"), b"Parall")
            tui.send(b"l")
            expect(tui.send(b"KKKK"), b"HP24")
            # e: each stage's drive and mod scale; filter 1 drives at 20 %
            # and takes 80 % of the LFO's cutoff movement.
            expect(tui.send(b"e"), b"Filter Mod |", b"Drv1", b"Mod4", b"+100")
            expect(tui.send(b"]]"), b"20")
            tui.send(b"l")
            expect(tui.send(b"[["), b"+80")
            expect(tui.send(b"e"), b"Shape", b"Rate", b"Trig", b"Sine", b"5.0Hz", b"1/4", b"Retrg")
            expect(tui.send(b"KKKK"), b"Sqr")
            tui.send(b"ll")
            expect(tui.send(b"K"), b"On")
            tui.send(b"l")
            expect(tui.send(b"K"), b"1/8")
            tui.send(b"llll")
            expect(tui.send(b"]]]]]"), b"50")
            # e: LFO 2, then the Mod page. Route 1: Velocity -> Level, -10%.
            expect(tui.send(b"e"), b"LFO 2 |", b"Shape", b"Sine")
            expect(tui.send(b"e"), b"Mod |", b"Src1", b"Tgt4", b"Amt4")
            expect(tui.send(b"KKKKK"), b"Veloc")
            tui.send(b"l")
            expect(tui.send(b"KKKK"), b"Level")
            tui.send(b"l")
            expect(tui.send(b"["), b"-10")
            # Route 2's source: the controllers come last, Mod CC at the end.
            tui.send(b"l")
            expect(tui.send(b"K" * 11), b"ModCC")
            # e: the Play page. Legato with a glide, three unison voices.
            expect(tui.send(b"e"), b"Play |", b"Glide", b"Uni", b"Poly")
            expect(tui.send(b"KK"), b"Legato")
            tui.send(b"l")
            expect(tui.send(b"]]]"), b"180ms")
            tui.send(b"l")
            expect(tui.send(b"KKKKKKKKK"), b"8")  # up to 8 voices, no further
            expect(tui.send(b"JJJJJ"), b"3")
            tui.send(b"l")
            expect(tui.send(b"]]"), b"20")
            # Then the pitch envelope, an octave up, and Send A at 30 %.
            tui.send(b"ll")
            expect(tui.send(b"]"), b"+12.0")
            tui.send(b"lll")
            expect(tui.send(b"]]]"), b"30")
            tui.send(b"hhhhh")
            # W saves slot 1 as a slot preset; R loads it into empty slot 4,
            # its sample and settings.
            slot_preset = root / "slot1.mlaslot"
            expect(tui.send_until(b"W", b"Save slot 1 (.mlaslot)"), b"Save slot 1 (.mlaslot)")
            expect(tui.send(b"\x15" + bytes(root / "slot1") + b"\r", 0.7), b"Saved slot preset:")
            assert slot_preset.exists()
            tui.send(b"jjj")
            expect(tui.send_until(b"R", b"Load preset into slot 4"), b"Load preset into slot 4")
            expect(tui.send(b"\x15" + bytes(slot_preset) + b"\r", 0.7), b"Slot 4: loaded")
            # Slot 4 plays Legato now: one step down is Mono (Poly would stay).
            tui.send(b"hhhh")
            expect(tui.send(b"J"), b"Mono")
            expect(tui.send(b"K"), b"Legato")
            expect(tui.send(b"w"), b"Slot 4 pad.wav")
            tui.send(b"w")
            tui.send(b"kkk")
            # e: velocity curve and key level, then tempo sync.
            expect(tui.send(b"e"), b"Vel/Tempo |", b"VCurve", b"Linear", b"Beats")
            expect(tui.send(b"K"), b"Soft")
            tui.send(b"ll")
            expect(tui.send(b"KK"), b"+1.0")
            tui.send(b"l")
            expect(tui.send(b"KK"), b"Stretch")
            expect(tui.send(b"K"), b"Beats")
            expect(tui.send(b"J"), b"Stretch")
            tui.send(b"l")
            expect(tui.send(b"K"), b"6")
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
            expect(tui.send(b"e"), b"Parall", b"HP24")
            expect(tui.send(b"e"), b"Filter Mod |", b"20", b"+80")
            expect(tui.send(b"e"), b"Sqr", b"On", b"1/8", b"50")
            tui.send(b"e")
            expect(tui.send(b"e"), b"Veloc", b"Level", b"-10", b"ModCC")
            expect(tui.send(b"e"), b"Legato", b"180ms", b"20", b"+12.0", b"30")
            expect(tui.send(b"e"), b"Soft", b"+1.0", b"Stretch")
            tui.send(b"e")
            # Hand-set cuts survive it too.
            expect(tui.send(b"w"), b"Slot 1 pad.wav")
            expect(tui.send(b"mmm"), b"2 cuts", b"[Cut 1/2 at 0]")
            expect(tui.send(b"."), b"[Cut 2/2 at 2401]")
            tui.send(b"m")
            tui.send(b"w")
            # Frame-accurate loop points survive the round trip.
            tui.send(b"jj")
            expect(tui.send(b"w"), b"Slot 3 square.wav", b"Start 0", b"End 4700", b"Ofs 0")
            tui.send(b"w")
            # C slices slot 3's sample into the slots after it: 8 equal
            # slices at first, down to 2, which Enter puts on slots 3 and 4.
            expect(tui.send(b"C"), b"Slice slot 3", b"8 slices equal", b"Slice to slots 3-10")
            expect(tui.send(b"t"), b"at transients")
            expect(tui.send(b"t"), b"at cuts")
            expect(tui.send(b"t"), b"slices equal")
            expect(tui.send(b"JJJJJJJ"), b"2 slices equal", b"Slice to slots 3-4")
            expect(tui.send(b"\r", 0.6), b"Sliced slot 3 into slots 3-4")
            expect(tui.send(b"w"), b"Slot 3 square slice 1.wav", b"End 2350")
            tui.send(b"w")
            # A auto-maps note-named samples as key zones from the selected slot.
            tui.send(b"j" * 7)
            expect(tui.send_until(b"A", b"Auto-map samples from slot 10"), b"Auto-map samples from slot 10")
            expect(tui.send(b"\x15" + bytes(tone) + b"\r", 0.7), b"Auto-mapped 1 sample(s) to slots 10-10")
            expect(tui.send(b"w"), b"Slot 10 Tone_G4.aif")
            tui.send(b"w")
            # v shows the key map: slot 10 over every key, slots 3 and 4 as pads.
            expect(tui.send(b"v"), b"Key map | slot 10 Tone_G4.aif", b"Lyr", b"v1-127")
            expect(tui.send(b"v"), b"Mode", b"Loop")
            expect(tui.send(b"S"), b"Sampler closed")  # "q" quits again
        finally:
            tui.close()


if __name__ == "__main__":
    main()
