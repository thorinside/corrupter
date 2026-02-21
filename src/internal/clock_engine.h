#ifndef CORRUPTER_DSP_INTERNAL_CLOCK_ENGINE_H_
#define CORRUPTER_DSP_INTERNAL_CLOCK_ENGINE_H_

#include <cstdint>

namespace corrupter {
namespace internal {

class ClockEngine {
 public:
  void Reset(float sample_rate_hz, float time_01);
  void SetInternalMode(bool internal_mode);
  void SetTime(float time_01);
  float CurrentPeriodSamples() const;
  bool Step(uint64_t sample_index, bool external_pulse);
  bool ExternalSignalPresent() const;

 private:
  static float ExternalRateMultiplier(float time_01);
  float ComputeInternalPeriodSamples() const;

  bool internal_mode_ = true;
  float sample_rate_hz_ = 96000.0f;
  float time_01_ = 0.5f;
  float external_period_samples_ = 48000.0f;
  float active_period_samples_ = 48000.0f;
  uint64_t next_tick_sample_ = 0;
  uint64_t last_external_pulse_sample_ = 0;
  bool have_external_pulse_ = false;
  bool external_signal_present_ = false;
};

}  // namespace internal
}  // namespace corrupter

#endif  // CORRUPTER_DSP_INTERNAL_CLOCK_ENGINE_H_
