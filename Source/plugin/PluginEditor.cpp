#include "plugin/PluginEditor.h"

#include "astro/HistoricalPresets.h"
#include "BinaryData.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string_view>

namespace astral::plugin {
namespace {

constexpr int kPadding = 12;
constexpr int kTopBarHeight = 48;
constexpr int kAstroBarHeight = 42;
constexpr int kLeftPanelWidth = 220;
constexpr int kInspectorWidth = 244;
constexpr int kMeterWidth = 72;
constexpr int kSummaryHeight = 78;
constexpr int kBottomHeight = 116;
constexpr int kPanelGap = 8;
constexpr int kInspectorRowHeight = 26;
constexpr int kInspectorSliderRowHeight = 20;
constexpr int kDefaultEditorWidth = 1220;
constexpr int kDefaultEditorHeight = 690;

struct EditorLayout {
    juce::Rectangle<int> topBar;
    juce::Rectangle<int> astroBar;
    juce::Rectangle<int> leftPanel;
    juce::Rectangle<int> orbitPanel;
    juce::Rectangle<int> summaryStrip;
    juce::Rectangle<int> inspector;
    juce::Rectangle<int> meterColumn;
    juce::Rectangle<int> macroStrip;
};

EditorLayout editorLayoutFor(juce::Rectangle<int> bounds)
{
    auto content = bounds.reduced(kPadding);
    EditorLayout layout;
    layout.topBar = content.removeFromTop(kTopBarHeight);
    content.removeFromTop(6);
    layout.astroBar = content.removeFromTop(kAstroBarHeight);
    content.removeFromTop(kPanelGap);
    layout.macroStrip = content.removeFromBottom(kBottomHeight);
    content.removeFromBottom(kPanelGap);

    layout.leftPanel = content.removeFromLeft(kLeftPanelWidth);
    content.removeFromLeft(kPanelGap);
    layout.meterColumn = content.removeFromRight(kMeterWidth);
    content.removeFromRight(kPanelGap);
    layout.inspector = content.removeFromRight(kInspectorWidth);
    content.removeFromRight(kPanelGap);

    layout.summaryStrip = content.removeFromBottom(kSummaryHeight);
    content.removeFromBottom(kPanelGap);
    layout.orbitPanel = content;
    return layout;
}
constexpr float kOrbitRadiusFactor = 0.46f;
constexpr float kSignMarkerOffset = 22.0f;
constexpr float kOrbitTitleBand = 44.0f;
constexpr float kHarmonicsStripWidth = 0.0f;
constexpr float kPi = 3.14159265358979323846f;
// Background atmosphere: texture strength and how much the dark veil dims it.
constexpr float kEditorBackgroundTextureOpacity = 0.58f;
constexpr float kEditorBackgroundVeilAlpha = 0.34f;
constexpr float kOrbitMapBackgroundTextureOpacity = 0.48f;
constexpr float kOrbitMapPanelFillAlpha = 0.78f;
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
    return juce::Colour(20, 24, 29);
}

// Shared line colour for orbit rings, ticks, and outlines.
juce::Colour lineColour()
{
    return juce::Colour(62, 72, 82);
}

juce::Colour goldColour()
{
    return juce::Colour(142, 168, 255);
}

juce::Colour cyanColour()
{
    return juce::Colour(105, 198, 212);
}

juce::Colour mutedTextColour()
{
    return juce::Colour(145, 154, 164);
}

juce::Font controlFont(float height, int style = juce::Font::plain)
{
#if JUCE_MAC
    return juce::Font(juce::FontOptions("Avenir Next", height, style));
#elif JUCE_WINDOWS
    return juce::Font(juce::FontOptions("Segoe UI", height, style));
#else
    return juce::Font(juce::FontOptions("Inter", height, style));
#endif
}

juce::Font displayFont(float height, int style = juce::Font::plain)
{
#if JUCE_MAC
    return juce::Font(juce::FontOptions("Baskerville", height, style));
#else
    return controlFont(height, style);
#endif
}

void drawPanel(juce::Graphics& graphics, juce::Rectangle<float> area, float alpha = 0.88f)
{
    juce::ColourGradient panelGradient(
        juce::Colour(24, 29, 34).withAlpha(alpha),
        area.getCentreX(),
        area.getY(),
        juce::Colour(13, 17, 21).withAlpha(alpha),
        area.getCentreX(),
        area.getBottom(),
        false);
    panelGradient.addColour(0.42, panelColour().withAlpha(alpha));
    graphics.setGradientFill(panelGradient);
    graphics.fillRoundedRectangle(area, 8.0f);

    graphics.saveState();
    graphics.reduceClipRegion(area.toNearestInt());
    graphics.setColour(juce::Colour(255, 255, 255).withAlpha(0.018f));
    for (int y = static_cast<int>(area.getY()) + 7; y < static_cast<int>(area.getBottom()); y += 9) {
        graphics.drawHorizontalLine(y, area.getX() + 8.0f, area.getRight() - 8.0f);
    }
    graphics.setColour(cyanColour().withAlpha(0.018f));
    for (int x = static_cast<int>(area.getX()) + 11; x < static_cast<int>(area.getRight()); x += 23) {
        graphics.drawVerticalLine(x, area.getY() + 8.0f, area.getBottom() - 8.0f);
    }
    graphics.restoreState();

    graphics.setColour(juce::Colour(255, 255, 255).withAlpha(0.035f));
    graphics.drawRoundedRectangle(area.reduced(1.0f), 7.0f, 1.0f);
    graphics.setColour(lineColour().withAlpha(0.42f));
    graphics.drawRoundedRectangle(area, 8.0f, 1.0f);
}

void drawSectionTitle(juce::Graphics& graphics, juce::Rectangle<float> area, const juce::String& text)
{
    graphics.setFont(controlFont(11.0f, juce::Font::bold));
    graphics.setColour(goldColour().withAlpha(0.92f));
    graphics.drawText(text.toUpperCase(), area, juce::Justification::centredLeft);
}

void drawSmallBadge(juce::Graphics& graphics, juce::Rectangle<float> area, const juce::String& text, juce::Colour colour, bool filled)
{
    graphics.setColour(filled ? colour.withAlpha(0.26f) : juce::Colour(18, 20, 24).withAlpha(0.82f));
    graphics.fillRoundedRectangle(area, 5.0f);
    graphics.setColour(colour.withAlpha(filled ? 0.92f : 0.72f));
    graphics.drawRoundedRectangle(area, 5.0f, 1.0f);
    graphics.setFont(controlFont(10.0f, juce::Font::bold));
    graphics.drawText(text, area.reduced(5.0f, 0.0f), juce::Justification::centred);
}

void drawProcessorHeader(juce::Graphics& graphics, juce::Rectangle<float> area, const juce::String& text, juce::Colour colour)
{
    auto header = area.reduced(0.0f, 1.0f);
    graphics.setColour(colour.withAlpha(0.07f));
    graphics.fillRoundedRectangle(header, 3.0f);
    graphics.setColour(colour.withAlpha(0.82f));
    graphics.fillRoundedRectangle(header.removeFromLeft(3.0f), 1.5f);
    graphics.setFont(controlFont(9.0f, juce::Font::bold));
    graphics.setColour(colour.withAlpha(0.94f));
    graphics.drawText(text, area.reduced(8.0f, 0.0f), juce::Justification::centredLeft);
}

class AstralLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    AstralLookAndFeel()
    {
        setColour(juce::ComboBox::backgroundColourId, juce::Colour(24, 31, 36));
        setColour(juce::ComboBox::outlineColourId, lineColour().withAlpha(0.86f));
        setColour(juce::ComboBox::textColourId, juce::Colour(234, 226, 202));
        setColour(juce::ComboBox::arrowColourId, goldColour());
        setColour(juce::PopupMenu::backgroundColourId, juce::Colour(18, 22, 27));
        setColour(juce::PopupMenu::textColourId, juce::Colour(234, 226, 202));
        setColour(juce::PopupMenu::highlightedBackgroundColourId, goldColour().withAlpha(0.22f));
        setColour(juce::PopupMenu::highlightedTextColourId, juce::Colour(250, 242, 218));
    }

    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override
    {
        return controlFont(static_cast<float>(std::min(13, std::max(10, buttonHeight / 3))), juce::Font::bold);
    }

    juce::Font getComboBoxFont(juce::ComboBox&) override
    {
        return controlFont(13.0f, juce::Font::bold);
    }

    juce::Font getPopupMenuFont() override
    {
        return controlFont(13.0f);
    }

    juce::Rectangle<int> getTooltipBounds(const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override
    {
        const int width = juce::jlimit(150, 300, 44 + tipText.length() * 5);
        const int height = tipText.length() > 42 ? 44 : 30;
        return juce::Rectangle<int>(
            screenPos.x > parentArea.getCentreX() ? screenPos.x - width - 14 : screenPos.x + 18,
            screenPos.y > parentArea.getCentreY() ? screenPos.y - height - 12 : screenPos.y + 12,
            width,
            height)
            .constrainedWithin(parentArea.reduced(8));
    }

    void drawTooltip(juce::Graphics& graphics, const juce::String& text, int width, int height) override
    {
        const auto bounds = juce::Rectangle<float>(0.5f, 0.5f, static_cast<float>(width) - 1.0f, static_cast<float>(height) - 1.0f);
        juce::ColourGradient fill(
            juce::Colour(18, 24, 31).withAlpha(0.96f),
            bounds.getX(),
            bounds.getY(),
            juce::Colour(8, 12, 17).withAlpha(0.98f),
            bounds.getRight(),
            bounds.getBottom(),
            false);
        graphics.setGradientFill(fill);
        graphics.fillRoundedRectangle(bounds, 6.0f);
        graphics.setColour(cyanColour().withAlpha(0.62f));
        graphics.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);
        graphics.setColour(juce::Colour(232, 236, 239));
        graphics.setFont(controlFont(10.0f, juce::Font::bold));
        graphics.drawFittedText(text, juce::Rectangle<int>(8, 4, width - 16, height - 8), juce::Justification::centredLeft, 2);
    }

    void drawButtonBackground(
        juce::Graphics& graphics,
        juce::Button& button,
        const juce::Colour&,
        bool isMouseOverButton,
        bool isButtonDown) override
    {
        const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        const bool on = button.getToggleState();
        const bool focusSwitch = static_cast<bool>(button.getProperties().getWithDefault("focusSwitch", false));
        if (focusSwitch) {
            const auto fill = on ? juce::Colour(23, 34, 47).withAlpha(0.96f)
                                 : juce::Colour(16, 22, 28).withAlpha(isMouseOverButton ? 0.98f : 0.86f);
            const auto stroke = on ? goldColour().withAlpha(0.84f)
                                   : lineColour().withAlpha(isMouseOverButton ? 0.88f : 0.66f);
            graphics.setColour(isButtonDown ? fill.brighter(0.08f) : fill);
            graphics.fillRoundedRectangle(bounds, 7.0f);
            graphics.setColour(stroke);
            graphics.drawRoundedRectangle(bounds, 7.0f, on ? 1.35f : 1.0f);
            return;
        }

        const auto fill = on ? goldColour().withAlpha(0.18f)
                             : juce::Colour(24, 31, 36).withAlpha(isMouseOverButton ? 0.96f : 0.82f);
        const auto stroke = on ? goldColour().withAlpha(0.86f)
                               : lineColour().withAlpha(isMouseOverButton ? 0.88f : 0.62f);

        graphics.setColour(isButtonDown ? fill.brighter(0.08f) : fill);
        graphics.fillRoundedRectangle(bounds, 6.0f);
        graphics.setColour(stroke);
        graphics.drawRoundedRectangle(bounds, 6.0f, on ? 1.2f : 1.0f);
    }

