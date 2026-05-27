# VCV Rack Port Plan

A VCV Rack module should reuse `Source/astro` and `Source/dsp` with a Rack adapter around it.

## Module Controls

- Knobs: Mix, Delay, Reverb, Time, Feedback, Space, Tone, Mod, Wow, Drive, Astro, Orbit, Output.
- Switches: Freeze, Astro Mode.
- CV inputs: Time, Feedback, Space, Astro Amount, Freeze gate.
- Audio: stereo in, stereo out.

## Rack-Specific Behavior

- Convert Rack voltages to normalized parameter values.
- Use Rack's `process(const ProcessArgs& args)` to process one frame at a time or collect small blocks.
- Follow Rack voltage standards: audio around +/-5V, modulation around 0-10V or +/-5V depending on input label.

## Build

Use the official Rack SDK helper to create the plugin skeleton, then link or copy the portable core. The Rack adapter should own only Rack widgets, ports, lights, and voltage scaling.

