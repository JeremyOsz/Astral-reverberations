# Astral Reverberations

Astral Reverberations is a learning-oriented C++ audio plugin project. The first target is a JUCE/CMake VST3 that combines a tape-style multi-head delay, a modulated stereo reverb, and a deterministic astrology-inspired modulation engine.

The astrology engine is intentionally offline. It uses approximate orbital periods and musical mappings rather than live ephemeris calls, so the plugin remains deterministic and safe inside a DAW.

## Repository Layout

- `Source/astro`: deterministic planetary simulation, aspect detection, and astro-to-DSP mapping.
- `Source/dsp`: reusable delay/reverb engine with no JUCE dependency.
- `Source/plugin`: JUCE `AudioProcessor`, parameters, state, and a simple editor.
- `tests`: portable C++ tests for the core engine.
- `docs`: learning notes and porting plans for Max/RNBO, VCV Rack, and Versio/Daisy.

## Capture

Capture is an armed input-recording mode. For audible orbit-tail capture, turn on `Capture` and hold a planet number key (`1-0`) while audio is entering the plugin. See `docs/capture-workflow.md` for the full signal path and troubleshooting notes.

## MIDI and Performance Control

MIDI input currently opens the drone gate and (optionally) sets the harmonic root from held notes. Planet tails, tank plucks, mutes, and orbit edits are keyboard/mouse-only. See `docs/midi-control.md` for the full parameter map, legacy automation IDs, and a recommended CC/note layout for better hardware control.

## Build The Core Tests

The core tests do not need JUCE or network access:

```sh
cmake -S . -B build-core -DASTRAL_BUILD_PLUGIN=OFF -DASTRAL_BUILD_TESTS=ON
cmake --build build-core
ctest --test-dir build-core --output-on-failure
```

## Build The Plugin

The plugin target fetches JUCE through CMake `FetchContent`.

```sh
cmake -S . -B build-plugin -DASTRAL_BUILD_PLUGIN=ON -DASTRAL_BUILD_TESTS=ON
cmake --build build-plugin --target AstralReverberationsPlugin_VST3
```

On macOS, a full Xcode install may be required for plugin builds. Command Line Tools are enough for many core C++ builds, but JUCE plugin packaging commonly expects the full developer toolchain.

## Learning Path

Start with:

1. `docs/learning/vst-basics.md`
2. `docs/astro-mapping.md`
3. `docs/capture-workflow.md`
4. `docs/dsp-design.md`
5. `Source/astro/AstrologyEngine.cpp`
6. `Source/dsp/AstralReverbDelay.cpp`
7. `Source/plugin/PluginProcessor.cpp`
