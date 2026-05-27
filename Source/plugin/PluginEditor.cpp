#include "plugin/PluginEditor.h"

#include <cmath>

namespace astral::plugin {
namespace {

constexpr int kPadding = 16;
constexpr int kHeaderHeight = 48;
constexpr int kInspectorWidth = 250;
constexpr int kBottomHeight = 132;
constexpr float kPi = 3.14159265358979323846f;

juce::Colour panelColour()
{
    return juce::Colour(27, 29, 34);
}

juce::Colour lineColour()
{
    return juce::Colour(73, 82, 92);
}

juce::Colour accentColour(std::size_t index)
{
    constexpr std::array<std::uint32_t, 10> colours{{
        0xfff3d58a, 0xffb9c7d8, 0xff69c6d4, 0xffe5a1bd, 0xffe06f57,
        0xffd8b65a, 0xff9aa3ad, 0xff7bc4a5, 0xff8ea8ff, 0xffc68ddd
    }};
    return juce::Colour(colours[index % colours.size()]);
}

juce::String noteName(int midiNote)
{
    constexpr std::array<const char*, 12> names{{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}};
    const int clamped = juce::jlimit(0, 127, midiNote);
    return juce::String(names[static_cast<std::size_t>(clamped % 12)]) + juce::String(clamped / 12 - 1);
}

float longitudeFromPoint(juce::Point<float> point, juce::Point<float> centre)
{
    const auto radians = std::atan2(point.y - centre.y, point.x - centre.x);
    return astral::astro::AstrologyEngine::normalizeDegrees(radians * 180.0f / kPi + 90.0f);
}

} // namespace

class OrbitMapComponent final : public juce::Component, private juce::Timer {
public:
    explicit OrbitMapComponent(AstralReverberationsAudioProcessor& owner)
        : processor(owner)
    {
        startTimerHz(24);
    }

    void paint(juce::Graphics& graphics) override
    {
        auto bounds = getLocalBounds().toFloat();
        graphics.setColour(panelColour());
        graphics.fillRoundedRectangle(bounds, 8.0f);

        auto orbitArea = bounds.reduced(18.0f);
        orbitArea.removeFromRight(92.0f);
        const auto centre = orbitArea.getCentre();
        const float radius = std::min(orbitArea.getWidth(), orbitArea.getHeight()) * 0.43f;
        const auto& snapshot = processor.getCurrentSnapshot();
        const auto& droneFrame = processor.getCurrentDroneFrame();

        graphics.setColour(juce::Colour(234, 226, 202));
        graphics.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        graphics.drawText("Orbit Harmony", bounds.withHeight(24.0f).reduced(16.0f, 0.0f), juce::Justification::centredLeft);

        drawZodiac(graphics, centre, radius);
        drawAspects(graphics, snapshot, centre, radius);

        graphics.setColour(juce::Colour(246, 217, 116));
        graphics.fillEllipse(centre.x - 9.0f, centre.y - 9.0f, 18.0f, 18.0f);

        for (std::size_t index = 0; index < snapshot.planets.size(); ++index) {
            const auto& planet = snapshot.planets[index];
            const float orbitRadius = radius * (0.26f + static_cast<float>(index) * 0.065f);
            const auto position = pointForLongitude(planet.longitudeDegrees, centre, orbitRadius);
            graphics.setColour(accentColour(index).withAlpha(0.18f));
            graphics.drawEllipse(centre.x - orbitRadius, centre.y - orbitRadius, orbitRadius * 2.0f, orbitRadius * 2.0f, 1.0f);
            graphics.setColour(accentColour(index));
            graphics.fillEllipse(position.x - 6.0f, position.y - 6.0f, 12.0f, 12.0f);
            graphics.setColour(juce::Colour(235, 239, 242));
            graphics.setFont(juce::FontOptions(10.0f));
            graphics.drawText(juce::String(astro::toString(planet.planet).data()),
                juce::Rectangle<float>(position.x + 8.0f, position.y - 8.0f, 58.0f, 16.0f),
                juce::Justification::centredLeft);
        }

        drawSpectrum(graphics, bounds.removeFromRight(96.0f).reduced(10.0f), droneFrame);
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        draggedPlanet = planetAt(event.position);
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (!draggedPlanet.has_value()) {
            return;
        }
        const auto mode = processor.getAstroMode();
        if (mode == AstralReverberationsAudioProcessor::AstroMode::Now) {
            return;
        }

        auto orbitArea = getLocalBounds().toFloat().reduced(18.0f);
        orbitArea.removeFromRight(92.0f);
        processor.setManualPlanetLongitude(*draggedPlanet, longitudeFromPoint(event.position, orbitArea.getCentre()));
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        draggedPlanet.reset();
    }

private:
    AstralReverberationsAudioProcessor& processor;
    std::optional<astro::PlanetId> draggedPlanet;

    void timerCallback() override
    {
        repaint();
    }

    static juce::Point<float> pointForLongitude(float longitude, juce::Point<float> centre, float radius)
    {
        const float radians = (longitude - 90.0f) * kPi / 180.0f;
        return {centre.x + std::cos(radians) * radius, centre.y + std::sin(radians) * radius};
    }

