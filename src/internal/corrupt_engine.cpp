#include "corrupt_engine.h"

#include <algorithm>
#include <cmath>

#include "dsp_common.h"

namespace corrupter {
namespace internal {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647693f;

float Quantize(float x, int bits) {
  bits = std::max(2, std::min(24, bits));
  const int levels = 1 << std::min(bits, 20);
  const float step = 2.0f / static_cast<float>(levels);
  return std::round(x / step) * step;
}

inline float SoftSat(float x, float drive) {
  return std::tanh(x * drive);
}

}  // namespace

float ProcessCorruptSample(float x, float intensity, CorruptBank bank, CorruptAlgorithm algo,
                           CorruptChannelState* state, XorShift32* rng, float sample_rate_hz) {
  intensity = Clamp01(intensity);
  if (!state || !rng) {
    return x;
  }
  const float sr = std::max(1.0f, sample_rate_hz);

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
        bool drop_active = false;
        if (state->dropout_remaining > 0u) {
          --state->dropout_remaining;
          drop_active = true;
        } else {
          const float event_prob = 0.00001f + intensity * 0.01f;
          if (rng->Next01() < event_prob) {
            const float max_dur_s = 0.20f - 0.18f * intensity;
            const float min_dur_s = 0.005f;
            const float dur_s =
                min_dur_s + rng->Next01() * std::max(0.0f, max_dur_s - min_dur_s);
            state->dropout_remaining =
                static_cast<uint32_t>(std::max(1.0f, dur_s * sr));
            drop_active = true;
          }
        }

        const float target_gain = drop_active ? 0.0f : 1.0f;
        if (std::fabs(target_gain - state->dropout_target_gain) > 1e-6f) {
          state->dropout_start_gain = state->dropout_gain;
          state->dropout_target_gain = target_gain;
          const float ramp_ms = 0.6f + 3.2f * intensity;
          state->dropout_ramp_samples =
              static_cast<uint32_t>(std::max(1.0f, ramp_ms * 0.001f * sr));
          state->dropout_ramp_remaining = state->dropout_ramp_samples;
        }

        if (state->dropout_ramp_remaining > 0u) {
          const float denom = static_cast<float>(std::max(uint32_t{1}, state->dropout_ramp_samples));
          const float remaining = static_cast<float>(state->dropout_ramp_remaining);
          const float t = 1.0f - (remaining / denom);
          const float shaped = 0.5f - 0.5f * std::cos(kPi * Clamp01(t));
          state->dropout_gain =
              state->dropout_start_gain +
              (state->dropout_target_gain - state->dropout_start_gain) * shaped;
          --state->dropout_ramp_remaining;
        } else {
          state->dropout_gain = state->dropout_target_gain;
        }

        return x * state->dropout_gain;
      }
      // Destroy: 2x oversampled tanh saturator (i<0.5) and hard clipper
      // (i>=0.5). 9-tap linear-phase half-band FIR for both upsample and
      // downsample image rejection. Coefficients sum to 1; the upsample
      // odd-tap pair carries a 2x gain correction for zero-stuffing so the
      // nonlinearity sees the full input amplitude.
      //
      //   h = [0, -0.05, 0, 0.30, 0.5, 0.30, 0, -0.05, 0]
      //
      // Net latency: ~2 samples at 96 kHz (neglected).

      // Shift input history (newest at [0])
      state->destroy_in[3] = state->destroy_in[2];
      state->destroy_in[2] = state->destroy_in[1];
      state->destroy_in[1] = state->destroy_in[0];
      state->destroy_in[0] = x;

      // Polyphase upsample (gain-corrected ×2)
      const float u_even = state->destroy_in[2];
      const float u_odd  = -0.10f * state->destroy_in[0]
                         +  0.60f * state->destroy_in[1]
                         +  0.60f * state->destroy_in[2]
                         -  0.10f * state->destroy_in[3];

