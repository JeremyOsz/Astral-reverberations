# Signal Path

This document describes the current audio-thread signal flow. It is meant as a debugging map for gain, crackle, capture, and MIDI-performance issues.

Primary source files:

- `Source/plugin/PluginProcessor.cpp`
- `Source/dsp/PlanetaryDrone.cpp`
- `Source/dsp/AstralReverbDelay.cpp`
- `Source/plugin/MacroMapping.cpp`

## Top-Level Audio Block

`AstralReverberationsAudioProcessor::processBlock` owns the per-block order. MIDI and control state are read first, then the internal drone/capture engine is rendered into a working stereo buffer, then the delay/reverb engine processes that result.

```mermaid
flowchart LR
    HostIn["Host / standalone audio input"]
    MidiIn["MIDI + keyboard state"]
    InputCopy["inputCopyBuffer\nlive input reference"]
    Work["processingBuffer\ninput + generated sound"]
    MidiState["Held notes, root, gate,\nCC macros, tail gates"]
    Astro["Astro frame\nNow / Manual / Simulated"]
    Drone["PlanetaryDrone\noscillators + capture + tails"]
    Euclid["Euclidean pluck scheduler"]
    TapQueue["pending reverb tank taps"]
    StripInput["Remove live input\nif Input Monitor off"]
    Effect["AstralReverbDelay\ndelay + reverb + EQ + output"]
    HostOut["Host / standalone output"]
    Meters["Input/output meters\norbit tail levels"]

    HostIn --> InputCopy
    HostIn --> Work
    MidiIn --> MidiState
    MidiState --> Drone
    Astro --> Drone
    InputCopy --> Drone
    Work --> Drone
    Drone --> Work
    Drone --> Meters
    Astro --> Euclid
    MidiState --> Euclid
    Euclid --> TapQueue
    TapQueue --> Effect
    Work --> StripInput
    InputCopy --> StripInput
    StripInput --> Effect
    Astro --> Effect
    Effect --> HostOut
    Effect --> Meters
```

Important detail: when `Input Monitor` is off, the live input is subtracted from `processingBuffer` before the effect block. The remaining signal is the internal instrument signal, so the effect dry path is kept open for drone and tails.

## Control And Root Path

MIDI and UI controls do not directly write audio. They update parameter/state values that are sampled by the processor at block boundaries.

```mermaid
flowchart TB
    UI["UI controls\nbuttons, sliders, mode selectors"]
    MIDI["MIDI input\nnotes, CC, pitch bend"]
    APVTS["APVTS parameters\nmacros, toggles, sound controls"]
    Runtime["Runtime state\nheld notes, sustain,\nmanual tail gates"]
    Root["Root resolver\nmanual root or lowest held note\nplus pitch bend"]
    Gate["Gate resolver\nDrone On, MIDI notes,\nsustain, gate mode"]
    Macro["MacroMapping\nSubstance, Choir, Mneme,\nEphemeris, Fate, Void, Ring, Root Drift"]
    DroneParams["PlanetaryDroneParameters"]
    EffectParams["AstralReverbDelayParameters"]

    UI --> APVTS
    MIDI --> APVTS
    MIDI --> Runtime
    APVTS --> Root
    Runtime --> Root
    APVTS --> Gate
    Runtime --> Gate
    APVTS --> Macro
    Macro --> DroneParams
    Macro --> EffectParams
    Root --> DroneParams
    Gate --> DroneParams
    APVTS --> DroneParams
    APVTS --> EffectParams
```

Performance mappings that affect audio routing:

- Held MIDI notes open the drone gate and can set root from the lowest held note.
- CC64 acts as sustain gate.
- MIDI notes `36-45` gate per-planet capture tails and auto-arm capture.
- MIDI notes `60-69` trigger reverb tank plucks.
- CC `20-29` drive the main macro/performance controls.
- CC `30-35` toggle Drone, Capture, Freeze, Euclidean, Manual Root, and Organ.
- Pitch bend offsets drone root and pluck frequency by the configured bend range.

## PlanetaryDrone Internals

`PlanetaryDrone` is both the oscillator instrument and the capture-tail source. It renders additively into the existing working buffer.

```mermaid
flowchart LR
    WorkIn["processingBuffer input\nlive input already copied"]
    CaptureIn["captureInput\ninputCopyBuffer"]
    Params["PlanetaryDroneParameters"]
    Frame["AstroDroneFrame\nplanet layers"]
    GateEnv["Gate envelope"]
    PlanetLoop["Per-planet layer loop"]
    Osc["Oscillator voice\nwave shape, detune,\nfilter, pan"]
    CaptureScan["Short capture buffer scan\nsubtle moving tape-head layer"]
    ManualTail["ManualTailBuffer\nper-planet keyed feedback tail"]
    CaptureWrite["Capture buffer write\nexternal input or generated drone"]
    Mix["Drone mix\nlayer scale + aspect gain"]
    WorkOut["processingBuffer output\ninput + drone + tails"]
    TailMeters["Tail level meters\nfor orbit arcs"]

    WorkIn --> PlanetLoop
    CaptureIn --> PlanetLoop
    Params --> GateEnv
    Params --> PlanetLoop
    Frame --> PlanetLoop
    GateEnv --> Osc
    PlanetLoop --> Osc
    PlanetLoop --> CaptureScan
    PlanetLoop --> ManualTail
    Osc --> Mix
    CaptureScan --> Mix
    ManualTail --> Mix
    ManualTail --> TailMeters
    Mix --> WorkOut
    Mix --> CaptureWrite
    CaptureIn --> CaptureWrite
    CaptureWrite --> CaptureScan
```

