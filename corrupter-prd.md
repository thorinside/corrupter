

**PRODUCT REQUIREMENTS DOCUMENT**

**Databender Clone**

*Circuit-Bent Digital Audio Buffer — Eurorack Module*

Version 1.0  |  February 2026

Reference Hardware: Qu-Bit Electronix Data Bender (firmware v1.4.5)

# **Table of Contents**

# **1\. Executive Summary**

This PRD specifies all functional, electrical, and user-experience requirements for an open-source Eurorack module that replicates the behavior of the Qu-Bit Electronix Data Bender. The Data Bender is a stereo circuit-bent digital audio buffer that emulates the sonic artifacts of failing playback media: skipping CDs, warped tapes, corrupted digital streams, and scratched vinyl. The clone targets feature-parity with firmware v1.4.5, delivering identical I/O topology, DSP behavior, and panel workflow in a 14HP form factor.

# **2\. Product Overview**

## **2.1 Core Concept**

The module continuously records stereo audio into a circular buffer (96 kHz / 24-bit, capable of storing over 60 seconds of stereo audio). Three orthogonal destruction axes—Bend, Break, and Corrupt—manipulate playback from that buffer in real time. An internal or external clock governs the rate at which new audio segments are captured and processed. Two operational modes (Macro and Micro) provide different levels of automation versus manual control over the Bend and Break parameters.

## **2.2 Design Philosophy**

The product embraces controlled failure as a creative tool. Every parameter is designed to model a specific real-world audio malfunction: tape warble, CD skip, buffer underrun, bitcrushing from wrong sample-rate interpretation, signal dropout, and vinyl degradation. Randomness is clock-synchronized so that destruction remains musically useful.

## **2.3 Physical Specifications**

| Parameter | Value |
| :---- | :---- |
| Width | 14 HP (70.8 mm) |
| Depth | 28 mm maximum (including power header) |
| Power \+12 V | 58 mA |
| Power ‒12 V | 60 mA |
| Power \+5 V | 0 mA (not used) |
| Audio Sample Rate | 96 kHz |
| Audio Bit Depth | 24-bit |
| Buffer Capacity | \>60 seconds stereo at 96 kHz / 24-bit |
| Audio I/O Level | Eurorack modular level (\~10 Vpp nominal, up to \~14 Vpp output with Corrupt) |
| CV Input Range | −5 V to \+5 V (all CV inputs) |
| Gate Threshold | 0.4 V (all gate inputs) |
| Power Connector | Eurorack 16-pin or 10-pin keyed IDC (red stripe \= ‒12 V) |

# **3\. Input / Output Specification**

## **3.1 Audio I/O**

| Jack | Direction | Description |
| :---- | :---- | :---- |
| IN L | Input | Left stereo audio input. Normalled to IN R when IN R is unpatched. |
| IN R | Input | Right stereo audio input. Receives normalled copy of IN L if unpatched. |
| OUT L | Output | Left stereo audio output. |
| OUT R | Output | Right stereo audio output. |

## **3.2 CV Inputs**

All CV inputs accept −5 V to \+5 V and are additive with the corresponding knob position.

| CV Jack | Controls |
| :---- | :---- |
| Time CV | Buffer acquisition period (internal mode) or clock division/multiplication (external mode). |
| Repeats CV | Number of buffer subdivisions. |
| Mix CV | Dry/wet balance between live input and processed buffer. |
| Bend CV | Macro: probability/intensity of tape-style manipulations. Micro: playback speed (±3 octaves, tracks 1V/oct). |
| Break CV | Macro: probability/intensity of CD-skip style glitches. Micro: buffer traverse position or silence duty cycle. |
| Corrupt CV | Intensity of the currently selected Corrupt algorithm. |

## **3.3 Gate Inputs**

All gate inputs trigger at a 0.4 V threshold. Configurable globally as latching or momentary via Shift+Clock.

