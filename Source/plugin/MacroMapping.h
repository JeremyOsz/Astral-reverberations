#pragma once

namespace astral::plugin {

/// User-facing macro controls in the range 0..1 (except root, which maps to semitone offset).
struct MacroControls {
    float substance = 0.42f;
    float mneme = 0.38f;
    float choir = 0.52f;
    float ephemeris = 0.5f;
    float fate = 0.48f;
    float void_ = 0.54f;
    float pulse = 0.58f;
    float root = 0.5f;
};

/// Granular targets derived from macros for DSP and orbit performance.
struct ResolvedMacroParameters {
    float mix = 0.35f;
    float delayLevel = 0.45f;
    float reverbLevel = 0.55f;
    float delayTimeMs = 420.0f;
    float feedback = 0.42f;
    float space = 0.55f;
    float tone = 0.55f;
    float modDepth = 0.35f;
    float wowFlutter = 0.25f;
    float drive = 0.15f;
    float astroAmount = 0.5f;
    float pluckTimbre = 0.5f;
    float pluckOctave = 0.0f;
    float outputGain = 1.0f;
    float captureLevel = 0.35f;
    float harmonicSpread = 0.65f;
    float aspectDepth = 0.5f;
    float rootOffsetSemitones = 0.0f;
};

/// Maps poetic macros to underlying tone parameters with non-linear curves.
ResolvedMacroParameters resolveMacroParameters(const MacroControls& macros);

} // namespace astral::plugin
