#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>

#include "corrupter_dsp/engine.h"

int main() {
  constexpr uint32_t kBlockFrames = 256;
  constexpr uint32_t kBlocks = 4000;
  constexpr float kSampleRate = 96000.0f;

  corrupter::EngineConfig cfg;
  cfg.sample_rate_hz = kSampleRate;
  cfg.max_block_frames = kBlockFrames;
  cfg.max_buffer_seconds = 60.0f;
  cfg.random_seed = 1234;

  const size_t dram_bytes = corrupter::Engine::required_dram_bytes(cfg);
  std::vector<uint8_t> dram(dram_bytes);

  corrupter::Engine engine;
  if (!engine.initialise(dram.data(), dram.size(), cfg)) {
    std::cerr << "engine init failed\n";
    return 1;
  }

  corrupter::PersistentState state;
  state.macro_mode = true;
  state.bend_enabled = true;
  state.break_enabled = true;
  state.unique_stereo_mode = true;
  state.corrupt_bank = corrupter::CorruptBank::kExpanded;
  state.corrupt_algorithm = corrupter::CorruptAlgorithm::kVinylSim;
  state.glitch_window_01 = 0.4f;
  engine.set_persistent_state(state);

  corrupter::KnobState knobs;
  knobs.time_01 = 1.0f;
  knobs.repeats_01 = 0.75f;
  knobs.mix_01 = 1.0f;
  knobs.bend_01 = 0.8f;
  knobs.break_01 = 0.8f;
  knobs.corrupt_01 = 0.7f;
  engine.set_knobs(knobs);

  std::vector<float> in_l(kBlockFrames), in_r(kBlockFrames), out_l(kBlockFrames), out_r(kBlockFrames);
  for (uint32_t i = 0; i < kBlockFrames; ++i) {
    in_l[i] = (static_cast<float>(i) / static_cast<float>(kBlockFrames)) * 2.0f - 1.0f;
    in_r[i] = (static_cast<float>(kBlockFrames - i) / static_cast<float>(kBlockFrames)) * 2.0f - 1.0f;
  }

  std::vector<float> clock_gate(kBlockFrames, 0.0f);
  for (uint32_t i = 0; i < kBlockFrames; i += 48) {
    clock_gate[i] = 5.0f;
  }

  corrupter::AudioBlock audio;
  audio.in_l = in_l.data();
  audio.in_r = in_r.data();
  audio.out_l = out_l.data();
  audio.out_r = out_r.data();
  audio.frames = kBlockFrames;

  corrupter::GateInputs gates;
  gates.clock_gate_v = clock_gate.data();

  const auto start = std::chrono::high_resolution_clock::now();
  for (uint32_t i = 0; i < kBlocks; ++i) {
    engine.process(audio, {}, gates);
  }
  const auto end = std::chrono::high_resolution_clock::now();
  const std::chrono::duration<double> elapsed = end - start;

  const double rendered_seconds =
      static_cast<double>(kBlockFrames) * static_cast<double>(kBlocks) / static_cast<double>(kSampleRate);
  const double realtime_factor = rendered_seconds / elapsed.count();

  std::cout << "corrupter_dsp benchmark\n";
  std::cout << "rendered_seconds=" << rendered_seconds << "\n";
  std::cout << "elapsed_seconds=" << elapsed.count() << "\n";
  std::cout << "realtime_factor=" << realtime_factor << "\n";
  return 0;
}

