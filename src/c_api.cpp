#include "corrupter_dsp/c_api.h"

#include <new>

#include "corrupter_dsp/engine.h"

namespace {

static inline corrupter::Engine* ToEngine(void* p) {
  return static_cast<corrupter::Engine*>(p);
}

static corrupter::EngineConfig ToCpp(const corrupter_engine_config_t& cfg) {
  corrupter::EngineConfig out;
  out.sample_rate_hz = cfg.sample_rate_hz;
  out.max_block_frames = cfg.max_block_frames;
  out.max_buffer_seconds = cfg.max_buffer_seconds;
  out.random_seed = cfg.random_seed;
  return out;
}

static corrupter::PersistentState ToCpp(const corrupter_persistent_state_t& state) {
  corrupter::PersistentState out;
  out.bend_enabled = state.bend_enabled != 0;
  out.break_enabled = state.break_enabled != 0;
  out.freeze_enabled = state.freeze_enabled != 0;
  out.macro_mode = state.macro_mode != 0;
  out.break_silence_mode = state.break_silence_mode != 0;
  out.unique_stereo_mode = state.unique_stereo_mode != 0;
  out.gate_latching = state.gate_latching != 0;
  out.freeze_latching = state.freeze_latching != 0;
  out.corrupt_gate_is_reset = state.corrupt_gate_is_reset != 0;
  out.corrupt_bank = static_cast<corrupter::CorruptBank>(state.corrupt_bank);
  out.corrupt_algorithm = static_cast<corrupter::CorruptAlgorithm>(state.corrupt_algorithm);
  out.glitch_window_01 = state.glitch_window_01;
  return out;
}

static corrupter::KnobState ToCpp(const corrupter_knob_state_t& knobs) {
  corrupter::KnobState out;
  out.time_01 = knobs.time_01;
  out.repeats_01 = knobs.repeats_01;
  out.mix_01 = knobs.mix_01;
  out.bend_01 = knobs.bend_01;
  out.break_01 = knobs.break_01;
  out.corrupt_01 = knobs.corrupt_01;
  out.bend_cv_attn_01 = knobs.bend_cv_attn_01;
  out.break_cv_attn_01 = knobs.break_cv_attn_01;
  out.corrupt_cv_attn_01 = knobs.corrupt_cv_attn_01;
  return out;
}

static corrupter::CvInputs ToCpp(const corrupter_cv_inputs_t& cv) {
  corrupter::CvInputs out;
  out.time_v = cv.time_v;
  out.repeats_v = cv.repeats_v;
  out.mix_v = cv.mix_v;
  out.bend_v = cv.bend_v;
  out.break_v = cv.break_v;
  out.corrupt_v = cv.corrupt_v;
  return out;
}

static corrupter::GateInputs ToCpp(const corrupter_gate_inputs_t& gates) {
  corrupter::GateInputs out;
  out.bend_gate_v = gates.bend_gate_v;
  out.break_gate_v = gates.break_gate_v;
  out.corrupt_gate_v = gates.corrupt_gate_v;
  out.freeze_gate_v = gates.freeze_gate_v;
  out.clock_gate_v = gates.clock_gate_v;
  return out;
}

static corrupter::AudioBlock ToCpp(const corrupter_audio_block_t& audio) {
  corrupter::AudioBlock out;
  out.in_l = audio.in_l;
  out.in_r = audio.in_r;
  out.out_l = audio.out_l;
  out.out_r = audio.out_r;
  out.frames = audio.frames;
  return out;
}

}  // namespace

extern "C" {

size_t corrupter_engine_sizeof(void) {
  return sizeof(corrupter::Engine);
}

size_t corrupter_engine_required_dram_bytes(const corrupter_engine_config_t* cfg) {
  if (!cfg) {
    return 0;
  }
  const corrupter::EngineConfig cpp_cfg = ToCpp(*cfg);
  return corrupter::Engine::required_dram_bytes(cpp_cfg);
}

int corrupter_engine_construct(void* engine_memory, size_t engine_memory_bytes) {
  if (!engine_memory || engine_memory_bytes < sizeof(corrupter::Engine)) {
    return 0;
  }
  new (engine_memory) corrupter::Engine();
  return 1;
}

void corrupter_engine_destruct(void* engine_memory) {
  if (!engine_memory) {
    return;
  }
  ToEngine(engine_memory)->~Engine();
}

int corrupter_engine_initialise(void* engine_memory, void* dram, size_t dram_bytes,
                                const corrupter_engine_config_t* cfg) {
  if (!engine_memory || !cfg) {
    return 0;
  }
  const corrupter::EngineConfig cpp_cfg = ToCpp(*cfg);
  return ToEngine(engine_memory)->initialise(dram, dram_bytes, cpp_cfg) ? 1 : 0;
}

void corrupter_engine_reset(void* engine_memory) {
  if (!engine_memory) {
    return;
  }
  ToEngine(engine_memory)->reset();
}

void corrupter_engine_set_knobs(void* engine_memory, const corrupter_knob_state_t* knobs) {
  if (!engine_memory || !knobs) {
    return;
  }
  ToEngine(engine_memory)->set_knobs(ToCpp(*knobs));
}

void corrupter_engine_set_persistent_state(void* engine_memory,
                                           const corrupter_persistent_state_t* state) {
  if (!engine_memory || !state) {
    return;
  }
  ToEngine(engine_memory)->set_persistent_state(ToCpp(*state));
}

int corrupter_engine_serialise_persistent_state(void* engine_memory, void* out,
                                                size_t out_bytes, size_t* written) {
  if (!engine_memory) {
    return 0;
  }
  return ToEngine(engine_memory)->serialise_persistent_state(out, out_bytes, written) ? 1 : 0;
}

int corrupter_engine_deserialise_persistent_state(void* engine_memory, const void* data,
                                                  size_t data_bytes) {
  if (!engine_memory) {
    return 0;
  }
  return ToEngine(engine_memory)->deserialise_persistent_state(data, data_bytes) ? 1 : 0;
}

void corrupter_engine_set_clock_mode_internal(void* engine_memory, int internal) {
  if (!engine_memory) {
    return;
  }
  ToEngine(engine_memory)->set_clock_mode_internal(internal != 0);
}

void corrupter_engine_process(void* engine_memory, const corrupter_audio_block_t* audio,
                              const corrupter_cv_inputs_t* cv,
                              const corrupter_gate_inputs_t* gates) {
  if (!engine_memory || !audio) {
    return;
  }

  const corrupter::AudioBlock cpp_audio = ToCpp(*audio);
  const corrupter::CvInputs cpp_cv = cv ? ToCpp(*cv) : corrupter::CvInputs();
  const corrupter::GateInputs cpp_gates = gates ? ToCpp(*gates) : corrupter::GateInputs();
  ToEngine(engine_memory)->process(cpp_audio, cpp_cv, cpp_gates);
}

}  // extern "C"
