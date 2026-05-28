# Documentation

Learning notes and reference material for Astral Reverberations. Start at the [repository README](../README.md) for build instructions and a control quick reference.

## Core Concepts

| Document | Contents |
|----------|----------|
| [`astro-mapping.md`](astro-mapping.md) | Offline sky simulation, aspects, and how longitudes map to delay/reverb/drone behavior |
| [`dsp-design.md`](dsp-design.md) | Tape delay, FDN reverb, capture/tail buffers, and portable DSP rules |
| [`capture-workflow.md`](capture-workflow.md) | Arming capture, planet tail keys, Mneme gain staging, orbit-ring feedback |
| [`midi-control.md`](midi-control.md) | MIDI note/CC map, APVTS parameters, host automation caveats, hardware roadmap |

## Learning

| Document | Contents |
|----------|----------|
| [`learning/vst-basics.md`](learning/vst-basics.md) | JUCE plugin structure, APVTS, and how this project is organized |

## Porting Plans

These describe how the JUCE-free `AstralCore` library could be reused elsewhere:

| Document | Target |
|----------|--------|
| [`ports/max-rnbo.md`](ports/max-rnbo.md) | Max for Live / RNBO |
| [`ports/vcv-rack.md`](ports/vcv-rack.md) | VCV Rack module |
| [`ports/versio-daisy.md`](ports/versio-daisy.md) | Chase Bliss Versio / Electrosmith Daisy firmware |

## Source Cross-References

| Topic | Primary code |
|-------|----------------|
| Sky engine | `Source/astro/AstrologyEngine.cpp`, `Source/astro/AstroMapper.cpp` |
| Historical dates | `Source/astro/HistoricalPresets.cpp` |
| Delay / reverb | `Source/dsp/AstralReverbDelay.cpp` |
| Drone, capture, tails | `Source/dsp/PlanetaryDrone.cpp` |
| Macros | `Source/plugin/MacroMapping.cpp` |
| MIDI constants | `Source/plugin/MidiMapping.h` |
| Audio thread + MIDI | `Source/plugin/PluginProcessor.cpp` |
| Orbit UI + keyboard | `Source/plugin/PluginEditor.cpp` |
