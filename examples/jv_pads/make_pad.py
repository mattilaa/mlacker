"""Synthesize JV-1080-style pad samples (root C-4) as 16-bit stereo WAVs.
Pure Python, no numpy: wavetable saws, sine partials, biquad filters, noise.

Usage: make_pad.py <preset> <out.wav>
Presets: atmosphere, dawn2dusk, footprint97, jd800pluck, jvchoir, clockbling
(clockbling's root is C-6)."""
import math
import random
import struct
import sys
import wave

RATE = 44100
ROOT = 261.6256  # C-4
TABLE = 4096


def saw_table(harmonics):
    """One period of a band-limited saw from `harmonics` partials."""
    table = [0.0] * TABLE
    for n in range(1, harmonics + 1):
        amp = 1.0 / n
        for i in range(TABLE):
            table[i] += amp * math.sin(2 * math.pi * n * i / TABLE)
    peak = max(abs(v) for v in table)
    return [v / peak for v in table]


class Biquad:
    """RBJ band-pass (constant 0 dB peak) or low-pass; `tune` re-designs it."""
    def __init__(self, kind, freq, q):
        self.kind, self.q = kind, q
        self.x1 = self.x2 = self.y1 = self.y2 = 0.0
        self.tune(freq)

    def tune(self, freq):
        w = 2 * math.pi * freq / RATE
        alpha = math.sin(w) / (2 * self.q)
        cos = math.cos(w)
        if self.kind == "bp":
            b0, b1, b2 = alpha, 0.0, -alpha
        else:
            b0 = b2 = (1 - cos) / 2
            b1 = 1 - cos
        a0, a1, a2 = 1 + alpha, -2 * cos, 1 - alpha
        self.b = (b0 / a0, b1 / a0, b2 / a0)
        self.a = (a1 / a0, a2 / a0)

    def run(self, x):
        b0, b1, b2 = self.b
        a1, a2 = self.a
        y = b0 * x + b1 * self.x1 + b2 * self.x2 - a1 * self.y1 - a2 * self.y2
        self.x2, self.x1 = self.x1, x
        self.y2, self.y1 = self.y1, y
        return y


class SawStack:
    """Detuned band-limited saws read from one wavetable."""
    def __init__(self, table, detunes, rng):
        self.table = table
        self.phases = [rng.random() for _ in detunes]
        self.steps = [ROOT * 2 ** (cents / 1200) / RATE for cents in detunes]

    def run(self):
        s = 0.0
        table = self.table
        for v, step in enumerate(self.steps):
            p = self.phases[v]
            index = p * TABLE
            k = int(index)
            frac = index - k
            a = table[k % TABLE]
            s += a + (table[(k + 1) % TABLE] - a) * frac
            p += step
            self.phases[v] = p - int(p)
        return s / len(self.steps)


class Choir:
    """An ensemble singing "aah": per singer a saw with its own vibrato
    (4.6-5.8 Hz, about +/-12 cents) and slow drift, the sum through four
    vowel formants (700, 1220, 2600, 3300 Hz) with a little breath."""
    def __init__(self, table, singers, seed):
        rng = random.Random(seed)
        self.table, self.rng = table, rng
        self.detunes = [rng.uniform(-14, 14) for _ in range(singers)]
        self.rates = [rng.uniform(4.6, 5.8) for _ in range(singers)]
        self.depths = [rng.uniform(8, 16) for _ in range(singers)]
        self.offsets = [rng.random() * 2 * math.pi for _ in range(singers)]
        self.drifts = [rng.uniform(0.07, 0.19) for _ in range(singers)]
        self.phases = [rng.random() for _ in range(singers)]
        self.steps = [ROOT / RATE] * singers
        self.formants = [(Biquad("bp", 700, 5.0), 1.0), (Biquad("bp", 1220, 7.0), 0.55),
                         (Biquad("bp", 2600, 11.0), 0.22), (Biquad("bp", 3300, 13.0), 0.12)]
        self.tilt = Biquad("lp", 3600, 0.7)
        self.count = 0

    def run(self, t):
        if self.count % 32 == 0:
            for v in range(len(self.steps)):
                cents = (self.detunes[v] + self.depths[v] * math.sin(2 * math.pi * self.rates[v] * t + self.offsets[v])
                         + 6 * math.sin(2 * math.pi * self.drifts[v] * t + self.offsets[v] * 1.7))
                self.steps[v] = ROOT * 2 ** (cents / 1200) / RATE
        self.count += 1
        s = 0.0
        table = self.table
        for v, step in enumerate(self.steps):
            p = self.phases[v]
            index = p * TABLE
            k = int(index)
            a = table[k % TABLE]
            s += a + (table[(k + 1) % TABLE] - a) * (index - k)
            p += step
            self.phases[v] = p - int(p)
        s /= len(self.steps)
        breath = self.rng.uniform(-1.0, 1.0) * 0.08
        voice = 0.0
        for formant, gain in self.formants:
            voice += formant.run(s + breath) * gain
        return self.tilt.run(voice * 2.4)


