#ifndef CORRUPTER_DSP_INTERNAL_PRNG_H_
#define CORRUPTER_DSP_INTERNAL_PRNG_H_

#include <cstdint>

namespace corrupter {
namespace internal {

struct XorShift32 {
  uint32_t state = 1;

  void Seed(uint32_t seed) {
    state = seed ? seed : 1;
  }

  uint32_t NextU32() {
    uint32_t x = state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state = x;
    return x;
  }

  float Next01() {
    return static_cast<float>(NextU32() & 0x00FFFFFFu) / 16777215.0f;
  }

  float NextSigned() {
    return Next01() * 2.0f - 1.0f;
  }
};

}  // namespace internal
}  // namespace corrupter

#endif  // CORRUPTER_DSP_INTERNAL_PRNG_H_

