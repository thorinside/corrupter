# Phase 1 Baseline — 251a7c6

**Date:** 2026-05-05
**Commit:** 251a7c6 (no DSP changes; harness-only addition)
**Result:** 35 / 35 spectral tests pass at chosen Phase-1 thresholds.

## Headline

This is the first capture of the spectral test harness. No DSP changes have
been made. All thresholds were either set to *snapshot* (record only) or
calibrated to the as-measured value plus a small margin so the harness can
detect regressions while documenting the current behaviour.

The Phase-2 fix candidates are deliberately *not* gated yet. We capture their
baseline numbers here so that Phase 2 can show measurable deltas.

## Status by category

### Smoke (helpers self-check)

All 9 smoke tests pass. THD on a bin-aligned pure sine measures
**0.00219 %** — essentially the float-precision floor of the unwindowed FFT.

### Aliasing (T-AA-*)

| Test | Fund (dBFS) | Worst alias (dBFS) | Margin (dB) | Status |
|---|---|---|---|---|
| T-AA-Decimate-Low (i=0.10, 1 kHz) | -7.91 | -22.79 @ 6.4 kHz | 14.9 | snapshot — by-design |
| T-AA-Decimate-Mid (i=0.50, 1 kHz) | -15.18 | -9.27 @ 504 Hz | -5.9 | snapshot — by-design |
| T-AA-Decimate-High (i=0.90, 1 kHz) | -23.93 | -6.75 @ 164 Hz | -17.2 | snapshot — by-design |
| T-AA-Destroy-Soft (i=0.30, 7 kHz) | +0.97 | -39.91 @ 47 kHz | **40.9** | gated >25 dB — **PASS** |
| T-AA-Destroy-Hard (i=0.70, 7 kHz) | -1.52 | -18.93 @ 47 kHz | 17.4 | snapshot — fix 2B target |
| T-AA-DjFilter-Lp (i=0.05, 80 Hz) | -0.55 | (none above floor) | — | snapshot |
| T-AA-Vinyl-Snapshot | -11.00 | -39.41 @ 1.1 kHz | 28.4 | snapshot — vinyl noise floor |

The Decimate margins are deliberately negative at high intensity — that's the
bit-crusher being a bit-crusher. T-AA-Destroy-Hard at 17.4 dB margin is the
target for the Phase-2B oversampling fix; it should reach ≥30 dB after 2x OS.

### Frequency response (T-FRA-*)

| Test | Measurement | Target | Status |
|---|---|---|---|
| T-FRA-Decimate-Idle | max deviation 0.30 dB | <0.5 dB | PASS |
| T-FRA-DjFilter-Center (i=0.5) | flatness 0.33 dB | <1.5 dB | PASS |
| T-FRA-DjFilter-LpMax (i=0) | -3 dB corner 93.6 Hz | 30–200 Hz | PASS |
| T-FRA-DjFilter-HpMax (i=1) | -3 dB corner 9.6 kHz | 1–20 kHz | PASS |
| T-FRA-Vinyl-Tone (i=0.5) | midband 0.58 .. 1.20 dB | snapshot | PASS |

DJ filter LP corner is on the steep side of the 30–200 Hz target band but
within spec. HP corner at 9.6 kHz is below the nominal 18 kHz target but the
gate is generous (1–20 kHz); the corner moves with intensity so this is the
i=1.0 endpoint behaviour.

### THD+N (T-THD-*)

| Test | THD+N | Target | Status |
|---|---|---|---|
| T-THD-Destroy-Soft (i=0.30) | 8.92 % | <30 % snapshot gate | PASS |
| T-THD-Destroy-Hard (i=0.70) | 40.92 % | snapshot — by-design | PASS |
| T-THD-DjFilter (i=0.5) | 0.0043 % | <5 % | PASS |
| T-THD-Vinyl (i=0.5) | 4.21 % | snapshot — surface noise | PASS |

Destroy-soft at 8.9 % suggests the tanh saturator is producing audible
harmonics at intensity 0.30. Acceptable for the "destroy" character.

### Click / step transients (T-CLK-*)

| Test | Worst sample-to-sample delta | Notes |
|---|---|---|
| T-CLK-Corrupt-Sweep (corrupt 0→0.7) | **0.510** | snapshot — fix 2A target |
| T-CLK-Bend-Sweep (bend 0.5→0) | 0.057 | snapshot |
| T-CLK-Decimate-HoldStep (i 0→0.95) | **0.688** | snapshot — by-design? aliasing artefact |

