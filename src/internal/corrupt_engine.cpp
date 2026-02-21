#include "corrupt_engine.h"

#include <algorithm>
#include <cmath>

#include "dsp_common.h"

namespace corrupter {
namespace internal {
namespace {

float Quantize(float x, int bits) {
  bits = std::max(2, std::min(24, bits));
  const int levels = 1 << std::min(bits, 20);
  const float step = 2.0f / static_cast<float>(levels);
  return std::round(x / step) * step;
}

}  // namespace

float ProcessCorruptSample(float x, float intensity, CorruptBank bank, CorruptAlgorithm algo,
                           CorruptChannelState* state, XorShift32* rng, float sample_rate_hz) {
  intensity = Clamp01(intensity);
  if (!state || !rng) {
    return x;
  }

  switch (bank) {
    case CorruptBank::kLegacy: {
      if (algo == CorruptAlgorithm::kDecimate) {
        const uint32_t hold = 1u + static_cast<uint32_t>(intensity * 127.0f);
        const int bits = 24 - static_cast<int>(intensity * 20.0f);
        if (state->decimate_hold_remaining == 0) {
          state->decimate_held = Quantize(x, bits);
          state->decimate_hold_remaining = hold;
        }
        if (state->decimate_hold_remaining > 0) {
          --state->decimate_hold_remaining;
        }
        return state->decimate_held;
      }
      if (algo == CorruptAlgorithm::kDropout) {
        if (state->dropout_remaining > 0) {
          --state->dropout_remaining;
          return 0.0f;
        }

        const float event_prob = 0.00001f + intensity * 0.01f;
        if (rng->Next01() < event_prob) {
          const float max_dur_s = 0.20f - 0.18f * intensity;
          const float min_dur_s = 0.005f;
          const float dur_s = min_dur_s + rng->Next01() * std::max(0.0f, max_dur_s - min_dur_s);
          state->dropout_remaining = static_cast<uint32_t>(std::max(1.0f, dur_s * sample_rate_hz));
          return 0.0f;
        }
        return x;
      }
      if (intensity < 0.5f) {
        const float drive = 1.0f + intensity * 10.0f;
        return std::tanh(x * drive);
      }
      const float t = (intensity - 0.5f) * 2.0f;
      const float drive = 6.0f + 24.0f * t;
      const float clip = 1.0f - 0.85f * t;
      return Clamp(x * drive, -clip, clip);
    }

    case CorruptBank::kExpanded: {
      if (algo == CorruptAlgorithm::kDjFilter) {
        const float centered = (intensity - 0.5f) * 2.0f;
        if (std::fabs(centered) < 0.05f) {
          return x;
        }

        if (centered < 0.0f) {
          const float amt = -centered;
          const float cutoff = 80.0f + (1.0f - amt) * 19920.0f;
          const float a = std::exp(-2.0f * 3.14159265f * cutoff / sample_rate_hz);
          state->filter_lp = (1.0f - a) * x + a * state->filter_lp;
          return state->filter_lp;
        }

        const float amt = centered;
        const float cutoff = 20.0f + amt * 12000.0f;
        const float a = std::exp(-2.0f * 3.14159265f * cutoff / sample_rate_hz);
        state->filter_lp = (1.0f - a) * x + a * state->filter_lp;
        return x - state->filter_lp;
      }

      const float noise = rng->NextSigned() * (0.0008f + 0.02f * intensity);
      float crackle = 0.0f;
      if (rng->Next01() < 0.0005f * intensity) {
        crackle = rng->NextSigned() * (0.1f + 0.4f * intensity);
      }

      const float y = x + noise + crackle;
      const float cutoff = 3500.0f + (1.0f - intensity) * 11500.0f;
      const float a = std::exp(-2.0f * 3.14159265f * cutoff / sample_rate_hz);
      state->vinyl_lp = (1.0f - a) * y + a * state->vinyl_lp;
      return 0.85f * state->vinyl_lp;
    }
  }

  return x;
}

}  // namespace internal
}  // namespace corrupter
