#include "plugin/PluginEditor.h"

#include "astro/HistoricalPresets.h"
#include "BinaryData.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string_view>

namespace astral::plugin {
namespace {

constexpr int kPadding = 16;
constexpr int kHeaderHeight = 48;
constexpr int kInspectorWidth = 236;
constexpr int kInspectorSliderRowHeight = 32;
constexpr int kBottomHeight = 92;
constexpr int kInputMeterHeight = 42;
constexpr float kOrbitRadiusFactor = 0.46f;
constexpr float kSignMarkerOffset = 22.0f;
constexpr float kOrbitTitleBand = 28.0f;
constexpr float kHarmonicsStripWidth = 78.0f;
constexpr float kPi = 3.14159265358979323846f;
// Background atmosphere: texture strength and how much the dark veil dims it.
constexpr float kEditorBackgroundTextureOpacity = 0.58f;
constexpr float kEditorBackgroundVeilAlpha = 0.34f;
constexpr float kOrbitMapBackgroundTextureOpacity = 0.54f;
constexpr float kOrbitMapPanelFillAlpha = 0.76f;
constexpr int kMaxOutputParticles = 130;

// Lazily decodes the generated astral texture embedded by JUCE BinaryData.
const juce::Image& astralTexture()
{
    static const juce::Image image = juce::ImageCache::getFromMemory(
        BinaryData::astralorbittexture_png,
        BinaryData::astralorbittexture_pngSize);
    return image;
}

// Draws the generated texture as atmosphere, then leaves controls fully legible.
void drawAstralTexture(juce::Graphics& graphics, juce::Rectangle<float> area, float opacity)
{
    const auto& texture = astralTexture();
    if (!texture.isValid()) {
        return;
    }

    graphics.saveState();
    graphics.setOpacity(opacity);
    graphics.drawImage(texture, area, juce::RectanglePlacement::fillDestination);
    graphics.restoreState();
}

// Shared panel fill for the editor's dark control surfaces.
juce::Colour panelColour()
{
    return juce::Colour(27, 29, 34);
}

// Shared line colour for orbit rings, ticks, and outlines.
juce::Colour lineColour()
{
    return juce::Colour(73, 82, 92);
}

// Returns a stable per-planet accent colour by planet index.
juce::Colour accentColour(std::size_t index)
{
    constexpr std::array<std::uint32_t, 10> colours{{
        0xfff3d58a, 0xffb9c7d8, 0xff69c6d4, 0xffe5a1bd, 0xffe06f57,
        0xffd8b65a, 0xff9aa3ad, 0xff7bc4a5, 0xff8ea8ff, 0xffc68ddd
    }};
    return juce::Colour(colours[index % colours.size()]);
}

// Converts a MIDI note number into a compact note name for the root selector.
juce::String noteName(int midiNote)
{
    constexpr std::array<const char*, 12> names{{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}};
    const int clamped = juce::jlimit(0, 127, midiNote);
    return juce::String(names[static_cast<std::size_t>(clamped % 12)]) + juce::String(clamped / 12 - 1);
}

// Returns a font that can render astrological sign glyphs on the current platform.
juce::Font zodiacGlyphFont(float height)
{
#if JUCE_MAC
    constexpr const char* typeface = "Apple Symbols";
#elif JUCE_WINDOWS
    constexpr const char* typeface = "Segoe UI Symbol";
#else
    constexpr const char* typeface = "Noto Sans Symbols 2";
#endif
    return juce::Font(juce::FontOptions(typeface, height, juce::Font::bold));
}

// Returns the compact visible marker text for a zodiac sign.
juce::String signGlyph(std::size_t index)
{
    return juce::String::charToString(static_cast<juce::juce_wchar>(0x2648 + (index % 12)));
}

// Returns the short visible label for an assignable sign effect.
juce::String signEffectLabel(std::size_t index)
{
    constexpr std::array<const char*, 12> labels{{"Drive", "Damp", "Taps", "Space", "Shine", "Tone", "Width", "Feed", "Time", "Hold", "Move", "Wash"}};
    return labels[index % labels.size()];
}

// Provides concise control help without adding visible instructional text.
juce::String sliderTooltip(const juce::String& parameterId)
{
    if (parameterId == "macro_substance") {
        return "Overall presence: wet blend, echo/verb send, and output level.";
    }
    if (parameterId == "macro_mneme") {
        return "Memory: capture tails, orbit echo path, and safer feedback when tails are hot.";
    }
    if (parameterId == "macro_choir") {
        return "Harmonic body: drone level, spread, aspect bloom, and ring pluck level.";
    }
    if (parameterId == "macro_ephemeris") {
        return "Sky motion: astrological modulation depth across delay and reverb.";
    }
    if (parameterId == "macro_fate") {
        return "Tape loop law: echo time, repeats, wow/flutter, and saturation.";
    }
    if (parameterId == "macro_void") {
        return "Space character: verb size, darkness, and reverb emphasis.";
    }
    if (parameterId == "macro_pulse") {
        return "Ring plucks: timbre and octave spread (Euclidean rate/wet are separate).";
    }
    if (parameterId == "euclid_rate") {
        return "Sets the Euclidean pluck sequencer step rate in steps per second.";
    }
    if (parameterId == "euclid_wet") {
        return "Blends Euclidean plucks from direct dry to fully sent through tape delay and reverb.";
    }
    if (parameterId == "macro_root") {
        return "Transposes the harmonic root in semitones.";
    }
    if (parameterId == "output_gain") {
        return "Final output trim after the Substance macro.";
    }
    return {};
}

// Formats macro readouts as musical percentages or semitones.
juce::String sliderText(const juce::String& parameterId, double value)
{
    if (parameterId == "macro_root") {
        const int semitones = static_cast<int>(std::round((value - 0.5) * 24.0));
        return (semitones >= 0 ? "+" : "") + juce::String(semitones) + " st";
    }
    if (parameterId == "output_gain") {
        return juce::String(value, 2) + "x";
    }
    if (parameterId == "euclid_rate") {
        return juce::String(value, 1) + " st/s";
    }
    if (parameterId == "euclid_wet") {
        return juce::String(static_cast<int>(std::round(value * 100.0))) + "%";
    }
    if (parameterId.startsWith("macro_")) {
        return juce::String(static_cast<int>(std::round(value * 100.0))) + "%";
    }
    return juce::String(value, 2);
}

// Converts a UI point around the orbit centre into an astrological longitude.
float longitudeFromPoint(juce::Point<float> point, juce::Point<float> centre)
{
    const auto radians = std::atan2(point.y - centre.y, point.x - centre.x);
    return astral::astro::AstrologyEngine::normalizeDegrees(radians * 180.0f / kPi + 90.0f);
}

// Spaces planet rings from inner to outer orbit within the available radius.
float orbitRadiusForIndex(float radius, std::size_t index)
{
    return radius * (0.26f + static_cast<float>(index) * 0.065f);
}

// Measures angular distance across the 0/360 boundary.
float wrappedDistanceDegrees(float a, float b)
{
    const float difference = std::abs(astral::astro::AstrologyEngine::normalizeDegrees(a - b + 180.0f) - 180.0f);
    return difference;
}

// Finds the closest distance from a mouse point to an aspect line segment.
float distanceToLineSegment(juce::Point<float> point, juce::Point<float> a, juce::Point<float> b)
{
    const auto ab = b - a;
    const auto ap = point - a;
    const float lengthSquared = ab.x * ab.x + ab.y * ab.y;
    if (lengthSquared <= 0.0001f) {
        return point.getDistanceFrom(a);
    }

    const float t = juce::jlimit(0.0f, 1.0f, (ap.x * ab.x + ap.y * ab.y) / lengthSquared);
    return point.getDistanceFrom({a.x + ab.x * t, a.y + ab.y * t});
}

// Maps keys onto Sun through Pluto in visible orbit order.
std::optional<astro::PlanetId> planetForIndexKey(juce::juce_wchar keyCode, std::string_view keys)
{
    const auto lower = static_cast<char>(std::tolower(static_cast<unsigned char>(keyCode)));
    const auto index = keys.find(lower);
    if (index == std::string_view::npos || index >= static_cast<std::size_t>(astro::PlanetId::Count)) {
        return std::nullopt;
    }
    return static_cast<astro::PlanetId>(index);
}

std::optional<astro::PlanetId> planetForPlaybackKey(juce::juce_wchar keyCode)
{
    return planetForIndexKey(keyCode, "qwertyuiop");
}

std::optional<astro::PlanetId> planetForCaptureKey(juce::juce_wchar keyCode)
{
    return planetForIndexKey(keyCode, "1234567890");
}

std::optional<astro::PlanetId> planetForMuteKey(juce::juce_wchar keyCode)
{
    return planetForIndexKey(keyCode, "asdfghjkl;");
}

juce::String keyLabelForPlanet(std::string_view keys, astro::PlanetId planet)
{
    const auto index = static_cast<std::size_t>(planet);
    if (index >= keys.size()) {
        return {};
    }
    return juce::String::charToString(static_cast<juce::juce_wchar>(keys[index])).toUpperCase();
}

} // namespace

class OrbitMapComponent final : public juce::Component, public juce::TooltipClient, private juce::Timer {
public:
    explicit OrbitMapComponent(AstralReverberationsAudioProcessor& owner)
        : processor(owner)
    {
        startTimerHz(24);
    }

