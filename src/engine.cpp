#include "corrupter_dsp/engine.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "internal/clock_engine.h"
#include "internal/corrupt_engine.h"
#include "internal/dsp_common.h"
#include "internal/prng.h"

namespace corrupter {
namespace {

static inline float CvOrZero(const float* ptr, uint32_t i) {
  return ptr ? ptr[i] : 0.0f;
}

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

struct PlaybackChannelState {
  float phase = 0.0f;
  float rate = 1.0f;
  uint32_t subsection_index = 0;
  float silence_duty = 0.0f;
  bool reverse = false;
  OnePole rate_smoother;
  internal::CorruptChannelState corrupt;

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

float ReadBufferLinear(const float* buffer, uint32_t buffer_frames, float index) {
  if (!buffer || buffer_frames == 0) {
    return 0.0f;
  }

  const float wrapped = internal::WrapPositive(index, static_cast<float>(buffer_frames));
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

  internal::ClockEngine clock;

  uint32_t buffer_frames = 0;
  uint32_t write_idx = 0;
  uint64_t processed_frames = 0;
  uint64_t observed_ticks = 0;
  float* buffer_l = nullptr;
  float* buffer_r = nullptr;

  SegmentState segment;
  PlaybackChannelState channels[2];
  internal::XorShift32 rng;

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
    const float cv_scaled =
        (cv_volts / internal::kCvRangeV) * internal::Clamp01(atten_01);
    return internal::Clamp01(knob + cv_scaled);
  }

  uint32_t ComputeRepeats(float repeats_01) const {
    return 1u + static_cast<uint32_t>(internal::Clamp01(repeats_01) * 31.0f);
  }

  float ComputeWindowGain(float phase, uint32_t subsection_len) const {
    if (subsection_len <= 1u) {
      return 1.0f;
    }

    const float win_01 = internal::Clamp01(state.glitch_window_01);
    const float fade_ratio = 0.02f + 0.48f * win_01;
    const float fade = std::max(1.0f, fade_ratio * static_cast<float>(subsection_len));

    const float from_start = phase / fade;
    const float from_end =
        (static_cast<float>(subsection_len) - phase - 1.0f) / fade;
    return internal::Clamp(std::min(from_start, from_end), 0.0f, 1.0f);
  }

  void UpdateSegmentOnTick(float repeats_01) {
    const uint32_t period =
        static_cast<uint32_t>(std::max(1.0f, clock.CurrentPeriodSamples()));
    segment.length = std::min(std::max(1u, period), buffer_frames);
    segment.start = (write_idx + buffer_frames - segment.length) % buffer_frames;
    segment.repeats = ComputeRepeats(repeats_01);
    segment.subsection_length =
        std::max(1u, segment.length / std::max(1u, segment.repeats));

    for (auto& ch : channels) {
      ch.phase = 0.0f;
    }
  }