The drone path has three audible ingredients:

- Oscillator drone voices from planetary harmonic layers.
- Short captured-input scans, enabled by `Capture` and scaled by `Mneme`.
- Manual planet tails, excited by number keys or MIDI tail notes while capture is armed.

## Manual Orbit Tail Loop

Manual tails are separate from the main delay/reverb freeze. They are per-planet feedback loops inside `PlanetaryDrone`.

```mermaid
flowchart LR
    KeyGate["Planet tail gate\n1-0 or MIDI 36-45"]
    SourceSelect["Tail source\nexternal input if present,\notherwise planet voice"]
    Size["Tail Size\nscales tail seconds"]
    Regen["Regen\nfeedback lift"]
    Mode["Tail Mode\nLIM / DIST / SHIM"]
    DelayRead["Smoothed interpolated\nfeedback read"]
    FeedbackShape["Mode shaping\nlimiter, distortion,\nor shimmer injection"]
    Write["Circular tail buffer write"]
    TailOut["Tail output\nenvelope + output drive"]
    DroneMix["Back to drone mix"]

    KeyGate --> SourceSelect
    SourceSelect --> Write
    Size --> DelayRead
    Regen --> FeedbackShape
    Mode --> FeedbackShape
    Write --> DelayRead
    DelayRead --> FeedbackShape
    FeedbackShape --> Write
    DelayRead --> TailOut
    TailOut --> DroneMix
```

Crackle-sensitive points:

- `Tail Size` changes the feedback read distance.
- The read distance is smoothed before interpolation.
- `Regen` can approach very high feedback values, so the write path is bounded with soft limiting.
- `DIST` mode is intentionally nonlinear and can sound gritty at high regen.

## Euclidean And Tank Plucks

The Euclidean engine does not directly write to the drone buffer. It schedules one-shot plucks into the delay/reverb tank.

```mermaid
flowchart LR
    EuclidOn["Euclidean enabled"]
    Rate["Rate"]
    PlanetPattern["Per-planet Euclidean pattern\nsteps, pulses, rotation"]
    Mutes["Planet mutes"]
    Wet["Euclidean wet/send"]
    Queue["pendingReverbTankTap\nlevel, pan, freq, wet, tone"]
    Tank["KarplusPluck\ninside AstralReverbDelay"]
    EffectInput["Delay/reverb input"]
    Display["Euclidean display state\nhits, active step, recent levels"]

    EuclidOn --> PlanetPattern
    Rate --> PlanetPattern
    Mutes --> PlanetPattern
    Wet --> Queue
    PlanetPattern --> Queue
    PlanetPattern --> Display
    Queue --> Tank
    Tank --> EffectInput
```

## Delay, Reverb, EQ, Output

`AstralReverbDelay` takes the post-drone working buffer and applies the main spatial instrument path.

```mermaid
flowchart LR
    In["Post-drone input"]
    DryGate["Dry gain\nInput Monitor flag"]
    TankTap["Reverb tank tap\nQ-P, MIDI 60-69,\nEuclidean"]
    DelayReads["Stereo delay reads\nmain, short, cross taps"]
    NodeTaps["Astro node taps\nfrom delayTapDensity"]
    FeedbackFilter["Feedback low-pass"]
    Freeze["Freeze switch\nrecirculate or write input"]
    SoftClip["Soft clip feedback write"]
    Reverb["Compact FDN reverb\nmodulated + damped"]
    WetMix["Delay + reverb wet mix"]
    EQ["Low / Mid / High"]
    Output["Output gain\nhost output"]

    In --> DryGate
    In --> TankTap
    TankTap --> DelayReads
    TankTap --> WetMix
    DelayReads --> NodeTaps
    NodeTaps --> FeedbackFilter
    FeedbackFilter --> Freeze
    In --> Freeze
    Freeze --> SoftClip
    SoftClip --> DelayReads
    DelayReads --> Reverb
    DelayReads --> WetMix
    Reverb --> WetMix
    DryGate --> WetMix
    WetMix --> EQ
    EQ --> Output
```

DSP safety points:

- Delay time is smoothed per sample.
- Delay reads use interpolation.
- Feedback is low-pass filtered.
- Feedback writes are soft-clipped.
- Non-finite samples are sanitized.
- `Freeze` stops new dry input from entering the feedback loop.

## Practical Debug Map

Use this table when a symptom appears.

| Symptom | Most likely area | What to inspect |
|---------|------------------|-----------------|
| Drone is quiet with Input Monitor off | Top-level dry routing | `PluginProcessor.cpp` live-input subtraction and effect `inputMonitor` handoff |
| Crackle while moving tail controls | Manual orbit tail loop | Tail read smoothing, interpolation, regen value, `DIST` mode |
| Capture tails are not audible | `PlanetaryDrone` manual tails | `Capture`, Mneme/capture level, tail gate key, planet mute, input level |
| Euclidean display moves but no plucks | Euclidean scheduler | `euclid_enable`, rate, send/wet, planet mutes, pending tap queue |
| Plucks work but drone does not gate | Control/root path | Held notes, sustain, `Drone On`, gate mode, manual root |
| Output meter low despite active sound | Delay/reverb/output block | Wet/dry mix, Low/Mid/High, Output macro, limiter state |