    void paint(juce::Graphics& graphics) override
    {
        auto bounds = getLocalBounds().toFloat();
        graphics.setColour(panelColour().withAlpha(kOrbitMapPanelFillAlpha));
        graphics.fillRoundedRectangle(bounds, 8.0f);

        const auto layout = orbitLayout();
        const auto centre = layout.centre;
        const float radius = layout.radius;
        const auto snapshot = processor.getCurrentSnapshotCopy();
        const auto droneFrame = processor.getCurrentDroneFrameCopy();

        {
            juce::Graphics::ScopedSaveState textureScope(graphics);
            graphics.reduceClipRegion(layout.contentArea.toNearestInt());
            drawAstralTexture(graphics, layout.contentArea, kOrbitMapBackgroundTextureOpacity);
        }

        graphics.setColour(juce::Colour(234, 226, 202));
        graphics.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        graphics.drawText("Orbit Harmony", bounds.withHeight(24.0f).reduced(16.0f, 0.0f), juce::Justification::centredLeft);

        drawCymaticField(graphics, layout.contentArea, centre, radius, snapshot, droneFrame);
        drawOutputParticles(graphics, layout.contentArea, centre, radius);
        drawZodiac(graphics, centre, radius);
        drawSignMarkers(graphics, centre, layout.markerRadius, snapshot);
        drawAspects(graphics, snapshot, centre, radius);
        drawInteractionFeedback(graphics, centre, radius);

        graphics.setColour(juce::Colour(246, 217, 116));
        graphics.fillEllipse(centre.x - 9.0f, centre.y - 9.0f, 18.0f, 18.0f);

        for (std::size_t index = 0; index < snapshot.planets.size(); ++index) {
            const auto& planet = snapshot.planets[index];
            const float orbitRadius = orbitRadiusForIndex(radius, index);
            const auto& layer = droneFrame.layers[index];
            const auto position = pointForLongitude(planet.longitudeDegrees, centre, orbitRadius);
            const bool muted = processor.isPlanetMuted(planet.planet);
            graphics.setColour(accentColour(index).withAlpha(0.18f));
            graphics.drawEllipse(centre.x - orbitRadius, centre.y - orbitRadius, orbitRadius * 2.0f, orbitRadius * 2.0f, 1.0f);
            const float tailLevel = muted ? 0.0f : processor.getOrbitTailLevel(planet.planet);
            drawTailArc(graphics,
                centre,
                orbitRadius,
                planet.longitudeDegrees,
                layer.tailSeconds,
                tailLevel,
                processor.isManualOrbitTailActive(planet.planet),
                accentColour(index));
            if (processor.isManualOrbitTailActive(planet.planet)) {
                graphics.setColour(accentColour(index).withAlpha(0.45f));
                graphics.drawEllipse(position.x - 15.0f, position.y - 15.0f, 30.0f, 30.0f, 2.0f);
            }
            if (processor.hasPlanetLongitudeOffset(planet.planet)) {
                graphics.setColour(juce::Colour(246, 217, 116).withAlpha(0.8f));
                graphics.drawEllipse(position.x - 19.0f, position.y - 19.0f, 38.0f, 38.0f, 1.6f);
            }
            graphics.setColour(muted ? juce::Colour(72, 78, 86) : accentColour(index));
            graphics.fillEllipse(position.x - 8.0f, position.y - 8.0f, 16.0f, 16.0f);
            if (muted) {
                graphics.setColour(juce::Colour(185, 195, 202).withAlpha(0.65f));
                graphics.drawLine(position.x - 9.0f, position.y + 9.0f, position.x + 9.0f, position.y - 9.0f, 2.0f);
            }
            graphics.setColour(juce::Colour(235, 239, 242));
            graphics.setFont(juce::FontOptions(12.0f, juce::Font::bold));
            graphics.drawText(juce::String(astro::toString(planet.planet).data()),
                juce::Rectangle<float>(position.x + 10.0f, position.y - 9.0f, 70.0f, 18.0f),
                juce::Justification::centredLeft);
        }

        drawSpectrum(graphics, layout.harmonicsArea, droneFrame);
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (const auto sign = signAt(event.position)) {
            if (event.mods.isRightButtonDown() || event.mods.isAltDown() || event.mods.isPopupMenu()) {
                processor.cycleSignEffect(*sign);
            } else {
                processor.toggleSignEffect(*sign);
            }
            addInteractionPulse(static_cast<std::size_t>(*sign), 0.0f, 0.72f);
            repaint();
            return;
        }

        draggedPlanet = planetAt(event.position);
        if (draggedPlanet.has_value()) {
            selectedPlanet = draggedPlanet;
            addPlanetInteractionPulse(*draggedPlanet, 0.88f);
            if (event.getNumberOfClicks() >= 2) {
                processor.togglePlanetMute(*draggedPlanet);
                draggedPlanet.reset();
                repaint();
            }
            return;
        }

        if (const auto planet = orbitAt(event.position)) {
            selectedPlanet = planet;
            processor.tapOrbitReverbTank(*planet);
            addPlanetInteractionPulse(*planet, 1.0f);
            repaint();
        }
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (!draggedPlanet.has_value()) {
            return;
        }
        const auto layout = orbitLayout();
        processor.setManualPlanetLongitude(*draggedPlanet, longitudeFromPoint(event.position, layout.centre));
        addPlanetInteractionPulse(*draggedPlanet, 0.48f);
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        draggedPlanet.reset();
    }

    astro::PlanetId getSelectedPlanet() const
    {
        return selectedPlanet.value_or(astro::PlanetId::Sun);
    }

    void showPlanetFeedback(astro::PlanetId planet, float energy)
    {
        selectedPlanet = planet;
        addPlanetInteractionPulse(planet, energy);
        repaint();
    }

    /// Builds dynamic tooltip text for hovered planets, tails, signs, aspects, or the general map.
    juce::String getTooltip() override
    {
        const auto point = getMouseXYRelative().toFloat();
        const auto layout = orbitLayout();
        const auto centre = layout.centre;
        const float radius = layout.radius;
        const auto snapshot = processor.getCurrentSnapshotCopy();
        const auto droneFrame = processor.getCurrentDroneFrameCopy();

        if (const auto sign = signAt(point)) {
            const bool enabled = processor.isSignEffectEnabled(*sign);
            const auto assignment = static_cast<std::size_t>(processor.getSignEffectAssignment(*sign));
            return juce::String(astro::toString(*sign).data())
                + " sign marker: click to "
                + (enabled ? "disable " : "enable ")
                + signEffectLabel(assignment)
                + "; right-click or Alt-click to choose another effect.";
        }

        for (std::size_t index = 0; index < snapshot.planets.size(); ++index) {
            const auto& planet = snapshot.planets[index];
            const float orbitRadius = orbitRadiusForIndex(radius, index);
            const auto position = pointForLongitude(planet.longitudeDegrees, centre, orbitRadius);
            const auto planetName = juce::String(astro::toString(planet.planet).data());
            if (position.getDistanceFrom(point) <= 18.0f) {
                return planetName
                    + ": drag to offset orbit position; double-click to "
                    + (processor.isPlanetMuted(planet.planet) ? "unmute" : "mute")
                    + ".";
            }

            const auto& layer = droneFrame.layers[index];
            if (isPointNearTailArc(point, centre, orbitRadius, planet.longitudeDegrees, layer.tailSeconds)) {
                const int tailPercent = static_cast<int>(std::round(juce::jlimit(0.0f, 1.0f, processor.getOrbitTailLevel(planet.planet)) * 100.0f));
                return planetName
                    + " orbit tail: "
                    + juce::String(layer.tailSeconds, 1)
                    + " s decay, "
                    + juce::String(tailPercent)
                    + "% captured energy. Hold "
                    + keyNameForPlanet(planet.planet)
                    + " while Capture is on to play it.";
            }

            if (isPointNearOrbitLine(point, centre, orbitRadius)) {
                return planetName + " orbit: click to tap the main reverb tank.";
            }
        }

        for (const auto& aspect : snapshot.aspects) {
            const auto aIndex = static_cast<std::size_t>(aspect.a);
            const auto bIndex = static_cast<std::size_t>(aspect.b);
            const auto a = pointForLongitude(snapshot.planets[aIndex].longitudeDegrees, centre, orbitRadiusForIndex(radius, aIndex));
            const auto b = pointForLongitude(snapshot.planets[bIndex].longitudeDegrees, centre, orbitRadiusForIndex(radius, bIndex));
            if (distanceToLineSegment(point, a, b) <= 6.0f) {
                return juce::String(astro::toString(aspect.type).data())
                    + " aspect: relative planet position creates harmonic motion and delay tap energy.";
            }
        }

        return "Orbit map: drag planets in any mode, hold number keys 1-0 with Capture for planet tails, and hover nodes for details.";
    }

private:
    struct OutputParticle {
        float angle = 0.0f;
        float radius = 0.0f;
        float radialVelocity = 0.0f;
        float angularVelocity = 0.0f;
        float life = 0.0f;
        float maxLife = 1.0f;
        float size = 2.0f;
        std::size_t colourIndex = 0;
    };

    struct InteractionPulse {
        float radius = 0.0f;
        float life = 0.0f;
        float maxLife = 1.0f;
        float energy = 1.0f;
        std::size_t colourIndex = 0;
    };

    struct OrbitLayout {
        juce::Rectangle<float> contentArea;
        juce::Rectangle<float> harmonicsArea;
        juce::Point<float> centre;
        float radius;
        float markerRadius;
    };

    AstralReverberationsAudioProcessor& processor;
    std::optional<astro::PlanetId> draggedPlanet;
    std::optional<astro::PlanetId> selectedPlanet;
    std::vector<OutputParticle> outputParticles;
    std::vector<InteractionPulse> interactionPulses;
    juce::Random particleRandom;
    float particleSpawnAccumulator = 0.0f;
    float cymaticPhase = 0.0f;
    int dragPulseCooldown = 0;

    void timerCallback() override
    {
        cymaticPhase = std::fmod(cymaticPhase + 0.027f + processor.getOutputLevel() * 0.046f, kPi * 2.0f);
        updateOutputParticles();
        updateInteractionFeedback();
        repaint();
    }

