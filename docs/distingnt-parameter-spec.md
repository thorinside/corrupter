# Corrupter disting NT Parameter and Page Spec

This file is the concrete wrapper contract for the implementer building the disting NT plug-in around `libcorrupter_dsp`.

## 1. Parameter ID Map (Stable)

These IDs are stable and should not be reordered once presets exist.

| ID | Symbol | Name | Type | Min | Max | Default | Unit | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | `kParamAudioInL` | In L | audio bus | 1 | `kNT_lastBus` | 1 | audio input | Required |
| 1 | `kParamAudioInR` | In R | audio bus | 1 | `kNT_lastBus` | 2 | audio input | Required |
| 2 | `kParamAudioOutL` | Out L | audio bus | 1 | `kNT_lastBus` | 13 | audio output | Required |
| 3 | `kParamAudioOutLMode` | Out L mode | enum | 0 | 1 | 1 | output mode | 0 add, 1 replace |
| 4 | `kParamAudioOutR` | Out R | audio bus | 1 | `kNT_lastBus` | 14 | audio output | Required |
| 5 | `kParamAudioOutRMode` | Out R mode | enum | 0 | 1 | 1 | output mode | 0 add, 1 replace |
| 6 | `kParamTime` | Time | scalar | 0 | 1000 | 500 | percent x10 | 0..1 mapped in wrapper |
| 7 | `kParamRepeats` | Repeats | scalar | 0 | 1000 | 0 | percent x10 | 0..1 mapped |
| 8 | `kParamMix` | Mix | scalar | 0 | 1000 | 0 | percent x10 | dry->wet |
| 9 | `kParamBend` | Bend | scalar | 0 | 1000 | 0 | percent x10 | macro intensity/micro speed base |
| 10 | `kParamBreak` | Break | scalar | 0 | 1000 | 0 | percent x10 | macro intensity/micro target |
| 11 | `kParamCorrupt` | Corrupt | scalar | 0 | 1000 | 0 | percent x10 | algorithm intensity |
| 12 | `kParamBendCvAttn` | Bend CV Attn | scalar | 0 | 1000 | 1000 | percent x10 | 0..1 |
| 13 | `kParamBreakCvAttn` | Break CV Attn | scalar | 0 | 1000 | 1000 | percent x10 | 0..1 |
| 14 | `kParamCorruptCvAttn` | Corrupt CV Attn | scalar | 0 | 1000 | 1000 | percent x10 | 0..1 |
| 15 | `kParamMode` | Mode | enum | 0 | 1 | 0 | enum | 0 macro, 1 micro |
| 16 | `kParamBreakMicroMode` | Break Micro Mode | enum | 0 | 1 | 0 | enum | 0 traverse, 1 silence |
| 17 | `kParamBendEnabled` | Bend Enabled | enum | 0 | 1 | 0 | bool | persistent |
| 18 | `kParamBreakEnabled` | Break Enabled | enum | 0 | 1 | 0 | bool | persistent |
| 19 | `kParamFreezeEnabled` | Freeze Enabled | enum | 0 | 1 | 0 | bool | persistent |
| 20 | `kParamClockSource` | Clock Source | enum | 0 | 1 | 0 | enum | 0 internal, 1 external |
| 21 | `kParamStereoMode` | Stereo Mode | enum | 0 | 1 | 0 | enum | 0 shared, 1 unique |
| 22 | `kParamGateMode` | Gate Mode | enum | 0 | 1 | 0 | enum | 0 latching, 1 momentary |
| 23 | `kParamFreezeGateMode` | Freeze Gate Mode | enum | 0 | 1 | 0 | enum | 0 latching, 1 momentary |
| 24 | `kParamCorruptGateMode` | Corrupt Gate Mode | enum | 0 | 1 | 0 | enum | 0 toggle, 1 clock reset |
| 25 | `kParamCorruptBank` | Corrupt Bank | enum | 0 | 1 | 0 | enum | 0 legacy, 1 expanded |
| 26 | `kParamCorruptAlgorithm` | Corrupt Algorithm | enum | 0 | 4 | 0 | enum | see section 3 |
| 27 | `kParamGlitchWindow` | Glitch Window | scalar | 0 | 1000 | 20 | percent x10 | default 2 percent |
| 28 | `kParamTimeCvInput` | Time CV In | CV bus | 0 | `kNT_lastBus` | 0 | CV input | 0 means unpatched |
| 29 | `kParamRepeatsCvInput` | Repeats CV In | CV bus | 0 | `kNT_lastBus` | 0 | CV input | 0 means unpatched |
| 30 | `kParamMixCvInput` | Mix CV In | CV bus | 0 | `kNT_lastBus` | 0 | CV input | 0 means unpatched |
| 31 | `kParamBendCvInput` | Bend CV In | CV bus | 0 | `kNT_lastBus` | 0 | CV input | 0 means unpatched |
| 32 | `kParamBreakCvInput` | Break CV In | CV bus | 0 | `kNT_lastBus` | 0 | CV input | 0 means unpatched |
| 33 | `kParamCorruptCvInput` | Corrupt CV In | CV bus | 0 | `kNT_lastBus` | 0 | CV input | 0 means unpatched |
| 34 | `kParamBendGateInput` | Bend Gate In | CV bus | 0 | `kNT_lastBus` | 0 | CV input | gate threshold in DSP |
| 35 | `kParamBreakGateInput` | Break Gate In | CV bus | 0 | `kNT_lastBus` | 0 | CV input | gate threshold in DSP |
| 36 | `kParamCorruptGateInput` | Corrupt Gate In | CV bus | 0 | `kNT_lastBus` | 0 | CV input | gate threshold in DSP |
| 37 | `kParamFreezeGateInput` | Freeze Gate In | CV bus | 0 | `kNT_lastBus` | 0 | CV input | gate threshold in DSP |
| 38 | `kParamClockGateInput` | Clock In | CV bus | 0 | `kNT_lastBus` | 0 | CV input | gate threshold in DSP |
| 39 | `kParamRandomSeedMode` | Random Seed Mode | enum | 0 | 1 | 0 | enum | 0 session random, 1 fixed |
| 40 | `kParamFixedSeed` | Fixed Seed | scalar | 0 | 65535 | 1 | integer | used when seed mode fixed |
| 41 | `kParamBufferSeconds` | Buffer Seconds | scalar | 5 | 60 | 60 | seconds | may be reduced for memory |
| 42 | `kParamResetEngine` | Reset Engine | confirm | 0 | 1 | 0 | confirm | host calls `Engine::reset()` |
| 43 | `kParamRestoreDefaults` | Restore Defaults | confirm | 0 | 1 | 0 | confirm | resets persistent state |

