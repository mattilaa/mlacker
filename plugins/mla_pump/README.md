# Mla Pump

A tempo-locked sidechain pump: a VST3 effect (subcategory `Fx|Dynamics`)
that dips a track's gain on every beat and lets it swell back, the
"breathing" of a trance or house mix, in the manner of LFO Tool or Kickstart.
A sidechained compressor keyed from the kick does the same job, but mlacker
routes no sidechains. On a four-on-the-floor kick, a shape locked to the
beat dips at the same moments, and it dips exactly the same way every time.

The DSP is MLang (`src/mla_pump_dsp.mla`); `src/plugin.cpp` is the VST3
wrapper.

## The shape

Each period (a 1/4 note by default) starts on the grid line:

1. **Attack:** the gain falls linearly from full to 1 − Depth in Attack ms.
2. **Hold:** it stays down for Hold, a fraction of the period.
3. **Release:** it swells back to full gain over Release, a fraction of the
   period, along x^(1 + 3 × Curve).
   - At Curve 0 it is a straight ramp.
   - Higher values stay down longer and come back late, which is the
     pumping sound.

If Hold and Release add up to more than the period, the release is
shortened, so the gain always reaches full by the next dip.

## Tempo

- **Tempo Source = Host** (the default) follows the host's tempo, and its
  quarter-note position while the transport plays. In mlacker the dips land
  on the sequencer's beats, which is where the kick plays.
- **Manual**, or when the host gives no tempo: the grid runs free at **BPM**.

## Parameters

IDs start at 100 and are stable.

| ID | Parameter | Range | Default |
|----|-----------|-------|---------|
| 100 | Depth | 0–1 (gain reduction at the bottom of the dip) | 0.7 |
| 101 | Rate | 1/1, 1/2, 1/4, 1/8, 1/16, 1/4. (dotted) | 1/4 |
| 102 | Attack | 0–20 ms | 1 ms |
| 103 | Hold | 0–0.5 of the period | 0.05 |
| 104 | Release | 0.05–1 of the period | 0.55 |
| 105 | Curve | 0–1 | 0.4 |
| 106 | Tempo Source | Host, Manual | Host |
| 107 | BPM | 20–400 | 120 |
| 108 | Bypass | off/on | off |

The mod wheel (CC 1) sets Depth.

## In mlacker

Put it on the tracks that should duck under the kick (bass, pads, leads),
not on the drums.

1. With Pattern view focused, press `f` to show the insert slots.
2. Select the track and a slot, press Enter and choose `MlaPump.vst3`.
3. Put it after any limiter or compressor on the track, so they don't even
   the pump back out.

Inserts belong to a pattern, so a breakdown without a kick can simply leave
the pump out.

`examples/mla8000_supersaw_arp.mlaproj` uses it on the bass, the pad and the
arpeggios.

## Build and test

From the repository root, `./build.sh --plugins` builds every plug-in,
including this one; add `--install` to copy them to the plug-in directory.
To build and test this plug-in on its own:

```sh
../../subprojects/mlang/build/mlang pkg --config mlang.toml run build   # -> build/cmake/VST3/Release/MlaPump.vst3
../../subprojects/mlang/build/mlang pkg --config mlang.toml run test
```

`tests/pump_tests.cpp` drives the processor with a constant input, so the
output is the gain curve itself. It checks:

- the linear release from 1 − Depth
- Curve
- Hold, and that the gain recovers by the next beat
- the attack ramp
- following the host's beat at the 1/4 and 1/2 rates
- Depth 0 and Bypass being transparent
- state round trips
