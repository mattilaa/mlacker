"""Check audible opening and closing in rendered JP8000 lead/sub examples."""
import array
import math
import sys
import wave


def main():
    with wave.open(sys.argv[1], "rb") as wav:
        assert wav.getnchannels() == 2 and wav.getsampwidth() == 2
        rate = wav.getframerate()
        samples = array.array("h", wav.readframes(wav.getnframes()))
    if sys.byteorder != "little":
        samples.byteswap()
    peak = max(abs(value) for value in samples) / 32768
    assert .01 < peak < .98, ("silence or clipping", peak)
    period = 128 * 60 / 138 / 4

    def brightness(first, last):
        start, stop = int(first * rate) * 2, int(last * rate) * 2
        region = samples[start:stop]
        energy = sum(float(value) ** 2 for value in region)
        assert energy > len(region), "Silent sweep region"
        # Normalized first-difference energy: high harmonics increase this
        # independently of volume. Compare the opening with BOTH closed ends.
        changes = sum(float(samples[i] - samples[i - 2]) ** 2
                      for i in range(start + 2, stop))
        return changes / energy

    for section in range(4):
        start = section * period
        closed = brightness(start + .1, start + 1.2)
        opened = brightness(start + period / 2, start + period / 2 + 1.1)
        returned = brightness(start + period - 1.2, start + period - .1)
        ratio = opened / max(closed, returned)
        assert math.isfinite(ratio) and ratio > 2, ("Sweep missing", section + 1, ratio)
        print(f"PASS: section {section + 1} opens and closes; brightness ratio {ratio:.1f}")


if __name__ == "__main__":
    main()
