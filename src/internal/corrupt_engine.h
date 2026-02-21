#ifndef CORRUPTER_DSP_INTERNAL_CORRUPT_ENGINE_H_
#define CORRUPTER_DSP_INTERNAL_CORRUPT_ENGINE_H_

#include "corrupter_dsp/engine.h"
#include "internal/prng.h"

namespace corrupter {
namespace internal {

struct CorruptChannelState {
  uint32_t decimate_hold_remaining = 0;
  float decimate_held = 0.0f;

  uint32_t dropout_remaining = 0;
  float dropout_gain = 1.0f;
  float dropout_start_gain = 1.0f;
  float dropout_target_gain = 1.0f;
  uint32_t dropout_ramp_remaining = 0;
  uint32_t dropout_ramp_samples = 0;

  float dj_ic1eq = 0.0f;
  float dj_ic2eq = 0.0f;
  float dj_prev_input = 0.0f;
  float dj_pos_smooth = 0.0f;

  float vinyl_lp = 0.0f;
  float vinyl_hiss_lp = 0.0f;
  float vinyl_rumble_lp = 0.0f;
  float vinyl_crackle_env = 0.0f;
  float vinyl_crackle_sign = 1.0f;
  float vinyl_pop_env = 0.0f;
  float vinyl_pop_sign = 1.0f;
  float vinyl_tone_hp = 0.0f;
  float vinyl_last_tone_in = 0.0f;
};

float ProcessCorruptSample(float x, float intensity, CorruptBank bank, CorruptAlgorithm algo,
                           CorruptChannelState* state, XorShift32* rng, float sample_rate_hz);

}  // namespace internal
}  // namespace corrupter

#endif  // CORRUPTER_DSP_INTERNAL_CORRUPT_ENGINE_H_
