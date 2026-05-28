#pragma once

#include "plugin/PluginProcessor.h"

#include <array>
#include <memory>
#include <vector>

namespace astral::plugin {

class OrbitMapComponent;

/// Main plugin editor containing the orbit map, inspector controls, and keyboard tail gates.
class AstralReverberationsEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit AstralReverberationsEditor(AstralReverberationsAudioProcessor& processor);
    ~AstralReverberationsEditor() override = default;

    /// Paints the editor background frame.
    void paint(juce::Graphics& graphics) override;
    /// Draws transient overlays above child controls.
    void paintOverChildren(juce::Graphics& graphics) override;
    /// Lays out the orbit map, inspector, and bottom controls.
    void resized() override;
    /// Handles keyboard ring playback, mute toggles, and overlay shortcuts.
    bool keyPressed(const juce::KeyPress& key) override;
    /// Keeps momentary planet-tail gates synchronized with held keys.
    bool keyStateChanged(bool isKeyDown) override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    AstralReverberationsAudioProcessor& audioProcessor;
    std::unique_ptr<OrbitMapComponent> orbitMap;
    juce::OwnedArray<juce::Slider> sliders;
    juce::OwnedArray<juce::Label> labels;
    juce::ComboBox rootNoteBox;
    juce::ComboBox planetWaveBox;
    juce::Label planetWaveLabel;
    juce::Slider planetFilterSlider;
    juce::Label planetFilterLabel;
    juce::ToggleButton droneHoldButton;
    juce::ToggleButton rootLockButton;
    juce::ToggleButton inputMonitorButton;
    juce::ToggleButton freezeButton;
    juce::ToggleButton captureButton;
    juce::ToggleButton organButton;
    juce::ToggleButton euclidButton;
    juce::TextButton resetPlanetButton;
    juce::TextButton resetOrbitsButton;
    juce::TextButton slowButton;
    juce::TextButton normalButton;
    juce::TextButton fastButton;
    juce::TextButton controlsButton;
    juce::ComboBox astroModeBox;
    juce::ComboBox historicalPresetBox;
    juce::Label titleLabel;
    juce::Label statusLabel;
    juce::TooltipWindow tooltipWindow;
    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
    std::unique_ptr<SliderAttachment> planetFilterAttachment;
    std::unique_ptr<ComboBoxAttachment> planetWaveAttachment;
    std::unique_ptr<ButtonAttachment> droneHoldAttachment;
    std::unique_ptr<ButtonAttachment> rootLockAttachment;
    std::unique_ptr<ButtonAttachment> inputMonitorAttachment;
    std::unique_ptr<ButtonAttachment> freezeAttachment;
    std::unique_ptr<ButtonAttachment> captureAttachment;
    std::unique_ptr<ButtonAttachment> organAttachment;
    std::unique_ptr<ButtonAttachment> euclidAttachment;
    std::array<bool, static_cast<std::size_t>(astro::PlanetId::Count)> playbackKeyWasDown{};
    std::array<bool, static_cast<std::size_t>(astro::PlanetId::Count)> captureKeyWasDown{};
    std::array<bool, static_cast<std::size_t>(astro::PlanetId::Count)> muteKeyWasDown{};
    bool controlsPopupVisible = false;
    bool syncingHistoricalPresetUi = false;
    astro::PlanetId attachedPlanetControls = astro::PlanetId::Count;

    enum class SliderLayout { Inspector, Bottom };

    /// Creates a labelled slider and binds it to an APVTS parameter.
    void addSlider(const char* parameterId, const juce::String& labelText, SliderLayout layout);
    /// Writes a normalized host-notified value to an APVTS parameter by id.
    void setParameterValue(const char* parameterId, float value);
    /// Synchronizes the root combo box with the root_note parameter.
    void updateRootNoteBox();
    /// Polls keyboard rows and updates momentary ring gates plus mute toggles.
    void updateKeyboardControls();
    void timerCallback() override;
    /// Rebinds selected-planet wave/filter controls when the selected orbit node changes.
    void updatePlanetControls();
    /// Refreshes gate/root/input status text in the header.
    void updateStatus();
    /// Applies a built-in historical Julian day and switches to Manual Date mode.
    void applyHistoricalPreset(std::size_t presetIndex);
    /// Keeps the preset combo aligned with the stored manual Julian day.
    void syncHistoricalPresetBox();
    /// Draws the dedicated live input meter in the right inspector strip.
    void drawInputMeter(juce::Graphics& graphics);
    /// Draws the controls reference popup.
    void drawControlsPopup(juce::Graphics& graphics);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AstralReverberationsEditor)
};

} // namespace astral::plugin
