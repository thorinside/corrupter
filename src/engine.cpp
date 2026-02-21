#include "corrupter_dsp/engine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>

namespace corrupter {
namespace {

static inline float Clamp01(float x) {
  return std::max(0.0f, std::min(1.0f, x));
}

static inline bool GateHigh(float v) {
  return v >= 0.4f;
}

static inline bool RisingEdge(bool current, bool previous) {
  return current && !previous;
}

static inline float CvOrZero(const float* ptr, uint32_t i) {
  return ptr ? ptr[i] : 0.0f;
}

}  // namespace

struct Engine::Impl {
  struct GateState {
    bool bend = false;
    bool brk = false;
    bool corrupt = false;
    bool freeze = false;
    bool clock = false;
  };

  EngineConfig cfg;
  KnobState knobs;
  PersistentState state;
  bool clock_mode_internal = true;
  uint32_t buffer_frames = 0;
  uint32_t write_idx = 0;
  uint64_t processed_frames = 0;
  uint32_t observed_clock_edges = 0;
  float* buffer_l = nullptr;
  float* buffer_r = nullptr;
  GateState prev_gate;
};

Engine::Engine() noexcept : impl_(nullptr) {}

Engine::~Engine() noexcept {
  impl_ = nullptr;
}

size_t Engine::required_dram_bytes(const EngineConfig& cfg) noexcept {
  if (cfg.sample_rate_hz <= 0.0f || cfg.max_buffer_seconds <= 0.0f) {
    return 0;
  }
  const double frames_f64 =
      static_cast<double>(cfg.sample_rate_hz) * static_cast<double>(cfg.max_buffer_seconds);
  const size_t frames = static_cast<size_t>(std::ceil(frames_f64));
  const size_t audio_bytes = frames * 2u * sizeof(float);
  const size_t state_bytes = sizeof(Impl);
  const size_t alignment = 64u;
  return state_bytes + alignment + audio_bytes + alignment;
}

bool Engine::initialise(void* dram, size_t dram_bytes, const EngineConfig& cfg) noexcept {
  const size_t required = required_dram_bytes(cfg);
  if (!dram || required == 0 || dram_bytes < required) {
    impl_ = nullptr;
    return false;
  }

  uintptr_t base = reinterpret_cast<uintptr_t>(dram);
  uintptr_t aligned_impl = (base + 63u) & ~uintptr_t(63u);
  Impl* raw_impl = reinterpret_cast<Impl*>(aligned_impl);
  new (raw_impl) Impl();
  impl_ = raw_impl;
  impl_->cfg = cfg;

  impl_->buffer_frames = static_cast<uint32_t>(
      std::max(1.0, std::ceil(static_cast<double>(cfg.sample_rate_hz) *
                              static_cast<double>(cfg.max_buffer_seconds))));

  uint8_t* audio_start = reinterpret_cast<uint8_t*>(aligned_impl + sizeof(Impl));
  uintptr_t aligned_audio = (reinterpret_cast<uintptr_t>(audio_start) + 63u) & ~uintptr_t(63u);
  float* audio = reinterpret_cast<float*>(aligned_audio);

  impl_->buffer_l = audio;
  impl_->buffer_r = audio + impl_->buffer_frames;

  std::memset(impl_->buffer_l, 0, impl_->buffer_frames * sizeof(float));
  std::memset(impl_->buffer_r, 0, impl_->buffer_frames * sizeof(float));
  reset();
  return true;
}

void Engine::reset() noexcept {
  if (!impl_) {
    return;
  }
  impl_->write_idx = 0;
  impl_->processed_frames = 0;
  impl_->observed_clock_edges = 0;
  impl_->prev_gate = {};
  if (impl_->buffer_l && impl_->buffer_r && impl_->buffer_frames > 0) {
    std::memset(impl_->buffer_l, 0, impl_->buffer_frames * sizeof(float));
    std::memset(impl_->buffer_r, 0, impl_->buffer_frames * sizeof(float));
  }
}

void Engine::set_knobs(const KnobState& knobs) noexcept {
  if (!impl_) {
    return;
  }
  impl_->knobs = knobs;
  impl_->knobs.time_01 = Clamp01(impl_->knobs.time_01);
  impl_->knobs.repeats_01 = Clamp01(impl_->knobs.repeats_01);
  impl_->knobs.mix_01 = Clamp01(impl_->knobs.mix_01);
  impl_->knobs.bend_01 = Clamp01(impl_->knobs.bend_01);
  impl_->knobs.break_01 = Clamp01(impl_->knobs.break_01);
  impl_->knobs.corrupt_01 = Clamp01(impl_->knobs.corrupt_01);
  impl_->knobs.bend_cv_attn_01 = Clamp01(impl_->knobs.bend_cv_attn_01);
  impl_->knobs.break_cv_attn_01 = Clamp01(impl_->knobs.break_cv_attn_01);
  impl_->knobs.corrupt_cv_attn_01 = Clamp01(impl_->knobs.corrupt_cv_attn_01);
}

void Engine::set_persistent_state(const PersistentState& state) noexcept {
  if (!impl_) {
    return;
  }
  impl_->state = state;
  impl_->state.glitch_window_01 = Clamp01(impl_->state.glitch_window_01);
}

void Engine::set_clock_mode_internal(bool internal) noexcept {
  if (!impl_) {
    return;
  }
  impl_->clock_mode_internal = internal;
}

void Engine::process(const AudioBlock& audio, const CvInputs& cv, const GateInputs& gates) noexcept {
  if (!impl_ || !audio.out_l || !audio.out_r || audio.frames == 0) {
    return;
  }

  for (uint32_t i = 0; i < audio.frames; ++i) {
    const float in_l = audio.in_l ? audio.in_l[i] : 0.0f;
    const float in_r = audio.in_r ? audio.in_r[i] : 0.0f;

    const bool bend_gate = GateHigh(CvOrZero(gates.bend_gate_v, i));
    const bool break_gate = GateHigh(CvOrZero(gates.break_gate_v, i));
    const bool corrupt_gate = GateHigh(CvOrZero(gates.corrupt_gate_v, i));
    const bool freeze_gate = GateHigh(CvOrZero(gates.freeze_gate_v, i));
    const bool clock_gate = GateHigh(CvOrZero(gates.clock_gate_v, i));

    const bool bend_rise = RisingEdge(bend_gate, impl_->prev_gate.bend);
    const bool break_rise = RisingEdge(break_gate, impl_->prev_gate.brk);
    const bool corrupt_rise = RisingEdge(corrupt_gate, impl_->prev_gate.corrupt);
    const bool freeze_rise = RisingEdge(freeze_gate, impl_->prev_gate.freeze);
    const bool clock_rise = RisingEdge(clock_gate, impl_->prev_gate.clock);

    impl_->prev_gate.bend = bend_gate;
    impl_->prev_gate.brk = break_gate;
    impl_->prev_gate.corrupt = corrupt_gate;
    impl_->prev_gate.freeze = freeze_gate;
    impl_->prev_gate.clock = clock_gate;

    if (!impl_->state.gate_latching) {
      impl_->state.bend_enabled = bend_gate;
      impl_->state.break_enabled = break_gate;
      if (!impl_->state.corrupt_gate_is_reset) {
        // Corrupt enable will be momentary in wrapper state if desired.
      }
    } else {
      if (bend_rise) {
        impl_->state.bend_enabled = !impl_->state.bend_enabled;
      }
      if (break_rise) {
        impl_->state.break_enabled = !impl_->state.break_enabled;
      }
      if (corrupt_rise && !impl_->state.corrupt_gate_is_reset) {
        // Placeholder for corrupt gate toggle path.
      }
    }

    if (impl_->state.freeze_latching) {
      if (freeze_rise) {
        impl_->state.freeze_enabled = !impl_->state.freeze_enabled;
      }
    } else {
      impl_->state.freeze_enabled = freeze_gate;
    }

    if (impl_->state.corrupt_gate_is_reset && corrupt_rise) {
      impl_->write_idx = 0;
    }
    if (clock_rise) {
      ++impl_->observed_clock_edges;
    }

    if (!impl_->state.freeze_enabled) {
      impl_->buffer_l[impl_->write_idx] = in_l;
      impl_->buffer_r[impl_->write_idx] = in_r;
      impl_->write_idx = (impl_->write_idx + 1u) % impl_->buffer_frames;
    }

    const uint32_t read_idx =
        (impl_->write_idx == 0u) ? (impl_->buffer_frames - 1u) : (impl_->write_idx - 1u);
    const float wet_l = impl_->state.freeze_enabled ? impl_->buffer_l[read_idx] : in_l;
    const float wet_r = impl_->state.freeze_enabled ? impl_->buffer_r[read_idx] : in_r;

    const float mix_cv = CvOrZero(cv.mix_v, i) * 0.2f;
    const float mix = Clamp01(impl_->knobs.mix_01 + mix_cv);

    const float dry_gain = std::sqrt(std::max(0.0f, 1.0f - mix));
    const float wet_gain = std::sqrt(mix);
    audio.out_l[i] = in_l * dry_gain + wet_l * wet_gain;
    audio.out_r[i] = in_r * dry_gain + wet_r * wet_gain;

    ++impl_->processed_frames;
  }
}

}  // namespace corrupter

