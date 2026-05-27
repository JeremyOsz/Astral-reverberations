#pragma once

#include "astro/AstroTypes.h"

#include <chrono>

namespace astral::astro {

// The engine is deliberately deterministic and offline. It borrows the idea of
// planetary longitude from astrology, but uses approximate orbital periods so a
// plugin session never depends on network requests or API keys.
class AstrologyEngine {
public:
    static constexpr double J2000JulianDay = 2451545.0;

    static float normalizeDegrees(float degrees);
    static float shortestAngularDistance(float aDegrees, float bDegrees);
    static ZodiacSign signForLongitude(float longitudeDegrees);
    static Element elementForSign(ZodiacSign sign);
    static Quality qualityForSign(ZodiacSign sign);
    static double julianDayFromUnixSeconds(double unixSeconds);
    static double unixSecondsFromSystemClock();

    AstroSnapshot snapshotForJulianDay(double julianDay) const;
    AstroSnapshot snapshotForUnixSeconds(double unixSeconds) const;
    std::vector<AspectEvent> detectAspects(const AstroSnapshot& snapshot) const;

};

} // namespace astral::astro