    void drawButtonText(juce::Graphics& graphics, juce::TextButton& button, bool, bool) override
    {
        const bool focusSwitch = static_cast<bool>(button.getProperties().getWithDefault("focusSwitch", false));
        if (focusSwitch) {
            const auto bounds = button.getLocalBounds().reduced(9, 5);
            const auto label = button.getProperties().getWithDefault("focusLabel", button.getButtonText()).toString();
            const auto onText = button.getProperties().getWithDefault("focusOnText", "ON").toString();
            const auto offText = button.getProperties().getWithDefault("focusOffText", "OFF").toString();
            const bool on = button.getToggleState();
            const auto stateText = on ? onText : offText;

            auto textArea = bounds;
            textArea.removeFromRight(34);
            graphics.setFont(controlFont(8.5f, juce::Font::bold));
            graphics.setColour(mutedTextColour().withAlpha(0.96f));
            graphics.drawText(label, textArea.removeFromTop(13), juce::Justification::centredLeft);
            graphics.setFont(controlFont(11.0f, juce::Font::bold));
            graphics.setColour(on ? goldColour() : juce::Colour(232, 236, 239));
            graphics.drawText(stateText, textArea, juce::Justification::centredLeft);

            auto rail = juce::Rectangle<float>(
                static_cast<float>(bounds.getRight() - 30),
                static_cast<float>(bounds.getCentreY() - 7),
                28.0f,
                14.0f);
            graphics.setColour(on ? cyanColour().withAlpha(0.28f) : lineColour().withAlpha(0.42f));
            graphics.fillRoundedRectangle(rail, 7.0f);
            graphics.setColour(on ? cyanColour().withAlpha(0.85f) : mutedTextColour().withAlpha(0.52f));
            const float thumbX = on ? rail.getRight() - 12.0f : rail.getX() + 2.0f;
            graphics.fillEllipse(thumbX, rail.getY() + 2.0f, 10.0f, 10.0f);
            return;
        }

        graphics.setFont(getTextButtonFont(button, button.getHeight()));
        graphics.setColour(button.getToggleState() ? goldColour() : juce::Colour(232, 236, 239));
        graphics.drawText(button.getButtonText(), button.getLocalBounds().reduced(8, 0), juce::Justification::centred);
    }

