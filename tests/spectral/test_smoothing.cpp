#include <algorithm>
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

#include "engine_driver.h"
#include "spectral_helpers.h"
#include "test_framework.h"

namespace corrupter_spec {
namespace {

constexpr float kSr = 96000.0f;

// Sideband energy ratio: ratio of power in a narrow band around target_hz
// (excluding a guard around the carrier) to the carrier power. Useful as a
// "zipper noise" proxy when the parameter is modulated periodically.
float SidebandRatio(const float* x, std::size_t fft_len, float carrier_hz,
                    float band_hz) {
  std::vector<std::complex<float>> spec(fft_len);
  Fft(x, fft_len, spec.data());
  const float carrier = MagAtHz(spec.data(), fft_len, kSr, carrier_hz);
  const float carrier_power = carrier * carrier;
  const float lo = std::max(20.0f, carrier_hz - band_hz);
  const float hi = std::min(kSr * 0.5f - 1.0f, carrier_hz + band_hz);
  // Sum sideband power excluding +/-100 Hz guard around carrier.
  const float bin_width = kSr / static_cast<float>(fft_len);
  const std::size_t fund_bin =
      static_cast<std::size_t>(std::lround(carrier_hz / bin_width));
  const std::size_t guard = 8;
  const std::size_t lo_bin = std::max<std::size_t>(
      1, static_cast<std::size_t>(std::lround(lo / bin_width)));
  const std::size_t hi_bin = std::min<std::size_t>(
      fft_len / 2 - 1, static_cast<std::size_t>(std::lround(hi / bin_width)));
  float sb_power = 0.0f;
  for (std::size_t k = lo_bin; k <= hi_bin; ++k) {
    if (k + guard >= fund_bin && k <= fund_bin + guard) continue;
    const float m = std::abs(spec[k]);
    sb_power += m * m;
  }
  if (carrier_power < 1e-30f) return 0.0f;
  return sb_power / carrier_power;
}

// T-SM-Corrupt-Step: modulate corrupt_01 with a slow square wave through the
// full engine. The carrier is a steady tone; an un-smoothed corrupt CV will
// imprint sideband energy near the modulation rate (zipper noise).
//
// Phase 1: snapshot only (records baseline). Phase 2A target: this should
// drop substantially after the corrupt-CV one-pole smoother lands.
bool TestSmCorruptStep() {
  DriverConfig dc;
  dc.cfg.sample_rate_hz = kSr;
  dc.cfg.max_supported_sample_rate_hz = kSr;
  dc.cfg.max_block_frames = 64;
  dc.cfg.max_buffer_seconds = 1.0f;
  dc.state.bend_enabled = false;
  dc.state.break_enabled = false;
  dc.state.freeze_enabled = false;
  dc.state.corrupt_bank = corrupter::CorruptBank::kLegacy;
  dc.state.corrupt_algorithm = corrupter::CorruptAlgorithm::kDestroy;
  dc.knobs.mix_01 = 1.0f;
  dc.knobs.corrupt_01 = 0.30f;
  dc.knobs.time_01 = 0.05f;

  EngineDriver driver;
  CORRUPTER_SPEC_REQUIRE(driver.Init(dc), "engine init");

  constexpr std::uint32_t kSettle = 4096;
  constexpr std::uint32_t kFftN = 32768;
  constexpr std::uint32_t kTotal = kSettle + kFftN;
  constexpr float kCarrierHz = 1500.0f;

  std::vector<float> input(kTotal);
  GenSine(input.data(), input.size(), kCarrierHz, kSr, 0.5f);
  std::vector<float> output(kTotal, 0.0f);

  // Settle.
  driver.ProcessMonoLeft(input.data(), output.data(), kSettle);

  // Square-wave modulate corrupt every 480 samples (~ 100 Hz mod rate).
  const std::uint32_t kModBlock = 480;
  std::uint32_t i = 0;
  bool high = true;
  while (i < kFftN) {
    const std::uint32_t n = std::min(kModBlock, kFftN - i);
    corrupter::KnobState k = dc.knobs;
    k.corrupt_01 = high ? 0.55f : 0.30f;
    driver.SetKnobs(k);
    driver.ProcessMonoLeft(input.data() + kSettle + i,
                           output.data() + kSettle + i, n);
    high = !high;
    i += n;
  }

  WriteSamplesCsv(OutputPath("sm_corrupt_step.csv"),
                  output.data(), output.size());

  const float ratio = SidebandRatio(output.data() + kSettle, kFftN,
                                    kCarrierHz, 8000.0f);
  const float ratio_db = 10.0f * std::log10(ratio + 1e-30f);
  std::cerr << "  sm-corrupt-step sideband/carrier: " << ratio_db << " dB\n";
  // Snapshot — Phase 2A target.
  CORRUPTER_SPEC_INFO("sm-corrupt-step sideband/carrier: " << ratio_db << " dB");
  return true;
}

// T-SM-TapeDrive-Step: tape drive coefficient is updated each clock tick,
// no smoothing. Step bend (which drives tape parameters in macro mode) and
// look for sideband energy.
bool TestSmTapeDriveStep() {
  DriverConfig dc;
  dc.cfg.sample_rate_hz = kSr;
  dc.cfg.max_supported_sample_rate_hz = kSr;
  dc.cfg.max_block_frames = 64;
  dc.cfg.max_buffer_seconds = 1.0f;
  dc.state.bend_enabled = true;
  dc.state.break_enabled = false;
  dc.state.freeze_enabled = false;
  dc.state.macro_mode = true;
  dc.knobs.mix_01 = 1.0f;
  dc.knobs.bend_01 = 0.30f;
  dc.knobs.time_01 = 0.10f;

  EngineDriver driver;
  CORRUPTER_SPEC_REQUIRE(driver.Init(dc), "engine init");

  constexpr std::uint32_t kSettle = 8192;
  constexpr std::uint32_t kFftN = 32768;
  constexpr std::uint32_t kTotal = kSettle + kFftN;
  constexpr float kCarrierHz = 1500.0f;

  std::vector<float> input(kTotal);
  GenSine(input.data(), input.size(), kCarrierHz, kSr, 0.5f);
  std::vector<float> output(kTotal, 0.0f);

  driver.ProcessMonoLeft(input.data(), output.data(), kSettle);

  const std::uint32_t kModBlock = 1920;  // ~25 Hz mod rate
  std::uint32_t i = 0;
  bool high = true;
  while (i < kFftN) {
    const std::uint32_t n = std::min(kModBlock, kFftN - i);
    corrupter::KnobState k = dc.knobs;
    k.bend_01 = high ? 0.95f : 0.05f;  // ±octave excursion in micro mode
    driver.SetKnobs(k);
    driver.ProcessMonoLeft(input.data() + kSettle + i,
                           output.data() + kSettle + i, n);
    high = !high;
    i += n;
  }

  WriteSamplesCsv(OutputPath("sm_tape_drive_step.csv"),
                  output.data(), output.size());

  const float ratio = SidebandRatio(output.data() + kSettle, kFftN,
                                    kCarrierHz, 4000.0f);
  const float ratio_db = 10.0f * std::log10(ratio + 1e-30f);
  std::cerr << "  sm-tape-drive-step sideband/carrier: " << ratio_db << " dB\n";
  CORRUPTER_SPEC_INFO("sm-tape-drive-step sideband/carrier: "
                      << ratio_db << " dB");
  return true;
}

// T-SM-Pitch-Step: time knob jumps mid-stream changing playback rate.
// Pitch quantize has its own smoothing already, but a raw time-knob step
// should still be reasonably clean. Snapshot baseline.
bool TestSmPitchStep() {
  DriverConfig dc;
  dc.cfg.sample_rate_hz = kSr;
  dc.cfg.max_supported_sample_rate_hz = kSr;
  dc.cfg.max_block_frames = 64;
  dc.cfg.max_buffer_seconds = 2.0f;
  dc.state.bend_enabled = false;
  dc.state.break_enabled = false;
  dc.state.freeze_enabled = true;  // freeze to isolate pitch behavior
  dc.state.freeze_latching = true;
  dc.knobs.mix_01 = 1.0f;
  dc.knobs.time_01 = 0.50f;

  EngineDriver driver;
  CORRUPTER_SPEC_REQUIRE(driver.Init(dc), "engine init");

  constexpr std::uint32_t kPre = 32000;
  constexpr std::uint32_t kPost = 32000;
  constexpr std::uint32_t kTotal = kPre + kPost;
  std::vector<float> input(kTotal);
  GenSine(input.data(), input.size(), 800.0f, kSr, 0.5f);
  std::vector<float> output(kTotal, 0.0f);

  driver.ProcessMonoLeft(input.data(), output.data(), kPre);
  corrupter::KnobState k = dc.knobs;
  k.time_01 = 0.20f;
  driver.SetKnobs(k);
  driver.ProcessMonoLeft(input.data() + kPre, output.data() + kPre, kPost);

  WriteSamplesCsv(OutputPath("sm_pitch_step.csv"),
                  output.data(), output.size());

  // Click energy in window around the step.
  const std::uint32_t lo = kPre - 256;
  const std::uint32_t hi = kPre + 256;
  float worst = 0.0f;
  for (std::uint32_t i = lo + 1; i < hi; ++i) {
    const float d = std::fabs(output[i] - output[i - 1]);
    if (d > worst) worst = d;
  }
  std::cerr << "  sm-pitch-step worst delta: " << worst << "\n";
  CORRUPTER_SPEC_INFO("sm-pitch-step worst delta: " << worst);
  return true;
}

// T-SM-Destroy-Crossover: at the soft/hard branch boundary in Destroy
// (intensity = 0.5), the derivative is discontinuous. Sweep intensity
// linearly across [0.40, 0.60] and compare the local mean-squared
// difference between the soft and hard paths' transfer-curve outputs at
// the crossing. Reported as a max DC discontinuity proxy.
bool TestSmDestroyCrossover() {
  corrupter::internal::CorruptChannelState state{};
  corrupter::internal::XorShift32 rng;
  rng.Seed(0xCAFE);

  constexpr std::uint32_t kFrames = 96000;  // 1 second
  std::vector<float> input(kFrames);
  GenSine(input.data(), input.size(), 1000.0f, kSr, 0.05f);
  std::vector<float> output(kFrames, 0.0f);

  // Linear ramp 0.40 -> 0.60 across the buffer. The boundary at 0.5 falls
  // exactly halfway through.
  for (std::uint32_t i = 0; i < kFrames; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(kFrames - 1);
    const float intensity = 0.40f + 0.20f * t;
    output[i] = corrupter::internal::ProcessCorruptSample(
        input[i], intensity, corrupter::CorruptBank::kLegacy,
        corrupter::CorruptAlgorithm::kDestroy, &state, &rng, kSr);
  }
  WriteSamplesCsv(OutputPath("sm_destroy_crossover.csv"),
                  output.data(), output.size());

  // Compare averaged RMS for two narrow windows of intensity straddling
  // 0.5. A derivative kink shows up as a step change in average level
  // across the boundary; smoothing reduces it.
  const std::uint32_t pre_mid = kFrames / 2 - 480;   // i ≈ 0.499
  const std::uint32_t post_mid = kFrames / 2 + 480;  // i ≈ 0.501
  const std::uint32_t span = 480;  // ~5 sine periods

  auto rms = [&](std::uint32_t centre) -> float {
    float sum_sq = 0.0f;
    for (std::uint32_t i = centre - span; i < centre + span; ++i) {
      sum_sq += output[i] * output[i];
    }
    return std::sqrt(sum_sq / static_cast<float>(2 * span));
  };
  const float r_pre  = rms(pre_mid);
  const float r_post = rms(post_mid);
  const float jump = std::fabs(r_post - r_pre);
  std::cerr << "  sm-destroy-crossover RMS jump across i=0.5: " << jump
            << " (pre=" << r_pre << " post=" << r_post << ")\n";
  CORRUPTER_SPEC_INFO("sm-destroy-crossover RMS jump: " << jump);
  // Phase 2D smoothstep crossfade brought this from 0.0067 to 0.0022.
  // Gate at 0.005 to catch regressions while leaving 2x headroom.
  CORRUPTER_SPEC_REQUIRE(jump < 0.005f,
                         "destroy-crossover: RMS jump across i=0.5 < 0.005");
  return true;
}

}  // namespace

void RegisterSmoothingTests(std::vector<TestCase>& out) {
  out.push_back({"T-SM-Corrupt-Step", TestSmCorruptStep});
  out.push_back({"T-SM-TapeDrive-Step", TestSmTapeDriveStep});
  out.push_back({"T-SM-Pitch-Step", TestSmPitchStep});
  out.push_back({"T-SM-Destroy-Crossover", TestSmDestroyCrossover});
}

}  // namespace corrupter_spec
