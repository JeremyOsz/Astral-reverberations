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

    /// Wraps any degree value into the inclusive/exclusive range [0, 360).
    static float normalizeDegrees(float degrees);
    /// Returns the smaller angular distance between two longitudes in degrees.
    static float shortestAngularDistance(float aDegrees, float bDegrees);
    /// Converts a longitude into its zodiac sign bucket.
    static ZodiacSign signForLongitude(float longitudeDegrees);
    /// Returns the classical element associated with a zodiac sign.
    static Element elementForSign(ZodiacSign sign);
    /// Returns the classical quality associated with a zodiac sign.
    static Quality qualityForSign(ZodiacSign sign);
    /// Converts Unix seconds to a Julian day for deterministic snapshot generation.
    static double julianDayFromUnixSeconds(double unixSeconds);
    /// Converts a proleptic Gregorian UTC date-time into a Julian day.
    static double julianDayFromUtc(int year, int month, int day, int hour = 12, int minute = 0, int second = 0);
    /// Reads the system clock as Unix seconds for Now mode.
    static double unixSecondsFromSystemClock();

    /// Builds a deterministic planet snapshot for a Julian day.
    AstroSnapshot snapshotForJulianDay(double julianDay) const;
    /// Builds a deterministic planet snapshot for a Unix timestamp.
    AstroSnapshot snapshotForUnixSeconds(double unixSeconds) const;
    /// Detects musically relevant aspects between all planet pairs in a snapshot.
    std::vector<AspectEvent> detectAspects(const AstroSnapshot& snapshot) const;

};

} // namespace astral::astro