| Gate Jack | Function |
| :---- | :---- |
| Bend Gate | Toggles Bend on/off (latching) or holds Bend active (momentary). |
| Break Gate | Toggles Break on/off (latching) or holds Break active (momentary). |
| Corrupt Gate | Default: toggles Corrupt on/off. Alternate (Shift+Corrupt): acts as clock reset/sync input. |
| Freeze Gate | Engages/disengages buffer freeze (latching syncs to next clock beat; momentary is instant). |
| Clock Input | External clock source. Module auto-detects clock presence (times out after 4 missed beats). |

# **4\. Panel Controls**

## **4.1 Potentiometers (6)**

| Knob | Primary Function | Shift Function |
| :---- | :---- | :---- |
| Time | Internal: buffer period (16 s → 80 Hz). External: clock div/mult. | Glitch Windowing amount (hard edges → fully windowed). |
| Repeats | Buffer subdivisions (1 \= no repeat, increasing \= more subdivisions). | None. |
| Mix | Dry/wet crossfade (full dry → full wet). | None. |
| Bend | Macro: probability envelope. Micro: playback speed ±3 oct. | Bend CV attenuator (Macro mode only). |
| Break | Macro: probability envelope. Micro: traverse position or silence duty cycle. | Break CV attenuator (both modes). |
| Corrupt | Intensity of active Corrupt algorithm. | Corrupt CV attenuator (both modes). |

## **4.2 Buttons (6)**

| Button | Primary Function | Shift Function |
| :---- | :---- | :---- |
| Bend | Toggle Bend on/off. | Toggle stereo behavior (Unique / Shared). |
| Break | Macro: toggle Break on/off. Micro: toggle Traverse / Silence sub-mode. | Restore all settings to defaults. |
| Corrupt | Cycle through Corrupt algorithms. | Toggle Corrupt jack as clock reset input. |
| Freeze | Engage/disengage buffer freeze. | Toggle Freeze button latching / momentary behavior. |
| Clock | Toggle Internal / External clock mode. | Toggle all gate inputs latching / momentary. |
| Mode | Toggle Macro (Blue) / Micro (Green) mode. | Toggle Corrupt algorithm set (legacy / expanded). |

## **4.3 Shift Button**

A dedicated Shift button (illuminates blue when held) provides access to all secondary functions described above. Settings are persisted to non-volatile storage whenever Shift is released, throttled to once every two seconds.

# **5\. LED Feedback Specification**

Each button has an associated multicolor LED. The following table defines the complete LED state machine.

| LED | Color | Condition | Meaning |
| :---- | :---- | :---- | :---- |
| Clock | Blue (blink) | Internal clock | Blinks at internal clock rate. |
| Clock | White (blink) | External clock | Blinks at effective external clock rate (post div/mult). |
| Clock | Dim white | External, no signal | No clock pulses received for 4+ beats. |
| Clock | Gold (flash) | Div/mult change | Brief flash when Time knob crosses a div/mult boundary. |
| Mode | Blue | Macro mode | Macro mode active. |
| Mode | Green | Micro mode | Micro mode active. |
| Bend | Blue | Micro, forward | Forward playback (non-octave speed). |
| Bend | Cyan | Micro, forward octave | Forward playback at exact octave multiple. |
| Bend | Green | Micro, reverse | Reverse playback (non-octave speed). |
| Bend | Gold | Micro, reverse octave | Reverse playback at exact octave multiple. |
| Break | Off | Micro, Traverse | Break controls subsection selection. |
| Break | On | Micro, Silence | Break controls silence duty cycle. |
| Break | Gold (blip) | Subsection change | Flash when playback moves to a different subsection. |
| Shift | Blue | Held | Shift is active. |
| Shift | Off→White | Shift+Time | Brightness indicates current glitch window amount. |

# **6\. DSP Architecture**

## **6.1 Audio Buffer**