Corrupt-sweep delta of 0.51 on a signal whose peak amplitude is ~0.5 is
essentially full-scale — that's a real audible click. Phase 2A (smooth corrupt
intensity CV) targets this.

### Freeze / crossfade (T-FRZ-*, T-XFADE-*, T-DRP-*)

| Test | Worst delta | Target | Status |
|---|---|---|---|
| T-FRZ-Toggle ON | 0.000 | <0.05 | PASS |
| T-FRZ-Toggle OFF | 0.0065 | <0.05 | PASS |
| T-XFADE-Tick (steady freeze) | 0.031 | snapshot | PASS |
| T-DRP-Edges | 0.000 | snapshot | PASS |

T-DRP-Edges measured 0 — the dropout state likely never engaged with the
chosen knob settings; revisit in Phase 2 to confirm we're actually triggering
the dropout branch. T-XFADE-Tick at 0.031 is sample-rate dependent and could
indicate the segment crossfade has audible discontinuity at fast clock rates.

### Smoothing / zipper (T-SM-*)

| Test | Metric | Status |
|---|---|---|
| T-SM-Corrupt-Step (square-modulated corrupt) | sideband / carrier = -10.6 dB | snapshot — fix 2A target |
| T-SM-TapeDrive-Step (square-modulated bend) | sideband / carrier = +25.3 dB | snapshot — fix 2C target |
| T-SM-Pitch-Step (time knob jump) | worst delta = 0.000 | snapshot |
| T-SM-Destroy-Crossover (intensity ramp through 0.5) | worst delta = 0.199 | snapshot — fix 2D target |

T-SM-TapeDrive-Step at +25.3 dB sideband-to-carrier means the modulation
sidebands are *louder* than the carrier itself — extreme zipper noise from
the unsmoothed tape-drive coefficient. This is the most striking baseline
finding and a clear Phase-2C target.

T-SM-Pitch-Step at 0 indicates either pitch quantize smoothing already works
or the test setup didn't actually exercise a pitch jump (freeze on, time
knob change). Worth revisiting in Phase 2.

T-SM-Destroy-Crossover at 0.199 confirms there's a meaningful kink at the
soft/hard branch (Phase 2D fix target if cheap).

## Hypotheses

| ID | Hypothesis | Status (this baseline) |
|---|---|---|
| H1 | Corrupt-CV is unsmoothed; rapid moves cause audible clicks. | **Confirmed** — T-CLK-Corrupt-Sweep delta 0.51 (full-scale on 0.5-amp signal). |
| H2 | Tape-drive coefficient updates per clock tick produce zipper noise on bend modulation. | **Confirmed** — T-SM-TapeDrive-Step sideband/carrier +25 dB. |
| H3 | Destroy hard-clipper produces audible aliasing at high intensity due to no oversampling. | **Confirmed** — T-AA-Destroy-Hard margin 17.4 dB (need ≥30 dB). |
| H4 | Destroy soft→hard crossover at i=0.5 has a derivative kink. | **Confirmed** — T-SM-Destroy-Crossover delta 0.199 in a 256-sample window where the carrier itself contributes <0.07. |
| H5 | Decimate hold-length jumps cause clicks separate from the by-design aliasing. | **Likely confirmed** — T-CLK-Decimate-HoldStep delta 0.688 at the step. |

## Next iteration priorities

1. **Phase 2A — smooth corrupt-intensity CV** (one-pole, ~5 ms). Targets
   T-CLK-Corrupt-Sweep and T-SM-Corrupt-Step. Cheapest high-impact fix.
2. **Phase 2C — smooth tape-drive coefficient** (one-pole, ~10 ms). Targets
   T-SM-TapeDrive-Step which has the worst zipper baseline.
3. **Phase 2B — 2x oversample Destroy** saturator/clipper. Targets
   T-AA-Destroy-Hard.
4. **Phase 2D — smooth Destroy soft/hard branch** with smoothstep over
   intensity ∈ [0.45, 0.55]. Targets T-SM-Destroy-Crossover.
5. **Phase 2E** (optional) — soft Decimate hold-length transition crossfade.
   Aliasing remains untouched per by-design call.

## Test setup

- Sample rate: 96 kHz throughout.
- Spectral suite runtime: 2.6 s.
- Hardware build: zero warnings under `-Werror` (`cd nt && make hardware`).
- Regression suite: 5.6 s, all PASS.

## Repro

```
cmake -S . -B build && cmake --build build
cd build && ctest --output-on-failure
ls build/spectral-output/   # 46 CSV artefacts
```