def jvchoir(table, detunes, seed, frames):
    """The JV-1080 "aah" choir on its own, with a slow ensemble swell."""
    choir = Choir(table, 8, seed)
    out = []
    for i in range(frames):
        t = i / RATE
        swell = 0.9 + 0.1 * math.sin(2 * math.pi * 0.17 * t + seed)
        out.append(choir.run(t) * swell)
    return out


def clockbling(table, detunes, seed, frames):
    """A clock-chime "bling" at C-6: inharmonic bell partials, the higher
    ones dying faster, and a 3 ms bright tick on the attack."""
    root = ROOT * 4
    ratios = [1.0, 2.41, 3.93, 5.40, 6.79, 8.93]
    levels = [1.0, 0.55, 0.38, 0.25, 0.16, 0.10]
    decays = [1.10, 0.55, 0.32, 0.22, 0.15, 0.10]
    # The bell itself is mono; its chorus, echo and reverb make it wide.
    spread = 1.0
    # Both ears share the partials' phases, so the bell stays focused.
    shared = random.Random(7)
    phases = [shared.random() for _ in ratios]
    tick = Biquad("bp", 6500, 2.0)
    out = []
    for i in range(frames):
        t = i / RATE
        s = 0.0
        for n, ratio in enumerate(ratios):
            s += math.sin(2 * math.pi * (root * ratio * spread * t + phases[n])) * levels[n] * math.exp(-t / decays[n])
        click = tick.run(shared.uniform(-1.0, 1.0)) * (1.0 - t / 0.003) if t < 0.003 else tick.run(0.0)
        out.append(s * 0.5 + click * 1.5)
    return out


def atmosphere(table, detunes, seed, frames):
    """Roland "Atmosphere": a choir-like "aah" over saws, with a breath band."""
    rng = random.Random(seed)
    saws = SawStack(table, detunes, rng)
    f1, f2 = Biquad("bp", 730, 4.0), Biquad("bp", 1150, 5.0)
    tone, breath = Biquad("lp", 5200, 0.7), Biquad("bp", 2600, 1.2)
    out = []
    for i in range(frames):
        t = i / RATE
        s = saws.run()
        swell = 0.85 + 0.15 * math.sin(2 * math.pi * 0.21 * t + seed)
        choir = 0.55 * s + 1.6 * f1.run(s) + 1.2 * f2.run(s)
        air = breath.run(rng.uniform(-1.0, 1.0)) * (0.10 + 0.05 * math.sin(2 * math.pi * 0.13 * t))
        out.append(tone.run(choir * swell + air))
    return out


def dawn2dusk(table, detunes, seed, frames):
    """"Dawn 2 Dusk": an evolving soundscape. Soft saws, glassy partials that
    fade in and out on their own slow cycles, and a wind band sweeping."""
    rng = random.Random(seed)
    saws = SawStack(table, detunes, rng)
    choir = Choir(table, 6, seed + 10)
    soft = Biquad("lp", 2400, 0.6)
    tone = Biquad("lp", 7000, 0.7)
    wind = Biquad("bp", 900, 2.5)
    # Glass: near-harmonic partials (slightly stretched, like a bell layer).
    ratios = [2.0, 3.01, 4.98, 6.03, 8.07]
    rates = [0.047, 0.071, 0.059, 0.083, 0.037]
    starts = [rng.random() * 2 * math.pi for _ in ratios]
    glass_phase = [rng.random() for _ in ratios]
    out = []
    for i in range(frames):
        t = i / RATE
        body = soft.run(saws.run())
        glass = 0.0
        for n, ratio in enumerate(ratios):
            glass_phase[n] = (glass_phase[n] + ROOT * ratio / RATE) % 1.0
            level = max(0.0, math.sin(2 * math.pi * rates[n] * t + starts[n])) ** 2
            glass += math.sin(2 * math.pi * glass_phase[n]) * level / (n + 1.5)
        if i % 64 == 0:
            # The wind's centre drifts 500..3000 Hz over about 11 s.
            wind.tune(1750 + 1250 * math.sin(2 * math.pi * 0.09 * t + seed))
        air = wind.run(rng.uniform(-1.0, 1.0)) * 0.22
        swell = 0.8 + 0.2 * math.sin(2 * math.pi * 0.11 * t + seed)
        voices = choir.run(t) * (0.75 + 0.25 * math.sin(2 * math.pi * 0.06 * t + seed))
        out.append(tone.run(0.6 * body * swell + 0.55 * voices + 0.35 * glass + air))
    return out


