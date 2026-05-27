#include "plugin/PluginProcessor.h"

#include "plugin/PluginEditor.h"

#include <cmath>

namespace astral::plugin {
namespace {

constexpr auto kAstroModeProperty = "astroMode";
constexpr auto kManualJulianDayProperty = "manualJulianDay";

juce::String parameterId(const char* id)
{
    return juce::String(id);
}

float getFloatParameter(const juce::AudioProcessorValueTreeState& state, const char* id)
{
    if (const auto* value = state.getRawParameterValue(parameterId(id))) {
        return value->load();
    }
    return 0.0f;
}

float midiNoteToFrequency(int note)
{
    return 440.0f * std::pow(2.0f, (static_cast<float>(note) - 69.0f) / 12.0f);
}

juce::NormalisableRange<float> range(float start, float end, float interval = 0.0f, float skew = 1.0f)
{
    return {start, end, interval, skew};
}

} // namespace

AstralReverberationsAudioProcessor::AstralReverberationsAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameterState(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    currentSnapshot = astrologyEngine.snapshotForJulianDay(simulatedJulianDay);
    manualSnapshot = currentSnapshot;
    currentDroneFrame = astroMapper.mapDroneSnapshot(currentSnapshot);
    parameterState.state.setProperty(kAstroModeProperty, static_cast<int>(AstroMode::Now), nullptr);
    parameterState.state.setProperty(kManualJulianDayProperty, manualJulianDay, nullptr);
}

void AstralReverberationsAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    effect.prepare(sampleRate, samplesPerBlock);
    drone.prepare(sampleRate, samplesPerBlock);
    processingBuffer.setSize(2, samplesPerBlock, false, false, true);
}

void AstralReverberationsAudioProcessor::releaseResources()
{
    effect.reset();
    drone.reset();
}

bool AstralReverberationsAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& input = layouts.getMainInputChannelSet();
    const auto& output = layouts.getMainOutputChannelSet();
    return input == output && output == juce::AudioChannelSet::stereo();
}

void AstralReverberationsAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    updateMidiGate(midiMessages);

    const auto totalInputChannels = getTotalNumInputChannels();
    const auto totalOutputChannels = getTotalNumOutputChannels();
    for (auto channel = totalInputChannels; channel < totalOutputChannels; ++channel) {
        buffer.clear(channel, 0, buffer.getNumSamples());
    }

    if (buffer.getNumChannels() < 2) {
        buffer.clear();
        return;
    }

    if (processingBuffer.getNumSamples() < buffer.getNumSamples()) {
        processingBuffer.setSize(2, buffer.getNumSamples(), false, false, true);
    }

    processingBuffer.copyFrom(0, 0, buffer, 0, 0, buffer.getNumSamples());
    processingBuffer.copyFrom(1, 0, buffer, 1, 0, buffer.getNumSamples());

    const auto astroFrame = nextAstroFrame(buffer.getNumSamples());
    drone.processBlock(
        {buffer.getWritePointer(0), buffer.getWritePointer(1), buffer.getNumSamples()},
        {processingBuffer.getWritePointer(0), processingBuffer.getWritePointer(1), buffer.getNumSamples()},
        readDroneParameters(),
        currentDroneFrame);

    effect.processBlock(
        {processingBuffer.getWritePointer(0), processingBuffer.getWritePointer(1), buffer.getNumSamples()},
        {processingBuffer.getWritePointer(0), processingBuffer.getWritePointer(1), buffer.getNumSamples()},
        readParameters(),
        astroFrame);

    buffer.copyFrom(0, 0, processingBuffer, 0, 0, buffer.getNumSamples());
    buffer.copyFrom(1, 0, processingBuffer, 1, 0, buffer.getNumSamples());
}

juce::AudioProcessorEditor* AstralReverberationsAudioProcessor::createEditor()
{
    return new AstralReverberationsEditor(*this);
}

void AstralReverberationsAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameterState.copyState();
    state.setProperty(kAstroModeProperty, static_cast<int>(getAstroMode()), nullptr);
    state.setProperty(kManualJulianDayProperty, manualJulianDay, nullptr);
    if (auto xml = state.createXml()) {
        copyXmlToBinary(*xml, destData);
    }
}

void AstralReverberationsAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes)) {
        if (xml->hasTagName(parameterState.state.getType())) {
            parameterState.replaceState(juce::ValueTree::fromXml(*xml));
            manualJulianDay = static_cast<double>(parameterState.state.getProperty(
                kManualJulianDayProperty,
                astro::AstrologyEngine::J2000JulianDay));
        }
    }
}

AstralReverberationsAudioProcessor::AstroMode AstralReverberationsAudioProcessor::getAstroMode() const
{
    const int raw = static_cast<int>(parameterState.state.getProperty(kAstroModeProperty, 0));
    return static_cast<AstroMode>(juce::jlimit(0, 2, raw));
}

void AstralReverberationsAudioProcessor::setAstroMode(AstroMode mode)
{
    parameterState.state.setProperty(kAstroModeProperty, static_cast<int>(mode), nullptr);
    if (mode == AstroMode::Now) {
        hasManualSnapshot = false;
    }
}

double AstralReverberationsAudioProcessor::getManualJulianDay() const
{
    return manualJulianDay;
}

void AstralReverberationsAudioProcessor::setManualJulianDay(double julianDay)
{
    manualJulianDay = julianDay;
    hasManualSnapshot = false;
    parameterState.state.setProperty(kManualJulianDayProperty, manualJulianDay, nullptr);
}

bool AstralReverberationsAudioProcessor::isDroneGateOpen() const
{
    return droneGateOpen || isDroneHeld();
}

bool AstralReverberationsAudioProcessor::isDroneHeld() const
{
    return getFloatParameter(parameterState, "drone_hold") >= 0.5f;
}

void AstralReverberationsAudioProcessor::setManualPlanetLongitude(astro::PlanetId planet, float longitudeDegrees)
{
    if (getAstroMode() == AstroMode::Now) {
        return;
    }

    if (!hasManualSnapshot) {
        manualSnapshot = currentSnapshot;
        hasManualSnapshot = true;
    }

    const auto index = static_cast<std::size_t>(planet);
    manualSnapshot.planets[index].longitudeDegrees = astro::AstrologyEngine::normalizeDegrees(longitudeDegrees);
    manualSnapshot.aspects = astrologyEngine.detectAspects(manualSnapshot);
    currentSnapshot = manualSnapshot;
    currentDroneFrame = astroMapper.mapDroneSnapshot(currentSnapshot);
}

dsp::AstralParameters AstralReverberationsAudioProcessor::readParameters() const
{
    dsp::AstralParameters parameters;
    parameters.mix = getFloatParameter(parameterState, "mix");
    parameters.delayLevel = getFloatParameter(parameterState, "delay_level");
    parameters.reverbLevel = getFloatParameter(parameterState, "reverb_level");
    parameters.delayTimeMs = getFloatParameter(parameterState, "delay_time");
    parameters.feedback = getFloatParameter(parameterState, "feedback");
    parameters.space = getFloatParameter(parameterState, "space");
    parameters.tone = getFloatParameter(parameterState, "tone");
    parameters.modDepth = getFloatParameter(parameterState, "mod_depth");
    parameters.wowFlutter = getFloatParameter(parameterState, "wow_flutter");
    parameters.drive = getFloatParameter(parameterState, "drive");
    parameters.astroAmount = getFloatParameter(parameterState, "astro_amount");
    parameters.astroSpeed = getFloatParameter(parameterState, "astro_speed");
    parameters.freeze = getFloatParameter(parameterState, "freeze") >= 0.5f;
    parameters.outputGain = getFloatParameter(parameterState, "output_gain");
    return parameters;
}

dsp::PlanetaryDroneParameters AstralReverberationsAudioProcessor::readDroneParameters() const
{
    dsp::PlanetaryDroneParameters parameters;
    parameters.droneLevel = getFloatParameter(parameterState, "drone_level");
    parameters.captureLevel = getFloatParameter(parameterState, "capture_level");
    parameters.harmonicSpread = getFloatParameter(parameterState, "harmonic_spread");
    parameters.aspectDepth = getFloatParameter(parameterState, "aspect_depth");
    parameters.rootFrequencyHz = midiNoteToFrequency(currentRootMidiNote + static_cast<int>(std::round(getFloatParameter(parameterState, "root_offset"))));
    parameters.gate = isDroneGateOpen();
    parameters.captureEnabled = getFloatParameter(parameterState, "capture_enable") >= 0.5f;
    return parameters;
}

