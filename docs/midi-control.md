# MIDI and Performance Control

Astral Reverberations accepts MIDI input (`NEEDS_MIDI_INPUT` in CMake) but uses it narrowly today. Most performance gestures live in the **editor keyboard** and **mouse orbit map**, not in the MIDI stream.

This document covers what MIDI does now, what is not MIDI-accessible, how host automation relates to MIDI CC, and a practical roadmap for better MIDI control.

## Current MIDI Behavior

Implementation: `Source/plugin/PluginProcessor.cpp` → `processMidiInput()`, with note/CC constants in `Source/plugin/MidiMapping.h`.

| MIDI message | Handled? | Effect |
|--------------|----------|--------|
| Note On (general) | Yes | Opens drone gate; lowest held note sets root |
| Note On **36–45** | Yes | Momentary planet orbit-tail gate (auto-arms capture) |
| Note On **60–69** | Yes | One-shot reverb tank pluck (velocity → wet) |
| Note Off | Yes | Clears held note / tail gate |
| All Notes Off / All Sound Off | Yes | Clears held notes and tail gates |
| CC **20–27** | Yes | Macros Mix … Root Drift |
| CC **28–29** | Yes | Euclidean rate and wet |
| CC **30–34** | Yes | Drone On, Capture, Freeze, Euclidean enable, Manual Root |
| CC **64** | Yes | Sustain pedal holds drone gate |
| Pitch Bend | Yes | ±2 semitones on drone root and plucks |
| Channel Pressure / Poly Aftertouch | No | Ignored |
| Program Change | No | Ignored |

### Drone gate

The planetary drone oscillators and their harmonic layers only sound when the drone **gate** is open:

```
gate open = any held MIDI note OR drone_hold ("Drone On") parameter
```

`drone_hold` is the standalone-friendly substitute when no keyboard controller is connected. See `docs/capture-workflow.md`.

### Root pitch

`getDroneRootNote()` chooses the frequency anchor for drone layers, orbit-tail excitation, and Euclidean reverb taps:

| Condition | Root source |
|-----------|-------------|
| `root_lock` ("Manual Root") is on | `root_note` parameter (default MIDI 45 = A2) |
| No MIDI notes held (gate closed via MIDI) | `root_note` |
| MIDI gate open and `root_lock` off | **Lowest** held note number (scan 0→127, first match wins) |

`macro_root` ("Root Drift") adds ±12 semitones on top via `resolveMacroParameters()`.

**Root selection:** lowest held gate note (pluck notes 60–69 do not hold the gate).

### What MIDI does *not* control

These features exist only in the UI / editor keyboard:

| Feature | UI / keyboard access | MIDI today |
|---------|---------------------|------------|
| Planet orbit-tail capture gates | Number keys `1`–`0` | Notes **36–45** (capture auto-arms) |
| Reverb tank plucks (orbit ring) | `Q`–`P` or click orbit ring | Notes **60–69** |
| Planet mute | `A`–`;` or double-click planet | None |
| Planet longitude drag offset | Drag planet node | None |
| Zodiac sign effect toggle / cycle | Click sign marker | None |
| Astro mode (Now / Manual / Simulated) | Inspector combo | None |
| Historical date preset | Inspector menu | None (preset sets `root_note` only) |
| Orbit speed buttons | Slow / Normal / Fast | None (`astro_speed` is automatable) |

Keyboard map (editor focus required; not available from MIDI):

| Keys | Planets (Sun → Pluto) | Action |
|------|----------------------|--------|
| `Q W E R T Y U I O P` | 1–10 | One-shot reverb tank tap |
| `1 2 3 4 5 6 7 8 9 0` | 1–10 | Momentary orbit-tail capture gate |
| `A S D F G H J K L ;` | 1–10 | Toggle mute (edge on key down) |

## Host Automation vs Native MIDI CC

All parameters are exposed through JUCE **APVTS** (`createParameterLayout()`). A DAW can map MIDI CC to those parameters via MIDI learn / generic remote—**but only parameters that the audio thread actually reads will change sound**.

### Parameters that affect audio (safe for MIDI learn)