    /// Computes the shared orbit-map geometry so paint and hit-testing stay aligned.
    OrbitLayout orbitLayout() const
    {
        auto bounds = getLocalBounds().toFloat();
        auto contentArea = bounds.reduced(12.0f);
        contentArea.removeFromTop(kOrbitTitleBand);

        const auto centre = contentArea.getCentre();
        const float orbitSpan = std::min(contentArea.getWidth() - kHarmonicsStripWidth, contentArea.getHeight());
        const float radius = orbitSpan * kOrbitRadiusFactor;

        auto harmonicsArea = contentArea;
        harmonicsArea.setLeft(contentArea.getRight() - kHarmonicsStripWidth);
        harmonicsArea = harmonicsArea.reduced(8.0f, 4.0f);

        return {contentArea, harmonicsArea, centre, radius, radius + kSignMarkerOffset};
    }

    /// Converts an astrological longitude and orbit radius into a point on the map.
    static juce::Point<float> pointForLongitude(float longitude, juce::Point<float> centre, float radius)
    {
        const float radians = (longitude - 90.0f) * kPi / 180.0f;
        return {centre.x + std::cos(radians) * radius, centre.y + std::sin(radians) * radius};
    }

    /// Hit-tests the draggable planet nodes.
    std::optional<astro::PlanetId> planetAt(juce::Point<float> point) const
    {
        const auto layout = orbitLayout();
        const auto centre = layout.centre;
        const float radius = layout.radius;
        const auto snapshot = processor.getCurrentSnapshotCopy();

        for (std::size_t index = 0; index < snapshot.planets.size(); ++index) {
            const auto position = pointForLongitude(snapshot.planets[index].longitudeDegrees, centre, orbitRadiusForIndex(radius, index));
            if (position.getDistanceFrom(point) <= 16.0f) {
                return snapshot.planets[index].planet;
            }
        }
        return std::nullopt;
    }

    /// Hit-tests the circular orbit rings without catching planet nodes.
    std::optional<astro::PlanetId> orbitAt(juce::Point<float> point) const
    {
        const auto layout = orbitLayout();
        const auto centre = layout.centre;
        const float radius = layout.radius;
        const auto snapshot = processor.getCurrentSnapshotCopy();

        for (std::size_t index = 0; index < snapshot.planets.size(); ++index) {
            const float orbitRadius = orbitRadiusForIndex(radius, index);
            if (isPointNearOrbitLine(point, centre, orbitRadius)) {
                return snapshot.planets[index].planet;
            }
        }
        return std::nullopt;
    }

    /// Hit-tests the clickable zodiac sign markers outside the orbit rings.
    std::optional<astro::ZodiacSign> signAt(juce::Point<float> point) const
    {
        const auto layout = orbitLayout();
        const auto centre = layout.centre;
        const float markerRadius = layout.markerRadius;

        for (std::size_t index = 0; index < 12; ++index) {
            const float longitude = static_cast<float>(index) * 30.0f + 15.0f;
            const auto marker = pointForLongitude(longitude, centre, markerRadius);
            if (marker.getDistanceFrom(point) <= 22.0f) {
                return static_cast<astro::ZodiacSign>(index);
            }
        }
        return std::nullopt;
    }

    std::optional<std::size_t> planetIndexForId(astro::PlanetId planet) const
    {
        const auto snapshot = processor.getCurrentSnapshotCopy();
        for (std::size_t index = 0; index < snapshot.planets.size(); ++index) {
            if (snapshot.planets[index].planet == planet) {
                return index;
            }
        }
        return std::nullopt;
    }

    void addPlanetInteractionPulse(astro::PlanetId planet, float energy)
    {
        if (dragPulseCooldown > 0 && energy < 0.6f) {
            return;
        }

        const auto layout = orbitLayout();
        if (const auto index = planetIndexForId(planet)) {
            addInteractionPulse(*index, orbitRadiusForIndex(layout.radius, *index), energy);
            dragPulseCooldown = energy < 0.6f ? 3 : 0;
        }
    }

    void addInteractionPulse(std::size_t colourIndex, float radius, float energy)
    {
        const auto layout = orbitLayout();
        const float pulseRadius = radius > 0.0f ? radius : layout.markerRadius;
        interactionPulses.push_back({
            pulseRadius,
            0.46f + juce::jlimit(0.0f, 1.0f, energy) * 0.26f,
            0.46f + juce::jlimit(0.0f, 1.0f, energy) * 0.26f,
            juce::jlimit(0.0f, 1.0f, energy),
            colourIndex
        });
        if (interactionPulses.size() > 18) {
            interactionPulses.erase(interactionPulses.begin(), interactionPulses.begin() + static_cast<std::ptrdiff_t>(interactionPulses.size() - 18));
        }
    }

    /// Returns the keyboard key that excites a planet's manual orbit tail.
    static juce::String keyNameForPlanet(astro::PlanetId planet)
    {
        return keyLabelForPlanet("1234567890", planet);
    }

    static bool isPointNearOrbitLine(juce::Point<float> point, juce::Point<float> centre, float orbitRadius)
    {
        return std::abs(point.getDistanceFrom(centre) - orbitRadius) <= 7.0f;
    }

    /// Hit-tests the visible decay arc for a planet tail.
    static bool isPointNearTailArc(
        juce::Point<float> point,
        juce::Point<float> centre,
        float orbitRadius,
        float longitudeDegrees,
        float tailSeconds)
    {
        constexpr float kMinimumTailSeconds = 1.5f;
        constexpr float kMaximumTailSeconds = 18.0f;
        const float normalizedTail = juce::jlimit(0.0f, 1.0f, (tailSeconds - kMinimumTailSeconds) / (kMaximumTailSeconds - kMinimumTailSeconds));
        const float arcDegrees = 26.0f + normalizedTail * 236.0f;
        const float distanceFromCentre = point.getDistanceFrom(centre);
        if (std::abs(distanceFromCentre - orbitRadius) > 9.0f) {
            return false;
        }

        const float pointLongitude = longitudeFromPoint(point, centre);
        const float behindHead = astral::astro::AstrologyEngine::normalizeDegrees(longitudeDegrees - pointLongitude);
        return behindHead <= arcDegrees + 2.0f || wrappedDistanceDegrees(pointLongitude, longitudeDegrees - arcDegrees) <= 3.0f;
    }

    /// Advances short-lived nodal grains that visualise the post-effect playback level.
    void updateOutputParticles()
    {
        constexpr float frameSeconds = 1.0f / 24.0f;
        const auto layout = orbitLayout();
        if (layout.contentArea.isEmpty()) {
            return;
        }

        for (auto& particle : outputParticles) {
            particle.radius += particle.radialVelocity;
            particle.angle += particle.angularVelocity;
            particle.radialVelocity *= 0.986f;
            particle.angularVelocity *= 0.992f;
            particle.life -= frameSeconds;
        }
        outputParticles.erase(
            std::remove_if(outputParticles.begin(), outputParticles.end(), [](const OutputParticle& particle) {
                return particle.life <= 0.0f;
            }),
            outputParticles.end());

        const float output = juce::jlimit(0.0f, 1.0f, processor.getOutputLevel());
        const float visibleOutput = std::pow(output, 0.42f);
        const auto snapshot = processor.getCurrentSnapshotCopy();
        const auto droneFrame = processor.getCurrentDroneFrameCopy();
        if (snapshot.planets.empty() || droneFrame.layers.empty()) {
            return;
        }

        float orbitTailActivity = 0.0f;
        float harmonicEnergy = 0.0f;
        float strongestHarmonic = 0.0f;
        std::array<float, static_cast<std::size_t>(astro::PlanetId::Count)> layerWeights{};
        for (std::size_t index = 0; index < snapshot.planets.size(); ++index) {
            const auto& planet = snapshot.planets[index];
            const auto& layer = droneFrame.layers[index % droneFrame.layers.size()];
            const float tailLevel = processor.getOrbitTailLevel(planet.planet);
            const float layerEnergy = juce::jlimit(0.0f, 1.0f, layer.amplitude * 1.2f + tailLevel * 0.65f);
            layerWeights[index] = 0.04f + layerEnergy * layerEnergy * 1.65f;
            orbitTailActivity = std::max(orbitTailActivity, tailLevel);
            harmonicEnergy += layerEnergy;
            strongestHarmonic = std::max(strongestHarmonic, layerEnergy);
        }
        harmonicEnergy = juce::jlimit(0.0f, 1.0f, harmonicEnergy / static_cast<float>(snapshot.planets.size()));

        particleSpawnAccumulator += visibleOutput * 2.5f + harmonicEnergy * 1.5f + strongestHarmonic * 1.0f + orbitTailActivity * 1.2f;
        int spawnCount = std::min(4, static_cast<int>(particleSpawnAccumulator));
        particleSpawnAccumulator -= static_cast<float>(spawnCount);

        while (spawnCount-- > 0 && outputParticles.size() < kMaxOutputParticles) {
            float totalWeight = 0.0f;
            for (std::size_t weightIndex = 0; weightIndex < snapshot.planets.size(); ++weightIndex) {
                totalWeight += layerWeights[weightIndex];
            }
            float pick = particleRandom.nextFloat() * std::max(0.0001f, totalWeight);
            std::size_t index = 0;
            for (; index + 1 < snapshot.planets.size(); ++index) {
                pick -= layerWeights[index];
                if (pick <= 0.0f) {
                    break;
                }
            }
            const auto& planet = snapshot.planets[index];
            const auto& layer = droneFrame.layers[index % droneFrame.layers.size()];
            const float haloInnerRadius = layout.radius * 1.03f;
            const float haloOuterRadius = std::max(
                haloInnerRadius + 8.0f,
                std::min(layout.contentArea.getWidth() * 0.48f, layout.contentArea.getHeight() * 0.48f));
            const float angle = (planet.longitudeDegrees - 90.0f) * kPi / 180.0f + (particleRandom.nextFloat() - 0.5f) * 0.18f;
            const float tailLevel = processor.getOrbitTailLevel(planet.planet);
            const float layerEnergy = juce::jlimit(0.0f, 1.0f, layer.amplitude * 1.2f + tailLevel * 0.65f);
            const float mode = static_cast<float>((index % 5) + 3);
            const float nodalMix = std::abs(std::sin(mode * angle + cymaticPhase));
            const float nodalRadius = haloInnerRadius + (haloOuterRadius - haloInnerRadius) * (0.18f + nodalMix * 0.76f);
            const float startRadius = juce::jlimit(haloInnerRadius, haloOuterRadius, nodalRadius + (particleRandom.nextFloat() - 0.5f) * 10.0f);
            const float outwardSpeed = 0.04f + visibleOutput * 0.20f + tailLevel * 0.15f + layerEnergy * 0.10f;
            const float spinDirection = particleRandom.nextBool() ? 1.0f : -1.0f;
            const float angularSpeed = spinDirection * (0.003f + visibleOutput * 0.006f + layer.driftRate * 0.006f + layerEnergy * 0.003f);
            const float lifetime = 0.78f + particleRandom.nextFloat() * 0.68f + layerEnergy * 0.18f;

            outputParticles.push_back({
                angle,
                startRadius,
                outwardSpeed,
                angularSpeed,
                lifetime,
                lifetime,
                0.9f + visibleOutput * 1.1f + layerEnergy * 1.45f + particleRandom.nextFloat() * 0.65f,
                index
            });
        }
    }

