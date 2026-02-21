# Corrupter DSP Library Plan + disting NT Integration Spec

## 1. Scope

Build only the DSP as a reusable C++ library that reproduces the PRD behavior of the Data Bender-style engine. A separate implementer should be able to wrap that library as a disting NT C++ plug-in without changing DSP internals.

In scope:
- Real-time stereo DSP engine at 96 kHz.
- Macro + Micro behavior for Bend/Break.
- Corrupt banks (legacy + expanded) and algorithm switching.
- Clocking, Freeze, Repeats, glitch windowing, stereo unique/shared behavior.
- Deterministic state handling and serialization hooks.
- A strict integration contract for disting NT (`distingNT_API`).

Out of scope:
- Hardware UI scanning, LEDs, panel firmware, non-volatile hardware storage drivers.
- Module mechanical/electrical design.
- Direct plugin UI implementation (only wrapper specs).

## 2. Normative Inputs

Primary requirements:
- `/Users/nealsanche/nosuch/corrupter/corrupter-prd.md`

Plugin API target (verified from official API headers/examples):
- `expertsleepersltd/distingNT_API` (`include/distingnt/api.h`, API current = `kNT_apiVersion13`)
- API facts used here:
  - `step(_NT_algorithm*, float* busFrames, int numFramesBy4)` with busses laid out contiguously.
  - Bus constants in API header: `kNT_numInputBusses=12`, `kNT_numOutputBusses=8`, `kNT_numAuxBusses=44`.
  - Real-time globals: `NT_globals.sampleRate`, `NT_globals.maxFramesPerStep`.
  - No dynamic allocation should occur in `step()`.

## 3. Deliverables

### 3.1 DSP Library Deliverables
- `libcorrupter_dsp` static library.
- Public C++ headers (`include/corrupter_dsp/*.h`).
- Optional C ABI shim (`include/corrupter_dsp/c_api.h`) for wrapper portability.
- Offline test harness (CLI runner for WAV in/out and deterministic regression tests).
- Golden test vectors and expected metrics.

### 3.2 Handoff Deliverables for Plugin Implementer
- Parameter schema (IDs, ranges, units, defaults, page grouping).
- Required wrapper lifecycle and call sequencing.
- Memory contract (SRAM/DRAM requests, alignment, ownership).
- Bus contract (audio, CV, gate, clock/reset routing).
- Serialization contract for preset save/restore.
- Acceptance checklist mapped to PRD criteria.

## 4. Real-Time Constraints and Budgets

Hard constraints:
- 96,000 Hz stereo sample processing.
- Block size variable (`1..NT_globals.maxFramesPerStep`), block-invariant behavior.
- No locks, no heap alloc/free, no filesystem I/O in audio path.
- Denormal-safe math and bounded parameter smoothing.

Targets:
- Real-time CPU headroom target: <= 55% worst case on disting NT for full feature load.
- DRAM target: 40-52 MB depending sample format and optional caches.
- Deterministic output for identical seed + control/event stream.

## 5. DSP Library Architecture

### 5.1 Module Graph
- `EngineCore`
  - `ControlMapper`
  - `ClockEngine`
  - `CircularBuffer`
  - `SegmentScheduler`
  - `RepeatsEngine`
  - `BendEngine`
  - `BreakEngine`
  - `WindowingEngine`
  - `CorruptEngine` (strategy bank)
  - `FreezeEngine`
  - `MixEngine`
  - `RandomEngine`

Signal flow (matches PRD):
1. Input write to circular buffer (unless freeze active).
2. Clock-triggered segment capture.
3. Repeats subdivision and subsection selection.
4. Bend processing.
5. Break processing.
6. Glitch windowing.
7. Corrupt algorithm.
8. Equal-power dry/wet mix.

### 5.2 Library Layering
- `core/`: lock-free primitives, ring buffer, smoothing, PRNG.
- `dsp/`: engines and algorithm implementations.
- `model/`: parameter/state model, defaults, persistent-state struct.
- `io/`: optional non-RT helpers for tests (WAV load/store in harness only).

### 5.3 Memory Model
- One contiguous memory block provided by host wrapper.
- Placement-new objects and fixed-capacity arrays inside provided memory.
- Recommended sample storage format: `float` for DSP path consistency.
  - 60 s stereo float @96 kHz: `96000 * 60 * 2 * 4 = 46.08 MB`.
