# Iter 2B — 2× oversample Destroy

**Date:** 2026-05-05
**Baseline:** iter-2C (251a7c6 + Phase 2A corrupt-CV smoother + Phase 2C tape-drive smoother)
**Result:** 35 / 35 spectral tests pass. Significant alias-margin improvement.
Hard-clip target partially reached.

## Headline

Wrapped Destroy's tanh saturator (i<0.5) and hard clipper (i≥0.5) in a 2×
oversampler. Linear-phase 9-tap half-band FIR for both upsample and
downsample. The nonlinearity is evaluated twice per input sample — once
on the pass-through `u_even = x[k-2]` and once on the polyphase-interpolated
`u_odd`.

## Measured deltas

| Test | iter-2C | iter-2B | Delta |
|---|---|---|---|
| **T-AA-Destroy-Soft** (alias margin @ 7 kHz, i=0.30) | 40.9 dB | **45.97 dB** | +5.0 dB |
| **T-AA-Destroy-Hard** (alias margin @ 7 kHz, i=0.70) | 17.4 dB | **23.99 dB** | **+6.6 dB** |
| T-THD-Destroy-Soft (i=0.30) | 8.92 % | 8.93 % | unchanged |
| T-THD-Destroy-Hard (i=0.70) | 40.92 % | 41.04 % | +0.12 pp (snapshot — by-design) |
| T-SM-Destroy-Crossover (i=0.5 ramp) | 0.199 | 0.202 | unchanged (still Phase 2D target) |
| All other tests | (see iter-2C) | identical | no regressions |

## Result vs target

The `docs/AUDIO_QUALITY_SPEC.md` target for T-AA-Destroy-Hard is **≥30 dB**
margin. We reached 24 dB — a clear improvement (alias power dropped from
~17 dB to ~24 dB below the fundamental, i.e. 7 dB cleaner) but still
6 dB short of the gate.

The residual aliases live near 47 kHz at 96 kHz output rate. With a 9-tap
half-band FIR, the filter -6 dB point is at 24 kHz (oversampled-rate fs/4)
and the transition band runs out to ~36 kHz. Content above 36 kHz at the
192 kHz internal rate is attenuated by ~24 dB (typical 9-tap stopband).
That's the floor we just hit.

To clear the 30 dB gate would need either:

- **A sharper FIR** (13- or 17-tap Kaiser half-band, ~50–80 dB stopband).
  Mostly more state and arithmetic; same algorithmic shape.
- **4× oversampling** (cascade two 2× stages with the same 9-tap filter).
  Pushes alias products from 7-kHz hard-clip harmonics out of the
  16-bit-relevant band entirely. More expensive but conceptually simple.

These are saved as a possible iter-2B-pass-2 if the 24 dB result proves
inadequate by ear. The PRD calls hard-clip "the feature" and the test
gate is engineering-set rather than dictated by spec, so 24 dB may be
acceptable; left for Neal's ear test.

## Code change

`src/internal/corrupt_engine.h`:

- Added `float destroy_in[4]` and `float destroy_us[8]` to
  `CorruptChannelState`.

`src/internal/corrupt_engine.cpp`:

- Replaced lines 93–100 (the unsmoothed `tanh` / `Clamp` branch) with a
  polyphase 2× oversampling block.
- Coefficients (sum to 1; gain-corrected ×2 in upsample odd-tap pair):
  ```
  h = [0, -0.05, 0, 0.30, 0.5, 0.30, 0, -0.05, 0]
  ```
- Per-input arithmetic: 4 mac for upsample odd, 5 mac for downsample,
  2 nonlinearity evaluations. Latency ~2 samples at 96 kHz.

Hardware build still zero-warning under `-Werror`. Object grew from 46 K
to 47 K.

## Hypotheses

| ID | Hypothesis | Status this iter |
|---|---|---|
| H1 | Corrupt-CV is unsmoothed; rapid moves cause audible clicks. | Confirmed + fixed (Phase 2A). |
| H2 | Tape-drive coefficient updates per clock tick produce zipper noise on bend modulation. | Confirmed; smoother lands (Phase 2C). |
| H3 | Destroy hard-clipper produces audible aliasing at high intensity. | **Partially fixed (this iter).** Margin improved 17.4 → 24 dB. Gate target 30 dB not yet reached; longer FIR or 4× OS would close the rest. |
| H4 | Destroy soft→hard crossover at i=0.5 has a derivative kink. | Phase 2D target. Unchanged. |
| H5 | Decimate hold-length jumps cause clicks separate from by-design aliasing. | Phase 2E target. Unchanged. |

## Next iteration

Phase 2D — smooth Destroy's soft→hard crossover (`smoothstep(0.45, 0.55, i)`
between the two output paths). Cheaper than upgrading the FIR and addresses
T-SM-Destroy-Crossover (currently 0.199 worst delta in a 256-sample
window). Defer iter-2B-pass-2 (longer FIR or 4× OS) until after 2D and
2E land — at that point we'll know which remaining defects are most
audible and whether the 24 dB Destroy-Hard margin is the remaining gap
worth investing in.