    /// Draws a cymatic standing-wave plate driven by planet harmonic amplitudes.
    void drawCymaticField(
        juce::Graphics& graphics,
        juce::Rectangle<float> contentArea,
        juce::Point<float> centre,
        float radius,
        const astro::AstroSnapshot& snapshot,
        const astro::AstroDroneFrame& frame)
    {
        if (snapshot.planets.empty() || frame.layers.empty()) {
            return;
        }

        juce::Graphics::ScopedSaveState cymaticScope(graphics);
        graphics.reduceClipRegion(contentArea.toNearestInt());

        const float output = juce::jlimit(0.0f, 1.0f, processor.getOutputLevel());
        const float input = juce::jlimit(0.0f, 1.0f, processor.getInputLevel());
        const float energy = juce::jlimit(0.0f, 1.0f, std::pow(output * 0.78f + input * 0.36f, 0.55f));
        const float plateRadius = radius * 0.96f;

        graphics.setColour(juce::Colour(13, 15, 18).withAlpha(0.18f + energy * 0.10f));
        graphics.fillEllipse(centre.x - plateRadius, centre.y - plateRadius, plateRadius * 2.0f, plateRadius * 2.0f);

        const int visibleLayers = static_cast<int>(std::min<std::size_t>(frame.layers.size(), snapshot.planets.size()));
        for (int ring = 1; ring <= 6; ++ring) {
            float ringEnergy = 0.0f;
            std::size_t colourIndex = static_cast<std::size_t>(ring);
            for (int index = 0; index < visibleLayers; ++index) {
                const auto& layer = frame.layers[static_cast<std::size_t>(index)];
                const float tail = processor.getOrbitTailLevel(layer.planet);
                const float layerEnergy = juce::jlimit(0.0f, 1.0f, layer.amplitude * 1.25f + tail * 0.85f);
                const float mode = static_cast<float>(((index + ring) % 7) + 2);
                ringEnergy += std::abs(std::sin(cymaticPhase * (0.42f + mode * 0.05f) + mode * 0.73f)) * layerEnergy;
                if (layerEnergy > 0.08f) {
                    colourIndex = static_cast<std::size_t>(index);
                }
            }

            const float normalized = juce::jlimit(0.0f, 1.0f, ringEnergy / std::max(1, visibleLayers));
            const float base = static_cast<float>(ring) / 7.0f;
            const float wobble = std::sin(cymaticPhase * 0.7f + static_cast<float>(ring) * 1.31f) * (1.1f + normalized * 2.6f);
            const float ringRadius = plateRadius * (0.14f + base * 0.78f) + wobble;
            const auto bounds = juce::Rectangle<float>(
                centre.x - ringRadius,
                centre.y - ringRadius,
                ringRadius * 2.0f,
                ringRadius * 2.0f);
            const float alpha = 0.026f + energy * 0.052f + normalized * 0.13f;
            graphics.setColour(accentColour(colourIndex).withAlpha(alpha));
            graphics.drawEllipse(bounds, 0.65f + normalized * 1.0f);
        }

        for (int index = 0; index < visibleLayers; index += 2) {
            const auto& layer = frame.layers[static_cast<std::size_t>(index)];
            const float layerEnergy = juce::jlimit(0.0f, 1.0f, layer.amplitude * 1.2f + processor.getOrbitTailLevel(layer.planet) * 0.9f);
            if (layerEnergy <= 0.012f) {
                continue;
            }

            const float mode = static_cast<float>((index % 4) + 3);
            const float longitude = snapshot.planets[static_cast<std::size_t>(index)].longitudeDegrees - 90.0f;
            const float phase = longitude * kPi / 180.0f + cymaticPhase * (0.38f + layer.driftRate * 0.18f);
            const int spokes = static_cast<int>(mode);
            for (int spoke = 0; spoke < spokes; ++spoke) {
                const float angle = phase + static_cast<float>(spoke) * kPi * 2.0f / static_cast<float>(spokes);
                const float inner = plateRadius * (0.12f + 0.10f * std::abs(std::sin(angle * mode)));
                const float outer = plateRadius * (0.36f + 0.40f * layerEnergy);
                const auto a = juce::Point<float>(centre.x + std::cos(angle) * inner, centre.y + std::sin(angle) * inner);
                const auto b = juce::Point<float>(centre.x + std::cos(angle) * outer, centre.y + std::sin(angle) * outer);
                graphics.setColour(accentColour(static_cast<std::size_t>(index)).withAlpha(0.012f + layerEnergy * 0.058f));
                graphics.drawLine(a.x, a.y, b.x, b.y, 0.55f + layerEnergy * 0.65f);
            }
        }

        const float centrePulse = 10.0f + energy * 15.0f + std::abs(std::sin(cymaticPhase * 1.6f)) * (2.0f + energy * 8.0f);
        graphics.setColour(juce::Colour(246, 217, 116).withAlpha(0.08f + energy * 0.16f));
        graphics.drawEllipse(centre.x - centrePulse, centre.y - centrePulse, centrePulse * 2.0f, centrePulse * 2.0f, 0.8f + energy * 0.9f);
    }

    /// Draws resonant nodal grains in the outer halo so the planet controls stay readable.
    void drawOutputParticles(juce::Graphics& graphics, juce::Rectangle<float> contentArea, juce::Point<float> centre, float radius)
    {
        juce::Graphics::ScopedSaveState particleScope(graphics);
        graphics.reduceClipRegion(contentArea.toNearestInt());

        const float output = juce::jlimit(0.0f, 1.0f, processor.getOutputLevel());
        if (output > 0.006f) {
            const float pulseRadius = radius * 1.08f;
            graphics.setColour(juce::Colour(105, 198, 212).withAlpha(std::min(0.10f, output * 0.13f)));
            graphics.drawEllipse(centre.x - pulseRadius, centre.y - pulseRadius, pulseRadius * 2.0f, pulseRadius * 2.0f, 0.8f + output * 1.4f);
        }

        for (const auto& particle : outputParticles) {
            if (particle.radius < radius * 0.98f) {
                continue;
            }
            const float age01 = 1.0f - juce::jlimit(0.0f, 1.0f, particle.life / std::max(0.0001f, particle.maxLife));
            const float alpha = (1.0f - age01) * (0.10f + output * 0.18f);
            const float size = particle.size * (0.72f + std::sin(age01 * kPi) * 0.34f);
            const juce::Point<float> position(
                centre.x + std::cos(particle.angle) * particle.radius,
                centre.y + std::sin(particle.angle) * particle.radius);
            const auto bounds = juce::Rectangle<float>(
                position.x - size * 0.5f,
                position.y - size * 0.5f,
                size,
                size);

            graphics.setColour(accentColour(particle.colourIndex).withAlpha(alpha));
            graphics.fillEllipse(bounds);
            if (output > 0.16f && size > 3.2f) {
                graphics.setColour(juce::Colour(246, 217, 116).withAlpha(alpha * 0.12f));
                graphics.drawEllipse(bounds.expanded(size * 0.46f), 0.7f);
            }
        }
    }

    void updateInteractionFeedback()
    {
        constexpr float frameSeconds = 1.0f / 24.0f;
        if (dragPulseCooldown > 0) {
            --dragPulseCooldown;
        }

        for (auto& pulse : interactionPulses) {
            pulse.radius += 0.9f + pulse.energy * 2.2f;
            pulse.life -= frameSeconds;
        }
        interactionPulses.erase(
            std::remove_if(interactionPulses.begin(), interactionPulses.end(), [](const InteractionPulse& pulse) {
                return pulse.life <= 0.0f;
            }),
            interactionPulses.end());
    }