The core data structure is a stereo circular buffer operating at 96 kHz / 24-bit. The buffer must hold at least 60 seconds of stereo audio, requiring approximately 96000 samples/sec × 2 channels × 3 bytes × 60 sec \= \~34.56 MB of sample RAM. Audio outside the active Time window is continuously written in the background so that adjusting Time to a longer window reveals recently-heard audio up to \~60 seconds in the past.

## **6.2 Clock System**

The clock system governs when new audio segments are captured from the circular buffer for processing. In internal mode, the Time knob smoothly sweeps the clock period from 16 seconds (fully CCW) to \~12.5 ms / 80 Hz (fully CW). In external mode, the module phase-locks to incoming clock pulses and the Time knob selects a division or multiplication of that clock according to the following table:

| Knob Position (CCW → CW) | Clock Relationship |
| :---- | :---- |
| Position 1 | ÷16 (4 bars) |
| Position 2 | ÷8 (2 bars) |
| Position 3 | ÷4 (1 bar) |
| Position 4 | ÷2 |
| Position 5 (center) | ×1 (matches input) |
| Position 6 | ×2 (eighth notes) |
| Position 7 | ×3 (eighth note triplets) |
| Position 8 | ×4 (sixteenth notes) |
| Position 9 | ×8 (thirty-second notes) |

The clock system must auto-detect loss of external clock (4 missed beats at the last known rate) and continue running at the last-known tempo until a new pulse arrives.

## **6.3 Repeats Engine**

The Repeats parameter subdivides the current buffer segment into N equal-length subsections. At minimum (knob fully CCW), N=1 and the buffer plays as a single contiguous segment. As the knob increases, N increases, producing faster repetition of smaller buffer chunks. The Break parameter and Macro-mode randomization select which of the N subsections actually plays back.

## **6.4 Glitch Windowing**

Accessible via Shift+Time, glitch windowing applies an amplitude envelope to each repeated subsection. At minimum (default \~2%), transitions between subsections have hard edges producing audible clicks—desirable for percussive glitch work. At maximum, each subsection fades in and out smoothly, suitable for ambient textures. The windowing amount is stored in non-volatile memory.

# **7\. Bend — Tape Medium Emulation**

## **7.1 Macro Mode**

In Macro mode, Bend is a probabilistic engine synchronized to the clock. Each clock tick, the module rolls against the Bend knob/CV value to determine whether a tape-style manipulation occurs. The knob position controls both the probability and the intensity of the effect. At minimum settings, only occasional playback reversal at normal speed occurs. At maximum, the engine introduces varispeed pitch changes across a wide range, forward and reverse playback, slewed speed transitions (tape-stop/start effects), and vinyl-style pops and clicks. The randomization is per-clock-tick, giving rhythmically coherent results.

## **7.2 Micro Mode**

In Micro mode, the Bend knob/CV directly controls playback speed across a ±3 octave range (6 octaves total). The center position is unity speed. The control tracks 1V/oct from CV. Pressing the Bend button toggles between forward and reverse playback. LED color indicates direction and whether the current speed lands on an exact octave multiple.

# **8\. Break — Digital Failure Emulation**

## **8.1 Macro Mode**

In Macro mode, Break is a probabilistic engine synchronized to the clock, analogous to Bend. Each clock tick, the module may jump the playback head to a random subsection of the buffer, increase the effective Repeats value, or insert silence into the playback stream. At minimum settings, occasional subtle repeat-count changes or position jumps occur. At maximum, playback becomes chaotic: the head jumps freely between any subsection, the repeat count can spike to extreme values, and up to 90% of each repeat can be replaced with silence.

## **8.2 Micro Mode**

In Micro mode, Break offers two sub-modes toggled by the Break button:

* **Traverse:** The knob/CV selects which subsection of the buffer (as defined by Repeats) is currently playing. Far left \= first subsection, far right \= last. If Repeats is 1, Traverse has no effect.

