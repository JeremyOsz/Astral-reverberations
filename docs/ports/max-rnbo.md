# Max/RNBO Port Plan

The fastest Max-style route is to reproduce the core signal flow in RNBO:

- stereo input
- multi-tap delay with interpolated `delay` reads
- low-pass feedback loop
- soft clipping
- four-line modulated reverb network
- parameter inputs matching the VST names
- a data or message inlet for `AstroModulationFrame`

## Suggested Patch Structure

- `p astro_control`: receives normalized values for delay drift, bloom, size, damping, shimmer, and stereo motion.
- `rnbo~ astral_core`: contains delay/reverb DSP.
- host Max patch: handles manual dates, simulated orbit stepping, presets, and UI.

## Export Path

RNBO can export audio plugins and portable C++ code, but the native JUCE implementation remains the reference for this repo. Treat RNBO as a patching mockup and comparison tool unless the project later chooses Max as the primary authoring environment.

