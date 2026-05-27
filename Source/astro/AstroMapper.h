#pragma once

#include "astro/AstroTypes.h"

namespace astral::astro {

class AstroMapper {
public:
    /// Maps a planetary snapshot into delay/reverb modulation, node taps, and zodiac sign effect weights.
    AstroModulationFrame mapSnapshot(const AstroSnapshot& snapshot) const;

    /// Maps the same snapshot into drone harmonic layers, aspect energies, and orbit-tail lengths.
    AstroDroneFrame mapDroneSnapshot(const AstroSnapshot& snapshot) const;
};

} // namespace astral::astro
