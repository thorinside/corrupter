# Corrupter to 4ms MetaModule feasibility

## Primary-source scope

Research date: 2026-08-31. MetaModule Plugin SDK source reviewed at commit [`f9e7c1e`](https://github.com/4ms/metamodule-plugin-sdk/tree/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8); MetaModule firmware at [`2219ae3`](https://github.com/4ms/metamodule/tree/2219ae3280ebebf97e114a2a37606a255a425669); official plugin examples at [`604b370`](https://github.com/4ms/metamodule-plugin-examples/tree/604b370eb62175119baebc232cdf2ceef5a78f0b). This note uses only first-party 4ms documentation/source and STMicroelectronics documentation. Facts and inferences are separated explicitly.

## Executive findings

- **Confirmed:** 4ms provides a supported VCV Rack adaptation path. Existing Rack `Module`, `ModuleWidget`, `createModel()`, `plugin.json`, and registration patterns are presented through a Rack-compatible adapter whose API is "very close to" Rack SDK 2.4.1. The source is cross-compiled into an ARM shared object and packaged with metadata/assets as an `.mmplugin` file. ([SDK Rack interface](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/rack-interface/README.md), [SDK overview and conversion example](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/README.md), [plugin loading model](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/docs/how-plugins-work.md))
- **Confirmed:** The current production MetaModule is a bare-metal, dual-core 800 MHz Cortex-A7 system with a Cortex-M4 coprocessor and 512 MB of DDR3 RAM. The SDK emits Cortex-A7/NEON-VFPv4 hard-float code. The firmware exposes about 290 MB to modules/plugins in the cited firmware, not the full 512 MB. ([4ms product specification](https://www.4mscompany.com/metamodule.html), [SDK target flags](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/cmake/arch_mp15xa7.cmake), [SDK memory API](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/docs/system-api.md#memory))
- **Confirmed:** Ordinary Rack knobs, switches, sliders, jacks, and lights are discoverable from the `ModuleWidget`, but MetaModule redraws them using its own engine. SVG content is not rendered at runtime; assets must be converted to PNG. Custom NanoVG display widgets often work, subject to clipping, layer, frame-rate, color, texture, and API limits. ([SDK limitations](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/docs/limitations.md), [graphics conversion guide](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/docs/graphics.md), [dynamic display guide](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/docs/graphic-displays.md))
- **Confirmed:** The hardware can run at 24, 36, 48, or 96 kHz with block sizes 16 through 512. CPU and memory headroom must be measured on hardware: 4ms defines 200% CPU as both A7 cores, reports a 7-9% per-patch base overhead, and warns that plugin memory is shared. ([audio preferences](https://metamodule.info/docs/preferences.html), [official Module Finder CPU explanation](https://metamodule.info/modulefinder), [SDK real-time and memory guidance](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/docs/tips.md))
- **Inference:** For a conventional, single Rack module with self-contained C++ DSP, no expander dependency, no real-time allocation, and standard controls, the adapter route is materially lower risk than a native `CoreProcessor` rewrite. Compilation is only the first gate; the acceptance gate is correct graphics/control discovery plus measured audio behavior and CPU/RAM use on a MetaModule.

## Recommendation

**Proceed with a narrow adapter-based prototype. Corrupter is a high-feasibility MetaModule port, and Neal could realistically do it.** The strongest evidence is not analogy to another plugin: at repository commit `3d5c23c`, the existing Corrupter Rack wrapper and all five DSP translation units cross-compiled, linked, and packaged *without source edits* using the current official MetaModule Plugin SDK 2.3. The remaining work is productization and hardware validation, not a DSP rewrite.

A reasonable estimate is **3-6 focused engineering days to a hardware-tested beta**, followed by **1-2 days of release polish** if official catalog distribution is desired. A compile-and-load prototype should fit in **half to one day** now that source compatibility has been established. These are engineering estimates, not measured delivery dates; CPU profiling could add optimization time if the worst-case algorithms exceed the target budget.

## 1. Corrupter as it exists today

Repository scope reviewed: root DSP library, Rack wrapper and manual, tests/benchmark, and the existing disting NT adapter. The claims below come from [`README.md`](../README.md), [`vcv/src/Corrupter.cpp`](../vcv/src/Corrupter.cpp), [`vcv/Makefile`](../vcv/Makefile), [`include/corrupter_dsp/engine.h`](../include/corrupter_dsp/engine.h), and [`nt/Makefile`](../nt/Makefile).

### Product and DSP behavior

Corrupter is an 18 HP stereo circuit-bent buffer effect inspired by failing media. It records as much as 30 seconds and combines three independent destruction axes:

- **Bend:** randomized rate/reverse/wow/flutter/saturation in Macro mode; continuous tape-like pitch shift over plus or minus three octaves in Micro mode, optionally quantized to nine built-in scale choices and a root note.
- **Break:** randomized buffer-position jumps in Macro mode; continuous traversal or rhythmic silence/dropout behavior in Micro mode.
- **Corrupt:** five output-degradation algorithms: Decimate, Dropout, Destroy, two-times-oversampled TPT DJ Filter, and Vinyl Sim.
- **Buffer controls:** Time, Repeats, dry/wet Mix, Freeze, external/internal clock selection, and unique-versus-linked stereo behavior.

The engine is a platform-independent C++17 library with a flat C wrapper. Its public header uses only fixed-width and size types; audio/CV/gate data arrive as caller-owned arrays; engine state and the stereo audio buffer are placement-constructed into caller-supplied DRAM; and processing is `noexcept`. There is no filesystem, networking, MIDI, thread, expander, GPU, or third-party DSP dependency. The same engine already builds for the disting NT's Cortex-M7 with hard-float, `-fno-rtti`, and `-fno-exceptions`. That is useful embedded-portability evidence, although it is not a MetaModule performance measurement.

### Rack-facing surface

The Rack module declares **18 parameter IDs, 12 inputs, 2 outputs, and 15 light IDs**. One declared parameter, `PARAM_CORRUPT_BANK`, has no panel widget and is not triggered by the current Rack process path; the algorithm button derives the bank automatically. The user-visible surface is therefore 17 controls:

| Surface | Count | Elements |
| --- | ---: | --- |
| Continuous controls | 10 | Time, Repeats, Mix, Bend, Break, Corrupt, three Bend/Break/Corrupt CV attenuators, Glitch Window |
| Buttons | 7 visible | Bend, Break, Freeze, Macro/Micro Mode, Corrupt Algorithm, Break Traverse/Silence, Stereo Mode |
| Inputs | 12 | stereo audio L/R; Time/Repeats/Mix/Bend/Break/Corrupt CV; Bend/Break/Freeze gates; Clock |
| Outputs | 2 | stereo audio L/R |
| Context options | 3 groups | Gate Mode; Bend quantization Scale; Bend quantization Root |

The wrapper normalizes Rack audio from plus or minus 5 V to the engine's float range and restores the output scale; right input normals from left. It automatically uses the internal clock when the Clock jack is unpatched. JSON persistence covers enable states, macro/micro and stereo modes, corruption algorithm/bank, glitch window, gate mode, scale, and root. Keeping the existing plugin and module slugs (`Corrupter`) and parameter/port ordering is important for patch identity.

### UI and dependency shape

This is a direct Rack plugin, but it uses only standard Rack components: `PJ301MPort`, `RoundBigBlackKnob`, `Trimpot`, `VCVButton`, standard lights, and screws. It does **not** depend on Fundamental or another widget library. The panel uses one SVG background plus two custom NanoVG widgets:

- `CorrupterDisplay`, a bounded LED display that draws a 128-bin waveform, write cursor, Bend/Break/Freeze status, and algorithm name using DejaVu Sans.
- `CorrupterLabels`, a full-panel transparent overlay that draws almost all static labels at runtime because the SVG does not contain them.

Standard controls are a good fit for the MetaModule Rack adapter. The display is within the documented custom-display model and is likely to work with small compatibility fixes, but needs simulator/device visual QA. The label overlay is a poor embedded display surface: it would create a large dynamic panel-sized buffer for text that never changes. Its labels should be baked into the MetaModule faceplate PNG instead.

### Corrupter-specific runtime characteristics

Three existing choices matter more on MetaModule:

1. **Constructor allocation:** the Rack constructor immediately configures the engine for 30 seconds at a maximum supported rate of 96 kHz and calls `malloc()`. The stereo float buffer alone is `96,000 x 30 x 2 x 4 = 23,040,000` bytes (about 21.97 MiB), plus engine state/alignment. The adapter constructs a module during plugin discovery, so that memory is paid at plugin load before a patch contains Corrupter. At a fixed 48 kHz ceiling, the audio allocation is about 10.99 MiB.
2. **A 256-frame adapter block:** Rack's per-sample callback fills local arrays, runs the DSP only every 256 samples, and plays the previous block. This adds about 5.33 ms at 48 kHz and concentrates work into periodic bursts. MetaModule's own guidance recommends 16 frames for block-oriented ports to avoid CPU spikes at its minimum block size.
3. **String assignment in the audio path:** after every block, `processBlock()` assigns seven `std::string` parameter descriptions for desktop hover tooltips. The SDK explicitly forbids string creation/resizing from `process()`. These assignments should be removed, made static, or moved behind a desktop-only guard.

The DSP itself is computationally non-trivial: stereo cubic buffer reads and crossfades, double-precision read phases, per-sample sine/tanh work, pitch quantization, and two-times-oversampled nonlinear/filter modes. That is a **profiling risk**, not evidence of infeasibility. The existing desktop benchmark is useful only for regression and relative optimization; it cannot predict the Cortex-A7 load.

## 2. Hardware and runtime constraints

### Confirmed facts

| Area | Confirmed platform behavior | Porting consequence |
| --- | --- | --- |
| Processor | The shipping MetaModule is specified as **dual-core 800 MHz Cortex-A7 plus a Cortex-M4 coprocessor**, running bare-metal. The SDK targets `cortex-a7` with NEON/VFPv4 and the hard-float ABI. ([4ms product page](https://www.4mscompany.com/metamodule.html), [SDK architecture CMake](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/cmake/arch_mp15xa7.cmake)) | Desktop-only x86 intrinsics cannot be assumed. Generic C++/scalar DSP is the safe starting point; ARM NEON or SIMDe compatibility must be checked if explicit SIMD is used. |
| SoC family | The MetaModule firmware build defines `STM32MP157Cxx` and uses STM32MP15xx linker/startup files. ST specifies dual Cortex-A7 up to 800 MHz, a Cortex-M4 up to 209 MHz, 32 KB instruction plus 32 KB data L1 per A7, 256 KB shared L2, and external DDR up to 533 MHz. ([MetaModule firmware architecture CMake](https://github.com/4ms/metamodule/blob/2219ae3280ebebf97e114a2a37606a255a425669/firmware/cmake/arch_mp15xa7.cmake), [ST STM32MP157 datasheet](https://www.st.com/resource/en/datasheet/stm32mp157d.pdf)) | Cache locality and memory traffic matter much more than on a desktop. This is architectural context, not a per-plugin benchmark. |
| RAM | Hardware has **512 MB DDR3 at 533 MHz**. SDK guidance says about **300 MB is dedicated to plugins**, shared by plugin code and module instances; `System::total_memory()` returned 304,087,040 bytes (~290 MB) on firmware v2.0.12 and is explicitly documented as firmware-dependent. Assets such as PNGs are not counted as plugin code/data in that SDK guidance, but plugin files are unpacked to a RAM disk and unused assets still consume RAM. ([4ms product page](https://www.4mscompany.com/metamodule.html), [SDK tips](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/docs/tips.md#memory), [memory API](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/docs/system-api.md#memory), [plugin file format](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/docs/plugin-file-format.md)) | Budget code, per-instance buffers, and packaged assets separately. Test duplicate instances, not only one instance in isolation. |
| CPU accounting | The official Module Finder reports load as a percentage of **one** core at 48 kHz; two cores provide about **200%** total. It also reports 7-9% base overhead per patch plus cable/mapping overhead. ([official Module Finder](https://metamodule.info/modulefinder)) | A module reading 40% is not 40% of the whole device. Leave margin for routing, UI, mappings, and other modules. |
| Audio settings | User-selectable sample rates are 24, 36, 48, and 96 kHz. Block sizes are 16, 32, 64, 128, 256, and 512. Higher sample rates use more CPU; smaller blocks reduce latency, while efficiency effects vary by module and patch. ([MetaModule preferences](https://metamodule.info/docs/preferences.html)) | Validate sample-rate-dependent state through Rack's sample-rate-change mechanism. At minimum test 48 kHz/16 and the intended quality setting; also exercise rate changes. |
| Physical audio/CV | The product page specifies 24-bit, 48 kHz, DC-coupled, -10 V to +10 V CV/audio jacks. This physical I/O specification is distinct from the internal engine's selectable sample rate. ([4ms product page](https://www.4mscompany.com/metamodule.html)) | Do not infer internal engine rates from the codec/jack headline; use `ProcessArgs.sampleRate`/sample-rate callbacks as normal. |

The DSP callback is hard real-time. The SDK explicitly prohibits allocations in Rack `process()` or anything it calls: do not grow containers or create/resize strings there. Construction, a pre-audio first call to `process()`, or an `AsyncThread` are the documented alternatives. The SDK also warns that adapter-based plugins instantiate every module at plugin load to scan widget trees; large constructor allocations can therefore prevent the *entire plugin* from loading even when that module is not in a patch. ([real-time allocation guidance](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/docs/tips.md#memory-allocations-in-audio-process), [asynchronous work API](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/docs/async-threads.md))

The SDK recommends 16 frames when a module benefits from block processing because that is MetaModule's minimum block size; a larger internal processing quantum can produce CPU spikes even if average work is similar. The adapter still exposes Rack's per-sample `process()` model, so a module need not be rewritten as block DSP merely to compile. ([SDK block-size guidance](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/docs/tips.md#block-sizes), [Rack adapter overview](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/docs/rack-adaptor.md))

### Cortex-A7 versus Cortex-A53 boundary

**Confirmed:** Every current first-party source tied to the shipping product and plugin toolchain identifies Cortex-A7, not Cortex-A53: the product page says dual 800 MHz Cortex-A7, the SDK's architecture file passes `-mcpu=cortex-a7`, and the SDK emits a 32-bit Arm hard-float shared object. ([4ms product page](https://www.4mscompany.com/metamodule.html), [SDK target flags](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/cmake/arch_mp15xa7.cmake))

**Inference:** Do not count the M4 coprocessor as a third general plugin DSP core. The public plugin target is A7, while 4ms's own CPU accounting exposes a two-core/200% budget. No reviewed public plugin API provides M4 code deployment. ([SDK target flags](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/cmake/arch_mp15xa7.cmake), [official Module Finder](https://metamodule.info/modulefinder))

**Research boundary / inference:** No official 4ms source reviewed identifies a Cortex-A53 MetaModule hardware variant. 4ms does maintain a separate STM32MP2 bare-metal experiment, but that target is dual Cortex-A35, not A53, and the repository does not describe it as shipping MetaModule hardware. It should not be used to raise the Corrupter performance estimate. ([4ms STM32MP2 bare-metal repository](https://github.com/4ms/stm32mp2-baremetal))

## 3. What the MetaModule SDK actually adapts

### Confirmed architecture

There are two supported plugin styles:

1. **Rack adapter port.** Compile the VCV plugin's source against the SDK's `rack-interface`. That interface says its plugin-facing API is very close to Rack SDK 2.4.1. The SDK replaces or modifies selected Rack surfaces—including `ModuleWidget`, `Module`, `Port`, `Model`, `createModel()`, MIDI buffering, and the minBLEP table—while retaining familiar Rack source structure. ([Rack interface README](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/rack-interface/README.md), [adapter internals guide](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/docs/rack-adaptor.md))
2. **Native plugin.** Implement MetaModule's `CoreProcessor`, provide a `ModuleInfo`/element description, and register with `register_module()`. The SDK presents this as an alternative for code that is not Rack-based. ([SDK overview](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/README.md), [CoreProcessor API](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/docs/coreprocessor.md), [module registry](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/docs/module-registry.md))

The common phrase "build with the MetaModule fork of the Rack SDK" is directionally useful but technically imprecise. The documented build includes `metamodule-plugin-sdk/plugin.cmake`; this supplies the Rack-compatible headers/interface and links the module into a MetaModule plugin. It does **not** use the desktop `RACK_DIR` build. The *plugin source repository* may be original or a small 4ms/maintainer fork if adaptations are required. The official examples explicitly contain both unaltered sources and fork/branch sources with minor changes, and sometimes omit unsupported modules. ([conversion CMake example](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/README.md#basic-example-for-converting-a-rack-plugin), [official examples overview](https://github.com/4ms/metamodule-plugin-examples/blob/604b370eb62175119baebc232cdf2ceef5a78f0b/README.md))

At build time `create_plugin()` defines `METAMODULE`, applies Cortex-A7/PIC/shared-object flags, links against the MetaModule SDK, validates metadata, copies assets, and builds the tar-based `.mmplugin`. At load time firmware untars it, copies assets/metadata to its RAM disk, relocates the ELF `.so` against firmware API symbols, runs static constructors, and calls `init(rack::Plugin*)` or `init()`. ([`plugin.cmake`](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/plugin.cmake), [plugin loader sequence](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/docs/how-plugins-work.md), [package contents](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/docs/plugin-file-format.md))

### UI and widget compatibility

**Standard controls are adapted.** The adapter's `ModuleWidget` has explicit paths for `ParamWidget`, knobs, sliders, switches, input/output ports, lights, lighted buttons/sliders, panel images, and selected SVG widgets. It scans the constructed widget tree to build MetaModule elements. ([adapter `ModuleWidget`](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/rack-interface/include/app/ModuleWidget.hpp))

**Runtime SVG rendering is not supported.** Convert Rack's `res/**/*.svg` files to PNG, preserve relative paths/base names, and point `SOURCE_ASSETS` at the resulting asset tree. The adapter rewrites a typical `res/foo.svg` asset request to `foo.png`. Faceplates should be 240 pixels high with an opaque background; the screen is 240 × 320 RGB565. The SDK includes `scripts/SvgToPng.py`, which uses Inkscape, but recommends hand-tuning images when low-resolution quality matters. ([graphics guide](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/docs/graphics.md))

**Knob/jack/light appearance is not the Rack widget renderer.** MetaModule draws parameters, jacks, and lights itself, and ignores children of those widget types. Therefore decorative children, custom shadows, foregrounds, or labels embedded in a control widget may disappear even when the control itself works. ([SDK limitations](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/docs/limitations.md))

**Custom displays can work, but are a separate QA surface.** Non-control child widgets become dynamic displays; their `step()`, `draw()`, and `drawLayer(..., 1)` methods can render supported NanoVG calls into a pixel buffer. Limits include clipping to the widget box, layer 1 only, variable/slow frame rate (the guide cites a 20 FPS target for one screen in a development branch), full-opacity text, no SVGs/textures, and missing NanoVG text-measurement functions. TTF assets are supported. ([graphics display guide](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/docs/graphic-displays.md))

**Expanders are unsupported.** Modules cannot communicate through Rack expander links and behave as though no expander is attached. ([SDK limitations](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/docs/limitations.md))

**Inference:** A standard Rack panel usually needs asset conversion and visual QA, not a conceptual UI redesign. A bespoke control whose identity depends on child widgets, custom hit behavior, runtime SVG, multiple draw layers, textures, or a fast display may need MetaModule-specific code guarded by `#ifdef METAMODULE` or replacement with adapter-recognized primitives.

## 4. Build, package, and install path

### Confirmed minimum workflow

1. Clone `metamodule-plugin-sdk` recursively and install CMake 3.22+, Ninja (or another generator), Python 3.6+, and a supported `arm-none-eabi-gcc`. The current README lists GCC 12.2, 12.3, and 15.3 with prebuilt plugin-libc archives; 13.2/13.3, 14.2/14.3, and 15.2 require generating a matching archive. ([SDK requirements](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/README.md#requirements), [plugin-libc instructions](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/plugin-libc/README.md))
2. Add a MetaModule `CMakeLists.txt` that locates `METAMODULE_SDK_DIR`, includes `plugin.cmake`, defines a static source library, lists the Rack plugin source files/includes, then calls `create_plugin(SOURCE_LIB … PLUGIN_NAME … PLUGIN_JSON … SOURCE_ASSETS … DESTINATION …)`. `PLUGIN_NAME` must match the Rack brand slug so asset paths and patch identity resolve. ([conversion example](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/README.md#basic-example-for-converting-a-rack-plugin), [plugin-name requirement](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/docs/plugin-file-format.md#plugin-name))
3. Retain the Rack `plugin.json` and add `plugin-mm.json` at the MetaModule project root. The latter declares MetaModule display/maintainer data and exactly which module slugs are included; `slug` must match patch/plugin JSON identity. ([MetaModule metadata reference](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/docs/plugin-mm-json.md))
4. Convert required SVG assets to PNG and include fonts/presets actually needed. Avoid packaging unused files because the package is unpacked to RAM. ([graphics guide](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/docs/graphics.md), [package format](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/docs/plugin-file-format.md))
5. Configure and build with `cmake --fresh -B build -G Ninja` and `cmake --build build`. The output is an `.mmplugin` tarball containing an ARM `.so`, an SDK compatibility marker, both metadata files, and assets. ([SDK build example](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/README.md), [package format](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/docs/plugin-file-format.md))
6. Copy the `.mmplugin` into `metamodule-plugins/` or the root of a USB drive/microSD card. On the device use **Settings → Plugins → Scan disks**, then load it. ([MetaModule plugin installation manual](https://metamodule.info/docs/plugins.html#installing-plugins))
7. Match firmware compatibility. For SDK/firmware v2+, plugin SDK major must equal firmware major and SDK minor must be no greater than firmware minor; the SDK marker is embedded in the package and checked by the loader. ([versions and compatibility](https://metamodule.info/docs/versions.html))

For release, 4ms specifies `[BrandSlug]-v[plugin-version].mmplugin` (optionally with an SDK/firmware suffix) and documents a GitHub Actions flow that clones the SDK, builds with CMake/Ninja, and attaches the result to a tagged release. Listing on the official plugin page has maintainer, licensing, filename, metadata, hosting, and operational requirements. ([SDK release guide](https://github.com/4ms/metamodule-plugin-sdk/blob/f9e7c1e481c8f99e98f0dc4ec00e5a459718fdb8/docs/release.md))

### Suggested verification ladder (inference, derived from the confirmed interfaces)

1. Cross-compile and inspect/untar the `.mmplugin` for the correct brand directory, `.so`, SDK marker, JSON, and only required PNG/font assets.
2. Load on current firmware and confirm the module appears under the intended brand with every parameter/input/output/light named and ordered correctly.
3. Compare saved patch compatibility between VCV Rack and MetaModule using unchanged brand/module slugs.
4. Exercise all controls, CV inputs, outputs, state serialization, bypass, sample-rate changes, and any context-menu/alternate parameters.
5. Run signal-level comparisons at 24/36/48/96 kHz as applicable; test at block size 16 and the intended deployment block size.
6. Measure device CPU and RAM for one and multiple instances, inside a representative patch, leaving margin under the documented 200% two-core budget.
7. Power-cycle/reload the patch to verify plugin loading, state restoration, and asset availability from removable storage.

## 5. Concrete first-party port examples

The official [`metamodule-plugin-examples`](https://github.com/4ms/metamodule-plugin-examples/tree/604b370eb62175119baebc232cdf2ceef5a78f0b) repository is the primary pattern library. It includes VCV ports for Fundamental, Bogaudio, ChowDSP, NANO Modules, Geodesics, Impromptu Modular, RebelTech, Vostok, and others; it states that some use original unaltered Rack source, some use a fork/branch with minor MetaModule changes, and some omit modules. ([examples README](https://github.com/4ms/metamodule-plugin-examples/blob/604b370eb62175119baebc232cdf2ceef5a78f0b/README.md))

Three useful implementation shapes are visible in that repository:

- **Straight source-list adapter:** RebelTech's MetaModule CMake includes the SDK, lists the Rack module `.cpp` files plus the original `plugin.cpp`, supplies source includes/assets/JSON, and calls `create_plugin()`. This is the baseline shape when registration and widgets already fit. ([RebelTech CMake](https://github.com/4ms/metamodule-plugin-examples/blob/604b370eb62175119baebc232cdf2ceef5a78f0b/RebelTech/CMakeLists.txt))
- **Selective/custom registration:** NANO Modules compiles selected Rack modules and supporting DaisySP source but substitutes a MetaModule-side `plugin.cpp` so blank panels are excluded. This demonstrates preserving DSP/module code while changing only registration and the included module set. ([NANO Modules CMake](https://github.com/4ms/metamodule-plugin-examples/blob/604b370eb62175119baebc232cdf2ceef5a78f0b/NANOModules/CMakeLists.txt), [NANO MetaModule registration](https://github.com/4ms/metamodule-plugin-examples/blob/604b370eb62175119baebc232cdf2ceef5a78f0b/NANOModules/plugin.cpp))
- **Fork plus compatibility edits/omissions:** ChowDSP uses a 4ms-maintained VCV fork, a MetaModule-specific `plugin.cpp`, explicit sources/options, and excludes modules documented in the CMake as crashing. Bogaudio similarly uses a 4ms fork, a `metamodule-plugin.cpp`, a mutex stub, warning accommodations, and excludes analyzer sources with an unsupported dependency in that example. These are evidence that real ports can require more than an SDK swap and that a partial plugin is an accepted outcome. ([ChowDSP CMake](https://github.com/4ms/metamodule-plugin-examples/blob/604b370eb62175119baebc232cdf2ceef5a78f0b/ChowDSP/CMakeLists.txt), [Bogaudio CMake](https://github.com/4ms/metamodule-plugin-examples/blob/604b370eb62175119baebc232cdf2ceef5a78f0b/Bogaudio/CMakeLists.txt), [examples submodule sources](https://github.com/4ms/metamodule-plugin-examples/blob/604b370eb62175119baebc232cdf2ceef5a78f0b/.gitmodules))

The official examples build individual brands with CMake/Ninja and place `.mmplugin` artifacts under a `metamodule-plugins` directory, matching the public SDK's recommended flow. ([examples build instructions](https://github.com/4ms/metamodule-plugin-examples/blob/604b370eb62175119baebc232cdf2ceef5a78f0b/README.md#building-the-examples))

## 6. Corrupter compile probe

To separate API compatibility from speculation, a read-only, out-of-tree probe was built against MetaModule Plugin SDK commit `f9e7c1e` using the installed `arm-none-eabi-g++ 15.3.Rel1`. The temporary CMake target listed the repository's existing `vcv/src/plugin.cpp`, `vcv/src/Corrupter.cpp`, and five DSP source files, then called the SDK's `create_plugin()` with the current Rack `plugin.json`. No repository source or build file was edited for the probe.

Results:

- All existing Corrupter Rack and DSP sources compiled for Cortex-A7.
- The link completed with all MetaModule/Rack-adapter symbols resolved.
- The SDK emitted a valid SDK-2.3 `.mmplugin` package.
- Before assets, the package was 230 KiB; its ARM shared object was 218 KiB and `arm-none-eabi-size` reported 143,241 bytes text, 6,364 data, and 420 BSS.
- The repository's native Release regression test also passed. A local benchmark rendered 10.67 seconds of 96 kHz audio at about 68.8x real time on the development Mac; this is **not** target CPU evidence.

This materially raises confidence in the adapter route. It does **not** prove that the module loads on firmware, that the controls and context menu are discovered correctly, that the display is legible, that audio matches Rack, or that CPU/RAM remain acceptable in a patch. Those require simulator and physical-device checks.

## 7. Specific implementation plan

Keep the existing VCV Makefile and root DSP CMake intact. Add a parallel, narrowly scoped MetaModule target; do not fork or rewrite the engine unless profiling demands it.

| Work item | Concrete change | Estimate |
| --- | --- | ---: |
| Build and metadata | Add a MetaModule CMake target using `plugin.cmake`; list the same Rack/DSP sources; add `plugin-mm.json`; preserve `Corrupter` brand/module slugs and all numeric IDs. | 0.5-1 day |
| Static panel assets | Convert `Corrupter.svg` to the required 240-pixel-high PNG; bake `CorrupterLabels` text into that PNG; include only required PNG/font assets. | 0.5-1 day |
| Dynamic display | First try the current bounded NanoVG waveform widget; verify clipping, font, colors, and update rate in simulator/device. If it misbehaves, add a small `#ifdef METAMODULE` display or use the SDK's text-display facilities for status. | 0.5 day typical; up to 1.5 days with fallback |
| Real-time safety | Remove/guard the seven tooltip-description string assignments from the process path. Change the MetaModule accumulation quantum from 256 to 16 frames, or call the engine with the host's safe block cadence. | 0.5-1 day |
| Memory lifecycle | Avoid the 96 kHz/30-second constructor commitment for every scanned module. For a 48 kHz prototype, allocate for the actual rate; for multi-rate support, defer allocation to the documented pre-audio first process call and fail visibly if unavailable. Re-test rate changes before deciding whether to reserve the full 96 kHz buffer. | 0.5-1 day |
| Functional/device QA | Validate audio/CV/gates/clock, mono normalization, every button, context menu, JSON reload, display, and one/multiple instances. Profile worst cases at 48 kHz/block 16, then 96 kHz if promised. | 1-2 days |
| Release polish | Versioned `[BrandSlug]-v[version].mmplugin`, CI/release asset, device install instructions, firmware floor, catalog submission if desired. | 1-2 days |

The 17 visible controls exceed the MetaModule's 12 physical knobs, and Corrupter exposes 12 virtual inputs while the hardware has fewer physical panel inputs. This is normal for MetaModule: users assign virtual knobs across knob sets and map or virtually patch the subset of ports they need. It is not a source-level UI blocker, but the prototype should include a sensible demonstration mapping.

Context-menu options are expected to surface through MetaModule's module Actions menu, which is documented as the home for module-specific options equivalent to Rack right-click behavior. Corrupter's Gate Mode, Scale, and Root menus still need explicit device verification. ([MetaModule Actions menu](https://metamodule.info/docs/action_menu.html))

## 8. Risk register and acceptance boundary

| Risk | Level | Why / mitigation | Required evidence |
| --- | --- | --- | --- |
| CPU headroom | Medium | The algorithms use cubic interpolation, transcendental functions, double phases, and two-times oversampling; reduce the wrapper block to 16 and profile the Destroy, DJ Filter, and Vinyl Sim paths in stereo. | On-device CPU at 48 kHz/block 16 in a representative patch, with margin under the two-core budget |
| Constructor and per-instance RAM | Medium, controllable | Current maximum buffer is about 21.97 MiB of audio per instance; defer discovery-time allocation and/or size to the supported runtime rate. | Plugin loads from cold boot; free-memory measurements before/after one and multiple instances |
| Dynamic display | Low-medium | Documented NanoVG subset matches most calls, but font loading, RGB565 rendering, clipping, and update rate are device-specific. Bake static labels into PNG. | Simulator plus device screenshot/visual inspection while audio runs |
| Real-time allocation | Known fix | Tooltip `std::string` assignments occur in the Rack audio callback. Guard or remove them. | Code inspection plus sustained audio run without allocation/overrun diagnostics |
| 256-frame burst/latency | Known fix | Current wrapper delays a block and concentrates work; use a 16-frame cadence on MetaModule. | Latency/audio check and CPU trace at block 16 |
| Sample-rate changes | Medium | Current callback frees and reallocates the large buffer; MetaModule supports four rates. Decide whether the product supports all four and test changes/reload. | 24/36/48/96 kHz matrix for every advertised rate |
| Patch/control identity | Low | Adapter discovery depends on stable slugs and numeric parameter/port order; do not reorder them. | Save/reload and VCV/MetaModule patch identity test |
| UI density/mapping | Low | More virtual controls/inputs than physical controls/jacks is expected but demands a usable mapping. | Owner evaluation of a demo patch/knob-set layout |

No source-level blocker was found. In particular, Corrupter has no expander, x86 SIMD, filesystem, runtime thread, third-party widget library, or unportable desktop service to remove. The main unknown is performance on the actual A7, not whether the code can be built for it.

## 9. Suggested next step and decision

Create a short-lived `metamodule/port-prototype` branch with one milestone: **load Corrupter on a current-firmware MetaModule at 48 kHz/block 16, show the static faceplate and dynamic status display, pass stereo audio, and record CPU plus free memory for the worst corruption modes**. Keep the first branch limited to build metadata/assets and the three host fixes (string updates, 16-frame cadence, deferred/right-sized buffer). Do not invest in release automation until that evidence exists.

Decision table:

| Question | Current answer |
| --- | --- |
| Can the existing source compile against the official SDK? | **Yes, proven** by the out-of-tree SDK 2.3 ARM package build |
| Does Corrupter require a native MetaModule DSP rewrite? | **No**; use the Rack adapter |
| Is a GUI rewrite required? | **No conceptual rewrite**; convert/bake static art and validate or lightly adapt the dynamic display |
| Is memory likely to fit? | **Yes for one or several instances**, but remove discovery-time/full-96-kHz allocation and measure shared headroom |
| Is CPU known to fit? | **Unknown until hardware profiling**; source characteristics make this the principal risk |
| Can Neal realistically port it? | **Yes.** Estimated 3-6 focused days to hardware-tested beta, plus 1-2 days for release polish |
| Should Neal promise a release now? | **Not yet.** Promise a prototype/measurement pass first |

**Bottom line:** accept the feasibility request and prototype it. Corrupter is unusually well positioned because its DSP is already standalone and embedded-tested, its Rack surface is conventional, and current sources already pass the MetaModule cross-compile/link/package gate. Hardware CPU, RAM, UI, and audio evidence—not more desktop research—should determine the final release commitment.
