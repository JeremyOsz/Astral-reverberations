# Astral Reverberations

Astral Reverberations is a learning-oriented C++ audio plugin: a JUCE/CMake VST3 and standalone app that combines a tape-style multi-head delay, a modulated stereo reverb, a ten-voice planetary drone, and a deterministic astrology-inspired modulation engine.

The astrology engine is intentionally offline. It uses approximate orbital periods and musical mappings rather than live ephemeris calls, so the plugin stays deterministic and DAW-safe.

## What You Get

- **Tape-style delay** — multi-tap stereo buffer with wow/flutter and soft saturation (`Source/dsp/AstralReverbDelay.cpp`).
- **Modulated reverb** — compact feedback-delay network with damping and slow stereo movement.
- **Planetary drone** — ten harmonic layers keyed to Sun through Pluto, with per-planet wave shape and filter.
- **Orbit map** — drag planets, mute layers, pluck the reverb tank, and read tail energy on the rings.
- **Capture** — arm input recording, then hold planet keys to excite orbit-scaled tail buffers (see below).
- **Euclidean plucks** — rhythmic sends into the reverb tank from the astro clock.
- **Historical presets** — 30 fixed UTC moments (Battle of Hastings through September 11) each with a suggested key; recall switches to Manual Date mode.
- **Eight macros** — Mix, Tail Memory, Voices, Orbit Mod, Echo, Reverb, Pluck, and Root Drift fold many underlying DSP parameters.

Inspector astro modes: **Now**, **Manual Date**, and **Simulated Orbit** (`astro_speed`).

## Repository Layout

| Path | Role |
|------|------|
| `Source/astro` | Deterministic planetary simulation, aspects, historical presets, astro-to-DSP mapping |
| `Source/dsp` | Reusable delay, reverb, and drone engine (no JUCE dependency) |
| `Source/plugin` | JUCE `AudioProcessor`, APVTS parameters, MIDI, editor |
| `tests` | Portable C++ tests for astro, DSP, macros, and MIDI helpers |
| `docs` | Learning notes, workflows, and porting plans — see [`docs/README.md`](docs/README.md) |

## Performance Controls (Quick Reference)

Editor focus is required for the keyboard row; MIDI duplicates several gestures. Full detail: [`docs/midi-control.md`](docs/midi-control.md).

| Gesture | Keyboard | MIDI |
|---------|----------|------|
| Orbit-tail capture (per planet) | `1`–`0` hold | Notes **36–45** (auto-arms capture) |
| Reverb tank pluck | `Q`–`P` | Notes **60–69** (velocity → wet) |
| Planet mute | `A`–`;` toggle | — |
| Organ layer gate | `A`–`;` hold when **Organ** on | CC **35** toggles Organ |
| Drone gate | **Drone On** or held notes | Held notes (not 60–69), CC **64** sustain |
| Macros | Inspector + bottom sliders | CC **20–27** |

**Organ mode:** with **Organ** enabled, the `A`–`;` row momentarily gates each planet’s drone layer instead of toggling mute. Tail capture (`1`–`0` / notes 36–45) is unchanged.

## Capture

Capture is an armed input-recording mode. For audible orbit-tail capture, turn on **Capture**, raise **Tail Memory**, and hold a planet number key (`1`–`0`) or MIDI note **36–45** while audio is entering the plugin.

See [`docs/capture-workflow.md`](docs/capture-workflow.md) for the signal path, orbit decay lengths, and troubleshooting.

## MIDI and Host Automation

MIDI opens the drone gate, sets root from the lowest held note (unless **Manual Root**), maps CC **20–35** to macros and performance toggles, and exposes dedicated note ranges for tails and tank plucks. Pitch bend shifts root ±2 semitones.

Planet longitude offsets, mutes, sign routing, and astro mode are editor/state properties today — not on the MIDI wire. See [`docs/midi-control.md`](docs/midi-control.md) for the APVTS map, legacy parameter IDs, and roadmap CC layout.

## Build The Core Tests

Core tests do not need JUCE or network access:

```sh
cmake -S . -B build-core -DASTRAL_BUILD_PLUGIN=OFF -DASTRAL_BUILD_TESTS=ON
cmake --build build-core
ctest --test-dir build-core --output-on-failure
```

## Build The Plugin

The plugin target fetches JUCE through CMake `FetchContent`:

```sh
cmake -S . -B build-plugin -DASTRAL_BUILD_PLUGIN=ON -DASTRAL_BUILD_TESTS=ON
cmake --build build-plugin --target AstralReverberationsPlugin_VST3
```

Standalone (microphone permission for capture):

```sh
cmake --build build-plugin --target AstralReverberationsPlugin_Standalone
```

On macOS, a full Xcode install may be required for plugin packaging. Command Line Tools are often enough for core C++ builds, but JUCE plugin targets commonly expect the full developer toolchain.

## Learning Path

1. [`docs/learning/vst-basics.md`](docs/learning/vst-basics.md)
2. [`docs/astro-mapping.md`](docs/astro-mapping.md)
3. [`docs/capture-workflow.md`](docs/capture-workflow.md)
4. [`docs/dsp-design.md`](docs/dsp-design.md)
5. [`docs/midi-control.md`](docs/midi-control.md)
6. `Source/astro/AstrologyEngine.cpp`
7. `Source/dsp/AstralReverbDelay.cpp` and `Source/dsp/PlanetaryDrone.cpp`
8. `Source/plugin/PluginProcessor.cpp`
