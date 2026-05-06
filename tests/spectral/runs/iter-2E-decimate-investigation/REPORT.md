---
# Iter 2E — Decimate hold-length transition investigation

**Date:** 2026-05-05
**Baseline:** iter-2D (251a7c6 + 2A + 2B + 2C + 2D)
**Result:** No code change. 35/35 spectral tests still pass.
**Conclusion:** No mechanical click separate from by-design aliasing was found.

## Headline

Phase 2E was scoped to "smooth Decimate hold-length transitions if
T-CLK-Decimate-HoldStep fails". After re-examining the test data with
the corrupt-CV smoother (Phase 2A) already in place upstream of
`ProcessCorruptSample`, the only candidate for a fixable click is the
intensity-step boundary itself. Direct measurement shows that boundary
delta is **already below the natural sine slew rate** at the test
frequency. The dominant deltas come from **by-design** sample-and-hold
phase mismatches at hold-cycle boundaries, which the PRD explicitly
calls "the feature".

No code change.

## Measured deltas

`build/spectral-output/clk_decimate_holdstep.csv` — 1 kHz, A=0.5,
fs=96 kHz, intensity steps from 0.0 → 0.95 at sample 4096.

| Location | Delta | Source |
|---|---:|---|
| **Worst delta in entire trace** | 0.7500 | Sample 6637: held value flips −0.375 → +0.375 (S&H grabbing opposite sine phase). **By-design aliasing.** |
| **Intensity-step boundary** (sample 4095 → 4096) | 0.0218 | Quantizer switches 24-bit → 5-bit, but on a sine value where the two grids happen to round close to each other. |
| **Natural sine slew rate** (1 kHz, A=0.5, fs=96k) | 0.0327 | `2π · f · A / fs` |

The boundary click (0.0218) is **smaller than the natural slew rate of
the input signal** (0.0327). It is acoustically inaudible — there is no
mechanical defect to fix here.

## Why the original concern is moot

The plan's worry was "intensity jumps cause clicks separate from
by-design aliasing". Two things in the existing code already neutralize
that concern:

1. **Phase 2A corrupt-CV smoother** (10 ms one-pole, `engine.cpp`).
   In real use, intensity *cannot* step instantaneously through
   `ProcessCorruptSample`'s eyes — the smoother spreads any knob/CV
   move over ~10 ms. The test harness here calls
   `ProcessCorruptSample` directly, bypassing the smoother. So the
   test is more aggressive than any real input path.

2. **Decimate's hold-counter logic captures the new sample on the
   *next* boundary**, not on the intensity change itself. The
   intensity transition only changes future hold lengths and bit
   depths; it does not force a re-quantize at the transition sample.
   So there is no inherent "step on transition" mechanism to clean up.

## Worst-delta location is by-design

The 0.75 worst-delta occurs at the boundary between two consecutive
hold cycles (sample 6637, held value flips −0.375 → +0.375). At
intensity=0.95, hold length = 121 samples ≈ 1.26 ms ≈ ~800 Hz hold
rate. The 1 kHz input slides through ~38° per hold cycle, so each new
held sample lands at a randomly different phase of the sine. The
amplitude difference between consecutive holds *is* the
sample-and-hold aliasing, and the PRD explicitly calls this the
feature:

> *"At low settings, subtle quantization noise. At high settings,
> extreme aliasing and bit-crushing artifacts reminiscent of playing
> audio at the wrong sample rate or bit depth."* — `corrupter-prd.md`

A crossfade between consecutive held values would *destroy* the
sample-and-hold character — it would low-pass-filter the bit-crush.
That is not the right fix for a side effect we want to keep.

## What about hold-length 1 → 121 ratio (>1.5×)?

The plan suggested "when `hold` length changes by more than ~1.5×,
brief crossfade on the `held` value". Re-reading the post-2A code path:
the only time `hold` length changes is when `intensity` changes, and
intensity is now smoothed. So `hold` ramps gradually 1 → 2 → 3 → … as
intensity ramps 0 → 0.95, never producing a >1.5× single-step jump.
The original concern was based on the unsmoothed-intensity
pre-Phase-2A code path. After 2A, the concern is moot.

## Decision

Conclude Phase 2E without a code change. The investigation confirmed
that:

- No mechanical click separate from by-design aliasing is present in
  the Decimate path.
- Phase 2A's corrupt-CV smoother already handles the only realistic
  source of an "intensity jump click" in this algorithm.
- Forcing a crossfade between consecutive held values would destroy
  the by-design sample-and-hold character.

`T-CLK-Decimate-HoldStep` remains a snapshot test. Its 0.75 worst
delta is documented as a by-design measurement, not a defect.

## Hypotheses

| ID | Hypothesis | Status this iter |
|---|---|---|
| H1 | Corrupt-CV is unsmoothed; rapid moves cause audible clicks. | Confirmed + fixed (2A). |
| H2 | Tape-drive coefficient updates per clock tick produce zipper noise. | Smoother in place (2C). Test metric inadequate, documented. |
| H3 | Destroy hard-clipper produces audible aliasing at high intensity. | Partial fix (2B): margin 17.4 → 24 dB; gate 30 dB not yet reached. |
| H4 | Destroy soft→hard crossover at i=0.5 has a derivative kink. | Confirmed + fixed (2D). RMS jump 0.0067 → 0.0022 (3× cut). |
| H5 | Decimate hold-length jumps cause clicks separate from by-design aliasing. | **Refuted.** Boundary delta (0.022) < natural slew rate (0.033). Worst-delta locations are by-design S&H phase mismatches. |

## Next iteration

iter-2B-pass-2 — extend Destroy's half-band FIR (current 9-tap simple
half-band) to a longer Kaiser-windowed half-band (13 or 17 tap) to
push T-AA-Destroy-Hard alias margin from 24 dB → ≥30 dB. The 9-tap
filter's stopband floor (~24 dB) is what we are hitting now; a longer
filter with a sharper transition band trades arithmetic and 4 more
samples of state for the remaining 6 dB.

Alternative: cascade two 2× stages (4× total OS) using the same 9-tap
filter. Pushes alias products of 7 kHz hard-clip harmonics out of the
audible band entirely. More samples of latency but conceptually
simpler.

Pick longer FIR first — it is a smaller code change and keeps the
existing 2× polyphase structure.
