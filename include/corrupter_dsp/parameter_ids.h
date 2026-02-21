#ifndef CORRUPTER_DSP_PARAMETER_IDS_H_
#define CORRUPTER_DSP_PARAMETER_IDS_H_

#include <cstdint>

namespace corrupter {

enum class DistingNtParamId : uint16_t {
  kParamAudioInL = 0,
  kParamAudioInR = 1,
  kParamAudioOutL = 2,
  kParamAudioOutLMode = 3,
  kParamAudioOutR = 4,
  kParamAudioOutRMode = 5,
  kParamTime = 6,
  kParamRepeats = 7,
  kParamMix = 8,
  kParamBend = 9,
  kParamBreak = 10,
  kParamCorrupt = 11,
  kParamBendCvAttn = 12,
  kParamBreakCvAttn = 13,
  kParamCorruptCvAttn = 14,
  kParamMode = 15,
  kParamBreakMicroMode = 16,
  kParamBendEnabled = 17,
  kParamBreakEnabled = 18,
  kParamFreezeEnabled = 19,
  kParamClockSource = 20,
  kParamStereoMode = 21,
  kParamGateMode = 22,
  kParamFreezeGateMode = 23,
  kParamCorruptGateMode = 24,
  kParamCorruptBank = 25,
  kParamCorruptAlgorithm = 26,
  kParamGlitchWindow = 27,
  kParamTimeCvInput = 28,
  kParamRepeatsCvInput = 29,
  kParamMixCvInput = 30,
  kParamBendCvInput = 31,
  kParamBreakCvInput = 32,
  kParamCorruptCvInput = 33,
  kParamBendGateInput = 34,
  kParamBreakGateInput = 35,
  kParamCorruptGateInput = 36,
  kParamFreezeGateInput = 37,
  kParamClockGateInput = 38,
  kParamRandomSeedMode = 39,
  kParamFixedSeed = 40,
  kParamBufferSeconds = 41,
  kParamResetEngine = 42,
  kParamRestoreDefaults = 43,
  kParamCount = 44
};

}  // namespace corrupter

#endif  // CORRUPTER_DSP_PARAMETER_IDS_H_

