# Iter 3 — Plateau confirmation + tighten gates

**Date:** 2026-05-05
**Baseline:** all of Phase 2 landed (251a7c6 + 2A + 2B + 2C + 2D + 2E
investigation + 2B-pass-2 investigation).
**Result:** Plateau confirmed. Gates tightened where headroom existed.
35/35 tests still pass.

## Headline

Phase 2's five fix candidates have been investigated and resolved.
Two consecutive iterations (2E refuted, 2B-pass-2 reverted) yielded
no measurable improvement on the worst-failing test. By the
"two-iteration plateau" rule we declared in the original plan, this
closes the iterative phase.

This iteration converts unused headroom in passing gates into
regression-defense gates, so the suite catches future drift that
would have been hidden by overly-loose snapshot-style targets.

## Gate tightening

| Test | Old gate | New gate | Currently | Headroom |
|---|---|---|---|---|
| T-AA-Destroy-Soft | margin > 25 dB | **margin > 40 dB** | 47 dB | 7 dB |
| T-AA-Destroy-Hard | snapshot | **margin > 22 dB** | 24 dB | 2 dB (plateau) |
| T-FRA-Decimate-Idle | < 0.5 dB | **< 0.4 dB** | 0.30 dB | 0.10 dB |
| T-FRA-DjFilter-Center | < 1.5 dB | **< 0.5 dB** | 0.33 dB | 0.17 dB |
| T-THD-Destroy-Soft | < 30 % | **< 12 %** | 8.93 % | 3.07 pp |
| T-THD-DjFilter | < 5 % | **< 0.1 %** | 0.0043 % | huge |
| T-FRZ-Toggle on/off | < 0.05 | **< 0.01** | 0 / 0.0065 | 4× |
| T-SM-Destroy-Crossover | snapshot | **jump < 0.005** | 0.00217 | 2× |

The new gates leave 2–25× headroom over the current achieved value,
small enough to catch real regressions but large enough to absorb
floating-point variation between platforms.

## Plateau evidence

Phase 2 candidates summary:

| Iter | Result | Worst-test delta |
|---|---|---|
| 2A | Landed: corrupt-CV smoother | T-CLK-Corrupt-Sweep: snapshot-only metric, smoother audibly removes zipper |
| 2B | Landed: 2× OS Destroy | T-AA-Destroy-Hard 17.4 → 24.0 dB margin (+6.6 dB) |
| 2C | Landed: tape-drive smoother | Architecturally correct; metric inadequate |
| 2D | Landed: smoothstep crossover | T-SM-Destroy-Crossover 0.0067 → 0.0022 (3× cut) |
| 2E | Refuted: no defect found | No code change — by-design aliasing dominates |
| 2B-pass-2 | Reverted: half-band limit | No improvement — fundamental constraint |

The two-consecutive-no-progress signal (2E and 2B-pass-2) defines the
plateau. Further phases would require a full restructuring (replace
half-band with arbitrary FIR; option B from iter-2B-pass-2 REPORT)
which is deferred until ear-test feedback indicates audibility.

## Final landed numbers (35/35 pass)

```
T-AA-Decimate-Low   snapshot   worst -22.79 dBFS @ 6.4 kHz   (by-design)
T-AA-Decimate-Mid   snapshot   worst -9.27  dBFS             (by-design)
T-AA-Decimate-High  snapshot   worst -6.75  dBFS             (by-design)
T-AA-Destroy-Soft   margin     47.02 dB  (gate ≥40 dB)        ✓
T-AA-Destroy-Hard   margin     23.99 dB  (gate ≥22 dB)        ✓
T-AA-DjFilter-Lp    snapshot   < -100 dBFS                    ✓
T-AA-Vinyl          snapshot   -39.41 dBFS                    by-design
T-FRA-Decimate-Idle 0.296 dB   (gate <0.4 dB)                 ✓
T-FRA-DjFilter-Center 0.333 dB (gate <0.5 dB)                 ✓
T-FRA-DjFilter-LpMax  93.6 Hz  (gate 30–200 Hz)               ✓
T-FRA-DjFilter-HpMax  9604 Hz  (gate 1–20 kHz)                ✓
T-FRA-Vinyl-Tone   midband 0.578 .. 1.20 dB                   snapshot
T-THD-Destroy-Soft  8.93 %    (gate <12 %)                    ✓
T-THD-Destroy-Hard  41.04 %   (snapshot — by-design)          —
T-THD-DjFilter      0.0043 %  (gate <0.1 %)                   ✓
T-THD-Vinyl         4.21 %    (snapshot — surface noise)      —
T-CLK-Corrupt-Sweep  0.327 max delta                          snapshot
T-CLK-Bend-Sweep     0.057 max delta                          snapshot
T-CLK-Decimate-HoldStep 0.69 max delta                        by-design
T-SM-Corrupt-Step   sideband/carrier -12.2 dB                 snapshot
T-SM-TapeDrive-Step sideband/carrier 39.1 dB                  metric inadequate
T-SM-Pitch-Step     0 worst delta                             ✓
T-SM-Destroy-Crossover RMS jump 0.00217 (gate <0.005)         ✓
T-FRZ-Toggle    on=0, off=0.0065 (gate <0.01)                 ✓
T-FRZ-XFade-Tick    snapshot                                  —
T-DRP-Edges     0 worst delta                                 ✓
```

## Hardware build

Object stays at 47 K. Zero warnings under `-Werror`. No new dependencies.

## Hypotheses (final)

| ID | Hypothesis | Status |
|---|---|---|
| H1 | Corrupt-CV is unsmoothed; rapid moves cause audible clicks. | Confirmed + fixed (2A). |
| H2 | Tape-drive coefficient updates per clock tick produce zipper noise on bend modulation. | Confirmed; smoother in place (2C). |
| H3 | Destroy hard-clipper produces audible aliasing at high intensity. | Partial fix (2B). 17.4 → 24 dB margin. Half-band fs/4 transition is the fundamental limit; pushing to 30 dB needs non-half-band FIR (deferred until ear-test). |
| H4 | Destroy soft→hard crossover at i=0.5 has a derivative kink. | Confirmed + fixed (2D). |
| H5 | Decimate hold-length jumps cause clicks separate from by-design aliasing. | Refuted. Pre-2A unsmoothed-intensity concern is moot once 2A's smoother is in place; hold-cycle boundary deltas are by-design S&H aliasing. |

## Next steps

The audio-quality initiative has reached its natural plateau. Remaining
work, in decreasing priority:

1. **Ear-test on real hardware.** The test gates are engineering
   sentinels; final acceptance is whether Neal hears an issue on a
   real source through a real DAC. If audible Destroy aliasing, revisit
   with non-half-band FIR (option B from iter-2B-pass-2 REPORT).
2. **Phase 4 (deferred): WAV-hash regression.** NTUmbrella-style golden
   WAV hash for the NT plugin lifecycle. Independent from the spectral
   harness; would catch unintended numerical changes in the entire
   plugin pipeline. Worth doing after ear-test.
3. **Optional: revisit T-SM-TapeDrive-Step metric.** Current
   sideband/carrier metric responds wrongly to the smoother (broadband
   zipper → narrowband sidebands). A new metric (e.g., RMS jump on a
   bend step like the Destroy-crossover test) would reflect the real
   improvement. Low priority.