* **Silence:** The knob/CV sets a duty-cycle ratio for silence injection. Far left \= no silence, far right \= 90% silence. The silence is applied per-subsection, creating rhythmic gaps.

# **9\. Corrupt — Signal Degradation Algorithms**

Corrupt is an end-of-chain effect applied after Bend and Break processing. The active algorithm is selected by pressing the Corrupt button (cycles through modes). The knob/CV controls intensity. Firmware v1.4.5 includes the following algorithms across two banks toggled via Shift+Mode:

## **9.1 Legacy Bank**

### **Decimate**

Combines variable bit-depth reduction and sample-rate downsampling. At low settings, subtle quantization noise. At high settings, extreme aliasing and bit-crushing artifacts reminiscent of playing audio at the wrong sample rate or bit depth.

### **Dropout**

Introduces random audio dropouts (signal goes to zero). The knob position controls the tradeoff between dropout count and duration: left side \= fewer but longer dropouts, right side \= more frequent but shorter dropouts.

### **Destroy**

Two-stage saturation/distortion. The first half of the knob range applies soft saturation (warm overdrive). The second half introduces hard clipping that escalates to extreme levels. Warning: output level can significantly exceed input level at high settings, especially with line-level sources.

## **9.2 Expanded Bank (v1.4.5+)**

### **DJ Filter**

A resonant filter that sweeps from low-pass (CCW) through bypass (center) to high-pass (CW). Designed to emulate DJ mixer filter sweeps.

### **Vinyl Sim**

A composite vinyl simulation effect that introduces surface noise (pops, crackle), frequency-dependent filtering, and tonal coloring to emulate playback from a worn vinyl record.

# **10\. Freeze**

When Freeze is engaged, the circular buffer stops recording new audio. The current buffer contents are locked and all Bend/Break/Corrupt manipulations operate non-destructively on the frozen data. This allows extensive mangling without losing source material.

Key behaviors:

* In latching mode (default), freeze activates on the next clock beat to maintain sync.

* In momentary mode (Shift+Freeze toggle), freeze engages/disengages instantly on button press/release.

* If Mix is fully dry when Freeze is engaged, Mix automatically jumps to fully wet so the frozen buffer is audible. This enables a performance workflow: pass audio through dry, then hit Freeze to instantly hear the processed buffer.

* Extending the Time knob below its value when Freeze was engaged will reveal old buffer data, producing discontinuities and fragments of sonic history.

# **11\. Stereo Processing**

The module provides two stereo processing modes, toggled via Shift+Bend:

* **Shared Mode (Green, default):** Macro-mode randomization produces identical Bend/Break parameters for both stereo channels. The stereo image is preserved.

* **Unique Mode (Blue):** Each stereo channel receives independent random Bend/Break parameters per clock tick. This produces a wide, decorrelated stereo field with distinct glitch patterns in each ear.

In Micro mode, both channels always share the same manual Bend/Break settings regardless of stereo behavior selection.

# **12\. Non-Volatile Settings Persistence**

The following settings are stored to non-volatile memory whenever the Shift button is released (rate-limited to once every 2 seconds) and restored on power-up:

* Bend state (on/off)

* Break state (on/off)

* Corrupt algorithm selection

* Clock source (internal/external)

* Processor mode (Macro/Micro)

* Stereo mode (Unique/Shared)

* Glitch windowing amount

* Gate behavior (latching/momentary)

* Freeze button behavior (latching/momentary)

* Corrupt-as-reset behavior

## **12.1 Restore Defaults**

Shift+Break restores all persistent settings to factory defaults: Bend off, Break off, Freeze off, Macro mode, Shared stereo, latching gates, latching freeze, default windowing (2%), Corrupt jack in normal mode.

# **13\. Hardware Architecture Requirements**

## **13.1 Processor**