    std::optional<astro::PlanetId> planetAt(juce::Point<float> point) const
    {
        auto orbitArea = getLocalBounds().toFloat().reduced(18.0f);
        orbitArea.removeFromRight(92.0f);
        const auto centre = orbitArea.getCentre();
        const float radius = std::min(orbitArea.getWidth(), orbitArea.getHeight()) * 0.43f;
        const auto& snapshot = processor.getCurrentSnapshot();

        for (std::size_t index = 0; index < snapshot.planets.size(); ++index) {
            const auto position = pointForLongitude(snapshot.planets[index].longitudeDegrees, centre, radius * (0.26f + static_cast<float>(index) * 0.065f));
            if (position.getDistanceFrom(point) <= 16.0f) {
                return snapshot.planets[index].planet;
            }
        }
        return std::nullopt;
    }

    void drawZodiac(juce::Graphics& graphics, juce::Point<float> centre, float radius)
    {
        graphics.setColour(lineColour().withAlpha(0.45f));
        for (int tick = 0; tick < 36; ++tick) {
            const float longitude = static_cast<float>(tick) * 10.0f;
            const auto outer = pointForLongitude(longitude, centre, radius);
            const auto inner = pointForLongitude(longitude, centre, radius - (tick % 3 == 0 ? 12.0f : 6.0f));
            graphics.drawLine(inner.x, inner.y, outer.x, outer.y, tick % 3 == 0 ? 1.2f : 0.7f);
        }
    }

    void drawAspects(juce::Graphics& graphics, const astro::AstroSnapshot& snapshot, juce::Point<float> centre, float radius)
    {
        for (const auto& aspect : snapshot.aspects) {
            const auto aIndex = static_cast<std::size_t>(aspect.a);
            const auto bIndex = static_cast<std::size_t>(aspect.b);
            const auto a = pointForLongitude(snapshot.planets[aIndex].longitudeDegrees, centre, radius * (0.26f + static_cast<float>(aIndex) * 0.065f));
            const auto b = pointForLongitude(snapshot.planets[bIndex].longitudeDegrees, centre, radius * (0.26f + static_cast<float>(bIndex) * 0.065f));
            const auto colour = aspect.type == astro::AspectType::Square || aspect.type == astro::AspectType::Opposition
                ? juce::Colour(224, 111, 87)
                : juce::Colour(105, 198, 212);
            graphics.setColour(colour.withAlpha(0.25f + aspect.strength01 * 0.45f));
            graphics.drawLine(a.x, a.y, b.x, b.y, 1.0f + aspect.strength01 * 1.6f);
        }
    }

    void drawSpectrum(juce::Graphics& graphics, juce::Rectangle<float> area, const astro::AstroDroneFrame& frame)
    {
        graphics.setColour(juce::Colour(43, 47, 54));
        graphics.fillRoundedRectangle(area, 7.0f);
        graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        graphics.setColour(juce::Colour(235, 239, 242));
        graphics.drawText("Harmonics", area.removeFromTop(18.0f), juce::Justification::centred);

        const float bottom = area.getBottom() - 8.0f;
        const float maxHeight = area.getHeight() - 12.0f;
        const float width = area.getWidth() / static_cast<float>(frame.layers.size());
        for (std::size_t index = 0; index < frame.layers.size(); ++index) {
            const float barHeight = frame.layers[index].amplitude * maxHeight;
            const auto bar = juce::Rectangle<float>(area.getX() + static_cast<float>(index) * width + 2.0f, bottom - barHeight, width - 4.0f, barHeight);
            graphics.setColour(accentColour(index).withAlpha(0.82f));
            graphics.fillRoundedRectangle(bar, 2.0f);
        }
    }
};

AstralReverberationsEditor::AstralReverberationsEditor(AstralReverberationsAudioProcessor& owner)
    : AudioProcessorEditor(&owner),
      audioProcessor(owner),
      orbitMap(std::make_unique<OrbitMapComponent>(owner))
{
    titleLabel.setText("Astral Reverberations", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(234, 226, 202));
    titleLabel.setFont(juce::FontOptions(24.0f, juce::Font::bold));
    addAndMakeVisible(titleLabel);

    statusLabel.setJustificationType(juce::Justification::centredRight);
    statusLabel.setColour(juce::Label::textColourId, juce::Colour(185, 195, 202));
    statusLabel.setFont(juce::FontOptions(13.0f));
    addAndMakeVisible(statusLabel);

    addAndMakeVisible(*orbitMap);

    addSlider("drone_level", "Drone");
    addSlider("capture_level", "Capture");
    addSlider("harmonic_spread", "Spread");
    addSlider("aspect_depth", "Aspects");
    addSlider("mix", "Mix");
    addSlider("delay_level", "Delay");
    addSlider("reverb_level", "Reverb");
    addSlider("delay_time", "Time");
    addSlider("feedback", "Feedback");
    addSlider("space", "Space");
    addSlider("tone", "Tone");
    addSlider("output_gain", "Output");

    droneHoldButton.setButtonText("Drone On");
    droneHoldButton.setColour(juce::ToggleButton::textColourId, juce::Colour(235, 239, 242));
    addAndMakeVisible(droneHoldButton);
    droneHoldAttachment = std::make_unique<ButtonAttachment>(audioProcessor.parameters(), "drone_hold", droneHoldButton);

    freezeButton.setButtonText("Freeze");
    freezeButton.setColour(juce::ToggleButton::textColourId, juce::Colour(235, 239, 242));
    addAndMakeVisible(freezeButton);
    freezeAttachment = std::make_unique<ButtonAttachment>(audioProcessor.parameters(), "freeze", freezeButton);

    captureButton.setButtonText("Capture");
    captureButton.setColour(juce::ToggleButton::textColourId, juce::Colour(235, 239, 242));
    addAndMakeVisible(captureButton);
    captureAttachment = std::make_unique<ButtonAttachment>(audioProcessor.parameters(), "capture_enable", captureButton);

    astroModeBox.addItem("Now", 1);
    astroModeBox.addItem("Manual Date", 2);
    astroModeBox.addItem("Simulated Orbit", 3);
    astroModeBox.setSelectedId(static_cast<int>(audioProcessor.getAstroMode()) + 1, juce::dontSendNotification);
    astroModeBox.onChange = [this] {
        audioProcessor.setAstroMode(static_cast<AstralReverberationsAudioProcessor::AstroMode>(
            astroModeBox.getSelectedId() - 1));
    };
    addAndMakeVisible(astroModeBox);

    setSize(980, 620);
    startTimerHz(12);
}

void AstralReverberationsEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(16, 17, 20));

    auto bounds = getLocalBounds().toFloat().reduced(8.0f);
    graphics.setColour(juce::Colour(52, 58, 66));
    graphics.drawRoundedRectangle(bounds, 8.0f, 1.5f);
}

