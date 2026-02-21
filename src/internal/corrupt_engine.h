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

  float filter_lp = 0.0f;
  float vinyl_lp = 0.0f;
};

float ProcessCorruptSample(float x, float intensity, CorruptBank bank, CorruptAlgorithm algo,
                           CorruptChannelState* state, XorShift32* rng, float sample_rate_hz);

}  // namespace internal
}  // namespace corrupter

#endif  // CORRUPTER_DSP_INTERNAL_CORRUPT_ENGINE_H_

