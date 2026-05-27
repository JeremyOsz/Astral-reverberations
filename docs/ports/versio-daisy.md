# Versio/Daisy Port Plan

Noise Engineering Versio modules support custom firmware development through libDaisy. This repo should treat Versio/Daisy as a later embedded target, not as v1.

## Feasibility

The portable DSP core is close in shape to firmware code, but a Daisy port must account for:

- fixed memory budgets
- SDRAM speed and delay-line size
- no heap allocation in the audio callback
- hardware-specific ADC/control smoothing
- stereo codec block size
- firmware flashing and recovery workflow

## Mapping

- Pot 1: Mix
- Pot 2: Delay Time
- Pot 3: Feedback
- Pot 4: Space
- Pot 5: Tone
- Pot 6: Astro Amount or Orbit Speed
- Switches: Astro Mode, Freeze, Delay/Reverb emphasis

## Implementation Notes

Use fixed-size delay buffers and consider DaisySP utilities such as delay lines and stereo reverb references as starting points. The plugin's current dynamic buffers are acceptable on desktop but should become statically allocated or explicitly placed in external memory for firmware.

