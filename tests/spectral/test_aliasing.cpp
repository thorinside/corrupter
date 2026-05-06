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
constexpr std::size_t kFftN = 8192;
constexpr std::size_t kSettleN = 4096;

struct AlgoConfig {
  const char* tag;
  corrupter::CorruptBank bank;
  corrupter::CorruptAlgorithm algo;
  float intensity;
  float input_amp;
};

struct AliasResult {
  float fundamental_dbfs;
  float worst_alias_dbfs;
  float worst_alias_hz;
};

AliasResult MeasureAliasing(const AlgoConfig& cfg, float f_in_hz,
                            const std::string& csv_prefix) {
  corrupter::internal::CorruptChannelState state{};
  corrupter::internal::XorShift32 rng;
  rng.Seed(0xC0FFEE);

  std::vector<float> input(kSettleN + kFftN, 0.0f);
  GenSine(input.data(), input.size(), f_in_hz, kSr, cfg.input_amp);

  std::vector<float> output(input.size(), 0.0f);
  for (std::size_t i = 0; i < input.size(); ++i) {
    output[i] = corrupter::internal::ProcessCorruptSample(
        input[i], cfg.intensity, cfg.bank, cfg.algo, &state, &rng, kSr);
  }

  std::vector<std::complex<float>> spec(kFftN);
  Fft(output.data() + kSettleN, kFftN, spec.data());

  AliasResult r{};
  const float fund_mag = MagAtHz(spec.data(), kFftN, kSr, f_in_hz);
  r.fundamental_dbfs = MagToDbfs(fund_mag, kFftN);

  const float bin_width = kSr / static_cast<float>(kFftN);
  const std::size_t fund_bin =
      static_cast<std::size_t>(std::lround(f_in_hz / bin_width));
  const std::size_t guard = 8;  // skip leakage around fundamental
  const std::size_t max_bin = kFftN / 2;

  // Compute integer harmonics of f0 within Nyquist; we skip them so that the
  // remaining peaks are genuine aliased images rather than ordinary harmonic
  // distortion.
  std::vector<std::size_t> harm_bins;
  for (int n = 1; n * f_in_hz < kSr * 0.5f; ++n) {
    harm_bins.push_back(static_cast<std::size_t>(
        std::lround(static_cast<float>(n) * f_in_hz / bin_width)));
  }
  auto in_harm_band = [&](std::size_t k) {
    for (std::size_t h : harm_bins) {
      if (k + guard >= h && k <= h + guard) return true;
    }
    return false;
  };

  float worst = 0.0f;
  std::size_t worst_bin = 0;
  for (std::size_t k = 1; k < max_bin; ++k) {
    if (k + guard >= fund_bin && k <= fund_bin + guard) continue;
    if (in_harm_band(k)) continue;
    const float m = std::abs(spec[k]);
    if (m > worst) {
      worst = m;
      worst_bin = k;
    }
  }
  r.worst_alias_dbfs = MagToDbfs(worst, kFftN);
  r.worst_alias_hz = static_cast<float>(worst_bin) * bin_width;

  if (!csv_prefix.empty()) {
    WriteSamplesCsv(OutputPath(csv_prefix + "_input.csv"),
                    input.data() + kSettleN, kFftN);
    WriteSamplesCsv(OutputPath(csv_prefix + "_output.csv"),
                    output.data() + kSettleN, kFftN);
    WriteSpectrumCsv(OutputPath(csv_prefix + "_spectrum.csv"),
                     spec.data(), kFftN, kSr);
  }
  return r;
}

bool RunSnapshot(const AlgoConfig& cfg, float f_in_hz,
                 const std::string& csv_prefix) {
  const auto r = MeasureAliasing(cfg, f_in_hz, csv_prefix);
  std::cerr << "  [snapshot] " << csv_prefix
            << "  fund=" << r.fundamental_dbfs << " dBFS"
            << "  worst_alias=" << r.worst_alias_dbfs
            << " dBFS @ " << r.worst_alias_hz << " Hz\n";
  return true;  // snapshot tests never fail; they just record numbers.
}

bool TestAaDecimateLow() {
  AlgoConfig c{"decimate-low",
               corrupter::CorruptBank::kLegacy,
               corrupter::CorruptAlgorithm::kDecimate,
               0.1f, 0.5f};
  return RunSnapshot(c, 1000.0f, "aa_decimate_i010_1k");
}

bool TestAaDecimateMid() {
  AlgoConfig c{"decimate-mid",
               corrupter::CorruptBank::kLegacy,
               corrupter::CorruptAlgorithm::kDecimate,
               0.5f, 0.5f};
  return RunSnapshot(c, 1000.0f, "aa_decimate_i050_1k");
}

bool TestAaDecimateHigh() {
  AlgoConfig c{"decimate-high",
               corrupter::CorruptBank::kLegacy,
               corrupter::CorruptAlgorithm::kDecimate,
               0.9f, 0.5f};
  return RunSnapshot(c, 1000.0f, "aa_decimate_i090_1k");
}

// Bin-snap a target frequency for clean fundamental measurement.
float BinAligned(float target_hz) {
  const float bin_width = kSr / static_cast<float>(kFftN);
  return std::round(target_hz / bin_width) * bin_width;
}

