# DSP Acceptance Matrix (PRD Section 15)

Scope: DSP-library-only acceptance status for `libcorrupter_dsp`.

Legend:
- `DONE` implemented and covered by code/tests in this repository.
- `PARTIAL` implemented but needs additional measurement/tuning against reference hardware.
- `OUT OF SCOPE` hardware/UI requirement outside DSP library.

## Criteria Mapping

1. CV inputs respond to -5V..+5V additively.
- Status: `PARTIAL`
- Evidence: additive CV mapping for Time/Repeats/Mix/Bend/Break/Corrupt in `/Users/nealsanche/nosuch/corrupter/src/engine.cpp`.
- Gap: no hardware calibration layer in this repo.

2. Gate inputs trigger at 0.4V in latching/momentary.
- Status: `DONE`
- Evidence: threshold logic in `/Users/nealsanche/nosuch/corrupter/src/internal/dsp_common.h`; behavior tests in `/Users/nealsanche/nosuch/corrupter/examples/regression_tests.cpp`.

3. Transparent bypass at Mix fully dry.
- Status: `DONE`
- Evidence: `dry_bypass` regression test in `/Users/nealsanche/nosuch/corrupter/examples/regression_tests.cpp`.

4. Buffer stores at least 60 seconds stereo.
- Status: `DONE`
- Evidence: buffer sizing uses `sample_rate * max_buffer_seconds` in `/Users/nealsanche/nosuch/corrupter/src/engine.cpp`.

5. External clock stable with div/mult and fallback.
- Status: `PARTIAL`
- Evidence: ratio table and free-running fallback in `/Users/nealsanche/nosuch/corrupter/src/internal/clock_engine.cpp`; timeout status tested in `external_clock_timeout_status`.
- Gap: tempo-range validation (20-300 BPM) still needs dedicated test vectors.

6. Macro Bend/Break produce distinct synced behavior.
- Status: `DONE`
- Evidence: macro event path in `/Users/nealsanche/nosuch/corrupter/src/engine.cpp`; regressions include seed determinism, break intensity effect, external clock impact.

7. Micro Bend 1V/oct across +/-3 oct.
- Status: `PARTIAL`
- Evidence: micro mapping path implemented in `/Users/nealsanche/nosuch/corrupter/src/engine.cpp`.
- Gap: explicit calibration/linearity test for 1V/oct tracking is not yet implemented.

8. Corrupt algorithms are distinct and usable.
- Status: `DONE`
- Evidence: Decimate/Dropout/Destroy/DJ Filter/Vinyl Sim in `/Users/nealsanche/nosuch/corrupter/src/internal/corrupt_engine.cpp`.

9. Freeze locks buffer with clean engage/disengage.
- Status: `PARTIAL`
- Evidence: freeze modes + auto-wet behavior in `/Users/nealsanche/nosuch/corrupter/src/engine.cpp`; regression `freeze_momentary_autowet`.
- Gap: objective click metrics still needed.

10. Persistent settings survive power cycle and defaults reset.
- Status: `PARTIAL`
- Evidence: persistent-state struct + wrapper serialization hooks.
- Gap: full persistence ownership is wrapper/host responsibility and needs integration validation.

11. LED states match spec.
- Status: `OUT OF SCOPE`
- Reason: LED/UI behavior is host/hardware layer, not library DSP.

12. Mechanical/power limits.
- Status: `OUT OF SCOPE`
- Reason: hardware requirement, not software DSP library.

## Test Entry Points

- Regression suite: `/Users/nealsanche/nosuch/corrupter/examples/regression_tests.cpp`
- Smoke: `/Users/nealsanche/nosuch/corrupter/examples/offline_smoke.cpp`
- Performance harness: `/Users/nealsanche/nosuch/corrupter/examples/benchmark.cpp`