| ID | Label | Role |
|----|----------|------|
| `macro_substance` | Mix | Wet mix, echo/reverb levels, output level (via macro resolver) |
| `macro_mneme` | Tail Memory | Capture level, feedback safety |
| `macro_choir` | Voices | Harmonic spread and aspect bloom |
| `drone_level` | Drone | Planetary oscillator level (0-1.5x) |
| `pluck_level` | Pluck | Karplus ring pluck level (0-1.5x) |
| `macro_ephemeris` | Orbit Mod | Astro modulation depth |
| `macro_fate` | Echo | Delay time, feedback, wow, drive |
| `macro_void` | Reverb | Size, tone, reverb emphasis |
| `macro_pulse` | Pluck | Pluck timbre, octave |
| `macro_root` | Root Drift | ±12 semitone root offset |
| `delay_level` | Level | Direct echo output level offset from the Mix and Echo macros |
| `delay_time` | Time | Direct echo time offset from the Echo macro |
| `feedback` | Feedback | Direct echo regeneration offset from the Echo macro |
| `wow_flutter` | Wobble | Direct tape-style modulation offset from the Echo macro |
| `drive` | Drive | Direct tape-style echo saturation offset from the Echo macro |
| `reverb_level` | Level | Direct reverb output level offset from the Mix and Reverb macros |
| `space` | Size | Direct reverb size offset from the Reverb macro |
| `tone` | Tone | Shared echo/reverb brightness offset from the Reverb macro |
| `reverb_decay` | Regen | Reverb tank regeneration |
| `reverb_damping` | Damp | Reverb tank damping |
| `reverb_mod` | Motion | Reverb tank stereo motion |
| `pluck_send` | Tap In | Host/MIDI-learn send amount from orbit taps and Euclidean plucks into the tank |
| `tail_mode` | Mode | Capture-tail character: `LIM`, `DIST`, or `SHIM` |
| `tail_size` | Length | Capture-tail length multiplier |
| `tail_regen` | Feedback | Capture-tail regeneration |
| `euclid_enable` | Euclidean Plucks | On/off |
| `euclid_rate` | Rate | Pluck tempo |
| `euclid_wet` | Wet | Pluck send level |
| `drone_hold` | Drone On | Gate without MIDI |
| `capture_enable` | Capture | Arms capture buffers |
| `organ_mode` | Organ | A-; row gates planets instead of mute |
| `root_note` | Root (combo) | Manual root MIDI note 24–84 |
| `root_lock` | Manual Root | Ignore MIDI for root |
| `freeze` | Freeze | Main delay/reverb freeze |
| `input_monitor` | Input Monitor | Dry input in wet path |
| `filter_route` | Filter Route | Bypass, dry branch, or wet return routing |
| `filter_mode` | Filter Mode | LP, BP, HP, or notch response |
| `filter_cutoff` | Cutoff | Routed stereo multimode filter cutoff |
| `filter_resonance` | Q | Routed stereo multimode filter resonance |
| `filter_radiate` | Radiate | Stereo cutoff spread/asymmetry |
| `astro_speed` | (speed buttons) | Simulated orbit rate |

Macro → DSP mapping detail: `Source/plugin/MacroMapping.cpp` and `docs/dsp-design.md`. The direct echo/reverb/tail controls are read by `readParameters()` as offsets or dedicated controls, so they are safe for MIDI learn.

### Parameters in state but not wired to `processBlock`

These exist for preset compatibility / future use. **MIDI CC mapped to them will not change audio today:**

`pluck_timbre`, `pluck_octave`, `harmonic_spread`, `aspect_depth`, `root_offset`, `master_eq_low`, `master_eq_mid`, `master_eq_high`.

The `master_eq_*` parameters are retained for old session/preset state compatibility. The visible output tone controls are now `filter_route`, `filter_mode`, `filter_cutoff`, `filter_resonance`, and `filter_radiate`.

The direct wet/effect trim parameters (`mix`, `mod_depth`, `astro_amount`, `capture_level`, `output_gain`) are applied directly in `readParameters()`.

### State not in APVTS (no MIDI learn path)

Stored in processor atomics / ValueTree properties; lost unless custom MIDI mapping is added:

- Per-planet longitude offsets (`setManualPlanetLongitude`)
- Per-planet mutes
- Sign effect enable + assignment remapping
- `AstroMode` and `manualJulianDay` (partially in ValueTree properties `astroMode`, `manualJulianDay`)

## Default MIDI Map (implemented)

| Control | MIDI | Target |
|---------|------|--------|
| Drone gate | Held notes (not 60–69) or CC 64 ≥ 64 | Gate |
| Manual root | CC 34 ≥ 64 | `root_lock` |
| Tail capture | Notes **36–45** | Planet tail gates; capture auto-arms |
| Tank pluck | Notes **60–69** | `tapOrbitReverbTank` (velocity → wet) |
| Mix … Root Drift | CC **20–27** | `macro_substance` … `macro_root` |
| Euclidean rate / wet | CC **28–29** | `euclid_rate`, `euclid_wet` |
| Drone On / Capture / Freeze / Euclidean | CC **30–33** | Toggles (≥ 64 on) |
| Organ mode | CC **35** | `organ_mode` |
| Pitch bend | Wheel | ±2 semitones on root |

### Roadmap (not yet in code)

- CC **40–49** / **50–59**: per-planet tail gates and mutes  
- CC **70–81**: zodiac sign effects  
- NRPN: planet longitude offsets  
- Program Change: historical preset recall

## Testing MIDI

Core tests (`tests/DspTests.cpp`) cover gate open/closed at the DSP layer with `PlanetaryDroneParameters::gate`, not full `processBlock` MIDI parsing.

Manual check in a DAW:

1. Insert plugin on audio track with MIDI input.
2. Hold a single note → status bar should show `Gate Live` and root name (unless Manual Root).
3. Toggle Manual Root → root should stay on combo selection.
4. Enable Capture (or hold notes **36–45**) → orbit tails respond on those notes.
5. Notes **60–69** → reverb tank plucks; velocity scales intensity.

## Related Docs

- `docs/capture-workflow.md` — tail capture and number keys  
- `docs/dsp-design.md` — drone, delay, reverb signal path  
- `docs/astro-mapping.md` — what the sky engine modulates  
- `Source/plugin/MidiMapping.h` — note/CC constants and helpers  
- `Source/plugin/PluginProcessor.cpp` — `processMidiInput`, `getDroneRootNote`, `createParameterLayout`
