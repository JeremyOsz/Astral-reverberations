#include "plugin/MacroMapping.h"

#include <cmath>
#include <iostream>

namespace {

void requireNear(float actual, float expected, float tolerance, const char* message)
{
    if (std::abs(actual - expected) > tolerance) {
        std::cerr << message << " (expected " << expected << ", got " << actual << ")\n";
        std::exit(1);
    }
}

} // namespace

void runMacroMappingTests()
{
    astral::plugin::MacroControls defaults;
    const auto resolved = astral::plugin::resolveMacroParameters(defaults);

    requireNear(resolved.mix, 0.35f, 0.08f, "default substance maps near legacy wet mix");
    requireNear(resolved.captureLevel, 0.35f, 0.08f, "default mneme maps near legacy capture level");
    requireNear(resolved.droneLevel, 0.45f, 0.10f, "default choir maps near legacy drone level");
    requireNear(resolved.rootOffsetSemitones, 0.0f, 0.5f, "centered root macro is near zero semitones");

    astral::plugin::MacroControls hotMneme = defaults;
    hotMneme.mneme = 1.0f;
    const auto mnemeHot = astral::plugin::resolveMacroParameters(hotMneme);
    requireNear(mnemeHot.feedback, 0.38f, 0.02f, "high mneme caps feedback for tail safety");
}
