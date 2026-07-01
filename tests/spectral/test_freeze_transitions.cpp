#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "corrupter_dsp/engine.h"

#include "spectral_helpers.h"
#include "test_framework.h"

namespace corrupter_spec {
namespace {

constexpr float kSr = 96000.0f;

float MaxDeltaWindow(const float* x, std::uint32_t len, std::uint32_t center,
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

struct EngineHarness {
  corrupter::EngineConfig cfg;
  corrupter::PersistentState state;
  corrupter::KnobState knobs;
  std::vector<std::uint8_t> dram;
  corrupter::Engine engine;

  bool Init() {
    cfg.sample_rate_hz = kSr;
    cfg.max_supported_sample_rate_hz = kSr;
    cfg.max_block_frames = 256;
    cfg.max_buffer_seconds = 2.0f;
    cfg.random_seed = 0xF00D;
    const std::size_t bytes = corrupter::Engine::required_dram_bytes(cfg);
    dram.assign(bytes, 0u);
    if (!engine.initialise(dram.data(), dram.size(), cfg)) return false;
    engine.set_persistent_state(state);
    engine.set_knobs(knobs);
    engine.set_clock_mode_internal(true);
    engine.set_audio_context(cfg.sample_rate_hz, cfg.max_block_frames);
    return true;
  }

  void Run(const float* in, float* out_l, float* out_r,
           const float* freeze_gate, const float* bend_gate,
           std::uint32_t frames) {
    corrupter::AudioBlock audio;
    audio.in_l = in;
    audio.in_r = in;
    audio.out_l = out_l;
    audio.out_r = out_r;
    audio.frames = frames;
    corrupter::CvInputs cv;
    corrupter::GateInputs gates;
    gates.freeze_gate_v = freeze_gate;
    gates.bend_gate_v = bend_gate;
    engine.process(audio, cv, gates);
  }
};

// T-FRZ-On / T-FRZ-Off: toggle freeze, look for click at edges.
bool TestFrzToggle() {
  constexpr std::uint32_t kFrames = 96000;  // 1 second
  constexpr std::uint32_t kFreezeOn = 32000;
  constexpr std::uint32_t kFreezeOff = 64000;

  EngineHarness h;
  h.state.freeze_enabled = false;
  h.state.freeze_latching = false;
  h.knobs.time_01 = 0.5f;
  h.knobs.mix_01 = 1.0f;
  CORRUPTER_SPEC_REQUIRE(h.Init(), "engine init");

  std::vector<float> input(kFrames);
  GenSine(input.data(), input.size(), 200.0f, kSr, 0.5f);
  std::vector<float> out_l(kFrames, 0.0f);
  std::vector<float> out_r(kFrames, 0.0f);

  std::vector<float> freeze_gate(kFrames, 0.0f);
  for (std::uint32_t i = kFreezeOn; i < kFreezeOff; ++i) {
    freeze_gate[i] = 5.0f;
  }

  h.Run(input.data(), out_l.data(), out_r.data(),
        freeze_gate.data(), nullptr, kFrames);

  WriteSamplesCsv(OutputPath("frz_toggle.csv"),
                  out_l.data(), out_l.size());

  const float on_delta =
      MaxDeltaWindow(out_l.data(), kFrames, kFreezeOn, 256);
  const float off_delta =
      MaxDeltaWindow(out_l.data(), kFrames, kFreezeOff, 256);
  std::cerr << "  frz on max delta: " << on_delta
            << "  off max delta: " << off_delta << "\n";

  // Tightened: on=0, off=0.0065 currently. Gate at 0.01 (4–5× headroom).
  CORRUPTER_SPEC_REQUIRE(on_delta < 0.01f,
                         "freeze-ON delta below 0.01");
  CORRUPTER_SPEC_REQUIRE(off_delta < 0.01f,
                         "freeze-OFF delta below 0.01");
  return true;
}

// T-XFADE-Tick: when freeze is engaged, the buffer-segment crossfade triggers
// at the next clock tick. Verify no obvious click during a long freeze hold.
bool TestXfadeTick() {
  constexpr std::uint32_t kFrames = 192000;  // 2 seconds — multiple ticks
  constexpr std::uint32_t kFreezeOn = 16000;

  EngineHarness h;
  h.state.freeze_enabled = false;
  h.state.freeze_latching = false;
  h.knobs.time_01 = 0.20f;  // short delay -> faster tick rate -> more xfades
  h.knobs.mix_01 = 1.0f;
  CORRUPTER_SPEC_REQUIRE(h.Init(), "engine init");

  std::vector<float> input(kFrames);
  GenSine(input.data(), input.size(), 200.0f, kSr, 0.5f);
  std::vector<float> out_l(kFrames, 0.0f);
  std::vector<float> out_r(kFrames, 0.0f);

  std::vector<float> freeze_gate(kFrames, 0.0f);
  for (std::uint32_t i = kFreezeOn; i < kFrames; ++i) {
    freeze_gate[i] = 5.0f;
  }

  h.Run(input.data(), out_l.data(), out_r.data(),
        freeze_gate.data(), nullptr, kFrames);

  WriteSamplesCsv(OutputPath("frz_xfade_ticks.csv"),
                  out_l.data(), out_l.size());

  // Look for worst sample-to-sample delta in the steady-freeze region.
  // Skip a settle window after the initial freeze-engage transient.
  const std::uint32_t scan_lo = kFreezeOn + 4096;
  const std::uint32_t scan_hi = kFrames - 256;
  float worst = 0.0f;
  for (std::uint32_t i = scan_lo; i < scan_hi; ++i) {
    const float d = std::fabs(out_l[i] - out_l[i - 1]);
    if (d > worst) worst = d;
  }
  std::cerr << "  xfade-tick worst delta in steady freeze: " << worst << "\n";
  CORRUPTER_SPEC_REQUIRE(worst < 0.012f,
                         "xfade-tick steady-freeze delta below 0.012");
  return true;
}

// T-DRP-Edges: dropout cosine ramp. Drive bend with a gate edge to force
// a dropout transition; measure click energy at edges.
bool TestDrpEdges() {
  constexpr std::uint32_t kFrames = 96000;
  constexpr std::uint32_t kEdge1 = 24000;
  constexpr std::uint32_t kEdge2 = 48000;
  constexpr std::uint32_t kEdge3 = 72000;

  EngineHarness h;
  h.state.bend_enabled = true;
  h.state.gate_latching = false;
  h.state.macro_mode = true;
  h.state.corrupt_bank = corrupter::CorruptBank::kLegacy;
  h.state.corrupt_algorithm = corrupter::CorruptAlgorithm::kDropout;
  h.knobs.time_01 = 0.10f;
  h.knobs.mix_01 = 1.0f;
  h.knobs.bend_01 = 0.5f;
  h.knobs.corrupt_01 = 0.6f;
  CORRUPTER_SPEC_REQUIRE(h.Init(), "engine init");

  std::vector<float> input(kFrames);
  GenSine(input.data(), input.size(), 1000.0f, kSr, 0.5f);
  std::vector<float> out_l(kFrames, 0.0f);
  std::vector<float> out_r(kFrames, 0.0f);

  std::vector<float> bend_gate(kFrames, 0.0f);
  for (std::uint32_t i = kEdge1; i < kEdge2; ++i) bend_gate[i] = 5.0f;
  for (std::uint32_t i = kEdge3; i < kFrames; ++i) bend_gate[i] = 5.0f;

  h.Run(input.data(), out_l.data(), out_r.data(),
        nullptr, bend_gate.data(), kFrames);

  WriteSamplesCsv(OutputPath("drp_edges.csv"),
                  out_l.data(), out_l.size());

  const float d1 = MaxDeltaWindow(out_l.data(), kFrames, kEdge1, 512);
  const float d2 = MaxDeltaWindow(out_l.data(), kFrames, kEdge2, 512);
  const float d3 = MaxDeltaWindow(out_l.data(), kFrames, kEdge3, 512);
  const float worst = std::max(std::max(d1, d2), d3);
  std::cerr << "  drp-edges worst delta across 3 edges: " << worst << "\n";
  // Snapshot for now — gate after baseline. Cosine ramp shape inherent.
  CORRUPTER_SPEC_INFO("drp-edges worst delta: " << worst);
  return true;
}

}  // namespace

void RegisterFreezeTests(std::vector<TestCase>& out) {
  out.push_back({"T-FRZ-Toggle", TestFrzToggle});
  out.push_back({"T-XFADE-Tick", TestXfadeTick});
  out.push_back({"T-DRP-Edges", TestDrpEdges});
}

}  // namespace corrupter_spec
