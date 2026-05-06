# Iterative Audio Quality Process for Corrupter

## Context

The corrupter DSP library has 5 corruption algorithms (Decimate, Dropout, Destroy, DJ Filter, Vinyl Sim) plus tape color and pitch quantization. Current testing covers determinism, finiteness, and parameter-mapping correctness, but provides no spectral or audio-quality measurement. Exploration uncovered concrete quality risks:

- **Unsmoothed corrupt-intensity CV** in `engine.cpp:824-826` — rapid knob/CV moves cause zipper noise.
- **Destroy aliasing at high intensity** (`corrupt_engine.cpp:93-100`) — hard clipper has no oversampling.
- **Tape drive coefficient unsmoothed** (`engine.cpp:394`) — discrete steps at every clock tick.
- **Discontinuous derivative** at Destroy's soft→hard branch boundary (`intensity = 0.5`).
- **Decimate hold-length steps** can click when intensity jumps.

The goal is to add a measurement-driven iterative improvement workflow modeled on:

- **`nosuch_texture/tests/spectral/`** — Catch2-based spectral suite with FFT, FRA, THD+N helpers, per-mode numerical targets, baseline/runs CSV captures, and `REPORT.md` files documenting each iteration (hypothesis → measurement → fix → revalidation).
- **NTUmbrella `test_harness/`** — SHA-256 golden WAV regression for the NT plugin lifecycle (deferred to Phase 4).

Outcome: a `corrupter_dsp_spectral_tests` CTest target, a `docs/AUDIO_QUALITY_SPEC.md` describing per-algorithm gates with explicit by-design carve-outs (the "circuit-bent" character is intentional grit, not a bug), and a documented iterative workflow that runs until gates plateau.

## Phased Approach

### Phase 1 — Build the harness and capture baseline (no DSP changes)

Stand the harness up, run it, document what's there. The DSP library is untouched in this phase so the baseline is a faithful snapshot of current behavior.

### Phase 2 — Targeted fixes, one at a time

Implement each fix candidate in isolation, re-run the harness, capture a new run directory under `tests/spectral/runs/<iter-tag>/`, and write a `REPORT.md` showing measured deltas. Order is by audibility-vs-difficulty:

1. **2A — Smooth corrupt intensity CV** (5–10 ms one-pole). Eliminates zipper noise; cheapest high-impact fix.
2. **2B — 2× oversample Destroy's saturator/clipper.** Mirrors DJ filter's existing 2× pattern; uses a polyphase half-band FIR around the nonlinearity.
3. **2C — Smooth tape drive coefficient** (~10 ms one-pole) at `engine.cpp:394, 441, 942`.
4. **2D — Smooth Destroy soft→hard crossover** (`intensity ∈ [0.45, 0.55]`) if cheap.
5. **2E — Smooth Decimate hold-length transitions** if T-CLK-Decimate-HoldStep fails. Aliasing is left untouched (by-design per PRD); only the *mechanical click* on hold-counter jumps gets a brief crossfade.

### Phase 3 — Iterate until plateau

Re-run the spec; tighten gates that are now over-comfortable; document remaining failures as engineering trade-offs. Stop when two consecutive iterations yield no measurable improvement on the worst-failing test.

### Phase 4 — Plugin-level golden hash (optional, deferred)

NTUmbrella-style WAV-hash regression for the NT plugin lifecycle. Requires `nt_api_stub.cpp` and a `PluginInstance` driver. Independent of the spectral harness; do this after Phase 3 stabilizes.

## File Structure

### New files

