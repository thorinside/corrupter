#include "clock_engine.h"

#include <algorithm>
#include <cmath>

#include "dsp_common.h"

namespace corrupter {
namespace internal {

namespace {

constexpr float kMinInternalPeriodSeconds = 0.0125f;
constexpr float kMaxInternalPeriodSeconds = 16.0f;

}  // namespace

void ClockEngine::Reset(float sample_rate_hz, float time_01) {
  sample_rate_hz_ = std::max(1.0f, sample_rate_hz);
  time_01_ = Clamp01(time_01);
  external_period_samples_ = sample_rate_hz_ * 0.5f;
  active_period_samples_ = ComputeInternalPeriodSamples();
  next_tick_sample_ = 0;
  last_external_pulse_sample_ = 0;
  have_external_pulse_ = false;
  external_signal_present_ = false;
}

void ClockEngine::SetSampleRate(float sample_rate_hz, uint64_t current_sample_index) {
  const float new_sr = std::max(1.0f, sample_rate_hz);
  const float old_sr = std::max(1.0f, sample_rate_hz_);
  const float ratio = new_sr / old_sr;
  sample_rate_hz_ = new_sr;

  external_period_samples_ = std::max(1.0f, external_period_samples_ * ratio);
  active_period_samples_ = std::max(1.0f, active_period_samples_ * ratio);

  if (next_tick_sample_ > current_sample_index) {
    const uint32_t remaining = static_cast<uint32_t>(next_tick_sample_ - current_sample_index);
    const uint32_t scaled = static_cast<uint32_t>(std::max(1.0f, static_cast<float>(remaining) * ratio));
    next_tick_sample_ = current_sample_index + scaled;
  } else {
    next_tick_sample_ = current_sample_index;
  }

  if (have_external_pulse_) {
    if (current_sample_index >= last_external_pulse_sample_) {
      const uint32_t elapsed = static_cast<uint32_t>(current_sample_index - last_external_pulse_sample_);
      const uint32_t scaled_elapsed =
          static_cast<uint32_t>(std::max(0.0f, static_cast<float>(elapsed) * ratio));
      if (scaled_elapsed >= static_cast<uint32_t>(current_sample_index)) {
        last_external_pulse_sample_ = 0;
      } else {
        last_external_pulse_sample_ = current_sample_index - scaled_elapsed;
      }
    } else {
      last_external_pulse_sample_ = current_sample_index;
    }
  }

  if (internal_mode_) {
    active_period_samples_ = ComputeInternalPeriodSamples();
  }
}

void ClockEngine::SetInternalMode(bool internal_mode) {
  internal_mode_ = internal_mode;
}

void ClockEngine::SetTime(float time_01) {
  time_01_ = Clamp01(time_01);
  if (internal_mode_) {
    active_period_samples_ = ComputeInternalPeriodSamples();
  } else if (have_external_pulse_) {
    const float mult = ExternalRateMultiplier(time_01_);
    active_period_samples_ = std::max(1.0f, external_period_samples_ / mult);
  }
}

float ClockEngine::CurrentPeriodSamples() const {
  return std::max(1.0f, active_period_samples_);
}

bool ClockEngine::Step(uint64_t sample_index, bool external_pulse) {
  bool tick = false;

  if (internal_mode_) {
    external_signal_present_ = false;
    const float period = ComputeInternalPeriodSamples();
    active_period_samples_ = period;
    if (sample_index >= next_tick_sample_) {
      tick = true;
      next_tick_sample_ = sample_index + static_cast<uint32_t>(std::max(1.0f, period));
    }
    return tick;
  }

  if (external_pulse) {
    if (!have_external_pulse_) {
      have_external_pulse_ = true;
      last_external_pulse_sample_ = sample_index;
      external_signal_present_ = true;

      const float mult = ExternalRateMultiplier(time_01_);
      active_period_samples_ = std::max(1.0f, external_period_samples_ / mult);
      next_tick_sample_ = sample_index;
    } else {
      const uint32_t elapsed = static_cast<uint32_t>(sample_index - last_external_pulse_sample_);
      external_period_samples_ = std::max(1.0f, static_cast<float>(elapsed));
      last_external_pulse_sample_ = sample_index;
      external_signal_present_ = true;

      const float mult = ExternalRateMultiplier(time_01_);
      active_period_samples_ = std::max(1.0f, external_period_samples_ / mult);

      const uint32_t period_u =
          static_cast<uint32_t>(std::max(1.0f, active_period_samples_));
      if (sample_index > next_tick_sample_ &&
          (sample_index - next_tick_sample_) > (period_u / 2u)) {
        next_tick_sample_ = sample_index;
      }
      if (next_tick_sample_ > (sample_index + period_u)) {
        next_tick_sample_ = sample_index;
      }
    }
  }

  if (have_external_pulse_) {
    const uint32_t timeout =
        static_cast<uint32_t>(std::max(1.0f, external_period_samples_ * 4.0f));
    if ((sample_index - last_external_pulse_sample_) >= timeout) {
      external_signal_present_ = false;
    }
  }

  if (sample_index >= next_tick_sample_) {
    tick = true;
    const uint32_t period_u =
        static_cast<uint32_t>(std::max(1.0f, CurrentPeriodSamples()));
    next_tick_sample_ += period_u;
    if (next_tick_sample_ <= sample_index) {
      next_tick_sample_ = sample_index + period_u;
    }
  }

  return tick;
}

bool ClockEngine::ExternalSignalPresent() const {
  return external_signal_present_;
}

float ClockEngine::ExternalRateMultiplier(float time_01) {
  static constexpr float kTable[9] = {
      1.0f / 16.0f, 1.0f / 8.0f, 1.0f / 4.0f, 1.0f / 2.0f, 1.0f, 2.0f, 3.0f, 4.0f, 8.0f};
  int idx = static_cast<int>(time_01 * 9.0f);
  idx = std::max(0, std::min(8, idx));
  return kTable[idx];
}

float ClockEngine::ComputeInternalPeriodSamples() const {
  const float log_min = std::log(kMinInternalPeriodSeconds);
  const float log_max = std::log(kMaxInternalPeriodSeconds);
  const float seconds = std::exp(log_min + (log_max - log_min) * Clamp01(time_01_));
  return std::max(1.0f, seconds * sample_rate_hz_);
}

}  // namespace internal
}  // namespace corrupter