Total plug-in parameters: 44

## 2. Parameter Pages

Page layout targets a fast performance flow and strict routing separation.

| Page Index | Page Name | Group | Parameters |
| --- | --- | --- | --- |
| 0 | Main | 1 | 6, 7, 8, 9, 10, 11 |
| 1 | Modes | 2 | 15, 16, 17, 18, 19, 20, 21, 22, 23, 24 |
| 2 | Corrupt | 3 | 25, 26, 11, 14, 27 |
| 3 | Routing Audio | 4 | 0, 1, 2, 3, 4, 5 |
| 4 | Routing CV | 4 | 28, 29, 30, 31, 32, 33, 12, 13, 14 |
| 5 | Routing Gates | 4 | 34, 35, 36, 37, 38 |
| 6 | Advanced | 5 | 39, 40, 41, 42, 43 |

Group behavior:
- pages in group `4` preserve cursor position when switching between routing pages.
- other groups are independent.

## 3. Enum Strings

`kParamMode`:
- `Macro`
- `Micro`

`kParamBreakMicroMode`:
- `Traverse`
- `Silence`

`kParamClockSource`:
- `Internal`
- `External`

`kParamStereoMode`:
- `Shared`
- `Unique`

`kParamGateMode`:
- `Latching`
- `Momentary`

`kParamFreezeGateMode`:
- `Latching`
- `Momentary`

`kParamCorruptGateMode`:
- `Toggle`
- `Clock Reset`

`kParamCorruptBank`:
- `Legacy`
- `Expanded`

`kParamCorruptAlgorithm`:
- `Decimate`
- `Dropout`
- `Destroy`
- `DJ Filter`
- `Vinyl Sim`

`kParamRandomSeedMode`:
- `Session`
- `Fixed`

## 4. Wrapper Rules

- Convert all normalized controls from `0..1000` to `0.0f..1.0f` before calling DSP.
- CV bus params are sampled per frame. If bus select is `0`, pass null pointer for that CV/gate input.
- Gate inputs are regular CV busses interpreted by DSP at threshold `0.4V`.
- Construct `EngineConfig` with:
  - `sample_rate_hz = NT_globals.sampleRate`
  - `max_supported_sample_rate_hz = 96000.0f` (or current platform max)
  - `max_block_frames = NT_globals.maxFramesPerStep`
- Call `engine.set_audio_context(NT_globals.sampleRate, NT_globals.maxFramesPerStep)` at least once per `step()` before `process()`.
- `kParamResetEngine` and `kParamRestoreDefaults` use confirm semantics and return to `0`.
- Keep this mapping stable after initial release; only append new IDs.