- `docs/AUDIO_QUALITY_SPEC.md` — per-algorithm gates, rationale, by-design vs accidental.
- `tests/spectral/CMakeLists.txt` — declares the executable, registers with `ctest`.
- `tests/spectral/spectral_helpers.h/.cpp` — port of `nosuch_texture/tests/spectral/spectral_helpers.cpp`. Rename namespace `beads_spec` → `corrupter_spec`. Reuse verbatim: `GenSine`, `GenLogSweep`, `GenImpulse`, `GenWhiteNoise` (xorshift32), `Fft` (radix-2 Cooley-Tukey), `MagAtHz`, `PowerInBand`, `ThdPlusNPct`, `SteppedSineFra`, `IrFra`, `Rms`, `PeakAbs`, `WriteFraCsv`, `WriteSamplesCsv`, `OutputPath`. Add corrupter-specific helpers: `MagToDbfs`, `PeakInBand`, `ClickEnergy` (1st-difference RMS), `MeasureZipperNoise`.
- `tests/spectral/test_runner.cpp` — `main()` enumerating all `bool TestX()` functions, prints summary, exits non-zero on any fail. Matches the hand-rolled style already in `examples/regression_tests.cpp`.
- `tests/spectral/test_smoke.cpp` — sanity-checks the helpers (sine amplitude, FFT bin accuracy, FRA flatness on identity).
- `tests/spectral/test_aliasing.cpp` — `T-AA-Decimate-*` (snapshot only, by-design), `T-AA-Destroy-*` (gated, fix target), `T-AA-DjFilter-*` (snapshot, already good), `T-AA-Vinyl-*` (snapshot).
- `tests/spectral/test_fra_per_module.cpp` — `T-FRA-Tape-Color`, `T-FRA-Vinyl-LP`, `T-FRA-DjFilter-{Center,LpMax,HpMax}`, `T-FRA-Decimate-Idle`.
- `tests/spectral/test_thd.cpp` — `T-THD-Destroy-{Soft,Hard}`, `T-THD-DjFilter`, `T-THD-Vinyl`.
- `tests/spectral/test_clicks.cpp` — `T-CLK-Corrupt-Sweep`, `T-CLK-TapeDrive-Sweep`, `T-CLK-Bend-Sweep`, `T-CLK-Decimate-HoldStep`.
- `tests/spectral/test_freeze_transitions.cpp` — `T-FRZ-{On,Off}`, `T-XFADE-Tick`, `T-DRP-Edges`.
- `tests/spectral/test_smoothing.cpp` — `T-SM-Corrupt-Step`, `T-SM-TapeDrive-Step`, `T-SM-Pitch-Step`, `T-SM-Destroy-Crossover`.
- `tests/spectral/baseline/<sha>/` — first capture (CSVs + REPORT.md).
- `tests/spectral/runs/<iter-tag>/` — per-iteration capture.

### Modifications

- `CMakeLists.txt` — add `option(CORRUPTER_BUILD_SPECTRAL_TESTS ...)` and `add_subdirectory(tests/spectral)` guarded by the option AND `NOT CORRUPTER_ENABLE_COVERAGE`.
- `docs/verification.md` (if it exists; otherwise add to README) — document how to run spectral tests and regenerate baselines.

### Untouched

- `nt/Makefile` — hardware build excludes test code; spectral tests only run on host.
- DSP library sources — Phase 1 makes zero DSP changes so the baseline is faithful.

### Phase 2 modifications (per-iteration)

- **2A**: `src/engine.cpp` — add `OnePole corrupt_smoother` to `Impl`; init in `ResetPlayback`; smooth `corrupt_eff` at lines 824-826 / 948-953.
- **2B**: `src/internal/corrupt_engine.cpp:93-100` — wrap saturator + clipper in 2× oversampling block. Use polyphase half-band FIR (4–8 taps); the linear-interpolation pattern at `corrupt_engine.cpp:124-144` is fine for filtering but inadequate around hard nonlinearities.
- **2C**: `src/engine.cpp:394, 441, 942` — add `OnePole tape_drive_smoother` to `PlaybackChannelState`; smooth before applying.
- **2D**: `src/internal/corrupt_engine.cpp:93-100` — replace the hard branch at `intensity == 0.5` with a `smoothstep(0.45, 0.55, intensity)` crossfade between soft and hard outputs.
- **2E**: `src/internal/corrupt_engine.cpp:38-49` — when `hold` length changes by more than ~1.5×, brief crossfade on the `held` value instead of an abrupt switch. Aliasing of held samples remains untouched.

## Test Framework Choice

**Hand-rolled, matching `examples/regression_tests.cpp`'s existing `bool TestX()` + summary-table pattern.** Rationale: zero new dependencies, no FetchContent, parity with the project's current style. The `nosuch_texture` Catch2 helpers are framework-agnostic (only the `TEST_CASE`/`REQUIRE` shells touch Catch2) so the helpers port cleanly. Adopt nosuch_texture's `T-AA-*` / `T-FRA-*` / `T-THD-*` / `T-NF-*` / `T-IR-*` naming convention for test functions.

## Critical Files

