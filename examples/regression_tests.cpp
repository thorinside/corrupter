#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "corrupter_dsp/c_api.h"
#include "corrupter_dsp/engine.h"

namespace {

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

StereoBuffers RunScenarioC(const Scenario& s, const ModulationBuffers* mod = nullptr) {
  StereoBuffers buf = MakeInput(s.frames);

  const size_t engine_bytes = corrupter_engine_sizeof();
  std::vector<uint8_t> engine_mem(engine_bytes);

  if (!corrupter_engine_construct(engine_mem.data(), engine_mem.size())) {
    throw std::runtime_error("c_api construct failed");
  }

  corrupter_engine_config_t cfg{};
  cfg.sample_rate_hz = s.cfg.sample_rate_hz;
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

  s1.knobs.time_01 = 1.0f;
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
      {"c_api_parity", TestCApiParity},
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
