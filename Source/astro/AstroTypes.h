#pragma once

#include <array>
#include <cstddef>
#include <string_view>
#include <vector>

namespace astral::astro {

enum class PlanetId : std::size_t {
    Sun = 0,
    Moon,
    Mercury,
    Venus,
    Mars,
    Jupiter,
    Saturn,
    Uranus,
    Neptune,
    Pluto,
    Count
};

enum class AspectType {
    Conjunction,
    Sextile,
    Square,
    Trine,
    Opposition
};

enum class ZodiacSign : std::size_t {
    Aries = 0,
    Taurus,
    Gemini,
    Cancer,
    Leo,
    Virgo,
    Libra,
    Scorpio,
    Sagittarius,
    Capricorn,
    Aquarius,
    Pisces
};

enum class Element {
    Fire,
    Earth,
    Air,
    Water
};

enum class Quality {
    Cardinal,
    Fixed,
    Mutable
};

struct PlanetPosition {
    PlanetId planet{};
    float longitudeDegrees{};
    float speed{};
    bool retrograde{};
};

struct AspectEvent {
    PlanetId a{};
    PlanetId b{};
    AspectType type{};
    float orbDegrees{};
    float strength01{};
};

struct AstroSnapshot {
    double julianDay{};
    std::array<PlanetPosition, static_cast<std::size_t>(PlanetId::Count)> planets{};
    std::vector<AspectEvent> aspects;
};

struct AstroModulationFrame {
    float delayDrift{};
    float feedbackBloom{};
    float reverbSize{};
    float damping{};
    float shimmer{};
    float stereoMotion{};
};

struct AstroHarmonicLayer {
    PlanetId planet{};
    float ratio = 1.0f;
    float amplitude{};
    float detuneCents{};
    float pan{};
    float tapePosition{};
    float driftRate{};
};

struct AstroDroneFrame {
    std::array<AstroHarmonicLayer, static_cast<std::size_t>(PlanetId::Count)> layers{};
    float rootReinforcement{};
    float consonantBloom{};
    float tensionBeating{};
    float phaseSplit{};
    float aspectTapDensity{};
};

std::string_view toString(PlanetId planet);
std::string_view toString(AspectType aspect);
std::string_view toString(ZodiacSign sign);
std::string_view toString(Element element);
std::string_view toString(Quality quality);

} // namespace astral::astro
