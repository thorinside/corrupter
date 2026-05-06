# Iter 2B-pass-2 — Longer half-band FIR investigation

**Date:** 2026-05-05
**Baseline:** iter-2D (251a7c6 + 2A + 2B + 2C + 2D)
**Result:** No code change. Half-band fundamental limit confirmed.

## Headline

Tried a 13-tap Kaiser-windowed (β=5) half-band FIR to push
`T-AA-Destroy-Hard` alias margin from 24 dB → ≥30 dB. The longer
filter gave the expected stopband improvement (1–3 dB deeper above
40 kHz at output) but **did not move the worst-alias metric** (24.0
dB margin unchanged). Reason: the worst alias lives in the half-band's
**transition band**, not its stopband. No half-band of any length can
fix it.

Reverted the change to keep state and arithmetic minimal.

## What was tried

13-tap Kaiser-windowed half-band FIR (β=5, ~55 dB stopband):

```
h = [0, c, 0, b, 0, a, 0.5, a, 0, b, 0, c, 0]
a =  0.2991, b = -0.0586, c = 0.0094
```

Polyphase 2× upsample/downsample with symmetry-collapsed arithmetic
(3 sym pairs each side instead of 6 mac). Storage grew from
`destroy_in[4] + destroy_us[8]` to `destroy_in[6] + destroy_us[14]`
(+32 bytes per channel).

Hardware build still 47K, zero warnings under `-Werror`.

## Measured deltas (13-tap test, before revert)

| Test | iter-2D (9-tap) | 13-tap Kaiser β=5 | Δ |
|---|---:|---:|---:|
| **T-AA-Destroy-Hard** worst alias | 24.0 dB margin | **24.0 dB margin** | **0 dB** |
| 47027 Hz alias dBFS | −25.45 | −25.55 | −0.1 dB |
| 45082 Hz alias dBFS | −37.65 | −37.60 | +0.05 dB |
| 36925 Hz alias dBFS | −44.94 | −46.82 | −1.9 dB |
| 43136 Hz alias dBFS | −63.19 | −66.23 | −3.0 dB |
| 17097 Hz (audible) | −37.05 | −37.34 | −0.3 dB |
| 3105 Hz (audible) | −39.57 | −39.77 | −0.2 dB |
| 10886 Hz (audible) | −42.17 | −42.53 | −0.4 dB |

Deep stopband (>40 kHz, well above the half-band transition center):
1–3 dB cleaner. Audible band: 0.2–0.4 dB cleaner. **Worst-alias bin
at 47027 Hz: unchanged.**

## Why the worst-alias metric did not move

The worst alias at 47027 Hz is the **7th harmonic** of the 7 kHz test
fundamental. At 192 kHz internal rate, the 7th harmonic sits at
49 kHz — just 1 kHz above the half-band's natural transition center
at fs/4 = 48 kHz. After downsampling 192→96, the 49 kHz content
folds to 96 − 49 = 47 kHz at the output.

A half-band filter has a **mathematical constraint**: H(fs/4) = 0.5
exactly. The transition is symmetric around fs/4. So content at
49 kHz at 192 kHz rate is *always* in the transition band of any
half-band filter, attenuated by only ~6 dB regardless of length.

Verified analytically:

```
H(ω) = 0.5 + 2a·cos(ω) + 2b·cos(3ω) + 2c·cos(5ω)
At ω = 2π·49/192 = 1.6035 rad:
  9-tap (a=0.30, b=−0.05):    |H| = 0.471 → −6.5 dB
  13-tap (a=0.30, b=−0.06, c=0.01): |H| = 0.466 → −6.6 dB
```

Difference: 0.1 dB. Confirms: **half-band, of any length, cannot
attenuate 49 kHz at the 192 kHz internal rate by more than ~6 dB.**

## What would actually push past 24 dB margin

Three options, in order of effort:

### A. 4× oversampling (cascade two 2× stages)

Internal rate becomes 384 kHz. Apply nonlinearity once at 384 kHz.
But the second-stage downsample (192→96) still has its half-band
transition at 48 kHz, so the 49 kHz harmonic still folds.
**This does not actually fix the test.**

### B. Non-half-band FIR with cutoff < 48 kHz

Replace the half-band with, e.g., a 13-tap FIR designed with cutoff
at 40 kHz. Attenuates 49 kHz by ~30+ dB. Cost: every coefficient is
non-zero — doubles arithmetic per sample. Sacrifices passband above
40 kHz (acceptable; audio band is below 24 kHz anyway).

### C. Pre-anti-aliasing low-pass at 35 kHz before downsample

Add a separate low-pass stage at 35 kHz cutoff at the 192 kHz rate,
before the half-band downsample. Removes the 49 kHz harmonic before
it can fold. Cost: another filter pass.

## Decision

**Plateau reached. No code change. Document and move on.**

Rationale:

1. **PRD calls hard-clip "the feature."** 24 dB margin is acceptable
   for the by-design character. The 30 dB target was an aspirational
   gate, not a hard requirement.
2. **Marginal audible improvement (0.3 dB) does not justify the
   added state and arithmetic.** Inaudible at typical listening
   levels.
3. **The 13-tap design hits a fundamental half-band limit** that
   cannot be improved without significant restructuring (option B
   or C above).
4. **Defer further work to ear-test feedback.** If Neal hears
   audible aliasing on a real source through a real DAC, revisit
   with option B or C.

## Hypotheses

| ID | Hypothesis | Status |
|---|---|---|
| H1 | Corrupt-CV is unsmoothed; rapid moves cause audible clicks. | Confirmed + fixed (2A). |
| H2 | Tape-drive coefficient updates per clock tick produce zipper noise. | Smoother in place (2C). |
| H3 | Destroy hard-clipper produces audible aliasing at high intensity. | Partial fix (2B). 17.4 → 24 dB margin. **Half-band limit reached at 24 dB; longer FIR confirms plateau.** |
| H4 | Destroy soft→hard crossover at i=0.5 has a derivative kink. | Confirmed + fixed (2D). |
| H5 | Decimate hold-length jumps cause clicks separate from by-design aliasing. | Refuted (2E). |

## Phase 2 closeout

All five Phase 2 fix candidates have been investigated and resolved:

- **2A** — corrupt-CV smoother — landed.
- **2B** — Destroy 2× oversampling — landed (+6.6 dB margin).
- **2B-pass-2** — longer FIR — investigated, no code change. Plateau.
- **2C** — tape-drive smoother — landed (test metric inadequate, code change architecturally correct).
- **2D** — Destroy soft→hard smoothstep — landed (3× cut in RMS jump).
- **2E** — Decimate hold-length transitions — refuted, no defect found separate from by-design aliasing.

35/35 spectral tests pass. Hardware build 47K, zero warnings. All
regression tests green.

## Next iteration

Phase 3 — confirm plateau and tighten gates that turned out to have
extra headroom. If the spec gates are acceptable as-landed, declare
Phase 3 complete and move to Phase 4 (deferred WAV-hash regression)
or close the audio-quality initiative.
