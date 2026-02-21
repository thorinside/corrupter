#ifndef CORRUPTER_DSP_ENGINE_H_
#define CORRUPTER_DSP_ENGINE_H_

#include <cstddef>
#include <cstdint>

namespace corrupter {

enum class CorruptBank : uint8_t {
  kLegacy = 0,
  kExpanded = 1
};

enum class CorruptAlgorithm : uint8_t {
  kDecimate = 0,
  kDropout = 1,
  kDestroy = 2,
  kDjFilter = 3,
  kVinylSim = 4
};

struct EngineConfig {
  float sample_rate_hz = 96000.0f;
  uint32_t max_block_frames = 256;
  float max_buffer_seconds = 60.0f;
  uint32_t random_seed = 1;
};

struct PersistentState {
  bool bend_enabled = false;
  bool break_enabled = false;
  bool freeze_enabled = false;
  bool macro_mode = true;
  bool break_silence_mode = false;
  bool unique_stereo_mode = false;
  bool gate_latching = true;
  bool freeze_latching = true;
  bool corrupt_gate_is_reset = false;
  CorruptBank corrupt_bank = CorruptBank::kLegacy;
  CorruptAlgorithm corrupt_algorithm = CorruptAlgorithm::kDecimate;
  float glitch_window_01 = 0.02f;
};

struct KnobState {
  float time_01 = 0.5f;
  float repeats_01 = 0.0f;
  float mix_01 = 0.0f;
  float bend_01 = 0.0f;
  float break_01 = 0.0f;
  float corrupt_01 = 0.0f;
  float bend_cv_attn_01 = 1.0f;
  float break_cv_attn_01 = 1.0f;
  float corrupt_cv_attn_01 = 1.0f;
};

struct CvInputs {
  const float* time_v = nullptr;
  const float* repeats_v = nullptr;
  const float* mix_v = nullptr;
  const float* bend_v = nullptr;
  const float* break_v = nullptr;
  const float* corrupt_v = nullptr;
};

struct GateInputs {
  const float* bend_gate_v = nullptr;
  const float* break_gate_v = nullptr;
  const float* corrupt_gate_v = nullptr;
  const float* freeze_gate_v = nullptr;
  const float* clock_gate_v = nullptr;
};

struct AudioBlock {
  const float* in_l = nullptr;
  const float* in_r = nullptr;
  float* out_l = nullptr;
  float* out_r = nullptr;
  uint32_t frames = 0;
};

struct RuntimeInfo {
  uint64_t processed_frames = 0;
  uint64_t observed_ticks = 0;
  bool external_clock_present = false;
  float current_rate_l = 1.0f;
  float current_rate_r = 1.0f;
};

class Engine {
 public:
  Engine() noexcept;
  ~Engine() noexcept;
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  static size_t required_dram_bytes(const EngineConfig& cfg) noexcept;

  bool initialise(void* dram, size_t dram_bytes, const EngineConfig& cfg) noexcept;
  void reset() noexcept;

  void set_knobs(const KnobState& knobs) noexcept;
  void set_persistent_state(const PersistentState& state) noexcept;
  bool get_persistent_state(PersistentState* out) const noexcept;
  void set_clock_mode_internal(bool internal) noexcept;
  bool get_runtime_info(RuntimeInfo* out) const noexcept;

  void process(const AudioBlock& audio, const CvInputs& cv, const GateInputs& gates) noexcept;

 private:
  struct Impl;
  Impl* impl_;
};

}  // namespace corrupter

#endif  // CORRUPTER_DSP_ENGINE_H_
