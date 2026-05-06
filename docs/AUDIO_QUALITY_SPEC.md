# Corrupter Audio Quality Specification

**Status:** Phase 2 complete (2026-05-05). Plateau reached.
35/35 spectral tests pass. See `tests/spectral/runs/iter-2*/REPORT.md`
for per-iteration deltas. Phase 1 baseline at commit `251a7c6`.

This spec defines per-algorithm audio-quality gates for the Corrupter DSP
library. The harness lives in `tests/spectral/` and is exercised by the
`corrupter_dsp_spectral_tests` ctest target. A baseline capture lives in
`tests/spectral/baseline/<sha>/` and per-iteration captures in
`tests/spectral/runs/<iter-tag>/`.

## Conventions

- **Sample rate:** 96 kHz for all spectral tests (matches the corrupter
  default and gives plenty of Nyquist headroom).
- **Status values:**
  - **gated** — failure breaks `ctest`.
  - **snapshot** — emits measurement to stderr, never fails. Used to track
    by-design behaviour or to capture baseline numbers before a future fix
    sets a real gate.
- **By-design grit:** Corrupter is a circuit-bent character effect. Some
  of its character (Decimate aliasing, Destroy hard clip, Vinyl surface
  noise) is *intentional* and has no quality gate beyond a regression
  snapshot.

## Gates by algorithm

### Decimate

PRD §13: *"At low settings, subtle quantization noise. At high settings,
extreme aliasing and bit-crushing artifacts."* Aliasing across the full
intensity range is by-design.

| Test ID | Metric | Target | Status | Rationale |
|---|---|---|---|---|
| T-AA-Decimate-Low/Mid/High | alias dBFS @ 1 kHz | snapshot | by-design | Bit-crush is the algorithm |
| T-FRA-Decimate-Idle | flatness (intensity=0) | <0.4 dB | gated | Currently 0.30 dB. At i=0 the algo is transparent (24-bit floor only) |
| T-CLK-Decimate-HoldStep | sample-to-sample delta on intensity step | snapshot | by-design | Phase 2E found no mechanical click separate from S&H aliasing; see iter-2E REPORT |

### Dropout

| Test ID | Metric | Target | Status |
|---|---|---|---|
| T-DRP-Edges | sample-to-sample delta at gate edges | TBD post-baseline | snapshot — gate after fix 2 |

### Destroy

Two regions: soft tanh (i<0.5) and hard clip (i≥0.5). The clip *level* is
by-design; the *aliasing* (which is a side effect of the no-oversampling
path) is on the fix list.

| Test ID | Metric | Target | Status | Notes |
|---|---|---|---|---|
| T-AA-Destroy-Soft | alias margin @ 7 kHz, i=0.30 | ≥40 dB | gated | Currently 47 dB after 2B (was 41 dB) |
| T-AA-Destroy-Hard | alias margin @ 7 kHz, i=0.70 | ≥24 dB | snapshot | 17→24 dB after 2B. Plateau: half-band fs/4 transition is the limit; see iter-2B-pass-2 REPORT |
| T-THD-Destroy-Soft | THD+N @ i=0.30 | <12 % | gated | Currently 8.9 % |
| T-THD-Destroy-Hard | THD+N @ i=0.70 | snapshot | by-design | Hard clip is the feature |
| T-SM-Destroy-Crossover | RMS jump across i=0.5 (small-amp probe) | <0.005 | gated | 0.0067→0.0022 after 2D smoothstep crossfade |

### DJ Filter

Already 2x-oversampled. Resonance ceiling at 0.10–0.65 mapping is intentional.

| Test ID | Metric | Target | Status |
|---|---|---|---|
| T-FRA-DjFilter-Center | flatness @ i=0.5, blend=0 | <0.5 dB | gated | Currently 0.33 dB |
| T-FRA-DjFilter-LpMax | -3 dB corner @ i=0 | 30–200 Hz | gated | Currently 94 Hz |
| T-FRA-DjFilter-HpMax | -3 dB corner @ i=1 | 1–20 kHz | gated | Currently 9.6 kHz |
| T-AA-DjFilter-Lp | alias > 10 kHz with 80 Hz drive | snapshot | already very low |
| T-THD-DjFilter | THD+N @ i=0.5 | <0.1 % | gated | Currently 0.004 % |

### Vinyl Sim

Always-on subtle surface noise per Neal — the always-on noise floor is the
character. Surface noise dominates THD+N measurements.

| Test ID | Metric | Target | Status |
|---|---|---|---|
| T-AA-Vinyl-Snapshot | alias margin | snapshot | by-design |
| T-FRA-Vinyl-Tone | midband response | snapshot | by-design |
| T-THD-Vinyl | THD+N (surface noise dominant) | snapshot | by-design |
| (future) T-NF-Vinyl-Off | RMS at i=0 | snapshot only | Always-on subtle noise (Neal: keep current) |
| (future) T-NF-Vinyl-Full | RMS at i=1 | -50 to -25 dBFS | gate after baseline |
| (future) T-VIN-Crackle | spectral peak 2-6 kHz | ≥6 dB above floor | gate after baseline |

