# Corrupter User Manual

**No Such Device** | Author: Thorinside | 18 HP

Corrupter is a circuit-bent stereo buffer effect for VCV Rack. It captures incoming audio into an internal buffer of up to 30 seconds, then does unspeakable things to it -- pitch bending, rhythmic breaks, and a rotating cast of corruption algorithms that range from "tastefully degraded" to "what have I done." Don't Panic. The damage is entirely reversible. Probably.

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
3. **MIX** defaults to 100% (fully wet) -- leave it or turn it down for parallel processing.
4. Set **TIME** and **REPEATS** to taste.
5. Turn up the **BEND**, **BREAK**, or **CORRUPT** knobs.
6. Press the corresponding **BEND**, **BREAK**, or **FREEZE** buttons below the display to activate each effect. Their status indicators appear on the display.
7. Press **ALGO** to cycle through the five corruption algorithms.

You should now hear your audio being enthusiastically mistreated. From here, experiment with the knobs, gate inputs, and right-click menu options.

---

## Signal Flow

```
Audio In (L/R) --> Internal Buffer (up to 30s) --> DSP Engine --> Audio Out (L/R)
                                                       ^
                                           Bend / Break / Corrupt
                                           CV / Gates / Clock
```

- If only **IN L** is connected, it is normalled (copied) to **IN R**, producing a dual-mono signal.
- The DSP engine processes audio in blocks of 256 frames internally.
- The buffer length is controlled by the **TIME** knob and can be externally clocked via the **CLOCK** input.

---

## Panel Layout

From top to bottom:

```
+----------------------------------------------+
|               CORRUPTER                       |
| IN L   IN R   CLOCK            OUT L   OUT R  |
|  [o]    [o]    [o]              [o]     [o]   |
|                                               |
|       TIME       REPEATS        MIX           |
|       (  )        (  )         (  )           |
|       [cv]        [cv]         [cv]           |
|                                               |
|       BEND       BREAK        CORRUPT         |
|       (  )        (  )         (  )           |
|     [cv](a)     [cv](a)      [cv](a)         |
|                                               |
| [============== DISPLAY ================]     |
|                                               |
|   BEND    BREAK    FREEZE    ALGO             |
|   [btn]   [btn]    [btn]    [btn]             |
|   [gate]  [gate]   [gate]   (GW)              |
|                                               |
|       MODE       SLNC        ST MODE          |
|       [btn]      [btn]       [btn]            |
|                                               |
|            NO SUCH DEVICE                     |
+----------------------------------------------+

(  ) = large knob    (a) = trimpot    [o] = port
[btn] = button       [cv] = CV input  [gate] = gate input
```

---

## Controls Reference

### Audio I/O

| Port | Type | Description |
|------|------|-------------|
| **IN L** | Input | Left audio input. |
| **IN R** | Input | Right audio input. If unpatched, receives a copy of IN L. |
| **CLOCK** | Input | External clock input. When a cable is connected, the module automatically switches from its internal clock to the external clock. Disconnect to return to internal clock. |
| **OUT L** | Output | Left audio output (processed signal). |
| **OUT R** | Output | Right audio output (processed signal). |

All audio ports expect standard VCV Rack levels (+/- 5V).

---

### Row 1: Time / Repeats / Mix

These three large knobs control the fundamental delay-buffer behaviour.

| Knob | Range | Default | Description |
|------|-------|---------|-------------|
| **TIME** | 0--100% | 50% | Controls the delay/buffer time. Lower values produce shorter, tighter loops; higher values create longer, more spacious delays. |
| **REPEATS** | 0--100% | 50% | Controls the feedback/repeat amount. At 0% you hear a single pass through the buffer. Higher values feed the output back in for cascading repetitions -- the audio equivalent of standing between two mirrors. |
| **MIX** | 0--100% | 100% | Dry/wet balance. At 0% you hear only the dry input signal. At 100% you hear only the wet (processed) signal. |