      // Nonlinearity at 2x rate. Soft (tanh) and hard (clip) paths are
      // crossfaded with a smoothstep over intensity ∈ [0.45, 0.55] to
      // remove the derivative kink at i=0.5. Outside that band only one
      // path runs; inside both run and blend.
      const float drive_soft = 1.0f + intensity * 10.0f;
      const float t_hard = (intensity - 0.5f) * 2.0f;
      const float drive_hard = 6.0f + 24.0f * t_hard;
      const float clip_hard = 1.0f - 0.85f * t_hard;

      float w_even, w_odd;
      if (intensity <= 0.45f) {
        w_even = std::tanh(u_even * drive_soft);
        w_odd  = std::tanh(u_odd  * drive_soft);
      } else if (intensity >= 0.55f) {
        w_even = Clamp(u_even * drive_hard, -clip_hard, clip_hard);
        w_odd  = Clamp(u_odd  * drive_hard, -clip_hard, clip_hard);
      } else {
        const float u = (intensity - 0.45f) * 10.0f;  // 0..1
        const float blend = u * u * (3.0f - 2.0f * u);
        const float soft_e = std::tanh(u_even * drive_soft);
        const float soft_o = std::tanh(u_odd  * drive_soft);
        const float hard_e = Clamp(u_even * drive_hard, -clip_hard, clip_hard);
        const float hard_o = Clamp(u_odd  * drive_hard, -clip_hard, clip_hard);
        w_even = (1.0f - blend) * soft_e + blend * hard_e;
        w_odd  = (1.0f - blend) * soft_o + blend * hard_o;
      }

      // Shift upsample history right by 2, then push current pair.
      state->destroy_us[7] = state->destroy_us[5];
      state->destroy_us[6] = state->destroy_us[4];
      state->destroy_us[5] = state->destroy_us[3];
      state->destroy_us[4] = state->destroy_us[2];
      state->destroy_us[3] = state->destroy_us[1];
      state->destroy_us[2] = state->destroy_us[0];
      state->destroy_us[1] = w_even;  // w[2k]
      state->destroy_us[0] = w_odd;   // w[2k+1]

