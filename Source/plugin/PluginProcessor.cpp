#include "plugin/PluginProcessor.h"

#include "plugin/MacroMapping.h"
#include "plugin/MidiMapping.h"
#include "plugin/PluginEditor.h"

#include <cmath>

namespace astral::plugin {
namespace {

constexpr auto kAstroModeProperty = "astroMode";
constexpr auto kManualJulianDayProperty = "manualJulianDay";
constexpr int kSignEffectCount = 12;
constexpr std::array<int, static_cast<std::size_t>(astro::PlanetId::Count)> kEuclideanSteps{{8, 12, 8, 16, 16, 16, 13, 15, 17, 19}};
constexpr std::array<int, static_cast<std::size_t>(astro::PlanetId::Count)> kEuclideanPulses{{5, 7, 3, 5, 7, 9, 5, 7, 9, 11}};

// Converts string literals into JUCE parameter ids at call sites.
juce::String parameterId(const char* id)
{
    return juce::String(id);
}

// Reads a float parameter from APVTS, returning zero for missing legacy state.
float getFloatParameter(const juce::AudioProcessorValueTreeState& state, const char* id)
{
    if (const auto* value = state.getRawParameterValue(parameterId(id))) {
        return value->load();
    }
    return 0.0f;
}

// Converts a MIDI note number into equal-tempered frequency in Hz.
float midiNoteToFrequency(int note)
{
    return 440.0f * std::pow(2.0f, (static_cast<float>(note) - 69.0f) / 12.0f);
}

float midiNoteToFrequency(float note)
{
    return 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
}

float centsToRatio(float cents)
{
    return std::pow(2.0f, cents / 1200.0f);
}

// Reads an integer-like parameter, preserving a caller-provided fallback for old states.
int getIntParameter(const juce::AudioProcessorValueTreeState& state, const char* id, int fallback)
{
    if (const auto* value = state.getRawParameterValue(parameterId(id))) {
        return static_cast<int>(std::round(value->load()));
    }
    return fallback;
}

// Shortens APVTS range construction while keeping skew/step explicit.
juce::NormalisableRange<float> range(float start, float end, float interval = 0.0f, float skew = 1.0f)
{
    return {start, end, interval, skew};
}

bool euclideanHit(int step, int pulses, int steps)
{
    if (steps <= 0 || pulses <= 0) {
        return false;
    }
    pulses = std::clamp(pulses, 0, steps);
    const int wrappedStep = ((step % steps) + steps) % steps;
    return ((wrappedStep + 1) * pulses) / steps != (wrappedStep * pulses) / steps;
}

MacroControls readMacroControls(const juce::AudioProcessorValueTreeState& state)
{
    MacroControls macros;
    macros.substance = getFloatParameter(state, "macro_substance");
    macros.mneme = getFloatParameter(state, "macro_mneme");
    macros.choir = getFloatParameter(state, "macro_choir");
    macros.ephemeris = getFloatParameter(state, "macro_ephemeris");
    macros.fate = getFloatParameter(state, "macro_fate");
    macros.void_ = getFloatParameter(state, "macro_void");
    macros.pulse = getFloatParameter(state, "macro_pulse");
    macros.root = getFloatParameter(state, "macro_root");
    return macros;
}

} // namespace

AstralReverberationsAudioProcessor::AstralReverberationsAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameterState(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    currentSnapshot = astrologyEngine.snapshotForJulianDay(simulatedJulianDay);
    currentDroneFrame = astroMapper.mapDroneSnapshot(currentSnapshot);
    for (std::size_t index = 0; index < planetLongitudeOffsets.size(); ++index) {
        planetLongitudeOffsets[index].store(0.0f, std::memory_order_relaxed);
        planetLongitudeOffsetEnabled[index].store(false, std::memory_order_relaxed);
        manualOrbitTailGates[index].store(false, std::memory_order_relaxed);
        planetMutes[index].store(false, std::memory_order_relaxed);
        orbitTailLevels[index].store(0.0f, std::memory_order_relaxed);
    }
    for (std::size_t index = 0; index < signEffectEnabled.size(); ++index) {
        signEffectEnabled[index].store(true, std::memory_order_relaxed);
        signEffectAssignments[index].store(static_cast<int>(index), std::memory_order_relaxed);
    }
    parameterState.state.setProperty(kAstroModeProperty, static_cast<int>(AstroMode::Now), nullptr);
    parameterState.state.setProperty(kManualJulianDayProperty, manualJulianDay, nullptr);
}

void AstralReverberationsAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    euclideanStepAccumulator = 0.0;
    euclideanSteps.fill(0);
    effect.prepare(sampleRate, samplesPerBlock);
    drone.prepare(sampleRate, samplesPerBlock);
    processingBuffer.setSize(2, samplesPerBlock, false, false, true);
    inputCopyBuffer.setSize(2, samplesPerBlock, false, false, true);
}

