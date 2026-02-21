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
- `c_api_parity`
  - C API path output matches C++ API output for the same scenario.

## Notes

- These tests are deterministic and intended for CI.
- The disting NT wrapper reference in
  `/Users/nealsanche/nosuch/corrupter/examples/distingnt_wrapper_reference.cpp`
  is not built by default in this repository because `distingNT_API` headers are external.