    void drawInteractionFeedback(juce::Graphics& graphics, juce::Point<float> centre, float radius)
    {
        if (const auto hoveredPlanet = orbitAt(getMouseXYRelative().toFloat())) {
            if (const auto index = planetIndexForId(*hoveredPlanet)) {
                const float orbitRadius = orbitRadiusForIndex(radius, *index);
                const auto bounds = juce::Rectangle<float>(
                    centre.x - orbitRadius,
                    centre.y - orbitRadius,
                    orbitRadius * 2.0f,
                    orbitRadius * 2.0f);
                graphics.setColour(accentColour(*index).withAlpha(0.34f));
                graphics.drawEllipse(bounds, 2.2f);
            }
        }

        for (const auto& pulse : interactionPulses) {
            const float age01 = 1.0f - juce::jlimit(0.0f, 1.0f, pulse.life / std::max(0.0001f, pulse.maxLife));
            const float alpha = (1.0f - age01) * (0.22f + pulse.energy * 0.48f);
            const float thickness = 1.1f + pulse.energy * 2.2f;
            const auto bounds = juce::Rectangle<float>(
                centre.x - pulse.radius,
                centre.y - pulse.radius,
                pulse.radius * 2.0f,
                pulse.radius * 2.0f);
            graphics.setColour(accentColour(pulse.colourIndex).withAlpha(alpha));
            graphics.drawEllipse(bounds, thickness);
            graphics.setColour(juce::Colour(246, 217, 116).withAlpha(alpha * 0.38f));
            graphics.drawEllipse(bounds.expanded(3.0f + age01 * 7.0f), 0.8f);
        }

        if (selectedPlanet.has_value()) {
            if (const auto index = planetIndexForId(*selectedPlanet)) {
                const float orbitRadius = orbitRadiusForIndex(radius, *index);
                const auto bounds = juce::Rectangle<float>(
                    centre.x - orbitRadius,
                    centre.y - orbitRadius,
                    orbitRadius * 2.0f,
                    orbitRadius * 2.0f);
                graphics.setColour(accentColour(*index).withAlpha(0.18f));
                graphics.drawEllipse(bounds.expanded(4.0f), 1.4f);
            }
        }
    }

    /// Draws the 10-degree and sign-boundary tick marks.
    void drawZodiac(juce::Graphics& graphics, juce::Point<float> centre, float radius)
    {
        graphics.setColour(lineColour().withAlpha(0.72f));
        for (int tick = 0; tick < 36; ++tick) {
            const float longitude = static_cast<float>(tick) * 10.0f;
            const auto outer = pointForLongitude(longitude, centre, radius);
            const auto inner = pointForLongitude(longitude, centre, radius - (tick % 3 == 0 ? 12.0f : 6.0f));
            graphics.drawLine(inner.x, inner.y, outer.x, outer.y, tick % 3 == 0 ? 1.2f : 0.7f);
        }
    }

    /// Draws active zodiac effect markers around the orbit map.
    void drawSignMarkers(juce::Graphics& graphics, juce::Point<float> centre, float markerRadius, const astro::AstroSnapshot& snapshot)
    {
        std::array<float, 12> signActivity{};
        for (const auto& planet : snapshot.planets) {
            const auto sign = astro::AstrologyEngine::signForLongitude(planet.longitudeDegrees);
            signActivity[static_cast<std::size_t>(sign)] += 0.18f;
        }

        for (std::size_t index = 0; index < signActivity.size(); ++index) {
            const float longitude = static_cast<float>(index) * 30.0f + 15.0f;
            const auto point = pointForLongitude(longitude, centre, markerRadius);
            const float activity = juce::jlimit(0.0f, 1.0f, signActivity[index]);
            const auto markerBounds = juce::Rectangle<float>(point.x - 20.0f, point.y - 15.0f, 40.0f, 30.0f);
            const auto sign = static_cast<astro::ZodiacSign>(index);
            const bool enabled = processor.isSignEffectEnabled(sign);
            const auto assignment = static_cast<std::size_t>(processor.getSignEffectAssignment(sign));
            graphics.setColour((enabled ? juce::Colour(43, 47, 54) : juce::Colour(25, 27, 31)).withAlpha(0.88f));
            graphics.fillRoundedRectangle(markerBounds, 6.0f);
            graphics.setColour((enabled ? juce::Colour(105, 198, 212) : juce::Colour(92, 98, 105)).withAlpha(0.22f + activity * 0.55f));
            graphics.drawRoundedRectangle(markerBounds, 6.0f, 1.2f + activity * 1.8f);
            graphics.setColour(enabled ? juce::Colour(235, 239, 242) : juce::Colour(105, 112, 120));
            graphics.setFont(zodiacGlyphFont(16.0f));
            graphics.drawText(signGlyph(index), markerBounds.withTrimmedBottom(13.0f), juce::Justification::centred);
            graphics.setColour(enabled ? juce::Colour(185, 195, 202) : juce::Colour(91, 98, 106));
            graphics.setFont(juce::FontOptions(8.0f));
            graphics.drawText(signEffectLabel(assignment), markerBounds.withTrimmedTop(14.0f), juce::Justification::centred);
            if (!enabled) {
                graphics.drawLine(markerBounds.getX() + 7.0f, markerBounds.getBottom() - 6.0f, markerBounds.getRight() - 7.0f, markerBounds.getY() + 6.0f, 1.6f);
            }
        }
    }

    /// Draws aspect lines between planet pairs that are currently close to target angles.
    void drawAspects(juce::Graphics& graphics, const astro::AstroSnapshot& snapshot, juce::Point<float> centre, float radius)
    {
        for (const auto& aspect : snapshot.aspects) {
            const auto aIndex = static_cast<std::size_t>(aspect.a);
            const auto bIndex = static_cast<std::size_t>(aspect.b);
            const auto a = pointForLongitude(snapshot.planets[aIndex].longitudeDegrees, centre, orbitRadiusForIndex(radius, aIndex));
            const auto b = pointForLongitude(snapshot.planets[bIndex].longitudeDegrees, centre, orbitRadiusForIndex(radius, bIndex));
            const auto colour = aspect.type == astro::AspectType::Square || aspect.type == astro::AspectType::Opposition
                ? juce::Colour(224, 111, 87)
                : juce::Colour(105, 198, 212);
            graphics.setColour(colour.withAlpha(0.18f + aspect.strength01 * 0.30f));
            graphics.drawLine(a.x, a.y, b.x, b.y, 0.8f + aspect.strength01 * 1.1f);
        }
    }