void AstralReverberationsAudioProcessor::releaseResources()
{
    effect.reset();
    drone.reset();
    heldDroneGateNotes.fill(false);
    midiSustainPedalDown = false;
    midiPitchBendSemitones = 0.0f;
    droneGateOpen = false;
}

bool AstralReverberationsAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& input = layouts.getMainInputChannelSet();
    const auto& output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::stereo()
        && (input == juce::AudioChannelSet::mono() || input == juce::AudioChannelSet::stereo());
}

void AstralReverberationsAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    processMidiInput(midiMessages);

    const auto totalInputChannels = getTotalNumInputChannels();
    const auto totalOutputChannels = getTotalNumOutputChannels();
    for (auto channel = totalInputChannels; channel < totalOutputChannels; ++channel) {
        buffer.clear(channel, 0, buffer.getNumSamples());
    }

    if (buffer.getNumChannels() < 2) {
        buffer.clear();
        return;
    }

    const int numSamples = buffer.getNumSamples();
    if (processingBuffer.getNumSamples() < numSamples || processingBuffer.getNumChannels() < 2) {
        processingBuffer.setSize(2, numSamples, false, false, true);
    }
    if (inputCopyBuffer.getNumSamples() < numSamples || inputCopyBuffer.getNumChannels() < 2) {
        inputCopyBuffer.setSize(2, numSamples, false, false, true);
    }

    inputCopyBuffer.copyFrom(0, 0, buffer, 0, 0, buffer.getNumSamples());
    if (totalInputChannels > 1) {
        inputCopyBuffer.copyFrom(1, 0, buffer, 1, 0, buffer.getNumSamples());
    } else {
        inputCopyBuffer.copyFrom(1, 0, buffer, 0, 0, buffer.getNumSamples());
    }
    processingBuffer.makeCopyOf(inputCopyBuffer);
    float inputPeak = 0.0f;
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
        const float leftInput = std::abs(buffer.getSample(0, sample));
        const float rightInput = totalInputChannels > 1 ? std::abs(buffer.getSample(1, sample)) : leftInput;
        inputPeak = std::max(inputPeak, std::max(leftInput, rightInput));
    }
    const float previousInputLevel = inputLevel.load(std::memory_order_relaxed);
    inputLevel.store(previousInputLevel + (std::clamp(inputPeak, 0.0f, 1.0f) - previousInputLevel) * 0.35f, std::memory_order_relaxed);

    const auto astroFrame = nextAstroFrame(buffer.getNumSamples());
    const auto droneFrame = getCurrentDroneFrameCopy();
    drone.processBlock(
        {processingBuffer.getWritePointer(0), processingBuffer.getWritePointer(1), buffer.getNumSamples()},
        {processingBuffer.getWritePointer(0), processingBuffer.getWritePointer(1), buffer.getNumSamples()},
        readDroneParameters(),
        droneFrame);
    const auto tailLevels = drone.getTailLevels();
    for (std::size_t index = 0; index < tailLevels.size(); ++index) {
        orbitTailLevels[index].store(tailLevels[index], std::memory_order_relaxed);
    }
    processEuclideanPlucks(buffer.getNumSamples(), droneFrame);

    auto effectParameters = readParameters();
    const int pendingTaps = pendingReverbTankTaps.exchange(0, std::memory_order_relaxed);
    if (pendingTaps > 0) {
        effectParameters.reverbTankTapLevel = std::min(1.0f, static_cast<float>(pendingTaps) * 0.62f * effectParameters.pluckLevel);
        effectParameters.reverbTankTapPan = pendingReverbTankTapPan.load(std::memory_order_relaxed);
        effectParameters.reverbTankTapFrequencyHz = pendingReverbTankTapFrequencyHz.load(std::memory_order_relaxed);
        effectParameters.reverbTankTapWet = pendingReverbTankTapWet.load(std::memory_order_relaxed);
        const float planetTone = pendingReverbTankTapTone.load(std::memory_order_relaxed);
        effectParameters.reverbTankTapTone = std::clamp(
            planetTone + (effectParameters.pluckTimbre - 0.5f) * 0.9f,
            0.0f,
            1.0f);
    }
    if (!effectParameters.inputMonitor) {
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
            processingBuffer.setSample(
                0,
                sample,
                processingBuffer.getSample(0, sample) - inputCopyBuffer.getSample(0, sample));
            processingBuffer.setSample(
                1,
                sample,
                processingBuffer.getSample(1, sample) - inputCopyBuffer.getSample(1, sample));
        }
    }

    effect.processBlock(
        {processingBuffer.getWritePointer(0), processingBuffer.getWritePointer(1), buffer.getNumSamples()},
        {processingBuffer.getWritePointer(0), processingBuffer.getWritePointer(1), buffer.getNumSamples()},
        effectParameters,
        astroFrame);

    buffer.copyFrom(0, 0, processingBuffer, 0, 0, buffer.getNumSamples());
    buffer.copyFrom(1, 0, processingBuffer, 1, 0, buffer.getNumSamples());

    float outputPeak = 0.0f;
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
        outputPeak = std::max(outputPeak, std::max(std::abs(buffer.getSample(0, sample)), std::abs(buffer.getSample(1, sample))));
    }
    const float previousOutputLevel = outputLevel.load(std::memory_order_relaxed);
    outputLevel.store(previousOutputLevel + (std::clamp(outputPeak, 0.0f, 1.0f) - previousOutputLevel) * 0.42f, std::memory_order_relaxed);
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
}