def footprint97(table, detunes, seed, frames):
    """A bright string-like supersaw pad: wide detune, a presence band, a
    slow sweep of the top end."""
    rng = random.Random(seed)
    saws = SawStack(table, detunes, rng)
    presence = Biquad("bp", 1800, 1.4)
    top = Biquad("lp", 6500, 0.8)
    out = []
    for i in range(frames):
        t = i / RATE
        if i % 64 == 0:
            top.tune(5200 + 1800 * math.sin(2 * math.pi * 0.15 * t + seed))
        s = saws.run()
        out.append(top.run(0.8 * s + 0.6 * presence.run(s)))
    return out


def jd800pluck(table, detunes, seed, frames):
    """A JD-800-style synth pluck: saws plus a pulse (two saws a half period
    apart) through a resonant low-pass that snaps shut, on a fast decay."""
    rng = random.Random(seed)
    saws = SawStack(table, detunes, rng)
    shifted = SawStack(table, detunes, rng)
    shifted.phases = [(p + 0.5) % 1.0 for p in saws.phases]
    snap = Biquad("lp", 9000, 1.8)
    out = []
    for i in range(frames):
        t = i / RATE
        if i % 32 == 0:
            # Cutoff falls from 9 kHz towards 500 Hz with a 90 ms time constant.
            snap.tune(500 + 8500 * math.exp(-t / 0.09))
        a = saws.run()
        pulse = a - shifted.run()
        amp = math.exp(-t / 0.32)
        out.append(snap.run(0.6 * a + 0.5 * pulse) * amp)
    return out


PRESETS = {
    "atmosphere": (atmosphere, 6.0, 48, [-14, -9, -4, 0, 5, 10, 15], [-15, -10, -5, 1, 4, 9, 14]),
    "dawn2dusk": (dawn2dusk, 10.0, 32, [-8, -3, 2, 7], [-7, -2, 3, 8]),
    "clockbling": (clockbling, 2.5, 8, [], []),
    "jvchoir": (jvchoir, 6.0, 40, [], []),
    "jd800pluck": (jd800pluck, 1.5, 64, [-6, 0, 7], [-7, 1, 6]),
    "footprint97": (footprint97, 6.0, 64, [-26, -17, -9, -3, 0, 4, 10, 18, 25], [-25, -18, -10, -4, 1, 3, 9, 17, 26]),
}


def main():
    voice, seconds, harmonics, left_detunes, right_detunes = PRESETS[sys.argv[1]]
    frames = int(RATE * seconds)
    table = saw_table(harmonics)
    left = voice(table, left_detunes, 1, frames)
    right = voice(table, right_detunes, 2, frames)
    # Fade in over 40 ms (the sampler's envelope shapes the rest).
    fade = int(0.04 * RATE)
    for i in range(fade):
        left[i] *= i / fade
        right[i] *= i / fade
    peak = max(max(abs(v) for v in left), max(abs(v) for v in right))
    gain = 0.7 / peak  # about -3 dBFS
    with wave.open(sys.argv[2], "wb") as out:
        out.setnchannels(2)
        out.setsampwidth(2)
        out.setframerate(RATE)
        data = bytearray()
        for l, r in zip(left, right):
            data += struct.pack("<hh", int(l * gain * 32767), int(r * gain * 32767))
        out.writeframes(bytes(data))
    print(f"{sys.argv[2]}: {frames} frames")


if __name__ == "__main__":
    main()
