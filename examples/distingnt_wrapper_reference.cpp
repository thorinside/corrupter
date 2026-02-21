// Reference wrapper for integrating libcorrupter_dsp with distingNT_API.
//
// Usage:
// 1) Copy this file into a build tree that has distingNT_API include path configured.
// 2) Link with libcorrupter_dsp.a.
// 3) Fill any TODOs you want for final product behavior/UI polish.

#include <new>

#include <distingnt/api.h>
#include <distingnt/serialisation.h>

#include "corrupter_dsp/engine.h"
#include "corrupter_dsp/parameter_ids.h"

namespace {

using corrupter::DistingNtParamId;

static const char* kEnumBinary[] = {"Off", "On", nullptr};
static const char* kEnumMode[] = {"Macro", "Micro", nullptr};
static const char* kEnumBreakMicro[] = {"Traverse", "Silence", nullptr};
static const char* kEnumClockSource[] = {"Internal", "External", nullptr};
static const char* kEnumStereo[] = {"Shared", "Unique", nullptr};
static const char* kEnumGateMode[] = {"Latching", "Momentary", nullptr};
static const char* kEnumCorruptGate[] = {"Toggle", "Clock Reset", nullptr};
static const char* kEnumCorruptBank[] = {"Legacy", "Expanded", nullptr};
static const char* kEnumCorruptAlgo[] = {"Decimate", "Dropout", "Destroy", "DJ Filter", "Vinyl Sim", nullptr};

constexpr int P(DistingNtParamId id) { return static_cast<int>(id); }

float norm01(int16_t v, int16_t lo, int16_t hi) {
  if (hi <= lo) {
    return 0.0f;
  }
  const float x = static_cast<float>(v - lo) / static_cast<float>(hi - lo);
  return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

int32_t clampBus(int32_t v, bool allow_zero) {
  const int32_t lo = allow_zero ? 0 : 1;
  if (v < lo) return lo;
  if (v > kNT_lastBus) return kNT_lastBus;
  return v;
}

float* busPtr(float* bus_frames, int num_frames, int bus_sel) {
  if (bus_sel <= 0) {
    return nullptr;
  }
  return bus_frames + (bus_sel - 1) * num_frames;
}

struct CorrupterAlgorithm : public _NT_algorithm {
  corrupter::Engine engine;
  corrupter::EngineConfig cfg;

  corrupter::KnobState knobs;
  corrupter::PersistentState persistent;

  int in_l = 1;
  int in_r = 2;
  int out_l = 13;
  int out_r = 14;
  bool out_l_replace = true;
  bool out_r_replace = true;

  int cv_time = 0;
  int cv_repeats = 0;
  int cv_mix = 0;
  int cv_bend = 0;
  int cv_break = 0;
  int cv_corrupt = 0;

  int gate_bend = 0;
  int gate_break = 0;
  int gate_corrupt = 0;
  int gate_freeze = 0;
  int gate_clock = 0;

  uint32_t fixed_seed = 1;

  bool initialised = false;
};

static const _NT_parameter parameters[] = {
    NT_PARAMETER_AUDIO_INPUT("In L", 1, 1)
    NT_PARAMETER_AUDIO_INPUT("In R", 1, 2)
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE("Out L", 1, 13)
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE("Out R", 1, 14)

    {.name = "Time", .min = 0, .max = 1000, .def = 500, .unit = kNT_unitPercent, .scaling = kNT_scaling10, .enumStrings = nullptr},
    {.name = "Repeats", .min = 0, .max = 1000, .def = 0, .unit = kNT_unitPercent, .scaling = kNT_scaling10, .enumStrings = nullptr},
    {.name = "Mix", .min = 0, .max = 1000, .def = 0, .unit = kNT_unitPercent, .scaling = kNT_scaling10, .enumStrings = nullptr},
    {.name = "Bend", .min = 0, .max = 1000, .def = 0, .unit = kNT_unitPercent, .scaling = kNT_scaling10, .enumStrings = nullptr},
    {.name = "Break", .min = 0, .max = 1000, .def = 0, .unit = kNT_unitPercent, .scaling = kNT_scaling10, .enumStrings = nullptr},
    {.name = "Corrupt", .min = 0, .max = 1000, .def = 0, .unit = kNT_unitPercent, .scaling = kNT_scaling10, .enumStrings = nullptr},

    {.name = "Bend CV Attn", .min = 0, .max = 1000, .def = 1000, .unit = kNT_unitPercent, .scaling = kNT_scaling10, .enumStrings = nullptr},
    {.name = "Break CV Attn", .min = 0, .max = 1000, .def = 1000, .unit = kNT_unitPercent, .scaling = kNT_scaling10, .enumStrings = nullptr},
    {.name = "Corrupt CV Attn", .min = 0, .max = 1000, .def = 1000, .unit = kNT_unitPercent, .scaling = kNT_scaling10, .enumStrings = nullptr},

    {.name = "Mode", .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = kEnumMode},
    {.name = "Break Micro", .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = kEnumBreakMicro},
    {.name = "Bend On", .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = kEnumBinary},
    {.name = "Break On", .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = kEnumBinary},
    {.name = "Freeze", .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = kEnumBinary},
    {.name = "Clock Src", .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = kEnumClockSource},
    {.name = "Stereo", .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = kEnumStereo},
    {.name = "Gate Mode", .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = kEnumGateMode},
    {.name = "Freeze Gate", .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = kEnumGateMode},
    {.name = "Corrupt Gate", .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = kEnumCorruptGate},
    {.name = "Corrupt Bank", .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = kEnumCorruptBank},
    {.name = "Corrupt Algo", .min = 0, .max = 4, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = kEnumCorruptAlgo},
    {.name = "Glitch Win", .min = 0, .max = 1000, .def = 20, .unit = kNT_unitPercent, .scaling = kNT_scaling10, .enumStrings = nullptr},

    NT_PARAMETER_CV_INPUT("Time CV In", 0, 0)
    NT_PARAMETER_CV_INPUT("Repeats CV In", 0, 0)
    NT_PARAMETER_CV_INPUT("Mix CV In", 0, 0)
    NT_PARAMETER_CV_INPUT("Bend CV In", 0, 0)
    NT_PARAMETER_CV_INPUT("Break CV In", 0, 0)
    NT_PARAMETER_CV_INPUT("Corrupt CV In", 0, 0)

    NT_PARAMETER_CV_INPUT("Bend Gate In", 0, 0)
    NT_PARAMETER_CV_INPUT("Break Gate In", 0, 0)
    NT_PARAMETER_CV_INPUT("Corrupt Gate In", 0, 0)
    NT_PARAMETER_CV_INPUT("Freeze Gate In", 0, 0)
    NT_PARAMETER_CV_INPUT("Clock In", 0, 0)

    {.name = "Fixed Seed", .min = 0, .max = 65535, .def = 1, .unit = kNT_unitNone, .scaling = 0, .enumStrings = nullptr},
    {.name = "Buffer Seconds", .min = 5, .max = 60, .def = 60, .unit = kNT_unitSeconds, .scaling = 0, .enumStrings = nullptr},
};

// Keep pages lightweight; full production page map is in docs/distingnt-parameter-spec.md.
static const uint8_t page_main[] = {
    P(DistingNtParamId::kParamTime), P(DistingNtParamId::kParamRepeats),
    P(DistingNtParamId::kParamMix), P(DistingNtParamId::kParamBend),
    P(DistingNtParamId::kParamBreak), P(DistingNtParamId::kParamCorrupt),
};

static const uint8_t page_modes[] = {
    P(DistingNtParamId::kParamMode),        P(DistingNtParamId::kParamBreakMicroMode),
    P(DistingNtParamId::kParamBendEnabled), P(DistingNtParamId::kParamBreakEnabled),
    P(DistingNtParamId::kParamFreezeEnabled), P(DistingNtParamId::kParamClockSource),
    P(DistingNtParamId::kParamStereoMode), P(DistingNtParamId::kParamGateMode),
    P(DistingNtParamId::kParamFreezeGateMode), P(DistingNtParamId::kParamCorruptGateMode),
};

static const uint8_t page_routing_audio[] = {
    P(DistingNtParamId::kParamAudioInL), P(DistingNtParamId::kParamAudioInR),
    P(DistingNtParamId::kParamAudioOutL), P(DistingNtParamId::kParamAudioOutLMode),
    P(DistingNtParamId::kParamAudioOutR), P(DistingNtParamId::kParamAudioOutRMode),
};

static const uint8_t page_routing_cv[] = {
    P(DistingNtParamId::kParamTimeCvInput), P(DistingNtParamId::kParamRepeatsCvInput),
    P(DistingNtParamId::kParamMixCvInput), P(DistingNtParamId::kParamBendCvInput),
    P(DistingNtParamId::kParamBreakCvInput), P(DistingNtParamId::kParamCorruptCvInput),
    P(DistingNtParamId::kParamBendCvAttn), P(DistingNtParamId::kParamBreakCvAttn),
    P(DistingNtParamId::kParamCorruptCvAttn),
};

static const uint8_t page_routing_gate[] = {
    P(DistingNtParamId::kParamBendGateInput), P(DistingNtParamId::kParamBreakGateInput),
    P(DistingNtParamId::kParamCorruptGateInput), P(DistingNtParamId::kParamFreezeGateInput),
    P(DistingNtParamId::kParamClockGateInput),
};

static const _NT_parameterPage pages[] = {
    {.name = "Main", .numParams = ARRAY_SIZE(page_main), .group = 1, .params = page_main},
    {.name = "Modes", .numParams = ARRAY_SIZE(page_modes), .group = 2, .params = page_modes},
    {.name = "Routing Audio", .numParams = ARRAY_SIZE(page_routing_audio), .group = 4, .params = page_routing_audio},
    {.name = "Routing CV", .numParams = ARRAY_SIZE(page_routing_cv), .group = 4, .params = page_routing_cv},
    {.name = "Routing Gates", .numParams = ARRAY_SIZE(page_routing_gate), .group = 4, .params = page_routing_gate},
};

static const _NT_parameterPages parameter_pages = {
    .numPages = ARRAY_SIZE(pages),
    .pages = pages,
};

void calculateRequirements(_NT_algorithmRequirements& req, const int32_t* specs) {
  (void)specs;
  req.numParameters = ARRAY_SIZE(parameters);
  req.sram = sizeof(CorrupterAlgorithm);

  corrupter::EngineConfig cfg;
  cfg.sample_rate_hz = static_cast<float>(NT_globals.sampleRate);
  cfg.max_block_frames = NT_globals.maxFramesPerStep;
  cfg.max_buffer_seconds = 60.0f;
  cfg.random_seed = 1;
  req.dram = static_cast<uint32_t>(corrupter::Engine::required_dram_bytes(cfg));

  req.dtc = 0;
  req.itc = 0;
}

_NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs, const _NT_algorithmRequirements& req,
                         const int32_t* specs) {
  (void)req;
  (void)specs;

  CorrupterAlgorithm* alg = new (ptrs.sram) CorrupterAlgorithm();
  alg->parameters = parameters;
  alg->parameterPages = &parameter_pages;

  alg->cfg.sample_rate_hz = static_cast<float>(NT_globals.sampleRate);
  alg->cfg.max_block_frames = NT_globals.maxFramesPerStep;
  alg->cfg.max_buffer_seconds = static_cast<float>(alg->v[P(DistingNtParamId::kParamBufferSeconds)]);
  alg->cfg.random_seed = 1;

  alg->initialised = alg->engine.initialise(ptrs.dram,
                                             corrupter::Engine::required_dram_bytes(alg->cfg),
                                             alg->cfg);
  return alg;
}

void syncParameters(CorrupterAlgorithm* alg) {
  if (!alg) return;

  const int16_t* v = alg->v;

  alg->in_l = clampBus(v[P(DistingNtParamId::kParamAudioInL)], false);
  alg->in_r = clampBus(v[P(DistingNtParamId::kParamAudioInR)], false);
  alg->out_l = clampBus(v[P(DistingNtParamId::kParamAudioOutL)], false);
  alg->out_l_replace = (v[P(DistingNtParamId::kParamAudioOutLMode)] != 0);
  alg->out_r = clampBus(v[P(DistingNtParamId::kParamAudioOutR)], false);
  alg->out_r_replace = (v[P(DistingNtParamId::kParamAudioOutRMode)] != 0);

  alg->cv_time = clampBus(v[P(DistingNtParamId::kParamTimeCvInput)], true);
  alg->cv_repeats = clampBus(v[P(DistingNtParamId::kParamRepeatsCvInput)], true);
  alg->cv_mix = clampBus(v[P(DistingNtParamId::kParamMixCvInput)], true);
  alg->cv_bend = clampBus(v[P(DistingNtParamId::kParamBendCvInput)], true);
  alg->cv_break = clampBus(v[P(DistingNtParamId::kParamBreakCvInput)], true);
  alg->cv_corrupt = clampBus(v[P(DistingNtParamId::kParamCorruptCvInput)], true);

  alg->gate_bend = clampBus(v[P(DistingNtParamId::kParamBendGateInput)], true);
  alg->gate_break = clampBus(v[P(DistingNtParamId::kParamBreakGateInput)], true);
  alg->gate_corrupt = clampBus(v[P(DistingNtParamId::kParamCorruptGateInput)], true);
  alg->gate_freeze = clampBus(v[P(DistingNtParamId::kParamFreezeGateInput)], true);
  alg->gate_clock = clampBus(v[P(DistingNtParamId::kParamClockGateInput)], true);

  alg->knobs.time_01 = norm01(v[P(DistingNtParamId::kParamTime)], 0, 1000);
  alg->knobs.repeats_01 = norm01(v[P(DistingNtParamId::kParamRepeats)], 0, 1000);
  alg->knobs.mix_01 = norm01(v[P(DistingNtParamId::kParamMix)], 0, 1000);
  alg->knobs.bend_01 = norm01(v[P(DistingNtParamId::kParamBend)], 0, 1000);
  alg->knobs.break_01 = norm01(v[P(DistingNtParamId::kParamBreak)], 0, 1000);
  alg->knobs.corrupt_01 = norm01(v[P(DistingNtParamId::kParamCorrupt)], 0, 1000);

  alg->knobs.bend_cv_attn_01 = norm01(v[P(DistingNtParamId::kParamBendCvAttn)], 0, 1000);
  alg->knobs.break_cv_attn_01 = norm01(v[P(DistingNtParamId::kParamBreakCvAttn)], 0, 1000);
  alg->knobs.corrupt_cv_attn_01 = norm01(v[P(DistingNtParamId::kParamCorruptCvAttn)], 0, 1000);

  alg->persistent.macro_mode = (v[P(DistingNtParamId::kParamMode)] == 0);
  alg->persistent.break_silence_mode = (v[P(DistingNtParamId::kParamBreakMicroMode)] != 0);
  alg->persistent.bend_enabled = (v[P(DistingNtParamId::kParamBendEnabled)] != 0);
  alg->persistent.break_enabled = (v[P(DistingNtParamId::kParamBreakEnabled)] != 0);
  alg->persistent.freeze_enabled = (v[P(DistingNtParamId::kParamFreezeEnabled)] != 0);
  alg->persistent.unique_stereo_mode = (v[P(DistingNtParamId::kParamStereoMode)] != 0);
  alg->persistent.gate_latching = (v[P(DistingNtParamId::kParamGateMode)] == 0);
  alg->persistent.freeze_latching = (v[P(DistingNtParamId::kParamFreezeGateMode)] == 0);
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

void parameterChanged(_NT_algorithm* self, int p) {
  (void)p;
  syncParameters(static_cast<CorrupterAlgorithm*>(self));
}

void step(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
  CorrupterAlgorithm* alg = static_cast<CorrupterAlgorithm*>(self);
  if (!alg || !alg->initialised) {
    return;
  }

  const int numFrames = numFramesBy4 * 4;

  float* in_l = busPtr(busFrames, numFrames, alg->in_l);
  float* in_r = busPtr(busFrames, numFrames, alg->in_r);
  float* out_l = busPtr(busFrames, numFrames, alg->out_l);
  float* out_r = busPtr(busFrames, numFrames, alg->out_r);

  // Use host work buffer to avoid dynamic allocation.
  float* scratch = NT_globals.workBuffer;
  if (!scratch || NT_globals.workBufferSizeBytes < static_cast<uint32_t>(numFrames * 2 * sizeof(float))) {
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
  cv.time_v = busPtr(busFrames, numFrames, alg->cv_time);
  cv.repeats_v = busPtr(busFrames, numFrames, alg->cv_repeats);
  cv.mix_v = busPtr(busFrames, numFrames, alg->cv_mix);
  cv.bend_v = busPtr(busFrames, numFrames, alg->cv_bend);
  cv.break_v = busPtr(busFrames, numFrames, alg->cv_break);
  cv.corrupt_v = busPtr(busFrames, numFrames, alg->cv_corrupt);

  corrupter::GateInputs gates;
  gates.bend_gate_v = busPtr(busFrames, numFrames, alg->gate_bend);
  gates.break_gate_v = busPtr(busFrames, numFrames, alg->gate_break);
  gates.corrupt_gate_v = busPtr(busFrames, numFrames, alg->gate_corrupt);
  gates.freeze_gate_v = busPtr(busFrames, numFrames, alg->gate_freeze);
  gates.clock_gate_v = busPtr(busFrames, numFrames, alg->gate_clock);

  alg->engine.process(audio, cv, gates);

  if (!out_l || !out_r) {
    return;
  }

  for (int i = 0; i < numFrames; ++i) {
    if (alg->out_l_replace) {
      out_l[i] = render_l[i];
    } else {
      out_l[i] += render_l[i];
    }

    if (alg->out_r_replace) {
      out_r[i] = render_r[i];
    } else {
      out_r[i] += render_r[i];
    }
  }
}

void serialise(_NT_algorithm* self, _NT_jsonStream& stream) {
  CorrupterAlgorithm* alg = static_cast<CorrupterAlgorithm*>(self);
  if (!alg) return;

  stream.addMemberName("schema_version");
  stream.addNumber(1);

  stream.addMemberName("fixed_seed");
  stream.addNumber(static_cast<int>(alg->fixed_seed));
}

bool deserialise(_NT_algorithm* self, _NT_jsonParse& parse) {
  CorrupterAlgorithm* alg = static_cast<CorrupterAlgorithm*>(self);
  if (!alg) return false;

  int members = 0;
  if (!parse.numberOfObjectMembers(members)) {
    return false;
  }

  for (int i = 0; i < members; ++i) {
    if (parse.matchName("schema_version")) {
      int version = 0;
      if (!parse.number(version)) return false;
    } else if (parse.matchName("fixed_seed")) {
      int value = 1;
      if (!parse.number(value)) return false;
      alg->fixed_seed = static_cast<uint32_t>(value < 0 ? 0 : value);
    } else {
      if (!parse.skipMember()) return false;
    }
  }

  return true;
}

static const _NT_factory factory = {
    .guid = NT_MULTICHAR('C', 'r', 'p', 'T'),
    .name = "Corrupter",
    .description = "Circuit-bent stereo buffer effect",
    .numSpecifications = 0,
    .specifications = nullptr,
    .calculateStaticRequirements = nullptr,
    .initialise = nullptr,
    .calculateRequirements = calculateRequirements,
    .construct = construct,
    .parameterChanged = parameterChanged,
    .step = step,
    .draw = nullptr,
    .midiRealtime = nullptr,
    .midiMessage = nullptr,
    .tags = kNT_tagEffect,
    .hasCustomUi = nullptr,
    .customUi = nullptr,
    .setupUi = nullptr,
    .serialise = serialise,
    .deserialise = deserialise,
    .midiSysEx = nullptr,
    .parameterUiPrefix = nullptr,
    .parameterString = nullptr,
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
