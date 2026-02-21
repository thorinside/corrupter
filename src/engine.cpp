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

  void SetTimeMs(float ms, float sample_rate_hz) {
    const float sr = std::max(1.0f, sample_rate_hz);
    const float tau_s = std::max(0.0001f, ms * 0.001f);
    coeff = 1.0f - std::exp(-1.0f / (tau_s * sr));
  }

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
  uint32_t repeat_scale = 1;
  float silence_duty = 0.0f;
  bool reverse = false;
  OnePole rate_smoother;
  float tape_wow_phase = 0.0f;
  float tape_flutter_phase = 0.0f;
  float tape_wow_hz = 0.0f;
  float tape_flutter_hz = 0.0f;
  float tape_wow_depth = 0.0f;
  float tape_flutter_depth = 0.0f;
  float tape_drive = 0.0f;
  float tape_color_coeff = 0.0f;
  float tape_color_lp = 0.0f;
  internal::CorruptChannelState corrupt;

  void Reset() {
    phase = 0.0f;
    rate = 1.0f;
    subsection_index = 0;
    repeat_scale = 1;
    silence_duty = 0.0f;
    reverse = false;
    tape_wow_phase = 0.0f;
    tape_flutter_phase = 0.0f;
    tape_wow_hz = 0.0f;
    tape_flutter_hz = 0.0f;
    tape_wow_depth = 0.0f;
    tape_flutter_depth = 0.0f;
    tape_drive = 0.0f;
    tape_color_coeff = 0.0f;
    tape_color_lp = 0.0f;
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

constexpr float kTwoPi = 6.28318530717958647693f;

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

float Hermite4(float y0, float y1, float y2, float y3, float t) {
  const float c0 = y1;
  const float c1 = 0.5f * (y2 - y0);
  const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
  const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
  return ((c3 * t + c2) * t + c1) * t + c0;
}

float ReadBufferCubic(const float* buffer, uint32_t buffer_frames, float index) {
  if (!buffer || buffer_frames == 0u) {
    return 0.0f;
  }
  if (buffer_frames < 4u) {
    return ReadBufferLinear(buffer, buffer_frames, index);
  }

  const float wrapped = internal::WrapPositive(index, static_cast<float>(buffer_frames));
  const uint32_t i1 = static_cast<uint32_t>(wrapped);
  const uint32_t i0 = (i1 + buffer_frames - 1u) % buffer_frames;
  const uint32_t i2 = (i1 + 1u) % buffer_frames;
  const uint32_t i3 = (i1 + 2u) % buffer_frames;
  const float t = wrapped - static_cast<float>(i1);

  const float y0 = buffer[i0];
  const float y1 = buffer[i1];
  const float y2 = buffer[i2];
  const float y3 = buffer[i3];
  const float v = Hermite4(y0, y1, y2, y3, t);
  const float lo = std::min(y1, y2) - 0.25f;
  const float hi = std::max(y1, y2) + 0.25f;
  return internal::Clamp(v, lo, hi);
}

struct PersistentBlobV1 {
  uint32_t magic;
  uint16_t version;
  uint8_t bend_enabled;
  uint8_t break_enabled;
  uint8_t freeze_enabled;
  uint8_t macro_mode;
  uint8_t break_silence_mode;
  uint8_t unique_stereo_mode;
  uint8_t gate_latching;
  uint8_t freeze_latching;
  uint8_t corrupt_gate_is_reset;
  uint8_t corrupt_bank;
  uint8_t corrupt_algorithm;
  float glitch_window_01;
};

constexpr uint32_t kPersistentMagic = 0x50525243u;  // 'CRRP'
constexpr uint16_t kPersistentVersion = 1u;

float EffectiveInitialSampleRate(const EngineConfig& cfg) {
  return (cfg.sample_rate_hz > 0.0f) ? cfg.sample_rate_hz : 96000.0f;
}

float EffectiveMaxSampleRate(const EngineConfig& cfg) {
  const float init_sr = EffectiveInitialSampleRate(cfg);
  const float max_sr =
      (cfg.max_supported_sample_rate_hz > 0.0f) ? cfg.max_supported_sample_rate_hz : init_sr;
  return std::max(init_sr, max_sr);
}

uint32_t EffectiveMaxBlockFrames(const EngineConfig& cfg) {
  return std::max(1u, cfg.max_block_frames);
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
  float current_rate_l = 1.0f;
  float current_rate_r = 1.0f;
  float runtime_sample_rate_hz = 96000.0f;
  uint32_t runtime_max_block_frames = 256;
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
                       float break_eff, float corrupt_eff, float sample_rate_hz) {
    const bool shared = (!state.unique_stereo_mode) || (!state.macro_mode);
    const float sr = std::max(1.0f, sample_rate_hz);

    auto apply_one = [&](PlaybackChannelState* ch) {
      if (!ch) {
        return;
      }

      if (state.macro_mode) {
        if (state.bend_enabled) {
          const float wobble = internal::Clamp01(bend_macro_eff);
          float rate_oct = rng.NextSigned() * (0.35f + 2.65f * wobble);
          if (rng.Next01() < (0.12f * wobble)) {
            rate_oct -= (0.5f + 1.7f * rng.Next01()) * wobble;
          }
          ch->rate = std::pow(2.0f, internal::Clamp(rate_oct, -3.0f, 3.0f));
          ch->reverse = (rng.Next01() < (0.15f + 0.75f * bend_macro_eff));
          ch->tape_wow_hz = 0.05f + 0.55f * wobble * (0.6f + 0.4f * rng.Next01());
          ch->tape_flutter_hz = 2.0f + 9.0f * wobble * (0.7f + 0.3f * rng.Next01());
          ch->tape_wow_depth = 0.0005f + 0.028f * wobble;
          ch->tape_flutter_depth = 0.0003f + 0.012f * wobble;
          ch->tape_drive = 0.08f + 1.15f * wobble;
          const float tape_cut_hz = 18000.0f - 12000.0f * wobble;
          ch->tape_color_coeff = std::exp(-kTwoPi * tape_cut_hz / sr);
          ch->rate_smoother.SetTimeMs(8.0f + 95.0f * wobble, sr);
        } else {
          ch->rate = 1.0f;
          ch->reverse = false;
          ch->tape_wow_hz = 0.0f;
          ch->tape_flutter_hz = 0.0f;
          ch->tape_wow_depth = 0.0f;
          ch->tape_flutter_depth = 0.0f;
          ch->tape_drive = 0.0f;
          ch->tape_color_coeff = 0.0f;
          ch->rate_smoother.SetTimeMs(6.0f, sr);
        }

        if (state.break_enabled) {
          ch->subsection_index =
              static_cast<uint32_t>(rng.Next01() * static_cast<float>(segment.repeats));
          if (ch->subsection_index >= segment.repeats) {
            ch->subsection_index = segment.repeats - 1;
          }
          const uint32_t max_scale =
              1u + static_cast<uint32_t>(break_eff * 7.0f);
          ch->repeat_scale =
              1u + static_cast<uint32_t>(rng.Next01() * static_cast<float>(max_scale));
          if (ch->repeat_scale > max_scale) {
            ch->repeat_scale = max_scale;
          }
          ch->silence_duty = 0.9f * break_eff * rng.Next01();
        } else {
          ch->subsection_index = 0;
          ch->repeat_scale = 1;
          ch->silence_duty = 0.0f;
        }
      } else {
        const float micro_oct = internal::Clamp(bend_micro_octaves, -3.0f, 3.0f);
        const float micro_intensity =
            state.bend_enabled ? internal::Clamp01(std::fabs(micro_oct) / 3.0f) : 0.0f;
        ch->rate = std::pow(2.0f, micro_oct);
        ch->reverse = state.bend_enabled;
        ch->tape_wow_hz = 0.12f + 0.30f * micro_intensity;
        ch->tape_flutter_hz = 3.0f + 5.0f * micro_intensity;
        ch->tape_wow_depth = 0.0002f + 0.0080f * micro_intensity;
        ch->tape_flutter_depth = 0.0002f + 0.0030f * micro_intensity;
        ch->tape_drive = 0.05f + 0.45f * micro_intensity;
        const float tape_cut_hz = 19000.0f - 7000.0f * micro_intensity;
        ch->tape_color_coeff = std::exp(-kTwoPi * tape_cut_hz / sr);
        ch->rate_smoother.SetTimeMs(4.0f + 20.0f * micro_intensity, sr);

        if (!state.break_silence_mode) {
          ch->subsection_index =
              static_cast<uint32_t>(break_eff * static_cast<float>(segment.repeats));
          if (ch->subsection_index >= segment.repeats) {
            ch->subsection_index = segment.repeats - 1;
          }
          ch->repeat_scale = 1;
          ch->silence_duty = 0.0f;
        } else {
          ch->subsection_index = 0;
          ch->repeat_scale = 1;
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
      channels[1].repeat_scale = channels[0].repeat_scale;
      channels[1].silence_duty = channels[0].silence_duty;
      channels[1].tape_wow_hz = channels[0].tape_wow_hz;
      channels[1].tape_flutter_hz = channels[0].tape_flutter_hz;
      channels[1].tape_wow_depth = channels[0].tape_wow_depth;
      channels[1].tape_flutter_depth = channels[0].tape_flutter_depth;
      channels[1].tape_drive = channels[0].tape_drive;
      channels[1].tape_color_coeff = channels[0].tape_color_coeff;
      channels[1].rate_smoother.coeff = channels[0].rate_smoother.coeff;
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
  if (cfg.max_buffer_seconds <= 0.0f) {
    return 0;
  }

  const float max_sample_rate_hz = EffectiveMaxSampleRate(cfg);
  const double frames_f64 =
      static_cast<double>(max_sample_rate_hz) * static_cast<double>(cfg.max_buffer_seconds);
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
  impl_->cfg.sample_rate_hz = EffectiveInitialSampleRate(cfg);
  impl_->cfg.max_supported_sample_rate_hz = EffectiveMaxSampleRate(cfg);
  impl_->cfg.max_block_frames = EffectiveMaxBlockFrames(cfg);
  impl_->buffer_frames = static_cast<uint32_t>(
      std::max(1.0, std::ceil(static_cast<double>(impl_->cfg.max_supported_sample_rate_hz) *
                              static_cast<double>(cfg.max_buffer_seconds))));
  impl_->runtime_sample_rate_hz = impl_->cfg.sample_rate_hz;
  impl_->runtime_max_block_frames = impl_->cfg.max_block_frames;

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
  impl_->clock.Reset(impl_->runtime_sample_rate_hz, impl_->knobs.time_01);
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
  impl_->current_rate_l = 1.0f;
  impl_->current_rate_r = 1.0f;
  impl_->prev_gate = {};
  impl_->pending_freeze_toggle = false;
  impl_->corrupt_enabled = false;
  impl_->runtime_sample_rate_hz = impl_->cfg.sample_rate_hz;
  impl_->runtime_max_block_frames = impl_->cfg.max_block_frames;
  impl_->clock.Reset(impl_->runtime_sample_rate_hz, impl_->knobs.time_01);
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

void Engine::set_audio_context(float sample_rate_hz, uint32_t max_block_frames) noexcept {
  if (!impl_) {
    return;
  }

  const float max_sr = EffectiveMaxSampleRate(impl_->cfg);
  const float requested_sr =
      (sample_rate_hz > 0.0f) ? sample_rate_hz : impl_->runtime_sample_rate_hz;
  const float clamped_sr = internal::Clamp(requested_sr, 1.0f, max_sr);
  if (std::fabs(clamped_sr - impl_->runtime_sample_rate_hz) > 1e-6f) {
    impl_->runtime_sample_rate_hz = clamped_sr;
    impl_->clock.SetSampleRate(clamped_sr, impl_->processed_frames);
  }

  if (max_block_frames > 0u) {
    impl_->runtime_max_block_frames = max_block_frames;
  }
}

void Engine::set_persistent_state(const PersistentState& state) noexcept {
  if (!impl_) {
    return;
  }

  impl_->state = state;
  impl_->state.glitch_window_01 = internal::Clamp01(impl_->state.glitch_window_01);
}

bool Engine::get_persistent_state(PersistentState* out) const noexcept {
  if (!impl_ || !out) {
    return false;
  }
  *out = impl_->state;
  return true;
}

bool Engine::serialise_persistent_state(void* out, size_t out_bytes, size_t* written) const noexcept {
  if (!impl_ || !out || out_bytes < sizeof(PersistentBlobV1)) {
    return false;
  }

  PersistentBlobV1 blob{};
  blob.magic = kPersistentMagic;
  blob.version = kPersistentVersion;
  blob.bend_enabled = impl_->state.bend_enabled ? 1u : 0u;
  blob.break_enabled = impl_->state.break_enabled ? 1u : 0u;
  blob.freeze_enabled = impl_->state.freeze_enabled ? 1u : 0u;
  blob.macro_mode = impl_->state.macro_mode ? 1u : 0u;
  blob.break_silence_mode = impl_->state.break_silence_mode ? 1u : 0u;
  blob.unique_stereo_mode = impl_->state.unique_stereo_mode ? 1u : 0u;
  blob.gate_latching = impl_->state.gate_latching ? 1u : 0u;
  blob.freeze_latching = impl_->state.freeze_latching ? 1u : 0u;
  blob.corrupt_gate_is_reset = impl_->state.corrupt_gate_is_reset ? 1u : 0u;
  blob.corrupt_bank = static_cast<uint8_t>(impl_->state.corrupt_bank);
  blob.corrupt_algorithm = static_cast<uint8_t>(impl_->state.corrupt_algorithm);
  blob.glitch_window_01 = impl_->state.glitch_window_01;

  std::memcpy(out, &blob, sizeof(blob));
  if (written) {
    *written = sizeof(blob);
  }
  return true;
}

bool Engine::deserialise_persistent_state(const void* data, size_t data_bytes) noexcept {
  if (!impl_ || !data || data_bytes < sizeof(PersistentBlobV1)) {
    return false;
  }

  PersistentBlobV1 blob{};
  std::memcpy(&blob, data, sizeof(blob));
  if (blob.magic != kPersistentMagic || blob.version != kPersistentVersion) {
    return false;
  }

  PersistentState state = impl_->state;
  state.bend_enabled = (blob.bend_enabled != 0u);
  state.break_enabled = (blob.break_enabled != 0u);
  state.freeze_enabled = (blob.freeze_enabled != 0u);
  state.macro_mode = (blob.macro_mode != 0u);
  state.break_silence_mode = (blob.break_silence_mode != 0u);
  state.unique_stereo_mode = (blob.unique_stereo_mode != 0u);
  state.gate_latching = (blob.gate_latching != 0u);
  state.freeze_latching = (blob.freeze_latching != 0u);
  state.corrupt_gate_is_reset = (blob.corrupt_gate_is_reset != 0u);
  state.corrupt_bank = static_cast<CorruptBank>(blob.corrupt_bank);
  state.corrupt_algorithm = static_cast<CorruptAlgorithm>(blob.corrupt_algorithm);
  state.glitch_window_01 = internal::Clamp01(blob.glitch_window_01);
  set_persistent_state(state);
  return true;
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
  out->current_rate_l = impl_->current_rate_l;
  out->current_rate_r = impl_->current_rate_r;
  out->sample_rate_hz = impl_->runtime_sample_rate_hz;
  out->max_block_frames = impl_->runtime_max_block_frames;
  return true;
}

void Engine::process(const AudioBlock& audio, const CvInputs& cv,
                     const GateInputs& gates) noexcept {
  if (!impl_ || !audio.out_l || !audio.out_r || audio.frames == 0u) {
    return;
  }

  if (audio.frames > impl_->runtime_max_block_frames) {
    impl_->runtime_max_block_frames = audio.frames;
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
      impl_->ApplyTickEvents(bend_eff, bend_micro_oct, break_eff, corrupt_eff,
                             impl_->runtime_sample_rate_hz);
    }

    if (!impl_->state.freeze_enabled) {
      impl_->buffer_l[impl_->write_idx] = in_l;
      impl_->buffer_r[impl_->write_idx] = in_r;
      impl_->write_idx = (impl_->write_idx + 1u) % impl_->buffer_frames;
    }

    auto render_channel = [&](int ch, float input_sample) {
      PlaybackChannelState& pcs = impl_->channels[ch];
      const float* buffer = (ch == 0) ? impl_->buffer_l : impl_->buffer_r;

      const float sr = std::max(1.0f, impl_->runtime_sample_rate_hz);
      if ((pcs.tape_wow_depth > 0.0f) || (pcs.tape_flutter_depth > 0.0f)) {
        pcs.tape_wow_phase += (kTwoPi * pcs.tape_wow_hz) / sr;
        if (pcs.tape_wow_phase >= kTwoPi) {
          pcs.tape_wow_phase -= kTwoPi;
        }
        pcs.tape_flutter_phase += (kTwoPi * pcs.tape_flutter_hz) / sr;
        if (pcs.tape_flutter_phase >= kTwoPi) {
          pcs.tape_flutter_phase -= kTwoPi;
        }
      }
      const float wow = pcs.tape_wow_depth * std::sin(pcs.tape_wow_phase);
      const float flutter = pcs.tape_flutter_depth * std::sin(pcs.tape_flutter_phase);
      const float tape_mod = std::max(0.2f, 1.0f + wow + flutter);
      const float target_rate = std::max(0.01f, pcs.rate * tape_mod);
      const float smooth_rate = pcs.rate_smoother.Tick(target_rate);
      if (ch == 0) {
        impl_->current_rate_l = smooth_rate;
      } else {
        impl_->current_rate_r = smooth_rate;
      }

      const uint32_t sub_idx =
          std::min(pcs.subsection_index, std::max(1u, impl_->segment.repeats) - 1u);
      const uint32_t sub_len = std::max(
          1u, impl_->segment.subsection_length / std::max(1u, pcs.repeat_scale));
      const uint32_t subsection_start =
          (impl_->segment.start + sub_idx * impl_->segment.subsection_length) % impl_->buffer_frames;

      float read_pos = pcs.phase;
      if (pcs.reverse) {
        read_pos = static_cast<float>(sub_len - 1u) - read_pos;
      }

      const float read_index = static_cast<float>(subsection_start) + read_pos;
      float wet = ReadBufferCubic(buffer, impl_->buffer_frames, read_index);

      const float window = impl_->ComputeWindowGain(pcs.phase, sub_len);
      wet *= window;

      if (pcs.silence_duty > 0.0f) {
        const float silence_from =
            (1.0f - internal::Clamp01(pcs.silence_duty)) *
            static_cast<float>(sub_len);
        if (pcs.phase >= silence_from) {
          wet = 0.0f;
        }
      }

      if ((pcs.tape_drive > 0.0f) || (pcs.tape_color_coeff > 0.0f)) {
        const float driven = std::tanh(wet * (1.0f + pcs.tape_drive));
        const float a = internal::Clamp(pcs.tape_color_coeff, 0.0f, 0.9999f);
        pcs.tape_color_lp = (1.0f - a) * driven + a * pcs.tape_color_lp;
        wet = 0.65f * driven + 0.35f * pcs.tape_color_lp;
      }

      if (impl_->corrupt_enabled || impl_->knobs.corrupt_01 > 0.0001f ||
          CvOrZero(cv.corrupt_v, i) != 0.0f) {
        wet = internal::ProcessCorruptSample(
            wet, corrupt_eff, impl_->state.corrupt_bank, impl_->state.corrupt_algorithm,
            &pcs.corrupt, &impl_->rng, impl_->runtime_sample_rate_hz);
      }

      pcs.phase += smooth_rate;
      if (pcs.phase >= static_cast<float>(sub_len)) {
        pcs.phase = internal::WrapPositive(
            pcs.phase, static_cast<float>(sub_len));
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
