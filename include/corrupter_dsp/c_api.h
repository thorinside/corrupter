#ifndef CORRUPTER_DSP_C_API_H_
#define CORRUPTER_DSP_C_API_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  float sample_rate_hz;
  uint32_t max_block_frames;
  float max_buffer_seconds;
  uint32_t random_seed;
} corrupter_engine_config_t;

typedef struct {
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
} corrupter_persistent_state_t;

typedef struct {
  float time_01;
  float repeats_01;
  float mix_01;
  float bend_01;
  float break_01;
  float corrupt_01;
  float bend_cv_attn_01;
  float break_cv_attn_01;
  float corrupt_cv_attn_01;
} corrupter_knob_state_t;

typedef struct {
  const float* time_v;
  const float* repeats_v;
  const float* mix_v;
  const float* bend_v;
  const float* break_v;
  const float* corrupt_v;
} corrupter_cv_inputs_t;

typedef struct {
  const float* bend_gate_v;
  const float* break_gate_v;
  const float* corrupt_gate_v;
  const float* freeze_gate_v;
  const float* clock_gate_v;
} corrupter_gate_inputs_t;

typedef struct {
  const float* in_l;
  const float* in_r;
  float* out_l;
  float* out_r;
  uint32_t frames;
} corrupter_audio_block_t;

size_t corrupter_engine_sizeof(void);
size_t corrupter_engine_required_dram_bytes(const corrupter_engine_config_t* cfg);
int corrupter_engine_construct(void* engine_memory, size_t engine_memory_bytes);
void corrupter_engine_destruct(void* engine_memory);
int corrupter_engine_initialise(void* engine_memory, void* dram, size_t dram_bytes,
                                const corrupter_engine_config_t* cfg);
void corrupter_engine_reset(void* engine_memory);
void corrupter_engine_set_knobs(void* engine_memory, const corrupter_knob_state_t* knobs);
void corrupter_engine_set_persistent_state(void* engine_memory,
                                           const corrupter_persistent_state_t* state);
void corrupter_engine_set_clock_mode_internal(void* engine_memory, int internal);
void corrupter_engine_process(void* engine_memory, const corrupter_audio_block_t* audio,
                              const corrupter_cv_inputs_t* cv,
                              const corrupter_gate_inputs_t* gates);

#ifdef __cplusplus
}
#endif

#endif  // CORRUPTER_DSP_C_API_H_