    /// Draws one planet's maximum tail length and current captured-tail energy.
    void drawTailArc(
        juce::Graphics& graphics,
        juce::Point<float> centre,
        float orbitRadius,
        float longitudeDegrees,
        float tailSeconds,
        float tailLevel,
        bool active,
        juce::Colour colour)
    {
        constexpr float kMinimumTailSeconds = 1.5f;
        constexpr float kMaximumTailSeconds = 18.0f;
        const float normalizedTail = juce::jlimit(0.0f, 1.0f, (tailSeconds - kMinimumTailSeconds) / (kMaximumTailSeconds - kMinimumTailSeconds));
        const float arcDegrees = 26.0f + normalizedTail * 236.0f;
        const int segments = 24 + static_cast<int>(normalizedTail * 28.0f);
        juce::Path tailPath;

        for (int segment = 0; segment <= segments; ++segment) {
            const float proportion = static_cast<float>(segment) / static_cast<float>(segments);
            const float longitude = longitudeDegrees - arcDegrees * proportion;
            const auto point = pointForLongitude(longitude, centre, orbitRadius);
            if (segment == 0) {
                tailPath.startNewSubPath(point);
            } else {
                tailPath.lineTo(point);
            }
        }

        const float level = juce::jlimit(0.0f, 1.0f, tailLevel);
        const float idleAlpha = 0.08f + normalizedTail * 0.10f;
        const float alpha = active ? 0.92f : idleAlpha + level * 0.72f;
        const float thickness = active ? 5.6f : 1.4f + normalizedTail * 1.2f + level * 4.2f;
        graphics.setColour(colour.withAlpha(alpha));
        graphics.strokePath(tailPath, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float markerLongitude = longitudeDegrees - arcDegrees;
        const auto marker = pointForLongitude(markerLongitude, centre, orbitRadius);
        const float markerSize = 4.0f + level * 8.0f;
        graphics.setColour(colour.withAlpha(active ? 0.95f : 0.28f + level * 0.62f));
        graphics.fillEllipse(marker.x - markerSize * 0.5f, marker.y - markerSize * 0.5f, markerSize, markerSize);

        if (active || level > 0.08f) {
            const float glowSize = 11.0f + level * 13.0f;
            graphics.setColour(juce::Colour(246, 217, 116).withAlpha(active ? 0.9f : level * 0.55f));
            graphics.drawEllipse(marker.x - glowSize * 0.5f, marker.y - glowSize * 0.5f, glowSize, glowSize, 1.4f);
        }
    }

    /// Draws the compact harmonic-layer and tail-length strip beside the orbit map.
    void drawSpectrum(juce::Graphics& graphics, juce::Rectangle<float> area, const astro::AstroDroneFrame& frame)
    {
        graphics.setColour(juce::Colour(43, 47, 54));
        graphics.fillRoundedRectangle(area, 7.0f);
        graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        graphics.setColour(juce::Colour(235, 239, 242));
        graphics.drawText("Modes", area.removeFromTop(18.0f), juce::Justification::centred);

        const float bottom = area.getBottom() - 8.0f;
        const float maxHeight = area.getHeight() - 12.0f;
        const float width = area.getWidth() / static_cast<float>(frame.layers.size());
        for (std::size_t index = 0; index < frame.layers.size(); ++index) {
            const float barHeight = frame.layers[index].amplitude * maxHeight;
            const auto bar = juce::Rectangle<float>(area.getX() + static_cast<float>(index) * width + 2.0f, bottom - barHeight, width - 4.0f, barHeight);
            const bool active = processor.isManualOrbitTailActive(frame.layers[index].planet);
            graphics.setColour(accentColour(index).withAlpha(active ? 1.0f : 0.82f));
            graphics.fillRoundedRectangle(bar, 2.0f);
            if (active) {
                graphics.setColour(juce::Colour(246, 217, 116));
                graphics.drawRoundedRectangle(bar.expanded(1.0f), 2.0f, 1.2f);
            }
            graphics.setColour(juce::Colour(185, 195, 202));
            graphics.setFont(juce::FontOptions(8.0f));
            graphics.drawText(juce::String(frame.layers[index].tailSeconds, 1),
                juce::Rectangle<float>(area.getX() + static_cast<float>(index) * width, bottom + 1.0f, width, 10.0f),
                juce::Justification::centred);
        }
    }
};

AstralReverberationsEditor::AstralReverberationsEditor(AstralReverberationsAudioProcessor& owner)
    : AudioProcessorEditor(&owner),
      audioProcessor(owner),
      orbitMap(std::make_unique<OrbitMapComponent>(owner)),
      tooltipWindow(this, 450)
{
    setWantsKeyboardFocus(true);

    titleLabel.setText("Astral Reverberations", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(234, 226, 202));
    titleLabel.setFont(juce::FontOptions(24.0f, juce::Font::bold));
    titleLabel.setTooltip("Astral Reverberations: orbit-controlled drone, capture tails, delay, and reverb.");
    addAndMakeVisible(titleLabel);

    statusLabel.setJustificationType(juce::Justification::centredRight);
    statusLabel.setColour(juce::Label::textColourId, juce::Colour(185, 195, 202));
    statusLabel.setFont(juce::FontOptions(13.0f));
    statusLabel.setTooltip("Shows gate state, effective root source, and live input level.");
    addAndMakeVisible(statusLabel);

    controlsButton.setButtonText("Controls");
    controlsButton.setTooltip("Shows keyboard and orbit interaction controls.");
    controlsButton.onClick = [this] {
        controlsPopupVisible = !controlsPopupVisible;
        repaint();
    };
    addAndMakeVisible(controlsButton);

    addAndMakeVisible(*orbitMap);

    addSlider("macro_choir", "Choir", SliderLayout::Inspector);
    addSlider("macro_mneme", "Mneme", SliderLayout::Inspector);
    addSlider("macro_ephemeris", "Ephemeris", SliderLayout::Inspector);
    addSlider("macro_root", "Root Drift", SliderLayout::Inspector);
    addSlider("euclid_rate", "Rate", SliderLayout::Inspector);
    addSlider("euclid_wet", "Wet", SliderLayout::Inspector);
    addSlider("macro_substance", "Substance", SliderLayout::Bottom);
    addSlider("macro_fate", "Fate", SliderLayout::Bottom);
    addSlider("macro_void", "Void", SliderLayout::Bottom);
    addSlider("macro_pulse", "Ring", SliderLayout::Bottom);
    addSlider("output_gain", "Output", SliderLayout::Bottom);

    droneHoldButton.setButtonText("Drone On");
    droneHoldButton.setColour(juce::ToggleButton::textColourId, juce::Colour(235, 239, 242));
    droneHoldButton.setTooltip("Opens the drone gate without MIDI, useful in standalone mode.");
    addAndMakeVisible(droneHoldButton);
    droneHoldAttachment = std::make_unique<ButtonAttachment>(audioProcessor.parameters(), "drone_hold", droneHoldButton);

    for (int note = 24; note <= 84; ++note) {
        rootNoteBox.addItem(noteName(note), note + 1);
    }
    rootNoteBox.onChange = [this] {
        const int note = rootNoteBox.getSelectedId() - 1;
        if (note >= 24 && note <= 84) {
            setParameterValue("root_note", static_cast<float>(note));
        }
    };
    rootNoteBox.setTooltip("Chooses the manual drone root note.");
    addAndMakeVisible(rootNoteBox);
    updateRootNoteBox();

    rootLockButton.setButtonText("Manual Root");
    rootLockButton.setColour(juce::ToggleButton::textColourId, juce::Colour(235, 239, 242));
    rootLockButton.setTooltip("Keeps the selected root stable instead of following incoming MIDI notes.");
    addAndMakeVisible(rootLockButton);
    rootLockAttachment = std::make_unique<ButtonAttachment>(audioProcessor.parameters(), "root_lock", rootLockButton);

    freezeButton.setButtonText("Freeze");
    freezeButton.setColour(juce::ToggleButton::textColourId, juce::Colour(235, 239, 242));
    freezeButton.setTooltip("Freezes the main delay/reverb feedback path, not the planet tail buffers.");
    addAndMakeVisible(freezeButton);
    freezeAttachment = std::make_unique<ButtonAttachment>(audioProcessor.parameters(), "freeze", freezeButton);

    euclidButton.setButtonText("Euclid");
    euclidButton.setColour(juce::ToggleButton::textColourId, juce::Colour(235, 239, 242));
    euclidButton.setTooltip("Runs planet-position Euclidean rhythms that trigger ring plucks. Planet longitude rotates each pattern.");
    addAndMakeVisible(euclidButton);
    euclidAttachment = std::make_unique<ButtonAttachment>(audioProcessor.parameters(), "euclid_enable", euclidButton);

    captureButton.setButtonText("Capture");
    captureButton.setColour(juce::ToggleButton::textColourId, juce::Colour(235, 239, 242));
    captureButton.setTooltip("Arms input capture. Hold number keys 1-0 to feed planet-specific orbit tails.");
    addAndMakeVisible(captureButton);
    captureAttachment = std::make_unique<ButtonAttachment>(audioProcessor.parameters(), "capture_enable", captureButton);

    inputMonitorButton.setButtonText("Input Monitor");
    inputMonitorButton.setColour(juce::ToggleButton::textColourId, juce::Colour(235, 239, 242));
    inputMonitorButton.setTooltip("Keeps live input audible at unity level, useful for monitoring a microphone in standalone mode.");
    addAndMakeVisible(inputMonitorButton);
    inputMonitorAttachment = std::make_unique<ButtonAttachment>(audioProcessor.parameters(), "input_monitor", inputMonitorButton);

    resetPlanetButton.setButtonText("Reset Planet");
    resetPlanetButton.setTooltip("Clears the selected planet's manual longitude offset.");
    resetPlanetButton.onClick = [this] {
        audioProcessor.resetPlanetLongitude(orbitMap->getSelectedPlanet());
    };
    addAndMakeVisible(resetPlanetButton);

    resetOrbitsButton.setButtonText("Reset Orbits");
    resetOrbitsButton.setTooltip("Clears all planet longitude offsets.");
    resetOrbitsButton.onClick = [this] {
        audioProcessor.resetPlanetLongitudes();
    };
    addAndMakeVisible(resetOrbitsButton);

    slowButton.setButtonText("Slow");
    slowButton.setTooltip("Sets simulated orbit motion to slow speed.");
    slowButton.onClick = [this] { setParameterValue("astro_speed", 0.25f); };
    addAndMakeVisible(slowButton);

    normalButton.setButtonText("Normal");
    normalButton.setTooltip("Sets simulated orbit motion to normal speed.");
    normalButton.onClick = [this] { setParameterValue("astro_speed", 1.0f); };
    addAndMakeVisible(normalButton);

    fastButton.setButtonText("Fast");
    fastButton.setTooltip("Sets simulated orbit motion to fast speed.");
    fastButton.onClick = [this] { setParameterValue("astro_speed", 4.0f); };
    addAndMakeVisible(fastButton);

    astroModeBox.addItem("Now", 1);
    astroModeBox.addItem("Manual Date", 2);
    astroModeBox.addItem("Simulated Orbit", 3);
    astroModeBox.setTooltip("Chooses orbit source: clock-based Now, fixed Manual Date, or animated Simulated Orbit. Drag offsets work in all modes.");
    astroModeBox.setSelectedId(static_cast<int>(audioProcessor.getAstroMode()) + 1, juce::dontSendNotification);
    astroModeBox.onChange = [this] {
        audioProcessor.setAstroMode(static_cast<AstralReverberationsAudioProcessor::AstroMode>(
            astroModeBox.getSelectedId() - 1));
    };
    addAndMakeVisible(astroModeBox);

    historicalPresetBox.addItem("Custom date", 1);
    for (std::size_t index = 0; index < astro::historicalPresets().size(); ++index) {
        const auto& preset = astro::historicalPresets()[index];
        historicalPresetBox.addItem(juce::String(preset.label.data()), static_cast<int>(index + 2));
    }
    historicalPresetBox.setTooltip(
        "Recalls approximate planet positions for a historical UTC moment. Switches to Manual Date.");
    historicalPresetBox.onChange = [this] {
        if (syncingHistoricalPresetUi) {
            return;
        }
        const int selectedId = historicalPresetBox.getSelectedId();
        if (selectedId <= 1) {
            return;
        }
        applyHistoricalPreset(static_cast<std::size_t>(selectedId - 2));
    };
    addAndMakeVisible(historicalPresetBox);
    syncHistoricalPresetBox();

    setSize(1220, 760);
    startTimerHz(12);
}

void AstralReverberationsEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(16, 17, 20));
    drawAstralTexture(graphics, getLocalBounds().toFloat(), kEditorBackgroundTextureOpacity);
    graphics.fillAll(juce::Colour(16, 17, 20).withAlpha(kEditorBackgroundVeilAlpha));

    auto bounds = getLocalBounds().toFloat().reduced(8.0f);
    graphics.setColour(juce::Colour(52, 58, 66));
    graphics.drawRoundedRectangle(bounds, 8.0f, 1.5f);
    drawInputMeter(graphics);
}

void AstralReverberationsEditor::paintOverChildren(juce::Graphics& graphics)
{
    if (controlsPopupVisible) {
        drawControlsPopup(graphics);
    }
}

bool AstralReverberationsEditor::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && controlsPopupVisible) {
        controlsPopupVisible = false;
        repaint();
        return true;
    }

    updateKeyboardControls();
    const auto character = key.getTextCharacter();
    return planetForPlaybackKey(character).has_value()
        || planetForCaptureKey(character).has_value()
        || planetForMuteKey(character).has_value();
}

bool AstralReverberationsEditor::keyStateChanged(bool)
{
    updateKeyboardControls();
    return true;
}