double AstralReverberationsAudioProcessor::getManualJulianDay() const
{
    return manualJulianDay;
}

astro::AstroSnapshot AstralReverberationsAudioProcessor::getCurrentSnapshotCopy() const
{
    const juce::ScopedLock lock(astroStateLock);
    return currentSnapshot;
}

astro::AstroDroneFrame AstralReverberationsAudioProcessor::getCurrentDroneFrameCopy() const
{
    const juce::ScopedLock lock(astroStateLock);
    return currentDroneFrame;
}

void AstralReverberationsAudioProcessor::setManualJulianDay(double julianDay)
{
    manualJulianDay = julianDay;
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

bool AstralReverberationsAudioProcessor::isManualRootLocked() const
{
    return getFloatParameter(parameterState, "root_lock") >= 0.5f;
}

int AstralReverberationsAudioProcessor::getDroneRootNote() const
{
    if (isManualRootLocked() || !droneGateOpen) {
        return getIntParameter(parameterState, "root_note", 45);
    }
    return currentMidiRootNote;
}

void AstralReverberationsAudioProcessor::setManualOrbitTailGate(astro::PlanetId planet, bool active)
{
    manualOrbitTailGates[static_cast<std::size_t>(planet)].store(active, std::memory_order_relaxed);
}

void AstralReverberationsAudioProcessor::tapOrbitReverbTank(astro::PlanetId planet, float wetAmount)
{
    const auto index = static_cast<std::size_t>(planet);
    if (index >= planetMutes.size() || isPlanetMuted(planet)) {
        return;
    }

    {
        const juce::ScopedLock lock(astroStateLock);
        if (index < currentDroneFrame.layers.size()) {
            queueReverbTankTap(index, currentDroneFrame.layers[index], wetAmount);
        }
    }
}

bool AstralReverberationsAudioProcessor::isManualOrbitTailActive(astro::PlanetId planet) const
{
    return !isPlanetMuted(planet) && manualOrbitTailGates[static_cast<std::size_t>(planet)].load(std::memory_order_relaxed);
}

void AstralReverberationsAudioProcessor::queueReverbTankTap(std::size_t planetIndex, const astro::AstroHarmonicLayer& layer, float wetAmount)
{
    const auto resolved = resolveMacroParameters(readMacroControls(parameterState));
    float frequencyHz = getDroneRootFrequencyHz();
    const float harmonicSpread = std::clamp(resolved.harmonicSpread, 0.0f, 1.0f);
    const float ratio = std::clamp(1.0f + (layer.ratio - 1.0f) * harmonicSpread, 0.125f, 12.0f);
    const float octaveOffset = resolved.pluckOctave;
    frequencyHz = std::clamp(frequencyHz * ratio * centsToRatio(layer.detuneCents) * std::pow(2.0f, octaveOffset), 35.0f, 2400.0f);

    pendingReverbTankTapPan.store(std::clamp(layer.pan, -1.0f, 1.0f), std::memory_order_relaxed);
    pendingReverbTankTapFrequencyHz.store(frequencyHz, std::memory_order_relaxed);
    pendingReverbTankTapWet.store(std::clamp(wetAmount, 0.0f, 1.0f), std::memory_order_relaxed);
    pendingReverbTankTapTone.store(
        std::clamp(static_cast<float>(planetIndex) / static_cast<float>(std::max<std::size_t>(1, planetMutes.size() - 1)), 0.0f, 1.0f),
        std::memory_order_relaxed);
    pendingReverbTankTaps.fetch_add(1, std::memory_order_relaxed);
}

void AstralReverberationsAudioProcessor::processEuclideanPlucks(int samplesInBlock, const astro::AstroDroneFrame& droneFrame)
{
    if (getFloatParameter(parameterState, "euclid_enable") < 0.5f) {
        euclideanStepAccumulator = 0.0;
        return;
    }

    const float rate = std::clamp(getFloatParameter(parameterState, "euclid_rate"), 0.25f, 16.0f);
    euclideanStepAccumulator += static_cast<double>(std::max(0, samplesInBlock)) * static_cast<double>(rate) / std::max(1.0, currentSampleRate);

    int stepsToProcess = 0;
    while (euclideanStepAccumulator >= 1.0 && stepsToProcess < 8) {
        euclideanStepAccumulator -= 1.0;
        ++stepsToProcess;
    }

    while (stepsToProcess-- > 0) {
        for (std::size_t index = 0; index < droneFrame.layers.size(); ++index) {
            if (planetMutes[index].load(std::memory_order_relaxed)) {
                continue;
            }

            const auto& layer = droneFrame.layers[index];
            const int steps = kEuclideanSteps[index];
            const int pulses = std::clamp(kEuclideanPulses[index] + static_cast<int>(std::round(layer.amplitude * 2.0f)), 1, steps - 1);
            const int rotation = static_cast<int>(std::round(layer.tapePosition * static_cast<float>(steps)));
            if (euclideanHit(euclideanSteps[index] + rotation, pulses, steps)) {
                queueReverbTankTap(index, layer, getFloatParameter(parameterState, "euclid_wet"));
            }
            euclideanSteps[index] = (euclideanSteps[index] + 1) % steps;
        }
    }
}

float AstralReverberationsAudioProcessor::getOrbitTailLevel(astro::PlanetId planet) const
{
    if (isPlanetMuted(planet)) {
        return 0.0f;
    }
    return orbitTailLevels[static_cast<std::size_t>(planet)].load(std::memory_order_relaxed);
}

void AstralReverberationsAudioProcessor::togglePlanetMute(astro::PlanetId planet)
{
    const auto index = static_cast<std::size_t>(planet);
    const bool next = !planetMutes[index].load(std::memory_order_relaxed);
    planetMutes[index].store(next, std::memory_order_relaxed);
    if (next) {
        manualOrbitTailGates[index].store(false, std::memory_order_relaxed);
        orbitTailLevels[index].store(0.0f, std::memory_order_relaxed);
    }
}

bool AstralReverberationsAudioProcessor::isPlanetMuted(astro::PlanetId planet) const
{
    return planetMutes[static_cast<std::size_t>(planet)].load(std::memory_order_relaxed);
}

float AstralReverberationsAudioProcessor::getInputLevel() const
{
    return inputLevel.load(std::memory_order_relaxed);
}

float AstralReverberationsAudioProcessor::getOutputLevel() const
{
    return outputLevel.load(std::memory_order_relaxed);
}

bool AstralReverberationsAudioProcessor::isSignEffectEnabled(astro::ZodiacSign sign) const
{
    return signEffectEnabled[static_cast<std::size_t>(sign)].load(std::memory_order_relaxed);
}

void AstralReverberationsAudioProcessor::toggleSignEffect(astro::ZodiacSign sign)
{
    const auto index = static_cast<std::size_t>(sign);
    signEffectEnabled[index].store(!signEffectEnabled[index].load(std::memory_order_relaxed), std::memory_order_relaxed);
}

void AstralReverberationsAudioProcessor::cycleSignEffect(astro::ZodiacSign sign)
{
    const auto index = static_cast<std::size_t>(sign);
    const int current = signEffectAssignments[index].load(std::memory_order_relaxed);
    signEffectAssignments[index].store((current + 1) % kSignEffectCount, std::memory_order_relaxed);
}

int AstralReverberationsAudioProcessor::getSignEffectAssignment(astro::ZodiacSign sign) const
{
    return signEffectAssignments[static_cast<std::size_t>(sign)].load(std::memory_order_relaxed);
}

void AstralReverberationsAudioProcessor::setManualPlanetLongitude(astro::PlanetId planet, float longitudeDegrees)
{
    const auto index = static_cast<std::size_t>(planet);
    const juce::ScopedLock lock(astroStateLock);
    float baseLongitude = currentSnapshot.planets[index].longitudeDegrees;
    if (planetLongitudeOffsetEnabled[index].load(std::memory_order_relaxed)) {
        baseLongitude = astro::AstrologyEngine::normalizeDegrees(
            baseLongitude - planetLongitudeOffsets[index].load(std::memory_order_relaxed));
    }
    const float offset = astro::AstrologyEngine::normalizeDegrees(longitudeDegrees - baseLongitude + 180.0f) - 180.0f;
    planetLongitudeOffsets[index].store(offset, std::memory_order_relaxed);
    planetLongitudeOffsetEnabled[index].store(true, std::memory_order_relaxed);
    currentSnapshot.planets[index].longitudeDegrees = astro::AstrologyEngine::normalizeDegrees(baseLongitude + offset);
    currentSnapshot.aspects = astrologyEngine.detectAspects(currentSnapshot);
    currentDroneFrame = astroMapper.mapDroneSnapshot(currentSnapshot);
}

void AstralReverberationsAudioProcessor::resetPlanetLongitude(astro::PlanetId planet)
{
    const auto index = static_cast<std::size_t>(planet);
    planetLongitudeOffsets[index].store(0.0f, std::memory_order_relaxed);
    planetLongitudeOffsetEnabled[index].store(false, std::memory_order_relaxed);
}

void AstralReverberationsAudioProcessor::resetPlanetLongitudes()
{
    for (std::size_t index = 0; index < planetLongitudeOffsets.size(); ++index) {
        planetLongitudeOffsets[index].store(0.0f, std::memory_order_relaxed);
        planetLongitudeOffsetEnabled[index].store(false, std::memory_order_relaxed);
    }
}

bool AstralReverberationsAudioProcessor::hasPlanetLongitudeOffset(astro::PlanetId planet) const
{
    return planetLongitudeOffsetEnabled[static_cast<std::size_t>(planet)].load(std::memory_order_relaxed);
}

dsp::AstralParameters AstralReverberationsAudioProcessor::readParameters() const
{
    const auto resolved = resolveMacroParameters(readMacroControls(parameterState));
    dsp::AstralParameters parameters;
    parameters.mix = resolved.mix;
    parameters.delayLevel = resolved.delayLevel;
    parameters.reverbLevel = resolved.reverbLevel;
    parameters.delayTimeMs = resolved.delayTimeMs;
    parameters.feedback = resolved.feedback;
    parameters.space = resolved.space;
    parameters.tone = resolved.tone;
    parameters.modDepth = resolved.modDepth;
    parameters.wowFlutter = resolved.wowFlutter;
    parameters.drive = resolved.drive;
    parameters.astroAmount = resolved.astroAmount;
    parameters.astroSpeed = getFloatParameter(parameterState, "astro_speed");
    parameters.pluckLevel = resolved.pluckLevel;
    parameters.pluckTimbre = resolved.pluckTimbre;
    parameters.freeze = getFloatParameter(parameterState, "freeze") >= 0.5f;
    parameters.inputMonitor = getFloatParameter(parameterState, "input_monitor") >= 0.5f;
    parameters.outputGain = std::clamp(getFloatParameter(parameterState, "output_gain"), 0.0f, 2.0f);
    return parameters;
}

dsp::PlanetaryDroneParameters AstralReverberationsAudioProcessor::readDroneParameters() const
{
    const auto resolved = resolveMacroParameters(readMacroControls(parameterState));
    dsp::PlanetaryDroneParameters parameters;
    parameters.droneLevel = resolved.droneLevel;
    parameters.captureLevel = resolved.captureLevel;
    parameters.harmonicSpread = resolved.harmonicSpread;
    parameters.aspectDepth = resolved.aspectDepth;
    parameters.rootFrequencyHz = getDroneRootFrequencyHz();
    parameters.gate = isDroneGateOpen();
    bool anyMidiTailHeld = false;
    for (int tailNote = kMidiTailNoteFirst; tailNote <= kMidiTailNoteLast; ++tailNote) {
        if (heldDroneGateNotes[static_cast<std::size_t>(tailNote)]) {
            anyMidiTailHeld = true;
            break;
        }
    }
    parameters.captureEnabled = getFloatParameter(parameterState, "capture_enable") >= 0.5f || anyMidiTailHeld;
    for (std::size_t index = 0; index < parameters.manualLayerGates.size(); ++index) {
        const bool muted = planetMutes[index].load(std::memory_order_relaxed);
        parameters.manualLayerGates[index] = !muted && manualOrbitTailGates[index].load(std::memory_order_relaxed);
        parameters.mutedLayers[index] = muted;
    }
    return parameters;
}

astro::AstroModulationFrame AstralReverberationsAudioProcessor::nextAstroFrame(int samplesInBlock)
{
    astro::AstroSnapshot nextSnapshot;
    switch (getAstroMode()) {
        case AstroMode::Now:
            nextSnapshot = astrologyEngine.snapshotForUnixSeconds(astro::AstrologyEngine::unixSecondsFromSystemClock());
            break;
        case AstroMode::ManualDate:
            nextSnapshot = astrologyEngine.snapshotForJulianDay(manualJulianDay);
            break;
        case AstroMode::SimulatedOrbit: {
            const double blockSeconds = static_cast<double>(samplesInBlock) / currentSampleRate;
            const double daysPerSecond = 0.01 * std::max(0.1f, getFloatParameter(parameterState, "astro_speed"));
            simulatedJulianDay += blockSeconds * daysPerSecond;
            nextSnapshot = astrologyEngine.snapshotForJulianDay(simulatedJulianDay);
            break;
        }
    }

    applyPlanetLongitudeOffsets(nextSnapshot);
    const auto nextDroneFrame = astroMapper.mapDroneSnapshot(nextSnapshot);
    auto modulationFrame = astroMapper.mapSnapshot(nextSnapshot);
    for (std::size_t index = 0; index < modulationFrame.delayTaps.size(); ++index) {
        if (planetMutes[index].load(std::memory_order_relaxed)) {
            modulationFrame.delayTaps[index].level = 0.0f;
        }
    }
    std::array<float, 12> remappedSignEffects{};
    for (std::size_t index = 0; index < modulationFrame.signEffects.size(); ++index) {
        if (!signEffectEnabled[index].load(std::memory_order_relaxed)) {
            continue;
        }
        const auto assignment = static_cast<std::size_t>(signEffectAssignments[index].load(std::memory_order_relaxed) % kSignEffectCount);
        remappedSignEffects[assignment] = std::max(remappedSignEffects[assignment], modulationFrame.signEffects[index]);
    }
    modulationFrame.signEffects = remappedSignEffects;

    {
        const juce::ScopedLock lock(astroStateLock);
        currentSnapshot = nextSnapshot;
        currentDroneFrame = nextDroneFrame;
    }

    return modulationFrame;
}

float AstralReverberationsAudioProcessor::getDroneRootFrequencyHz() const
{
    const auto resolved = resolveMacroParameters(readMacroControls(parameterState));
    const float rootNote = static_cast<float>(getDroneRootNote()) + resolved.rootOffsetSemitones + midiPitchBendSemitones;
    return midiNoteToFrequency(rootNote);
}

void AstralReverberationsAudioProcessor::setParameterFromMidiCc(const char* parameterId, int ccValue)
{
    if (auto* parameter = parameterState.getParameter(parameterId)) {
        parameter->setValueNotifyingHost(parameter->convertTo0to1(midiCcToNormalized(ccValue)));
    }
}

void AstralReverberationsAudioProcessor::setBoolParameterFromMidiCc(const char* parameterId, int ccValue)
{
    if (auto* parameter = parameterState.getParameter(parameterId)) {
        parameter->setValueNotifyingHost(ccValue >= 64 ? 1.0f : 0.0f);
    }
}

void AstralReverberationsAudioProcessor::refreshMidiVoiceState()
{
    droneGateOpen = midiSustainPedalDown;
    int lowestHeldNote = -1;

    for (int note = 0; note < static_cast<int>(heldDroneGateNotes.size()); ++note) {
        if (!heldDroneGateNotes[static_cast<std::size_t>(note)]) {
            continue;
        }

        droneGateOpen = true;
        if (lowestHeldNote < 0 || note < lowestHeldNote) {
            lowestHeldNote = note;
        }
    }

    if (lowestHeldNote >= 0) {
        currentMidiRootNote = lowestHeldNote;
    }
}

void AstralReverberationsAudioProcessor::processMidiInput(const juce::MidiBuffer& midiMessages)
{
    for (const auto metadata : midiMessages) {
        const auto message = metadata.getMessage();
        const int note = message.getNoteNumber();

        if (message.isNoteOn()) {
            if (const auto planet = planetForPluckMidiNote(note)) {
                if (!isPlanetMuted(*planet)) {
                    tapOrbitReverbTank(*planet, midiVelocityToWet(message.getVelocity()));
                }
                continue;
            }

            if (const auto planet = planetForTailMidiNote(note)) {
                if (!isPlanetMuted(*planet)) {
                    setManualOrbitTailGate(*planet, true);
                }
            }

            heldDroneGateNotes[static_cast<std::size_t>(note)] = true;
        } else if (message.isNoteOff()) {
            if (const auto planet = planetForTailMidiNote(note)) {
                setManualOrbitTailGate(*planet, false);
            }
            heldDroneGateNotes[static_cast<std::size_t>(note)] = false;
        } else if (message.isAllNotesOff() || message.isAllSoundOff()) {
            heldDroneGateNotes.fill(false);
            for (std::size_t index = 0; index < manualOrbitTailGates.size(); ++index) {
                manualOrbitTailGates[index].store(false, std::memory_order_relaxed);
            }
        } else if (message.isPitchWheel()) {
            midiPitchBendSemitones = midiPitchWheelToSemitones(message.getPitchWheelValue());
        } else if (message.isController()) {
            const int cc = message.getControllerNumber();
            const int value = message.getControllerValue();

            switch (cc) {
                case kMidiCcMacroSubstance:
                    setParameterFromMidiCc("macro_substance", value);
                    break;
                case kMidiCcMacroMneme:
                    setParameterFromMidiCc("macro_mneme", value);
                    break;
                case kMidiCcMacroChoir:
                    setParameterFromMidiCc("macro_choir", value);
                    break;
                case kMidiCcMacroEphemeris:
                    setParameterFromMidiCc("macro_ephemeris", value);
                    break;
                case kMidiCcMacroFate:
                    setParameterFromMidiCc("macro_fate", value);
                    break;
                case kMidiCcMacroVoid:
                    setParameterFromMidiCc("macro_void", value);
                    break;
                case kMidiCcMacroPulse:
                    setParameterFromMidiCc("macro_pulse", value);
                    break;
                case kMidiCcMacroRoot:
                    setParameterFromMidiCc("macro_root", value);
                    break;
                case kMidiCcEuclidRate:
                    setParameterFromMidiCc("euclid_rate", value);
                    break;
                case kMidiCcEuclidWet:
                    setParameterFromMidiCc("euclid_wet", value);
                    break;
                case kMidiCcDroneHold:
                    setBoolParameterFromMidiCc("drone_hold", value);
                    break;
                case kMidiCcCaptureEnable:
                    setBoolParameterFromMidiCc("capture_enable", value);
                    break;
                case kMidiCcFreeze:
                    setBoolParameterFromMidiCc("freeze", value);
                    break;
                case kMidiCcEuclidEnable:
                    setBoolParameterFromMidiCc("euclid_enable", value);
                    break;
                case kMidiCcRootLock:
                    setBoolParameterFromMidiCc("root_lock", value);
                    break;
                case kMidiCcSustainPedal:
                    midiSustainPedalDown = value >= 64;
                    break;
                default:
                    break;
            }
        }
    }

    refreshMidiVoiceState();
}

void AstralReverberationsAudioProcessor::applyPlanetLongitudeOffsets(astro::AstroSnapshot& snapshot) const
{
    bool hasOffset = false;
    for (std::size_t index = 0; index < snapshot.planets.size(); ++index) {
        if (!planetLongitudeOffsetEnabled[index].load(std::memory_order_relaxed)) {
            continue;
        }

        hasOffset = true;
        const float offset = planetLongitudeOffsets[index].load(std::memory_order_relaxed);
        snapshot.planets[index].longitudeDegrees = astro::AstrologyEngine::normalizeDegrees(
            snapshot.planets[index].longitudeDegrees + offset);
    }

    if (hasOffset) {
        snapshot.aspects = astrologyEngine.detectAspects(snapshot);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout AstralReverberationsAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> parameters;
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("macro_substance", "Substance", range(0.0f, 1.0f), 0.42f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("macro_mneme", "Mneme", range(0.0f, 1.0f), 0.38f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("macro_choir", "Choir", range(0.0f, 1.0f), 0.52f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("macro_ephemeris", "Ephemeris", range(0.0f, 1.0f), 0.5f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("macro_fate", "Fate", range(0.0f, 1.0f), 0.48f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("macro_void", "Void", range(0.0f, 1.0f), 0.54f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("macro_pulse", "Pulse", range(0.0f, 1.0f), 0.58f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("macro_root", "Root Drift", range(0.0f, 1.0f), 0.5f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("mix", "Wet Mix", range(0.0f, 1.0f), 0.35f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("delay_level", "Tape Echo Level", range(0.0f, 1.0f), 0.45f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("reverb_level", "Space Reverb Level", range(0.0f, 1.0f), 0.55f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("delay_time", "Tape Echo Time", range(40.0f, 2000.0f, 0.0f, 0.45f), 420.0f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("feedback", "Tape Regeneration", range(0.0f, 0.95f), 0.42f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("space", "Reverb Tank Size", range(0.0f, 1.0f), 0.55f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("tone", "Tape/Reverb Tone", range(0.0f, 1.0f), 0.55f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("pluck_timbre", "Pluck Timbre", range(0.0f, 1.0f), 0.5f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("pluck_level", "Pluck Level", range(0.0f, 1.5f), 1.0f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("pluck_octave", "Pluck Octave", range(-2.0f, 2.0f, 1.0f), 0.0f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("mod_depth", "Orbit Mod Depth", range(0.0f, 1.0f), 0.35f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("wow_flutter", "Tape Wow/Flutter", range(0.0f, 1.0f), 0.25f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("drive", "Tape Drive", range(0.0f, 1.0f), 0.15f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("astro_amount", "Astro Mod Amount", range(0.0f, 1.0f), 0.5f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("astro_speed", "Simulated Orbit Speed", range(0.1f, 8.0f, 0.0f, 0.5f), 1.0f));
    parameters.push_back(std::make_unique<juce::AudioParameterBool>("euclid_enable", "Euclidean Plucks", false));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("euclid_rate", "Euclidean Rate", range(0.25f, 16.0f, 0.0f, 0.55f), 4.0f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("euclid_wet", "Euclidean Wet", range(0.0f, 1.0f), 1.0f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("drone_level", "Drone Level", range(0.0f, 1.0f), 0.45f));
    parameters.push_back(std::make_unique<juce::AudioParameterBool>("drone_hold", "Drone On", false));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("capture_level", "Capture Level", range(0.0f, 1.0f), 0.35f));
    parameters.push_back(std::make_unique<juce::AudioParameterBool>("capture_enable", "Capture Enable", false));
    parameters.push_back(std::make_unique<juce::AudioParameterInt>("root_note", "Root Note", 24, 84, 45));
    parameters.push_back(std::make_unique<juce::AudioParameterBool>("root_lock", "Manual Root", true));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("root_offset", "Root Offset", range(-24.0f, 24.0f, 1.0f), 0.0f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("harmonic_spread", "Harmonic Spread", range(0.0f, 1.0f), 0.65f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("aspect_depth", "Aspect Depth", range(0.0f, 1.0f), 0.5f));
    parameters.push_back(std::make_unique<juce::AudioParameterBool>("freeze", "Freeze", false));
    parameters.push_back(std::make_unique<juce::AudioParameterBool>("input_monitor", "Input Monitor", false));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>("output_gain", "Output", range(0.0f, 2.0f), 1.0f));
    return {parameters.begin(), parameters.end()};
}

} // namespace astral::plugin

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new astral::plugin::AstralReverberationsAudioProcessor();
}
