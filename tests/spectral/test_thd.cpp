#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "corrupter_dsp/engine.h"
#include "internal/corrupt_engine.h"
#include "internal/prng.h"

#include "spectral_helpers.h"
#include "test_framework.h"

namespace corrupter_spec {
namespace {

constexpr float kSr = 96000.0f;
constexpr std::size_t kFftN = 16384;
constexpr std::size_t kSettleN = 4096;

float RunThd(corrupter::CorruptBank bank, corrupter::CorruptAlgorithm algo,
             float intensity, float f0_hz, float input_amp,
             const std::string& csv_prefix) {
  corrupter::internal::CorruptChannelState state{};
  corrupter::internal::XorShift32 rng;
  rng.Seed(0xC0FFEE);

  std::vector<float> input(kSettleN + kFftN, 0.0f);
  GenSine(input.data(), input.size(), f0_hz, kSr, input_amp);
  std::vector<float> output(input.size(), 0.0f);
  for (std::size_t i = 0; i < input.size(); ++i) {
    output[i] = corrupter::internal::ProcessCorruptSample(
        input[i], intensity, bank, algo, &state, &rng, kSr);
  }

  std::vector<std::complex<float>> spec(kFftN);
  Fft(output.data() + kSettleN, kFftN, spec.data());

  if (!csv_prefix.empty()) {
    WriteSpectrumCsv(OutputPath(csv_prefix + "_spectrum.csv"),
                     spec.data(), kFftN, kSr);
  }

  return ThdPlusNPct(spec.data(), kFftN, kSr, f0_hz);
}

bool TestThdDestroySoft() {
  // Snap to a bin-aligned tone for clean fundamental measurement.
  const float bin_width = kSr / static_cast<float>(kFftN);
  const float f0 = std::round(1000.0f / bin_width) * bin_width;
  const float thd = RunThd(corrupter::CorruptBank::kLegacy,
                           corrupter::CorruptAlgorithm::kDestroy,
                           0.30f, f0, 0.30f, "thd_destroy_soft");
  std::cerr << "  destroy-soft THD+N: " << thd << " %\n";
  // Soft saturator at intensity=0.3 with amp=0.3: drive = 4, tanh(0.3*4)=tanh(1.2)
  // produces moderate harmonics. Currently ~8.9 %; gate at 12 %.
  CORRUPTER_SPEC_REQUIRE(thd < 12.0f, "destroy-soft THD+N below 12 %");
  return true;
}

bool TestThdDestroyHard() {
  const float bin_width = kSr / static_cast<float>(kFftN);
  const float f0 = std::round(1000.0f / bin_width) * bin_width;
  const float thd = RunThd(corrupter::CorruptBank::kLegacy,
                           corrupter::CorruptAlgorithm::kDestroy,
                           0.70f, f0, 0.30f, "thd_destroy_hard");
  std::cerr << "  destroy-hard THD+N: " << thd << " % (snapshot)\n";
  // Snapshot only — hard clip is by-design feature.
  return true;
}

bool TestThdDjFilter() {
  const float bin_width = kSr / static_cast<float>(kFftN);
  const float f0 = std::round(1000.0f / bin_width) * bin_width;
  const float thd = RunThd(corrupter::CorruptBank::kExpanded,
                           corrupter::CorruptAlgorithm::kDjFilter,
                           0.5f, f0, 0.30f, "thd_djfilter");
  std::cerr << "  djfilter THD+N: " << thd << " %\n";
  // 2x oversampled with mild soft-sat at center (drive ~ 1, tanh(0.3) is nearly
  // linear). Currently ~0.004 %; gate at 0.1 % (25× headroom).
  CORRUPTER_SPEC_REQUIRE(thd < 0.1f, "djfilter THD+N below 0.1 %");
  return true;
}

bool TestThdVinyl() {
  const float bin_width = kSr / static_cast<float>(kFftN);
  const float f0 = std::round(1000.0f / bin_width) * bin_width;
  const float thd = RunThd(corrupter::CorruptBank::kExpanded,
                           corrupter::CorruptAlgorithm::kVinylSim,
                           0.5f, f0, 0.30f, "thd_vinyl");
  std::cerr << "  vinyl THD+N (incl. surface noise): " << thd << " %\n";
  // Vinyl includes engineered surface noise — THD+N high by design. Snapshot.
  return true;
}

}  // namespace

void RegisterThdTests(std::vector<TestCase>& out) {
  out.push_back({"T-THD-Destroy-Soft", TestThdDestroySoft});
  out.push_back({"T-THD-Destroy-Hard", TestThdDestroyHard});
  out.push_back({"T-THD-DjFilter", TestThdDjFilter});
  out.push_back({"T-THD-Vinyl", TestThdVinyl});
}

}  // namespace corrupter_spec
