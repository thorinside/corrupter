#ifndef CORRUPTER_TESTS_SPECTRAL_ENGINE_DRIVER_H_
#define CORRUPTER_TESTS_SPECTRAL_ENGINE_DRIVER_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "corrupter_dsp/engine.h"

namespace corrupter_spec {

struct DriverConfig {
  corrupter::EngineConfig cfg;
  corrupter::PersistentState state;
  corrupter::KnobState knobs;
  bool clock_mode_internal = true;
};

class EngineDriver {
 public:
  EngineDriver();

  bool Init(const DriverConfig& dc);

  void SetKnobs(const corrupter::KnobState& knobs);
  void SetPersistentState(const corrupter::PersistentState& state);

  // Drive `frames` mono samples through the engine, mirroring on L and R.
  // Output is stereo; `out_l` / `out_r` may be null if the caller does not
  // care about that channel. Both must be sized to >= frames if non-null.
  void ProcessMono(const float* in, float* out_l, float* out_r,
                   std::uint32_t frames);

  // Drive `frames` mono samples; return left channel into `out`.
  void ProcessMonoLeft(const float* in, float* out, std::uint32_t frames);

  // Drive a 2-block (sample-by-sample) loop with a per-sample knob update.
  // `update(i, KnobState&)` is called before each block of `frames_per_step`
  // samples to mutate the live knob state.
  using KnobUpdateFn = void (*)(std::uint32_t, corrupter::KnobState*);

  void ProcessMonoStepped(const float* in, float* out_l, float* out_r,
                          std::uint32_t frames,
                          std::uint32_t frames_per_step,
                          KnobUpdateFn update);

  float SampleRate() const { return cfg_.sample_rate_hz; }

 private:
  corrupter::EngineConfig cfg_{};
  corrupter::Engine engine_;
  std::vector<std::uint8_t> dram_;
  bool initialised_ = false;

  std::vector<float> scratch_in_l_;
  std::vector<float> scratch_in_r_;
  std::vector<float> scratch_out_l_;
  std::vector<float> scratch_out_r_;
};

}  // namespace corrupter_spec

#endif  // CORRUPTER_TESTS_SPECTRAL_ENGINE_DRIVER_H_
