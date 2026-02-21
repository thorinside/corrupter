#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>

#include "corrupter_dsp/engine.h"

int main() {
  corrupter::EngineConfig cfg;
  cfg.sample_rate_hz = 96000.0f;
  cfg.max_block_frames = 256;
  cfg.max_buffer_seconds = 60.0f;
  cfg.random_seed = 12345;

  const size_t dram_bytes = corrupter::Engine::required_dram_bytes(cfg);
  std::vector<uint8_t> dram(dram_bytes);

  corrupter::Engine engine;
  if (!engine.initialise(dram.data(), dram.size(), cfg)) {
    std::cerr << "Failed to initialize engine\n";
    return 1;
  }

  corrupter::KnobState knobs;
  knobs.mix_01 = 0.0f;
  engine.set_knobs(knobs);

  const uint32_t frames = 128;
  std::vector<float> in_l(frames), in_r(frames), out_l(frames), out_r(frames);
  for (uint32_t i = 0; i < frames; ++i) {
    in_l[i] = (i % 32u) / 31.0f;
    in_r[i] = ((31u - (i % 32u)) / 31.0f);
  }

  corrupter::AudioBlock audio;
  audio.in_l = in_l.data();
  audio.in_r = in_r.data();
  audio.out_l = out_l.data();
  audio.out_r = out_r.data();
  audio.frames = frames;

  engine.process(audio, {}, {});

  const float peak_l = *std::max_element(out_l.begin(), out_l.end());
  const float peak_r = *std::max_element(out_r.begin(), out_r.end());

  std::cout << "corrupter_dsp smoke test\n";
  std::cout << "dram_bytes=" << dram_bytes << "\n";
  std::cout << "peak_l=" << peak_l << " peak_r=" << peak_r << "\n";
  return 0;
}