      // Polyphase downsample (no gain correction; coefficients sum to 1)
      return -0.05f * state->destroy_us[1]
           +  0.30f * state->destroy_us[3]
           +  0.50f * state->destroy_us[4]
           +  0.30f * state->destroy_us[5]
           -  0.05f * state->destroy_us[7];
    }

    case CorruptBank::kExpanded: {
      if (algo == CorruptAlgorithm::kDjFilter) {
        // 2x oversampled TPT SVF with input interpolation and DJ-style sweep law.
        const float target_pos = (intensity - 0.5f) * 2.0f;  // -1..1
        const float pos_slew = 1.0f - std::exp(-1.0f / (0.015f * sr));
        state->dj_pos_smooth += pos_slew * (target_pos - state->dj_pos_smooth);
        const float centered = Clamp(state->dj_pos_smooth, -1.0f, 1.0f);
        const float abs_c = std::fabs(centered);
        const float resonance = 0.10f + 0.55f * std::pow(abs_c, 0.8f);
        const float k = 2.0f - 2.0f * resonance;
        const float drive = 1.0f + 2.2f * abs_c;

        const float os_sr = sr * 2.0f;
        float cutoff = 18000.0f;
        if (centered < 0.0f) {
          const float amt = -centered;
          cutoff = 70.0f + std::pow(1.0f - amt, 2.2f) * (20000.0f - 70.0f);
        } else if (centered > 0.0f) {
          cutoff = 35.0f + std::pow(centered, 1.6f) * (18000.0f - 35.0f);
        }
        cutoff = Clamp(cutoff, 20.0f, 20000.0f);
        const float g = std::tan(kPi * cutoff / os_sr);
        const float a1 = 1.0f / (1.0f + g * (g + k));

        const float x0 = 0.5f * (state->dj_prev_input + x);
        const float x1 = x;
        float y = 0.0f;
        for (int os = 0; os < 2; ++os) {
          const float x_os = (os == 0) ? x0 : x1;
          const float v0 = SoftSat(x_os, drive);
          const float v3 = v0 - state->dj_ic2eq;
          const float v1 = a1 * (state->dj_ic1eq + g * v3);
          const float v2 = state->dj_ic2eq + g * v1;
          state->dj_ic1eq = 2.0f * v1 - state->dj_ic1eq;
          state->dj_ic2eq = 2.0f * v2 - state->dj_ic2eq;

          const float lp = v2;
          const float hp = v0 - k * v1 - v2;
          const float blend = Clamp(abs_c * 1.15f, 0.0f, 1.0f);
          const float filt = (centered < 0.0f) ? lp : hp;
          y += (1.0f - blend) * x_os + blend * filt;
        }
        state->dj_prev_input = x;
        return 0.92f * (0.5f * y);
      }

      // Vinyl: rumble, hiss, crackle, pops, and worn EQ profile.
      const float white = rng->NextSigned();
      const float rumble_cut = 18.0f + 45.0f * intensity;
      const float rumble_a = 1.0f - std::exp(-kTwoPi * rumble_cut / sr);
      state->vinyl_rumble_lp += rumble_a * (white - state->vinyl_rumble_lp);
      const float rumble = state->vinyl_rumble_lp * (0.0010f + 0.016f * intensity);

      const float hiss_cut = 5200.0f + 1200.0f * intensity;
      const float hiss_a = 1.0f - std::exp(-kTwoPi * hiss_cut / sr);
      state->vinyl_hiss_lp += hiss_a * (white - state->vinyl_hiss_lp);
      const float hiss_hp = white - state->vinyl_hiss_lp;
      const float hiss = hiss_hp * (0.0008f + 0.010f * intensity);

      if (state->vinyl_crackle_env <= 0.0001f &&
          rng->Next01() < (0.00012f + 0.0018f * intensity)) {
        state->vinyl_crackle_env = 1.0f;
        state->vinyl_crackle_sign = (rng->Next01() < 0.5f) ? -1.0f : 1.0f;
      }
      const float crackle = state->vinyl_crackle_sign * state->vinyl_crackle_env *
                            (0.006f + 0.090f * intensity);
      const float crackle_tau_s = 0.00060f - 0.00025f * intensity;
      const float crackle_decay = std::exp(-1.0f / std::max(1.0f, crackle_tau_s * sr));
      state->vinyl_crackle_env *= crackle_decay;

      if (state->vinyl_pop_env <= 0.0001f &&
          rng->Next01() < (0.00001f + 0.00025f * intensity)) {
        state->vinyl_pop_env = 1.0f;
        state->vinyl_pop_sign = (rng->Next01() < 0.5f) ? -1.0f : 1.0f;
      }
      const float pop = state->vinyl_pop_sign * state->vinyl_pop_env *
                        (0.010f + 0.150f * intensity);
      const float pop_tau_s = 0.004f + 0.010f * intensity;
      const float pop_decay = std::exp(-1.0f / std::max(1.0f, pop_tau_s * sr));
      state->vinyl_pop_env *= pop_decay;

      const float noisy = x + rumble + hiss + crackle + pop;
      const float driven = std::tanh(noisy * (1.0f + 0.8f * intensity));

      const float lp_cut = 4500.0f + (1.0f - intensity) * 11500.0f;
      const float lp_a = std::exp(-kTwoPi * lp_cut / sr);
      state->vinyl_lp = (1.0f - lp_a) * driven + lp_a * state->vinyl_lp;

      const float hp_cut = 18.0f + 22.0f * intensity;
      const float hp_a = std::exp(-kTwoPi * hp_cut / sr);
      state->vinyl_tone_hp = hp_a * (state->vinyl_tone_hp + state->vinyl_lp -
                                     state->vinyl_last_tone_in);
      state->vinyl_last_tone_in = state->vinyl_lp;

      const float blend = 0.35f + 0.65f * intensity;
      return (1.0f - blend) * x + blend * (0.90f * state->vinyl_tone_hp);
    }
  }

  return x;
}

}  // namespace internal
}  // namespace corrupter
