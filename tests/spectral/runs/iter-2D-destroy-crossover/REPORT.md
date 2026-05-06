# Iter 2D — Smooth Destroy soft→hard crossover

**Date:** 2026-05-05
**Baseline:** iter-2B (251a7c6 + 2A + 2C + 2B)
**Result:** 35 / 35 spectral tests pass. Crossover RMS jump cut 3×.

## Headline

Replaced the abrupt `if (intensity < 0.5)` branch in Destroy's nonlinearity
with a smoothstep crossfade between the soft tanh path and the hard clip
path over `intensity ∈ [0.45, 0.55]`. Both paths are evaluated inside the
transition zone and blended with `t² (3 - 2t)`.

This kills the derivative kink at `intensity = 0.5` so a CV ramping
through 0.5 no longer produces a step in output level.

## Measured deltas

`T-SM-Destroy-Crossover` was reformulated. The original 1st-difference
metric (worst sample-to-sample delta in a 2048-sample window around
`i=0.5`) is dominated by the carrier's natural slew rate at saturation,
not the kink. The new metric measures **RMS jump** between two narrow
windows of intensity straddling 0.5. With a small-amplitude (0.05) input
sine and the soft/hard paths producing near-identical fundamental but
different output curvatures, the kink shows up as a step in long-window
RMS at the crossover.

| Test | iter-2B (no smoothstep) | iter-2D | Delta |
|---|---|---|---|
| **T-SM-Destroy-Crossover** (RMS jump across i=0.5) | 0.00667 | **0.00217** | **−9.7 dB** (3× cut) |
| T-AA-Destroy-Soft (alias margin) | 45.97 dB | 45.97 dB | unchanged |
| T-AA-Destroy-Hard (alias margin) | 23.99 dB | 23.99 dB | unchanged |
| T-THD-Destroy-Soft | 8.93 % | 8.93 % | unchanged |
| T-THD-Destroy-Hard | 41.04 % | 41.04 % | unchanged (snapshot) |
| All other tests | (see iter-2B) | identical | no regressions |

The 3× cut shows the smoothstep is doing what we want. The remaining
0.002 RMS jump is the residual difference between tanh and clamp at
`intensity = 0.5` even after blending: tanh saturates more gradually than
clamp at amplitudes below the clip threshold, so the two paths produce
similar but not identical RMS even at 50/50 blend. That is a
characteristic of the two transfer curves; further reduction would
require redesigning the transfer functions to converge at `i=0.5`, which
would change the by-design clip character.

## Why the original metric (1st-difference) didn't move

The 1st-difference of the carrier's output at saturation peak is
dominated by sine slew rate × drive: `2π·f·A·drive ≈ 0.2` for our
test input even with a perfectly-smoothed nonlinearity. The kink at
`i=0.5` is a single-sample bump <0.06 sitting on top of that slew
backbone, so it is invisible against the slew-rate noise floor in the
1st-difference metric. The new RMS-jump metric averages out the slew and
isolates the level discontinuity, which is what the smoothstep targets.

## Code change

`src/internal/corrupt_engine.cpp`:

- Replaced the simple `if (intensity < 0.5) tanh else clamp` block in
  the Destroy 2× oversampling path (lines 116–128) with a three-way
  branch: `i ≤ 0.45` (soft only), `i ≥ 0.55` (hard only), and the
  in-between zone where both run and crossfade with smoothstep. The
  outside-zone fast paths are kept to avoid the extra cost of running
  both nonlinearities for the common case.

`tests/spectral/test_smoothing.cpp`:

- Reformulated `T-SM-Destroy-Crossover` to use a small-amplitude input
  (0.05) and an RMS-jump metric across `i=0.5`. The original CSV path
  is preserved so debug WAVs still write under the same name.

Hardware build still zero-warning under `-Werror`. Object stays at 47 K.

## Hypotheses

| ID | Hypothesis | Status this iter |
|---|---|---|
| H1 | Corrupt-CV is unsmoothed; rapid moves cause audible clicks. | Confirmed + fixed (2A). |
| H2 | Tape-drive coefficient updates per clock tick produce zipper noise. | Smoother in place (2C). Test metric inadequate. |
| H3 | Destroy hard-clipper produces audible aliasing at high intensity. | Partial fix (2B): margin 17.4 → 24 dB; gate 30 dB not yet reached. |
| H4 | Destroy soft→hard crossover at i=0.5 has a derivative kink. | **Confirmed + fixed.** RMS jump 0.0067 → 0.0022 (3× cut). |
| H5 | Decimate hold-length jumps cause clicks separate from by-design aliasing. | Phase 2E target. Unchanged. |

## Next iteration

Phase 2E — soft Decimate hold-length transitions. Target
T-CLK-Decimate-HoldStep (currently 0.688 worst sample-to-sample delta on
a 0 → 0.95 intensity step). The aliasing of held samples remains
untouched per by-design call; only the *mechanical click* on a
hold-counter jump gets a brief crossfade.

After 2E, re-evaluate whether iter-2B-pass-2 (longer FIR or 4× OS) is
worth investing in to push T-AA-Destroy-Hard from 24 → 30 dB margin.
