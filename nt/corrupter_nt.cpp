// Corrupter distingNT plugin - Data Bender-style tape manipulation effect
//
// Single-file plugin with custom UI mapping the Data Bender panel layout
// to distingNT's 3 pots + 2 encoders + 5 presses.

#include <cmath>
#include <cstring>
#include <new>

#include <distingnt/api.h>
#include <distingnt/serialisation.h>

#include "corrupter_dsp/engine.h"
#include "corrupter_dsp/parameter_ids.h"

namespace {

using corrupter::DistingNtParamId;
constexpr int P(DistingNtParamId id) { return static_cast<int>(id); }

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

float norm01(int16_t v, int16_t lo, int16_t hi) {
  if (hi <= lo) return 0.0f;
  float x = static_cast<float>(v - lo) / static_cast<float>(hi - lo);
  return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

float* busPtr(float* busFrames, int numFrames, int busSel) {
  if (busSel <= 0) return nullptr;
  return busFrames + (busSel - 1) * numFrames;
}

void setParam(const _NT_algorithm* self, int paramIdx, int16_t value) {
  NT_setParameterFromUi(NT_algorithmIndex(self), paramIdx + NT_parameterOffset(), value);
}

// ---------------------------------------------------------------------------
// Enum strings
// ---------------------------------------------------------------------------

static const char* kEnumBinary[] = {"Off", "On", nullptr};
static const char* kEnumMode[] = {"Macro", "Micro", nullptr};
static const char* kEnumBreakMicro[] = {"Traverse", "Silence", nullptr};
static const char* kEnumClockSource[] = {"Internal", "External", nullptr};
static const char* kEnumStereo[] = {"Shared", "Unique", nullptr};
static const char* kEnumGateMode[] = {"Latching", "Momentary", nullptr};
static const char* kEnumCorruptGate[] = {"Toggle", "Clock Reset", nullptr};
static const char* kEnumCorruptBank[] = {"Legacy", "Expanded", nullptr};
static const char* kEnumCorruptAlgo[] = {"Decimate", "Dropout", "Destroy", "DJ Filter", "Vinyl Sim", nullptr};
static const char* kEnumSeedMode[] = {"Session", "Fixed", nullptr};

// ---------------------------------------------------------------------------
// Specifications (user chooses at algorithm creation time)
// ---------------------------------------------------------------------------

enum { kSpecBufferSeconds = 0 };

static const _NT_specification specifications[] = {
  { .name = "Buffer seconds", .min = 5, .max = 30, .def = 15, .type = kNT_typeSeconds },
};

// ---------------------------------------------------------------------------
// Parameter definitions (44 total, matching parameter_ids.h exactly)
// ---------------------------------------------------------------------------

static const _NT_parameter parameters[] = {
  // 0-1: Audio inputs
  NT_PARAMETER_AUDIO_INPUT("In L", 1, 1)                                      // 0
  NT_PARAMETER_AUDIO_INPUT("In R", 1, 2)                                      // 1
  // 2-5: Audio outputs (each WITH_MODE expands to 2 params)
  NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE("Out L", 1, 13)                         // 2, 3
  NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE("Out R", 1, 14)                         // 4, 5
  // 6-11: Main knobs
  {.name = "Time",    .min = 0, .max = 1000, .def = 500,  .unit = kNT_unitPercent, .scaling = kNT_scaling10, .enumStrings = nullptr},  // 6
  {.name = "Repeats", .min = 0, .max = 1000, .def = 500,  .unit = kNT_unitPercent, .scaling = kNT_scaling10, .enumStrings = nullptr},  // 7
  {.name = "Mix",     .min = 0, .max = 1000, .def = 1000, .unit = kNT_unitPercent, .scaling = kNT_scaling10, .enumStrings = nullptr},  // 8
  {.name = "Bend",    .min = 0, .max = 1000, .def = 0,    .unit = kNT_unitPercent, .scaling = kNT_scaling10, .enumStrings = nullptr},  // 9
  {.name = "Break",   .min = 0, .max = 1000, .def = 0,    .unit = kNT_unitPercent, .scaling = kNT_scaling10, .enumStrings = nullptr},  // 10
  {.name = "Corrupt", .min = 0, .max = 1000, .def = 0,    .unit = kNT_unitPercent, .scaling = kNT_scaling10, .enumStrings = nullptr},  // 11
  // 12-14: CV attenuators
  {.name = "Bend CV Attn",    .min = 0, .max = 1000, .def = 1000, .unit = kNT_unitPercent, .scaling = kNT_scaling10, .enumStrings = nullptr},  // 12
  {.name = "Break CV Attn",   .min = 0, .max = 1000, .def = 1000, .unit = kNT_unitPercent, .scaling = kNT_scaling10, .enumStrings = nullptr},  // 13
  {.name = "Corrupt CV Attn", .min = 0, .max = 1000, .def = 1000, .unit = kNT_unitPercent, .scaling = kNT_scaling10, .enumStrings = nullptr},  // 14
  // 15-24: Mode enums
  {.name = "Mode",         .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = kEnumMode},          // 15
  {.name = "Break Micro",  .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = kEnumBreakMicro},    // 16
  {.name = "Bend On",      .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = kEnumBinary},        // 17
  {.name = "Break On",     .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = kEnumBinary},        // 18
  {.name = "Freeze",       .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = kEnumBinary},        // 19
  {.name = "Clock Src",    .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = kEnumClockSource},   // 20
  {.name = "Stereo",       .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = kEnumStereo},        // 21
  {.name = "Gate Mode",    .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = kEnumGateMode},      // 22
  {.name = "Freeze Gate",  .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = kEnumGateMode},      // 23
  {.name = "Corrupt Gate", .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = kEnumCorruptGate},   // 24
  // 25-26: Corrupt bank + algorithm
  {.name = "Corrupt Bank", .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = kEnumCorruptBank},   // 25
  {.name = "Corrupt Algo", .min = 0, .max = 4, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = kEnumCorruptAlgo},   // 26
  // 27: Glitch window
  {.name = "Glitch Win",   .min = 0, .max = 1000, .def = 20, .unit = kNT_unitPercent, .scaling = kNT_scaling10, .enumStrings = nullptr},  // 27
  // 28-33: CV routing
  NT_PARAMETER_CV_INPUT("Time CV In",    0, 0)    // 28
  NT_PARAMETER_CV_INPUT("Repeats CV In", 0, 0)    // 29
  NT_PARAMETER_CV_INPUT("Mix CV In",     0, 0)    // 30
  NT_PARAMETER_CV_INPUT("Bend CV In",    0, 0)    // 31
  NT_PARAMETER_CV_INPUT("Break CV In",   0, 0)    // 32
  NT_PARAMETER_CV_INPUT("Corrupt CV In", 0, 0)    // 33
  // 34-38: Gate routing
  NT_PARAMETER_CV_INPUT("Bend Gate In",    0, 0)  // 34
  NT_PARAMETER_CV_INPUT("Break Gate In",   0, 0)  // 35
  NT_PARAMETER_CV_INPUT("Corrupt Gate In", 0, 0)  // 36
  NT_PARAMETER_CV_INPUT("Freeze Gate In",  0, 0)  // 37
  NT_PARAMETER_CV_INPUT("Clock In",        0, 0)  // 38
  // 39-42: Advanced
  {.name = "Seed Mode",     .min = 0, .max = 1,     .def = 0, .unit = kNT_unitEnum,    .scaling = 0, .enumStrings = kEnumSeedMode},  // 39
  {.name = "Fixed Seed",    .min = 0, .max = 32767, .def = 1, .unit = kNT_unitNone,    .scaling = 0, .enumStrings = nullptr},         // 40
  {.name = "Reset Engine",  .min = 0, .max = 1,     .def = 0, .unit = kNT_unitConfirm, .scaling = 0, .enumStrings = nullptr},         // 41
  {.name = "Restore Defaults", .min = 0, .max = 1,  .def = 0, .unit = kNT_unitConfirm, .scaling = 0, .enumStrings = nullptr},         // 42
};

static_assert(ARRAY_SIZE(parameters) == static_cast<int>(DistingNtParamId::kParamCount),
              "Parameter count mismatch");

// ---------------------------------------------------------------------------
// Parameter pages
// ---------------------------------------------------------------------------

static const uint8_t page_main[] = {
  P(DistingNtParamId::kParamTime), P(DistingNtParamId::kParamRepeats),
  P(DistingNtParamId::kParamMix), P(DistingNtParamId::kParamBend),
  P(DistingNtParamId::kParamBreak), P(DistingNtParamId::kParamCorrupt),
};

static const uint8_t page_cv_attn[] = {
  P(DistingNtParamId::kParamBendCvAttn), P(DistingNtParamId::kParamBreakCvAttn),
  P(DistingNtParamId::kParamCorruptCvAttn), P(DistingNtParamId::kParamGlitchWindow),
};

static const uint8_t page_modes[] = {
  P(DistingNtParamId::kParamMode), P(DistingNtParamId::kParamBreakMicroMode),
  P(DistingNtParamId::kParamBendEnabled), P(DistingNtParamId::kParamBreakEnabled),
  P(DistingNtParamId::kParamFreezeEnabled), P(DistingNtParamId::kParamClockSource),
  P(DistingNtParamId::kParamStereoMode), P(DistingNtParamId::kParamGateMode),
  P(DistingNtParamId::kParamFreezeGateMode), P(DistingNtParamId::kParamCorruptGateMode),
};

static const uint8_t page_corrupt[] = {
  P(DistingNtParamId::kParamCorruptBank), P(DistingNtParamId::kParamCorruptAlgorithm),
};

static const uint8_t page_audio[] = {
  P(DistingNtParamId::kParamAudioInL), P(DistingNtParamId::kParamAudioInR),
  P(DistingNtParamId::kParamAudioOutL), P(DistingNtParamId::kParamAudioOutLMode),
  P(DistingNtParamId::kParamAudioOutR), P(DistingNtParamId::kParamAudioOutRMode),
};

static const uint8_t page_cv_routing[] = {
  P(DistingNtParamId::kParamTimeCvInput), P(DistingNtParamId::kParamRepeatsCvInput),
  P(DistingNtParamId::kParamMixCvInput), P(DistingNtParamId::kParamBendCvInput),
  P(DistingNtParamId::kParamBreakCvInput), P(DistingNtParamId::kParamCorruptCvInput),
};

static const uint8_t page_gates[] = {
  P(DistingNtParamId::kParamBendGateInput), P(DistingNtParamId::kParamBreakGateInput),
  P(DistingNtParamId::kParamCorruptGateInput), P(DistingNtParamId::kParamFreezeGateInput),
  P(DistingNtParamId::kParamClockGateInput),
};

static const uint8_t page_advanced[] = {
  P(DistingNtParamId::kParamRandomSeedMode), P(DistingNtParamId::kParamFixedSeed),
  P(DistingNtParamId::kParamResetEngine), P(DistingNtParamId::kParamRestoreDefaults),
};

static const _NT_parameterPage pages[] = {
  {.name = "Main",     .numParams = ARRAY_SIZE(page_main),       .group = 1, .unused = {}, .params = page_main},
  {.name = "CV Attn",  .numParams = ARRAY_SIZE(page_cv_attn),    .group = 2, .unused = {}, .params = page_cv_attn},
  {.name = "Modes",    .numParams = ARRAY_SIZE(page_modes),      .group = 3, .unused = {}, .params = page_modes},
  {.name = "Corrupt",  .numParams = ARRAY_SIZE(page_corrupt),    .group = 4, .unused = {}, .params = page_corrupt},
  {.name = "Audio",    .numParams = ARRAY_SIZE(page_audio),      .group = 5, .unused = {}, .params = page_audio},
  {.name = "CV Route", .numParams = ARRAY_SIZE(page_cv_routing), .group = 6, .unused = {}, .params = page_cv_routing},
  {.name = "Gates",    .numParams = ARRAY_SIZE(page_gates),      .group = 6, .unused = {}, .params = page_gates},
  {.name = "Advanced", .numParams = ARRAY_SIZE(page_advanced),   .group = 7, .unused = {}, .params = page_advanced},
};

static const _NT_parameterPages parameterPages = {
  .numPages = ARRAY_SIZE(pages),
  .pages = pages,
};

// ---------------------------------------------------------------------------
// Algorithm struct
// ---------------------------------------------------------------------------

struct CorrupterAlgorithm : public _NT_algorithm {
  corrupter::Engine engine;
  corrupter::EngineConfig cfg;
  corrupter::KnobState knobs;
  corrupter::PersistentState persistent;
  corrupter::RuntimeInfo runtime_info;

  // Cached bus routing
  int in_l = 1, in_r = 2;
  int out_l = 13, out_r = 14;
  bool out_l_replace = true, out_r_replace = true;
  int cv_bus[6] = {};     // time, repeats, mix, bend, break, corrupt
  int gate_bus[5] = {};   // bend, break, corrupt, freeze, clock

  // Waveform display: 128 bins of peak amplitude for output visualization
  static constexpr int kWaveBins = 128;
  uint8_t wave_peaks[kWaveBins] = {};  // 0-8, half-height of waveform box
  uint32_t wave_write_pos = 0;         // current bin being accumulated
  float wave_accum = 0.0f;             // running peak for current bin
  uint32_t wave_accum_count = 0;       // frames accumulated in current bin

  bool initialised = false;
};

// ---------------------------------------------------------------------------
// syncParameters - read all NT param values into cached engine state
// ---------------------------------------------------------------------------

void syncParameters(CorrupterAlgorithm* alg) {
  if (!alg) return;
  const int16_t* v = alg->v;

  // Audio routing
  alg->in_l = v[P(DistingNtParamId::kParamAudioInL)];
  alg->in_r = v[P(DistingNtParamId::kParamAudioInR)];
  alg->out_l = v[P(DistingNtParamId::kParamAudioOutL)];
  alg->out_l_replace = (v[P(DistingNtParamId::kParamAudioOutLMode)] != 0);
  alg->out_r = v[P(DistingNtParamId::kParamAudioOutR)];
  alg->out_r_replace = (v[P(DistingNtParamId::kParamAudioOutRMode)] != 0);

  // CV routing
  alg->cv_bus[0] = v[P(DistingNtParamId::kParamTimeCvInput)];
  alg->cv_bus[1] = v[P(DistingNtParamId::kParamRepeatsCvInput)];
  alg->cv_bus[2] = v[P(DistingNtParamId::kParamMixCvInput)];
  alg->cv_bus[3] = v[P(DistingNtParamId::kParamBendCvInput)];
  alg->cv_bus[4] = v[P(DistingNtParamId::kParamBreakCvInput)];
  alg->cv_bus[5] = v[P(DistingNtParamId::kParamCorruptCvInput)];

  // Gate routing
  alg->gate_bus[0] = v[P(DistingNtParamId::kParamBendGateInput)];
  alg->gate_bus[1] = v[P(DistingNtParamId::kParamBreakGateInput)];
  alg->gate_bus[2] = v[P(DistingNtParamId::kParamCorruptGateInput)];
  alg->gate_bus[3] = v[P(DistingNtParamId::kParamFreezeGateInput)];
  alg->gate_bus[4] = v[P(DistingNtParamId::kParamClockGateInput)];

  // Knobs
  alg->knobs.time_01            = norm01(v[P(DistingNtParamId::kParamTime)], 0, 1000);
  alg->knobs.repeats_01         = norm01(v[P(DistingNtParamId::kParamRepeats)], 0, 1000);
  alg->knobs.mix_01             = norm01(v[P(DistingNtParamId::kParamMix)], 0, 1000);
  alg->knobs.bend_01            = norm01(v[P(DistingNtParamId::kParamBend)], 0, 1000);
  alg->knobs.break_01           = norm01(v[P(DistingNtParamId::kParamBreak)], 0, 1000);
  alg->knobs.corrupt_01         = norm01(v[P(DistingNtParamId::kParamCorrupt)], 0, 1000);
  alg->knobs.bend_cv_attn_01    = norm01(v[P(DistingNtParamId::kParamBendCvAttn)], 0, 1000);
  alg->knobs.break_cv_attn_01   = norm01(v[P(DistingNtParamId::kParamBreakCvAttn)], 0, 1000);
  alg->knobs.corrupt_cv_attn_01 = norm01(v[P(DistingNtParamId::kParamCorruptCvAttn)], 0, 1000);

  // Persistent state
  alg->persistent.macro_mode           = (v[P(DistingNtParamId::kParamMode)] == 0);
  alg->persistent.break_silence_mode   = (v[P(DistingNtParamId::kParamBreakMicroMode)] != 0);
  alg->persistent.bend_enabled         = (v[P(DistingNtParamId::kParamBendEnabled)] != 0);
  alg->persistent.break_enabled        = (v[P(DistingNtParamId::kParamBreakEnabled)] != 0);
  alg->persistent.freeze_enabled       = (v[P(DistingNtParamId::kParamFreezeEnabled)] != 0);
  alg->persistent.unique_stereo_mode   = (v[P(DistingNtParamId::kParamStereoMode)] != 0);
  alg->persistent.gate_latching        = (v[P(DistingNtParamId::kParamGateMode)] == 0);
  alg->persistent.freeze_latching      = (v[P(DistingNtParamId::kParamFreezeGateMode)] == 0);
  alg->persistent.corrupt_gate_is_reset = (v[P(DistingNtParamId::kParamCorruptGateMode)] != 0);
  alg->persistent.corrupt_bank =
      (v[P(DistingNtParamId::kParamCorruptBank)] == 0) ? corrupter::CorruptBank::kLegacy
                                                        : corrupter::CorruptBank::kExpanded;
  alg->persistent.corrupt_algorithm =
      static_cast<corrupter::CorruptAlgorithm>(v[P(DistingNtParamId::kParamCorruptAlgorithm)]);
  alg->persistent.glitch_window_01 = norm01(v[P(DistingNtParamId::kParamGlitchWindow)], 0, 1000);

  alg->engine.set_knobs(alg->knobs);
  alg->engine.set_persistent_state(alg->persistent);
  alg->engine.set_clock_mode_internal(v[P(DistingNtParamId::kParamClockSource)] == 0);
}

// ---------------------------------------------------------------------------
// calculateRequirements
// ---------------------------------------------------------------------------

void calculateRequirements(_NT_algorithmRequirements& req, const int32_t* specs) {
  req.numParameters = ARRAY_SIZE(parameters);
  req.sram = sizeof(CorrupterAlgorithm);

  int32_t buf_secs = specs ? specs[kSpecBufferSeconds] : specifications[kSpecBufferSeconds].def;
  if (buf_secs < specifications[kSpecBufferSeconds].min) buf_secs = specifications[kSpecBufferSeconds].min;
  if (buf_secs > specifications[kSpecBufferSeconds].max) buf_secs = specifications[kSpecBufferSeconds].max;

  corrupter::EngineConfig cfg;
  cfg.sample_rate_hz = static_cast<float>(NT_globals.sampleRate);
  cfg.max_supported_sample_rate_hz = 96000.0f;
  cfg.max_block_frames = NT_globals.maxFramesPerStep;
  cfg.max_buffer_seconds = static_cast<float>(buf_secs);
  cfg.random_seed = 1;
  req.dram = static_cast<uint32_t>(corrupter::Engine::required_dram_bytes(cfg));

  req.dtc = 0;
  req.itc = 0;
}

// ---------------------------------------------------------------------------
// construct
// ---------------------------------------------------------------------------

_NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs,
                         const _NT_algorithmRequirements& req,
                         const int32_t* specs) {
  (void)req;
  (void)specs;

  CorrupterAlgorithm* alg = new (ptrs.sram) CorrupterAlgorithm();
  alg->parameters = parameters;
  alg->parameterPages = &parameterPages;

  int32_t buf_secs = specs ? specs[kSpecBufferSeconds] : specifications[kSpecBufferSeconds].def;
  if (buf_secs < specifications[kSpecBufferSeconds].min) buf_secs = specifications[kSpecBufferSeconds].min;
  if (buf_secs > specifications[kSpecBufferSeconds].max) buf_secs = specifications[kSpecBufferSeconds].max;

  alg->cfg.sample_rate_hz = static_cast<float>(NT_globals.sampleRate);
  alg->cfg.max_supported_sample_rate_hz = 96000.0f;
  alg->cfg.max_block_frames = NT_globals.maxFramesPerStep;
  alg->cfg.max_buffer_seconds = static_cast<float>(buf_secs);
  alg->cfg.random_seed = 1;

  alg->initialised = alg->engine.initialise(
      ptrs.dram,
      corrupter::Engine::required_dram_bytes(alg->cfg),
      alg->cfg);

  if (alg->initialised) {
    alg->engine.set_audio_context(
        static_cast<float>(NT_globals.sampleRate),
        NT_globals.maxFramesPerStep);
  }

  return alg;
}

// ---------------------------------------------------------------------------
// parameterChanged
// ---------------------------------------------------------------------------

void parameterChanged(_NT_algorithm* self, int p) {
  CorrupterAlgorithm* alg = static_cast<CorrupterAlgorithm*>(self);

  // Handle action parameters
  if (p == P(DistingNtParamId::kParamResetEngine)) {
    if (alg->v[p] != 0) {
      alg->engine.reset();
      setParam(self, p, 0);
    }
    return;
  }
  if (p == P(DistingNtParamId::kParamRestoreDefaults)) {
    if (alg->v[p] != 0) {
      // Reset all main knob params to defaults
      setParam(self, P(DistingNtParamId::kParamTime), 500);
      setParam(self, P(DistingNtParamId::kParamRepeats), 500);
      setParam(self, P(DistingNtParamId::kParamMix), 1000);
      setParam(self, P(DistingNtParamId::kParamBend), 0);
      setParam(self, P(DistingNtParamId::kParamBreak), 0);
      setParam(self, P(DistingNtParamId::kParamCorrupt), 0);
      setParam(self, P(DistingNtParamId::kParamBendEnabled), 0);
      setParam(self, P(DistingNtParamId::kParamBreakEnabled), 0);
      setParam(self, P(DistingNtParamId::kParamFreezeEnabled), 0);
      setParam(self, P(DistingNtParamId::kParamMode), 0);
      setParam(self, P(DistingNtParamId::kParamCorruptAlgorithm), 0);
      setParam(self, p, 0);
    }
    return;
  }

  syncParameters(alg);
}

// ---------------------------------------------------------------------------
// step (audio processing)
// ---------------------------------------------------------------------------

void step(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
  CorrupterAlgorithm* alg = static_cast<CorrupterAlgorithm*>(self);
  if (!alg->initialised) return;

  const int numFrames = numFramesBy4 * 4;

  alg->engine.set_audio_context(
      static_cast<float>(NT_globals.sampleRate),
      NT_globals.maxFramesPerStep);

  float* in_l = busPtr(busFrames, numFrames, alg->in_l);
  float* in_r = busPtr(busFrames, numFrames, alg->in_r);
  float* out_l = busPtr(busFrames, numFrames, alg->out_l);
  float* out_r = busPtr(busFrames, numFrames, alg->out_r);

  // Use host work buffer for rendering
  float* scratch = NT_globals.workBuffer;
  if (!scratch ||
      NT_globals.workBufferSizeBytes < static_cast<uint32_t>(numFrames * 2 * sizeof(float))) {
    return;
  }
  float* render_l = scratch;
  float* render_r = scratch + numFrames;

  corrupter::AudioBlock audio;
  audio.in_l = in_l;
  audio.in_r = in_r;
  audio.out_l = render_l;
  audio.out_r = render_r;
  audio.frames = static_cast<uint32_t>(numFrames);

  corrupter::CvInputs cv;
  cv.time_v    = busPtr(busFrames, numFrames, alg->cv_bus[0]);
  cv.repeats_v = busPtr(busFrames, numFrames, alg->cv_bus[1]);
  cv.mix_v     = busPtr(busFrames, numFrames, alg->cv_bus[2]);
  cv.bend_v    = busPtr(busFrames, numFrames, alg->cv_bus[3]);
  cv.break_v   = busPtr(busFrames, numFrames, alg->cv_bus[4]);
  cv.corrupt_v = busPtr(busFrames, numFrames, alg->cv_bus[5]);

  corrupter::GateInputs gates;
  gates.bend_gate_v    = busPtr(busFrames, numFrames, alg->gate_bus[0]);
  gates.break_gate_v   = busPtr(busFrames, numFrames, alg->gate_bus[1]);
  gates.corrupt_gate_v = busPtr(busFrames, numFrames, alg->gate_bus[2]);
  gates.freeze_gate_v  = busPtr(busFrames, numFrames, alg->gate_bus[3]);
  gates.clock_gate_v   = busPtr(busFrames, numFrames, alg->gate_bus[4]);

  alg->engine.process(audio, cv, gates);

  // Copy rendered output to bus (replace or accumulate)
  if (out_l) {
    for (int i = 0; i < numFrames; ++i) {
      if (alg->out_l_replace)
        out_l[i] = render_l[i];
      else
        out_l[i] += render_l[i];
    }
  }
  if (out_r) {
    for (int i = 0; i < numFrames; ++i) {
      if (alg->out_r_replace)
        out_r[i] = render_r[i];
      else
        out_r[i] += render_r[i];
    }
  }

  // Update waveform display bins from output audio
  {
    const uint32_t buf_frames = static_cast<uint32_t>(alg->cfg.max_buffer_seconds) *
                                static_cast<uint32_t>(NT_globals.sampleRate);
    const uint32_t frames_per_bin = (buf_frames > 0)
        ? buf_frames / static_cast<uint32_t>(CorrupterAlgorithm::kWaveBins)
        : 1u;
    for (int i = 0; i < numFrames; ++i) {
      float s = render_l[i];
      if (s < 0.0f) s = -s;
      if (s > alg->wave_accum) alg->wave_accum = s;
      alg->wave_accum_count++;
      if (alg->wave_accum_count >= frames_per_bin) {
        int peak = static_cast<int>(alg->wave_accum * 8.0f);
        if (peak > 8) peak = 8;
        alg->wave_peaks[alg->wave_write_pos % CorrupterAlgorithm::kWaveBins] =
            static_cast<uint8_t>(peak);
        alg->wave_write_pos = (alg->wave_write_pos + 1) % CorrupterAlgorithm::kWaveBins;
        alg->wave_accum = 0.0f;
        alg->wave_accum_count = 0;
      }
    }
  }

  // Grab runtime info for display
  alg->engine.get_runtime_info(&alg->runtime_info);
}

// ---------------------------------------------------------------------------
// Custom UI
// ---------------------------------------------------------------------------

uint32_t hasCustomUi(_NT_algorithm* self) {
  (void)self;
  return kNT_potL | kNT_potC | kNT_potR |
         kNT_potButtonL | kNT_potButtonC | kNT_potButtonR |
         kNT_encoderL | kNT_encoderR |
         kNT_encoderButtonL | kNT_encoderButtonR;
}

void customUi(_NT_algorithm* self, const _NT_uiData& data) {
  CorrupterAlgorithm* alg = static_cast<CorrupterAlgorithm*>(self);

  // Pots -> Bend / Break / Corrupt amounts
  if (data.controls & kNT_potL) {
    int val = static_cast<int>(std::round(1000.0f * data.pots[0]));
    setParam(self, P(DistingNtParamId::kParamBend), static_cast<int16_t>(val));
  }
  if (data.controls & kNT_potC) {
    int val = static_cast<int>(std::round(1000.0f * data.pots[1]));
    setParam(self, P(DistingNtParamId::kParamBreak), static_cast<int16_t>(val));
  }
  if (data.controls & kNT_potR) {
    int val = static_cast<int>(std::round(1000.0f * data.pots[2]));
    setParam(self, P(DistingNtParamId::kParamCorrupt), static_cast<int16_t>(val));
  }

  // Detect rising edges for button presses
  uint16_t pressed = data.controls & ~data.lastButtons;

  // Pot presses: toggle Bend enable, Break enable, Freeze
  if (pressed & kNT_potButtonL) {
    int16_t cur = alg->v[P(DistingNtParamId::kParamBendEnabled)];
    setParam(self, P(DistingNtParamId::kParamBendEnabled), cur ? 0 : 1);
  }
  if (pressed & kNT_potButtonC) {
    int16_t cur = alg->v[P(DistingNtParamId::kParamBreakEnabled)];
    setParam(self, P(DistingNtParamId::kParamBreakEnabled), cur ? 0 : 1);
  }
  if (pressed & kNT_potButtonR) {
    int16_t cur = alg->v[P(DistingNtParamId::kParamFreezeEnabled)];
    setParam(self, P(DistingNtParamId::kParamFreezeEnabled), cur ? 0 : 1);
  }

  // Encoder presses: toggle Mode, cycle Corrupt Algorithm
  if (pressed & kNT_encoderButtonL) {
    int16_t cur = alg->v[P(DistingNtParamId::kParamMode)];
    setParam(self, P(DistingNtParamId::kParamMode), cur ? 0 : 1);
  }
  if (pressed & kNT_encoderButtonR) {
    int16_t cur = alg->v[P(DistingNtParamId::kParamCorruptAlgorithm)];
    int16_t next = (cur + 1) % 5;
    setParam(self, P(DistingNtParamId::kParamCorruptAlgorithm), next);
  }

  // Encoders: Time and Repeats (stepped, +-10 per click)
  if (data.encoders[0] != 0) {
    int16_t cur = alg->v[P(DistingNtParamId::kParamTime)];
    int16_t next = cur + data.encoders[0] * 10;
    if (next < 0) next = 0;
    if (next > 1000) next = 1000;
    setParam(self, P(DistingNtParamId::kParamTime), next);
  }
  if (data.encoders[1] != 0) {
    int16_t cur = alg->v[P(DistingNtParamId::kParamRepeats)];
    int16_t next = cur + data.encoders[1] * 10;
    if (next < 0) next = 0;
    if (next > 1000) next = 1000;
    setParam(self, P(DistingNtParamId::kParamRepeats), next);
  }
}

void setupUi(_NT_algorithm* self, _NT_float3& pots) {
  // Sync pot positions with current param values
  pots[0] = self->v[P(DistingNtParamId::kParamBend)] / 1000.0f;
  pots[1] = self->v[P(DistingNtParamId::kParamBreak)] / 1000.0f;
  pots[2] = self->v[P(DistingNtParamId::kParamCorrupt)] / 1000.0f;
}

// ---------------------------------------------------------------------------
// draw (256x64, 4-bit grayscale)
// ---------------------------------------------------------------------------

bool draw(_NT_algorithm* self) {
  CorrupterAlgorithm* alg = static_cast<CorrupterAlgorithm*>(self);
  const int16_t* v = alg->v;

  // Title bar (normal font 8px, baseline at y=8 places top at row 0)
  NT_drawText(0, 8, "CORRUPTER", 15, kNT_textLeft, kNT_textNormal);

  // Mode indicator (right side)
  bool macro = (v[P(DistingNtParamId::kParamMode)] == 0);
  NT_drawText(180, 8, macro ? "MACRO" : "MICRO", 12, kNT_textLeft, kNT_textNormal);

  // Clock source
  bool ext_clock = (v[P(DistingNtParamId::kParamClockSource)] != 0);
  NT_drawText(230, 8, ext_clock ? "EXT" : "INT", 8, kNT_textLeft, kNT_textNormal);

  // Separator line
  NT_drawShapeI(kNT_line, 0, 10, 255, 10, 6);

  // Status badges: BND / BRK / FRZ
  bool bend_on  = (v[P(DistingNtParamId::kParamBendEnabled)] != 0);
  bool break_on = (v[P(DistingNtParamId::kParamBreakEnabled)] != 0);
  bool freeze   = (v[P(DistingNtParamId::kParamFreezeEnabled)] != 0);

  // Draw badges as filled rectangles with text when active
  // Tiny font 5px: baseline at y=17 places top at row 12
  if (bend_on) {
    NT_drawShapeI(kNT_box, 0, 11, 30, 19, 15);
    NT_drawText(2, 17, "BND", 15, kNT_textLeft, kNT_textTiny);
  } else {
    NT_drawText(2, 17, "BND", 4, kNT_textLeft, kNT_textTiny);
  }

  if (break_on) {
    NT_drawShapeI(kNT_box, 35, 11, 65, 19, 15);
    NT_drawText(37, 17, "BRK", 15, kNT_textLeft, kNT_textTiny);
  } else {
    NT_drawText(37, 17, "BRK", 4, kNT_textLeft, kNT_textTiny);
  }

  if (freeze) {
    NT_drawShapeI(kNT_box, 70, 11, 100, 19, 15);
    NT_drawText(72, 17, "FRZ", 15, kNT_textLeft, kNT_textTiny);
  } else {
    NT_drawText(72, 17, "FRZ", 4, kNT_textLeft, kNT_textTiny);
  }

  // Corrupt algorithm name (right side, normal font baseline at 20 = top at 12)
  int algo = v[P(DistingNtParamId::kParamCorruptAlgorithm)];
  if (algo >= 0 && algo <= 4) {
    NT_drawText(180, 20, kEnumCorruptAlgo[algo], 10, kNT_textLeft, kNT_textNormal);
  }

  // Buffer visualization box
  NT_drawShapeI(kNT_box, 0, 22, 255, 42, 6);

  // Waveform display + write position
  {
    constexpr int box_l = 2, box_r = 253;
    constexpr int mid_y = 32;  // vertical center of box
    const int bins = CorrupterAlgorithm::kWaveBins;
    const int px_per_bin = (box_r - box_l) / bins;  // =1 for 128 bins in 251px, draw 2px wide

    for (int b = 0; b < bins; ++b) {
      // Read bins starting from oldest (write_pos is next to be written = oldest)
      int idx = (alg->wave_write_pos + b) % bins;
      int h = alg->wave_peaks[idx];  // 0-8
      if (h > 0) {
        int x = box_l + (b * (box_r - box_l)) / bins;
        NT_drawShapeI(kNT_line, x, mid_y - h, x, mid_y + h, 8);
        if (px_per_bin > 1)
          NT_drawShapeI(kNT_line, x + 1, mid_y - h, x + 1, mid_y + h, 8);
      }
    }

    // Write position marker
    int wp = (box_r - box_l) * static_cast<int>(alg->wave_write_pos) / bins + box_l;
    NT_drawShapeI(kNT_line, wp, 23, wp, 41, 15);
  }

  // Separator line
  NT_drawShapeI(kNT_line, 0, 43, 255, 43, 6);

  // Bottom: pot values
  {
    char buf[16];
    int bend_pct  = v[P(DistingNtParamId::kParamBend)] / 10;
    int break_pct = v[P(DistingNtParamId::kParamBreak)] / 10;
    int crpt_pct  = v[P(DistingNtParamId::kParamCorrupt)] / 10;

    // Tiny font 5px: baseline at 50 places top at row 45
    NT_drawText(0, 50, "Bend", 8, kNT_textLeft, kNT_textTiny);
    NT_intToString(buf, bend_pct);
    NT_drawText(30, 50, buf, 15, kNT_textLeft, kNT_textTiny);

    NT_drawText(90, 50, "Break", 8, kNT_textLeft, kNT_textTiny);
    NT_intToString(buf, break_pct);
    NT_drawText(122, 50, buf, 15, kNT_textLeft, kNT_textTiny);

    NT_drawText(180, 50, "Crpt", 8, kNT_textLeft, kNT_textTiny);
    NT_intToString(buf, crpt_pct);
    NT_drawText(210, 50, buf, 15, kNT_textLeft, kNT_textTiny);
  }

  // Bottom row: encoder-controlled values
  {
    char buf[16];
    int time_pct = v[P(DistingNtParamId::kParamTime)] / 10;
    int rpts_pct = v[P(DistingNtParamId::kParamRepeats)] / 10;

    // Tiny font 5px: baseline at 59 places top at row 54
    NT_drawText(0, 59, "Time", 8, kNT_textLeft, kNT_textTiny);
    // Time bar
    int time_w = time_pct * 40 / 100;
    NT_drawShapeI(kNT_rectangle, 28, 55, 28 + time_w, 59, 12);
    if (time_w < 40) {
      NT_drawShapeI(kNT_rectangle, 28 + time_w, 55, 68, 59, 3);
    }

    NT_drawText(90, 59, "Rpts", 8, kNT_textLeft, kNT_textTiny);
    NT_intToString(buf, rpts_pct);
    NT_drawText(122, 59, buf, 15, kNT_textLeft, kNT_textTiny);

    float gw = norm01(v[P(DistingNtParamId::kParamGlitchWindow)], 0, 1000);
    NT_drawText(180, 59, "GW", 8, kNT_textLeft, kNT_textTiny);
    NT_floatToString(buf, gw, 2);
    NT_drawText(200, 59, buf, 15, kNT_textLeft, kNT_textTiny);
  }

  return true;  // suppress standard parameter line
}

// ---------------------------------------------------------------------------
// Serialisation
// ---------------------------------------------------------------------------

void serialise(_NT_algorithm* self, _NT_jsonStream& stream) {
  CorrupterAlgorithm* alg = static_cast<CorrupterAlgorithm*>(self);
  if (!alg) return;

  stream.addMemberName("schema_version");
  stream.addNumber(1);

  stream.addMemberName("persistent");
  stream.openObject();
    stream.addMemberName("bend_enabled");
    stream.addBoolean(alg->persistent.bend_enabled);
    stream.addMemberName("break_enabled");
    stream.addBoolean(alg->persistent.break_enabled);
    stream.addMemberName("freeze_enabled");
    stream.addBoolean(alg->persistent.freeze_enabled);
    stream.addMemberName("macro_mode");
    stream.addBoolean(alg->persistent.macro_mode);
    stream.addMemberName("break_silence_mode");
    stream.addBoolean(alg->persistent.break_silence_mode);
    stream.addMemberName("unique_stereo_mode");
    stream.addBoolean(alg->persistent.unique_stereo_mode);
    stream.addMemberName("gate_latching");
    stream.addBoolean(alg->persistent.gate_latching);
    stream.addMemberName("freeze_latching");
    stream.addBoolean(alg->persistent.freeze_latching);
    stream.addMemberName("corrupt_gate_is_reset");
    stream.addBoolean(alg->persistent.corrupt_gate_is_reset);
    stream.addMemberName("corrupt_bank");
    stream.addNumber(static_cast<int>(alg->persistent.corrupt_bank));
    stream.addMemberName("corrupt_algorithm");
    stream.addNumber(static_cast<int>(alg->persistent.corrupt_algorithm));
    stream.addMemberName("glitch_window_01");
    stream.addNumber(alg->persistent.glitch_window_01);
  stream.closeObject();
}

bool deserialise(_NT_algorithm* self, _NT_jsonParse& parse) {
  CorrupterAlgorithm* alg = static_cast<CorrupterAlgorithm*>(self);
  if (!alg) return false;

  int members = 0;
  if (!parse.numberOfObjectMembers(members)) return false;

  for (int i = 0; i < members; ++i) {
    if (parse.matchName("schema_version")) {
      int version = 0;
      if (!parse.number(version)) return false;
    } else if (parse.matchName("persistent")) {
      int pMembers = 0;
      if (!parse.numberOfObjectMembers(pMembers)) return false;
      for (int j = 0; j < pMembers; ++j) {
        if (parse.matchName("bend_enabled")) {
          if (!parse.boolean(alg->persistent.bend_enabled)) return false;
        } else if (parse.matchName("break_enabled")) {
          if (!parse.boolean(alg->persistent.break_enabled)) return false;
        } else if (parse.matchName("freeze_enabled")) {
          if (!parse.boolean(alg->persistent.freeze_enabled)) return false;
        } else if (parse.matchName("macro_mode")) {
          if (!parse.boolean(alg->persistent.macro_mode)) return false;
        } else if (parse.matchName("break_silence_mode")) {
          if (!parse.boolean(alg->persistent.break_silence_mode)) return false;
        } else if (parse.matchName("unique_stereo_mode")) {
          if (!parse.boolean(alg->persistent.unique_stereo_mode)) return false;
        } else if (parse.matchName("gate_latching")) {
          if (!parse.boolean(alg->persistent.gate_latching)) return false;
        } else if (parse.matchName("freeze_latching")) {
          if (!parse.boolean(alg->persistent.freeze_latching)) return false;
        } else if (parse.matchName("corrupt_gate_is_reset")) {
          if (!parse.boolean(alg->persistent.corrupt_gate_is_reset)) return false;
        } else if (parse.matchName("corrupt_bank")) {
          int val = 0;
          if (!parse.number(val)) return false;
          alg->persistent.corrupt_bank = static_cast<corrupter::CorruptBank>(val);
        } else if (parse.matchName("corrupt_algorithm")) {
          int val = 0;
          if (!parse.number(val)) return false;
          alg->persistent.corrupt_algorithm = static_cast<corrupter::CorruptAlgorithm>(val);
        } else if (parse.matchName("glitch_window_01")) {
          float val = 0.0f;
          if (!parse.number(val)) return false;
          alg->persistent.glitch_window_01 = val;
        } else {
          if (!parse.skipMember()) return false;
        }
      }
      alg->engine.set_persistent_state(alg->persistent);
    } else {
      if (!parse.skipMember()) return false;
    }
  }

  return true;
}

// ---------------------------------------------------------------------------
// parameterString (for Confirm-type params)
// ---------------------------------------------------------------------------

int parameterString(_NT_algorithm* self, int p, int val, char* buff) {
  (void)self;
  if (p == P(DistingNtParamId::kParamResetEngine)) {
    if (val == 0) {
      strcpy(buff, "Ready");
      return 5;
    } else {
      strcpy(buff, "RESET?");
      return 6;
    }
  }
  if (p == P(DistingNtParamId::kParamRestoreDefaults)) {
    if (val == 0) {
      strcpy(buff, "Ready");
      return 5;
    } else {
      strcpy(buff, "RESTORE?");
      return 8;
    }
  }
  return 0;
}

// ---------------------------------------------------------------------------
// Factory + pluginEntry
// ---------------------------------------------------------------------------

static const _NT_factory factory = {
  .guid = NT_MULTICHAR('T', 'h', 'C', 'o'),
  .name = "Corrupter",
  .description = "Data Bender-style circuit-bent stereo buffer effect",
  .numSpecifications = ARRAY_SIZE(specifications),
  .specifications = specifications,
  .calculateStaticRequirements = nullptr,
  .initialise = nullptr,
  .calculateRequirements = calculateRequirements,
  .construct = construct,
  .parameterChanged = parameterChanged,
  .step = step,
  .draw = draw,
  .midiRealtime = nullptr,
  .midiMessage = nullptr,
  .tags = kNT_tagEffect | kNT_tagDelay,
  .hasCustomUi = hasCustomUi,
  .customUi = customUi,
  .setupUi = setupUi,
  .serialise = serialise,
  .deserialise = deserialise,
  .midiSysEx = nullptr,
  .parameterUiPrefix = nullptr,
  .parameterString = parameterString,
};

}  // namespace

extern "C" uintptr_t pluginEntry(_NT_selector selector, uint32_t data) {
  switch (selector) {
    case kNT_selector_version:
      return kNT_apiVersionCurrent;
    case kNT_selector_numFactories:
      return 1;
    case kNT_selector_factoryInfo:
      return (data == 0) ? reinterpret_cast<uintptr_t>(&factory) : 0;
  }
  return 0;
}
