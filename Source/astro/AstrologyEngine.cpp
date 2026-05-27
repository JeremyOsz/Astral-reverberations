#include "astro/AstrologyEngine.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace astral::astro {
namespace {

constexpr float kFullCircle = 360.0f;
constexpr double kUnixEpochJulianDay = 2440587.5;
constexpr double kSecondsPerDay = 86400.0;

// Static approximation of one planet's musical orbit model.
struct OrbitSpec {
    PlanetId planet;
    double periodDays;
    float phaseAtJ2000;
    float speedSign;
};

constexpr std::array<OrbitSpec, static_cast<std::size_t>(PlanetId::Count)> kOrbitSpecs{{
    {PlanetId::Sun, 365.256, 280.46f, 1.0f},
    {PlanetId::Moon, 27.3217, 218.32f, 1.0f},
    {PlanetId::Mercury, 87.969, 252.25f, 1.0f},
    {PlanetId::Venus, 224.701, 181.98f, 1.0f},
    {PlanetId::Mars, 686.98, 355.43f, 1.0f},
    {PlanetId::Jupiter, 4332.59, 34.35f, 1.0f},
    {PlanetId::Saturn, 10759.22, 50.08f, 1.0f},
    {PlanetId::Uranus, 30688.5, 314.05f, 1.0f},
    {PlanetId::Neptune, 60182.0, 304.35f, 1.0f},
    {PlanetId::Pluto, 90560.0, 238.93f, 1.0f},
}};

// Defines one aspect target and the orb allowed around it.
struct AspectSpec {
    AspectType type;
    float angle;
    float orb;
};

constexpr std::array<AspectSpec, 5> kAspectSpecs{{
    {AspectType::Conjunction, 0.0f, 8.0f},
    {AspectType::Sextile, 60.0f, 4.0f},
    {AspectType::Square, 90.0f, 6.0f},
    {AspectType::Trine, 120.0f, 6.0f},
    {AspectType::Opposition, 180.0f, 8.0f},
}};

// Adds a deterministic speed wobble so simulated orbits can enter retrograde-feeling spans.
float retrogradeWobble(double daysSinceJ2000, const OrbitSpec& spec)
{
    // This is a musical approximation, not an astronomy model. Inner planets
    // wobble faster; outer planets barely do. Negative speed marks a simulated
    // retrograde span that can drive darker modulation choices.
    const double cycle = daysSinceJ2000 / (spec.periodDays * 0.47);
    const float wobble = std::sin(static_cast<float>(cycle * 2.0 * M_PI));
    const bool canRetrograde = spec.planet == PlanetId::Mercury
        || spec.planet == PlanetId::Venus
        || spec.planet == PlanetId::Mars
        || spec.planet == PlanetId::Jupiter
        || spec.planet == PlanetId::Saturn;
    return canRetrograde ? wobble : 0.0f;
}

} // namespace

float AstrologyEngine::normalizeDegrees(float degrees)
{
    const float wrapped = std::fmod(degrees, kFullCircle);
    return wrapped < 0.0f ? wrapped + kFullCircle : wrapped;
}

float AstrologyEngine::shortestAngularDistance(float aDegrees, float bDegrees)
{
    const float difference = std::abs(normalizeDegrees(aDegrees) - normalizeDegrees(bDegrees));
    return std::min(difference, kFullCircle - difference);
}

ZodiacSign AstrologyEngine::signForLongitude(float longitudeDegrees)
{
    const auto index = static_cast<std::size_t>(normalizeDegrees(longitudeDegrees) / 30.0f);
    return static_cast<ZodiacSign>(std::min<std::size_t>(index, 11));
}

Element AstrologyEngine::elementForSign(ZodiacSign sign)
{
    switch (sign) {
        case ZodiacSign::Aries:
        case ZodiacSign::Leo:
        case ZodiacSign::Sagittarius:
            return Element::Fire;
        case ZodiacSign::Taurus:
        case ZodiacSign::Virgo:
        case ZodiacSign::Capricorn:
            return Element::Earth;
        case ZodiacSign::Gemini:
        case ZodiacSign::Libra:
        case ZodiacSign::Aquarius:
            return Element::Air;
        case ZodiacSign::Cancer:
        case ZodiacSign::Scorpio:
        case ZodiacSign::Pisces:
            return Element::Water;
    }
    return Element::Fire;
}

