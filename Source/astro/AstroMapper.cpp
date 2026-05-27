#include "astro/AstroMapper.h"

#include "astro/AstrologyEngine.h"

#include <algorithm>
#include <cmath>

namespace astral::astro {
namespace {

constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kEarthPeriodDays = 365.256f;

constexpr std::array<float, static_cast<std::size_t>(PlanetId::Count)> kOrbitalPeriodsDays{{
    365.256f,
    27.3217f,
    87.969f,
    224.701f,
    686.98f,
    4332.59f,
    10759.22f,
    30688.5f,
    60182.0f,
    90560.0f,
}};

constexpr std::array<float, static_cast<std::size_t>(PlanetId::Count)> kLayerWeights{{
    0.95f, 0.62f, 0.42f, 0.58f, 0.46f,
    0.70f, 0.50f, 0.36f, 0.48f, 0.32f
}};

float sin01(float degrees)
{
    return 0.5f + 0.5f * std::sin(degrees * kTwoPi / 360.0f);
}

float planetValue(const AstroSnapshot& snapshot, PlanetId planet)
{
    return snapshot.planets[static_cast<std::size_t>(planet)].longitudeDegrees;
}

float foldRatioToDroneRange(float ratio)
{
    if (!std::isfinite(ratio) || ratio <= 0.0f) {
        return 1.0f;
    }

    while (ratio < 0.25f) {
        ratio *= 2.0f;
    }
    while (ratio > 8.0f) {
        ratio *= 0.5f;
    }
    return std::clamp(ratio, 0.25f, 8.0f);
}

float aspectAngle(AspectType type)
{
    switch (type) {
        case AspectType::Conjunction: return 0.0f;
        case AspectType::Sextile: return 60.0f;
        case AspectType::Square: return 90.0f;
        case AspectType::Trine: return 120.0f;
        case AspectType::Opposition: return 180.0f;
    }
    return 0.0f;
}

float hardAspectEnergy(AspectType type)
{
    switch (type) {
        case AspectType::Conjunction:
        case AspectType::Square:
        case AspectType::Opposition:
            return 1.0f;
        case AspectType::Sextile:
        case AspectType::Trine:
            return 0.35f;
    }
    return 0.0f;
}

float softAspectEnergy(AspectType type)
{
    switch (type) {
        case AspectType::Sextile:
        case AspectType::Trine:
            return 1.0f;
        case AspectType::Conjunction:
        case AspectType::Square:
        case AspectType::Opposition:
            return 0.2f;
    }
    return 0.0f;
}

} // namespace

AstroModulationFrame AstroMapper::mapSnapshot(const AstroSnapshot& snapshot) const
{
    float hardEnergy = 0.0f;
    float softEnergy = 0.0f;
    for (const auto& aspect : snapshot.aspects) {
        hardEnergy += hardAspectEnergy(aspect.type) * aspect.strength01;
        softEnergy += softAspectEnergy(aspect.type) * aspect.strength01;
    }

    hardEnergy = std::clamp(hardEnergy / 4.0f, 0.0f, 1.0f);
    softEnergy = std::clamp(softEnergy / 4.0f, 0.0f, 1.0f);

    const auto sunSign = AstrologyEngine::signForLongitude(planetValue(snapshot, PlanetId::Sun));
    const auto moonSign = AstrologyEngine::signForLongitude(planetValue(snapshot, PlanetId::Moon));
    const auto sunElement = AstrologyEngine::elementForSign(sunSign);
    const auto moonQuality = AstrologyEngine::qualityForSign(moonSign);

    const float mercury = sin01(planetValue(snapshot, PlanetId::Mercury));
    const float venus = sin01(planetValue(snapshot, PlanetId::Venus));
    const float mars = sin01(planetValue(snapshot, PlanetId::Mars));
    const float jupiter = sin01(planetValue(snapshot, PlanetId::Jupiter));
    const float saturn = sin01(planetValue(snapshot, PlanetId::Saturn));
    const float uranus = sin01(planetValue(snapshot, PlanetId::Uranus));
    const float neptune = sin01(planetValue(snapshot, PlanetId::Neptune));
    const float pluto = sin01(planetValue(snapshot, PlanetId::Pluto));

    float elementTone = 0.0f;
    switch (sunElement) {
        case Element::Fire: elementTone = 0.20f; break;
        case Element::Earth: elementTone = -0.15f; break;
        case Element::Air: elementTone = 0.10f; break;
        case Element::Water: elementTone = -0.05f; break;
    }

    float qualityMotion = 0.0f;
    switch (moonQuality) {
        case Quality::Cardinal: qualityMotion = 0.20f; break;
        case Quality::Fixed: qualityMotion = -0.10f; break;
        case Quality::Mutable: qualityMotion = 0.10f; break;
    }

    AstroModulationFrame frame;
    frame.delayDrift = std::clamp((mercury - 0.5f) * 2.0f + qualityMotion, -1.0f, 1.0f);
    frame.feedbackBloom = std::clamp(0.22f + hardEnergy * 0.55f + mars * 0.16f - saturn * 0.18f, 0.0f, 1.0f);
    frame.reverbSize = std::clamp(0.35f + venus * 0.22f + jupiter * 0.28f + softEnergy * 0.20f, 0.0f, 1.0f);
    frame.damping = std::clamp(0.35f + saturn * 0.35f - venus * 0.12f - elementTone, 0.0f, 1.0f);
    frame.shimmer = std::clamp(neptune * 0.45f + jupiter * 0.20f + softEnergy * 0.25f + elementTone, 0.0f, 1.0f);
    frame.stereoMotion = std::clamp(0.20f + uranus * 0.38f + pluto * 0.18f + qualityMotion + softEnergy * 0.10f, 0.0f, 1.0f);
    return frame;
}

AstroDroneFrame AstroMapper::mapDroneSnapshot(const AstroSnapshot& snapshot) const
{
    AstroDroneFrame frame;

    float conjunctionEnergy = 0.0f;
    float consonantEnergy = 0.0f;
    float tensionEnergy = 0.0f;
    float oppositionEnergy = 0.0f;
    float tapEnergy = 0.0f;

    for (const auto& aspect : snapshot.aspects) {
        const float strength = std::clamp(aspect.strength01, 0.0f, 1.0f);
        tapEnergy += strength;

        switch (aspect.type) {
            case AspectType::Conjunction:
                conjunctionEnergy += strength;
                break;
            case AspectType::Sextile:
            case AspectType::Trine:
                consonantEnergy += strength;
                break;
            case AspectType::Square:
                tensionEnergy += strength;
                break;
            case AspectType::Opposition:
                oppositionEnergy += strength;
                break;
        }
    }

    frame.rootReinforcement = std::clamp(conjunctionEnergy / 2.5f, 0.0f, 1.0f);
    frame.consonantBloom = std::clamp(consonantEnergy / 3.0f, 0.0f, 1.0f);
    frame.tensionBeating = std::clamp(tensionEnergy / 2.0f, 0.0f, 1.0f);
    frame.phaseSplit = std::clamp(oppositionEnergy / 2.5f, 0.0f, 1.0f);
    frame.aspectTapDensity = std::clamp(tapEnergy / 6.0f, 0.0f, 1.0f);

    for (std::size_t index = 0; index < frame.layers.size(); ++index) {
        const auto planet = static_cast<PlanetId>(index);
        const auto& position = snapshot.planets[index];
        const float longitude = position.longitudeDegrees;
        const float ratio = foldRatioToDroneRange(kOrbitalPeriodsDays[index] / kEarthPeriodDays);
        const float longitudePhase = sin01(longitude);
        const float retrogradePull = position.retrograde ? -9.0f : 0.0f;
        const float signPan = std::sin(longitude * kTwoPi / 180.0f);

        frame.layers[index] = {
            planet,
            ratio,
            std::clamp(kLayerWeights[index] * (0.55f + longitudePhase * 0.45f), 0.0f, 1.0f),
            std::clamp((longitudePhase - 0.5f) * 34.0f + retrogradePull, -50.0f, 50.0f),
            std::clamp(signPan * 0.75f, -1.0f, 1.0f),
            std::clamp(longitude / 360.0f, 0.0f, 1.0f),
            std::clamp(std::abs(position.speed) / 14.0f, 0.0f, 1.0f)
        };
    }

    for (const auto& aspect : snapshot.aspects) {
        const auto a = static_cast<std::size_t>(aspect.a);
        const auto b = static_cast<std::size_t>(aspect.b);
        const float intervalBend = foldRatioToDroneRange(1.0f + aspectAngle(aspect.type) / 120.0f);
        const float amount = std::clamp(aspect.strength01 * 0.08f, 0.0f, 0.12f);
        frame.layers[a].amplitude = std::clamp(frame.layers[a].amplitude + amount, 0.0f, 1.0f);
        frame.layers[b].amplitude = std::clamp(frame.layers[b].amplitude + amount, 0.0f, 1.0f);
        frame.layers[b].ratio = foldRatioToDroneRange(frame.layers[b].ratio * (1.0f + (intervalBend - 1.0f) * aspect.strength01 * 0.08f));
    }

    return frame;
}

} // namespace astral::astro
