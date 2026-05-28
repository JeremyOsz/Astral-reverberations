#include "plugin/MacroMapping.h"

#include <algorithm>
#include <cmath>

namespace astral::plugin {
namespace {

float clamp01(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

float curvePow(float t, float exponent)
{
    return std::pow(clamp01(t), exponent);
}

} // namespace

ResolvedMacroParameters resolveMacroParameters(const MacroControls& macros)
{
    const float substance = clamp01(macros.substance);
    const float mneme = clamp01(macros.mneme);
    const float choir = clamp01(macros.choir);
    const float ephemeris = clamp01(macros.ephemeris);
    const float fate = clamp01(macros.fate);
    const float voidAmount = clamp01(macros.void_);
    const float pulse = clamp01(macros.pulse);
    const float root = clamp01(macros.root);

    ResolvedMacroParameters resolved;

    resolved.mix = lerp(0.10f, 0.68f, substance);
    resolved.delayLevel = lerp(0.18f, 0.82f, substance * 0.72f + mneme * 0.28f);
    resolved.reverbLevel = lerp(0.22f, 0.88f, substance * 0.55f + voidAmount * 0.45f);
    resolved.outputGain = lerp(0.62f, 1.42f, substance);

    resolved.captureLevel = lerp(0.04f, 0.88f, mneme);
    resolved.delayLevel = std::clamp(resolved.delayLevel + mneme * 0.12f, 0.0f, 1.0f);

    resolved.harmonicSpread = lerp(0.30f, 0.92f, choir);
    resolved.aspectDepth = lerp(0.16f, 0.84f, choir);

    resolved.astroAmount = lerp(0.02f, 0.96f, ephemeris);
    resolved.modDepth = lerp(0.06f, 0.78f, ephemeris);

    resolved.delayTimeMs = lerp(90.0f, 1680.0f, curvePow(fate, 0.48f));
    resolved.feedback = lerp(0.16f, 0.72f, fate);
    resolved.wowFlutter = lerp(0.02f, 0.58f, fate);
    resolved.drive = lerp(0.04f, 0.58f, fate);
    resolved.feedback = std::min(resolved.feedback, lerp(0.82f, 0.38f, mneme));

    resolved.space = lerp(0.18f, 0.90f, voidAmount);
    resolved.tone = lerp(0.78f, 0.28f, voidAmount);
    resolved.reverbLevel = std::clamp(resolved.reverbLevel * lerp(0.72f, 1.18f, voidAmount), 0.0f, 1.0f);

    resolved.pluckTimbre = lerp(0.22f, 0.86f, pulse);
    resolved.pluckOctave = std::round(lerp(-1.0f, 1.0f, pulse));

    resolved.rootOffsetSemitones = lerp(-12.0f, 12.0f, root);

    return resolved;
}

} // namespace astral::plugin