Quality AstrologyEngine::qualityForSign(ZodiacSign sign)
{
    switch (sign) {
        case ZodiacSign::Aries:
        case ZodiacSign::Cancer:
        case ZodiacSign::Libra:
        case ZodiacSign::Capricorn:
            return Quality::Cardinal;
        case ZodiacSign::Taurus:
        case ZodiacSign::Leo:
        case ZodiacSign::Scorpio:
        case ZodiacSign::Aquarius:
            return Quality::Fixed;
        case ZodiacSign::Gemini:
        case ZodiacSign::Virgo:
        case ZodiacSign::Sagittarius:
        case ZodiacSign::Pisces:
            return Quality::Mutable;
    }
    return Quality::Cardinal;
}

double AstrologyEngine::julianDayFromUnixSeconds(double unixSeconds)
{
    return kUnixEpochJulianDay + unixSeconds / kSecondsPerDay;
}

double AstrologyEngine::unixSecondsFromSystemClock()
{
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration<double>(now).count();
}

AstroSnapshot AstrologyEngine::snapshotForUnixSeconds(double unixSeconds) const
{
    return snapshotForJulianDay(julianDayFromUnixSeconds(unixSeconds));
}

AstroSnapshot AstrologyEngine::snapshotForJulianDay(double julianDay) const
{
    AstroSnapshot snapshot;
    snapshot.julianDay = julianDay;

    const double daysSinceJ2000 = julianDay - J2000JulianDay;
    for (const auto& spec : kOrbitSpecs) {
        const auto index = static_cast<std::size_t>(spec.planet);
        const double cycles = daysSinceJ2000 / spec.periodDays;
        const float wobble = retrogradeWobble(daysSinceJ2000, spec);
        const float longitude = normalizeDegrees(spec.phaseAtJ2000 + static_cast<float>(cycles * 360.0) + wobble * 5.0f);
        const float nominalSpeed = static_cast<float>(360.0 / spec.periodDays) * spec.speedSign;
        const bool retrograde = wobble < -0.88f;

        snapshot.planets[index] = {
            spec.planet,
            longitude,
            retrograde ? -std::abs(nominalSpeed) : nominalSpeed,
            retrograde
        };
    }

    snapshot.aspects = detectAspects(snapshot);
    return snapshot;
}

std::vector<AspectEvent> AstrologyEngine::detectAspects(const AstroSnapshot& snapshot) const
{
    std::vector<AspectEvent> events;
    events.reserve(12);

    for (std::size_t a = 0; a < snapshot.planets.size(); ++a) {
        for (std::size_t b = a + 1; b < snapshot.planets.size(); ++b) {
            const auto distance = shortestAngularDistance(
                snapshot.planets[a].longitudeDegrees,
                snapshot.planets[b].longitudeDegrees);

            for (const auto& spec : kAspectSpecs) {
                const float orb = std::abs(distance - spec.angle);
                if (orb <= spec.orb) {
                    events.push_back({
                        snapshot.planets[a].planet,
                        snapshot.planets[b].planet,
                        spec.type,
                        orb,
                        std::clamp(1.0f - orb / spec.orb, 0.0f, 1.0f)
                    });
                    break;
                }
            }
        }
    }

    return events;
}

std::string_view toString(PlanetId planet)
{
    constexpr std::array names{
        "Sun", "Moon", "Mercury", "Venus", "Mars",
        "Jupiter", "Saturn", "Uranus", "Neptune", "Pluto"
    };
    return names.at(static_cast<std::size_t>(planet));
}

std::string_view toString(AspectType aspect)
{
    switch (aspect) {
        case AspectType::Conjunction: return "Conjunction";
        case AspectType::Sextile: return "Sextile";
        case AspectType::Square: return "Square";
        case AspectType::Trine: return "Trine";
        case AspectType::Opposition: return "Opposition";
    }
    return "Unknown";
}

std::string_view toString(ZodiacSign sign)
{
    constexpr std::array names{
        "Aries", "Taurus", "Gemini", "Cancer", "Leo", "Virgo",
        "Libra", "Scorpio", "Sagittarius", "Capricorn", "Aquarius", "Pisces"
    };
    return names.at(static_cast<std::size_t>(sign));
}

std::string_view toString(Element element)
{
    switch (element) {
        case Element::Fire: return "Fire";
        case Element::Earth: return "Earth";
        case Element::Air: return "Air";
        case Element::Water: return "Water";
    }
    return "Unknown";
}

std::string_view toString(Quality quality)
{
    switch (quality) {
        case Quality::Cardinal: return "Cardinal";
        case Quality::Fixed: return "Fixed";
        case Quality::Mutable: return "Mutable";
    }
    return "Unknown";
}

} // namespace astral::astro
