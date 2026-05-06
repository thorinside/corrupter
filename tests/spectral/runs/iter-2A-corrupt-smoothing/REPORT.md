# Iter 2A — Smooth corrupt-intensity CV

**Date:** 2026-05-05
**Baseline:** 251a7c6 (this run on top of baseline + 1 DSP change)
**Result:** 35 / 35 spectral tests pass. Targeted metrics improved.

## Headline

Added a 10 ms one-pole smoother to the per-sample `corrupt_eff` value before
it is fed into `ProcessCorruptSample`. Intentionally left the *tick-event*
path (`ApplyTickEvents`) on the unsmoothed value — those events are
parameter-set events that fire at clock ticks (already coarse), so smoothing
there would only delay parameter updates without click reduction.

## Measured deltas

| Test | Baseline | Iter 2A | Delta |
|---|---|---|---|
| **T-CLK-Corrupt-Sweep** (sample-to-sample on corrupt step 0→0.7) | 0.510 | **0.081** | **-16 dB** (6.3× quieter) |
| T-SM-Corrupt-Step (sideband / carrier, 100 Hz square mod) | -10.6 dB | **-12.2 dB** | -1.6 dB |
| T-CLK-Decimate-HoldStep | 0.688 | 0.688 | unchanged (expected — different code path) |
| T-SM-TapeDrive-Step | +25.3 dB | +25.3 dB | unchanged (expected — Phase 2C target) |
| All other tests | (see baseline) | identical | no regressions |

The click metric drop is the headline: a corrupt-CV step from 0 → 0.7 mid-
stream went from "full-scale jump on a 0.5-amp signal" to a barely audible
transient. Listening to `clk_corrupt_step.csv` should show a brief 10 ms
ramp instead of an abrupt step.

## Why the sideband ratio only moved 1.6 dB

The T-SM-Corrupt-Step modulation rate is 100 Hz (period 10 ms = same as the
smoother time constant). The smoother fully attenuates DC steps but only
partially attenuates a 100 Hz square wave — at that rate the smoothed CV
oscillates between ~25%-75% of the target levels rather than fully settling.
The instantaneous click is gone (T-CLK-Corrupt-Sweep), but the periodic
modulation still imprints sidebands. To suppress those further would need
either a longer time constant (audible parameter lag) or a lower modulation
rate in real use.

If a future iteration wants to drive the sideband metric down too, options:

- 20 ms smoother — eliminates more sidebands at <50 Hz mod rates but
  introduces audible parameter lag (corrupt knob feels sluggish on quick
  twists).
- Use a slew-rate-limited smoother (variable τ that adapts to step size) —
  fast on small changes, slow on large jumps. More complex, marginal win.

10 ms is the sweet spot for now.

## Hypotheses

| ID | Hypothesis | Status this iter |
|---|---|---|
| H1 | Corrupt-CV is unsmoothed; rapid moves cause audible clicks. | **Confirmed + fixed.** Click delta dropped from 0.51 to 0.08. |
| H2 | Tape-drive coefficient updates per clock tick produce zipper noise on bend modulation. | **Confirmed (Phase 2C target).** Unchanged this iter. |
| H3 | Destroy hard-clipper produces audible aliasing at high intensity. | **Phase 2B target.** Unchanged this iter. |
| H4 | Destroy soft→hard crossover at i=0.5 has a derivative kink. | **Phase 2D target.** Unchanged this iter. |
| H5 | Decimate hold-length jumps cause clicks separate from by-design aliasing. | **Phase 2E target.** Unchanged this iter. |

## Code change

`src/engine.cpp`:

- Added `OnePole corrupt_smoother;` to `Engine::Impl`.
- `ResetPlayback()` now calls `corrupt_smoother.SetTimeMs(10.0f, sr)` and
  resets to current `knobs.corrupt_01`.
- `set_audio_context` re-runs `SetTimeMs` when sample rate changes.
- Per-sample loop: `corrupt_smoothed = corrupt_smoother.Tick(corrupt_eff)`
  is computed alongside the unsmoothed value; `ProcessCorruptSample` uses
  the smoothed value.
- `ApplyTickEvents` (clock-tick parameter setup) keeps the unsmoothed value.

Hardware build still zero-warning under `-Werror`. Regression suite still
all-pass.

## Next iteration

Phase 2C — smooth tape-drive coefficient. Largest unaddressed zipper
finding (T-SM-TapeDrive-Step at +25.3 dB sideband-to-carrier).