  void ApplyTickEvents(float bend_macro_eff, float bend_micro_octaves,
                       float break_eff, float corrupt_eff) {
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
        ch->rate = std::pow(2.0f, internal::Clamp(bend_micro_octaves, -3.0f, 3.0f));
        ch->reverse = state.bend_enabled;

        if (!state.break_silence_mode) {
          ch->subsection_index =
              static_cast<uint32_t>(break_eff * static_cast<float>(segment.repeats));
          if (ch->subsection_index >= segment.repeats) {
            ch->subsection_index = segment.repeats - 1;
          }
          ch->silence_duty = 0.0f;
        } else {
          ch->subsection_index = 0;
          ch->silence_duty = 0.9f * break_eff;
        }
      }

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
  return state_bytes + internal::kAlignment + audio_bytes + internal::kAlignment;
}

bool Engine::initialise(void* dram, size_t dram_bytes, const EngineConfig& cfg) noexcept {
  const size_t required = required_dram_bytes(cfg);
  if (!dram || required == 0 || dram_bytes < required) {
    impl_ = nullptr;
    return false;
  }

  const uintptr_t base = reinterpret_cast<uintptr_t>(dram);
  const uintptr_t aligned_impl =
      (base + (internal::kAlignment - 1u)) & ~uintptr_t(internal::kAlignment - 1u);
  Impl* raw_impl = reinterpret_cast<Impl*>(aligned_impl);
  new (raw_impl) Impl();
  impl_ = raw_impl;

  impl_->cfg = cfg;
  impl_->buffer_frames = static_cast<uint32_t>(
      std::max(1.0, std::ceil(static_cast<double>(cfg.sample_rate_hz) *
                              static_cast<double>(cfg.max_buffer_seconds))));

  uint8_t* audio_start = reinterpret_cast<uint8_t*>(aligned_impl + sizeof(Impl));
  const uintptr_t aligned_audio =
      (reinterpret_cast<uintptr_t>(audio_start) + (internal::kAlignment - 1u)) &
      ~uintptr_t(internal::kAlignment - 1u);
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
  impl_->observed_ticks = 0;
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
  impl_->knobs.time_01 = internal::Clamp01(impl_->knobs.time_01);
  impl_->knobs.repeats_01 = internal::Clamp01(impl_->knobs.repeats_01);
  impl_->knobs.mix_01 = internal::Clamp01(impl_->knobs.mix_01);
  impl_->knobs.bend_01 = internal::Clamp01(impl_->knobs.bend_01);
  impl_->knobs.break_01 = internal::Clamp01(impl_->knobs.break_01);
  impl_->knobs.corrupt_01 = internal::Clamp01(impl_->knobs.corrupt_01);
  impl_->knobs.bend_cv_attn_01 = internal::Clamp01(impl_->knobs.bend_cv_attn_01);
  impl_->knobs.break_cv_attn_01 = internal::Clamp01(impl_->knobs.break_cv_attn_01);
  impl_->knobs.corrupt_cv_attn_01 = internal::Clamp01(impl_->knobs.corrupt_cv_attn_01);

  impl_->clock.SetTime(impl_->knobs.time_01);
}

void Engine::set_persistent_state(const PersistentState& state) noexcept {
  if (!impl_) {
    return;
  }

  impl_->state = state;
  impl_->state.glitch_window_01 = internal::Clamp01(impl_->state.glitch_window_01);
}

void Engine::set_clock_mode_internal(bool internal) noexcept {
  if (!impl_) {
    return;
  }
  impl_->clock.SetInternalMode(internal);
}

bool Engine::get_runtime_info(RuntimeInfo* out) const noexcept {
  if (!impl_ || !out) {
    return false;
  }
  out->processed_frames = impl_->processed_frames;
  out->observed_ticks = impl_->observed_ticks;
  out->external_clock_present = impl_->clock.ExternalSignalPresent();
  return true;
}

void Engine::process(const AudioBlock& audio, const CvInputs& cv,
                     const GateInputs& gates) noexcept {
  if (!impl_ || !audio.out_l || !audio.out_r || audio.frames == 0u) {
    return;
  }

  for (uint32_t i = 0; i < audio.frames; ++i) {
    const uint64_t sample_index = impl_->processed_frames;

    const float in_l = audio.in_l ? audio.in_l[i] : 0.0f;
    const float in_r = audio.in_r ? audio.in_r[i] : 0.0f;

    const bool bend_gate = internal::GateHigh(CvOrZero(gates.bend_gate_v, i));
    const bool break_gate = internal::GateHigh(CvOrZero(gates.break_gate_v, i));
    const bool corrupt_gate = internal::GateHigh(CvOrZero(gates.corrupt_gate_v, i));
    const bool freeze_gate = internal::GateHigh(CvOrZero(gates.freeze_gate_v, i));
    const bool clock_gate = internal::GateHigh(CvOrZero(gates.clock_gate_v, i));

    const bool bend_rise = internal::RisingEdge(bend_gate, impl_->prev_gate.bend);
    const bool break_rise = internal::RisingEdge(break_gate, impl_->prev_gate.brk);
    const bool corrupt_rise = internal::RisingEdge(corrupt_gate, impl_->prev_gate.corrupt);
    const bool freeze_rise = internal::RisingEdge(freeze_gate, impl_->prev_gate.freeze);
    const bool clock_rise = internal::RisingEdge(clock_gate, impl_->prev_gate.clock);

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
    const float bend_micro_oct =
        (impl_->knobs.bend_01 * 2.0f - 1.0f) * 3.0f +
        CvOrZero(cv.bend_v, i) * impl_->knobs.bend_cv_attn_01;
    const float break_eff =
        impl_->EffectiveUnipolar(impl_->knobs.break_01, CvOrZero(cv.break_v, i),
                                 impl_->knobs.break_cv_attn_01);
    const float corrupt_eff =
        impl_->EffectiveUnipolar(impl_->knobs.corrupt_01, CvOrZero(cv.corrupt_v, i),
                                 impl_->knobs.corrupt_cv_attn_01);

    if (tick) {
      ++impl_->observed_ticks;
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

      const uint32_t sub_idx =
          std::min(pcs.subsection_index, std::max(1u, impl_->segment.repeats) - 1u);
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
            (1.0f - internal::Clamp01(pcs.silence_duty)) *
            static_cast<float>(impl_->segment.subsection_length);
        if (pcs.phase >= silence_from) {
          wet = 0.0f;
        }
      }

      if (impl_->corrupt_enabled || impl_->knobs.corrupt_01 > 0.0001f ||
          CvOrZero(cv.corrupt_v, i) != 0.0f) {
        wet = internal::ProcessCorruptSample(
            wet, corrupt_eff, impl_->state.corrupt_bank, impl_->state.corrupt_algorithm,
            &pcs.corrupt, &impl_->rng, impl_->cfg.sample_rate_hz);
      }

      pcs.phase += smooth_rate;
      if (pcs.phase >= static_cast<float>(impl_->segment.subsection_length)) {
        pcs.phase = internal::WrapPositive(
            pcs.phase, static_cast<float>(impl_->segment.subsection_length));
      }

      const float mix_cv = CvOrZero(cv.mix_v, i) * 0.2f;
      float mix = internal::Clamp01(impl_->knobs.mix_01 + mix_cv);
      if (impl_->state.freeze_enabled && mix < 0.001f) {
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
