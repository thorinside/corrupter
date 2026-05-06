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

// Per-sample wrapper to drive ProcessCorruptSample.
struct CorruptStream {
  corrupter::CorruptBank bank;
  corrupter::CorruptAlgorithm algo;
  float intensity;
  corrupter::internal::CorruptChannelState state{};
  corrupter::internal::XorShift32 rng;

  CorruptStream(corrupter::CorruptBank b, corrupter::CorruptAlgorithm a,
                float i, std::uint32_t seed)
      : bank(b), algo(a), intensity(i) {
    rng.Seed(seed ? seed : 1);
  }

  float operator()(float x) {
    return corrupter::internal::ProcessCorruptSample(
        x, intensity, bank, algo, &state, &rng, kSr);
  }
};

// --- Decimate flatness at intensity=0 ---
//
// At intensity=0: hold = 1 (so sample-and-hold does not skip), bits = 24
// (so quantization is at the 24-bit floor). Should look like unity passthrough.
bool TestFraDecimateIdle() {
  CorruptStream s(corrupter::CorruptBank::kLegacy,
                  corrupter::CorruptAlgorithm::kDecimate, 0.0f, 0xC0FFEE);
  auto grid = StandardFraGrid();
  auto pts = SteppedSineFra(std::ref(s), grid, kSr, 0.05f, 0.10f, 0.25f);
  WriteFraCsv(OutputPath("fra_decimate_idle.csv"), pts);

  // Find max deviation from 0 dB across the grid (excluding super-high freqs
  // where 24-bit quantization noise floor is non-trivial).
  float max_dev = 0.0f;
  for (const auto& pt : pts) {
    if (pt.hz < 20.0f || pt.hz > 18000.0f) continue;
    const float dev = std::fabs(pt.db);
    if (dev > max_dev) max_dev = dev;
  }
  std::cerr << "  decimate-idle  max deviation: " << max_dev << " dB\n";
  CORRUPTER_SPEC_REQUIRE(max_dev < 0.4f,
                         "decimate at intensity=0 is flat to within 0.4 dB");
  return true;
}

// --- DJ filter centered (intensity=0.5, blend=0) ---
bool TestFraDjFilterCenter() {
  CorruptStream s(corrupter::CorruptBank::kExpanded,
                  corrupter::CorruptAlgorithm::kDjFilter, 0.5f, 0xBEEF);
  auto grid = StandardFraGrid();
  auto pts = SteppedSineFra(std::ref(s), grid, kSr, 0.10f, 0.20f, 0.25f);
  WriteFraCsv(OutputPath("fra_djfilter_center.csv"), pts);

  // At intensity=0.5, abs_c = 0 so blend=0 and the filter output goes 100%
  // dry through the wet path. There is also a 0.92 * 0.5 * (x0 + x1) post-gain
  // (lines 142-146 of corrupt_engine.cpp). With x0 = 0.5*(prev+x), x1 = x and
  // sum/2 + a 0.92 factor, we get 0.92 * 0.5 * (0.5*prev + 1.5*x) = something
  // close to 0.92*x for steady state. So we expect ~ -0.7 dB but FLAT.
  //
  // Check flatness band (max - min) over 100 Hz - 10 kHz.
  float lo = 200.0f, hi = -200.0f;
  for (const auto& pt : pts) {
    if (pt.hz < 100.0f || pt.hz > 10000.0f) continue;
    if (pt.db < lo) lo = pt.db;
    if (pt.db > hi) hi = pt.db;
  }
  const float flatness = hi - lo;
  std::cerr << "  djfilter-center  flatness: " << flatness
            << " dB (band: " << lo << " .. " << hi << ")\n";
  CORRUPTER_SPEC_REQUIRE(flatness < 0.5f,
                         "djfilter at intensity=0.5 is flat to within 0.5 dB");
  return true;
}

