#pragma once

#include "astro/AstroTypes.h"

namespace astral::astro {

class AstroMapper {
public:
    AstroModulationFrame mapSnapshot(const AstroSnapshot& snapshot) const;
    AstroDroneFrame mapDroneSnapshot(const AstroSnapshot& snapshot) const;
};

} // namespace astral::astro
