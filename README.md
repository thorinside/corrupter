# Corrupter

A stereo circuit-bent digital audio buffer that emulates the sonic artifacts of failing media — skipping CDs, warped tapes, corrupted data streams, and scratched vinyl. Inspired by the Qu-Bit Electronix Data Bender.

Corrupter is both a standalone DSP library (C++ and C APIs) and a plugin for the [Expert Sleepers disting NT](https://expert-sleepers.co.uk/distingNT.html) Eurorack module.

## Effects

Three independent destruction axes, each with Macro (dramatic, randomised) and Micro (subtle, continuous) modes:

### Bend
Tape-speed manipulation. In Macro mode, each clock tick randomises playback rate and may trigger reverse playback, with wow, flutter, and tape saturation scaled to intensity. In Micro mode, the knob directly controls pitch shift across +-3 octaves with analog tape character (wow, flutter, drive, filtering).

### Break
Buffer-position glitching. In Macro mode, the read head jumps to random positions within the current segment. In Micro mode, two sub-modes are available: Traverse (scrubs through the buffer) and Silence (introduces rhythmic dropouts).

### Corrupt
Signal degradation applied to the output. Five algorithms across two banks:

| Bank | Algorithm | Description |
|------|-----------|-------------|
| Legacy | Decimate | Bitcrusher — reduces bit depth and sample rate |
| Legacy | Dropout | Random signal gating with smooth fade ramps |
| Legacy | Destroy | Soft saturation into hard clipping with variable drive |
| Expanded | DJ Filter | 2x oversampled TPT state-variable filter, lowpass below center, highpass above |
| Expanded | Vinyl Sim | Rumble, hiss, crackle, pops, and worn-vinyl EQ rolloff |

## disting NT Plugin

The NT plugin provides a custom UI with Data Bender-inspired control mapping on the disting NT's physical controls.

### Controls

| Control | Function |
|---------|----------|
| L pot | Bend amount |
| C pot | Break amount |
| R pot | Corrupt amount |
| L encoder | Time (delay period) |
| R encoder | Repeats |
| L pot press | Toggle Bend on/off |
| C pot press | Toggle Break on/off |
| R pot press | Toggle Freeze |
| L enc press | Toggle Macro/Micro mode |
| R enc press | Cycle corrupt algorithm |

Mix and other settings are available through the standard parameter pages.

### Display

The 256x64 OLED shows:
- Title bar with current mode (Macro/Micro) and clock source (INT/EXT)
- Status badges (BND/BRK/FRZ) with box highlight when active
- Active corrupt algorithm name
- Output waveform visualization with write position marker
- Current values for all pot and encoder parameters

### Installation

Download `corrupter-plugin.zip` from the [latest release](https://github.com/thorinside/corrupter/releases/latest). Unzip and copy the `programs/plug-ins/corrupter/` folder to your disting NT's SD card.

### Specification

When adding the plugin to an algorithm slot, you can configure the buffer length (5-30 seconds, default 15). Longer buffers use more DRAM.

## DSP Library

The DSP engine is a standalone library with no platform dependencies. It can be embedded in any host that provides audio buffers and parameter values.

### C++ API

```cpp
#include "corrupter_dsp/engine.h"

corrupter::EngineConfig cfg;
cfg.sample_rate_hz = 96000.0f;
cfg.max_buffer_seconds = 15.0f;
cfg.max_block_frames = 256;

// Allocate DRAM for audio buffers
size_t dram_size = corrupter::Engine::required_dram_bytes(cfg);
void* dram = malloc(dram_size);

corrupter::Engine engine;
engine.initialise(dram, dram_size, cfg);

// Set controls
corrupter::KnobState knobs{};
knobs.time_01 = 0.5f;
knobs.repeats_01 = 0.5f;
knobs.mix_01 = 1.0f;
engine.set_knobs(knobs);

// Process audio
corrupter::AudioBlock audio;
audio.in_l = input_left;
audio.in_r = input_right;
audio.out_l = output_left;
audio.out_r = output_right;
audio.frames = 256;
engine.process(audio, cv_inputs, gate_inputs);
```

### C API

A flat C-compatible API is also available for embedding in C projects or language bindings:

```c
#include "corrupter_dsp/c_api.h"

size_t engine_size = corrupter_engine_sizeof();
void* engine = malloc(engine_size);
corrupter_engine_construct(engine);

size_t dram_size = corrupter_engine_required_dram_bytes(&cfg);
void* dram = malloc(dram_size);
corrupter_engine_initialise(engine, dram, dram_size, &cfg);

corrupter_engine_set_knobs(engine, &knobs);
corrupter_engine_process(engine, &audio, &cv, &gates);
```

### Key Design Properties

- **No runtime allocations** — all memory is pre-allocated via DRAM buffers
- **No exceptions, no RTTI** — safe for embedded and real-time contexts
- **Deterministic randomness** — seeded PRNG for reproducible results across sessions
- **Block-based processing** — processes up to 256 frames per call
- **State serialisation** — full save/restore of persistent state

## Building

### Requirements

- C++17 compiler
- CMake 3.16+ (for desktop library and tests)
- ARM toolchain for hardware builds: `brew install --cask gcc-arm-embedded`

### Desktop (library + tests)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

### disting NT hardware plugin

```bash
cd nt
make hardware    # produces plugins/corrupter.o
```

### Desktop test plugin (for nt_emu / VCV Rack)

```bash
cd nt
make test        # produces plugins/corrupter.dylib (macOS) or .so (Linux)
```

### Push to device

```bash
cd nt
make push        # builds then pushes via ntpush
```

## Project Structure

```
corrupter/
  include/corrupter_dsp/
    engine.h              C++ API
    c_api.h               C API
    parameter_ids.h       43 parameter IDs
  src/
    engine.cpp            Main DSP engine
    c_api.cpp             C API wrapper
    internal/
      clock_engine.cpp    Internal/external clock with rate multipliers
      corrupt_engine.cpp  5 corrupt algorithms
  nt/
    corrupter_nt.cpp      disting NT plugin (custom UI + DSP bridge)
    Makefile              ARM hardware + desktop test builds
  examples/
    regression_tests.cpp  Comprehensive test suite
    offline_smoke.cpp     Minimal smoke test
    benchmark.cpp         Performance profiling
  distingNT_API/          API headers (git submodule)
```

## License

All rights reserved.
