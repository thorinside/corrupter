#ifndef CORRUPTER_DSP_INTERNAL_DSP_COMMON_H_
#define CORRUPTER_DSP_INTERNAL_DSP_COMMON_H_

#include <algorithm>

namespace corrupter {
namespace internal {

constexpr float kGateThresholdV = 0.4f;
constexpr float kCvRangeV = 5.0f;
constexpr unsigned kAlignment = 64u;

inline float Clamp(float x, float lo, float hi) {
  return std::max(lo, std::min(hi, x));
}

inline float Clamp01(float x) {
  return Clamp(x, 0.0f, 1.0f);
}

inline bool GateHigh(float volts) {
  return volts >= kGateThresholdV;
}

inline bool RisingEdge(bool current, bool previous) {
  return current && !previous;
}

inline float WrapPositive(float x, float n) {
  if (n <= 0.0f) {
    return 0.0f;
  }
  while (x >= n) {
    x -= n;
  }
  while (x < 0.0f) {
    x += n;
  }
  if (x >= n) {
    x = 0.0f;
  }
  return x;
}

inline double WrapPositive(double x, double n) {
  if (n <= 0.0) {
    return 0.0;
  }
  while (x >= n) {
    x -= n;
  }
  while (x < 0.0) {
    x += n;
  }
  if (x >= n) {
    x = 0.0;
  }
  return x;
}

}  // namespace internal
}  // namespace corrupter

#endif  // CORRUPTER_DSP_INTERNAL_DSP_COMMON_H_