- Optional compact mode: 24-bit packed/int32 internal storage to reduce DRAM, with conversion cost.

## 6. Public DSP API (Contract)

```cpp
namespace corrupter {

struct EngineConfig {
  float sample_rate_hz;             // expected 96000
  uint32_t max_block_frames;        // host max
  float max_buffer_seconds;         // >= 60
  uint32_t random_seed;
};

struct PersistentState {
  bool bend_enabled;
  bool break_enabled;
  bool freeze_enabled;
  bool macro_mode;
  bool break_silence_mode;          // micro sub-mode
  bool unique_stereo_mode;
  bool gate_latching;
  bool freeze_latching;
  bool corrupt_gate_is_reset;
  uint8_t corrupt_bank;             // legacy/expanded
  uint8_t corrupt_algorithm;
  float glitch_window_01;
};

struct KnobState {
  float time_01;
  float repeats_01;
  float mix_01;
  float bend_01;
  float break_01;
  float corrupt_01;
  float bend_cv_attn_01;
  float break_cv_attn_01;
  float corrupt_cv_attn_01;
};

struct CvInputs {
  const float* time_v;
  const float* repeats_v;
  const float* mix_v;
  const float* bend_v;
  const float* break_v;
  const float* corrupt_v;
};

struct GateInputs {
  const float* bend_gate_v;
  const float* break_gate_v;
  const float* corrupt_gate_v;
  const float* freeze_gate_v;
  const float* clock_gate_v;
};

struct AudioBlock {
  const float* in_l;
  const float* in_r;
  float* out_l;
  float* out_r;
  uint32_t frames;
};

class Engine {
public:
  static size_t required_dram_bytes(const EngineConfig& cfg);
  bool initialise(void* dram, size_t dram_bytes, const EngineConfig& cfg);
  void reset();

  void set_knobs(const KnobState& k);
  void set_persistent_state(const PersistentState& s);
  void set_clock_mode_internal(bool internal);

  void process(const AudioBlock& audio, const CvInputs& cv, const GateInputs& gates);
};

} // namespace corrupter
```

Contract rules:
- `process()` must be re-entrant only by single audio thread (no concurrent calls).
- All pointers valid for `frames` samples.
- Null CV/gate pointers mean disconnected input => treated as 0 V.
- Output buffers may alias input buffers.

## 7. Detailed Feature Plan

### 7.1 Clock Engine
- Internal mode:
  - Map `Time` knob 0..1 to period 16.0 s .. 0.0125 s (80 Hz) with perceptual/log scaling.
- External mode:
  - Pulse period estimator with jitter smoothing.
  - 9 ratio table: `÷16, ÷8, ÷4, ÷2, x1, x2, x3, x4, x8`.
  - Clock-loss detection after 4 missed beats; free-run at last tempo until pulse returns.
- Optional reset source:
  - Corrupt gate as reset if mode enabled.

### 7.2 Circular Buffer + Segment Capture
- Continuous stereo write pointer unless Freeze active.
- Segment metadata per clock tick:
  - start frame index
  - segment length in frames
  - timestamp/seed basis
- Time expansion behavior:
  - Long history always available up to max buffer duration.

### 7.3 Repeats Engine
- Map `Repeats` knob/CV to integer subdivision `N` (min 1).
- Derived subsection length = `segment_length / N`.
- Index quantization strategy must be deterministic and click-safe.

### 7.4 Bend Engine

Macro mode (clocked stochastic):
- On each clock tick, sample random event set weighted by Bend intensity:
  - reverse toggles
  - varispeed offsets
  - slewed speed transitions
  - optional pop/click injections
- Shared stereo: same event set both channels.
- Unique stereo: independent event set per channel.

Micro mode (direct control):
- Playback rate from `±3 oct` around unity.
- `1V/oct` CV tracking for Bend input.
- Direction from Bend button/gate state.
- Octave-state flags exposed for UI (plugin wrapper can ignore if no LED model).

### 7.5 Break Engine

Macro mode (clocked stochastic):
- Per tick weighted random:
  - subsection jump
  - transient increase in effective repeats
  - silence insert (up to 90% duty)

Micro mode:
- Traverse sub-mode: direct subsection index select.
- Silence sub-mode: duty ratio 0..90% per subsection.

### 7.6 Glitch Windowing
- Per-subsection envelope with 0..1 amount.
- Amount near 0 => hard edges (very short fade).
- Amount near 1 => full cosine/Hann envelope.
- Must preserve rhythm timing while reducing clicks.

