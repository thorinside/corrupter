# Corrupter DSP Verification

This project currently includes a smoke test and a regression test executable.

## Build + Run

```bash
cmake -S /Users/nealsanche/nosuch/corrupter -B /Users/nealsanche/nosuch/corrupter/build
cmake --build /Users/nealsanche/nosuch/corrupter/build
ctest --test-dir /Users/nealsanche/nosuch/corrupter/build --output-on-failure
```

## Executables

- `corrupter_dsp_smoke`
  - Basic initialization and run sanity check.
- `corrupter_dsp_regression`
  - Deterministic + behavior regression suite.
- `corrupter_dsp_benchmark`
  - Host-side throughput benchmark for profiling trends.

## Regression Coverage

- `deterministic_same_seed`
  - Same config/state/seed yields bit-identical output.
- `different_seed_changes_output`
  - Different random seeds yield different output in stochastic modes.
- `dry_bypass`
  - With `Mix=0`, output is dry passthrough.
- `freeze_momentary_autowet`
  - Momentary freeze gate engages non-dry frozen playback while held, returns dry after release.
- `external_clock_changes_timing`
  - External clock gate pulses alter timing behavior relative to no-pulse run.
- `block_invariance`
  - Output is invariant to host block segmentation.
- `external_clock_timeout_status`
  - External clock status drops after timeout and remains present with sustained pulses.
- `external_clock_ratios_tempo_range`
  - External clock division/multiplication ordering is validated across 20/60/120/300 BPM.
- `break_macro_intensity_affects_output`
  - Macro Break intensity changes rendered output for fixed input and seed.
- `c_api_parity`
  - C API path output matches C++ API output for the same scenario.
- `persistent_state_roundtrip`
  - Persistent state fields round-trip through the engine API.
- `persistent_blob_roundtrip`
  - Persistent state serializes/deserializes through the library blob API.
- `micro_bend_1v_per_oct`
  - Micro bend CV tracks expected octave-rate ratios for -1V, 0V, +1V.
- `cv_additive_range_affects_mix`
  - Positive/negative full-scale CV causes different mix behavior, validating additive CV range influence.
- `freeze_latching_sync_to_clock`
  - Latching freeze request is held until the next clock tick before engaging.
- `runtime_audio_context_switch`
  - Runtime sample-rate/max-block updates are applied correctly without reinitialization.
- `required_dram_uses_max_supported_rate`
  - DRAM sizing honors `max_supported_sample_rate_hz` instead of only initial sample rate.
- `audio_context_clamps_max_supported_rate`
  - Runtime audio-context sample rate is clamped to configured supported maximum.
- `external_clock_timeout_stable_after_sr_change`
  - External-clock loss timing remains stable when sample rate changes between pulses.
- `persistent_state_sanitises_invalid_enums`
  - Invalid persisted enum values are normalized to safe defaults.
- `c_api_guard_clauses`
  - C API null/invalid-argument guard branches return safely.
- `dropout_uses_smooth_edges`
  - Legacy dropout transitions are ramped and avoid hard one-sample steps.
- `dj_filter_tilt_response`
  - Expanded DJ filter sweeps from LP emphasis to HP emphasis.
- `vinyl_generates_surface_noise`
  - Expanded vinyl model emits bounded non-zero surface texture on silence.
- `corrupt_algorithms_finite_at_extremes`
  - Corrupt algorithms remain finite and bounded at extreme intensities.

## Notes

- These tests are deterministic and intended for CI.
- The disting NT wrapper reference in
  `/Users/nealsanche/nosuch/corrupter/examples/distingnt_wrapper_reference.cpp`
  is not built by default in this repository because `distingNT_API` headers are external.

## Coverage

Coverage instrumentation can be enabled with:

```bash
cmake -S /Users/nealsanche/nosuch/corrupter -B /Users/nealsanche/nosuch/corrupter/build-coverage -DCORRUPTER_ENABLE_COVERAGE=ON
cmake --build /Users/nealsanche/nosuch/corrupter/build-coverage
cmake --build /Users/nealsanche/nosuch/corrupter/build-coverage --target corrupter_dsp_coverage
```

On Clang toolchains, the `corrupter_dsp_coverage` target runs the regression suite and prints an `llvm-cov report` summary.
