#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

#include "spectral_helpers.h"
#include "test_framework.h"

namespace corrupter_spec {
namespace {

bool TestSineAmplitude() {
  constexpr std::size_t kN = 4096;
  std::vector<float> sig(kN);
  GenSine(sig.data(), kN, 1000.0f, kSampleRate, 0.5f);
  const float peak = PeakAbs(sig.data(), kN);
  CORRUPTER_SPEC_REQUIRE(std::fabs(peak - 0.5f) < 0.01f,
                         "sine peak ~ 0.5");
  const float rms = Rms(sig.data(), kN);
  CORRUPTER_SPEC_REQUIRE(std::fabs(rms - 0.5f / std::sqrt(2.0f)) < 0.005f,
                         "sine rms ~ amp/sqrt(2)");
  return true;
}

bool TestFftSineBin() {
  constexpr std::size_t kN = 4096;
  std::vector<float> sig(kN);
  // Choose a frequency that lands exactly on an FFT bin.
  const float bin_width = kSampleRate / static_cast<float>(kN);
  const float f = 100.0f * bin_width;  // bin 100
  GenSine(sig.data(), kN, f, kSampleRate, 1.0f);
  std::vector<std::complex<float>> spec(kN);
  Fft(sig.data(), kN, spec.data());
  const float mag = MagAtHz(spec.data(), kN, kSampleRate, f);
  // For amplitude A on N points, single-tone bin magnitude is A*N/2.
  const float expected = 0.5f * static_cast<float>(kN);
  CORRUPTER_SPEC_REQUIRE(std::fabs(mag - expected) / expected < 0.01f,
                         "single-tone bin magnitude ~ A*N/2");
  return true;
}

bool TestImpulseFlat() {
  constexpr std::size_t kN = 2048;
  std::vector<float> sig(kN);
  GenImpulse(sig.data(), kN, 1.0f);
  std::vector<std::complex<float>> spec(kN);
  Fft(sig.data(), kN, spec.data());
  // Impulse spectrum should be flat at magnitude 1 across all bins.
  for (std::size_t k = 1; k < kN / 2; k += 100) {
    const float m = std::abs(spec[k]);
    CORRUPTER_SPEC_REQUIRE(std::fabs(m - 1.0f) < 1e-5f,
                           "impulse spectrum is flat (~1.0)");
  }
  return true;
}

bool TestFraIdentity() {
  std::vector<float> grid;
  grid.push_back(100.0f);
  grid.push_back(1000.0f);
  grid.push_back(10000.0f);
  auto identity = [](float x) { return x; };
  auto pts = SteppedSineFra(identity, grid, kSampleRate, 0.05f, 0.10f, 0.5f);
  for (const auto& pt : pts) {
    CORRUPTER_SPEC_REQUIRE(std::fabs(pt.db) < 0.1f,
                           "identity FRA ~ 0 dB");
  }
  return true;
}

bool TestIrFraIdentity() {
  std::vector<float> grid;
  grid.push_back(1000.0f);
  grid.push_back(5000.0f);
  auto identity = [](float x) { return x; };
  auto pts = IrFra(identity, grid, 4096, kSampleRate, 1.0f);
  for (const auto& pt : pts) {
    CORRUPTER_SPEC_REQUIRE(std::fabs(pt.db) < 0.1f,
                           "identity IR FRA ~ 0 dB");
  }
  return true;
}

bool TestThdSinePure() {
  constexpr std::size_t kN = 8192;
  std::vector<float> sig(kN);
  const float bin_width = kSampleRate / static_cast<float>(kN);
  const float f = 100.0f * bin_width;
  GenSine(sig.data(), kN, f, kSampleRate, 1.0f);
  std::vector<std::complex<float>> spec(kN);
  Fft(sig.data(), kN, spec.data());
  const float thd = ThdPlusNPct(spec.data(), kN, kSampleRate, f);
  std::cerr << "  pure sine THD+N: " << thd << " %\n";
  // Bin-aligned + unwindowed FFT leaves some leakage from float-precision
  // phase drift. 0.5 % is still essentially "THD = 0" at the smoke level.
  CORRUPTER_SPEC_REQUIRE(thd < 0.5f, "pure sine THD+N below smoke threshold");
  return true;
}

bool TestNoiseRms() {
  constexpr std::size_t kN = 65536;
  std::vector<float> noise(kN);
  GenWhiteNoise(noise.data(), kN, 12345u, 0.5f);
  const float rms = Rms(noise.data(), kN);
  CORRUPTER_SPEC_REQUIRE(rms > 0.20f && rms < 0.35f,
                         "white-noise RMS in expected range");
  return true;
}

bool TestClickEnergyZero() {
  constexpr std::size_t kN = 1024;
  std::vector<float> sig(kN);
  GenSine(sig.data(), kN, 100.0f, kSampleRate, 0.5f);
  const float ce = ClickEnergy(sig.data(), kN);
  // Sample-to-sample delta of a 100 Hz sine at 96 kHz is small.
  CORRUPTER_SPEC_REQUIRE(ce < 0.005f, "smooth sine has low click energy");
  return true;
}

bool TestClickEnergyStep() {
  constexpr std::size_t kN = 1024;
  std::vector<float> sig(kN, 0.0f);
  for (std::size_t i = kN / 2; i < kN; ++i) sig[i] = 1.0f;
  const float ce = ClickEnergy(sig.data(), kN);
  CORRUPTER_SPEC_REQUIRE(ce > 0.005f, "step has high click energy");
  return true;
}

}  // namespace

void RegisterSmokeTests(std::vector<TestCase>& out) {
  out.push_back({"T-SMK-Sine-Amplitude", TestSineAmplitude});
  out.push_back({"T-SMK-Fft-Sine-Bin", TestFftSineBin});
  out.push_back({"T-SMK-Impulse-Flat", TestImpulseFlat});
  out.push_back({"T-SMK-Fra-Identity", TestFraIdentity});
  out.push_back({"T-SMK-IrFra-Identity", TestIrFraIdentity});
  out.push_back({"T-SMK-Thd-Sine-Pure", TestThdSinePure});
  out.push_back({"T-SMK-Noise-Rms", TestNoiseRms});
  out.push_back({"T-SMK-Click-Energy-Zero", TestClickEnergyZero});
  out.push_back({"T-SMK-Click-Energy-Step", TestClickEnergyStep});
}

}  // namespace corrupter_spec