astro::AstroModulationFrame AstralReverberationsAudioProcessor::nextAstroFrame(int samplesInBlock)
{
    switch (getAstroMode()) {
        case AstroMode::Now:
            currentSnapshot = astrologyEngine.snapshotForUnixSeconds(astro::AstrologyEngine::unixSecondsFromSystemClock());
            break;
        case AstroMode::ManualDate:
            currentSnapshot = hasManualSnapshot ? manualSnapshot : astrologyEngine.snapshotForJulianDay(manualJulianDay);
            break;
        case AstroMode::SimulatedOrbit: {
            if (hasManualSnapshot) {
                currentSnapshot = manualSnapshot;
            } else {
                const double blockSeconds = static_cast<double>(samplesInBlock) / currentSampleRate;
                const double daysPerSecond = 0.01 * std::max(0.1f, getFloatParameter(parameterState, "astro_speed"));
                simulatedJulianDay += blockSeconds * daysPerSecond;
                currentSnapshot = astrologyEngine.snapshotForJulianDay(simulatedJulianDay);
            }
            break;
        }
    }

    currentDroneFrame = astroMapper.mapDroneSnapshot(currentSnapshot);
    return astroMapper.mapSnapshot(currentSnapshot);
}

void AstralReverberationsAudioProcessor::updateMidiGate(const juce::MidiBuffer& midiMessages)
{
    for (const auto metadata : midiMessages) {
        const auto message = metadata.getMessage();
        if (message.isNoteOn()) {
            heldMidiNotes[static_cast<std::size_t>(message.getNoteNumber())] = true;
        } else if (message.isNoteOff()) {
            heldMidiNotes[static_cast<std::size_t>(message.getNoteNumber())] = false;
        } else if (message.isAllNotesOff() || message.isAllSoundOff()) {
            heldMidiNotes.fill(false);
        }
    }

    droneGateOpen = false;
    for (int note = 0; note < static_cast<int>(heldMidiNotes.size()); ++note) {
        if (heldMidiNotes[static_cast<std::size_t>(note)]) {
            currentRootMidiNote = note;
            droneGateOpen = true;
            return;
        }
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout AstralReverberationsAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> parameters;
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("mix", "Mix", range(0.0f, 1.0f), 0.35f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("delay_level", "Delay", range(0.0f, 1.0f), 0.45f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("reverb_level", "Reverb", range(0.0f, 1.0f), 0.55f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("delay_time", "Delay Time", range(40.0f, 2000.0f, 0.0f, 0.45f), 420.0f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("feedback", "Feedback", range(0.0f, 0.95f), 0.42f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("space", "Space", range(0.0f, 1.0f), 0.55f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("tone", "Tone", range(0.0f, 1.0f), 0.55f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("mod_depth", "Mod Depth", range(0.0f, 1.0f), 0.35f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("wow_flutter", "Wow Flutter", range(0.0f, 1.0f), 0.25f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("drive", "Drive", range(0.0f, 1.0f), 0.15f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("astro_amount", "Astro Amount", range(0.0f, 1.0f), 0.5f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("astro_speed", "Astro Speed", range(0.1f, 8.0f, 0.0f, 0.5f), 1.0f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("drone_level", "Drone Level", range(0.0f, 1.0f), 0.45f));
    parameters.push_back(std::make_unique<juce::AudioParameterBool>("drone_hold", "Drone On", false));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("capture_level", "Capture Level", range(0.0f, 1.0f), 0.35f));
    parameters.push_back(std::make_unique<juce::AudioParameterBool>("capture_enable", "Capture Enable", false));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("root_offset", "Root Offset", range(-24.0f, 24.0f, 1.0f), 0.0f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("harmonic_spread", "Harmonic Spread", range(0.0f, 1.0f), 0.65f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("aspect_depth", "Aspect Depth", range(0.0f, 1.0f), 0.5f));
    parameters.push_back(std::make_unique<juce::AudioParameterBool>("freeze", "Freeze", false));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("output_gain", "Output", range(0.0f, 2.0f), 1.0f));
    return {parameters.begin(), parameters.end()};
}

} // namespace astral::plugin

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new astral::plugin::AstralReverberationsAudioProcessor();
}