- `/Users/nealsanche/nosuch/corrupter/CMakeLists.txt`
- `/Users/nealsanche/nosuch/corrupter/src/engine.cpp` (lines 24-42 OnePole, 394, 441, 824-826, 877, 942, 948-953)
- `/Users/nealsanche/nosuch/corrupter/src/internal/corrupt_engine.cpp` (lines 38-49, 93-100, 104-147, 149-199)
- `/Users/nealsanche/nosuch/corrupter/examples/regression_tests.cpp` (existing test pattern; lines 91-97 has a Goertzel-style ToneAmplitude worth promoting to spectral_helpers)
- `/Users/nealsanche/nosuch/nosuch_texture/tests/spectral/spectral_helpers.h/.cpp` (helpers to port)
- `/Users/nealsanche/nosuch/nosuch_texture/tests/spectral/test_aliasing.cpp` (alias-test pattern to mirror)
- `/Users/nealsanche/nosuch/nosuch_texture/tests/spectral/baseline/51cd906/REPORT.md` (REPORT.md format to mirror)

## Reusable Helpers from nosuch_texture

Verbatim (rename namespace only):

- `StandardFraGrid()` — 60 log-spaced points 20 Hz – 22050 Hz.
- `GenSine`, `GenLogSweep`, `GenImpulse`, `GenWhiteNoise`.
- `Fft` — iterative radix-2 Cooley-Tukey, bit-reversed permutation.
- `MagAtHz`, `PowerInBand`, `Rms`, `PeakAbs`.
- `SteppedSineFra`, `IrFra`.
- `ThdPlusNPct`.
- `WriteFraCsv`, `WriteSamplesCsv`, `OutputPath`.

New (corrupter-specific):

- `MagToDbfs` (port from `nosuch_texture/tests/spectral/test_aliasing.cpp` lines 53-57).
- `PeakInBand` — find worst alias bin in a frequency window.
- `ClickEnergy(samples, len)` — 1st-difference RMS over a window for transient detection.
- `MeasureZipperNoise` — spectral energy in target sidebands relative to passband mean.

## Spec Document Skeleton (`docs/AUDIO_QUALITY_SPEC.md`)

Per-algorithm sections following the nosuch_texture format:

| Algorithm | Test ID | Metric | Target | Status | Notes |
|---|---|---|---|---|---|
| Decimate | T-AA-Decimate-* | alias dBFS | snapshot only | by-design | PRD: "extreme aliasing... is the feature" at high intensity |
| Decimate | T-FRA-Decimate-Idle | flatness | < 0.1 dB | gated | At intensity=0 |
| Decimate | T-CLK-Decimate-HoldStep | click energy | TBD post-baseline | gated | Hold-length jumps |
| Dropout | T-DRP-Edges | click energy | TBD | gated | Cosine ramp shape |
| Dropout | T-DRP-Hold | DC offset during mute | == 0 | gated | Drift sentinel |
| Destroy | T-AA-Destroy-Hard | alias dBFS | ≥ 30 dB below fundamental at I=0.6 | **fix 2B** | No oversampling |
| Destroy | T-THD-Destroy-Soft | THD+N | < 5 % | gated | Tanh region |
| Destroy | T-THD-Destroy-Hard | THD+N | snapshot | by-design | Hard clip is feature |
| Destroy | T-SM-Destroy-Crossover | click energy | TBD | gated | Soft→hard at I=0.5 |
| DJ Filter | T-FRA-DjFilter-Center | flat ±0.5 dB | gated | At I=0.5, blend=0 |
| DJ Filter | T-FRA-DjFilter-LpMax | -3 dB at ~70 Hz ±10 % | gated | I=0 |
| DJ Filter | T-FRA-DjFilter-HpMax | -3 dB at ~18 kHz ±10 % | gated | I=1 |
| DJ Filter | T-AA-DjFilter | alias < -50 dBFS above 10 kHz | gated | Already 2x OS |
| Vinyl | T-NF-Vinyl-Off | RMS at I=0 | snapshot only | by-design | Always-on subtle surface noise (Neal: keep current) |
| Vinyl | T-NF-Vinyl-Full | RMS at I=1 | -50 to -25 dBFS | gated | Surface noise feature |
| Vinyl | T-VIN-Crackle | spectral peak 2-6 kHz | ≥ 6 dB above floor | gated | Crackle character |
| Tape | T-FRA-Tape-Color | -3 dB at 12 kHz ±10 % | gated | bend_eff=0.5 |
| Tape | T-SM-TapeDrive-Step | click energy | TBD | **fix 2C** | Per-tick step |
| Top-level | T-SM-Corrupt-Step | click + zipper sidebands | TBD | **fix 2A** | Zipper noise |
| Top-level | T-SM-Corrupt-Ramp | spectrogram smoothness | TBD | **fix 2A** | Ramp test |
| Pitch | T-CLK-Pitch-Quantize | click on rate jump | gated | Already smoothed |
| Freeze | T-FRZ-{On,Off} | click energy | TBD | gated | 128-sample crossfade |
| Tick | T-XFADE-Tick | click energy | gated | Zero-crossing exit |

