#include "corrupter_dsp/engine.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

namespace corrupter {
namespace {

constexpr float kGateThresholdV = 0.4f;
constexpr float kCvRangeV = 5.0f;
constexpr uint32_t kAlignment = 64u;
constexpr float kMinInternalPeriodSeconds = 0.0125f;  // 80 Hz
constexpr float kMaxInternalPeriodSeconds = 16.0f;

static inline float Clamp(float x, float lo, float hi) {
  return std::max(lo, std::min(hi, x));
}

static inline float Clamp01(float x) {
  return Clamp(x, 0.0f, 1.0f);
}

static inline float CvOrZero(const float* ptr, uint32_t i) {
  return ptr ? ptr[i] : 0.0f;
}

static inline bool GateHigh(float volts) {
  return volts >= kGateThresholdV;
}

static inline bool RisingEdge(bool current, bool previous) {
  return current && !previous;
}

static inline float WrapPositive(float x, float n) {
  if (n <= 0.0f) {
    return 0.0f;
  }
  while (x >= n) {
    x -= n;
  }
  while (x < 0.0f) {
    x += n;
  }
  return x;
}

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

struct OnePole {
  float value = 0.0f;
  float coeff = 0.02f;

  void Reset(float v) {
    value = v;
  }

  float Tick(float target) {
    value += coeff * (target - value);
    return value;
  }
};

struct ClockEngine {
  bool internal_mode = true;
  float sample_rate = 96000.0f;
  float time_01 = 0.5f;
  float external_period_samples = 48000.0f;
  float active_period_samples = 48000.0f;
  uint64_t next_tick_sample = 0;
  uint64_t last_external_pulse_sample = 0;
  bool have_external_pulse = false;

  static float ExternalRateMultiplier(float time_01) {
    static constexpr float kTable[9] = {
        1.0f / 16.0f, 1.0f / 8.0f, 1.0f / 4.0f, 1.0f / 2.0f, 1.0f, 2.0f, 3.0f, 4.0f, 8.0f};
    int idx = static_cast<int>(time_01 * 9.0f);
    idx = std::max(0, std::min(8, idx));
    return kTable[idx];
  }

  void Reset(float sr, float time) {
    sample_rate = std::max(1.0f, sr);
    time_01 = Clamp01(time);
    external_period_samples = sample_rate * 0.5f;
    active_period_samples = ComputeInternalPeriodSamples();
    next_tick_sample = 0;
    last_external_pulse_sample = 0;
    have_external_pulse = false;
  }

  void SetInternalMode(bool internal) {
    internal_mode = internal;
  }

  void SetTime(float t) {
    time_01 = Clamp01(t);
    if (internal_mode) {
      active_period_samples = ComputeInternalPeriodSamples();
    } else if (have_external_pulse) {
      const float mult = ExternalRateMultiplier(time_01);
      active_period_samples = std::max(1.0f, external_period_samples / mult);
    }
  }

  float CurrentPeriodSamples() const {
    return std::max(1.0f, active_period_samples);
  }

  bool Step(uint64_t sample_index, bool external_pulse) {
    bool tick = false;

    if (internal_mode) {
      const float period = ComputeInternalPeriodSamples();
      active_period_samples = period;
      if (sample_index >= next_tick_sample) {
        tick = true;
        next_tick_sample = sample_index + static_cast<uint64_t>(std::max(1.0f, period));
      }
      return tick;
    }

    if (external_pulse) {
      if (have_external_pulse) {
        const uint64_t elapsed = sample_index - last_external_pulse_sample;
        external_period_samples = std::max(1.0f, static_cast<float>(elapsed));
      }
      last_external_pulse_sample = sample_index;
      have_external_pulse = true;

      const float mult = ExternalRateMultiplier(time_01);
      active_period_samples = std::max(1.0f, external_period_samples / mult);
      tick = true;
      next_tick_sample = sample_index + static_cast<uint64_t>(active_period_samples);
      return tick;
    }

    // No external pulse right now: keep free-running at last-known tempo.
    if (sample_index >= next_tick_sample) {
      tick = true;
      next_tick_sample = sample_index + static_cast<uint64_t>(CurrentPeriodSamples());
    }

    return tick;
  }