### 7.7 Corrupt Engine

Bank select + algorithm index are part of persistent state.

Legacy bank:
- Decimate: bit depth + sample rate reduction with smooth mapping.
- Dropout: stochastic mute model with duration/count tradeoff.
- Destroy: soft saturation then hard clipping regime.

Expanded bank:
- DJ Filter: LP -> bypass -> HP sweep around center.
- Vinyl Sim: crackle/noise + filtering + tonal wear shaping.

### 7.8 Freeze + Mix
- Freeze latching:
  - latch transitions quantized to next clock edge.
- Freeze momentary:
  - immediate engage/release.
- Auto-wet behavior:
  - if Mix effectively dry and freeze engages, force wet=1.
- Mix crossfade:
  - equal-power curve.

## 8. Parameter Mapping Spec (Library-Level)

Normalized control composition:
- `effective = clamp(knob + cv_scaled, 0, 1)` unless parameter is bipolar (Bend micro speed).
- CV assumptions:
  - nominal range `-5 V..+5 V`.
  - default conversion `v_norm = volts / 5`.
  - expose calibration constants for wrapper override.

Gate handling:
- Rising edge at `>= 0.4 V`, falling edge hysteresis at `< 0.3 V` (recommended).
- Supports latching and momentary globally, plus freeze-latching override.

Smoothing:
- 1-pole smoothing on continuous params.
- Separate faster smoother for pitch/rate controls in micro mode.

## 9. disting NT Wrapper Integration Spec

## 9.1 Build and ABI
- Toolchain: `arm-none-eabi-c++`.
- Minimum flags (from official examples):
  - `-std=c++11 -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -fno-rtti -fno-exceptions -Os -fPIC`
- Include path: `distingNT_API/include`.
- Entry symbol:
  - `extern "C" uintptr_t pluginEntry(_NT_selector selector, uint32_t data)`.
- Report API version with `kNT_apiVersionCurrent` (currently 13 in header).

## 9.2 Factory Lifecycle
Wrapper implements `_NT_factory` callbacks:
1. `calculateRequirements`
2. `construct`
3. `parameterChanged`
4. `step`
5. optional `serialise`/`deserialise`

Memory request strategy:
- `req.sram = sizeof(WrapperAlgorithm)`
- `req.dram = Engine::required_dram_bytes(config)`
- `req.dtc = 0` initially (reserve for hot tables if profiling demands)
- `req.itc = 0`

`construct` responsibilities:
- placement-new wrapper in `ptrs.sram`.
- set `self->parameters` and `self->parameterPages`.
- initialize engine with `ptrs.dram`.
- cache sample rate from `NT_globals.sampleRate`.

## 9.3 Bus and Parameter Contract
Use explicit routing parameters, not hardcoded bus IDs.

Minimum routing params:
- `In L`, `In R` as `NT_PARAMETER_AUDIO_INPUT`.
- `Out L`, `Out R` as `NT_PARAMETER_AUDIO_OUTPUT`.
- `Out mode` boolean (replace/add).

CV bus-select params (`NT_PARAMETER_CV_INPUT`, allow 0=off):
- `Time CV In`
- `Repeats CV In`
- `Mix CV In`
- `Bend CV In`
- `Break CV In`
- `Corrupt CV In`

Gate bus-select params (`NT_PARAMETER_CV_INPUT`, gate interpreted by threshold):
- `Bend Gate In`
- `Break Gate In`
- `Corrupt Gate In`
- `Freeze Gate In`
- `Clock In`

Control/state params:
- Time, Repeats, Mix, Bend, Break, Corrupt
- Macro/Micro
- Break micro sub-mode (Traverse/Silence)
- Bend enable, Break enable, Freeze enable
- Clock source (internal/external)
- Stereo mode (shared/unique)
- Gate mode (latching/momentary)
- Freeze mode (latching/momentary)
- Corrupt bank and algorithm
- Corrupt gate mode (toggle/reset)
- Glitch window amount

Recommended page groups:
- `Main`, `Modes`, `Corrupt`, `Routing Audio`, `Routing CV`, `Routing Gates`, `Advanced`

## 9.4 Audio Step Integration
Inside `_NT_factory.step`:
1. `frames = numFramesBy4 * 4`.
2. Resolve bus pointers for all selected routing params.
3. Populate `AudioBlock`, `CvInputs`, `GateInputs`.
4. Call `engine.process(...)`.
5. Write to output busses:
  - replace mode: assign.
  - add mode: accumulate.

