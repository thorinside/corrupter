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
      next_tick_sample_ = sample_index + static_cast<uint64_t>(std::max(1.0f, period));
    }
    return tick;
  }

  if (external_pulse) {
    if (have_external_pulse_) {
      const uint64_t elapsed = sample_index - last_external_pulse_sample_;
      external_period_samples_ = std::max(1.0f, static_cast<float>(elapsed));
    }
    last_external_pulse_sample_ = sample_index;
    have_external_pulse_ = true;
    external_signal_present_ = true;

    const float mult = ExternalRateMultiplier(time_01_);
    active_period_samples_ = std::max(1.0f, external_period_samples_ / mult);
    tick = true;
    next_tick_sample_ = sample_index + static_cast<uint64_t>(active_period_samples_);
    return tick;
  }

  if (have_external_pulse_) {
    const uint64_t timeout =
        static_cast<uint64_t>(std::max(1.0f, external_period_samples_ * 4.0f));
    if ((sample_index - last_external_pulse_sample_) >= timeout) {
      external_signal_present_ = false;
    }
  }

  if (sample_index >= next_tick_sample_) {
    tick = true;
    next_tick_sample_ = sample_index + static_cast<uint64_t>(CurrentPeriodSamples());
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
  const float seconds = std::exp(log_max + (log_min - log_max) * Clamp01(time_01_));
  return std::max(1.0f, seconds * sample_rate_hz_);
}

}  // namespace internal
}  // namespace corrupter