The DSP workload (96 kHz stereo processing with multiple real-time effects, buffer management, and clock tracking) requires a microcontroller or DSP with sufficient MIPS and memory. Recommended candidates include ARM Cortex-M7 (e.g., STM32H7 series) or a dedicated DSP (e.g., SHARC). The processor must support 24-bit audio natively or via efficient 32-bit integer/float operations.

## **13.2 Memory**

A minimum of 36 MB of sample RAM is required for the 60+ second stereo buffer at 96 kHz / 24-bit. This exceeds typical MCU internal SRAM, so external SDRAM or PSRAM is required. Additionally, \~256 KB of flash or EEPROM is needed for firmware and settings persistence.

## **13.3 Audio Codec**

A stereo audio codec supporting 96 kHz / 24-bit operation is required, such as the Cirrus Logic CS4272 or PCM1808/PCM5102 combination. The codec must support simultaneous stereo input and output (full-duplex).

## **13.4 Analog Front-End**

CV and gate inputs require op-amp conditioning circuits to scale the ±5 V Eurorack range to the ADC input range. Gate inputs need Schmitt-trigger conditioning with a 0.4 V threshold. Audio I/O requires AC-coupling and level-matching between Eurorack levels (\~10 Vpp) and codec levels. The left audio input must include a normalling circuit (switched jack) to copy its signal to the right channel when no cable is inserted in the right input.

## **13.5 User Interface**

Six potentiometers (linear taper recommended for all except possibly Mix), six tactile buttons with integrated or adjacent RGB/multicolor LEDs capable of displaying at minimum: blue, green, cyan, gold/amber, white, and variable brightness white. One additional Shift button with a single-color (blue) LED.

# **14\. Firmware Architecture Requirements**

## **14.1 Real-Time Audio Pipeline**

The firmware must maintain a sample-accurate stereo audio pipeline at 96 kHz with zero buffer underruns. The audio processing chain is: Input → Circular Buffer Write → Clock-triggered Segment Capture → Repeats Subdivision → Bend Processing → Break Processing → Glitch Windowing → Corrupt Processing → Mix Crossfade → Output. All DSP must complete within the audio interrupt deadline (\~10.4 μs per sample at 96 kHz).

## **14.2 Clock Management**

The clock subsystem must handle smooth internal clock sweeps, external clock phase-lock with division/multiplication, automatic fallback on clock loss, and reset/resync via the Corrupt gate (when configured). Transitions between internal and external mode must be glitch-free.

## **14.3 Randomization Engine**

Macro mode requires a pseudo-random number generator seeded per-clock-tick for both Bend and Break probability rolls. The PRNG must produce musically useful distributions—not uniform random—weighted by knob position to produce natural-sounding progressive intensity curves.

## **14.4 Control Rate**

Knob positions and CV values should be sampled at a control rate of at least 1 kHz to ensure responsive modulation without zipper noise. Smoothing/slew-limiting should be applied to prevent audible stepping artifacts on time-critical parameters (especially playback speed in Micro mode).

# **15\. Acceptance Criteria**

The following criteria define a complete and correct implementation:

1. All six CV inputs respond to −5 V to \+5 V with additive behavior relative to knob position.

2. All five gate inputs trigger reliably at 0.4 V threshold in both latching and momentary configurations.

3. Stereo audio passes through at 96 kHz / 24-bit with no audible degradation when all effects are bypassed (Mix fully dry).

4. Buffer stores and recalls at least 60 seconds of stereo audio.

5. External clock tracking is stable across tempos from 20 BPM to 300 BPM with all nine division/multiplication ratios.

6. Macro mode Bend and Break produce audibly distinct behaviors across the full knob range with clock-synchronized timing.

7. Micro mode Bend tracks 1V/oct for playback speed across the ±3 octave range.

8. All five Corrupt algorithms produce distinct, musically usable effects across their full range.

9. Freeze locks the buffer with zero audible glitches on engage/disengage (in latching mode, synced to clock).

10. All persistent settings survive power cycle and Restore Defaults correctly resets all to documented values.

