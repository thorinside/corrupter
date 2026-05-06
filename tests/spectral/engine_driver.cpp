#include "engine_driver.h"

#include <algorithm>
#include <cstring>

namespace corrupter_spec {

EngineDriver::EngineDriver() = default;

bool EngineDriver::Init(const DriverConfig& dc) {
  cfg_ = dc.cfg;
  const std::size_t need = corrupter::Engine::required_dram_bytes(cfg_);
  dram_.assign(need, 0u);
  if (!engine_.initialise(dram_.data(), dram_.size(), cfg_)) {
    return false;
  }
  engine_.set_persistent_state(dc.state);
  engine_.set_knobs(dc.knobs);
  engine_.set_clock_mode_internal(dc.clock_mode_internal);
  engine_.set_audio_context(cfg_.sample_rate_hz, cfg_.max_block_frames);

  scratch_in_l_.resize(cfg_.max_block_frames);
  scratch_in_r_.resize(cfg_.max_block_frames);
  scratch_out_l_.resize(cfg_.max_block_frames);
  scratch_out_r_.resize(cfg_.max_block_frames);
  initialised_ = true;
  return true;
}

void EngineDriver::SetKnobs(const corrupter::KnobState& knobs) {
  engine_.set_knobs(knobs);
}

void EngineDriver::SetPersistentState(
    const corrupter::PersistentState& state) {
  engine_.set_persistent_state(state);
}

void EngineDriver::ProcessMono(const float* in, float* out_l, float* out_r,
                               std::uint32_t frames) {
  if (!initialised_) return;
  std::uint32_t i = 0;
  while (i < frames) {
    const std::uint32_t n =
        std::min<std::uint32_t>(cfg_.max_block_frames, frames - i);
    for (std::uint32_t k = 0; k < n; ++k) {
      const float v = in[i + k];
      scratch_in_l_[k] = v;
      scratch_in_r_[k] = v;
    }

    corrupter::AudioBlock audio;
    audio.in_l = scratch_in_l_.data();
    audio.in_r = scratch_in_r_.data();
    audio.out_l = scratch_out_l_.data();
    audio.out_r = scratch_out_r_.data();
    audio.frames = n;
    corrupter::CvInputs cv;
    corrupter::GateInputs gates;
    engine_.process(audio, cv, gates);

    if (out_l) std::memcpy(out_l + i, scratch_out_l_.data(), n * sizeof(float));
    if (out_r) std::memcpy(out_r + i, scratch_out_r_.data(), n * sizeof(float));
    i += n;
  }
}

void EngineDriver::ProcessMonoLeft(const float* in, float* out,
                                   std::uint32_t frames) {
  ProcessMono(in, out, nullptr, frames);
}

void EngineDriver::ProcessMonoStepped(const float* in, float* out_l,
                                      float* out_r, std::uint32_t frames,
                                      std::uint32_t frames_per_step,
                                      KnobUpdateFn update) {
  if (!initialised_) return;
  if (frames_per_step == 0) frames_per_step = 1;

  corrupter::KnobState live_knobs;
  std::uint32_t i = 0;
  while (i < frames) {
    const std::uint32_t step_end =
        std::min<std::uint32_t>(frames, i + frames_per_step);
    if (update) update(i, &live_knobs);
    engine_.set_knobs(live_knobs);

    while (i < step_end) {
      const std::uint32_t remaining = step_end - i;
      const std::uint32_t n =
          std::min<std::uint32_t>(cfg_.max_block_frames, remaining);
      for (std::uint32_t k = 0; k < n; ++k) {
        const float v = in[i + k];
        scratch_in_l_[k] = v;
        scratch_in_r_[k] = v;
      }
      corrupter::AudioBlock audio;
      audio.in_l = scratch_in_l_.data();
      audio.in_r = scratch_in_r_.data();
      audio.out_l = scratch_out_l_.data();
      audio.out_r = scratch_out_r_.data();
      audio.frames = n;
      corrupter::CvInputs cv;
      corrupter::GateInputs gates;
      engine_.process(audio, cv, gates);
      if (out_l) std::memcpy(out_l + i, scratch_out_l_.data(), n * sizeof(float));
      if (out_r) std::memcpy(out_r + i, scratch_out_r_.data(), n * sizeof(float));
      i += n;
    }
  }
}

}  // namespace corrupter_spec
