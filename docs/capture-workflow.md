# Capture Workflow

Astral Reverberations has two capture paths. Both use live stereo input, but they behave differently.

## Quick Start

1. Send audio into the standalone app or plugin.
2. Turn on `Capture`.
3. Raise the **Mneme** macro (capture memory).
4. Hold a number key (`1`–`0`) or MIDI note **36–45** (C2–A2, Sun→Pluto) to feed a planet tail:
   - `1` Sun
   - `2` Moon
   - `3` Mercury
   - `4` Venus
   - `5` Mars
   - `6` Jupiter
   - `7` Saturn
   - `8` Uranus
   - `9` Neptune
   - `0` Pluto
5. Release the key and listen for the orbit tail to decay.

The orbit arc for that planet brightens while the captured tail has energy. Longer-orbit planets decay more slowly.

## What Capture Does

`Capture` arms input recording. It does not create sound by itself.

When `Capture` is on, incoming stereo audio is written into a short circular capture buffer. Planet layers can scan that buffer as moving tape heads. This path is blended into the drone voice and is intentionally subtle.

The louder, Sarum-inspired path is the planet tail capture. It needs two things at the same time:

- `Capture` is on.
- A planet number key or MIDI tail note (36–45) is held (MIDI tail notes auto-arm capture).

While the key is held, input excites that planet's reverb tail buffer. When the key is released, the buffer continues to ring out with a decay length based on the planet's orbit.

## Orbit Tail Lengths

Orbit lengths are log-scaled into practical musical reverb tails, roughly `1.5-18 seconds`.

Shorter-period bodies create shorter tails. Longer-period planets create longer tails. For example, Moon/Mercury tails are short and responsive, while Neptune/Pluto tails are longer and more accumulative.

## Why It Can Be Hard To Hear

The current capture implementation is conservative:

- **Mneme** macro controls both buffer scanning and orbit-tail excitation.
- Planet layer amplitude also scales the captured signal.
- The manual orbit-tail signal is mixed at a reduced gain before the main delay/reverb.
- The captured buffer scan path is only `22%` of `Capture * layer amplitude`.
- Muted planets contribute no capture, tail, drone, or node-delay signal.

For the clearest result, use a bright or percussive input, turn `Capture` up, hold a number key for a second or two, and choose a longer tail planet such as `7`, `8`, `9`, or `0`.

## Visual Feedback

The orbit rings show capture state:

- A faint arc shows the planet's maximum tail length.
- A brighter/thicker arc shows actual captured tail energy.
- The endpoint marker grows while the tail is ringing.
- A planet node ring appears while its number key is held.
- A slashed planet is muted and will not capture.

## Related Controls

- `Drone On`: opens the oscillator drone gate without MIDI.
- `Capture`: arms input capture and planet tail excitation.
- `Freeze`: holds the main delay/reverb feedback path, not the planet tail buffers.
- Number keys: momentary planet-tail capture gates.
- `Q`–`P`: one-shot reverb tank plucks per planet (see `docs/midi-control.md`).
- `A`–`;`: toggle planet mute (default), or hold to gate each planet drone layer when **Organ** is on.
- Double-click planet: mute/unmute that planet layer.
- `Reset Planet` / `Reset Orbits`: reset position offsets only; they do not clear captured audio.

For MIDI note/CC behavior, host automation, and a roadmap for hardware control, see `docs/midi-control.md`.
