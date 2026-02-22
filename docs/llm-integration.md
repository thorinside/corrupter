# Corrupter DSP: LLM Integration Guide

This document is for an LLM (or human using an LLM workflow) that needs to integrate `libcorrupter_dsp` into a host wrapper such as disting NT.

Use this as the canonical implementation checklist and guardrail set.

## 1. Non-Negotiable Runtime Contract

1. Do not allocate memory in the audio callback (`step`/`process` loop).
2. Always call `Engine::set_audio_context(host_sample_rate, host_max_block_frames)` before processing a block.
3. Support variable host sample rates and block sizes.
4. Pass null CV/gate pointers for unpatched inputs.
5. Treat gate high as `>= 0.4V`.

## 2. Engine API You Must Use

Primary API:
- `/Users/nealsanche/nosuch/corrupter/include/corrupter_dsp/engine.h`
- `/Users/nealsanche/nosuch/corrupter/include/corrupter_dsp/c_api.h`

Core sequence:
1. Fill `EngineConfig`.
2. Call `Engine::required_dram_bytes(config)`.
3. Allocate persistent DRAM once.
4. Call `engine.initialise(dram, dram_bytes, config)`.
5. For every audio block:
   - `engine.set_audio_context(sample_rate_hz, max_block_frames)`
   - `engine.set_knobs(...)` and `engine.set_persistent_state(...)` when parameters change
   - `engine.set_clock_mode_internal(...)`
   - `engine.process(audio, cv, gates)`

## 3. Required Config Values (NT-Safe)

For host engines like disting NT:
- `sample_rate_hz = NT_globals.sampleRate` (initial value)
- `max_supported_sample_rate_hz = 96000.0f` (platform max)
- `max_block_frames = NT_globals.maxFramesPerStep`
- `max_buffer_seconds = 60.0f` (or host-allowed reduction)

Why:
- DRAM allocation is sized from `max_supported_sample_rate_hz`, not only initial sample rate.
- Runtime changes are applied through `set_audio_context`.

## 4. Audio/CV/Gate Mapping Rules

Audio:
- `AudioBlock.in_l/in_r` are source channels.
- `AudioBlock.out_l/out_r` are render targets.
- In-place processing is allowed.

CV:
- Input range expected: `-5V..+5V`.
- Controls are additive with knob base.

Gates:
- Feed as CV streams.
- Use `0` bus/disabled route as null pointer.

## 5. Persistent State and Safety

Persist/restore using:
- `Engine::serialise_persistent_state(...)`
- `Engine::deserialise_persistent_state(...)`

Safety behavior:
- Invalid persisted enum values are sanitized to safe defaults (`Legacy`, `Decimate`).

## 6. Real-Time Do/Do-Not

Do:
- Cache bus indices and parameter transforms in `parameterChanged`.
- Reuse host work buffers.
- Keep audio-thread logic branch-light.

Do not:
- Allocate/free memory in block processing.
- Perform filesystem I/O in the audio thread.
- Rebuild/reinitialize the engine on sample-rate or block-size changes.

## 7. Known Failure Modes (and Fix)

1. External clock loss timing drifts when sample rate changes mid-beat.
- Fixed in `/Users/nealsanche/nosuch/corrupter/src/internal/clock_engine.cpp`.
- Guard test: `external_clock_timeout_stable_after_sr_change`.

2. Invalid serialized corrupt enums can produce undefined behavior.
- Fixed via sanitization in `/Users/nealsanche/nosuch/corrupter/src/engine.cpp`.
- Guard test: `persistent_state_sanitises_invalid_enums`.

3. Host wrappers forgetting runtime context updates.
- Guard tests: `runtime_audio_context_switch`, `audio_context_clamps_max_supported_rate`.

## 8. Integration Reference Files

Use these as implementation references:
- `/Users/nealsanche/nosuch/corrupter/examples/distingnt_wrapper_reference.cpp`
- `/Users/nealsanche/nosuch/corrupter/docs/distingnt-parameter-spec.md`
- `/Users/nealsanche/nosuch/corrupter/corrupter-dsp-disting-plan.md`

## 9. Verification Commands (Must Run)

```bash
cmake -S /Users/nealsanche/nosuch/corrupter -B /Users/nealsanche/nosuch/corrupter/build
cmake --build /Users/nealsanche/nosuch/corrupter/build
ctest --test-dir /Users/nealsanche/nosuch/corrupter/build --output-on-failure
```

Coverage run:

```bash
cmake -S /Users/nealsanche/nosuch/corrupter -B /Users/nealsanche/nosuch/corrupter/build-coverage -DCORRUPTER_ENABLE_COVERAGE=ON
cmake --build /Users/nealsanche/nosuch/corrupter/build-coverage
cmake --build /Users/nealsanche/nosuch/corrupter/build-coverage --target corrupter_dsp_coverage
```

## 10. LLM Task Prompt Template

If delegating this integration to another LLM, use this concise prompt:

```text
Integrate libcorrupter_dsp as a thin wrapper only. Do not edit DSP internals unless tests fail.
Respect runtime sample-rate/block-size changes by calling set_audio_context every block.
Use EngineConfig max_supported_sample_rate_hz=96000 and max_block_frames from host.
No allocation in audio callback. Null CV/gate pointers for unpatched routes.
Run ctest and coverage target; report failures with file+line and proposed fix.
```