    void drawComboBox(
        juce::Graphics& graphics,
        int width,
        int height,
        bool isButtonDown,
        int,
        int,
        int,
        int,
        juce::ComboBox& box) override
    {
        const auto bounds = juce::Rectangle<float>(0.5f, 0.5f, static_cast<float>(width) - 1.0f, static_cast<float>(height) - 1.0f);
        graphics.setColour(juce::Colour(24, 31, 36).withAlpha(isButtonDown ? 0.98f : 0.88f));
        graphics.fillRoundedRectangle(bounds, 6.0f);
        graphics.setColour((box.hasKeyboardFocus(false) ? goldColour() : lineColour()).withAlpha(0.82f));
        graphics.drawRoundedRectangle(bounds, 6.0f, 1.1f);

        juce::Path arrow;
        const float cx = static_cast<float>(width) - 18.0f;
        const float cy = static_cast<float>(height) * 0.52f;
        arrow.startNewSubPath(cx - 5.0f, cy - 3.0f);
        arrow.lineTo(cx, cy + 3.0f);
        arrow.lineTo(cx + 5.0f, cy - 3.0f);
        graphics.setColour(goldColour().withAlpha(0.86f));
        graphics.strokePath(arrow, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void positionComboBoxText(juce::ComboBox&, juce::Label& label) override
    {
        label.setBounds(10, 1, label.getParentWidth() - 34, label.getParentHeight() - 2);
        label.setFont(controlFont(13.0f, juce::Font::bold));
        label.setJustificationType(juce::Justification::centredLeft);
    }

    void drawLinearSlider(
        juce::Graphics& graphics,
        int x,
        int y,
        int width,
        int height,
        float sliderPos,
        float,
        float,
        const juce::Slider::SliderStyle,
        juce::Slider& slider) override
    {
        const auto track = juce::Rectangle<float>(
            static_cast<float>(x) + 2.0f,
            static_cast<float>(y) + static_cast<float>(height) * 0.5f - 2.0f,
            static_cast<float>(width) - 8.0f,
            4.0f);
        graphics.setColour(juce::Colour(45, 53, 62).withAlpha(0.72f));
        graphics.fillRoundedRectangle(track, 2.0f);
        auto fill = track;
        fill.setRight(juce::jlimit(track.getX(), track.getRight(), sliderPos));
        graphics.setColour(goldColour().withAlpha(0.86f));
        graphics.fillRoundedRectangle(fill, 2.0f);
        const auto thumb = juce::Rectangle<float>(sliderPos - 5.0f, track.getCentreY() - 5.0f, 10.0f, 10.0f);
        graphics.setColour(goldColour());
        graphics.fillEllipse(thumb);
        graphics.setColour(juce::Colour(18, 22, 27).withAlpha(0.55f));
        graphics.drawEllipse(thumb, 1.0f);
        slider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(232, 236, 239));
    }

    void drawRotarySlider(
        juce::Graphics& graphics,
        int x,
        int y,
        int width,
        int height,
        float sliderPos,
        float rotaryStartAngle,
        float rotaryEndAngle,
        juce::Slider&) override
    {
        const auto size = static_cast<float>(std::min(width, height)) - 12.0f;
        const auto bounds = juce::Rectangle<float>(
            static_cast<float>(x) + (static_cast<float>(width) - size) * 0.5f,
            static_cast<float>(y) + 4.0f,
            size,
            size);
        const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        const auto centre = bounds.getCentre();
        const float radius = bounds.getWidth() * 0.5f - 4.0f;

        for (int tick = 0; tick <= 12; ++tick) {
            const float proportion = static_cast<float>(tick) / 12.0f;
            const float tickAngle = rotaryStartAngle + proportion * (rotaryEndAngle - rotaryStartAngle) - juce::MathConstants<float>::halfPi;
            const float inner = radius + 3.0f;
            const float outer = radius + (tick % 3 == 0 ? 8.0f : 6.0f);
            const auto a = juce::Point<float>(centre.x + std::cos(tickAngle) * inner, centre.y + std::sin(tickAngle) * inner);
            const auto b = juce::Point<float>(centre.x + std::cos(tickAngle) * outer, centre.y + std::sin(tickAngle) * outer);
            graphics.setColour((tick % 3 == 0 ? cyanColour() : lineColour()).withAlpha(tick % 3 == 0 ? 0.38f : 0.24f));
            graphics.drawLine(a.x, a.y, b.x, b.y, tick % 3 == 0 ? 1.2f : 0.8f);
        }

        graphics.setColour(juce::Colour(7, 9, 12).withAlpha(0.68f));
        graphics.fillEllipse(bounds.reduced(1.0f).translated(0.0f, 2.0f));
        graphics.setColour(lineColour().withAlpha(0.45f));
        graphics.drawEllipse(bounds.reduced(2.0f), 1.1f);

        juce::ColourGradient capGradient(
            juce::Colour(36, 43, 50),
            centre.x - radius * 0.45f,
            centre.y - radius * 0.55f,
            juce::Colour(8, 11, 15),
            centre.x + radius * 0.42f,
            centre.y + radius * 0.56f,
            true);
        capGradient.addColour(0.58, juce::Colour(17, 22, 27));
        graphics.setGradientFill(capGradient);
        graphics.fillEllipse(bounds.reduced(8.0f));
        graphics.setColour(juce::Colour(255, 255, 255).withAlpha(0.045f));
        graphics.drawEllipse(bounds.reduced(9.0f), 1.0f);

        juce::Path arc;
        arc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, rotaryStartAngle, angle, true);
        graphics.setColour(cyanColour().withAlpha(0.92f));
        graphics.strokePath(arc, juce::PathStrokeType(4.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const auto pointerEnd = juce::Point<float>(
            centre.x + std::cos(angle - juce::MathConstants<float>::halfPi) * (radius - 12.0f),
            centre.y + std::sin(angle - juce::MathConstants<float>::halfPi) * (radius - 12.0f));
        graphics.setColour(goldColour().withAlpha(0.78f));
        graphics.drawLine(centre.x, centre.y, pointerEnd.x, pointerEnd.y, 2.0f);

        const auto thumb = juce::Point<float>(
            centre.x + std::cos(angle - juce::MathConstants<float>::halfPi) * radius,
            centre.y + std::sin(angle - juce::MathConstants<float>::halfPi) * radius);
        graphics.setColour(goldColour());
        graphics.fillEllipse(thumb.x - 4.0f, thumb.y - 4.0f, 8.0f, 8.0f);
    }
};

// Returns a stable per-planet accent colour by planet index.
juce::Colour accentColour(std::size_t index)
{
    constexpr std::array<std::uint32_t, 10> colours{{
        0xffffd36f, 0xffbcc8d2, 0xff70d5e4, 0xffee9fc7, 0xffff7668,
        0xffd6b780, 0xffb8a4ff, 0xff72d4c8, 0xff8ea8ff, 0xffc68ddd
    }};
    return juce::Colour(colours[index % colours.size()]);
}

float planetVisualRadius(astro::PlanetId planet)
{
    switch (planet) {
        case astro::PlanetId::Sun:
            return 11.0f;
        case astro::PlanetId::Moon:
        case astro::PlanetId::Jupiter:
        case astro::PlanetId::Saturn:
            return 9.5f;
        case astro::PlanetId::Uranus:
        case astro::PlanetId::Neptune:
            return 8.8f;
        case astro::PlanetId::Pluto:
            return 7.4f;
        case astro::PlanetId::Mercury:
        case astro::PlanetId::Venus:
        case astro::PlanetId::Mars:
        case astro::PlanetId::Count:
            break;
    }
    return 8.3f;
}

float parameterValue(juce::AudioProcessorValueTreeState& state, const char* id, float fallback = 0.0f)
{
    if (const auto* value = state.getRawParameterValue(id)) {
        return value->load();
    }
    return fallback;
}

float tailSizeMultiplier(float tailSize)
{
    const float size = juce::jlimit(0.0f, 1.0f, tailSize);
    return 1.0f + size * size * 9.0f;
}

float visualTailSeconds(float baseSeconds, float tailSize)
{
    return juce::jlimit(0.35f, 180.0f, baseSeconds * tailSizeMultiplier(tailSize));
}

// Converts a MIDI note number into a compact note name for the root selector.
juce::String noteName(int midiNote)
{
    constexpr std::array<const char*, 12> names{{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}};
    const int clamped = juce::jlimit(0, 127, midiNote);
    return juce::String(names[static_cast<std::size_t>(clamped % 12)]) + juce::String(clamped / 12 - 1);
}

juce::String planetParameterId(const char* prefix, astro::PlanetId planet)
{
    return juce::String(prefix) + juce::String(static_cast<int>(planet));
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
        return "Mix macro: wet blend, echo/reverb send, and output level.";
    }
    if (parameterId == "macro_mneme") {
        return "Tail Memory macro: capture tails, orbit echo path, and safer feedback when tails are hot.";
    }
    if (parameterId == "capture_level") {
        return "Orbit tail capture strength when Capture is armed (scaled by Tail Memory).";
    }
    if (parameterId == "tail_size") {
        return "Scales planet capture tails from normal holds into very long memory loops.";
    }
    if (parameterId == "tail_regen") {
        return "Adds feedback lift inside planet capture tails for near-infinite regeneration.";
    }
    if (parameterId == "macro_choir") {
        return "Voices macro: layer spread and aspect bloom (use Drone Level / Pluck Level for balance).";
    }
    if (parameterId == "drone_level") {
        return "Planetary oscillator drone loudness (independent of Karplus ring plucks).";
    }
    if (parameterId == "pluck_level") {
        return "Karplus ring pluck loudness for Q-P keys, orbit clicks, and MIDI notes 60-69.";
    }
    if (parameterId == "macro_ephemeris") {
        return "Orbit Mod macro: astrological modulation depth across delay and reverb.";
    }
    if (parameterId == "macro_fate") {
        return "Echo macro: delay time, repeats, wobble, and saturation.";
    }
    if (parameterId == "macro_void") {
        return "Reverb macro: tank size, darkness, and reverb emphasis.";
    }
    if (parameterId == "macro_pulse") {
        return "Pluck macro: timbre and octave spread (Euclidean rate/wet are separate).";
    }
    if (parameterId == "delay_time") {
        return "Main echo time, offset from the Echo macro.";
    }
    if (parameterId == "delay_level") {
        return "Echo output level, offset from the Mix and Echo macros.";
    }
    if (parameterId == "feedback") {
        return "Echo feedback/regeneration, offset from the Echo macro.";
    }
    if (parameterId == "wow_flutter") {
        return "Tape-style delay wobble, offset from the Echo macro.";
    }
    if (parameterId == "drive") {
        return "Tape-style echo saturation, offset from the Echo macro.";
    }
    if (parameterId == "reverb_level") {
        return "Reverb output level, offset from the Mix and Reverb macros.";
    }
    if (parameterId == "space") {
        return "Reverb tank size, offset from the Reverb macro.";
    }
    if (parameterId == "tone") {
        return "Shared echo/tank brightness, offset from the Reverb macro.";
    }
    if (parameterId == "euclid_rate") {
        return "Sets the Euclidean pluck sequencer step rate in steps per second.";
    }
    if (parameterId == "euclid_wet") {
        return "Blends Euclidean plucks from direct dry to fully sent through tape delay and reverb.";
    }
    if (parameterId == "reverb_decay") {
        return "Reverb tank regeneration for longer, denser Desmodus-style washes.";
    }
    if (parameterId == "reverb_damping") {
        return "High-frequency absorption inside the reverb tank.";
    }
    if (parameterId == "reverb_mod") {
        return "Stereo motion depth inside the reverb tank.";
    }
    if (parameterId == "pluck_send") {
        return "Send amount from orbit taps and Euclidean plucks into the reverb tank.";
    }
    if (parameterId == "filter_cutoff") {
        return "Cutoff frequency for the routed stereo multimode filter.";
    }
    if (parameterId == "filter_resonance") {
        return "Resonance amount for the routed stereo multimode filter.";
    }
    if (parameterId == "filter_radiate") {
        return "Stereo cutoff spread for the routed multimode filter.";
    }
    if (parameterId.startsWith("master_eq_")) {
        return "Legacy preset-compatible EQ parameter; no longer shown or applied.";
    }
    if (parameterId == "macro_root") {
        return "Transposes the harmonic root in semitones.";
    }
    if (parameterId == "output_gain") {
        return "Final output trim after the Mix macro.";
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
    if (parameterId == "delay_time") {
        return juce::String(static_cast<int>(std::round(value))) + " ms";
    }
    if (parameterId == "filter_cutoff") {
        if (value >= 1000.0) {
            return juce::String(value / 1000.0, 1) + "k";
        }
        return juce::String(static_cast<int>(std::round(value))) + " Hz";
    }
    if (parameterId == "tail_size") {
        const double multiplier = 1.0 + value * value * 9.0;
        return juce::String(multiplier, 1) + "x";
    }
    if (parameterId == "tail_regen" || parameterId == "euclid_wet" || parameterId == "delay_level" || parameterId == "feedback" || parameterId == "wow_flutter" || parameterId == "drive" || parameterId == "reverb_level" || parameterId == "space" || parameterId == "tone" || parameterId == "reverb_decay" || parameterId == "reverb_damping" || parameterId == "reverb_mod" || parameterId == "filter_resonance" || parameterId == "filter_radiate") {
        return juce::String(static_cast<int>(std::round(value * 100.0))) + "%";
    }
    if (parameterId == "capture_level") {
        return juce::String(static_cast<int>(std::round(value * 100.0))) + "%";
    }
    if (parameterId == "pluck_send" || parameterId == "drone_level" || parameterId == "pluck_level") {
        return juce::String(value, 2) + "x";
    }
    if (parameterId.startsWith("master_eq_")) {
        return (value >= 0.0 ? "+" : "") + juce::String(value, 1) + " dB";
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
        selectedPlanet = astro::PlanetId::Saturn;
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
        const float tailSize = parameterValue(processor.parameters(), "tail_size");

        {
            juce::Graphics::ScopedSaveState textureScope(graphics);
            graphics.reduceClipRegion(layout.contentArea.toNearestInt());
            drawAstralTexture(graphics, layout.contentArea, kOrbitMapBackgroundTextureOpacity);
        }

        auto titleArea = bounds.withHeight(kOrbitTitleBand).reduced(16.0f, 6.0f);
        graphics.setColour(juce::Colour(234, 226, 202));
        graphics.setFont(displayFont(18.0f, juce::Font::bold));
        graphics.drawText("Orbit Harmony", titleArea.removeFromTop(21.0f), juce::Justification::centredLeft);
        graphics.setColour(mutedTextColour());
        graphics.setFont(controlFont(11.0f));
        graphics.drawText("Planetary positions shape drone, capture tails, plucks, delay, and reverb",
            titleArea,
            juce::Justification::centredLeft);

        drawCymaticField(graphics, layout.contentArea, centre, radius, snapshot, droneFrame);
        drawOutputParticles(graphics, layout.contentArea, centre, radius);
        drawZodiac(graphics, centre, radius);
        drawSignMarkers(graphics, centre, layout.markerRadius, snapshot);
        drawAspects(graphics, snapshot, centre, radius);
        drawInteractionFeedback(graphics, centre, radius);

        for (std::size_t index = 0; index < snapshot.planets.size(); ++index) {
            const auto& planet = snapshot.planets[index];
            const float orbitRadius = orbitRadiusForIndex(radius, index);
            const auto& layer = droneFrame.layers[index];
            const auto position = pointForPlanet(planet, index, centre, radius);
            const bool muted = processor.isPlanetMuted(planet.planet);
            graphics.setColour(accentColour(index).withAlpha(0.18f));
            graphics.drawEllipse(centre.x - orbitRadius, centre.y - orbitRadius, orbitRadius * 2.0f, orbitRadius * 2.0f, 1.0f);
            const float tailLevel = muted ? 0.0f : processor.getOrbitTailLevel(planet.planet);
            const float scaledTailSeconds = visualTailSeconds(layer.tailSeconds, tailSize);
            drawTailArc(graphics,
                centre,
                orbitRadius,
                planet.longitudeDegrees,
                scaledTailSeconds,
                tailLevel,
                processor.isManualOrbitTailActive(planet.planet),
                accentColour(index));
            const bool active = processor.isManualOrbitTailActive(planet.planet) || processor.isOrganLayerActive(planet.planet);
            drawPlanetNode(graphics,
                planet.planet,
                position,
                accentColour(index),
                muted,
                selectedPlanet.has_value() && *selectedPlanet == planet.planet,
                active,
                processor.hasPlanetLongitudeOffset(planet.planet),
                tailLevel);

            const bool isCentreSun = planet.planet == astro::PlanetId::Sun;
            const bool labelOnLeft = position.x < centre.x;
            const auto labelBounds = labelOnLeft
                ? juce::Rectangle<float>(position.x - 82.0f, position.y - 9.0f, 70.0f, 18.0f)
                : isCentreSun ? juce::Rectangle<float>(position.x - 35.0f, position.y + 15.0f, 70.0f, 18.0f)
                              : juce::Rectangle<float>(position.x + 11.0f, position.y - 9.0f, 70.0f, 18.0f);
            if (active || (selectedPlanet.has_value() && *selectedPlanet == planet.planet)) {
                graphics.setColour(juce::Colour(7, 11, 16).withAlpha(0.62f));
                graphics.fillRoundedRectangle(labelBounds.expanded(4.0f, 2.0f), 5.0f);
            }
            graphics.setColour(muted ? juce::Colour(152, 160, 170) : juce::Colour(235, 239, 242));
            graphics.setFont(controlFont(12.0f, juce::Font::bold));
            graphics.drawText(juce::String(astro::toString(planet.planet).data()),
                labelBounds,
                isCentreSun ? juce::Justification::centred
                            : labelOnLeft ? juce::Justification::centredRight : juce::Justification::centredLeft);
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
        const float tailSize = parameterValue(processor.parameters(), "tail_size");

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
            const auto position = pointForPlanet(planet, index, centre, radius);
            const auto planetName = juce::String(astro::toString(planet.planet).data());
            if (position.getDistanceFrom(point) <= planetVisualRadius(planet.planet) + 9.0f) {
                return planetName
                    + ": drag to offset orbit position; double-click to "
                    + (processor.isPlanetMuted(planet.planet) ? "unmute" : "mute")
                    + ".";
            }

            const auto& layer = droneFrame.layers[index];
            const float scaledTailSeconds = visualTailSeconds(layer.tailSeconds, tailSize);
            if (isPointNearTailArc(point, centre, orbitRadius, planet.longitudeDegrees, scaledTailSeconds)) {
                const int tailPercent = static_cast<int>(std::round(juce::jlimit(0.0f, 1.0f, processor.getOrbitTailLevel(planet.planet)) * 100.0f));
                return planetName
                    + " orbit tail: "
                    + juce::String(scaledTailSeconds, 1)
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
            const auto a = pointForPlanet(snapshot.planets[aIndex], aIndex, centre, radius);
            const auto b = pointForPlanet(snapshot.planets[bIndex], bIndex, centre, radius);
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

        auto mapArea = contentArea;
        mapArea.setRight(contentArea.getRight() - kHarmonicsStripWidth);
        const auto centre = mapArea.getCentre();
        const float orbitSpan = std::min(mapArea.getWidth(), mapArea.getHeight());
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

    static juce::Point<float> pointForPlanet(const astro::PlanetPosition& planet, std::size_t index, juce::Point<float> centre, float radius)
    {
        if (planet.planet == astro::PlanetId::Sun) {
            return centre;
        }
        return pointForLongitude(planet.longitudeDegrees, centre, orbitRadiusForIndex(radius, index));
    }

    /// Hit-tests the draggable planet nodes.
    std::optional<astro::PlanetId> planetAt(juce::Point<float> point) const
    {
        const auto layout = orbitLayout();
        const auto centre = layout.centre;
        const float radius = layout.radius;
        const auto snapshot = processor.getCurrentSnapshotCopy();

        for (std::size_t index = 0; index < snapshot.planets.size(); ++index) {
            const auto position = pointForPlanet(snapshot.planets[index], index, centre, radius);
            if (position.getDistanceFrom(point) <= planetVisualRadius(snapshot.planets[index].planet) + 8.0f) {
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
        constexpr float kMaximumTailSeconds = 120.0f;
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
        graphics.setColour(goldColour().withAlpha(0.08f + energy * 0.16f));
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
                graphics.setColour(goldColour().withAlpha(alpha * 0.12f));
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
            graphics.setColour(goldColour().withAlpha(alpha * 0.38f));
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
            const auto markerBounds = juce::Rectangle<float>(point.x - 14.0f, point.y - 14.0f, 28.0f, 28.0f);
            const auto sign = static_cast<astro::ZodiacSign>(index);
            const bool enabled = processor.isSignEffectEnabled(sign);
            const auto assignment = static_cast<std::size_t>(processor.getSignEffectAssignment(sign));
            graphics.setColour((enabled ? cyanColour() : juce::Colour(92, 98, 105)).withAlpha(0.12f + activity * 0.32f));
            graphics.drawEllipse(markerBounds, 0.8f + activity * 1.1f);
            graphics.setColour((enabled ? juce::Colour(235, 239, 242) : juce::Colour(105, 112, 120)).withAlpha(enabled ? 0.62f : 0.36f));
            graphics.setFont(zodiacGlyphFont(15.0f));
            graphics.drawText(signGlyph(index), markerBounds, juce::Justification::centred);
            if (activity > 0.01f) {
                const auto labelBounds = markerBounds.translated(0.0f, 17.0f).expanded(10.0f, 0.0f);
                graphics.setColour((enabled ? mutedTextColour() : juce::Colour(91, 98, 106)).withAlpha(0.42f));
                graphics.setFont(juce::FontOptions(7.5f));
                graphics.drawText(signEffectLabel(assignment), labelBounds, juce::Justification::centred);
            }
            if (!enabled) {
                graphics.drawLine(markerBounds.getX() + 8.0f, markerBounds.getBottom() - 8.0f, markerBounds.getRight() - 8.0f, markerBounds.getY() + 8.0f, 1.0f);
            }
        }
    }

    /// Draws aspect lines between planet pairs that are currently close to target angles.
    void drawAspects(juce::Graphics& graphics, const astro::AstroSnapshot& snapshot, juce::Point<float> centre, float radius)
    {
        for (const auto& aspect : snapshot.aspects) {
            const auto aIndex = static_cast<std::size_t>(aspect.a);
            const auto bIndex = static_cast<std::size_t>(aspect.b);
            const auto a = pointForPlanet(snapshot.planets[aIndex], aIndex, centre, radius);
            const auto b = pointForPlanet(snapshot.planets[bIndex], bIndex, centre, radius);
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
        constexpr float kMaximumTailSeconds = 120.0f;
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
        const float alpha = active ? 0.34f : idleAlpha;
        const float thickness = active ? 2.6f : 1.4f + normalizedTail * 1.2f;
        graphics.setColour(colour.withAlpha(alpha));
        graphics.strokePath(tailPath, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (active || level > 0.01f) {
            const float animatedArcDegrees = arcDegrees * (active ? 1.0f : juce::jlimit(0.06f, 1.0f, level));
            const int animatedSegments = std::max(2, static_cast<int>(static_cast<float>(segments) * (animatedArcDegrees / arcDegrees)));
            juce::Path animatedPath;
            for (int segment = 0; segment <= animatedSegments; ++segment) {
                const float proportion = static_cast<float>(segment) / static_cast<float>(animatedSegments);
                const float longitude = longitudeDegrees - animatedArcDegrees * proportion;
                const auto point = pointForLongitude(longitude, centre, orbitRadius);
                if (segment == 0) {
                    animatedPath.startNewSubPath(point);
                } else {
                    animatedPath.lineTo(point);
                }
            }
            graphics.setColour(colour.withAlpha(active ? 0.95f : level * 0.86f));
            graphics.strokePath(animatedPath, juce::PathStrokeType(2.8f + level * 4.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        const float markerLongitude = longitudeDegrees - arcDegrees * (active ? 1.0f : juce::jlimit(0.06f, 1.0f, level));
        const auto marker = pointForLongitude(markerLongitude, centre, orbitRadius);
        const float markerSize = 4.0f + level * 8.0f;
        graphics.setColour(colour.withAlpha(active ? 0.95f : 0.28f + level * 0.62f));
        graphics.fillEllipse(marker.x - markerSize * 0.5f, marker.y - markerSize * 0.5f, markerSize, markerSize);

        if (active || level > 0.08f) {
            const float glowSize = 11.0f + level * 13.0f;
            graphics.setColour(goldColour().withAlpha(active ? 0.9f : level * 0.55f));
            graphics.drawEllipse(marker.x - glowSize * 0.5f, marker.y - glowSize * 0.5f, glowSize, glowSize, 1.4f);
        }
    }

    void drawPlanetNode(juce::Graphics& graphics,
        astro::PlanetId planet,
        juce::Point<float> position,
        juce::Colour colour,
        bool muted,
        bool selected,
        bool active,
        bool hasOffset,
        float tailLevel) const
    {
        const float radius = planetVisualRadius(planet);
        const auto bodyBounds = juce::Rectangle<float>(position.x - radius, position.y - radius, radius * 2.0f, radius * 2.0f);
        const auto bodyColour = muted ? juce::Colour(87, 96, 106) : colour;
        const float energy = juce::jlimit(0.0f, 1.0f, tailLevel);

        if (planet == astro::PlanetId::Sun && !muted) {
            const float glowRadius = radius + 32.0f + energy * 12.0f;
            juce::ColourGradient sunGlow(
                juce::Colour(255, 208, 84).withAlpha(0.28f),
                position.x,
                position.y,
                juce::Colours::transparentBlack,
                position.x + glowRadius,
                position.y + glowRadius,
                true);
            sunGlow.addColour(0.42, juce::Colour(255, 160, 55).withAlpha(0.10f));
            graphics.setGradientFill(sunGlow);
            graphics.fillEllipse(position.x - glowRadius, position.y - glowRadius, glowRadius * 2.0f, glowRadius * 2.0f);

            graphics.setColour(juce::Colour(255, 214, 98).withAlpha(0.34f));
            for (int ray = 0; ray < 12; ++ray) {
                const float angle = static_cast<float>(ray) * kPi / 6.0f;
                const auto inner = juce::Point<float>(position.x + std::cos(angle) * (radius + 6.0f), position.y + std::sin(angle) * (radius + 6.0f));
                const auto outer = juce::Point<float>(position.x + std::cos(angle) * (radius + 16.0f), position.y + std::sin(angle) * (radius + 16.0f));
                graphics.drawLine(inner.x, inner.y, outer.x, outer.y, 0.9f);
            }
        }

        if (selected || active || energy > 0.04f) {
            const float glowRadius = radius + (selected ? 18.0f : 10.0f) + energy * 8.0f;
            juce::ColourGradient glow(
                (selected ? goldColour() : bodyColour).withAlpha(selected ? 0.26f : 0.14f + energy * 0.16f),
                position.x,
                position.y,
                juce::Colours::transparentBlack,
                position.x + glowRadius,
                position.y + glowRadius,
                true);
            graphics.setGradientFill(glow);
            graphics.fillEllipse(position.x - glowRadius, position.y - glowRadius, glowRadius * 2.0f, glowRadius * 2.0f);
        }

        if (active) {
            graphics.setColour(cyanColour().withAlpha(0.72f));
            graphics.drawEllipse(bodyBounds.expanded(6.0f), 1.8f);
        }

        if (hasOffset) {
            graphics.setColour(goldColour().withAlpha(0.72f));
            graphics.drawEllipse(bodyBounds.expanded(9.0f), 1.2f);
            graphics.drawLine(position.x - radius - 14.0f, position.y, position.x - radius - 8.0f, position.y, 1.1f);
            graphics.drawLine(position.x + radius + 8.0f, position.y, position.x + radius + 14.0f, position.y, 1.1f);
        }

        if (selected) {
            graphics.setColour(goldColour().withAlpha(0.9f));
            graphics.drawEllipse(bodyBounds.expanded(10.0f), 2.0f);
            graphics.setColour(cyanColour().withAlpha(0.48f));
            graphics.drawEllipse(bodyBounds.expanded(14.0f), 1.0f);
        }

        if (planet == astro::PlanetId::Saturn) {
            juce::Path ring;
            ring.addEllipse(position.x - radius * 1.95f, position.y - radius * 0.58f, radius * 3.9f, radius * 1.16f);
            ring.applyTransform(juce::AffineTransform::rotation(-0.34f, position.x, position.y));
            graphics.setColour(bodyColour.withAlpha(muted ? 0.24f : 0.58f));
            graphics.strokePath(ring, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        graphics.setColour(juce::Colour(0, 0, 0).withAlpha(0.32f));
        graphics.fillEllipse(bodyBounds.translated(1.6f, 2.0f).expanded(1.0f));

        juce::ColourGradient bodyGradient(
            bodyColour.brighter(muted ? 0.08f : 0.42f),
            position.x - radius * 0.55f,
            position.y - radius * 0.68f,
            bodyColour.darker(muted ? 0.60f : 0.92f),
            position.x + radius * 0.62f,
            position.y + radius * 0.72f,
            true);
        bodyGradient.addColour(0.56, bodyColour.withAlpha(0.96f));
        graphics.setGradientFill(bodyGradient);
        graphics.fillEllipse(bodyBounds);

        if (!muted) {
            if (planet == astro::PlanetId::Moon) {
                graphics.setColour(panelColour().withAlpha(0.52f));
                graphics.fillEllipse(bodyBounds.translated(radius * 0.34f, -radius * 0.05f));
                graphics.setColour(juce::Colour(235, 239, 242).withAlpha(0.28f));
                graphics.drawEllipse(bodyBounds.reduced(radius * 0.18f), 1.0f);
            } else if (planet == astro::PlanetId::Sun) {
                graphics.setColour(juce::Colour(255, 245, 190).withAlpha(0.22f));
                graphics.drawLine(position.x - radius * 0.64f, position.y - radius * 0.10f, position.x + radius * 0.54f, position.y + radius * 0.06f, 1.2f);
                graphics.drawLine(position.x - radius * 0.38f, position.y + radius * 0.34f, position.x + radius * 0.50f, position.y + radius * 0.24f, 1.0f);
            } else if (planet == astro::PlanetId::Jupiter) {
                for (float band = -0.42f; band <= 0.44f; band += 0.28f) {
                    const float y = position.y + band * radius;
                    const float halfWidth = std::sqrt(std::max(0.0f, radius * radius - (y - position.y) * (y - position.y))) * 0.82f;
                    graphics.setColour(juce::Colour(235, 239, 242).withAlpha(0.18f));
                    graphics.drawLine(position.x - halfWidth, y, position.x + halfWidth, y, 1.2f);
                }
            } else if (planet == astro::PlanetId::Mars) {
                graphics.setColour(juce::Colour(255, 220, 210).withAlpha(0.22f));
                graphics.drawLine(position.x - radius * 0.48f, position.y + radius * 0.26f, position.x + radius * 0.42f, position.y - radius * 0.34f, 1.4f);
            } else if (planet == astro::PlanetId::Uranus || planet == astro::PlanetId::Neptune) {
                graphics.setColour(cyanColour().withAlpha(0.30f));
                graphics.drawEllipse(bodyBounds.reduced(radius * 0.20f), 1.1f);
                graphics.drawLine(position.x, position.y - radius * 0.64f, position.x, position.y + radius * 0.64f, 1.0f);
            } else if (planet == astro::PlanetId::Pluto) {
                graphics.setColour(juce::Colour(235, 239, 242).withAlpha(0.36f));
                graphics.fillEllipse(position.x + radius * 0.24f, position.y - radius * 0.48f, radius * 0.30f, radius * 0.30f);
            }
        }

        if (planet == astro::PlanetId::Saturn) {
            juce::Path ring;
            ring.addEllipse(position.x - radius * 1.95f, position.y - radius * 0.58f, radius * 3.9f, radius * 1.16f);
            ring.applyTransform(juce::AffineTransform::rotation(-0.34f, position.x, position.y));
            graphics.setColour(bodyColour.brighter(0.12f).withAlpha(muted ? 0.34f : 0.82f));
            graphics.strokePath(ring, juce::PathStrokeType(1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        graphics.setColour(juce::Colour(245, 250, 255).withAlpha(muted ? 0.12f : 0.34f));
        graphics.fillEllipse(position.x - radius * 0.42f, position.y - radius * 0.54f, radius * 0.46f, radius * 0.34f);

        graphics.setColour(juce::Colour(245, 250, 255).withAlpha(muted ? 0.18f : 0.56f));
        graphics.drawEllipse(bodyBounds, 1.2f);

        if (muted) {
            graphics.setColour(juce::Colour(11, 14, 18).withAlpha(0.48f));
            graphics.fillEllipse(bodyBounds);
            graphics.setColour(juce::Colour(220, 228, 238).withAlpha(0.68f));
            graphics.drawLine(position.x - radius * 0.82f, position.y + radius * 0.82f, position.x + radius * 0.82f, position.y - radius * 0.82f, 2.0f);
        }
    }

    /// Draws the compact harmonic-layer and tail-length strip beside the orbit map.
    void drawSpectrum(juce::Graphics& graphics, juce::Rectangle<float> area, const astro::AstroDroneFrame& frame)
    {
        if (area.getWidth() < 20.0f || area.getHeight() < 40.0f || frame.layers.empty()) {
            return;
        }

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
                graphics.setColour(goldColour());
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
      tooltipWindow(this, 1800)
{
    lookAndFeel = std::make_unique<AstralLookAndFeel>();
    setLookAndFeel(lookAndFeel.get());
    setWantsKeyboardFocus(true);

    titleLabel.setText("Astral Reverberations", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(234, 226, 202));
    titleLabel.setFont(displayFont(23.0f, juce::Font::bold));
    titleLabel.setTooltip("Astral Reverberations: orbit-controlled drone, capture tails, delay, and reverb.");
    addAndMakeVisible(titleLabel);

    statusLabel.setJustificationType(juce::Justification::centredRight);
    statusLabel.setColour(juce::Label::textColourId, juce::Colour(185, 195, 202));
    statusLabel.setFont(controlFont(11.0f));
    statusLabel.setTooltip("Shows gate state, effective root source, and live input level.");
    addAndMakeVisible(statusLabel);

    presetBox.addItem("Black Death in London", 1);
    presetBox.addItem("Voyager Lullaby", 2);
    presetBox.addItem("Custom sky", 3);
    presetBox.setSelectedId(1, juce::dontSendNotification);
    presetBox.setTooltip("Current preset.");
    addAndMakeVisible(presetBox);

    previousPresetButton.setButtonText("<");
    previousPresetButton.setTooltip("Previous preset.");
    addAndMakeVisible(previousPresetButton);

    nextPresetButton.setButtonText(">");
    nextPresetButton.setTooltip("Next preset.");
    addAndMakeVisible(nextPresetButton);

    favouriteButton.setButtonText("*");
    favouriteButton.setClickingTogglesState(true);
    favouriteButton.setTooltip("Favourite marker.");
    addAndMakeVisible(favouriteButton);

    aButton.setButtonText("A");
    aButton.setTooltip("Comparison slot A.");
    addAndMakeVisible(aButton);

    bButton.setButtonText("B");
    bButton.setTooltip("Comparison slot B.");
    addAndMakeVisible(bButton);

    copyABButton.setButtonText("A>B");
    copyABButton.setTooltip("Copy comparison slot A to B.");
    addAndMakeVisible(copyABButton);

    scaleButton.setButtonText("100%");
    scaleButton.setTooltip("UI scale.");
    addAndMakeVisible(scaleButton);

    settingsButton.setButtonText("...");
    settingsButton.setTooltip("Plugin settings.");
    addAndMakeVisible(settingsButton);

    dateButton.setButtonText("May 26, 2024");
    dateButton.setTooltip("Manual sky date. Historical presets update this internal date.");
    addAndMakeVisible(dateButton);

    timeButton.setButtonText("12:00");
    timeButton.setTooltip("Manual sky time summary.");
    addAndMakeVisible(timeButton);

    gateModeBox.addItem("Hold", 1);
    gateModeBox.addItem("Live", 2);
    gateModeBox.addItem("Sustain", 3);
    gateModeBox.setSelectedId(1, juce::dontSendNotification);
    gateModeBox.setTooltip("Gate behaviour summary: MIDI notes open the live gate, CC64 sustains it, Drone On holds it.");
    addAndMakeVisible(gateModeBox);

    limiterButton.setButtonText("Limiter");
    limiterButton.setClickingTogglesState(true);
    limiterButton.setToggleState(true, juce::dontSendNotification);
    limiterButton.setTooltip("Output limiter status indicator.");
    addAndMakeVisible(limiterButton);

    controlsButton.setButtonText("MIDI Map");
    controlsButton.setTooltip("Shows keyboard and orbit interaction controls.");
    controlsButton.onClick = [this] {
        controlsPopupVisible = !controlsPopupVisible;
        repaint();
    };
    addAndMakeVisible(controlsButton);

    focusButton.setButtonText("Focus Map");
    focusButton.setClickingTogglesState(true);
    focusButton.setTooltip("Expands Orbit Harmony and hides the side, inspector, meter, and macro controls.");
    focusButton.onClick = [this] {
        mapFocusMode = focusButton.getToggleState();
        controlsPopupVisible = false;
        resized();
        repaint();
    };
    addAndMakeVisible(focusButton);

    addAndMakeVisible(*orbitMap);

    addSlider("euclid_rate", "Rate", SliderLayout::Inspector);
    addSlider("euclid_wet", "Wet", SliderLayout::Inspector);
    addSlider("delay_level", "Level", SliderLayout::Inspector);
    addSlider("delay_time", "Time", SliderLayout::Inspector);
    addSlider("feedback", "Feedback", SliderLayout::Inspector);
    addSlider("wow_flutter", "Wobble", SliderLayout::Inspector);
    addSlider("drive", "Drive", SliderLayout::Inspector);
    addSlider("reverb_level", "Level", SliderLayout::Inspector);
    addSlider("space", "Size", SliderLayout::Inspector);
    addSlider("reverb_decay", "Regen", SliderLayout::Inspector);
    addSlider("reverb_damping", "Damp", SliderLayout::Inspector);
    addSlider("tone", "Tone", SliderLayout::Inspector);
    addSlider("tail_size", "Length", SliderLayout::Inspector);
    addSlider("tail_regen", "Feedback", SliderLayout::Inspector);
    addSlider("macro_substance", "Mix", SliderLayout::Bottom);
    addSlider("macro_choir", "Voices", SliderLayout::Bottom);
    addSlider("macro_mneme", "Tail Memory", SliderLayout::Bottom);
    addSlider("macro_ephemeris", "Orbit Mod", SliderLayout::Bottom);
    addSlider("macro_fate", "Echo", SliderLayout::Bottom);
    addSlider("macro_void", "Reverb", SliderLayout::Bottom);
    addSlider("macro_pulse", "Pluck", SliderLayout::Bottom);
    addSlider("macro_root", "Root Drift", SliderLayout::Bottom);
    addSlider("filter_cutoff", "Cutoff", SliderLayout::Bottom);
    addSlider("filter_resonance", "Q", SliderLayout::Bottom);
    addSlider("filter_radiate", "Radiate", SliderLayout::Bottom);
    addSlider("output_gain", "Output", SliderLayout::Bottom);

    droneHoldButton.setButtonText("On");
    droneHoldButton.setClickingTogglesState(true);
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

    rootLockButton.setButtonText("Manual");
    rootLockButton.setClickingTogglesState(true);
    rootLockButton.setTooltip("Keeps the selected root stable instead of following incoming MIDI notes.");
    addAndMakeVisible(rootLockButton);
    rootLockAttachment = std::make_unique<ButtonAttachment>(audioProcessor.parameters(), "root_lock", rootLockButton);

    planetWaveLabel.setText("Wave", juce::dontSendNotification);
    planetWaveLabel.setJustificationType(juce::Justification::centredLeft);
    planetWaveLabel.setColour(juce::Label::textColourId, juce::Colour(235, 239, 242));
    planetWaveLabel.setFont(controlFont(11.0f, juce::Font::bold));
    planetWaveLabel.setTooltip("Base oscillator waveform for the selected planet.");
    addAndMakeVisible(planetWaveLabel);

    planetWaveBox.addItem("Sine", 1);
    planetWaveBox.addItem("Square", 2);
    planetWaveBox.addItem("Saw", 3);
    planetWaveBox.addItem("Triangle", 4);
    planetWaveBox.addItem("Fold", 5);
    planetWaveBox.setTooltip("Base oscillator waveform for the selected planet.");
    addAndMakeVisible(planetWaveBox);

    tailModeLabel.setText("Mode", juce::dontSendNotification);
    tailModeLabel.setJustificationType(juce::Justification::centredLeft);
    tailModeLabel.setColour(juce::Label::textColourId, juce::Colour(235, 239, 242));
    tailModeLabel.setFont(controlFont(11.0f, juce::Font::bold));
    tailModeLabel.setTooltip("Character mode for captured planet tails.");
    addAndMakeVisible(tailModeLabel);

    tailModeBox.addItem("LIM", 1);
    tailModeBox.addItem("DIST", 2);
    tailModeBox.addItem("SHIM", 3);
    tailModeBox.setTooltip("LIM keeps tails bounded, DIST saturates the feedback loop, SHIM adds octave-like shimmer energy.");
    addAndMakeVisible(tailModeBox);
    tailModeAttachment = std::make_unique<ComboBoxAttachment>(audioProcessor.parameters(), "tail_mode", tailModeBox);

    filterModeLabel.setText("Mode", juce::dontSendNotification);
    filterModeLabel.setJustificationType(juce::Justification::centredLeft);
    filterModeLabel.setColour(juce::Label::textColourId, juce::Colour(235, 239, 242));
    filterModeLabel.setFont(controlFont(10.0f, juce::Font::bold));
    filterModeLabel.setTooltip("Filter response mode.");
    addAndMakeVisible(filterModeLabel);

    filterModeBox.addItem("LP", 1);
    filterModeBox.addItem("BP", 2);
    filterModeBox.addItem("HP", 3);
    filterModeBox.addItem("Notch", 4);
    filterModeBox.setTooltip("Selects the stereo multimode filter response.");
    addAndMakeVisible(filterModeBox);
    filterModeAttachment = std::make_unique<ComboBoxAttachment>(audioProcessor.parameters(), "filter_mode", filterModeBox);

    filterRouteLabel.setText("Route", juce::dontSendNotification);
    filterRouteLabel.setJustificationType(juce::Justification::centredLeft);
    filterRouteLabel.setColour(juce::Label::textColourId, juce::Colour(235, 239, 242));
    filterRouteLabel.setFont(controlFont(10.0f, juce::Font::bold));
    filterRouteLabel.setTooltip("Filter routing.");
    addAndMakeVisible(filterRouteLabel);

    filterRouteBox.addItem("Off", 1);
    filterRouteBox.addItem("Dry", 2);
    filterRouteBox.addItem("Wet", 3);
    filterRouteBox.setTooltip("Routes the stereo multimode filter to the dry branch, wet return, or bypass.");
    addAndMakeVisible(filterRouteBox);
    filterRouteAttachment = std::make_unique<ComboBoxAttachment>(audioProcessor.parameters(), "filter_route", filterRouteBox);

    planetFilterLabel.setText("Filter", juce::dontSendNotification);
    planetFilterLabel.setJustificationType(juce::Justification::centredLeft);
    planetFilterLabel.setColour(juce::Label::textColourId, juce::Colour(235, 239, 242));
    planetFilterLabel.setFont(controlFont(11.0f, juce::Font::bold));
    planetFilterLabel.setTooltip("Low-pass tone filter for the selected planet oscillator.");
    addAndMakeVisible(planetFilterLabel);

    planetFilterSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    planetFilterSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 46, 18);
    planetFilterSlider.setColour(juce::Slider::trackColourId, juce::Colour(52, 58, 66));
    planetFilterSlider.setColour(juce::Slider::backgroundColourId, juce::Colour(25, 27, 31));
    planetFilterSlider.setColour(juce::Slider::thumbColourId, goldColour());
    planetFilterSlider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(235, 239, 242));
    planetFilterSlider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(25, 27, 31));
    planetFilterSlider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    planetFilterSlider.setDoubleClickReturnValue(true, 0.82);
    planetFilterSlider.textFromValueFunction = [](double value) {
        return juce::String(static_cast<int>(std::round(value * 100.0))) + "%";
    };
    planetFilterSlider.setTooltip("Low-pass tone filter for the selected planet oscillator.");
    addAndMakeVisible(planetFilterSlider);

    freezeButton.setButtonText("Hold");
    freezeButton.setClickingTogglesState(true);
    freezeButton.setTooltip("Freezes the main delay/reverb feedback path, not the planet tail buffers.");
    addAndMakeVisible(freezeButton);
    freezeAttachment = std::make_unique<ButtonAttachment>(audioProcessor.parameters(), "freeze", freezeButton);

    euclidButton.setButtonText("On");
    euclidButton.setClickingTogglesState(true);
    euclidButton.setTooltip("Runs planet-position Euclidean rhythms that trigger ring plucks. Planet longitude rotates each pattern.");
    addAndMakeVisible(euclidButton);
    euclidAttachment = std::make_unique<ButtonAttachment>(audioProcessor.parameters(), "euclid_enable", euclidButton);

    captureButton.setButtonText("Arm");
    captureButton.setClickingTogglesState(true);
    captureButton.setTooltip("Arms input capture. Hold number keys 1-0 to feed planet-specific orbit tails.");
    addAndMakeVisible(captureButton);
    captureAttachment = std::make_unique<ButtonAttachment>(audioProcessor.parameters(), "capture_enable", captureButton);

    organButton.setButtonText("Organ");
    organButton.setClickingTogglesState(true);
    organButton.setTooltip("Maps A-; to momentary planet gates. When off, those keys toggle mute.");
    addAndMakeVisible(organButton);
    organAttachment = std::make_unique<ButtonAttachment>(audioProcessor.parameters(), "organ_mode", organButton);

    inputMonitorButton.setButtonText("On");
    inputMonitorButton.setClickingTogglesState(true);
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

    slowButton.setVisible(false);
    normalButton.setVisible(false);
    fastButton.setVisible(false);
    resetPlanetButton.setVisible(false);
    resetOrbitsButton.setVisible(false);

    astroModeBox.addItem("Now", 1);
    astroModeBox.addItem("Manual Date", 2);
    astroModeBox.addItem("Simulated", 3);
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
        historicalPresetBox.addItem(
            juce::String(preset.label.data()) + juce::String::charToString(0x00b7) + " " + juce::String(preset.keyName.data()),
            static_cast<int>(index + 2));
    }
    historicalPresetBox.setTooltip(
        "Recalls planet positions and a curated root key for a historical UTC moment. Switches to Manual Date.");
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
    updatePlanetControls();

    setSize(kDefaultEditorWidth, kDefaultEditorHeight);
    startTimerHz(12);
}

AstralReverberationsEditor::~AstralReverberationsEditor()
{
    setLookAndFeel(nullptr);
}

void AstralReverberationsEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(16, 17, 20));
    drawAstralTexture(graphics, getLocalBounds().toFloat(), kEditorBackgroundTextureOpacity);
    graphics.fillAll(juce::Colour(16, 17, 20).withAlpha(kEditorBackgroundVeilAlpha));

    auto bounds = getLocalBounds().toFloat().reduced(8.0f);
    graphics.setColour(juce::Colour(52, 58, 66));
    graphics.drawRoundedRectangle(bounds, 8.0f, 1.5f);
    drawInterfaceChrome(graphics);
}

void AstralReverberationsEditor::paintOverChildren(juce::Graphics& graphics)
{
    if (controlsPopupVisible) {
        drawControlsPopup(graphics);
    }
}

bool AstralReverberationsEditor::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && mapFocusMode) {
        mapFocusMode = false;
        focusButton.setToggleState(false, juce::dontSendNotification);
        resized();
        repaint();
        return true;
    }

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
    const auto layout = editorLayoutFor(getLocalBounds());
    const bool showControls = !mapFocusMode;

    for (auto* slider : sliders) {
        slider->setVisible(showControls);
    }
    for (auto* label : labels) {
        label->setVisible(showControls);
    }
    if (sliders.size() > 7 && labels.size() > 7) {
        sliders[7]->setVisible(false);
        labels[7]->setVisible(false);
    }

    presetBox.setVisible(false);
    const bool showFocusPerformance = mapFocusMode;
    rootNoteBox.setVisible(showControls || showFocusPerformance);
    gateModeBox.setVisible(false);
    planetWaveBox.setVisible(showControls);
    planetWaveLabel.setVisible(showControls);
    tailModeBox.setVisible(showControls);
    tailModeLabel.setVisible(showControls);
    filterModeBox.setVisible(showControls);
    filterModeLabel.setVisible(showControls);
    filterRouteBox.setVisible(showControls);
    filterRouteLabel.setVisible(showControls);
    planetFilterSlider.setVisible(showControls);
    planetFilterLabel.setVisible(showControls);
    droneHoldButton.setVisible(showControls || showFocusPerformance);
    rootLockButton.setVisible(showControls || showFocusPerformance);
    inputMonitorButton.setVisible(showControls || showFocusPerformance);
    freezeButton.setVisible(showControls || showFocusPerformance);
    captureButton.setVisible(showControls || showFocusPerformance);
    organButton.setVisible(showControls || showFocusPerformance);
    euclidButton.setVisible(showControls || showFocusPerformance);
    resetPlanetButton.setVisible(false);
    resetOrbitsButton.setVisible(false);
    slowButton.setVisible(false);
    normalButton.setVisible(false);
    fastButton.setVisible(false);
    controlsButton.setVisible(showControls);
    previousPresetButton.setVisible(false);
    nextPresetButton.setVisible(false);
    favouriteButton.setVisible(false);
    aButton.setVisible(false);
    bButton.setVisible(false);
    copyABButton.setVisible(false);
    scaleButton.setVisible(false);
    settingsButton.setVisible(false);
    dateButton.setVisible(false);
    timeButton.setVisible(false);
    limiterButton.setVisible(false);
    astroModeBox.setVisible(showControls);
    historicalPresetBox.setVisible(showControls);
    titleLabel.setVisible(true);
    statusLabel.setVisible(true);
    focusButton.setVisible(true);
    orbitMap->setVisible(true);
    focusButton.setButtonText(mapFocusMode ? "Exit Focus" : "Focus Map");
    focusButton.setToggleState(mapFocusMode, juce::dontSendNotification);

    const auto configureFocusSwitch = [this](juce::TextButton& button, const char* label, const char* offText, const char* onText) {
        button.getProperties().set("focusSwitch", mapFocusMode);
        button.getProperties().set("focusLabel", label);
        button.getProperties().set("focusOffText", offText);
        button.getProperties().set("focusOnText", onText);
    };
    configureFocusSwitch(droneHoldButton, "DRONE", "OFF", "ON");
    configureFocusSwitch(captureButton, "CAPTURE", "ARM", "ARMED");
    configureFocusSwitch(freezeButton, "FREEZE", "FREE", "HELD");
    configureFocusSwitch(inputMonitorButton, "MONITOR", "OFF", "ON");
    configureFocusSwitch(euclidButton, "EUCLID", "OFF", "ON");
    configureFocusSwitch(organButton, "ORGAN", "OFF", "ON");
    configureFocusSwitch(rootLockButton, "ROOT MODE", "FOLLOW", "MANUAL");

    if (mapFocusMode) {
        auto bounds = getLocalBounds().reduced(kPadding);
        auto top = bounds.removeFromTop(kTopBarHeight);
        titleLabel.setBounds(top.reduced(10, 8).removeFromLeft(330));
        focusButton.setBounds(top.reduced(10, 8).removeFromRight(110));
        statusLabel.setBounds(top.reduced(10, 8).withTrimmedRight(124).withTrimmedLeft(340));
        bounds.removeFromTop(kPanelGap);
        orbitMap->setBounds(bounds);

        auto overlay = bounds.reduced(14).removeFromBottom(82);
        overlay.setWidth(std::min(overlay.getWidth(), 1080));
        auto row = overlay.reduced(12, 10).removeFromBottom(46);
        const int cellWidth = 116;
        droneHoldButton.setBounds(row.removeFromLeft(cellWidth).removeFromBottom(44));
        row.removeFromLeft(8);
        captureButton.setBounds(row.removeFromLeft(cellWidth).removeFromBottom(44));
        row.removeFromLeft(8);
        freezeButton.setBounds(row.removeFromLeft(cellWidth).removeFromBottom(44));
        row.removeFromLeft(8);
        inputMonitorButton.setBounds(row.removeFromLeft(cellWidth).removeFromBottom(44));
        row.removeFromLeft(8);
        euclidButton.setBounds(row.removeFromLeft(cellWidth).removeFromBottom(44));
        row.removeFromLeft(8);
        organButton.setBounds(row.removeFromLeft(cellWidth).removeFromBottom(44));
        row.removeFromLeft(14);
        rootNoteBox.setBounds(row.removeFromLeft(84).removeFromBottom(44).reduced(0, 5));
        row.removeFromLeft(8);
        rootLockButton.setBounds(row.removeFromLeft(132).removeFromBottom(44));
        return;
    }

    auto top = layout.topBar.reduced(10, 8);
    titleLabel.setBounds(top.removeFromLeft(300));
    focusButton.setBounds(top.removeFromRight(98).reduced(4, 0));
    statusLabel.setBounds(top);

    auto astro = layout.astroBar.reduced(12, 8);
    astroModeBox.setBounds(astro.removeFromLeft(150));
    astro.removeFromLeft(12);
    historicalPresetBox.setBounds(astro.removeFromLeft(330));
    astro.removeFromLeft(12);
    rootNoteBox.setBounds(astro.removeFromLeft(82));
    astro.removeFromLeft(6);
    rootLockButton.setBounds(astro.removeFromLeft(118));

    auto left = layout.leftPanel.reduced(12);
    auto footer = left.removeFromBottom(34);
    organButton.setBounds(footer.removeFromLeft(84).reduced(0, 2));
    footer.removeFromLeft(8);
    controlsButton.setBounds(footer.reduced(0, 2));

    left.removeFromBottom(8);
    left.removeFromTop(24);
    auto performance = left.removeFromTop(136);
    auto toggleRow = performance.removeFromTop(32);
    droneHoldButton.setBounds(toggleRow.removeFromRight(72).reduced(0, 5));
    toggleRow = performance.removeFromTop(32);
    captureButton.setBounds(toggleRow.removeFromRight(72).reduced(0, 5));
    toggleRow = performance.removeFromTop(32);
    freezeButton.setBounds(toggleRow.removeFromRight(72).reduced(0, 5));
    toggleRow = performance.removeFromTop(32);
    inputMonitorButton.setBounds(toggleRow.removeFromRight(72).reduced(0, 5));

    left.removeFromTop(10);
    left.removeFromTop(24);
    auto pluck = left.removeFromTop(142);
    euclidButton.setBounds(pluck.removeFromTop(30).removeFromRight(72).reduced(0, 4));
    pluck.removeFromTop(54);
    for (int index = 0; index < 2; ++index) {
        auto row = pluck.removeFromTop(28).reduced(0, 2);
        labels[index]->setBounds(row.removeFromLeft(64));
        sliders[index]->setBounds(row);
    }

    orbitMap->setBounds(layout.orbitPanel);

    auto inspector = layout.inspector.reduced(12);
    inspector.removeFromTop(36);
    auto waveRow = inspector.removeFromTop(kInspectorRowHeight).reduced(0, 2);
    planetWaveLabel.setBounds(waveRow.removeFromLeft(92));
    planetWaveBox.setBounds(waveRow);
    auto filterRow = inspector.removeFromTop(kInspectorSliderRowHeight).reduced(0, 2);
    planetFilterLabel.setBounds(filterRow.removeFromLeft(92));
    planetFilterSlider.setBounds(filterRow);
    inspector.removeFromTop(4);
    inspector.removeFromTop(20);

    constexpr int kInspectorLabelWidth = 82;
    inspector.removeFromTop(13);
    for (int index = 2; index < 7; ++index) {
        auto row = inspector.removeFromTop(kInspectorSliderRowHeight).reduced(0, 2);
        labels[index]->setBounds(row.removeFromLeft(kInspectorLabelWidth));
        sliders[index]->setBounds(row);
    }

    inspector.removeFromTop(13);
    for (int index = 7; index < 12; ++index) {
        auto row = inspector.removeFromTop(kInspectorSliderRowHeight).reduced(0, 2);
        labels[index]->setBounds(row.removeFromLeft(kInspectorLabelWidth));
        sliders[index]->setBounds(row);
    }

    inspector.removeFromTop(13);
    auto tailModeRow = inspector.removeFromTop(kInspectorRowHeight).reduced(0, 2);
    tailModeLabel.setBounds(tailModeRow.removeFromLeft(kInspectorLabelWidth));
    tailModeBox.setBounds(tailModeRow);
    for (int index = 12; index < 14; ++index) {
        auto row = inspector.removeFromTop(kInspectorSliderRowHeight).reduced(0, 2);
        labels[index]->setBounds(row.removeFromLeft(kInspectorLabelWidth));
        sliders[index]->setBounds(row);
    }

    limiterButton.setBounds(layout.meterColumn.reduced(10).removeFromBottom(32));

    const int bottomSliderStart = 14;
    const int bottomSliderCount = sliders.size() - bottomSliderStart;
    auto bottomArea = layout.macroStrip;
    auto filterSelectArea = bottomArea.removeFromRight(116).reduced(6, 8);
    bottomArea.removeFromRight(4);
    const int cellWidth = bottomArea.getWidth() / std::max(1, bottomSliderCount);
    for (int index = bottomSliderStart; index < sliders.size(); ++index) {
        const int column = index - bottomSliderStart;
        auto cell = juce::Rectangle<int>(
            bottomArea.getX() + column * cellWidth,
            bottomArea.getY(),
            cellWidth,
            bottomArea.getHeight()).reduced(6, 8);
        labels[index]->setBounds(cell.removeFromTop(18));
        sliders[index]->setBounds(cell);
    }

    auto modeRow = filterSelectArea.removeFromTop(46);
    filterModeLabel.setBounds(modeRow.removeFromTop(16));
    filterModeBox.setBounds(modeRow.reduced(0, 2));
    filterSelectArea.removeFromTop(8);
    auto routeRow = filterSelectArea.removeFromTop(46);
    filterRouteLabel.setBounds(routeRow.removeFromTop(16));
    filterRouteBox.setBounds(routeRow.reduced(0, 2));
}

void AstralReverberationsEditor::updateKeyboardControls()
{
    constexpr std::array<juce::juce_wchar, 10> playbackKeys{{'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p'}};
    constexpr std::array<juce::juce_wchar, 10> captureKeys{{'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'}};
    constexpr std::array<juce::juce_wchar, 10> muteKeys{{'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';'}};
    const bool organMode = audioProcessor.parameters().getRawParameterValue("organ_mode") != nullptr
        && audioProcessor.parameters().getRawParameterValue("organ_mode")->load() >= 0.5f;

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
        const auto planet = static_cast<astro::PlanetId>(index);
        const bool isDown = juce::KeyPress::isKeyCurrentlyDown(muteKeys[index]);
        if (organMode) {
            if (isDown && !muteKeyWasDown[index] && !audioProcessor.isPlanetMuted(planet)) {
                orbitMap->showPlanetFeedback(planet, 0.72f);
            }
            audioProcessor.setOrganLayerGate(planet, isDown);
        } else if (isDown && !muteKeyWasDown[index]) {
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
    if (parameterId == "capture_level") {
        return 0.35f;
    }
    if (parameterId == "drone_level") {
        return 0.70f;
    }
    if (parameterId == "pluck_level") {
        return 1.0f;
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
    if (parameterId == "delay_time") {
        return 420.0f;
    }
    if (parameterId == "delay_level") {
        return 0.45f;
    }
    if (parameterId == "feedback") {
        return 0.42f;
    }
    if (parameterId == "wow_flutter") {
        return 0.25f;
    }
    if (parameterId == "drive") {
        return 0.15f;
    }
    if (parameterId == "reverb_level") {
        return 0.55f;
    }
    if (parameterId == "space" || parameterId == "tone") {
        return 0.55f;
    }
    if (parameterId == "reverb_decay") {
        return 0.5f;
    }
    if (parameterId == "reverb_damping") {
        return 0.45f;
    }
    if (parameterId == "reverb_mod") {
        return 0.35f;
    }
    if (parameterId == "pluck_send") {
        return 1.0f;
    }
    if (parameterId == "tail_size" || parameterId == "tail_regen") {
        return 0.0f;
    }
    if (parameterId == "filter_cutoff") {
        return 1200.0f;
    }
    if (parameterId == "filter_resonance") {
        return 0.25f;
    }
    if (parameterId == "filter_radiate") {
        return 0.35f;
    }
    if (parameterId == "master_eq_low" || parameterId == "master_eq_mid" || parameterId == "master_eq_high") {
        return 0.0f;
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
    label->setFont(controlFont(layout == SliderLayout::Inspector ? 11.0f : 10.0f, juce::Font::bold));
    label->setTooltip(sliderTooltip(parameter));
    addAndMakeVisible(label);

    auto* slider = sliders.add(new juce::Slider());
    if (layout == SliderLayout::Inspector) {
        slider->setSliderStyle(juce::Slider::LinearHorizontal);
        slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 15);
        slider->setColour(juce::Slider::trackColourId, juce::Colour(52, 58, 66));
        slider->setColour(juce::Slider::backgroundColourId, juce::Colour(25, 27, 31));
        slider->setColour(juce::Slider::thumbColourId, goldColour());
    } else {
        slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 14);
        slider->setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(105, 198, 212));
        slider->setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(52, 58, 66));
        slider->setColour(juce::Slider::thumbColourId, goldColour());
    }
    slider->setColour(juce::Slider::textBoxTextColourId, juce::Colour(235, 239, 242));
    slider->setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(25, 27, 31));
    slider->setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider->setScrollWheelEnabled(true);
    slider->setPopupDisplayEnabled(true, true, this);
    slider->setDoubleClickReturnValue(true, static_cast<double>(defaultSliderValue(parameter)));
    slider->setTooltip(sliderTooltip(parameter));
    addAndMakeVisible(slider);

    sliderAttachments.push_back(std::make_unique<SliderAttachment>(
        audioProcessor.parameters(),
        parameterId,
        *slider));
    slider->textFromValueFunction = [parameter](double value) {
        return sliderText(parameter, value);
    };
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
    updateKeyboardControls();
    updateRootNoteBox();
    updatePlanetControls();
    syncHistoricalPresetBox();
    updateStatus();
    repaint();
}

void AstralReverberationsEditor::updatePlanetControls()
{
    const auto selected = orbitMap->getSelectedPlanet();
    if (selected == attachedPlanetControls) {
        return;
    }

    attachedPlanetControls = selected;
    planetWaveLabel.setText("Wave", juce::dontSendNotification);
    planetFilterLabel.setText("Filter", juce::dontSendNotification);
    planetWaveAttachment = std::make_unique<ComboBoxAttachment>(
        audioProcessor.parameters(),
        planetParameterId("planet_wave_", selected),
        planetWaveBox);
    planetFilterAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters(),
        planetParameterId("planet_filter_", selected),
        planetFilterSlider);
    planetFilterSlider.textFromValueFunction = [](double value) {
        return juce::String(static_cast<int>(std::round(value * 100.0))) + "%";
    };
}

void AstralReverberationsEditor::applyHistoricalPreset(std::size_t presetIndex)
{
    const auto presets = astro::historicalPresets();
    if (presetIndex >= presets.size()) {
        return;
    }

    const auto& preset = presets[presetIndex];
    audioProcessor.setManualJulianDay(astro::julianDayForPreset(preset));
    setParameterValue("root_note", static_cast<float>(preset.rootMidiNote));
    setParameterValue("macro_root", 0.5f);
    audioProcessor.setAstroMode(AstralReverberationsAudioProcessor::AstroMode::ManualDate);
    astroModeBox.setSelectedId(static_cast<int>(AstralReverberationsAudioProcessor::AstroMode::ManualDate) + 1, juce::dontSendNotification);
    updateRootNoteBox();
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
    const auto gateText = audioProcessor.isDroneHeld() ? "Hold" : audioProcessor.isDroneGateOpen() ? "Live" : "Idle";
    const auto rootText = skyText + "  Root " + root + "  " + gateText + "  " + source + inputText;
    statusLabel.setText(rootText, juce::dontSendNotification);
}

void AstralReverberationsEditor::drawInterfaceChrome(juce::Graphics& graphics)
{
    if (mapFocusMode) {
        auto bounds = getLocalBounds().reduced(kPadding);
        drawPanel(graphics, bounds.removeFromTop(kTopBarHeight).toFloat(), 0.80f);
        bounds.removeFromTop(kPanelGap);
        auto overlay = bounds.reduced(14).removeFromBottom(82).toFloat();
        overlay.setWidth(std::min(overlay.getWidth(), 1080.0f));
        drawPanel(graphics, overlay, 0.78f);

        auto header = overlay.reduced(12.0f, 8.0f).removeFromTop(16.0f);
        graphics.setFont(controlFont(9.0f, juce::Font::bold));
        graphics.setColour(goldColour().withAlpha(0.92f));
        graphics.drawText("PERFORMANCE", header.removeFromLeft(720.0f), juce::Justification::centredLeft);
        graphics.setColour(cyanColour().withAlpha(0.88f));
        graphics.drawText("ROOT SOURCE", header, juce::Justification::centredLeft);
        return;
    }

    const auto layout = editorLayoutFor(getLocalBounds());
    drawPanel(graphics, layout.topBar.toFloat(), 0.82f);
    drawPanel(graphics, layout.astroBar.toFloat(), 0.84f);
    drawPanel(graphics, layout.leftPanel.toFloat(), 0.86f);
    drawPanel(graphics, layout.inspector.toFloat(), 0.88f);
    drawPanel(graphics, layout.meterColumn.toFloat(), 0.88f);
    drawPanel(graphics, layout.macroStrip.toFloat(), 0.86f);
    drawSelectedPlanetSummary(graphics, layout.summaryStrip);
    drawMeters(graphics, layout.meterColumn);

    const float input = juce::jlimit(0.0f, 1.0f, audioProcessor.getInputLevel());
    drawSmallBadge(graphics,
        juce::Rectangle<float>(layout.astroBar.getRight() - 80.0f, layout.astroBar.getY() + 10.0f, 62.0f, 22.0f),
        input > 0.01f ? "INPUT" : "IDLE",
        input > 0.01f ? cyanColour() : mutedTextColour(),
        input > 0.01f);

    auto left = layout.leftPanel.toFloat().reduced(14.0f);
    auto footer = left.removeFromBottom(34.0f);
    footer.removeFromLeft(92.0f);
    graphics.setFont(controlFont(9.0f, juce::Font::bold));
    graphics.setColour(mutedTextColour());
    graphics.drawText("Keyboard / MIDI", footer.reduced(0.0f, 8.0f), juce::Justification::centred);

    left.removeFromBottom(8.0f);
    drawSectionTitle(graphics, left.removeFromTop(20.0f), "Performance");
    const auto drawPerformanceRow = [&graphics](juce::Rectangle<float> row, const juce::String& title, const juce::String& subtitle) {
        graphics.setFont(controlFont(11.0f, juce::Font::bold));
        graphics.setColour(juce::Colour(235, 239, 242));
        graphics.drawText(title, row.withTrimmedRight(74.0f).removeFromTop(18.0f), juce::Justification::centredLeft);
        graphics.setFont(controlFont(9.5f));
        graphics.setColour(mutedTextColour());
        graphics.drawText(subtitle, row.withTrimmedRight(74.0f).translated(0.0f, 16.0f), juce::Justification::centredLeft);
    };
    drawPerformanceRow(left.removeFromTop(32.0f), "DRONE", "Sustain cosmic bed");
    drawPerformanceRow(left.removeFromTop(32.0f), "CAPTURE", "Sample input to orbit");
    drawPerformanceRow(left.removeFromTop(32.0f), "FREEZE", "Hold delay and reverb");
    drawPerformanceRow(left.removeFromTop(32.0f), "INPUT MONITOR", "Hear dry input");

    left.removeFromTop(10.0f);
    drawSectionTitle(graphics, left.removeFromTop(20.0f), "Pluck Engine");
    auto euclidRow = left.removeFromTop(30.0f);
    drawPerformanceRow(euclidRow, "EUCLIDEAN", "Generative plucks");
    left.removeFromTop(8.0f);
    auto polyArea = left.removeFromTop(54.0f);
    graphics.setFont(controlFont(9.0f, juce::Font::bold));
    graphics.setColour(mutedTextColour());
    graphics.drawText("POLY", polyArea.removeFromLeft(34.0f), juce::Justification::centredLeft);
    polyArea.removeFromLeft(4.0f);
    const auto euclidState = audioProcessor.getEuclideanPatternState();
    constexpr std::array<std::size_t, 5> visibleLanes{{0, 2, 4, 7, 9}};
    const float laneHeight = polyArea.getHeight() / static_cast<float>(visibleLanes.size());
    for (std::size_t laneNumber = 0; laneNumber < visibleLanes.size(); ++laneNumber) {
        const auto laneIndex = visibleLanes[laneNumber];
        const auto& lane = euclidState.lanes[laneIndex];
        auto laneArea = polyArea.withY(polyArea.getY() + static_cast<float>(laneNumber) * laneHeight).withHeight(laneHeight).reduced(0.0f, 1.0f);
        const auto colour = accentColour(laneIndex);
        graphics.setFont(controlFont(7.8f, juce::Font::bold));
        graphics.setColour((lane.muted ? mutedTextColour() : colour).withAlpha(lane.muted ? 0.42f : 0.82f));
        graphics.drawText(juce::String(astro::toString(lane.planet).data()).substring(0, 2).toUpperCase(), laneArea.removeFromLeft(18.0f), juce::Justification::centredLeft);
        laneArea.removeFromLeft(2.0f);
        const float cellWidth = laneArea.getWidth() / static_cast<float>(lane.steps);
        const float enabledAlpha = euclidState.enabled && !lane.muted ? 1.0f : 0.32f;
        for (int step = 0; step < lane.steps; ++step) {
            auto cell = laneArea.removeFromLeft(cellWidth).reduced(0.6f, 1.2f);
            const bool hit = lane.hits[static_cast<std::size_t>(step)];
            const bool active = lane.activeStep == step;
            graphics.setColour((hit ? colour : lineColour()).withAlpha((hit ? 0.66f : 0.24f) * enabledAlpha));
            graphics.fillRoundedRectangle(cell, 1.2f);
            if (active) {
                graphics.setColour(cyanColour().withAlpha(0.75f * enabledAlpha));
                graphics.drawRoundedRectangle(cell.expanded(0.7f), 1.8f, 0.8f);
            }
            if (hit && lane.level > 0.04f) {
                graphics.setColour(goldColour().withAlpha(lane.level * 0.62f));
                graphics.fillRoundedRectangle(cell.reduced(1.0f, 0.9f), 0.8f);
            }
        }
    }
    left.removeFromTop(56.0f);

    left.removeFromTop(10.0f);
    drawSectionTitle(graphics, left.removeFromTop(20.0f), "Root & Gate");
    graphics.setFont(controlFont(9.5f, juce::Font::bold));
    graphics.setColour(juce::Colour(235, 239, 242));
    graphics.drawText("MIDI root follows lowest held note.", left.removeFromTop(24.0f), juce::Justification::centredLeft);
    auto rootGateRow = left.removeFromTop(24.0f);
    graphics.setColour(goldColour());
    graphics.drawText("Root " + noteName(audioProcessor.getDroneRootNote()), rootGateRow.removeFromLeft(72.0f), juce::Justification::centredLeft);
    graphics.setColour(audioProcessor.isDroneGateOpen() ? cyanColour() : mutedTextColour());
    graphics.drawText(audioProcessor.isDroneGateOpen() ? "Gate live" : "Gate idle", rootGateRow, juce::Justification::centredLeft);

    auto inspector = layout.inspector.toFloat().reduced(14.0f);
    graphics.setFont(controlFont(10.0f, juce::Font::bold));
    graphics.setColour(juce::Colour(235, 239, 242).withAlpha(0.92f));
    graphics.drawText("SOUND EDITOR", inspector.removeFromTop(16.0f), juce::Justification::centredLeft);
    inspector.removeFromTop(6.0f);

    drawProcessorHeader(graphics, inspector.removeFromTop(14.0f), "SELECTED VOICE", goldColour());
    inspector.removeFromTop(static_cast<float>(kInspectorRowHeight + kInspectorSliderRowHeight + 4));
    graphics.setColour(lineColour().withAlpha(0.52f));
    graphics.drawHorizontalLine(static_cast<int>(std::round(inspector.getY())), inspector.getX(), inspector.getRight());
    inspector.removeFromTop(6.0f);
    graphics.setFont(controlFont(10.0f, juce::Font::bold));
    graphics.setColour(juce::Colour(235, 239, 242).withAlpha(0.92f));
    graphics.drawText("GLOBAL EFFECTS", inspector.removeFromTop(14.0f), juce::Justification::centredLeft);
    drawProcessorHeader(graphics, inspector.removeFromTop(13.0f), "ECHO", cyanColour());
    inspector.removeFromTop(static_cast<float>(kInspectorSliderRowHeight * 5));
    drawProcessorHeader(graphics, inspector.removeFromTop(13.0f), "REVERB", juce::Colour(150, 166, 255));
    inspector.removeFromTop(static_cast<float>(kInspectorSliderRowHeight * 5));
    drawProcessorHeader(graphics, inspector.removeFromTop(13.0f), "CAPTURE TAILS", juce::Colour(202, 142, 255));
}

void AstralReverberationsEditor::drawSelectedPlanetSummary(juce::Graphics& graphics, juce::Rectangle<int> area)
{
    drawPanel(graphics, area.toFloat(), 0.86f);
    auto content = area.toFloat().reduced(14.0f, 10.0f);
    const auto selected = orbitMap->getSelectedPlanet();
    const auto selectedIndex = static_cast<std::size_t>(selected);
    const auto snapshot = audioProcessor.getCurrentSnapshotCopy();
    const auto& planet = snapshot.planets[selectedIndex];
    const auto sign = astro::AstrologyEngine::signForLongitude(planet.longitudeDegrees);
    auto right = content.removeFromRight(285.0f);
    content.removeFromRight(16.0f);
    graphics.setColour(accentColour(selectedIndex).withAlpha(0.22f));
    graphics.fillEllipse(content.getX(), content.getY() + 5.0f, 46.0f, 46.0f);
    graphics.setColour(accentColour(selectedIndex));
    graphics.drawEllipse(content.getX(), content.getY() + 5.0f, 46.0f, 46.0f, 1.4f);

    auto text = content.withTrimmedLeft(58.0f);
    graphics.setFont(controlFont(18.0f, juce::Font::bold));
    graphics.setColour(juce::Colour(234, 226, 202));
    graphics.drawText(juce::String(astro::toString(selected).data()).toUpperCase(), text.removeFromTop(22.0f), juce::Justification::centredLeft);

    auto status = text.removeFromTop(22.0f);
    graphics.setFont(controlFont(10.0f, juce::Font::bold));
    graphics.setColour(audioProcessor.isPlanetMuted(selected) ? juce::Colour(224, 111, 87) : goldColour());
    graphics.drawText(audioProcessor.isPlanetMuted(selected) ? "Muted" : "Active", status.removeFromLeft(58.0f), juce::Justification::centredLeft);
    graphics.setColour(audioProcessor.isManualOrbitTailActive(selected) ? cyanColour() : mutedTextColour());
    graphics.drawText(audioProcessor.isManualOrbitTailActive(selected) ? "Capturing" : "Tail held", status, juce::Justification::centredLeft);

    graphics.setFont(controlFont(10.0f));
    graphics.setColour(mutedTextColour());
    graphics.drawText("Orbit tone, tail, and aspect response.", text.removeFromTop(18.0f), juce::Justification::centredLeft);

    auto meta = right.removeFromLeft(150.0f).withTrimmedTop(8.0f);
    graphics.setFont(controlFont(9.0f, juce::Font::bold));
    graphics.setColour(mutedTextColour());
    graphics.drawText("SIGN", meta.removeFromTop(13.0f), juce::Justification::centredLeft);
    graphics.setFont(controlFont(12.0f, juce::Font::bold));
    graphics.setColour(juce::Colour(235, 239, 242));
    graphics.drawText(juce::String(astro::toString(sign).data()), meta.removeFromTop(17.0f), juce::Justification::centredLeft);
    graphics.setFont(controlFont(9.0f, juce::Font::bold));
    graphics.setColour(mutedTextColour());
    graphics.drawText("SPEED", meta.removeFromTop(13.0f), juce::Justification::centredLeft);
    graphics.setFont(controlFont(12.0f, juce::Font::bold));
    graphics.setColour(juce::Colour(235, 239, 242));
    graphics.drawText(juce::String(planet.speed, 2) + " deg/day", meta.removeFromTop(17.0f), juce::Justification::centredLeft);

    auto aspects = right.withTrimmedLeft(8.0f).withTrimmedTop(8.0f);
    graphics.setFont(controlFont(9.0f, juce::Font::bold));
    graphics.setColour(mutedTextColour());
    graphics.drawText("ASPECTS", aspects.removeFromTop(14.0f), juce::Justification::centredLeft);
    graphics.setFont(controlFont(10.0f));
    graphics.setColour(juce::Colour(235, 239, 242));
    int drawn = 0;
    for (const auto& aspect : snapshot.aspects) {
        if (aspect.a != selected && aspect.b != selected) {
            continue;
        }
        const auto other = aspect.a == selected ? aspect.b : aspect.a;
        graphics.drawText(juce::String(astro::toString(aspect.type).data()) + " " + juce::String(astro::toString(other).data()),
            aspects.removeFromTop(16.0f),
            juce::Justification::centredLeft);
        if (++drawn >= 2) {
            break;
        }
    }
    if (drawn == 0) {
        graphics.drawText("No close aspects", aspects.removeFromTop(16.0f), juce::Justification::centredLeft);
    }
}

void AstralReverberationsEditor::drawMeters(juce::Graphics& graphics, juce::Rectangle<int> meterColumn)
{
    auto area = meterColumn.toFloat().reduced(12.0f, 14.0f);
    graphics.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    graphics.setColour(goldColour());
    graphics.drawText("OUTPUT", area.removeFromTop(18.0f), juce::Justification::centred);
    const float output = juce::jlimit(0.0f, 1.0f, audioProcessor.getOutputLevel());
    const float display = std::pow(output, 0.34f);
    const float db = juce::Decibels::gainToDecibels(std::max(output, 0.0001f));
    graphics.setFont(juce::FontOptions(13.0f));
    graphics.setColour(juce::Colour(235, 239, 242));
    graphics.drawText(output <= 0.0001f ? "-inf dB" : juce::String(db, 1) + " dB", area.removeFromTop(24.0f), juce::Justification::centred);

    auto meters = area.removeFromTop(290.0f).reduced(8.0f, 10.0f);
    const float barWidth = (meters.getWidth() - 8.0f) * 0.5f;
    for (int channel = 0; channel < 2; ++channel) {
        auto bar = juce::Rectangle<float>(meters.getX() + static_cast<float>(channel) * (barWidth + 8.0f), meters.getY(), barWidth, meters.getHeight());
        graphics.setColour(juce::Colour(14, 16, 20));
        graphics.fillRoundedRectangle(bar, 4.0f);
        auto fill = bar.reduced(3.0f);
        fill.setTop(fill.getBottom() - fill.getHeight() * display);
        const auto colour = output > 0.92f ? juce::Colour(224, 111, 87) : output > 0.68f ? juce::Colour(171, 132, 255) : juce::Colour(110, 214, 189);
        graphics.setColour(colour.withAlpha(0.92f));
        graphics.fillRoundedRectangle(fill, 3.0f);
        graphics.setColour(lineColour().withAlpha(0.62f));
        graphics.drawRoundedRectangle(bar, 4.0f, 1.0f);
    }

    auto warning = area.removeFromTop(24.0f);
    drawSmallBadge(graphics, warning.reduced(0.0f, 2.0f), output > 0.96f ? "CLIP" : "SAFE", output > 0.96f ? juce::Colour(224, 111, 87) : cyanColour(), output > 0.96f);
    area.removeFromTop(10.0f);
    graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    graphics.setColour(mutedTextColour());
    graphics.drawText("INPUT", area.removeFromTop(14.0f), juce::Justification::centred);
    const float input = juce::jlimit(0.0f, 1.0f, audioProcessor.getInputLevel());
    auto inputBar = area.removeFromTop(12.0f);
    graphics.setColour(lineColour().withAlpha(0.7f));
    graphics.fillRoundedRectangle(inputBar, 3.0f);
    auto inputFill = inputBar;
    inputFill.setWidth(inputBar.getWidth() * std::pow(input, 0.35f));
    graphics.setColour(cyanColour());
    graphics.fillRoundedRectangle(inputFill, 3.0f);
}

void AstralReverberationsEditor::drawControlsPopup(juce::Graphics& graphics)
{
    const auto layout = editorLayoutFor(getLocalBounds());
    auto popup = juce::Rectangle<float>(
        static_cast<float>(layout.inspector.getX() - 332),
        static_cast<float>(layout.astroBar.getBottom() + 8),
        320.0f,
        338.0f);

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
    const bool organMode = audioProcessor.parameters().getRawParameterValue("organ_mode") != nullptr
        && audioProcessor.parameters().getRawParameterValue("organ_mode")->load() >= 0.5f;
    graphics.drawText(
        organMode ? "QWERTY plucks    1-0 captures    A-; organ gates" : "QWERTY plucks    1-0 captures    A-; mutes",
        content.removeFromTop(18.0f),
        juce::Justification::centredLeft);
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
    drawLegendRow(content.removeFromTop(24.0f), "1-0", "hold with Capture on to feed audio tails", goldColour());
    drawLegendRow(content.removeFromTop(24.0f), "Eu", "toggle planet-rotated Euclidean plucks", juce::Colour(123, 196, 165));
    drawLegendRow(
        content.removeFromTop(24.0f),
        "A-;",
        organMode ? "hold to gate each planet drone layer" : "press to mute or unmute each ring",
        juce::Colour(224, 111, 87));
    drawLegendRow(content.removeFromTop(24.0f), "Org", "toggle Organ mode for A-; row", juce::Colour(224, 111, 87));
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
        const bool organHeld = organMode && audioProcessor.isOrganLayerActive(planet);
        graphics.setFont(juce::FontOptions(9.5f, juce::Font::bold));
        graphics.setColour(accentColour(index));
        graphics.drawText(keyLabelForPlanet(playKeys, planet), row.removeFromLeft(18.0f), juce::Justification::centredLeft);
        graphics.setColour(muted ? juce::Colour(224, 111, 87) : organHeld ? accentColour(index) : juce::Colour(185, 195, 202));
        graphics.drawText(keyLabelForPlanet(muteKeys, planet), row.removeFromLeft(18.0f), juce::Justification::centredLeft);
        row.removeFromLeft(6.0f);
        graphics.setFont(juce::FontOptions(9.5f));
        graphics.setColour(muted ? juce::Colour(125, 132, 140) : juce::Colour(235, 239, 242));
        graphics.drawText(juce::String(astro::toString(planet).data()), row, juce::Justification::centredLeft);
    }
}

} // namespace astral::plugin