Each knob has a corresponding **CV input** directly below it (see [CV Inputs](#cv-inputs-and-attenuators)).

---

### Row 2: Bend / Break / Corrupt

These three large knobs control the intensity of each glitch effect. All default to 0% -- you must turn them up *and* enable the corresponding button for the effect to be heard. The knob alone does nothing; the button alone does nothing. Both are required. This is not a design flaw. It is a feature. Probably.

| Knob | Range | Default | Description |
|------|-------|---------|-------------|
| **BEND** | 0--100% | 0% | Controls the pitch bend amount. When enabled (via button or gate), this determines how far the pitch shifts. Use the right-click menu to quantize bends to musical scales. |
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
  - **FRZ** -- appears in blue when Freeze is active.
  - **Algorithm name** -- the currently selected corruption algorithm (e.g., "DECIMATE", "VINYL SIM").

---

### Buttons

Four buttons are arranged in a row below the display. Each button's tooltip updates to show its current state when you hover over it.

| Button | Function |
|--------|----------|
| **BEND** | Toggles the Bend effect on/off. When active, the BEND knob amount is applied to pitch-shift the buffer playback. Indicated by "BND" on the display. |
| **BREAK** | Toggles the Break effect on/off. When active, the BREAK knob amount controls rhythmic glitch intensity. Indicated by "BRK" on the display. |
| **FREEZE** | Toggles buffer freeze on/off. When active, the buffer stops recording new input and loops its current contents. Indicated by "FRZ" on the display. |
| **ALGO** | Cycles through the five corruption algorithms. Each press advances to the next algorithm. The current algorithm name is shown on the display and in the tooltip. |

---

### Gate Inputs

Three gate input jacks sit below the **BEND**, **BREAK**, and **FREEZE** buttons. These allow external control of each effect's on/off state. The **GW** (Glitch Window) trimpot sits at the end of this row.

| Control | Type | Description |
|---------|------|-------------|
| **BEND GATE** | Gate input | Bend effect enable |
| **BREAK GATE** | Gate input | Break effect enable |
| **FREEZE GATE** | Gate input | Freeze enable |
| **GW** (Glitch Window) | Trimpot (0--100%, default 2%) | Controls the size of the glitch window. Smaller values produce tighter, more precise glitches; larger values create longer, more dramatic disruptions. |

Gate behaviour depends on the **Gate Mode** setting (see [Right-Click Menu](#right-click-menu)):

- **Latching** (default): A rising edge toggles the effect on or off.
- **Momentary**: The effect is active only while the gate is high.

---

### Bottom Section

The bottom row contains settings buttons, aligned under the three main knob columns.

| Control | Type | Description |
|---------|------|-------------|
| **MODE** | Button | Toggles between Macro and Micro mode. Green LED = Macro, red LED = Micro. |
| **SLNC** | Button | Toggles Break Silence mode. When enabled (green LED), breaks introduce silence rather than stuttered audio. |
| **ST MODE** | Button | Toggles Unique Stereo mode. When enabled (green LED), left and right channels are processed independently for wider stereo glitch effects. |

---

## Corruption Algorithms

Press the **ALGO** button to cycle through the five available algorithms. The **CORRUPT** knob controls the intensity of whichever algorithm is active. Think of it as a "how much damage" knob.

### Legacy Bank (Algorithms 1--3)

| Algorithm | Description |
|-----------|-------------|
| **DECIMATE** | Sample rate and bit depth reduction. Low settings add subtle grit; high settings produce harsh, aliased digital crunch. |
| **DROPOUT** | Randomly drops audio segments, creating gaps and stutters in the signal. Higher values produce more frequent and longer dropouts. |
| **DESTROY** | Aggressive digital distortion and buffer mangling. The most extreme algorithm. Handle with care, or don't -- we're not your parents. |

### Expanded Bank (Algorithms 4--5)

| Algorithm | Description |
|-----------|-------------|
| **DJ FILTER** | A resonant filter effect that sweeps through the frequency spectrum. Lower CORRUPT values apply subtle filtering; higher values produce dramatic sweeps. |
| **VINYL SIM** | Simulates vinyl record artifacts -- crackle, wow, flutter, and surface noise. For when you want your pristine digital audio to sound like it was found in a Flohmarkt in Kreuzberg. |

The algorithm bank is selected automatically when you cycle through algorithms.

---

## Right-Click Menu

Right-click the module panel to access additional settings.

### Gate Mode

Choose how the gate inputs respond:

- **Latching** (default): Each trigger toggles the effect on or off.
- **Momentary**: The effect is active only while the gate signal is high.

This setting affects all three gate inputs (Bend, Break, and Freeze) simultaneously.

### Bend Quantize

Constrain pitch bending to musically useful intervals. Because sometimes you want your glitches to be in key.

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

When a scale is selected, pitch bends snap to the nearest note in that scale.

---

## Tips and Techniques

### Getting Started

- **MIX defaults to 100%**, so you will hear the wet signal immediately. Turn it down to blend with the dry signal.
- **BEND, BREAK, and CORRUPT all default to 0%.** Turn the knobs up *and* press the corresponding enable button to hear the effect.
- The **display** is your best friend -- watch for the green "BND", orange "BRK", and blue "FRZ" indicators to confirm which effects are active.

### Rhythmic Glitching

- Patch a clock or sequencer gate output into the **BEND**, **BREAK**, or **FREEZE** gate inputs.
- Set Gate Mode to **Momentary** (right-click menu) so effects fire only on the beat.
- Use different clock divisions on each gate input for polyrhythmic glitch patterns.

### Tempo-Synced Buffer

- Patch your master clock into **CLK IN**. The buffer will automatically sync its timing to the incoming clock, keeping delay repeats locked to your tempo.
- The module detects the clock connection automatically -- no manual switching needed.

### Pitch Bending

- Enable **Bend Quantize** (right-click menu) to keep pitch bends musical. Pentatonic scales are a good starting point.
- The **Octaves** and **Fifths** scales produce dramatic, always-consonant pitch jumps.
- Modulate the BEND knob with a slow LFO for evolving pitch textures.

### Stereo Effects

- Enable **ST MODE** for wide, diffused glitch effects where each channel is processed independently.
- Combine with Bend for stereo pitch detuning, or with Break for offset rhythmic stutters between channels.

### Creative Combinations

- **Freeze + Bend**: Freeze a buffer segment and pitch-bend it for a granular-style effect.
- **Break + Corrupt**: Layer rhythmic stutters with corruption for heavily processed textures.
- **Low Repeats + High Corrupt**: Short buffer with minimal repeats but heavy corruption for real-time distortion.
- **High Repeats + Low Mix**: Create ambient washes by feeding back heavily while blending with the dry signal.

### Macro vs. Micro Mode

- **Macro** (green LED): Effects operate on a larger scale with broader, more sweeping changes.
- **Micro** (red LED): Effects operate on a finer scale with tighter, more detailed glitching.
- Toggle with the **MODE** button to find the character that suits your material.

### Break Silence Mode

- Enable **SLNC** to make breaks cut to silence instead of stuttering audio.
- This can create rhythmic gating effects, especially when driven by a sequenced gate pattern.
