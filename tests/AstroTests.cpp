#include "TestSupport.h"

#include "astro/AstroMapper.h"
#include "astro/AstrologyEngine.h"

using namespace astral::astro;

void runAstroTests()
{
    requireNear(AstrologyEngine::normalizeDegrees(370.0f), 10.0f, 0.0001f, "positive degree wrap");
    requireNear(AstrologyEngine::normalizeDegrees(-10.0f), 350.0f, 0.0001f, "negative degree wrap");
    requireNear(AstrologyEngine::shortestAngularDistance(350.0f, 10.0f), 20.0f, 0.0001f, "short angular distance wraps");

    require(AstrologyEngine::signForLongitude(0.0f) == ZodiacSign::Aries, "0 degrees is Aries");
    require(AstrologyEngine::signForLongitude(59.9f) == ZodiacSign::Taurus, "59.9 degrees is Taurus");
    require(AstrologyEngine::elementForSign(ZodiacSign::Scorpio) == Element::Water, "Scorpio is water");
    require(AstrologyEngine::qualityForSign(ZodiacSign::Aquarius) == Quality::Fixed, "Aquarius is fixed");

    AstrologyEngine engine;
    const auto first = engine.snapshotForJulianDay(AstrologyEngine::J2000JulianDay + 123.0);
    const auto second = engine.snapshotForJulianDay(AstrologyEngine::J2000JulianDay + 123.0);
    requireNear(first.planets[static_cast<std::size_t>(PlanetId::Mercury)].longitudeDegrees,
        second.planets[static_cast<std::size_t>(PlanetId::Mercury)].longitudeDegrees,
        0.0001f,
        "simulation is deterministic");
    require(!first.aspects.empty(), "snapshot detects at least one musical aspect for this fixture date");

    AstroMapper mapper;
    const auto frame = mapper.mapSnapshot(first);
    require(frame.delayDrift >= -1.0f && frame.delayDrift <= 1.0f, "delay drift is bounded");
    require(frame.feedbackBloom >= 0.0f && frame.feedbackBloom <= 1.0f, "feedback bloom is bounded");
    require(frame.reverbSize >= 0.0f && frame.reverbSize <= 1.0f, "reverb size is bounded");
    require(frame.damping >= 0.0f && frame.damping <= 1.0f, "damping is bounded");
    require(frame.shimmer >= 0.0f && frame.shimmer <= 1.0f, "shimmer is bounded");
    require(frame.stereoMotion >= 0.0f && frame.stereoMotion <= 1.0f, "stereo motion is bounded");

    const auto droneFrame = mapper.mapDroneSnapshot(first);
    for (const auto& layer : droneFrame.layers) {
        require(std::isfinite(layer.ratio), "drone ratio is finite");
        require(layer.ratio >= 0.25f && layer.ratio <= 8.0f, "drone ratio is musically bounded");
        require(layer.amplitude >= 0.0f && layer.amplitude <= 1.0f, "drone layer amplitude is bounded");
        require(layer.detuneCents >= -50.0f && layer.detuneCents <= 50.0f, "drone detune is bounded");
        require(layer.pan >= -1.0f && layer.pan <= 1.0f, "drone pan is bounded");
        require(layer.tapePosition >= 0.0f && layer.tapePosition <= 1.0f, "drone tape position is bounded");
    }
    require(droneFrame.rootReinforcement >= 0.0f && droneFrame.rootReinforcement <= 1.0f, "root reinforcement is bounded");
    require(droneFrame.consonantBloom >= 0.0f && droneFrame.consonantBloom <= 1.0f, "consonant bloom is bounded");
    require(droneFrame.tensionBeating >= 0.0f && droneFrame.tensionBeating <= 1.0f, "tension beating is bounded");
    require(droneFrame.phaseSplit >= 0.0f && droneFrame.phaseSplit <= 1.0f, "phase split is bounded");

    const auto secondDroneFrame = mapper.mapDroneSnapshot(second);
    requireNear(droneFrame.layers[static_cast<std::size_t>(PlanetId::Jupiter)].ratio,
        secondDroneFrame.layers[static_cast<std::size_t>(PlanetId::Jupiter)].ratio,
        0.0001f,
        "drone mapping is deterministic");
}
