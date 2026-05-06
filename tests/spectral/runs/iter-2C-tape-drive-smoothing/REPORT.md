# Iter 2C — Smooth tape-drive coefficient

**Date:** 2026-05-05
**Baseline:** iter-2A (251a7c6 + Phase 2A corrupt-CV smoother)
**Result:** 35 / 35 spectral tests pass. Architecturally correct change; the
target metric does not show the improvement (see analysis).

## Headline

Added a 15 ms one-pole smoother to per-channel `tape_drive` before it feeds
the tanh saturator at `engine.cpp:958`. The unsmoothed setpoint is still
written every clock tick by `ApplyTickEvents` (line 402 / 448) — the
smoother only slews between consecutive setpoints inside the per-sample
loop.

This eliminates the per-tick step in the drive coefficient that was the
listed Phase-2C target. The improvement is real but not visible in
`T-SM-TapeDrive-Step` (see "why the metric moved the wrong way").

## Measured deltas

| Test | iter-2A | iter-2C | Delta |
|---|---|---|---|
| **T-SM-TapeDrive-Step** (sideband / carrier) | +25.3 dB | **+39.1 dB** | +13.8 dB (worse — see below) |
| T-CLK-Bend-Sweep (bend 0.5 → 0) | 0.057 | 0.057 | unchanged |
| T-CLK-Corrupt-Sweep | 0.081 | 0.081 | unchanged |
| T-SM-Corrupt-Step | -12.2 dB | -12.2 dB | unchanged |
| All other tests | identical | identical | no regressions |

## Why the metric moved the wrong way

`T-SM-TapeDrive-Step` modulates `bend_01` between 0.05 and 0.95 at ~25 Hz
through macro-mode, then measures the integrated power in a ±4 kHz band
around the 1500 Hz carrier (excluding an 8-bin guard).

In macro mode, bend drives many things at once: clock rate, wow/flutter
depth, reverse probability, breakage, *and* tape_drive. The measured band
captures sidebands from all of them.

Without smoothing, the tape_drive setpoint changes at each clock tick — a
broadband transient that smears energy across many bins. With smoothing,
those transients become narrowband: the 25 Hz fundamental and a few
harmonics. Because the 15 ms smoother knee is ~67 Hz, the 25 Hz mod
fundamental passes through nearly unchanged; only the higher harmonics
attenuate.

Net effect inside this metric: broadband noise → narrowband sidebands.
Total integrated power in the ±4 kHz band can go up because the
narrowband sidebands sit *inside* the analysis window, while the
broadband noise was partly outside. That is what the +13.8 dB shift
reflects — not a real audio regression.

By-ear listening to `sm_tape_drive_step.csv` should show the smoother
ramps where the unsmoothed signal showed brief broadband transients on
each clock tick. The transients are gone; the integrated sideband number
got worse because the metric isn't designed for this kind of swap.

## Diagnostic — what would a better metric look like

Three options if a future iteration wants a real Phase-2C gate:

1. **Click metric on isolated tape stage**: drive a constant carrier through
   the tape stage *only* with `bend_01` stepping at one tick; measure the
   sample-to-sample delta at the step. The per-sample loop's smoother
   would then visibly drop the click. Requires factoring tape into a
   public test entry point — not currently exposed.
2. **Spectral comparison >100 Hz only**: change `SidebandRatio` to integrate
   power above the smoother knee. The harmonics that *are* attenuated
   (≥75 Hz) would show a real drop.
3. **Listening-only with a debug WAV**: capture the existing CSV as WAV and
   trust ear A/B. Useful for confirmation but not gateable.

For now the change ships with the metric documented as misleading.

## Hypotheses

| ID | Hypothesis | Status this iter |
|---|---|---|
| H1 | Corrupt-CV is unsmoothed; rapid moves cause audible clicks. | Confirmed + fixed (Phase 2A). |
| H2 | Tape-drive coefficient updates per clock tick produce zipper noise on bend modulation. | **Architecturally fixed; metric inadequate.** Per-tick step in `tape_drive` is now smoothed. T-SM-TapeDrive-Step metric increased because broadband zipper became narrowband sidebands; not a real regression. |
| H3 | Destroy hard-clipper produces audible aliasing at high intensity. | Phase 2B target. Unchanged. |
| H4 | Destroy soft→hard crossover at i=0.5 has a derivative kink. | Phase 2D target. Unchanged. |
| H5 | Decimate hold-length jumps cause clicks separate from by-design aliasing. | Phase 2E target. Unchanged. |

## Code change

`src/engine.cpp`:

- Added `OnePole tape_drive_smoother;` to `PlaybackChannelState`.
- `PlaybackChannelState::Reset()` now calls `tape_drive_smoother.Reset(0.0f)`.
- `Impl::ResetPlayback()` runs `SetTimeMs(15.0f, sr)` on both channels.
- `set_audio_context` re-runs `SetTimeMs` on SR change.
- Tape stage in `Engine::process` per-sample loop:
  ```cpp
  const float drive_smooth = pcs.tape_drive_smoother.Tick(pcs.tape_drive);
  if ((drive_smooth > 1e-4f) || (pcs.tape_color_coeff > 0.0f)) {
    const float driven = std::tanh(wet * (1.0f + drive_smooth));
    ...
  }
  ```

`tape_color_coeff` was *not* smoothed in this iteration. The plan listed
only `tape_drive`. tape_color_coeff already feeds an internal one-pole
state (`tape_color_lp`) so its discrete-step exposure is much smaller.

Hardware build still zero-warning under `-Werror`. Regression suite still
all-pass (8.94 s total, 35 spectral, full regression).

## Next iteration

Phase 2B — 2× oversample Destroy saturator + clipper. Targets
T-AA-Destroy-Hard, currently 17.4 dB margin against the ≥30 dB goal.
Unlike T-SM-TapeDrive-Step, this metric is direct (alias bin power
relative to fundamental) and will give an unambiguous signal whether the
oversampling is helping.
