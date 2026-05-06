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

float MaxClickInWindow(const float* x, std::uint32_t len, std::uint32_t center,
                       std::uint32_t window) {
  const std::uint32_t lo = (center > window) ? center - window : 1;
  const std::uint32_t hi =
      (center + window < len) ? center + window : (len - 1);
  float worst = 0.0f;
  for (std::uint32_t i = lo; i < hi; ++i) {
    const float d = std::fabs(x[i] - x[i - 1]);
    if (d > worst) worst = d;
  }
  return worst;
}

bool TestClkCorruptSweep() {
  // Mix=1, full wet, hold knob configuration, sweep corrupt knob mid-stream
  // and look for click at the parameter step. Uses the full engine to capture
  // any zipper noise from un-smoothed corrupt CV.
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
  dc.knobs.corrupt_01 = 0.0f;
  dc.knobs.time_01 = 0.05f;  // short delay for fast feedthrough

  EngineDriver driver;
  CORRUPTER_SPEC_REQUIRE(driver.Init(dc), "engine init");

  // Pre-roll buffer with sine for at least max_buffer_seconds + headroom.
  const std::uint32_t pre_roll = static_cast<std::uint32_t>(0.5f * kSr);
  const std::uint32_t step_at = pre_roll;
  const std::uint32_t post_roll = static_cast<std::uint32_t>(0.2f * kSr);
  const std::uint32_t total = pre_roll + post_roll;

  std::vector<float> input(total);
  GenSine(input.data(), input.size(), 1000.0f, kSr, 0.5f);
  std::vector<float> output(total, 0.0f);

  // Process pre_roll with corrupt=0.
  driver.ProcessMonoLeft(input.data(), output.data(), pre_roll);
  // Step corrupt to 0.7 in one go.
  corrupter::KnobState k = dc.knobs;
  k.corrupt_01 = 0.7f;
  driver.SetKnobs(k);
  driver.ProcessMonoLeft(input.data() + pre_roll, output.data() + pre_roll,
                         post_roll);

  WriteSamplesCsv(OutputPath("clk_corrupt_step.csv"),
                  output.data(), output.size());

  const float click = MaxClickInWindow(output.data(), total, step_at, 256);
  std::cerr << "  corrupt-step max sample-to-sample delta: " << click << "\n";

  // Snapshot for now; tighten gate after baseline. Phase 2A target.
  CORRUPTER_SPEC_INFO("corrupt-step click delta: " << click);
  return true;
}

bool TestClkBendSweep() {
  // Sweep bend gate on/off with bend enabled.
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
  dc.knobs.bend_01 = 0.5f;
  dc.knobs.time_01 = 0.10f;

  EngineDriver driver;
  CORRUPTER_SPEC_REQUIRE(driver.Init(dc), "engine init");

  const std::uint32_t pre = static_cast<std::uint32_t>(0.5f * kSr);
  const std::uint32_t mid = static_cast<std::uint32_t>(0.3f * kSr);
  const std::uint32_t total = pre + mid;
  std::vector<float> input(total);
  GenSine(input.data(), input.size(), 1000.0f, kSr, 0.5f);
  std::vector<float> output(total, 0.0f);

  driver.ProcessMonoLeft(input.data(), output.data(), pre);
  // Toggle bend off mid-stream by dropping bend_01 — this changes effective
  // tape parameters at the next clock tick.
  corrupter::KnobState k = dc.knobs;
  k.bend_01 = 0.0f;
  driver.SetKnobs(k);
  driver.ProcessMonoLeft(input.data() + pre, output.data() + pre, mid);

  WriteSamplesCsv(OutputPath("clk_bend_step.csv"),
                  output.data(), output.size());

  const float click = MaxClickInWindow(output.data(), total, pre, 512);
  std::cerr << "  bend-step max delta: " << click << "\n";
  return true;  // snapshot
}

// Decimate hold-length jump click. Tests ProcessCorruptSample directly with
// intensity stepping mid-stream. Hold counter resets only when it expires;
// when intensity jumps, the *next* hold becomes the new length. The click can
// happen because the new "held" sample is captured at an unrelated phase.
bool TestClkDecimateHoldStep() {
  corrupter::internal::CorruptChannelState state{};
  corrupter::internal::XorShift32 rng;
  rng.Seed(0xDEC1);

  constexpr std::uint32_t kPre = 4096;
  constexpr std::uint32_t kPost = 4096;
  constexpr std::uint32_t kTotal = kPre + kPost;
  std::vector<float> input(kTotal);
  GenSine(input.data(), input.size(), 1000.0f, kSr, 0.5f);
  std::vector<float> output(kTotal, 0.0f);

  for (std::uint32_t i = 0; i < kPre; ++i) {
    output[i] = corrupter::internal::ProcessCorruptSample(
        input[i], 0.0f, corrupter::CorruptBank::kLegacy,
        corrupter::CorruptAlgorithm::kDecimate, &state, &rng, kSr);
  }
  for (std::uint32_t i = 0; i < kPost; ++i) {
    output[kPre + i] = corrupter::internal::ProcessCorruptSample(
        input[kPre + i], 0.95f, corrupter::CorruptBank::kLegacy,
        corrupter::CorruptAlgorithm::kDecimate, &state, &rng, kSr);
  }
  WriteSamplesCsv(OutputPath("clk_decimate_holdstep.csv"),
                  output.data(), output.size());

  const float click = MaxClickInWindow(output.data(), kTotal, kPre, 256);
  std::cerr << "  decimate-holdstep max delta: " << click << "\n";
  // Decimate aliasing is by-design but the hold-length jump click is a
  // mechanical defect. Snapshot for now; gate after baseline.
  return true;
}

}  // namespace

void RegisterClickTests(std::vector<TestCase>& out) {
  out.push_back({"T-CLK-Corrupt-Sweep", TestClkCorruptSweep});
  out.push_back({"T-CLK-Bend-Sweep", TestClkBendSweep});
  out.push_back({"T-CLK-Decimate-HoldStep", TestClkDecimateHoldStep});
}

}  // namespace corrupter_spec
