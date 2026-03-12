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
  kParamCorruptBank = 24,
  kParamCorruptAlgorithm = 25,
  kParamGlitchWindow = 26,
  kParamTimeCvInput = 27,
  kParamRepeatsCvInput = 28,
  kParamMixCvInput = 29,
  kParamBendCvInput = 30,
  kParamBreakCvInput = 31,
  kParamCorruptCvInput = 32,
  kParamBendGateInput = 33,
  kParamBreakGateInput = 34,
  kParamFreezeGateInput = 35,
  kParamClockGateInput = 36,
  kParamRandomSeedMode = 37,
  kParamFixedSeed = 38,
  kParamScaleFile = 39,
  kParamScaleRoot = 40,
  kParamCount = 41
};

}  // namespace corrupter

#endif  // CORRUPTER_DSP_PARAMETER_IDS_H_