  float ComputeInternalPeriodSamples() const {
    const float log_min = std::log(kMinInternalPeriodSeconds);
    const float log_max = std::log(kMaxInternalPeriodSeconds);
    const float seconds = std::exp(log_max + (log_min - log_max) * Clamp01(time_01));
    return std::max(1.0f, seconds * sample_rate);
  }
};

struct CorruptChannelState {
  uint32_t decimate_hold_remaining = 0;
  float decimate_held = 0.0f;

  uint32_t dropout_remaining = 0;

  float filter_lp = 0.0f;
  float vinyl_lp = 0.0f;
};

struct PlaybackChannelState {
  float phase = 0.0f;
  float rate = 1.0f;
  uint32_t subsection_index = 0;
  float silence_duty = 0.0f;
  bool reverse = false;
  OnePole rate_smoother;
  CorruptChannelState corrupt;

  void Reset() {
    phase = 0.0f;
    rate = 1.0f;
    subsection_index = 0;
    silence_duty = 0.0f;
    reverse = false;
    rate_smoother.Reset(1.0f);
    corrupt = {};
  }
};

struct SegmentState {
  uint32_t start = 0;
  uint32_t length = 1;
  uint32_t repeats = 1;
  uint32_t subsection_length = 1;
};

float Quantize(float x, int bits) {
  bits = std::max(2, std::min(24, bits));
  const int levels = 1 << std::min(bits, 20);  // Avoid huge integer ranges.
  const float step = 2.0f / static_cast<float>(levels);
  return std::round(x / step) * step;
}

float ProcessCorruptSample(float x, float intensity, CorruptBank bank, CorruptAlgorithm algo,
                           CorruptChannelState* state, XorShift32* rng, float sample_rate) {
  intensity = Clamp01(intensity);
  if (!state || !rng) {
    return x;
  }

  switch (bank) {
    case CorruptBank::kLegacy: {
      if (algo == CorruptAlgorithm::kDecimate) {
        const uint32_t hold = 1u + static_cast<uint32_t>(intensity * 127.0f);
        const int bits = 24 - static_cast<int>(intensity * 20.0f);
        if (state->decimate_hold_remaining == 0) {
          state->decimate_held = Quantize(x, bits);
          state->decimate_hold_remaining = hold;
        }
        if (state->decimate_hold_remaining > 0) {
          --state->decimate_hold_remaining;
        }
        return state->decimate_held;
      }
      if (algo == CorruptAlgorithm::kDropout) {
        if (state->dropout_remaining > 0) {
          --state->dropout_remaining;
          return 0.0f;
        }

        const float event_prob = 0.00001f + intensity * 0.01f;
        if (rng->Next01() < event_prob) {
          const float max_dur_s = 0.20f - 0.18f * intensity;
          const float min_dur_s = 0.005f;
          const float dur_s = min_dur_s + rng->Next01() * std::max(0.0f, max_dur_s - min_dur_s);
          state->dropout_remaining =
              static_cast<uint32_t>(std::max(1.0f, dur_s * sample_rate));
          return 0.0f;
        }
        return x;
      }
      // Destroy
      if (intensity < 0.5f) {
        const float drive = 1.0f + intensity * 10.0f;
        return std::tanh(x * drive);
      }
      const float t = (intensity - 0.5f) * 2.0f;
      const float drive = 6.0f + 24.0f * t;
      const float clip = 1.0f - 0.85f * t;
      return Clamp(x * drive, -clip, clip);
    }

    case CorruptBank::kExpanded: {
      if (algo == CorruptAlgorithm::kDjFilter) {
        // 0..0.5 low-pass to bypass, 0.5..1 bypass to high-pass.
        const float centered = (intensity - 0.5f) * 2.0f;
        if (std::fabs(centered) < 0.05f) {
          return x;
        }

        if (centered < 0.0f) {
          const float amt = -centered;
          const float cutoff = 80.0f + (1.0f - amt) * 19920.0f;
          const float a = std::exp(-2.0f * 3.14159265f * cutoff / sample_rate);
          state->filter_lp = (1.0f - a) * x + a * state->filter_lp;
          return state->filter_lp;
        }

        const float amt = centered;
        const float cutoff = 20.0f + amt * 12000.0f;
        const float a = std::exp(-2.0f * 3.14159265f * cutoff / sample_rate);
        state->filter_lp = (1.0f - a) * x + a * state->filter_lp;
        return x - state->filter_lp;
      }

      // Vinyl simulation.
      const float noise = rng->NextSigned() * (0.0008f + 0.02f * intensity);
      float crackle = 0.0f;
      if (rng->Next01() < 0.0005f * intensity) {
        crackle = rng->NextSigned() * (0.1f + 0.4f * intensity);
      }

      const float y = x + noise + crackle;
      const float cutoff = 3500.0f + (1.0f - intensity) * 11500.0f;
      const float a = std::exp(-2.0f * 3.14159265f * cutoff / sample_rate);
      state->vinyl_lp = (1.0f - a) * y + a * state->vinyl_lp;
      return 0.85f * state->vinyl_lp;
    }
  }

  return x;
}

float ReadBufferLinear(const float* buffer, uint32_t buffer_frames, float index) {
  if (!buffer || buffer_frames == 0) {
    return 0.0f;
  }

  const float wrapped = WrapPositive(index, static_cast<float>(buffer_frames));
  const uint32_t i0 = static_cast<uint32_t>(wrapped);
  const uint32_t i1 = (i0 + 1u) % buffer_frames;
  const float frac = wrapped - static_cast<float>(i0);
  return buffer[i0] + (buffer[i1] - buffer[i0]) * frac;
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

  ClockEngine clock;

  uint32_t buffer_frames = 0;
  uint32_t write_idx = 0;
  uint64_t processed_frames = 0;
  float* buffer_l = nullptr;
  float* buffer_r = nullptr;

  SegmentState segment;
  PlaybackChannelState channels[2];
  XorShift32 rng;

  GateState prev_gate;
  bool pending_freeze_toggle = false;
  bool corrupt_enabled = false;

  void ResetPlayback() {
    segment = {};
    segment.start = 0;
    segment.length = std::max(1u, static_cast<uint32_t>(clock.CurrentPeriodSamples()));
    segment.repeats = 1;
    segment.subsection_length = segment.length;
    channels[0].Reset();
    channels[1].Reset();
  }

  float EffectiveUnipolar(float knob, float cv_volts, float atten_01) const {
    const float cv_scaled = (cv_volts / kCvRangeV) * Clamp01(atten_01);
    return Clamp01(knob + cv_scaled);
  }

  uint32_t ComputeRepeats(float repeats_01) const {
    return 1u + static_cast<uint32_t>(Clamp01(repeats_01) * 31.0f);
  }

  float ComputeWindowGain(float phase, uint32_t subsection_len) const {
    if (subsection_len <= 1u) {
      return 1.0f;
    }

    const float win_01 = Clamp01(state.glitch_window_01);
    const float fade_ratio = 0.02f + 0.48f * win_01;
    const float fade = std::max(1.0f, fade_ratio * static_cast<float>(subsection_len));

    const float from_start = phase / fade;
    const float from_end = (static_cast<float>(subsection_len) - phase - 1.0f) / fade;
    return Clamp(std::min(from_start, from_end), 0.0f, 1.0f);
  }

  void UpdateSegmentOnTick(float repeats_01) {
    const uint32_t period = static_cast<uint32_t>(std::max(1.0f, clock.CurrentPeriodSamples()));
    segment.length = std::min(std::max(1u, period), buffer_frames);
    segment.start = (write_idx + buffer_frames - segment.length) % buffer_frames;
    segment.repeats = ComputeRepeats(repeats_01);
    segment.subsection_length = std::max(1u, segment.length / segment.repeats);

    for (auto& ch : channels) {
      ch.phase = 0.0f;
    }
  }

  void ApplyTickEvents(float bend_macro_eff, float bend_micro_octaves, float break_eff,
                       float corrupt_eff) {
    const bool shared = (!state.unique_stereo_mode) || (!state.macro_mode);

    auto apply_one = [&](PlaybackChannelState* ch) {
      if (!ch) {
        return;
      }

      if (state.macro_mode) {
        if (state.bend_enabled) {
          const float rate_oct = (rng.NextSigned() * 3.0f) * bend_macro_eff;
          ch->rate = std::pow(2.0f, rate_oct);
          ch->reverse = (rng.Next01() < (0.15f + 0.75f * bend_macro_eff));
        } else {
          ch->rate = 1.0f;
          ch->reverse = false;
        }

        if (state.break_enabled) {
          ch->subsection_index =
              static_cast<uint32_t>(rng.Next01() * static_cast<float>(segment.repeats));
          if (ch->subsection_index >= segment.repeats) {
            ch->subsection_index = segment.repeats - 1;
          }
          ch->silence_duty = 0.9f * break_eff * rng.Next01();
        } else {
          ch->subsection_index = 0;
          ch->silence_duty = 0.0f;
        }
      } else {
        // Micro mode: direct controls.
        ch->rate = std::pow(2.0f, Clamp(bend_micro_octaves, -3.0f, 3.0f));
        ch->reverse = state.bend_enabled;

        if (!state.break_silence_mode) {
          ch->subsection_index = static_cast<uint32_t>(break_eff * static_cast<float>(segment.repeats));
          if (ch->subsection_index >= segment.repeats) {
            ch->subsection_index = segment.repeats - 1;
          }
          ch->silence_duty = 0.0f;
        } else {
          ch->subsection_index = 0;
          ch->silence_duty = 0.9f * break_eff;
        }
      }

      // Keep corrupt state stable across ticks but adjust dropout tendency by intensity by clearing stale long dropouts.
      if (corrupt_eff < 0.02f) {
        ch->corrupt.dropout_remaining = 0;
      }
    };

    if (shared) {
      apply_one(&channels[0]);
      channels[1].rate = channels[0].rate;
      channels[1].reverse = channels[0].reverse;
      channels[1].subsection_index = channels[0].subsection_index;
      channels[1].silence_duty = channels[0].silence_duty;
    } else {
      apply_one(&channels[0]);
      apply_one(&channels[1]);
    }
  }
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
  return state_bytes + kAlignment + audio_bytes + kAlignment;
}

bool Engine::initialise(void* dram, size_t dram_bytes, const EngineConfig& cfg) noexcept {
  const size_t required = required_dram_bytes(cfg);
  if (!dram || required == 0 || dram_bytes < required) {
    impl_ = nullptr;
    return false;
  }

  const uintptr_t base = reinterpret_cast<uintptr_t>(dram);
  const uintptr_t aligned_impl = (base + (kAlignment - 1u)) & ~uintptr_t(kAlignment - 1u);
  Impl* raw_impl = reinterpret_cast<Impl*>(aligned_impl);
  new (raw_impl) Impl();
  impl_ = raw_impl;

  impl_->cfg = cfg;
  impl_->buffer_frames = static_cast<uint32_t>(
      std::max(1.0, std::ceil(static_cast<double>(cfg.sample_rate_hz) *
                              static_cast<double>(cfg.max_buffer_seconds))));

  uint8_t* audio_start = reinterpret_cast<uint8_t*>(aligned_impl + sizeof(Impl));
  const uintptr_t aligned_audio =
      (reinterpret_cast<uintptr_t>(audio_start) + (kAlignment - 1u)) & ~uintptr_t(kAlignment - 1u);
  float* audio = reinterpret_cast<float*>(aligned_audio);
  impl_->buffer_l = audio;
  impl_->buffer_r = audio + impl_->buffer_frames;

  std::memset(impl_->buffer_l, 0, impl_->buffer_frames * sizeof(float));
  std::memset(impl_->buffer_r, 0, impl_->buffer_frames * sizeof(float));

  impl_->rng.Seed(cfg.random_seed);
  impl_->clock.Reset(cfg.sample_rate_hz, impl_->knobs.time_01);
  impl_->ResetPlayback();
  reset();
  return true;
}

void Engine::reset() noexcept {
  if (!impl_) {
    return;
  }

  impl_->write_idx = 0;
  impl_->processed_frames = 0;
  impl_->prev_gate = {};
  impl_->pending_freeze_toggle = false;
  impl_->corrupt_enabled = false;
  impl_->clock.Reset(impl_->cfg.sample_rate_hz, impl_->knobs.time_01);
  impl_->ResetPlayback();

  if (impl_->buffer_l && impl_->buffer_r && impl_->buffer_frames > 0u) {
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

  impl_->clock.SetTime(impl_->knobs.time_01);
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
  impl_->clock.SetInternalMode(internal);
}

void Engine::process(const AudioBlock& audio, const CvInputs& cv, const GateInputs& gates) noexcept {
  if (!impl_ || !audio.out_l || !audio.out_r || audio.frames == 0u) {
    return;
  }

  for (uint32_t i = 0; i < audio.frames; ++i) {
    const uint64_t sample_index = impl_->processed_frames;

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

    if (!impl_->state.gate_latching) {
      impl_->state.bend_enabled = bend_gate;
      impl_->state.break_enabled = break_gate;
      if (!impl_->state.corrupt_gate_is_reset) {
        impl_->corrupt_enabled = corrupt_gate;
      }
    } else {
      if (bend_rise) {
        impl_->state.bend_enabled = !impl_->state.bend_enabled;
      }
      if (break_rise) {
        impl_->state.break_enabled = !impl_->state.break_enabled;
      }
      if (corrupt_rise && !impl_->state.corrupt_gate_is_reset) {
        impl_->corrupt_enabled = !impl_->corrupt_enabled;
      }
    }

    if (impl_->state.freeze_latching) {
      if (freeze_rise) {
        impl_->pending_freeze_toggle = true;
      }
    } else {
      impl_->state.freeze_enabled = freeze_gate;
    }

    if (impl_->state.corrupt_gate_is_reset && corrupt_rise) {
      impl_->write_idx = 0;
      impl_->channels[0].phase = 0.0f;
      impl_->channels[1].phase = 0.0f;
    }

    const float time_eff =
        impl_->EffectiveUnipolar(impl_->knobs.time_01, CvOrZero(cv.time_v, i), 1.0f);
    impl_->clock.SetTime(time_eff);

    const bool tick = impl_->clock.Step(sample_index, clock_rise);

    const float repeats_eff =
        impl_->EffectiveUnipolar(impl_->knobs.repeats_01, CvOrZero(cv.repeats_v, i), 1.0f);
    const float bend_eff =
        impl_->EffectiveUnipolar(impl_->knobs.bend_01, CvOrZero(cv.bend_v, i),
                                 impl_->knobs.bend_cv_attn_01);
    const float bend_micro_oct = (impl_->knobs.bend_01 * 2.0f - 1.0f) * 3.0f +
                                 CvOrZero(cv.bend_v, i) * impl_->knobs.bend_cv_attn_01;
    const float break_eff = impl_->EffectiveUnipolar(impl_->knobs.break_01, CvOrZero(cv.break_v, i),
                                                     impl_->knobs.break_cv_attn_01);
    const float corrupt_eff =
        impl_->EffectiveUnipolar(impl_->knobs.corrupt_01, CvOrZero(cv.corrupt_v, i),
                                 impl_->knobs.corrupt_cv_attn_01);

    if (tick) {
      if (impl_->pending_freeze_toggle) {
        impl_->state.freeze_enabled = !impl_->state.freeze_enabled;
        impl_->pending_freeze_toggle = false;
      }

      impl_->UpdateSegmentOnTick(repeats_eff);
      impl_->ApplyTickEvents(bend_eff, bend_micro_oct, break_eff, corrupt_eff);
    }

    if (!impl_->state.freeze_enabled) {
      impl_->buffer_l[impl_->write_idx] = in_l;
      impl_->buffer_r[impl_->write_idx] = in_r;
      impl_->write_idx = (impl_->write_idx + 1u) % impl_->buffer_frames;
    }

    auto render_channel = [&](int ch, float input_sample) {
      PlaybackChannelState& pcs = impl_->channels[ch];
      const float* buffer = (ch == 0) ? impl_->buffer_l : impl_->buffer_r;

      const float target_rate = std::max(0.01f, pcs.rate);
      const float smooth_rate = pcs.rate_smoother.Tick(target_rate);

      const uint32_t sub_idx = std::min(pcs.subsection_index, impl_->segment.repeats - 1u);
      const uint32_t subsection_start =
          (impl_->segment.start + sub_idx * impl_->segment.subsection_length) % impl_->buffer_frames;

      float read_pos = pcs.phase;
      if (pcs.reverse) {
        read_pos = static_cast<float>(impl_->segment.subsection_length - 1u) - read_pos;
      }

      const float read_index = static_cast<float>(subsection_start) + read_pos;
      float wet = ReadBufferLinear(buffer, impl_->buffer_frames, read_index);

      const float window = impl_->ComputeWindowGain(pcs.phase, impl_->segment.subsection_length);
      wet *= window;

      if (pcs.silence_duty > 0.0f) {
        const float silence_from =
            (1.0f - Clamp01(pcs.silence_duty)) * static_cast<float>(impl_->segment.subsection_length);
        if (pcs.phase >= silence_from) {
          wet = 0.0f;
        }
      }

      if (impl_->corrupt_enabled || impl_->knobs.corrupt_01 > 0.0001f || CvOrZero(cv.corrupt_v, i) != 0.0f) {
        wet = ProcessCorruptSample(wet, corrupt_eff, impl_->state.corrupt_bank,
                                   impl_->state.corrupt_algorithm, &pcs.corrupt, &impl_->rng,
                                   impl_->cfg.sample_rate_hz);
      }

      pcs.phase += smooth_rate;
      if (pcs.phase >= static_cast<float>(impl_->segment.subsection_length)) {
        pcs.phase = WrapPositive(pcs.phase, static_cast<float>(impl_->segment.subsection_length));
      }

      const float mix_cv = CvOrZero(cv.mix_v, i) * 0.2f;
      float mix = Clamp01(impl_->knobs.mix_01 + mix_cv);
      if (impl_->state.freeze_enabled && mix < 0.001f) {
        // Freeze should be audible immediately if performer was fully dry.
        mix = 1.0f;
      }

      const float dry_gain = std::sqrt(std::max(0.0f, 1.0f - mix));
      const float wet_gain = std::sqrt(mix);
      return input_sample * dry_gain + wet * wet_gain;
    };

    audio.out_l[i] = render_channel(0, in_l);
    audio.out_r[i] = render_channel(1, in_r);

    impl_->prev_gate.bend = bend_gate;
    impl_->prev_gate.brk = break_gate;
    impl_->prev_gate.corrupt = corrupt_gate;
    impl_->prev_gate.freeze = freeze_gate;
    impl_->prev_gate.clock = clock_gate;

    ++impl_->processed_frames;
  }
}

}  // namespace corrupter