void AstralReverberationsEditor::resized()
{
    auto bounds = getLocalBounds().reduced(kPadding);
    auto header = bounds.removeFromTop(kHeaderHeight);
    titleLabel.setBounds(header.removeFromLeft(360));
    astroModeBox.setBounds(header.removeFromRight(184).reduced(0, 10));
    controlsButton.setBounds(header.removeFromRight(96).reduced(8, 10));
    statusLabel.setBounds(header);

    bounds.removeFromTop(10);
    auto bottom = bounds.removeFromBottom(kBottomHeight);
    bounds.removeFromBottom(10);
    auto inspector = bounds.removeFromRight(kInspectorWidth);
    bounds.removeFromRight(10);

    orbitMap->setBounds(bounds);

    auto rootRow = inspector.removeFromTop(34).reduced(8, 2);
    rootNoteBox.setBounds(rootRow.removeFromLeft(86));
    rootLockButton.setBounds(rootRow);

    auto speedRow = inspector.removeFromTop(34).reduced(8, 2);
    const int speedWidth = speedRow.getWidth() / 3;
    slowButton.setBounds(speedRow.removeFromLeft(speedWidth).reduced(2, 0));
    normalButton.setBounds(speedRow.removeFromLeft(speedWidth).reduced(2, 0));
    fastButton.setBounds(speedRow.reduced(2, 0));

    historicalPresetBox.setBounds(inspector.removeFromTop(34).reduced(8, 2));

    auto resetRow = inspector.removeFromTop(34).reduced(8, 2);
    resetPlanetButton.setBounds(resetRow.removeFromLeft(resetRow.getWidth() / 2).reduced(2, 0));
    resetOrbitsButton.setBounds(resetRow.reduced(2, 0));

    inspector.removeFromTop(kInputMeterHeight);

    constexpr int kInspectorLabelWidth = 84;
    for (int index = 0; index < 4; ++index) {
        auto row = inspector.removeFromTop(kInspectorSliderRowHeight).reduced(8, 3);
        labels[index]->setBounds(row.removeFromLeft(kInspectorLabelWidth));
        sliders[index]->setBounds(row);
    }

    auto toggleGrid = inspector.removeFromTop(58).reduced(8, 2);
    const int toggleColumnWidth = toggleGrid.getWidth() / 2;
    const int toggleRowHeight = toggleGrid.getHeight() / 2;
    auto toggleTopRow = toggleGrid.removeFromTop(toggleRowHeight);
    droneHoldButton.setBounds(toggleTopRow.removeFromLeft(toggleColumnWidth).reduced(2, 1));
    inputMonitorButton.setBounds(toggleTopRow.reduced(2, 1));
    captureButton.setBounds(toggleGrid.removeFromTop(toggleRowHeight).removeFromLeft(toggleColumnWidth).reduced(2, 1));
    euclidButton.setBounds(toggleGrid.reduced(2, 1));

    for (int index = 4; index < 6; ++index) {
        auto row = inspector.removeFromTop(kInspectorSliderRowHeight).reduced(8, 3);
        labels[index]->setBounds(row.removeFromLeft(kInspectorLabelWidth));
        sliders[index]->setBounds(row);
    }

    freezeButton.setBounds(inspector.removeFromTop(26).reduced(8, 2));

    const int bottomSliderStart = 6;
    const int bottomSliderCount = sliders.size() - bottomSliderStart;
    const int cellWidth = bottom.getWidth() / std::max(1, bottomSliderCount);
    for (int index = bottomSliderStart; index < sliders.size(); ++index) {
        const int column = index - bottomSliderStart;
        auto cell = juce::Rectangle<int>(bottom.getX() + column * cellWidth, bottom.getY(), cellWidth, bottom.getHeight()).reduced(4);
        labels[index]->setBounds(cell.removeFromTop(14));
        sliders[index]->setBounds(cell);
    }
}

void AstralReverberationsEditor::updateKeyboardControls()
{
    constexpr std::array<juce::juce_wchar, 10> playbackKeys{{'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p'}};
    constexpr std::array<juce::juce_wchar, 10> captureKeys{{'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'}};
    constexpr std::array<juce::juce_wchar, 10> muteKeys{{'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';'}};

    for (std::size_t index = 0; index < playbackKeys.size(); ++index) {
        const auto planet = static_cast<astro::PlanetId>(index);
        const bool playIsDown = juce::KeyPress::isKeyCurrentlyDown(playbackKeys[index]);
        if (playIsDown && !playbackKeyWasDown[index]) {
            audioProcessor.tapOrbitReverbTank(planet);
            orbitMap->showPlanetFeedback(planet, 0.92f);
        }
        playbackKeyWasDown[index] = playIsDown;

        const bool captureIsDown = juce::KeyPress::isKeyCurrentlyDown(captureKeys[index]);
        if (captureIsDown && !captureKeyWasDown[index] && !audioProcessor.isPlanetMuted(planet)) {
            orbitMap->showPlanetFeedback(planet, 0.58f);
        }
        audioProcessor.setManualOrbitTailGate(planet, captureIsDown);
        captureKeyWasDown[index] = captureIsDown;
    }

    for (std::size_t index = 0; index < muteKeys.size(); ++index) {
        const bool isDown = juce::KeyPress::isKeyCurrentlyDown(muteKeys[index]);
        if (isDown && !muteKeyWasDown[index]) {
            const auto planet = static_cast<astro::PlanetId>(index);
            audioProcessor.togglePlanetMute(planet);
            orbitMap->showPlanetFeedback(planet, 0.72f);
        }
        muteKeyWasDown[index] = isDown;
    }
}

static float defaultSliderValue(const juce::String& parameterId)
{
    if (parameterId == "macro_substance") {
        return 0.42f;
    }
    if (parameterId == "macro_mneme") {
        return 0.38f;
    }
    if (parameterId == "macro_choir") {
        return 0.52f;
    }
    if (parameterId == "macro_ephemeris") {
        return 0.5f;
    }
    if (parameterId == "macro_fate") {
        return 0.48f;
    }
    if (parameterId == "macro_void") {
        return 0.54f;
    }
    if (parameterId == "macro_pulse") {
        return 0.58f;
    }
    if (parameterId == "macro_root") {
        return 0.5f;
    }
    if (parameterId == "euclid_rate") {
        return 4.0f;
    }
    if (parameterId == "euclid_wet") {
        return 1.0f;
    }
    if (parameterId == "output_gain") {
        return 1.0f;
    }
    return 0.0f;
}

void AstralReverberationsEditor::addSlider(const char* parameterId, const juce::String& labelText, SliderLayout layout)
{
    const juce::String parameter(parameterId);
    auto* label = labels.add(new juce::Label());
    label->setText(labelText, juce::dontSendNotification);
    label->setJustificationType(layout == SliderLayout::Inspector ? juce::Justification::centredLeft
                                                                  : juce::Justification::centred);
    label->setColour(juce::Label::textColourId, juce::Colour(235, 239, 242));
    label->setFont(juce::FontOptions(layout == SliderLayout::Inspector ? 12.0f : 11.0f, juce::Font::bold));
    label->setTooltip(sliderTooltip(parameter));
    addAndMakeVisible(label);

    auto* slider = sliders.add(new juce::Slider());
    if (layout == SliderLayout::Inspector) {
        slider->setSliderStyle(juce::Slider::LinearHorizontal);
        slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 46, 18);
        slider->setColour(juce::Slider::trackColourId, juce::Colour(52, 58, 66));
        slider->setColour(juce::Slider::backgroundColourId, juce::Colour(25, 27, 31));
        slider->setColour(juce::Slider::thumbColourId, juce::Colour(243, 213, 138));
    } else {
        slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 58, 14);
        slider->setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(105, 198, 212));
        slider->setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(52, 58, 66));
        slider->setColour(juce::Slider::thumbColourId, juce::Colour(243, 213, 138));
    }
    slider->setColour(juce::Slider::textBoxTextColourId, juce::Colour(235, 239, 242));
    slider->setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(25, 27, 31));
    slider->setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider->setScrollWheelEnabled(true);
    slider->setPopupDisplayEnabled(true, true, this);
    slider->setDoubleClickReturnValue(true, static_cast<double>(defaultSliderValue(parameter)));
    slider->textFromValueFunction = [parameter](double value) {
        return sliderText(parameter, value);
    };
    slider->setTooltip(sliderTooltip(parameter));
    addAndMakeVisible(slider);

    sliderAttachments.push_back(std::make_unique<SliderAttachment>(
        audioProcessor.parameters(),
        parameterId,
        *slider));
}

void AstralReverberationsEditor::setParameterValue(const char* parameterId, float value)
{
    if (auto* parameter = audioProcessor.parameters().getParameter(parameterId)) {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
        parameter->endChangeGesture();
    }
}

void AstralReverberationsEditor::updateRootNoteBox()
{
    if (auto* value = audioProcessor.parameters().getRawParameterValue("root_note")) {
        const int note = static_cast<int>(std::round(value->load()));
        if (rootNoteBox.getSelectedId() != note + 1) {
            rootNoteBox.setSelectedId(note + 1, juce::dontSendNotification);
        }
    }
}

void AstralReverberationsEditor::timerCallback()
{
    updateRootNoteBox();
    syncHistoricalPresetBox();
    updateStatus();
    repaint();
}

void AstralReverberationsEditor::applyHistoricalPreset(std::size_t presetIndex)
{
    const auto presets = astro::historicalPresets();
    if (presetIndex >= presets.size()) {
        return;
    }

    audioProcessor.setManualJulianDay(astro::julianDayForPreset(presets[presetIndex]));
    audioProcessor.setAstroMode(AstralReverberationsAudioProcessor::AstroMode::ManualDate);
    astroModeBox.setSelectedId(static_cast<int>(AstralReverberationsAudioProcessor::AstroMode::ManualDate) + 1, juce::dontSendNotification);
    syncHistoricalPresetBox();
}