// --- DJ filter full LP (intensity=0) ---
bool TestFraDjFilterLpMax() {
  CorruptStream s(corrupter::CorruptBank::kExpanded,
                  corrupter::CorruptAlgorithm::kDjFilter, 0.0f, 0xBEEF);
  auto grid = StandardFraGrid();
  auto pts = SteppedSineFra(std::ref(s), grid, kSr, 0.30f, 0.30f, 0.25f);
  WriteFraCsv(OutputPath("fra_djfilter_lpmax.csv"), pts);

  // At intensity=0, target_pos = -1, abs_c = 1, blend = 1, full LP at 70 Hz.
  // BUT: the dj_pos_smooth has 15 ms slew, so settle for ~80 ms first via
  // settle_seconds=0.30f above.
  //
  // Find the -3 dB corner.
  float corner_hz = -1.0f;
  // Reference: average of low-band magnitude (20-50 Hz).
  float ref_db = 0.0f;
  int ref_n = 0;
  for (const auto& pt : pts) {
    if (pt.hz >= 20.0f && pt.hz <= 50.0f) {
      ref_db += pt.db;
      ++ref_n;
    }
  }
  if (ref_n > 0) ref_db /= static_cast<float>(ref_n);

  for (const auto& pt : pts) {
    if (pt.hz < 30.0f) continue;
    if (pt.db < ref_db - 3.0f) {
      corner_hz = pt.hz;
      break;
    }
  }
  std::cerr << "  djfilter-lpmax  ref_db=" << ref_db
            << "  -3dB corner: " << corner_hz << " Hz\n";
  CORRUPTER_SPEC_REQUIRE(corner_hz > 30.0f && corner_hz < 200.0f,
                         "LP corner is in 30-200 Hz range");
  return true;
}

// --- DJ filter full HP (intensity=1) ---
bool TestFraDjFilterHpMax() {
  CorruptStream s(corrupter::CorruptBank::kExpanded,
                  corrupter::CorruptAlgorithm::kDjFilter, 1.0f, 0xBEEF);
  auto grid = StandardFraGrid();
  auto pts = SteppedSineFra(std::ref(s), grid, kSr, 0.30f, 0.30f, 0.25f);
  WriteFraCsv(OutputPath("fra_djfilter_hpmax.csv"), pts);

  // High-band reference (10-15 kHz)
  float ref_db = 0.0f;
  int ref_n = 0;
  for (const auto& pt : pts) {
    if (pt.hz >= 10000.0f && pt.hz <= 15000.0f) {
      ref_db += pt.db;
      ++ref_n;
    }
  }
  if (ref_n > 0) ref_db /= static_cast<float>(ref_n);

  // Find -3 dB corner walking down from high to low.
  float corner_hz = -1.0f;
  for (auto it = pts.rbegin(); it != pts.rend(); ++it) {
    if (it->hz > 18000.0f) continue;
    if (it->db < ref_db - 3.0f) {
      corner_hz = it->hz;
      break;
    }
  }
  std::cerr << "  djfilter-hpmax  ref_db=" << ref_db
            << "  -3dB corner: " << corner_hz << " Hz\n";
  CORRUPTER_SPEC_REQUIRE(corner_hz > 1000.0f && corner_hz < 20000.0f,
                         "HP corner is in 1-20 kHz range");
  return true;
}

// --- Vinyl tone shaping (intensity=0.5) ---
bool TestFraVinylTone() {
  CorruptStream s(corrupter::CorruptBank::kExpanded,
                  corrupter::CorruptAlgorithm::kVinylSim, 0.5f, 0xBEEF);
  auto grid = StandardFraGrid();
  // Use longer settle/measure to average out the noise component.
  auto pts = SteppedSineFra(std::ref(s), grid, kSr, 0.10f, 0.50f, 0.25f);
  WriteFraCsv(OutputPath("fra_vinyl_tone.csv"), pts);

  // Snapshot only — vinyl response includes a non-zero noise floor from
  // rumble/hiss that distorts the simple RMS-based FRA. We capture and report.
  float lo = 200.0f, hi = -200.0f;
  for (const auto& pt : pts) {
    if (pt.hz < 200.0f || pt.hz > 5000.0f) continue;
    if (pt.db < lo) lo = pt.db;
    if (pt.db > hi) hi = pt.db;
  }
  std::cerr << "  vinyl-tone  midband range: " << lo << " .. " << hi << " dB\n";
  return true;  // snapshot
}

}  // namespace

void RegisterFraTests(std::vector<TestCase>& out) {
  out.push_back({"T-FRA-Decimate-Idle", TestFraDecimateIdle});
  out.push_back({"T-FRA-DjFilter-Center", TestFraDjFilterCenter});
  out.push_back({"T-FRA-DjFilter-LpMax", TestFraDjFilterLpMax});
  out.push_back({"T-FRA-DjFilter-HpMax", TestFraDjFilterHpMax});
  out.push_back({"T-FRA-Vinyl-Tone", TestFraVinylTone});
}

}  // namespace corrupter_spec
