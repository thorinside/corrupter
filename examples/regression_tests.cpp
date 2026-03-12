#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "corrupter_dsp/c_api.h"
#include "corrupter_dsp/engine.h"
#include "internal/clock_engine.h"
#include "internal/corrupt_engine.h"

namespace {

constexpr double kPi = 3.14159265358979323846;

struct StereoBuffers {
  std::vector<float> in_l;
  std::vector<float> in_r;
  std::vector<float> out_l;
  std::vector<float> out_r;
};

struct ModulationBuffers {
  std::vector<float> time_cv;
  std::vector<float> repeats_cv;
  std::vector<float> mix_cv;
  std::vector<float> bend_cv;
  std::vector<float> break_cv;
  std::vector<float> corrupt_cv;

  std::vector<float> bend_gate;
  std::vector<float> break_gate;
  std::vector<float> corrupt_gate;
  std::vector<float> freeze_gate;
  std::vector<float> clock_gate;
};

StereoBuffers MakeInput(uint32_t frames) {
  StereoBuffers b;
  b.in_l.resize(frames);
  b.in_r.resize(frames);
  b.out_l.resize(frames);
  b.out_r.resize(frames);

  for (uint32_t i = 0; i < frames; ++i) {
    const float t = static_cast<float>(i);
    b.in_l[i] = 0.5f * std::sin(0.0123f * t) + 0.35f * std::sin(0.043f * t);
    b.in_r[i] = 0.6f * std::cos(0.0091f * t) - 0.22f * std::sin(0.037f * t);
  }
  return b;
}

uint64_t HashF32(const std::vector<float>& a, const std::vector<float>& b) {
  const uint8_t* pa = reinterpret_cast<const uint8_t*>(a.data());
  const uint8_t* pb = reinterpret_cast<const uint8_t*>(b.data());
  const size_t na = a.size() * sizeof(float);
  const size_t nb = b.size() * sizeof(float);

  uint64_t h = 1469598103934665603ull;
  for (size_t i = 0; i < na; ++i) {
    h ^= static_cast<uint64_t>(pa[i]);
    h *= 1099511628211ull;
  }
  for (size_t i = 0; i < nb; ++i) {
    h ^= static_cast<uint64_t>(pb[i]);
    h *= 1099511628211ull;
  }
  return h;
}

bool ExactEqual(const std::vector<float>& a, const std::vector<float>& b) {
  return a.size() == b.size() &&
         std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

bool NearEqual(float a, float b, float eps = 1e-7f) {
  return std::fabs(a - b) <= eps;
}

bool NearRatio(float a, float b, float rel_tol = 0.08f) {
  if (b == 0.0f) {
    return false;
  }
  const float ratio = a / b;
  return std::fabs(ratio - 1.0f) <= rel_tol;
}

float ToneAmplitude(double sin_acc, double cos_acc, uint32_t n) {
  if (n == 0u) {
    return 0.0f;
  }
  const double scale = 2.0 / static_cast<double>(n);
  return static_cast<float>(scale * std::sqrt(sin_acc * sin_acc + cos_acc * cos_acc));
}

const float* VecPtr(const std::vector<float>& v, uint32_t frames) {
  return (v.size() == frames) ? v.data() : nullptr;
}

struct Scenario {
  corrupter::EngineConfig cfg;
  corrupter::PersistentState state;
  corrupter::KnobState knobs;
  bool clock_mode_internal = true;
  uint32_t frames = 0;
};

StereoBuffers RunScenarioCpp(const Scenario& s, const ModulationBuffers* mod = nullptr) {
  StereoBuffers buf = MakeInput(s.frames);

  const size_t dram_bytes = corrupter::Engine::required_dram_bytes(s.cfg);
  std::vector<uint8_t> dram(dram_bytes);
  corrupter::Engine engine;
  if (!engine.initialise(dram.data(), dram.size(), s.cfg)) {
    throw std::runtime_error("engine.init failed");
  }

  engine.set_persistent_state(s.state);
  engine.set_knobs(s.knobs);
  engine.set_clock_mode_internal(s.clock_mode_internal);
  engine.set_audio_context(s.cfg.sample_rate_hz, s.cfg.max_block_frames);

  corrupter::AudioBlock audio;
  audio.in_l = buf.in_l.data();
  audio.in_r = buf.in_r.data();
  audio.out_l = buf.out_l.data();
  audio.out_r = buf.out_r.data();
  audio.frames = s.frames;

  corrupter::CvInputs cv;
  corrupter::GateInputs gates;
  if (mod) {
    cv.time_v = VecPtr(mod->time_cv, s.frames);
    cv.repeats_v = VecPtr(mod->repeats_cv, s.frames);
    cv.mix_v = VecPtr(mod->mix_cv, s.frames);
    cv.bend_v = VecPtr(mod->bend_cv, s.frames);
    cv.break_v = VecPtr(mod->break_cv, s.frames);
    cv.corrupt_v = VecPtr(mod->corrupt_cv, s.frames);

    gates.bend_gate_v = VecPtr(mod->bend_gate, s.frames);
    gates.break_gate_v = VecPtr(mod->break_gate, s.frames);
    gates.corrupt_gate_v = VecPtr(mod->corrupt_gate, s.frames);
    gates.freeze_gate_v = VecPtr(mod->freeze_gate, s.frames);
    gates.clock_gate_v = VecPtr(mod->clock_gate, s.frames);
  }

  engine.process(audio, cv, gates);
  return buf;
}

const float* PtrAt(const std::vector<float>& v, uint32_t offset, uint32_t full_frames) {
  if (v.size() != full_frames || offset >= full_frames) {
    return nullptr;
  }
  return v.data() + offset;
}

StereoBuffers RunScenarioCppChunked(const Scenario& s, const ModulationBuffers* mod,
                                    const std::vector<uint32_t>& chunk_pattern) {
  StereoBuffers buf = MakeInput(s.frames);
  if (chunk_pattern.empty()) {
    throw std::runtime_error("chunk_pattern must not be empty");
  }

  const size_t dram_bytes = corrupter::Engine::required_dram_bytes(s.cfg);
  std::vector<uint8_t> dram(dram_bytes);
  corrupter::Engine engine;
  if (!engine.initialise(dram.data(), dram.size(), s.cfg)) {
    throw std::runtime_error("engine.init failed");
  }

  engine.set_persistent_state(s.state);
  engine.set_knobs(s.knobs);
  engine.set_clock_mode_internal(s.clock_mode_internal);
  engine.set_audio_context(s.cfg.sample_rate_hz, s.cfg.max_block_frames);

  uint32_t offset = 0;
  size_t pattern_i = 0;
  while (offset < s.frames) {
    const uint32_t remaining = s.frames - offset;
    const uint32_t n = std::min(remaining, chunk_pattern[pattern_i % chunk_pattern.size()]);
    ++pattern_i;

    corrupter::AudioBlock audio;
    audio.in_l = buf.in_l.data() + offset;
    audio.in_r = buf.in_r.data() + offset;
    audio.out_l = buf.out_l.data() + offset;
    audio.out_r = buf.out_r.data() + offset;
    audio.frames = n;

    corrupter::CvInputs cv;
    corrupter::GateInputs gates;
    if (mod) {
      cv.time_v = PtrAt(mod->time_cv, offset, s.frames);
      cv.repeats_v = PtrAt(mod->repeats_cv, offset, s.frames);
      cv.mix_v = PtrAt(mod->mix_cv, offset, s.frames);
      cv.bend_v = PtrAt(mod->bend_cv, offset, s.frames);
      cv.break_v = PtrAt(mod->break_cv, offset, s.frames);
      cv.corrupt_v = PtrAt(mod->corrupt_cv, offset, s.frames);

      gates.bend_gate_v = PtrAt(mod->bend_gate, offset, s.frames);
      gates.break_gate_v = PtrAt(mod->break_gate, offset, s.frames);
      gates.corrupt_gate_v = PtrAt(mod->corrupt_gate, offset, s.frames);
      gates.freeze_gate_v = PtrAt(mod->freeze_gate, offset, s.frames);
      gates.clock_gate_v = PtrAt(mod->clock_gate, offset, s.frames);
    }

    engine.process(audio, cv, gates);
    offset += n;
  }

  return buf;
}

corrupter::RuntimeInfo RunScenarioCppForInfo(const Scenario& s, const ModulationBuffers* mod) {
  StereoBuffers buf = MakeInput(s.frames);
  const size_t dram_bytes = corrupter::Engine::required_dram_bytes(s.cfg);
  std::vector<uint8_t> dram(dram_bytes);
  corrupter::Engine engine;
  if (!engine.initialise(dram.data(), dram.size(), s.cfg)) {
    throw std::runtime_error("engine.init failed");
  }

  engine.set_persistent_state(s.state);
  engine.set_knobs(s.knobs);
  engine.set_clock_mode_internal(s.clock_mode_internal);
  engine.set_audio_context(s.cfg.sample_rate_hz, s.cfg.max_block_frames);

  corrupter::AudioBlock audio;
  audio.in_l = buf.in_l.data();
  audio.in_r = buf.in_r.data();
  audio.out_l = buf.out_l.data();
  audio.out_r = buf.out_r.data();
  audio.frames = s.frames;

  corrupter::CvInputs cv;
  corrupter::GateInputs gates;
  if (mod) {
    cv.time_v = VecPtr(mod->time_cv, s.frames);
    cv.repeats_v = VecPtr(mod->repeats_cv, s.frames);
    cv.mix_v = VecPtr(mod->mix_cv, s.frames);
    cv.bend_v = VecPtr(mod->bend_cv, s.frames);
    cv.break_v = VecPtr(mod->break_cv, s.frames);
    cv.corrupt_v = VecPtr(mod->corrupt_cv, s.frames);

    gates.bend_gate_v = VecPtr(mod->bend_gate, s.frames);
    gates.break_gate_v = VecPtr(mod->break_gate, s.frames);
    gates.corrupt_gate_v = VecPtr(mod->corrupt_gate, s.frames);
    gates.freeze_gate_v = VecPtr(mod->freeze_gate, s.frames);
    gates.clock_gate_v = VecPtr(mod->clock_gate, s.frames);
  }

  engine.process(audio, cv, gates);

  corrupter::RuntimeInfo info{};
  if (!engine.get_runtime_info(&info)) {
    throw std::runtime_error("runtime info unavailable");
  }
  return info;
}

StereoBuffers RunScenarioC(const Scenario& s, const ModulationBuffers* mod = nullptr) {
  StereoBuffers buf = MakeInput(s.frames);

  const size_t engine_bytes = corrupter_engine_sizeof();
  std::vector<uint8_t> engine_mem(engine_bytes);

  if (!corrupter_engine_construct(engine_mem.data(), engine_mem.size())) {
    throw std::runtime_error("c_api construct failed");
  }

  corrupter_engine_config_t cfg{};
  cfg.sample_rate_hz = s.cfg.sample_rate_hz;
  cfg.max_supported_sample_rate_hz = s.cfg.max_supported_sample_rate_hz;
  cfg.max_block_frames = s.cfg.max_block_frames;
  cfg.max_buffer_seconds = s.cfg.max_buffer_seconds;
  cfg.random_seed = s.cfg.random_seed;

  const size_t dram_bytes = corrupter_engine_required_dram_bytes(&cfg);
  std::vector<uint8_t> dram(dram_bytes);

  if (!corrupter_engine_initialise(engine_mem.data(), dram.data(), dram.size(), &cfg)) {
    throw std::runtime_error("c_api init failed");
  }

  corrupter_persistent_state_t state{};
  state.bend_enabled = s.state.bend_enabled ? 1 : 0;
  state.break_enabled = s.state.break_enabled ? 1 : 0;
  state.freeze_enabled = s.state.freeze_enabled ? 1 : 0;
  state.macro_mode = s.state.macro_mode ? 1 : 0;
  state.break_silence_mode = s.state.break_silence_mode ? 1 : 0;
  state.unique_stereo_mode = s.state.unique_stereo_mode ? 1 : 0;
  state.gate_latching = s.state.gate_latching ? 1 : 0;
  state.freeze_latching = s.state.freeze_latching ? 1 : 0;
  state.corrupt_gate_is_reset = s.state.corrupt_gate_is_reset ? 1 : 0;
  state.corrupt_bank = static_cast<uint8_t>(s.state.corrupt_bank);
  state.corrupt_algorithm = static_cast<uint8_t>(s.state.corrupt_algorithm);
  state.glitch_window_01 = s.state.glitch_window_01;
  corrupter_engine_set_persistent_state(engine_mem.data(), &state);

  corrupter_knob_state_t knobs{};
  knobs.time_01 = s.knobs.time_01;
  knobs.repeats_01 = s.knobs.repeats_01;
  knobs.mix_01 = s.knobs.mix_01;
  knobs.bend_01 = s.knobs.bend_01;
  knobs.break_01 = s.knobs.break_01;
  knobs.corrupt_01 = s.knobs.corrupt_01;
  knobs.bend_cv_attn_01 = s.knobs.bend_cv_attn_01;
  knobs.break_cv_attn_01 = s.knobs.break_cv_attn_01;
  knobs.corrupt_cv_attn_01 = s.knobs.corrupt_cv_attn_01;
  corrupter_engine_set_knobs(engine_mem.data(), &knobs);
  corrupter_engine_set_clock_mode_internal(engine_mem.data(), s.clock_mode_internal ? 1 : 0);
  corrupter_engine_set_audio_context(engine_mem.data(), s.cfg.sample_rate_hz,
                                     s.cfg.max_block_frames);

  corrupter_audio_block_t audio{};
  audio.in_l = buf.in_l.data();
  audio.in_r = buf.in_r.data();
  audio.out_l = buf.out_l.data();
  audio.out_r = buf.out_r.data();
  audio.frames = s.frames;

  corrupter_cv_inputs_t cv{};
  corrupter_gate_inputs_t gates{};

  const corrupter_cv_inputs_t* cv_ptr = nullptr;
  const corrupter_gate_inputs_t* gate_ptr = nullptr;
  if (mod) {
    cv.time_v = VecPtr(mod->time_cv, s.frames);
    cv.repeats_v = VecPtr(mod->repeats_cv, s.frames);
    cv.mix_v = VecPtr(mod->mix_cv, s.frames);
    cv.bend_v = VecPtr(mod->bend_cv, s.frames);
    cv.break_v = VecPtr(mod->break_cv, s.frames);
    cv.corrupt_v = VecPtr(mod->corrupt_cv, s.frames);

    gates.bend_gate_v = VecPtr(mod->bend_gate, s.frames);
    gates.break_gate_v = VecPtr(mod->break_gate, s.frames);
    gates.corrupt_gate_v = VecPtr(mod->corrupt_gate, s.frames);
    gates.freeze_gate_v = VecPtr(mod->freeze_gate, s.frames);
    gates.clock_gate_v = VecPtr(mod->clock_gate, s.frames);

    cv_ptr = &cv;
    gate_ptr = &gates;
  }

  corrupter_engine_process(engine_mem.data(), &audio, cv_ptr, gate_ptr);
  corrupter_engine_destruct(engine_mem.data());
  return buf;
}

bool TestDeterministicSameSeed() {
  Scenario s{};
  s.cfg.sample_rate_hz = 96000.0f;
  s.cfg.max_block_frames = 256;
  s.cfg.max_buffer_seconds = 60.0f;
  s.cfg.random_seed = 1337;

  s.state.macro_mode = true;
  s.state.bend_enabled = true;
  s.state.break_enabled = true;
  s.state.corrupt_algorithm = corrupter::CorruptAlgorithm::kDestroy;
  s.state.corrupt_bank = corrupter::CorruptBank::kLegacy;
  s.state.glitch_window_01 = 0.25f;

  s.knobs.time_01 = 1.0f;
  s.knobs.repeats_01 = 0.85f;
  s.knobs.mix_01 = 1.0f;
  s.knobs.bend_01 = 0.8f;
  s.knobs.break_01 = 0.75f;
  s.knobs.corrupt_01 = 0.6f;
  s.frames = 8192;

  const StereoBuffers a = RunScenarioCpp(s);
  const StereoBuffers b = RunScenarioCpp(s);
  return ExactEqual(a.out_l, b.out_l) && ExactEqual(a.out_r, b.out_r);
}

bool TestDifferentSeedChangesOutput() {
  Scenario s1{};
  s1.cfg.sample_rate_hz = 96000.0f;
  s1.cfg.max_block_frames = 256;
  s1.cfg.max_buffer_seconds = 60.0f;
  s1.cfg.random_seed = 1001;

  s1.state.macro_mode = true;
  s1.state.bend_enabled = true;
  s1.state.break_enabled = true;
  s1.state.corrupt_algorithm = corrupter::CorruptAlgorithm::kDecimate;
  s1.state.corrupt_bank = corrupter::CorruptBank::kLegacy;
  s1.state.glitch_window_01 = 0.1f;

  s1.knobs.time_01 = 0.1f;
  s1.knobs.repeats_01 = 0.7f;
  s1.knobs.mix_01 = 1.0f;
  s1.knobs.bend_01 = 0.7f;
  s1.knobs.break_01 = 0.7f;
  s1.knobs.corrupt_01 = 0.7f;
  s1.frames = 8192;

  Scenario s2 = s1;
  s2.cfg.random_seed = 2002;

  const StereoBuffers a = RunScenarioCpp(s1);
  const StereoBuffers b = RunScenarioCpp(s2);
  return HashF32(a.out_l, a.out_r) != HashF32(b.out_l, b.out_r);
}

bool TestDryBypass() {
  Scenario s{};
  s.cfg.sample_rate_hz = 96000.0f;
  s.cfg.max_block_frames = 256;
  s.cfg.max_buffer_seconds = 60.0f;
  s.cfg.random_seed = 9;

  s.state.macro_mode = true;
  s.state.bend_enabled = false;
  s.state.break_enabled = false;
  s.state.freeze_enabled = false;

  s.knobs.mix_01 = 0.0f;
  s.frames = 4096;

  const StereoBuffers a = RunScenarioCpp(s);
  for (uint32_t i = 0; i < s.frames; ++i) {
    if (!NearEqual(a.in_l[i], a.out_l[i]) || !NearEqual(a.in_r[i], a.out_r[i])) {
      return false;
    }
  }
  return true;
}

bool TestFreezeMomentaryAutoWet() {
  Scenario s{};
  s.cfg.sample_rate_hz = 96000.0f;
  s.cfg.max_block_frames = 256;
  s.cfg.max_buffer_seconds = 60.0f;
  s.cfg.random_seed = 42;

  s.state.macro_mode = true;
  s.state.bend_enabled = false;
  s.state.break_enabled = false;
  s.state.freeze_enabled = false;
  s.state.freeze_latching = false;  // momentary

  s.knobs.time_01 = 1.0f;
  s.knobs.mix_01 = 0.0f;
  s.frames = 4096;

  ModulationBuffers mod;
  mod.freeze_gate.assign(s.frames, 0.0f);
  for (uint32_t i = 1000; i < 1700; ++i) {
    mod.freeze_gate[i] = 5.0f;
  }

  const StereoBuffers a = RunScenarioCpp(s, &mod);

  bool seen_dry_pre = false;
  for (uint32_t i = 100; i < 900; ++i) {
    if (NearEqual(a.in_l[i], a.out_l[i], 1e-6f) && NearEqual(a.in_r[i], a.out_r[i], 1e-6f)) {
      seen_dry_pre = true;
      break;
    }
  }

  bool seen_wet_diff = false;
  for (uint32_t i = 1050; i < 1650; ++i) {
    if (std::fabs(a.in_l[i] - a.out_l[i]) > 1e-3f || std::fabs(a.in_r[i] - a.out_r[i]) > 1e-3f) {
      seen_wet_diff = true;
      break;
    }
  }

  bool seen_dry_post = false;
  for (uint32_t i = 1800; i < 2600; ++i) {
    if (NearEqual(a.in_l[i], a.out_l[i], 1e-6f) && NearEqual(a.in_r[i], a.out_r[i], 1e-6f)) {
      seen_dry_post = true;
      break;
    }
  }

  return seen_dry_pre && seen_wet_diff && seen_dry_post;
}

bool TestExternalClockChangesTiming() {
  Scenario s{};
  s.cfg.sample_rate_hz = 96000.0f;
  s.cfg.max_block_frames = 256;
  s.cfg.max_buffer_seconds = 60.0f;
  s.cfg.random_seed = 77;

  s.state.macro_mode = true;
  s.state.bend_enabled = true;
  s.state.break_enabled = true;
  s.state.corrupt_bank = corrupter::CorruptBank::kLegacy;
  s.state.corrupt_algorithm = corrupter::CorruptAlgorithm::kDropout;
  s.knobs.time_01 = 1.0f;
  s.knobs.repeats_01 = 0.6f;
  s.knobs.mix_01 = 1.0f;
  s.knobs.bend_01 = 0.8f;
  s.knobs.break_01 = 0.9f;
  s.knobs.corrupt_01 = 0.3f;
  s.clock_mode_internal = false;
  s.frames = 8192;

  ModulationBuffers no_clock;

  ModulationBuffers ext_clock;
  ext_clock.clock_gate.assign(s.frames, 0.0f);
  for (uint32_t i = 100; i < s.frames; i += 200) {
    ext_clock.clock_gate[i] = 5.0f;
  }

  const StereoBuffers a = RunScenarioCpp(s, &no_clock);
  const StereoBuffers b = RunScenarioCpp(s, &ext_clock);
  return HashF32(a.out_l, a.out_r) != HashF32(b.out_l, b.out_r);
}

bool TestBlockInvariance() {
  Scenario s{};
  s.cfg.sample_rate_hz = 96000.0f;
  s.cfg.max_block_frames = 256;
  s.cfg.max_buffer_seconds = 60.0f;
  s.cfg.random_seed = 5151;

  s.state.macro_mode = true;
  s.state.bend_enabled = true;
  s.state.break_enabled = true;
  s.state.corrupt_bank = corrupter::CorruptBank::kExpanded;
  s.state.corrupt_algorithm = corrupter::CorruptAlgorithm::kDjFilter;
  s.state.glitch_window_01 = 0.35f;

  s.knobs.time_01 = 0.95f;
  s.knobs.repeats_01 = 0.8f;
  s.knobs.mix_01 = 0.9f;
  s.knobs.bend_01 = 0.65f;
  s.knobs.break_01 = 0.7f;
  s.knobs.corrupt_01 = 0.6f;
  s.frames = 8192;

  ModulationBuffers mod;
  mod.clock_gate.assign(s.frames, 0.0f);
  for (uint32_t i = 77; i < s.frames; i += 211) {
    mod.clock_gate[i] = 5.0f;
  }

  const StereoBuffers whole = RunScenarioCpp(s, &mod);
  const StereoBuffers chunked =
      RunScenarioCppChunked(s, &mod, {1u, 7u, 64u, 3u, 255u, 8u, 19u, 32u});
  return ExactEqual(whole.out_l, chunked.out_l) &&
         ExactEqual(whole.out_r, chunked.out_r);
}

bool TestExternalClockTimeoutStatus() {
  Scenario s{};
  s.cfg.sample_rate_hz = 96000.0f;
  s.cfg.max_block_frames = 256;
  s.cfg.max_buffer_seconds = 60.0f;
  s.cfg.random_seed = 9191;
  s.clock_mode_internal = false;
  s.knobs.time_01 = 0.5f;
  s.knobs.mix_01 = 0.7f;
  s.frames = 10000;

  ModulationBuffers timed_out;
  timed_out.clock_gate.assign(s.frames, 0.0f);
  timed_out.clock_gate[0] = 5.0f;
  timed_out.clock_gate[300] = 5.0f;
  const corrupter::RuntimeInfo info_timeout = RunScenarioCppForInfo(s, &timed_out);

  ModulationBuffers active;
  active.clock_gate.assign(s.frames, 0.0f);
  for (uint32_t i = 0; i < s.frames; i += 300) {
    active.clock_gate[i] = 5.0f;
  }
  const corrupter::RuntimeInfo info_active = RunScenarioCppForInfo(s, &active);

  if (info_timeout.observed_ticks == 0 || info_active.observed_ticks == 0) {
    return false;
  }
  return (!info_timeout.external_clock_present) && info_active.external_clock_present;
}

bool TestBreakMacroIntensityAffectsOutput() {
  Scenario low{};
  low.cfg.sample_rate_hz = 96000.0f;
  low.cfg.max_block_frames = 256;
  low.cfg.max_buffer_seconds = 60.0f;
  low.cfg.random_seed = 31337;
  low.state.macro_mode = true;
  low.state.bend_enabled = false;
  low.state.break_enabled = true;
  low.knobs.time_01 = 0.1f;
  low.knobs.repeats_01 = 0.65f;
  low.knobs.mix_01 = 1.0f;
  low.knobs.break_01 = 0.05f;
  low.frames = 8192;

  Scenario high = low;
  high.knobs.break_01 = 0.95f;

  const StereoBuffers out_low = RunScenarioCpp(low);
  const StereoBuffers out_high = RunScenarioCpp(high);

  const uint64_t low_hash = HashF32(out_low.out_l, out_low.out_r);
  const uint64_t high_hash = HashF32(out_high.out_l, out_high.out_r);
  return low_hash != high_hash;
}

bool TestCApiParity() {
  Scenario s{};
  s.cfg.sample_rate_hz = 96000.0f;
  s.cfg.max_block_frames = 256;
  s.cfg.max_buffer_seconds = 60.0f;
  s.cfg.random_seed = 777;

  s.state.macro_mode = false;
  s.state.bend_enabled = true;
  s.state.break_enabled = true;
  s.state.break_silence_mode = true;
  s.state.corrupt_bank = corrupter::CorruptBank::kExpanded;
  s.state.corrupt_algorithm = corrupter::CorruptAlgorithm::kVinylSim;
  s.state.glitch_window_01 = 0.8f;

  s.knobs.time_01 = 1.0f;
  s.knobs.repeats_01 = 0.4f;
  s.knobs.mix_01 = 0.65f;
  s.knobs.bend_01 = 0.2f;
  s.knobs.break_01 = 0.8f;
  s.knobs.corrupt_01 = 0.5f;
  s.knobs.bend_cv_attn_01 = 0.9f;
  s.knobs.break_cv_attn_01 = 0.7f;
  s.knobs.corrupt_cv_attn_01 = 0.6f;
  s.frames = 4096;

  ModulationBuffers mod;
  mod.bend_cv.assign(s.frames, 0.0f);
  mod.break_cv.assign(s.frames, 0.0f);
  mod.corrupt_cv.assign(s.frames, 0.0f);

  const StereoBuffers cpp = RunScenarioCpp(s, &mod);
  const StereoBuffers c = RunScenarioC(s, &mod);
  return ExactEqual(cpp.out_l, c.out_l) && ExactEqual(cpp.out_r, c.out_r);
}

bool TestPersistentStateRoundTrip() {
  corrupter::EngineConfig cfg;
  cfg.sample_rate_hz = 96000.0f;
  cfg.max_block_frames = 128;
  cfg.max_buffer_seconds = 5.0f;
  cfg.random_seed = 123;

  const size_t dram_bytes = corrupter::Engine::required_dram_bytes(cfg);
  std::vector<uint8_t> dram(dram_bytes);
  corrupter::Engine engine;
  if (!engine.initialise(dram.data(), dram.size(), cfg)) {
    return false;
  }

  corrupter::PersistentState set_state;
  set_state.bend_enabled = true;
  set_state.break_enabled = true;
  set_state.freeze_enabled = true;
  set_state.macro_mode = false;
  set_state.break_silence_mode = true;
  set_state.unique_stereo_mode = true;
  set_state.gate_latching = false;
  set_state.freeze_latching = false;
  set_state.corrupt_gate_is_reset = true;
  set_state.corrupt_bank = corrupter::CorruptBank::kExpanded;
  set_state.corrupt_algorithm = corrupter::CorruptAlgorithm::kVinylSim;
  set_state.glitch_window_01 = 0.77f;
  engine.set_persistent_state(set_state);

  corrupter::PersistentState got{};
  if (!engine.get_persistent_state(&got)) {
    return false;
  }

  return (got.bend_enabled == set_state.bend_enabled) &&
         (got.break_enabled == set_state.break_enabled) &&
         (got.freeze_enabled == set_state.freeze_enabled) &&
         (got.macro_mode == set_state.macro_mode) &&
         (got.break_silence_mode == set_state.break_silence_mode) &&
         (got.unique_stereo_mode == set_state.unique_stereo_mode) &&
         (got.gate_latching == set_state.gate_latching) &&
         (got.freeze_latching == set_state.freeze_latching) &&
         (got.corrupt_gate_is_reset == set_state.corrupt_gate_is_reset) &&
         (got.corrupt_bank == set_state.corrupt_bank) &&
         (got.corrupt_algorithm == set_state.corrupt_algorithm) &&
         NearEqual(got.glitch_window_01, set_state.glitch_window_01, 1e-6f);
}

bool TestPersistentBlobRoundTrip() {
  corrupter::EngineConfig cfg;
  cfg.sample_rate_hz = 96000.0f;
  cfg.max_block_frames = 128;
  cfg.max_buffer_seconds = 5.0f;
  cfg.random_seed = 123;

  const size_t dram_bytes = corrupter::Engine::required_dram_bytes(cfg);
  std::vector<uint8_t> dram_a(dram_bytes);
  std::vector<uint8_t> dram_b(dram_bytes);

  corrupter::Engine a;
  corrupter::Engine b;
  if (!a.initialise(dram_a.data(), dram_a.size(), cfg)) {
    return false;
  }
  if (!b.initialise(dram_b.data(), dram_b.size(), cfg)) {
    return false;
  }

  corrupter::PersistentState in;
  in.bend_enabled = true;
  in.break_enabled = true;
  in.freeze_enabled = true;
  in.macro_mode = false;
  in.break_silence_mode = true;
  in.unique_stereo_mode = true;
  in.gate_latching = false;
  in.freeze_latching = false;
  in.corrupt_gate_is_reset = true;
  in.corrupt_bank = corrupter::CorruptBank::kExpanded;
  in.corrupt_algorithm = corrupter::CorruptAlgorithm::kDjFilter;
  in.glitch_window_01 = 0.33f;
  a.set_persistent_state(in);

  std::vector<uint8_t> blob(128u, 0u);
  size_t written = 0;
  if (!a.serialise_persistent_state(blob.data(), blob.size(), &written)) {
    return false;
  }
  if (!b.deserialise_persistent_state(blob.data(), written)) {
    return false;
  }

  corrupter::PersistentState out{};
  if (!b.get_persistent_state(&out)) {
    return false;
  }
  return (out.bend_enabled == in.bend_enabled) &&
         (out.break_enabled == in.break_enabled) &&
         (out.freeze_enabled == in.freeze_enabled) &&
         (out.macro_mode == in.macro_mode) &&
         (out.break_silence_mode == in.break_silence_mode) &&
         (out.unique_stereo_mode == in.unique_stereo_mode) &&
         (out.gate_latching == in.gate_latching) &&
         (out.freeze_latching == in.freeze_latching) &&
         (out.corrupt_gate_is_reset == in.corrupt_gate_is_reset) &&
         (out.corrupt_bank == in.corrupt_bank) &&
         (out.corrupt_algorithm == in.corrupt_algorithm) &&
         NearEqual(out.glitch_window_01, in.glitch_window_01, 1e-6f);
}

bool TestMicroBendOneVoltPerOct() {
  Scenario base{};
  base.cfg.sample_rate_hz = 96000.0f;
  base.cfg.max_block_frames = 256;
  base.cfg.max_buffer_seconds = 5.0f;
  base.cfg.random_seed = 4242;
  base.state.macro_mode = false;
  base.state.bend_enabled = true;
  base.state.break_enabled = false;
  base.knobs.time_01 = 1.0f;
  base.knobs.mix_01 = 1.0f;
  base.knobs.bend_01 = 0.5f;  // center -> 0 oct base
  base.knobs.bend_cv_attn_01 = 1.0f;
  base.frames = 4096;

  ModulationBuffers cv0;
  cv0.bend_cv.assign(base.frames, 0.0f);
  const corrupter::RuntimeInfo info0 = RunScenarioCppForInfo(base, &cv0);

  ModulationBuffers cv_plus1;
  cv_plus1.bend_cv.assign(base.frames, 1.0f);
  const corrupter::RuntimeInfo info_plus1 = RunScenarioCppForInfo(base, &cv_plus1);

  ModulationBuffers cv_minus1;
  cv_minus1.bend_cv.assign(base.frames, -1.0f);
  const corrupter::RuntimeInfo info_minus1 = RunScenarioCppForInfo(base, &cv_minus1);

  const bool base_ok = NearRatio(info0.current_rate_l, 1.0f, 0.10f) &&
                       NearRatio(info0.current_rate_r, 1.0f, 0.10f);
  const bool plus_ok = NearRatio(info_plus1.current_rate_l, 2.0f, 0.12f) &&
                       NearRatio(info_plus1.current_rate_r, 2.0f, 0.12f);
  const bool minus_ok = NearRatio(info_minus1.current_rate_l, 0.5f, 0.12f) &&
                        NearRatio(info_minus1.current_rate_r, 0.5f, 0.12f);
  return base_ok && plus_ok && minus_ok;
}

bool TestExternalClockRatiosAcrossTempoRange() {
  static constexpr int kBpms[4] = {20, 60, 120, 300};

  // Use reduced SR for test runtime; clock ratio math is sample-rate invariant.
  const float sample_rate = 9600.0f;

  for (int bpm : kBpms) {
    const uint64_t beat_interval =
        static_cast<uint64_t>(sample_rate * 60.0f / static_cast<float>(bpm));
    if (beat_interval == 0) {
      return false;
    }
    const uint64_t total_samples = beat_interval * 64u;
    uint64_t ticks_lo = 0;
    uint64_t ticks_mid = 0;
    uint64_t ticks_hi = 0;

    for (int ratio_index = 0; ratio_index < 9; ++ratio_index) {
      const float time_01 = (static_cast<float>(ratio_index) + 0.5f) / 9.0f;

      corrupter::internal::ClockEngine clock;
      clock.Reset(sample_rate, time_01);
      clock.SetInternalMode(false);

      uint64_t ticks = 0;
      for (uint64_t s = 0; s < total_samples; ++s) {
        const bool pulse = ((s % beat_interval) == 0u);
        if (clock.Step(s, pulse)) {
          ++ticks;
        }
      }

      if (ticks == 0) {
        return false;
      }
      if (!clock.ExternalSignalPresent()) {
        return false;
      }

      if (ratio_index == 0) {
        ticks_lo = ticks;
      } else if (ratio_index == 4) {
        ticks_mid = ticks;
      } else if (ratio_index == 8) {
        ticks_hi = ticks;
      }
    }

    if (!(ticks_lo < ticks_mid && ticks_mid < ticks_hi)) {
      return false;
    }
  }
  return true;
}

bool TestCvAdditiveRangeAffectsMix() {
  Scenario s{};
  s.cfg.sample_rate_hz = 96000.0f;
  s.cfg.max_block_frames = 256;
  s.cfg.max_buffer_seconds = 5.0f;
  s.cfg.random_seed = 5656;
  s.state.macro_mode = true;
  s.state.bend_enabled = true;
  s.state.break_enabled = true;
  s.knobs.time_01 = 1.0f;
  s.knobs.repeats_01 = 0.6f;
  s.knobs.mix_01 = 0.3f;
  s.knobs.bend_01 = 0.7f;
  s.knobs.break_01 = 0.7f;
  s.frames = 4096;

  ModulationBuffers cv_pos;
  cv_pos.mix_cv.assign(s.frames, 5.0f);
  const StereoBuffers out_pos = RunScenarioCpp(s, &cv_pos);

  ModulationBuffers cv_neg;
  cv_neg.mix_cv.assign(s.frames, -5.0f);
  const StereoBuffers out_neg = RunScenarioCpp(s, &cv_neg);

  const uint64_t h_pos = HashF32(out_pos.out_l, out_pos.out_r);
  const uint64_t h_neg = HashF32(out_neg.out_l, out_neg.out_r);
  return h_pos != h_neg;
}

bool TestFreezeLatchingSyncToClock() {
  Scenario s{};
  s.cfg.sample_rate_hz = 96000.0f;
  s.cfg.max_block_frames = 256;
  s.cfg.max_buffer_seconds = 5.0f;
  s.cfg.random_seed = 919;
  s.clock_mode_internal = false;
  s.state.freeze_latching = true;
  s.state.freeze_enabled = false;
  s.knobs.time_01 = 0.5f;
  s.knobs.mix_01 = 0.0f;
  s.frames = 1600;

  ModulationBuffers mod;
  mod.clock_gate.assign(s.frames, 0.0f);
  for (uint32_t i = 0; i < s.frames; i += 200) {
    mod.clock_gate[i] = 5.0f;
  }
  mod.freeze_gate.assign(s.frames, 0.0f);
  mod.freeze_gate[150] = 5.0f;  // request freeze before the next clock tick at 200

  const StereoBuffers out = RunScenarioCpp(s, &mod);

  // Before sync point (sample 200), output should remain dry.
  for (uint32_t i = 20; i < 190; ++i) {
    if (!NearEqual(out.in_l[i], out.out_l[i], 1e-6f) ||
        !NearEqual(out.in_r[i], out.out_r[i], 1e-6f)) {
      return false;
    }
  }

  // After synced engage, freeze auto-wet should make output diverge from dry.
  bool diverged = false;
  for (uint32_t i = 240; i < 500; ++i) {
    if (std::fabs(out.in_l[i] - out.out_l[i]) > 1e-4f ||
        std::fabs(out.in_r[i] - out.out_r[i]) > 1e-4f) {
      diverged = true;
      break;
    }
  }
  return diverged;
}

bool TestRuntimeAudioContextSwitch() {
  corrupter::EngineConfig cfg;
  cfg.sample_rate_hz = 32000.0f;
  cfg.max_supported_sample_rate_hz = 96000.0f;
  cfg.max_block_frames = 512;
  cfg.max_buffer_seconds = 5.0f;
  cfg.random_seed = 2026;

  const size_t dram_bytes = corrupter::Engine::required_dram_bytes(cfg);
  std::vector<uint8_t> dram(dram_bytes);
  corrupter::Engine engine;
  if (!engine.initialise(dram.data(), dram.size(), cfg)) {
    return false;
  }

  engine.set_audio_context(32000.0f, 96);

  const uint32_t n1 = 64;
  std::vector<float> in_l1(n1, 0.0f), in_r1(n1, 0.0f), out_l1(n1, 0.0f), out_r1(n1, 0.0f);
  for (uint32_t i = 0; i < n1; ++i) {
    in_l1[i] = std::sin(0.03f * static_cast<float>(i));
    in_r1[i] = std::cos(0.02f * static_cast<float>(i));
  }
  corrupter::AudioBlock a1{in_l1.data(), in_r1.data(), out_l1.data(), out_r1.data(), n1};
  engine.process(a1, {}, {});

  engine.set_audio_context(96000.0f, 512);

  const uint32_t n2 = 257;
  std::vector<float> in_l2(n2, 0.0f), in_r2(n2, 0.0f), out_l2(n2, 0.0f), out_r2(n2, 0.0f);
  for (uint32_t i = 0; i < n2; ++i) {
    in_l2[i] = std::sin(0.017f * static_cast<float>(i));
    in_r2[i] = std::cos(0.019f * static_cast<float>(i));
  }
  corrupter::AudioBlock a2{in_l2.data(), in_r2.data(), out_l2.data(), out_r2.data(), n2};
  engine.process(a2, {}, {});

  corrupter::RuntimeInfo info{};
  if (!engine.get_runtime_info(&info)) {
    return false;
  }

  return NearEqual(info.sample_rate_hz, 96000.0f, 1e-3f) &&
         (info.max_block_frames == 512u) &&
         (info.processed_frames == static_cast<uint64_t>(n1 + n2));
}

bool TestRequiredDramUsesMaxSupportedRate() {
  corrupter::EngineConfig low_sr;
  low_sr.sample_rate_hz = 32000.0f;
  low_sr.max_supported_sample_rate_hz = 32000.0f;
  low_sr.max_block_frames = 128;
  low_sr.max_buffer_seconds = 60.0f;

  corrupter::EngineConfig high_sr = low_sr;
  high_sr.max_supported_sample_rate_hz = 96000.0f;

  const size_t bytes_low = corrupter::Engine::required_dram_bytes(low_sr);
  const size_t bytes_high = corrupter::Engine::required_dram_bytes(high_sr);
  return bytes_high > bytes_low;
}

bool TestAudioContextClampsToMaxSupportedRate() {
  corrupter::EngineConfig cfg;
  cfg.sample_rate_hz = 48000.0f;
  cfg.max_supported_sample_rate_hz = 96000.0f;
  cfg.max_block_frames = 128;
  cfg.max_buffer_seconds = 5.0f;
  cfg.random_seed = 707;

  const size_t dram_bytes = corrupter::Engine::required_dram_bytes(cfg);
  std::vector<uint8_t> dram(dram_bytes);
  corrupter::Engine engine;
  if (!engine.initialise(dram.data(), dram.size(), cfg)) {
    return false;
  }

  engine.set_audio_context(192000.0f, 1024u);
  corrupter::RuntimeInfo info{};
  if (!engine.get_runtime_info(&info)) {
    return false;
  }
  return NearEqual(info.sample_rate_hz, 96000.0f, 1e-3f) &&
         (info.max_block_frames == 1024u);
}

bool TestExternalClockTimeoutStableAfterSampleRateChange() {
  corrupter::internal::ClockEngine clock;
  clock.Reset(48000.0f, 0.5f);
  clock.SetInternalMode(false);

  bool present_before = false;
  bool present_at_timeout = true;
  for (uint64_t s = 0; s <= 408000u; ++s) {
    const bool pulse = (s == 0u) || (s == 48000u);
    clock.Step(s, pulse);
    if (s == 72000u) {
      // SR changes halfway between external pulses.
      clock.SetSampleRate(96000.0f, s);
    }
    if (s == 407999u) {
      present_before = clock.ExternalSignalPresent();
    }
    if (s == 408000u) {
      present_at_timeout = clock.ExternalSignalPresent();
    }
  }

  return present_before && !present_at_timeout;
}

bool TestPersistentStateSanitisesInvalidEnums() {
  corrupter::EngineConfig cfg;
  cfg.sample_rate_hz = 96000.0f;
  cfg.max_supported_sample_rate_hz = 96000.0f;
  cfg.max_block_frames = 128;
  cfg.max_buffer_seconds = 5.0f;
  cfg.random_seed = 123;

  const size_t dram_bytes = corrupter::Engine::required_dram_bytes(cfg);
  std::vector<uint8_t> dram(dram_bytes);
  corrupter::Engine engine;
  if (!engine.initialise(dram.data(), dram.size(), cfg)) {
    return false;
  }

  corrupter::PersistentState in;
  in.corrupt_bank = static_cast<corrupter::CorruptBank>(255u);
  in.corrupt_algorithm = static_cast<corrupter::CorruptAlgorithm>(255u);
  in.glitch_window_01 = 0.5f;
  engine.set_persistent_state(in);

  corrupter::PersistentState out{};
  if (!engine.get_persistent_state(&out)) {
    return false;
  }
  return (out.corrupt_bank == corrupter::CorruptBank::kLegacy) &&
         (out.corrupt_algorithm == corrupter::CorruptAlgorithm::kDecimate);
}

bool TestCApiGuardClauses() {
  if (corrupter_engine_required_dram_bytes(nullptr) != 0u) {
    return false;
  }
  if (corrupter_engine_construct(nullptr, 0u) != 0) {
    return false;
  }
  if (corrupter_engine_initialise(nullptr, nullptr, 0u, nullptr) != 0) {
    return false;
  }
  if (corrupter_engine_serialise_persistent_state(nullptr, nullptr, 0u, nullptr) != 0) {
    return false;
  }
  if (corrupter_engine_deserialise_persistent_state(nullptr, nullptr, 0u) != 0) {
    return false;
  }

  const size_t engine_bytes = corrupter_engine_sizeof();
  if (engine_bytes == 0u) {
    return false;
  }
  std::vector<uint8_t> engine_mem(engine_bytes, 0u);
  if (!corrupter_engine_construct(engine_mem.data(), engine_mem.size())) {
    return false;
  }

  if (corrupter_engine_initialise(engine_mem.data(), nullptr, 0u, nullptr) != 0) {
    return false;
  }

  corrupter_engine_set_knobs(nullptr, nullptr);
  corrupter_engine_set_persistent_state(nullptr, nullptr);
  corrupter_engine_set_audio_context(nullptr, 0.0f, 0u);
  corrupter_engine_set_clock_mode_internal(nullptr, 0);
  corrupter_engine_process(nullptr, nullptr, nullptr, nullptr);
  corrupter_engine_reset(nullptr);
  corrupter_engine_destruct(nullptr);

  corrupter_audio_block_t empty_audio{};
  empty_audio.frames = 0u;
  corrupter_engine_process(engine_mem.data(), &empty_audio, nullptr, nullptr);
  corrupter_engine_destruct(engine_mem.data());
  return true;
}

bool TestRequiredDramRejectsInvalidLargeConfig() {
  corrupter::EngineConfig cfg;
  cfg.sample_rate_hz = 96000.0f;
  cfg.max_supported_sample_rate_hz = 96000.0f;
  cfg.max_block_frames = 128u;
  cfg.max_buffer_seconds = std::numeric_limits<float>::infinity();
  cfg.random_seed = 1u;

  const size_t bytes = corrupter::Engine::required_dram_bytes(cfg);
  if (bytes != 0u) {
    return false;
  }

  std::vector<uint8_t> dram(256u, 0u);
  corrupter::Engine engine;
  return !engine.initialise(dram.data(), dram.size(), cfg);
}

bool TestDropoutUsesSmoothEdges() {
  corrupter::internal::CorruptChannelState state{};
  corrupter::internal::XorShift32 rng;
  rng.Seed(303u);

  constexpr float kSr = 96000.0f;
  constexpr uint32_t kSamples = 300000u;
  float prev = 1.0f;
  float max_delta = 0.0f;
  bool saw_dropout = false;

  for (uint32_t i = 0; i < kSamples; ++i) {
    const float y = corrupter::internal::ProcessCorruptSample(
        1.0f, 1.0f, corrupter::CorruptBank::kLegacy,
        corrupter::CorruptAlgorithm::kDropout, &state, &rng, kSr);
    if (!std::isfinite(y)) {
      return false;
    }
    saw_dropout = saw_dropout || (y < 0.05f);
    max_delta = std::max(max_delta, std::fabs(y - prev));
    prev = y;
  }

  return saw_dropout && (max_delta < 0.08f);
}

bool TestDjFilterTiltResponse() {
  auto run_case = [](float intensity, float* low_amp, float* high_amp) {
    if (!low_amp || !high_amp) {
      return false;
    }
    corrupter::internal::CorruptChannelState state{};
    corrupter::internal::XorShift32 rng;
    rng.Seed(404u);

    constexpr float kSr = 96000.0f;
    constexpr float kLowHz = 130.0f;
    constexpr float kHighHz = 5600.0f;
    constexpr uint32_t kTotal = 96000u;
    constexpr uint32_t kWarmup = 12000u;
    double low_sin = 0.0;
    double low_cos = 0.0;
    double high_sin = 0.0;
    double high_cos = 0.0;
    uint32_t measured = 0u;

    for (uint32_t i = 0; i < kTotal; ++i) {
      const double t = static_cast<double>(i) / static_cast<double>(kSr);
      const float x = static_cast<float>(0.70 * std::sin(2.0 * kPi * kLowHz * t) +
                                         0.70 * std::sin(2.0 * kPi * kHighHz * t));
      const float y = corrupter::internal::ProcessCorruptSample(
          x, intensity, corrupter::CorruptBank::kExpanded,
          corrupter::CorruptAlgorithm::kDjFilter, &state, &rng, kSr);
      if (!std::isfinite(y)) {
        return false;
      }
      if (i >= kWarmup) {
        const double p_low = 2.0 * kPi * kLowHz * t;
        const double p_high = 2.0 * kPi * kHighHz * t;
        low_sin += static_cast<double>(y) * std::sin(p_low);
        low_cos += static_cast<double>(y) * std::cos(p_low);
        high_sin += static_cast<double>(y) * std::sin(p_high);
        high_cos += static_cast<double>(y) * std::cos(p_high);
        ++measured;
      }
    }

    *low_amp = ToneAmplitude(low_sin, low_cos, measured);
    *high_amp = ToneAmplitude(high_sin, high_cos, measured);
    return true;
  };

  float lp_low = 0.0f;
  float lp_high = 0.0f;
  float hp_low = 0.0f;
  float hp_high = 0.0f;
  if (!run_case(0.05f, &lp_low, &lp_high)) {
    return false;
  }
  if (!run_case(0.95f, &hp_low, &hp_high)) {
    return false;
  }

  return (lp_low > (lp_high * 1.6f)) && (hp_high > (hp_low * 1.6f));
}

bool TestVinylGeneratesSurfaceNoise() {
  corrupter::internal::CorruptChannelState state{};
  corrupter::internal::XorShift32 rng;
  rng.Seed(505u);

  constexpr float kSr = 96000.0f;
  constexpr uint32_t kSamples = 96000u;
  double energy = 0.0;
  float peak = 0.0f;
  for (uint32_t i = 0; i < kSamples; ++i) {
    const float y = corrupter::internal::ProcessCorruptSample(
        0.0f, 0.9f, corrupter::CorruptBank::kExpanded,
        corrupter::CorruptAlgorithm::kVinylSim, &state, &rng, kSr);
    if (!std::isfinite(y)) {
      return false;
    }
    energy += static_cast<double>(y) * static_cast<double>(y);
    peak = std::max(peak, std::fabs(y));
  }
  const float rms = static_cast<float>(std::sqrt(energy / static_cast<double>(kSamples)));
  return (rms > 0.0005f) && (peak < 1.5f);
}

bool TestCorruptAlgorithmsFiniteAtExtremes() {
  struct AlgoCase {
    corrupter::CorruptBank bank;
    corrupter::CorruptAlgorithm algo;
  };
  const AlgoCase cases[] = {
      {corrupter::CorruptBank::kLegacy, corrupter::CorruptAlgorithm::kDecimate},
      {corrupter::CorruptBank::kLegacy, corrupter::CorruptAlgorithm::kDropout},
      {corrupter::CorruptBank::kLegacy, corrupter::CorruptAlgorithm::kDestroy},
      {corrupter::CorruptBank::kExpanded, corrupter::CorruptAlgorithm::kDjFilter},
      {corrupter::CorruptBank::kExpanded, corrupter::CorruptAlgorithm::kVinylSim},
  };
  const float intensities[] = {0.0f, 0.5f, 1.0f};

  for (const AlgoCase& c : cases) {
    for (float intensity : intensities) {
      corrupter::internal::CorruptChannelState state{};
      corrupter::internal::XorShift32 rng;
      rng.Seed(606u + static_cast<uint32_t>(static_cast<int>(intensity * 10.0f)));
      constexpr float kSr = 96000.0f;
      for (uint32_t i = 0; i < 40000u; ++i) {
        const float x = 0.95f * std::sin(0.0019f * static_cast<float>(i)) +
                        0.45f * rng.NextSigned();
        const float y = corrupter::internal::ProcessCorruptSample(
            x, intensity, c.bank, c.algo, &state, &rng, kSr);
        if (!std::isfinite(y) || std::fabs(y) > 8.0f) {
          return false;
        }
      }
    }
  }
  return true;
}

}  // namespace

int main() {
  struct TestCase {
    const char* name;
    bool (*fn)();
  };

  const TestCase tests[] = {
      {"deterministic_same_seed", TestDeterministicSameSeed},
      {"different_seed_changes_output", TestDifferentSeedChangesOutput},
      {"dry_bypass", TestDryBypass},
      {"freeze_momentary_autowet", TestFreezeMomentaryAutoWet},
      {"external_clock_changes_timing", TestExternalClockChangesTiming},
      {"block_invariance", TestBlockInvariance},
      {"external_clock_timeout_status", TestExternalClockTimeoutStatus},
      {"break_macro_intensity_affects_output", TestBreakMacroIntensityAffectsOutput},
      {"c_api_parity", TestCApiParity},
      {"persistent_state_roundtrip", TestPersistentStateRoundTrip},
      {"persistent_blob_roundtrip", TestPersistentBlobRoundTrip},
      {"micro_bend_1v_per_oct", TestMicroBendOneVoltPerOct},
      {"external_clock_ratios_tempo_range",
       TestExternalClockRatiosAcrossTempoRange},
      {"cv_additive_range_affects_mix", TestCvAdditiveRangeAffectsMix},
      {"freeze_latching_sync_to_clock", TestFreezeLatchingSyncToClock},
      {"runtime_audio_context_switch", TestRuntimeAudioContextSwitch},
      {"required_dram_uses_max_supported_rate", TestRequiredDramUsesMaxSupportedRate},
      {"audio_context_clamps_max_supported_rate",
       TestAudioContextClampsToMaxSupportedRate},
      {"external_clock_timeout_stable_after_sr_change",
       TestExternalClockTimeoutStableAfterSampleRateChange},
      {"persistent_state_sanitises_invalid_enums",
       TestPersistentStateSanitisesInvalidEnums},
      {"c_api_guard_clauses", TestCApiGuardClauses},
      {"required_dram_rejects_invalid_large_config",
       TestRequiredDramRejectsInvalidLargeConfig},
      {"dropout_uses_smooth_edges", TestDropoutUsesSmoothEdges},
      {"dj_filter_tilt_response", TestDjFilterTiltResponse},
      {"vinyl_generates_surface_noise", TestVinylGeneratesSurfaceNoise},
      {"corrupt_algorithms_finite_at_extremes", TestCorruptAlgorithmsFiniteAtExtremes},
  };

  int failures = 0;
  for (const auto& t : tests) {
    bool ok = false;
    try {
      ok = t.fn();
    } catch (const std::exception& e) {
      ok = false;
      std::cerr << "[ERROR] " << t.name << ": exception: " << e.what() << "\n";
    } catch (...) {
      ok = false;
      std::cerr << "[ERROR] " << t.name << ": unknown exception\n";
    }

    if (!ok) {
      ++failures;
      std::cerr << "[FAIL] " << t.name << "\n";
    } else {
      std::cout << "[PASS] " << t.name << "\n";
    }
  }

  if (failures != 0) {
    std::cerr << failures << " test(s) failed\n";
    return 1;
  }
  return 0;
}