void AstralReverberationsEditor::syncHistoricalPresetBox()
{
    const juce::ScopedValueSetter<bool> guard(syncingHistoricalPresetUi, true);
    const int customId = 1;
    if (const auto match = astro::findPresetIndexForJulianDay(audioProcessor.getManualJulianDay())) {
        historicalPresetBox.setSelectedId(static_cast<int>(*match + 2), juce::dontSendNotification);
        return;
    }
    historicalPresetBox.setSelectedId(customId, juce::dontSendNotification);
}

void AstralReverberationsEditor::updateStatus()
{
    const auto source = audioProcessor.isManualRootLocked() ? "Manual" : "MIDI";
    const auto root = noteName(audioProcessor.getDroneRootNote());
    const int inputPercent = static_cast<int>(std::round(juce::jlimit(0.0f, 1.0f, audioProcessor.getInputLevel()) * 100.0f));
    const auto inputText = "  In " + juce::String(inputPercent) + "%";
    juce::String skyText;
    switch (audioProcessor.getAstroMode()) {
        case AstralReverberationsAudioProcessor::AstroMode::Now:
            skyText = "Sky Now";
            break;
        case AstralReverberationsAudioProcessor::AstroMode::ManualDate:
            if (const auto match = astro::findPresetIndexForJulianDay(audioProcessor.getManualJulianDay())) {
                skyText = juce::String(astro::historicalPresets()[*match].label.data());
            } else {
                skyText = "Sky Manual";
            }
            break;
        case AstralReverberationsAudioProcessor::AstroMode::SimulatedOrbit:
            skyText = "Sky Sim";
            break;
    }
    const auto rootText = audioProcessor.isDroneHeld()
        ? skyText + "  Gate Hold  Root " + root + "  " + source + inputText
        : audioProcessor.isDroneGateOpen()
            ? skyText + "  Gate Live  Root " + root + "  " + source + inputText
        : skyText + "  Gate Idle  Root " + root + "  " + source + inputText;
    statusLabel.setText(rootText, juce::dontSendNotification);
}

void AstralReverberationsEditor::drawInputMeter(juce::Graphics& graphics)
{
    auto bounds = getLocalBounds().reduced(kPadding);
    bounds.removeFromTop(kHeaderHeight);
    bounds.removeFromTop(10);
    bounds.removeFromBottom(kBottomHeight);
    bounds.removeFromBottom(10);
    auto inspector = bounds.removeFromRight(kInspectorWidth);

    inspector.removeFromTop(34);
    inspector.removeFromTop(34);
    inspector.removeFromTop(34);
    inspector.removeFromTop(34);
    auto meter = inspector.removeFromTop(kInputMeterHeight).reduced(8, 4).toFloat();

    const float rawLevel = juce::jlimit(0.0f, 1.0f, audioProcessor.getInputLevel());
    const float displayLevel = std::pow(rawLevel, 0.35f);
    const int inputPercent = static_cast<int>(std::round(rawLevel * 100.0f));
    const bool captureEnabled = audioProcessor.parameters().getRawParameterValue("capture_enable") != nullptr
        && audioProcessor.parameters().getRawParameterValue("capture_enable")->load() >= 0.5f;

    const auto activeColour = rawLevel > 0.01f
        ? juce::Colour(105, 198, 212)
        : (captureEnabled ? juce::Colour(224, 111, 87) : juce::Colour(92, 98, 105));
    const auto statusText = rawLevel > 0.01f
        ? juce::String(inputPercent) + "%"
        : (captureEnabled ? "CHECK INPUT" : "IDLE");

    graphics.setColour(juce::Colour(25, 27, 31).withAlpha(0.94f));
    graphics.fillRoundedRectangle(meter, 6.0f);
    graphics.setColour(activeColour.withAlpha(captureEnabled || rawLevel > 0.01f ? 0.9f : 0.45f));
    graphics.drawRoundedRectangle(meter, 6.0f, 1.3f);

    auto labelArea = meter.removeFromTop(14.0f);
    graphics.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    graphics.setColour(juce::Colour(235, 239, 242));
    graphics.drawText("INPUT", labelArea.removeFromLeft(48.0f), juce::Justification::centredLeft);
    graphics.setColour(activeColour);
    graphics.drawText(statusText, labelArea, juce::Justification::centredRight);

    auto bar = meter.reduced(0.0f, 6.0f);
    graphics.setColour(juce::Colour(52, 58, 66));
    graphics.fillRoundedRectangle(bar, 3.0f);
    auto fill = bar;
    fill.setWidth(std::max(2.0f, bar.getWidth() * displayLevel));
    graphics.setColour(activeColour.withAlpha(rawLevel > 0.0f ? 0.95f : 0.35f));
    graphics.fillRoundedRectangle(fill, 3.0f);

    if (captureEnabled && rawLevel <= 0.01f) {
        graphics.setFont(juce::FontOptions(7.5f, juce::Font::bold));
        graphics.setColour(juce::Colour(224, 111, 87).withAlpha(0.85f));
        graphics.drawText("Options > Audio input / macOS mic permission", bar.translated(0.0f, 8.0f), juce::Justification::centred);
    }
}

void AstralReverberationsEditor::drawControlsPopup(juce::Graphics& graphics)
{
    auto popup = juce::Rectangle<float>(
        static_cast<float>(getWidth() - kInspectorWidth - 332),
        static_cast<float>(kPadding + kHeaderHeight + 8),
        320.0f,
        314.0f);

    graphics.setColour(juce::Colour(7, 8, 10).withAlpha(0.38f));
    graphics.fillRoundedRectangle(popup.translated(0.0f, 6.0f), 8.0f);
    graphics.setColour(juce::Colour(25, 27, 31).withAlpha(0.97f));
    graphics.fillRoundedRectangle(popup, 8.0f);
    graphics.setColour(juce::Colour(105, 198, 212).withAlpha(0.72f));
    graphics.drawRoundedRectangle(popup, 8.0f, 1.4f);

    auto content = popup.reduced(16.0f, 14.0f);
    graphics.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    graphics.setColour(juce::Colour(234, 226, 202));
    graphics.drawText("Controls", content.removeFromTop(22.0f), juce::Justification::centredLeft);

    graphics.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    graphics.setColour(juce::Colour(185, 195, 202));
    graphics.drawText("QWERTY plucks    1-0 captures    ASDF mutes", content.removeFromTop(18.0f), juce::Justification::centredLeft);
    content.removeFromTop(8.0f);

    const auto drawKey = [&graphics](juce::Rectangle<float> key, const juce::String& text, juce::Colour colour) {
        graphics.setColour(juce::Colour(43, 47, 54));
        graphics.fillRoundedRectangle(key, 4.0f);
        graphics.setColour(colour.withAlpha(0.78f));
        graphics.drawRoundedRectangle(key, 4.0f, 1.1f);
        graphics.setColour(juce::Colour(235, 239, 242));
        graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        graphics.drawText(text, key, juce::Justification::centred);
    };

    const auto drawLegendRow = [&graphics, &drawKey](juce::Rectangle<float> row, const juce::String& key, const juce::String& text, juce::Colour colour) {
        drawKey(row.removeFromLeft(34.0f).reduced(0.0f, 2.0f), key, colour);
        row.removeFromLeft(8.0f);
        graphics.setFont(juce::FontOptions(11.0f));
        graphics.setColour(juce::Colour(235, 239, 242));
        graphics.drawText(text, row, juce::Justification::centredLeft);
    };

    drawLegendRow(content.removeFromTop(24.0f), "Q-P", "play Karplus ring plucks", juce::Colour(105, 198, 212));
    drawLegendRow(content.removeFromTop(24.0f), "1-0", "hold with Capture on to feed audio tails", juce::Colour(246, 217, 116));
    drawLegendRow(content.removeFromTop(24.0f), "Eu", "toggle planet-rotated Euclidean plucks", juce::Colour(123, 196, 165));
    drawLegendRow(content.removeFromTop(24.0f), "A-;", "press to mute or unmute each ring", juce::Colour(224, 111, 87));
    drawLegendRow(content.removeFromTop(24.0f), "ESC", "close this popup", juce::Colour(185, 195, 202));
    content.removeFromTop(10.0f);

    graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    graphics.setColour(juce::Colour(185, 195, 202));
    graphics.drawText("Planet rows", content.removeFromTop(16.0f), juce::Justification::centredLeft);

    constexpr std::string_view playKeys = "qwertyuiop";
    constexpr std::string_view muteKeys = "asdfghjkl;";
    const auto snapshot = audioProcessor.getCurrentSnapshotCopy();
    const float rowHeight = 17.0f;
    for (std::size_t index = 0; index < snapshot.planets.size(); ++index) {
        auto row = content.removeFromTop(rowHeight);
        const auto planet = snapshot.planets[index].planet;
        const bool muted = audioProcessor.isPlanetMuted(planet);
        graphics.setFont(juce::FontOptions(9.5f, juce::Font::bold));
        graphics.setColour(accentColour(index));
        graphics.drawText(keyLabelForPlanet(playKeys, planet), row.removeFromLeft(18.0f), juce::Justification::centredLeft);
        graphics.setColour(muted ? juce::Colour(224, 111, 87) : juce::Colour(185, 195, 202));
        graphics.drawText(keyLabelForPlanet(muteKeys, planet), row.removeFromLeft(18.0f), juce::Justification::centredLeft);
        row.removeFromLeft(6.0f);
        graphics.setFont(juce::FontOptions(9.5f));
        graphics.setColour(muted ? juce::Colour(125, 132, 140) : juce::Colour(235, 239, 242));
        graphics.drawText(juce::String(astro::toString(planet).data()), row, juce::Justification::centredLeft);
    }
}

} // namespace astral::plugin