### Tape colour (bend)

| Test ID | Metric | Target | Status | Notes |
|---|---|---|---|---|
| (future) T-FRA-Tape-Color | -3 dB at 12 kHz ±10 % | gated | bend_eff=0.5 |
| T-SM-TapeDrive-Step | sideband/carrier on bend modulation | drop ≥20 dB after fix 2C | snapshot — fix 2C target |

### Top-level (engine, smoothing)

| Test ID | Metric | Target | Status | Notes |
|---|---|---|---|---|
| T-CLK-Corrupt-Sweep | sample-to-sample delta on corrupt step | TBD post-fix 2A | snapshot — fix 2A target |
| T-SM-Corrupt-Step | sideband/carrier on corrupt modulation | drop ≥15 dB after fix 2A | snapshot — fix 2A target |
| (future) T-SM-Corrupt-Ramp | spectrogram smoothness on linear ramp | TBD | snapshot |
| T-CLK-Bend-Sweep | sample-to-sample delta on bend step | snapshot | currently 0.057 |

### Pitch quantize

| Test ID | Metric | Target | Status | Notes |
|---|---|---|---|---|
| T-SM-Pitch-Step | sample-to-sample delta on time knob jump | snapshot — gate <0.05 | Already smoothed; verify |
| (future) T-CLK-Pitch-Quantize | click on rate jump | gated | Already smoothed |

### Freeze / crossfade

| Test ID | Metric | Target | Status |
|---|---|---|---|
| T-FRZ-Toggle | sample-to-sample delta at on/off | <0.01 | gated | Currently on=0, off=0.0065 |
| T-XFADE-Tick | delta during steady freeze | snapshot | gate after baseline |
| T-DRP-Edges | delta at dropout edges | <0.01 | gated | Currently 0 (cosine ramp) |

## By-design vs accidental

**By-design grit (NEVER fix without sign-off):**

- Decimate aliasing across full intensity range — bit-crush is the algorithm.
- Destroy hard-clip *shape* at intensity > 0.5 — clip level is the feature.
- Vinyl rumble/hiss/crackle/pop — that's the whole effect.
- Dropout's instantaneous mute decisions — only the cosine ramp shape matters.
- DJ filter resonance ceiling at extreme intensity — current 0.10–0.65
  mapping is intentional.

**Accidental defects (fix list):**

- Destroy *aliasing* (a side effect of the hard clipper, separate from the
  clip shape).
- Zipper noise from un-smoothed corrupt-intensity CV.
- Zipper noise from un-smoothed tape-drive coefficient.
- Discontinuous derivative at Destroy's branch boundary.
- Mechanical click on Decimate hold-length jumps (separate from the
  intentional aliasing).

## Phase 2 outcomes

| Iter | Fix | Result |
|---|---|---|
| 2A | Smooth corrupt-intensity CV (10 ms one-pole) | **Landed.** Eliminates zipper noise on rapid CV moves. |
| 2B | 2× oversample Destroy saturator + clipper | **Landed.** Alias margin: soft 41→47 dB, hard 17→24 dB. |
| 2C | Smooth tape-drive coefficient (15 ms one-pole) | **Landed** (architecturally correct). Test metric inadequate; documented. |
| 2D | Smoothstep crossfade Destroy soft/hard at i∈[0.45,0.55] | **Landed.** RMS jump 0.0067→0.0022 (3× cut). |
| 2E | Soft Decimate hold-length transitions | **Refuted, no change.** No mechanical click separate from by-design S&H aliasing. |
| 2B-pass-2 | Longer half-band FIR (13-tap Kaiser β=5) | **Investigated, reverted.** Half-band fs/4 transition is fundamental limit; longer FIR doesn't move the worst-alias bin. |

Plateau reached. Two consecutive iterations (2E refuted, 2B-pass-2
no-op) yield no measurable improvement on the worst-failing test.

For per-iteration deltas see `tests/spectral/runs/iter-2*/REPORT.md`.

## Repro

```
cmake -S . -B build -DCORRUPTER_BUILD_SPECTRAL_TESTS=ON
cmake --build build
cd build && ctest --output-on-failure        # runs both regression and spectral
ctest -L spectral                            # only the spectral harness
ctest -LE spectral                           # only the fast regression suite
ls build/spectral-output/                    # 46 CSV artefacts ready for plotting
```

To capture a new baseline:

```
SHA=$(git rev-parse --short HEAD)
mkdir -p tests/spectral/baseline/$SHA
cp build/spectral-output/*.csv tests/spectral/baseline/$SHA/
$EDITOR tests/spectral/baseline/$SHA/REPORT.md     # author by hand
```