// Destroy soft region (intensity < 0.5 -> tanh saturator with no oversampling
// but smooth nonlinearity). Drive at 7 kHz so 11th & 13th harmonics fold
// back into the audio band; integer harmonics are excluded by MeasureAliasing.
bool TestAaDestroySoft() {
  AlgoConfig c{"destroy-soft",
               corrupter::CorruptBank::kLegacy,
               corrupter::CorruptAlgorithm::kDestroy,
               0.30f, 0.5f};
  const float f0 = BinAligned(7000.0f);
  const auto r = MeasureAliasing(c, f0, "aa_destroy_i030_7k");
  std::cerr << "  destroy soft  fund=" << r.fundamental_dbfs
            << "  worst=" << r.worst_alias_dbfs
            << " @ " << r.worst_alias_hz << "\n";
  // Soft saturator with 2x OS (Phase 2B): alias images sit ~47 dB below
  // fundamental. Gate at 40 dB to leave 7 dB margin before regression.
  const float margin = r.fundamental_dbfs - r.worst_alias_dbfs;
  CORRUPTER_SPEC_REQUIRE(margin > 40.0f,
                         "destroy-soft: alias more than 40 dB below fundamental");
  return true;
}

// Destroy hard region: 2x-oversampled hard clipper. The worst alias lives in
// the half-band's natural fs/4 transition band (the 7th harmonic of 7 kHz at
// the 192 kHz internal rate folds to 47 kHz at output) — see
// iter-2B-pass-2 REPORT for the analysis. Plateau at ~24 dB margin; gate at
// 22 dB.
bool TestAaDestroyHard() {
  AlgoConfig c{"destroy-hard",
               corrupter::CorruptBank::kLegacy,
               corrupter::CorruptAlgorithm::kDestroy,
               0.70f, 0.5f};
  const float f0 = BinAligned(7000.0f);
  const auto r = MeasureAliasing(c, f0, "aa_destroy_i070_7k");
  std::cerr << "  destroy hard  fund=" << r.fundamental_dbfs
            << "  worst=" << r.worst_alias_dbfs
            << " @ " << r.worst_alias_hz << "\n";
  const float margin = r.fundamental_dbfs - r.worst_alias_dbfs;
  CORRUPTER_SPEC_INFO("destroy-hard alias margin: " << margin << " dB");
  CORRUPTER_SPEC_REQUIRE(margin > 22.0f,
                         "destroy-hard: alias more than 22 dB below fundamental");
  return true;
}

// DJ filter is already 2x oversampled. Drive 80 Hz at full LP (intensity ~ 0).
bool TestAaDjFilterLpResonance() {
  AlgoConfig c{"djfilter-lp",
               corrupter::CorruptBank::kExpanded,
               corrupter::CorruptAlgorithm::kDjFilter,
               0.05f, 0.5f};
  const auto r = MeasureAliasing(c, 80.0f, "aa_djfilter_i005_80hz");
  std::cerr << "  dj-filter LP  fund=" << r.fundamental_dbfs
            << "  worst=" << r.worst_alias_dbfs
            << " @ " << r.worst_alias_hz << "\n";
  // Look for alias images above 10 kHz only; this captures any cubic-saturation
  // products that escape oversampling.
  std::vector<float> input(kSettleN + kFftN, 0.0f);
  GenSine(input.data(), input.size(), 80.0f, kSr, c.input_amp);
  corrupter::internal::CorruptChannelState state{};
  corrupter::internal::XorShift32 rng;
  rng.Seed(0xBEEF);
  std::vector<float> output(input.size(), 0.0f);
  for (std::size_t i = 0; i < input.size(); ++i) {
    output[i] = corrupter::internal::ProcessCorruptSample(
        input[i], c.intensity, c.bank, c.algo, &state, &rng, kSr);
  }
  std::vector<std::complex<float>> spec(kFftN);
  Fft(output.data() + kSettleN, kFftN, spec.data());
  const float peak_high = PeakInBand(spec.data(), kFftN, kSr,
                                     10000.0f, kSr * 0.5f);
  const float peak_high_dbfs = MagToDbfs(peak_high, kFftN);
  CORRUPTER_SPEC_INFO("dj-filter LP alias > 10 kHz: " << peak_high_dbfs << " dBFS");
  // Snapshot for now; tighten gate after baseline.
  return true;
}

bool TestAaVinylSnapshot() {
  AlgoConfig c{"vinyl",
               corrupter::CorruptBank::kExpanded,
               corrupter::CorruptAlgorithm::kVinylSim,
               0.5f, 0.3f};
  return RunSnapshot(c, 1000.0f, "aa_vinyl_i050_1k");
}

}  // namespace

void RegisterAliasingTests(std::vector<TestCase>& out) {
  out.push_back({"T-AA-Decimate-Low", TestAaDecimateLow});
  out.push_back({"T-AA-Decimate-Mid", TestAaDecimateMid});
  out.push_back({"T-AA-Decimate-High", TestAaDecimateHigh});
  out.push_back({"T-AA-Destroy-Soft", TestAaDestroySoft});
  out.push_back({"T-AA-Destroy-Hard", TestAaDestroyHard});
  out.push_back({"T-AA-DjFilter-Lp", TestAaDjFilterLpResonance});
  out.push_back({"T-AA-Vinyl-Snapshot", TestAaVinylSnapshot});
}

}  // namespace corrupter_spec