Numbers marked TBD become "achieved + 1 dB margin" after the Phase 1 baseline.

## By-Design vs Accidental

**By-design grit (NEVER fix without sign-off):**

- Decimate aliasing across full intensity range — bit-crush is the algorithm.
- Destroy hard-clip *shape* at intensity > 0.5 — clip level is the feature.
- Vinyl rumble/hiss/crackle/pop — that's the whole effect.
- Dropout's instantaneous mute decisions — only the cosine ramp shape matters.
- DJ filter resonance ceiling at extreme intensity — current 0.10–0.65 mapping is intentional.

**Accidental defects (fix list):**

- Destroy *aliasing* (a side effect of the hard clipper, separate from the clip shape).
- Zipper noise from un-smoothed CV.
- Discontinuous derivative at Destroy's branch boundary.
- Tape drive per-tick steps.

## Iteration Documentation Format

Each `tests/spectral/runs/<iter-tag>/REPORT.md` follows the `nosuch_texture/tests/spectral/baseline/51cd906/REPORT.md` template:

1. **Headline** — N/M tests passing, deltas vs prior iteration.
2. **Hypotheses table** — H1, H2, … each with status (confirmed / refuted / partial).
3. **Failure detail** — per-test measured value, target, root cause.
4. **Fix actions** — what was changed in this iteration, file/line.
5. **Spec defects discovered** — gates that turned out to be unrealistic.
6. **Next iteration** — recommended priority for next round.

## Verification

- `cmake -S . -B build && cmake --build build` — must succeed with zero warnings.
- `cd build && ctest --output-on-failure` — both `corrupter_dsp_regression` and `corrupter_dsp_spectral_tests` pass.
- `ctest -L spectral` runs only the slow harness; `ctest -LE spectral` runs the fast suite.
- `cd nt && make hardware` — must continue producing zero warnings (`-Werror`). Spectral tests are host-only; nothing in `nt/` changes during Phase 1.
- After each Phase-2 fix:
  - Re-run `ctest`; verify regression tests still pass (some may need expected-hash updates if smoothing changes deterministic output — flag before landing).
  - Capture `cp -r build/spectral-output tests/spectral/runs/iter-2A-corrupt-smoothing/` and author the iteration's REPORT.md.
  - Sanity-check by listening to a generated WAV (the spectral suite emits `*.wav` debug captures alongside CSVs for ear verification).

## Decisions (resolved with Neal)

- **Vinyl Sim at `intensity=0`**: keep current always-on subtle surface noise. T-NF-Vinyl-Off becomes a snapshot test (capture current RMS) rather than a "must be silent" gate. Documented as by-design.
- **Decimate aliasing**: fully by-design per `corrupter-prd.md:194-196` — *"At low settings, subtle quantization noise. At high settings, extreme aliasing and bit-crushing artifacts reminiscent of playing audio at the wrong sample rate or bit depth."* T-AA-Decimate-* tests are snapshot-only, no gates. **Fix 2D is dropped.** What remains for Decimate: T-FRA-Decimate-Idle (flatness at intensity=0 confirms "subtle" quantization noise floor), and T-CLK-Decimate-HoldStep (clicks on hold-length jumps are *not* by-design — they're a separate mechanical defect from the intentional aliasing).
- **Test framework**: hand-rolled, matching `examples/regression_tests.cpp`'s `bool TestX()` + summary-table style. No Catch2 / FetchContent.
- **Phase 4 (NTUmbrella golden WAV hash)**: deferred until after Phase 3 plateaus, to avoid hash-regen churn during Phase 2 iterations.