11. All LED states match the specification in Section 5\.

12. Module fits within 14 HP / 28 mm depth and draws ≤58 mA on \+12 V and ≤60 mA on ‒12 V.

# **16\. DSP Implementation Notes**

This section provides algorithmic guidance for implementers. These are not strict requirements but represent best-practice approaches for achieving sonic fidelity to the reference hardware.

## **16.1 Playback Speed (Micro Bend)**

Variable-speed playback should use high-quality interpolation (at minimum linear, preferably cubic or sinc) to avoid aliasing when pitching down and to maintain audio quality across the ±3 octave range. The playback pointer is a fractional-sample index that advances by a variable increment each sample period. For 1V/oct tracking, the increment should follow: rate \= 2^(voltage) where 0V \= unity speed.

## **16.2 Bit-Crush (Decimate)**

Bit-depth reduction: requantize the 24-bit sample to N bits by masking or rounding to the nearest step of size (2^24 / 2^N). Sample-rate reduction: use a sample-and-hold approach, outputting the same sample value for (96000/target\_rate) consecutive output samples. Both parameters should sweep smoothly across the knob range.

## **16.3 Destroy Saturation**

The first half of the knob should apply a soft-clipping function such as tanh(x \* gain) or a polynomial waveshaper. The second half transitions to hard clipping (simple min/max clamping) with progressively increasing gain. The gain curve should be exponential to give usable range across the full knob sweep.

## **16.4 Dropout Generation**

Dropouts should be generated using a random process that determines both onset time and duration. The knob position should control a tradeoff parameter: at low settings, generate events with low probability but high duration; at high settings, high probability but low duration. Zero-crossings or windowed muting should be used to avoid clicks at dropout boundaries.

## **16.5 DJ Filter**

Implement as a state-variable filter or biquad with resonance. The knob position controls cutoff frequency: CCW sweeps the low-pass cutoff down from 20 kHz, center \= bypass (full bandwidth), CW sweeps a high-pass cutoff up from 20 Hz. Resonance should be moderate and fixed (or very gently knob-scaled) to avoid self-oscillation.

## **16.6 Mix Crossfade**

The Mix control should implement an equal-power crossfade (using sine/cosine curves or square-root scaling) to avoid a perceived volume dip at the 50% position.

# **17\. Signal Flow Summary**

The complete signal flow through the module is as follows:

**STEREO INPUT L/R**

    ↓

Circular Buffer (continuous write, 96 kHz / 24-bit, 60+ sec)

    ↓

**Clock-Triggered Segment Capture** (Time knob governs period)

    ↓

**Repeats Subdivision** (divide segment into N equal parts)

    ↓

**Bend Processing** (playback speed, direction, tape effects)

    ↓

**Break Processing** (position jumps, silence injection, stutter)

    ↓

**Glitch Windowing** (amplitude envelope per subsection)

    ↓

**Corrupt Processing** (decimate / dropout / destroy / DJ filter / vinyl sim)

    ↓

**Mix Crossfade** (dry input ↔ processed buffer)

    ↓

**STEREO OUTPUT L/R**

The Freeze function, when engaged, halts the circular buffer write stage. All downstream processing continues to operate on the frozen buffer contents non-destructively.

# **18\. Open Questions and Future Scope**

* Exact PRNG algorithm and probability distribution curves used in Macro mode are not publicly documented. Perceptual matching against reference hardware is the target, not mathematical equivalence.

* The vinyl simulation algorithm details (noise generation, filtering characteristics) are not publicly specified. Iterative tuning against the reference module is recommended.

* Firmware update mechanism (USB, SPI flash, etc.) is left to the hardware implementation team.

* Panel artwork, legend layout, and jack/knob placement should closely follow the reference module for user familiarity, within the constraints of the chosen components.

* Potential future additions: MIDI clock input, expanded Corrupt bank, CV-selectable Corrupt mode, USB audio interface functionality.