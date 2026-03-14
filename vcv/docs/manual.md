# Corrupter User Manual

**No Such Device** | Author: Thorinside | 18 HP

Corrupter is a Data Bender-style circuit-bent stereo buffer effect for VCV Rack. It captures incoming audio into an internal buffer (up to 30 seconds), then mangles it with pitch bending, rhythmic breaks, and a selection of corruption algorithms. The result ranges from subtle tape-warble textures to full-on digital destruction.

---

## Table of Contents

1. [Quick Start](#quick-start)
2. [Signal Flow](#signal-flow)
3. [Panel Layout](#panel-layout)
4. [Controls Reference](#controls-reference)
   - [Audio I/O](#audio-io)
   - [Row 1: Time / Repeats / Mix](#row-1-time--repeats--mix)
   - [Row 2: Bend / Break / Corrupt](#row-2-bend--break--corrupt)
   - [CV Inputs and Attenuators](#cv-inputs-and-attenuators)
   - [Display](#display)
   - [Buttons](#buttons)
   - [Gate Inputs](#gate-inputs)
   - [Bottom Section](#bottom-section)
5. [Corruption Algorithms](#corruption-algorithms)
6. [Right-Click Menu](#right-click-menu)
7. [Tips and Techniques](#tips-and-techniques)

---

## Quick Start

1. Patch a stereo or mono audio source into **IN L** (and optionally **IN R**).
2. Connect **OUT L** and **OUT R** to your mixer or audio output.
3. Set **MIX** to 100% (fully clockwise -- this is the default).
4. Set **TIME** and **REPEATS** to taste.
5. Turn up the **BEND**, **BREAK**, or **CORRUPT** knobs.
6. Press the corresponding **BEND**, **BREAK**, or **FREEZE** buttons to activate each effect. Their status indicators appear on the display.
7. Press **ALGO** to cycle through the five corruption algorithms.

You should now hear the buffer being processed. From here, experiment with the knobs, gate inputs, and right-click menu options.

---

## Signal Flow

```
Audio In (L/R) --> Internal Buffer (up to 30s) --> DSP Engine --> Audio Out (L/R)
                                                      ^
                                          Bend / Break / Corrupt
                                          CV / Gates / Clock
```

- Stereo input: If only **IN L** is connected, it is normalled (copied) to **IN R**, producing a dual-mono signal.
- The DSP engine processes audio in blocks of 256 frames internally.
- The buffer length is controlled by the **TIME** knob and can be externally clocked via **CLK IN**.

---

## Panel Layout

From top to bottom, the module is organized as follows:

```
+------------------------------------------+
|              CORRUPTER                    |
|  [IN L] [IN R]          [OUT L] [OUT R]  |
|                                          |
|      (TIME)    (REPEATS)    (MIX)        |  <- Row 1: Big knobs
|      [CV]       [CV]        [CV]         |  <- CV inputs
|                                          |
|      (BEND)    (BREAK)    (CORRUPT)      |  <- Row 2: Big knobs
|    [CV](att)  [CV](att)  [CV](att)       |  <- CV inputs + attenuators
|                                          |
|  [=========== DISPLAY ==============]    |  <- Waveform + status
|                                          |
|   [BEND]  [BREAK] [FREEZE]  [ALGO]      |  <- Buttons
|   [GATE]  [GATE]  [GATE]                |  <- Gate inputs
|                                          |
|  (GW) [CLK IN] [MODE] [SLNC] [STER]     |  <- Bottom section
|                                          |
|           NO SUCH DEVICE                 |
+------------------------------------------+
```

---

## Controls Reference

### Audio I/O

| Port | Type | Description |
|------|------|-------------|
| **IN L** | Input | Left audio input. |
| **IN R** | Input | Right audio input. If unpatched, receives a copy of IN L. |
| **OUT L** | Output | Left audio output (processed signal). |
| **OUT R** | Output | Right audio output (processed signal). |

All audio ports expect standard VCV Rack levels (+/- 5V).

---

### Row 1: Time / Repeats / Mix

These three large knobs control the fundamental delay-buffer behavior.

| Knob | Range | Default | Description |
|------|-------|---------|-------------|
| **TIME** | 0--100% | 50% | Controls the delay/buffer time. Lower values produce shorter, tighter loops; higher values create longer, more spacious delays. |
| **REPEATS** | 0--100% | 50% | Controls the feedback/repeat amount. At 0% you hear a single pass through the buffer. Higher values feed the output back into the buffer for cascading repetitions. |
| **MIX** | 0--100% | 100% | Dry/wet balance. At 0% you hear only the dry input signal. At 100% you hear only the wet (processed) signal. |

Each knob has a corresponding **CV input** directly below it (see [CV Inputs](#cv-inputs-and-attenuators)).

---

### Row 2: Bend / Break / Corrupt

These three large knobs control the amount of each glitch effect. All default to 0% -- you must turn them up *and* enable the corresponding button for the effect to be heard.

| Knob | Range | Default | Description |
|------|-------|---------|-------------|
| **BEND** | 0--100% | 0% | Controls the pitch bend amount. When the Bend effect is enabled (via button or gate), this determines how far the pitch shifts. Use the right-click menu to quantize pitch bends to musical scales. |
| **BREAK** | 0--100% | 0% | Controls the break/glitch intensity. When enabled, this introduces rhythmic stutters, repeats, and buffer-position jumps. |
| **CORRUPT** | 0--100% | 0% | Controls the intensity of the currently selected corruption algorithm (see [Corruption Algorithms](#corruption-algorithms)). |

Each knob has a corresponding **CV input** with an **attenuator trimpot** (see below).

---

### CV Inputs and Attenuators

#### Row 1 CV (below TIME / REPEATS / MIX)

Three CV input jacks, one below each Row 1 knob. These accept standard voltage and add to the knob position.

| Input | Modulates |
|-------|-----------|
| **TIME CV** | Time knob |
| **REPEATS CV** | Repeats knob |
| **MIX CV** | Mix knob |

#### Row 2 CV (below BEND / BREAK / CORRUPT)

Three CV input jacks with paired **attenuator trimpots**. Each trimpot scales the incoming CV from 0% to 100% before it is added to the knob value. The attenuators default to 100% (fully open).

| Input | Attenuator | Modulates |
|-------|------------|-----------|
| **BEND CV** | Bend CV Attn | Bend knob |
| **BREAK CV** | Break CV Attn | Break knob |
| **CORRUPT CV** | Corrupt CV Attn | Corrupt knob |

---

### Display

The LED display sits between the knob section and the button section. It shows:

- **Waveform visualization**: A real-time view of the output buffer amplitude, rendered as vertical bars. A white cursor marks the current write position.
- **Status indicators** (bottom row of the display):
  - **BND** -- appears in green when the Bend effect is active.
  - **BRK** -- appears in orange when the Break effect is active.
  - **FRZ** -- appears in blue when the Freeze function is active.
  - **Algorithm name** -- the currently selected corruption algorithm (e.g., "DECIMATE", "VINYL SIM").

---

### Buttons

Four buttons are arranged in a row below the display.

| Button | Function |
|--------|----------|
| **BEND** | Toggles the Bend effect on/off. When active, the BEND knob amount is applied to pitch-shift the buffer playback. Indicated by "BND" on the display. |
| **BREAK** | Toggles the Break effect on/off. When active, the BREAK knob amount controls rhythmic glitch intensity. Indicated by "BRK" on the display. |
| **FREEZE** | Toggles buffer freeze on/off. When active, the buffer stops recording new input and loops its current contents. Indicated by "FRZ" on the display. |
| **ALGO** | Cycles through the five corruption algorithms. Each press advances to the next algorithm. The current algorithm name is shown on the display. The algorithm bank (Legacy or Expanded) is selected automatically based on the chosen algorithm. |

---

### Gate Inputs

Three gate input jacks sit below the **BEND**, **BREAK**, and **FREEZE** buttons. These allow external control of each effect's on/off state.

| Input | Controls |
|-------|----------|
| **BEND GATE** | Bend effect enable |
| **BREAK GATE** | Break effect enable |
| **FREEZE GATE** | Freeze enable |

Gate behavior depends on the **Gate Mode** setting (see [Right-Click Menu](#right-click-menu)):

- **Latching** (default): A rising edge toggles the effect on or off. Send triggers from a sequencer to toggle effects at specific steps.
- **Momentary**: The effect is active only while the gate is high. Release the gate and the effect turns off.

---

### Bottom Section

The bottom row contains utility controls.

| Control | Type | Range/Default | Description |
|---------|------|---------------|-------------|
| **GW** (Glitch Window) | Trimpot | 0--100%, default 2% | Controls the size of the glitch window. Smaller values produce tighter, more precise glitches; larger values create longer, more dramatic disruptions. |
| **CLK IN** | Input jack | -- | External clock input. When a cable is connected, the module automatically switches from its internal clock to the external clock. The buffer timing locks to the incoming clock pulses, allowing tempo-synced effects. Disconnect the cable to return to internal clock mode. |
| **MODE** | Button | -- | Toggles between Macro and Micro mode. A green LED indicates Macro mode; a red LED indicates Micro mode. This changes the character and scale of the break and bend effects. |
| **SLNC** | Button | -- | Toggles Break Silence mode. When enabled (green LED), breaks introduce silence rather than repeated/stuttered audio. |
| **STER** | Button | -- | Toggles Unique Stereo mode. When enabled (green LED), the left and right channels are processed independently, creating wider stereo glitch effects. When disabled, both channels receive the same processing. |

---

## Corruption Algorithms

Press the **ALGO** button to cycle through the five available algorithms. The **CORRUPT** knob controls the intensity of whichever algorithm is active.

### Legacy Bank (Algorithms 0--2)

| Algorithm | Description |
|-----------|-------------|
| **DECIMATE** | Sample rate and bit depth reduction. Low settings add subtle grit; high settings produce harsh, aliased digital crunch. |
| **DROPOUT** | Randomly drops audio segments, creating gaps and stutters in the signal. Higher values produce more frequent and longer dropouts. |
| **DESTROY** | Aggressive digital distortion and buffer mangling. The most extreme of the legacy algorithms. |

### Expanded Bank (Algorithms 3--4)

| Algorithm | Description |
|-----------|-------------|
| **DJ FILTER** | A resonant filter effect that sweeps through the frequency spectrum. Lower CORRUPT values apply subtle filtering; higher values produce dramatic sweeps. |
| **VINYL SIM** | Simulates vinyl record artifacts -- crackle, wow, flutter, and surface noise. Higher values increase the intensity of the vinyl degradation. |

The algorithm bank (Legacy or Expanded) is selected automatically when you cycle through algorithms -- you do not need to select it manually.

---

## Right-Click Menu

Right-click the module panel to access additional settings.

### Gate Mode

Choose how the gate inputs respond:

- **Latching** (default): Each trigger toggles the effect on or off.
- **Momentary**: The effect is active only while the gate signal is high.

This setting affects all three gate inputs (Bend, Break, and Freeze) simultaneously.

### Bend Quantize

Constrain pitch bending to musically useful intervals.

**Scale** -- select from:

| Scale | Notes per octave |
|-------|-----------------|
| None (free) | Continuous -- no quantization |
| Chromatic | 12 (all semitones) |
| Major | 7 |
| Minor | 7 |
| Pentatonic Major | 5 |
| Pentatonic Minor | 5 |
| Whole Tone | 6 |
| Octaves | 1 (octave jumps only) |
| Fifths | 2 (fifths and octaves) |

**Root** -- select the root note of the scale (C through B). The default is C.

When a scale is selected, pitch bends snap to the nearest note in that scale. This is useful for keeping pitch-bent material harmonically related to the rest of your patch. Set to "None (free)" for unquantized pitch bending.

---

## Tips and Techniques

### Getting Started

- **MIX defaults to 100%**, so you will hear the effect immediately. Turn it down to blend with the dry signal.
- **BEND, BREAK, and CORRUPT all default to 0%.** Turn the knobs up *and* press the corresponding enable button to hear the effect.
- The **display** is your best friend -- watch for the green "BND", orange "BRK", and blue "FRZ" indicators to confirm which effects are active.

### Rhythmic Glitching

- Patch a clock or sequencer gate output into the **BEND**, **BREAK**, or **FREEZE** gate inputs.
- Set Gate Mode to **Momentary** (right-click menu) so effects are only active on the beat.
- Use different clock divisions on each gate input for polyrhythmic glitch patterns.

### Tempo-Synced Buffer

- Patch your master clock into **CLK IN**. The buffer will automatically sync its timing to the incoming clock, keeping delay repeats locked to your tempo.
- The module detects the clock connection automatically -- no manual switching needed.

### Pitch Bending

- Enable **Bend Quantize** (right-click menu) to keep pitch bends musical. Pentatonic scales are a good starting point for interesting but harmonious results.
- The **Octaves** and **Fifths** scales produce dramatic, always-consonant pitch jumps.
- Modulate the BEND knob with a slow LFO for evolving pitch textures.

### Stereo Effects

- Enable **STER** (Unique Stereo mode) for wide, diffused glitch effects where each channel is processed independently.
- Combine with Bend for stereo pitch detuning, or with Break for offset rhythmic stutters between channels.

### Creative Combinations

- **Freeze + Bend**: Freeze a buffer segment and pitch-bend it for a granular-style effect.
- **Break + Corrupt**: Layer rhythmic stutters with corruption for heavily processed textures.
- **Low Repeats + High Corrupt**: Use a short buffer with minimal repeats but heavy corruption for real-time distortion effects.
- **High Repeats + Low Mix**: Create ambient washes by feeding back heavily while blending with the dry signal.

### Macro vs. Micro Mode

- **Macro** (green LED): Effects operate on a larger scale with broader, more sweeping changes.
- **Micro** (red LED): Effects operate on a finer scale with tighter, more detailed glitching.
- Toggle with the **MODE** button to find the character that suits your material.

### Break Silence Mode

- Enable **SLNC** to make breaks cut to silence instead of stuttering audio.
- This can create rhythmic gating effects, especially when driven by a sequenced gate pattern.
