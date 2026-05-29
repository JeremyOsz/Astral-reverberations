#pragma once

#include "astro/AstroMapper.h"
#include "astro/AstrologyEngine.h"
#include "dsp/AstralReverbDelay.h"
#include "dsp/PlanetaryDrone.h"

#include <array>
#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>

namespace astral::plugin {

class AstralReverberationsAudioProcessor final : public juce::AudioProcessor {
public:
    /// Selects the base orbit source before per-planet longitude offsets are applied.
    enum class AstroMode {
        Now = 0,
        ManualDate,
        SimulatedOrbit
    };

    struct EuclideanPatternState {
        std::array<bool, 16> hits{};
        std::array<float, 16> levels{};
        int activeStep = -1;
        bool enabled = false;
    };

    AstralReverberationsAudioProcessor();
    ~AstralReverberationsAudioProcessor() override = default;

    /// Prepares the DSP engines and scratch buffers for playback.
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    /// Clears DSP resources and state when playback stops.
    void releaseResources() override;
    /// Restricts the plugin to matching stereo input and stereo output.
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    /// Processes one audio/MIDI block through drone, capture, delay, and reverb.
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    /// Creates the Orbit Harmony editor.
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

    /// Serializes APVTS parameters and orbit mode/date state.
    void getStateInformation(juce::MemoryBlock& destData) override;
    /// Restores APVTS parameters and orbit mode/date state from host state.
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& parameters() { return parameterState; }
    const juce::AudioProcessorValueTreeState& parameters() const { return parameterState; }

    /// Returns the active orbit source mode.
    AstroMode getAstroMode() const;
    /// Sets the orbit source mode used by the next audio block.
    void setAstroMode(AstroMode mode);
    /// Returns the fixed Julian day used by Manual Date mode.
    double getManualJulianDay() const;
    /// Sets the fixed Julian day used by Manual Date mode.
    void setManualJulianDay(double julianDay);
    /// Returns a lock-protected copy of the latest UI-visible orbit snapshot.
    astro::AstroSnapshot getCurrentSnapshotCopy() const;
    /// Returns a lock-protected copy of the latest UI-visible drone mapping.
    astro::AstroDroneFrame getCurrentDroneFrameCopy() const;
    /// Returns true when MIDI or the Drone On parameter is opening the drone gate.
    bool isDroneGateOpen() const;
    /// Returns true when the manual Drone On parameter is active.
    bool isDroneHeld() const;
    /// Returns true when the UI root should ignore incoming MIDI notes.
    bool isManualRootLocked() const;
    /// Returns the effective MIDI root note before transposition.
    int getDroneRootNote() const;
    /// Sets a per-planet longitude offset relative to the active orbit source.
    void setManualPlanetLongitude(astro::PlanetId planet, float longitudeDegrees);
    /// Clears one planet's longitude offset.
    void resetPlanetLongitude(astro::PlanetId planet);
    /// Clears all planet longitude offsets.
    void resetPlanetLongitudes();
    /// Returns true when a planet currently has a user offset.
    bool hasPlanetLongitudeOffset(astro::PlanetId planet) const;
    /// Opens or closes the momentary keyboard gate for one planet tail.
    void setManualOrbitTailGate(astro::PlanetId planet, bool active);
    /// Opens or closes the momentary organ-mode gate for one planet drone layer.
    void setOrganLayerGate(astro::PlanetId planet, bool active);
    /// Queues a one-shot excitation into the main reverb tank from an orbit-ring click or MIDI pluck.
    void tapOrbitReverbTank(astro::PlanetId planet, float wetAmount = 1.0f);
    /// Returns true when a planet tail key is held and the planet is not muted.
    bool isManualOrbitTailActive(astro::PlanetId planet) const;
    /// Returns true when an organ-mode gate key is held and the planet is not muted.
    bool isOrganLayerActive(astro::PlanetId planet) const;
    /// Returns the latest smoothed captured-tail level for a planet.
    float getOrbitTailLevel(astro::PlanetId planet) const;
    /// Toggles a planet layer across drone, capture, and node-tap contribution.
    void togglePlanetMute(astro::PlanetId planet);
    /// Returns true when a planet layer is muted.
    bool isPlanetMuted(astro::PlanetId planet) const;
    /// Returns a smoothed input peak meter in the range 0..1.
    float getInputLevel() const;
    /// Returns a smoothed post-effect output peak meter in the range 0..1.
    float getOutputLevel() const;
    /// Returns whether a zodiac sign effect marker is active.
    bool isSignEffectEnabled(astro::ZodiacSign sign) const;
    /// Toggles one zodiac sign effect marker.
    void toggleSignEffect(astro::ZodiacSign sign);
    /// Advances one sign marker to the next assignable effect role.
    void cycleSignEffect(astro::ZodiacSign sign);
    /// Returns the current effect role assigned to a sign marker.
    int getSignEffectAssignment(astro::ZodiacSign sign) const;
    /// Returns a compact UI snapshot of the Euclidean pluck pattern and recent activity.
    EuclideanPatternState getEuclideanPatternState() const;