void AstralReverberationsEditor::resized()
{
    auto bounds = getLocalBounds().reduced(kPadding);
    auto header = bounds.removeFromTop(kHeaderHeight);
    titleLabel.setBounds(header.removeFromLeft(360));
    astroModeBox.setBounds(header.removeFromRight(184).reduced(0, 10));
    statusLabel.setBounds(header);

    bounds.removeFromTop(10);
    auto bottom = bounds.removeFromBottom(kBottomHeight);
    bounds.removeFromBottom(10);
    auto inspector = bounds.removeFromRight(kInspectorWidth);
    bounds.removeFromRight(10);

    orbitMap->setBounds(bounds);

    for (int index = 0; index < 4; ++index) {
        auto cell = inspector.removeFromTop(76).reduced(8);
        labels[index]->setBounds(cell.removeFromTop(18));
        sliders[index]->setBounds(cell);
    }
    droneHoldButton.setBounds(inspector.removeFromTop(30).reduced(8, 2));
    captureButton.setBounds(inspector.removeFromTop(30).reduced(8, 2));
    freezeButton.setBounds(inspector.removeFromTop(30).reduced(8, 2));

    const int bottomSliderStart = 4;
    const int bottomSliderCount = sliders.size() - bottomSliderStart;
    const int cellWidth = bottom.getWidth() / std::max(1, bottomSliderCount);
    for (int index = bottomSliderStart; index < sliders.size(); ++index) {
        const int column = index - bottomSliderStart;
        auto cell = juce::Rectangle<int>(bottom.getX() + column * cellWidth, bottom.getY(), cellWidth, bottom.getHeight()).reduced(5);
        labels[index]->setBounds(cell.removeFromTop(18));
        sliders[index]->setBounds(cell);
    }
}

void AstralReverberationsEditor::addSlider(const char* parameterId, const juce::String& labelText)
{
    auto* label = labels.add(new juce::Label());
    label->setText(labelText, juce::dontSendNotification);
    label->setJustificationType(juce::Justification::centred);
    label->setColour(juce::Label::textColourId, juce::Colour(235, 239, 242));
    label->setFont(juce::FontOptions(11.0f, juce::Font::bold));
    addAndMakeVisible(label);

    auto* slider = sliders.add(new juce::Slider());
    slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 17);
    slider->setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(105, 198, 212));
    slider->setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(52, 58, 66));
    slider->setColour(juce::Slider::thumbColourId, juce::Colour(243, 213, 138));
    slider->setColour(juce::Slider::textBoxTextColourId, juce::Colour(235, 239, 242));
    slider->setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(slider);

    sliderAttachments.push_back(std::make_unique<SliderAttachment>(
        audioProcessor.parameters(),
        parameterId,
        *slider));
}

void AstralReverberationsEditor::timerCallback()
{
    updateStatus();
}

void AstralReverberationsEditor::updateStatus()
{
    const auto rootText = audioProcessor.isDroneHeld()
        ? "Gate Hold  Root " + noteName(audioProcessor.getDroneRootNote())
        : audioProcessor.isDroneGateOpen()
            ? "Gate Live  Root " + noteName(audioProcessor.getDroneRootNote())
        : "Gate Idle  Root " + noteName(audioProcessor.getDroneRootNote());
    statusLabel.setText(rootText, juce::dontSendNotification);
}

} // namespace astral::plugin
