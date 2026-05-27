#pragma once

#include "plugin/PluginProcessor.h"

#include <memory>
#include <vector>

namespace astral::plugin {

class OrbitMapComponent;

class AstralReverberationsEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit AstralReverberationsEditor(AstralReverberationsAudioProcessor& processor);
    ~AstralReverberationsEditor() override = default;

    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    AstralReverberationsAudioProcessor& audioProcessor;
    std::unique_ptr<OrbitMapComponent> orbitMap;
    juce::OwnedArray<juce::Slider> sliders;
    juce::OwnedArray<juce::Label> labels;
    juce::ToggleButton droneHoldButton;
    juce::ToggleButton freezeButton;
    juce::ToggleButton captureButton;
    juce::ComboBox astroModeBox;
    juce::Label titleLabel;
    juce::Label statusLabel;
    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
    std::unique_ptr<ButtonAttachment> droneHoldAttachment;
    std::unique_ptr<ButtonAttachment> freezeAttachment;
    std::unique_ptr<ButtonAttachment> captureAttachment;

    void addSlider(const char* parameterId, const juce::String& labelText);
    void timerCallback() override;
    void updateStatus();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AstralReverberationsEditor)
};

} // namespace astral::plugin