Important:
- Never allocate memory in `step`.
- Prefer stack locals and cached indices.
- Validate bus selection once in `parameterChanged`; clamp invalid to safe default.

## 9.5 Parameter Updates
`parameterChanged` should:
- Refresh cached routing indices and mode booleans.
- Push knob/value changes into engine control state.
- Recompute any non-audio-rate derived coefficients.

Do not run expensive recomputation in `step` if it can be done here.

## 9.6 Preset Serialization
Use `_NT_jsonStream` / `_NT_jsonParse` to persist:
- persistent toggles/modes
- selected corrupt bank/algo
- glitch window
- optionally PRNG seed/state for deterministic recall

Keep serialized keys stable and versioned:
- include `"schema_version"` integer.

## 9.7 Plugin Deployment Workflow (for implementer)
- Compile to `.o` plugin object.
- Transfer to `/programs/plug-ins/<name>.o` on the module.
- Trigger plugin rescan (official helper script in `distingNT/tools/push_plugin_to_device.py` documents this flow).

## 10. Implementation Phases

Phase 0: Spec lock
- Freeze parameter schema, ranges, defaults, and mode semantics.
- Define deterministic event model for macro engines.
Exit criteria:
- Signed-off `params.md` and `state_machine.md`.

Phase 1: Skeleton + core infra
- Create library project layout, build system, CI.
- Implement memory arena, ring buffer, smoothing, PRNG.
Exit criteria:
- Unit tests green for core infra.

Phase 2: Clock/segment/repeats/freeze/mix
- Implement clocking, segment capture, repeats engine, freeze logic, equal-power mix.
Exit criteria:
- Audio harness demonstrates stable loop/repeat behavior and freeze modes.

Phase 3: Bend + Break
- Implement macro/micro Bend and Break with stereo shared/unique behavior.
- Implement gate mode state machines.
Exit criteria:
- Behavioral tests for mode transitions, range bounds, repeat traversal.

Phase 4: Corrupt bank
- Implement Decimate/Dropout/Destroy + DJ Filter/Vinyl Sim.
- Add algorithm-switch smoothing/safe switching.
Exit criteria:
- Distinct effect metrics + listening tests pass.

Phase 5: Wrapper reference + integration docs
- Build a minimal reference disting NT wrapper (thin adapter only).
- Validate routing, preset serialization, and performance.
Exit criteria:
- Plugin runs with no underruns in stress presets.

Phase 6: Matching and QA
- Perceptual tuning against reference hardware captures.
- Acceptance tests mapped to PRD section 15.
Exit criteria:
- Pass/fail report with captured evidence.

## 11. Test Strategy

Automated tests:
- Unit: each engine block and state machine.
- Property: bounds/NaN safety, no out-of-range pointers, deterministic replay.
- Regression: fixed input + seed => bit-exact output hash (or tolerance-based if platform float differences).

Audio validation:
- Scenario renders for each feature extreme and midpoint.
- AB matching sessions against reference module recordings.

Performance validation:
- Cycle/time profiling in wrapper `step` path.
- Worst-case preset with all effects, high repeats, unique stereo.

## 12. Risks and Mitigations

Risk: PRNG/probability curve mismatch vs original device.
- Mitigation: isolate curve tables for quick perceptual retuning.

Risk: DRAM pressure from 60 s stereo buffer.
- Mitigation: provide compile-time storage mode and runtime max-time fallback.

Risk: Clock jitter artifacts in external sync.
- Mitigation: robust period filtering + hysteresis + lost-clock fallback.

Risk: clicks at segment boundaries and mode switches.
- Mitigation: envelope windowing, short crossfades, zero-cross heuristics.

## 13. Handoff Checklist for Plugin Implementer

Required from DSP team:
- Library headers + static lib.
- Parameter/state schema docs.
- Example adapter pseudo-code.
- Memory requirement calculator and recommended constants.
- Test vector pack.

Required from plugin implementer:
- `_NT_factory` wrapper with full parameter pages.
- Bus routing UI parameters and validation.
- Preset serialization glue.
- On-device profiling and final tuning feedback.

Definition of done:
- Wrapper links without modifying DSP internals.
- PRD acceptance items 1-11 (DSP-relevant) validated.
- No real-time violations under stress conditions.