    /// Builds the complete APVTS parameter layout for plugin state and automation.
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    juce::AudioProcessorValueTreeState parameterState;
    dsp::AstralReverbDelay effect;
    dsp::PlanetaryDrone drone;
    astro::AstrologyEngine astrologyEngine;
    astro::AstroMapper astroMapper;
    astro::AstroSnapshot currentSnapshot;
    astro::AstroDroneFrame currentDroneFrame;
    mutable juce::CriticalSection astroStateLock;
    juce::AudioBuffer<float> processingBuffer;
    juce::AudioBuffer<float> inputCopyBuffer;
    std::array<bool, 128> heldDroneGateNotes{};
    std::array<std::atomic<bool>, static_cast<std::size_t>(astro::PlanetId::Count)> manualOrbitTailGates{};
    std::array<std::atomic<bool>, static_cast<std::size_t>(astro::PlanetId::Count)> organLayerGates{};
    std::array<std::atomic<bool>, static_cast<std::size_t>(astro::PlanetId::Count)> planetMutes{};
    std::array<std::atomic<float>, static_cast<std::size_t>(astro::PlanetId::Count)> orbitTailLevels{};
    std::array<std::atomic<float>, static_cast<std::size_t>(astro::PlanetId::Count)> planetLongitudeOffsets{};
    std::array<std::atomic<bool>, static_cast<std::size_t>(astro::PlanetId::Count)> planetLongitudeOffsetEnabled{};
    std::array<std::atomic<bool>, 12> signEffectEnabled{};
    std::array<std::atomic<int>, 12> signEffectAssignments{};
    std::atomic<int> pendingReverbTankTaps{0};
    std::atomic<float> pendingReverbTankTapPan{0.0f};
    std::atomic<float> pendingReverbTankTapTone{0.45f};
    std::atomic<float> pendingReverbTankTapFrequencyHz{220.0f};
    std::atomic<float> pendingReverbTankTapWet{1.0f};
    std::atomic<float> inputLevel{0.0f};
    std::atomic<float> outputLevel{0.0f};
    std::array<int, static_cast<std::size_t>(astro::PlanetId::Count)> euclideanSteps{};
    std::array<std::atomic<float>, 16> euclideanDisplayLevels{};
    std::atomic<int> euclideanDisplayStep{-1};
    double simulatedJulianDay = astro::AstrologyEngine::J2000JulianDay;
    double manualJulianDay = astro::AstrologyEngine::J2000JulianDay;
    double currentSampleRate = 44100.0;
    double euclideanStepAccumulator = 0.0;
    bool droneGateOpen = false;
    bool midiSustainPedalDown = false;
    float midiPitchBendSemitones = 0.0f;
    int currentMidiRootNote = 45;

    /// Reads APVTS effect parameters into the JUCE-free delay/reverb parameter struct.
    dsp::AstralParameters readParameters() const;
    /// Reads APVTS and UI state into the JUCE-free drone parameter struct.
    dsp::PlanetaryDroneParameters readDroneParameters() const;
    /// Advances or reads the active orbit source and maps it into modulation frames.
    astro::AstroModulationFrame nextAstroFrame(int samplesInBlock);
    /// Handles MIDI notes, CC, pitch bend, and sustain for performance control.
    void processMidiInput(const juce::MidiBuffer& midiMessages);
    /// Applies a MIDI CC value to a normalized APVTS parameter.
    void setParameterFromMidiCc(const char* parameterId, int ccValue);
    /// Applies a MIDI CC threshold to a boolean APVTS parameter.
    void setBoolParameterFromMidiCc(const char* parameterId, int ccValue);
    /// Recomputes drone gate and root from held notes and sustain pedal.
    void refreshMidiVoiceState();
    /// Root frequency in Hz including macro offset and pitch bend.
    float getDroneRootFrequencyHz() const;
    /// Applies user longitude offsets to a freshly generated orbit snapshot.
    void applyPlanetLongitudeOffsets(astro::AstroSnapshot& snapshot) const;
    /// Advances the planet-position Euclidean pluck sequencer.
    void processEuclideanPlucks(int samplesInBlock, const astro::AstroDroneFrame& droneFrame);
    /// Queues a pluck tap using one mapped planet harmonic layer.
    void queueReverbTankTap(std::size_t planetIndex, const astro::AstroHarmonicLayer& layer, float wetAmount);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AstralReverberationsAudioProcessor)
};

} // namespace astral::plugin
