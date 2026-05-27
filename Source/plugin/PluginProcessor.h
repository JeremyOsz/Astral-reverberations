#pragma once

#include "astro/AstroMapper.h"
#include "astro/AstrologyEngine.h"
#include "dsp/AstralReverbDelay.h"
#include "dsp/PlanetaryDrone.h"

#include <array>
#include <juce_audio_processors/juce_audio_processors.h>

namespace astral::plugin {

class AstralReverberationsAudioProcessor final : public juce::AudioProcessor {
public:
    enum class AstroMode {
        Now = 0,
        ManualDate,
        SimulatedOrbit
    };

    AstralReverberationsAudioProcessor();
    ~AstralReverberationsAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& parameters() { return parameterState; }
    const juce::AudioProcessorValueTreeState& parameters() const { return parameterState; }

    AstroMode getAstroMode() const;
    void setAstroMode(AstroMode mode);
    double getManualJulianDay() const;
    void setManualJulianDay(double julianDay);
    const astro::AstroSnapshot& getCurrentSnapshot() const { return currentSnapshot; }
    const astro::AstroDroneFrame& getCurrentDroneFrame() const { return currentDroneFrame; }
    bool isDroneGateOpen() const;
    bool isDroneHeld() const;
    int getDroneRootNote() const { return currentRootMidiNote; }
    void setManualPlanetLongitude(astro::PlanetId planet, float longitudeDegrees);

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    juce::AudioProcessorValueTreeState parameterState;
    dsp::AstralReverbDelay effect;
    dsp::PlanetaryDrone drone;
    astro::AstrologyEngine astrologyEngine;
    astro::AstroMapper astroMapper;
    astro::AstroSnapshot currentSnapshot;
    astro::AstroSnapshot manualSnapshot;
    astro::AstroDroneFrame currentDroneFrame;
    juce::AudioBuffer<float> processingBuffer;
    std::array<bool, 128> heldMidiNotes{};
    double simulatedJulianDay = astro::AstrologyEngine::J2000JulianDay;
    double manualJulianDay = astro::AstrologyEngine::J2000JulianDay;
    double currentSampleRate = 44100.0;
    bool hasManualSnapshot = false;
    bool droneGateOpen = false;
    int currentRootMidiNote = 45;

    dsp::AstralParameters readParameters() const;
    dsp::PlanetaryDroneParameters readDroneParameters() const;
    astro::AstroModulationFrame nextAstroFrame(int samplesInBlock);
    void updateMidiGate(const juce::MidiBuffer& midiMessages);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AstralReverberationsAudioProcessor)
};

} // namespace astral::plugin
