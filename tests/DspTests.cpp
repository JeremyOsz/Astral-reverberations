#include "TestSupport.h"

#include "dsp/AstralReverbDelay.h"
#include "dsp/PlanetaryDrone.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

using namespace astral::dsp;
using astral::astro::AstroModulationFrame;

namespace {

bool allFinite(const std::vector<float>& values)
{
    return std::all_of(values.begin(), values.end(), [](float value) { return std::isfinite(value); });
}

float peak(const std::vector<float>& values)
{
    float result = 0.0f;
    for (float value : values) {
        result = std::max(result, std::abs(value));
    }
    return result;
}

float absoluteDifferenceSum(const std::vector<float>& a, const std::vector<float>& b)
{
    float sum = 0.0f;
    const auto count = std::min(a.size(), b.size());
    for (std::size_t index = 0; index < count; ++index) {
        sum += std::abs(a[index] - b[index]);
    }
    return sum;
}

int zeroCrossings(const std::vector<float>& values)
{
    int crossings = 0;
    float previous = values.empty() ? 0.0f : values.front();
    for (float value : values) {
        if ((previous < 0.0f && value >= 0.0f) || (previous >= 0.0f && value < 0.0f)) {
            ++crossings;
        }
        previous = value;
    }
    return crossings;
}

} // namespace

void runDspTests()
{
    AstralReverbDelay processor;
    processor.prepare(48000.0, 512);

    constexpr int sampleCount = 48000;
    std::vector<float> inputLeft(sampleCount, 0.0f);
    std::vector<float> inputRight(sampleCount, 0.0f);
    std::vector<float> outputLeft(sampleCount, 0.0f);
    std::vector<float> outputRight(sampleCount, 0.0f);
    inputLeft[0] = 1.0f;
    inputRight[0] = 1.0f;

    AstralParameters parameters;
    parameters.mix = 1.0f;
    parameters.delayLevel = 0.7f;
    parameters.reverbLevel = 0.6f;
    parameters.delayTimeMs = 120.0f;
    parameters.feedback = 0.55f;
    parameters.space = 0.7f;
    parameters.outputGain = 0.8f;

    AstroModulationFrame frame;
    frame.delayDrift = 0.4f;
    frame.feedbackBloom = 0.3f;
    frame.reverbSize = 0.6f;
    frame.damping = 0.4f;
    frame.shimmer = 0.2f;
    frame.stereoMotion = 0.8f;

    processor.processBlock(
        {inputLeft.data(), inputRight.data(), sampleCount},
        {outputLeft.data(), outputRight.data(), sampleCount},
        parameters,
        frame);

    require(allFinite(outputLeft), "left output remains finite");
    require(allFinite(outputRight), "right output remains finite");
    require(peak(outputLeft) < 4.0f, "left output is bounded");
    require(peak(outputRight) < 4.0f, "right output is bounded");
    require(peak(outputLeft) > 0.001f, "impulse creates an audible left tail");
    require(peak(outputRight) > 0.001f, "impulse creates an audible right tail");

    const float latePeak = peak(std::vector<float>(outputLeft.end() - 4096, outputLeft.end()));
    require(latePeak < peak(outputLeft), "non-freeze tail decays below initial peak");

    processor.reset();
    std::fill(inputLeft.begin(), inputLeft.end(), 0.0f);
    std::fill(inputRight.begin(), inputRight.end(), 0.0f);
    std::fill(outputLeft.begin(), outputLeft.end(), 0.0f);
    std::fill(outputRight.begin(), outputRight.end(), 0.0f);

    processor.processBlock(
        {inputLeft.data(), inputRight.data(), sampleCount},
        {outputLeft.data(), outputRight.data(), sampleCount},
        parameters,
        frame);

    require(peak(outputLeft) < 0.000001f, "reset clears left delay state");
    require(peak(outputRight) < 0.000001f, "reset clears right delay state");

    AstralReverbDelay monitorDelay;
    monitorDelay.prepare(48000.0, 512);
    std::fill(inputLeft.begin(), inputLeft.end(), 0.0f);
    std::fill(inputRight.begin(), inputRight.end(), 0.0f);
    std::fill(outputLeft.begin(), outputLeft.end(), 0.0f);
    std::fill(outputRight.begin(), outputRight.end(), 0.0f);
    inputLeft[0] = 0.5f;
    inputRight[0] = -0.5f;
    AstralParameters monitorParameters;
    monitorParameters.mix = 1.0f;
    monitorParameters.delayLevel = 0.0f;
    monitorParameters.reverbLevel = 0.0f;
    monitorParameters.feedback = 0.0f;
    monitorParameters.inputMonitor = true;
    monitorDelay.processBlock(
        {inputLeft.data(), inputRight.data(), 128},
        {outputLeft.data(), outputRight.data(), 128},
        monitorParameters,
        frame);
    requireNear(outputLeft[0], 0.5f, 0.0001f, "input monitor passes live left input at full wet mix");
    requireNear(outputRight[0], -0.5f, 0.0001f, "input monitor passes live right input at full wet mix");

    AstralReverbDelay silentDryDelay;
    silentDryDelay.prepare(48000.0, 512);
    std::fill(inputLeft.begin(), inputLeft.end(), 0.0f);
    std::fill(inputRight.begin(), inputRight.end(), 0.0f);
    std::fill(outputLeft.begin(), outputLeft.end(), 0.0f);
    std::fill(outputRight.begin(), outputRight.end(), 0.0f);
    inputLeft[0] = 0.5f;
    inputRight[0] = -0.5f;
    AstralParameters silentDryParameters;
    silentDryParameters.mix = 0.35f;
    silentDryParameters.delayLevel = 0.0f;
    silentDryParameters.reverbLevel = 0.0f;
    silentDryParameters.feedback = 0.0f;
    silentDryParameters.inputMonitor = false;
    silentDryDelay.processBlock(
        {inputLeft.data(), inputRight.data(), 128},
        {outputLeft.data(), outputRight.data(), 128},
        silentDryParameters,
        frame);
    requireNear(outputLeft[0], 0.0f, 0.0001f, "input monitor off blocks immediate dry passthrough");
    requireNear(outputRight[0], 0.0f, 0.0001f, "input monitor off blocks immediate dry passthrough");

    AstralReverbDelay tankTapDelay;
    tankTapDelay.prepare(48000.0, 512);
    std::fill(inputLeft.begin(), inputLeft.end(), 0.0f);
    std::fill(inputRight.begin(), inputRight.end(), 0.0f);
    std::fill(outputLeft.begin(), outputLeft.end(), 0.0f);
    std::fill(outputRight.begin(), outputRight.end(), 0.0f);
    AstralParameters tankTapParameters;
    tankTapParameters.mix = 1.0f;
    tankTapParameters.delayLevel = 0.0f;
    tankTapParameters.reverbLevel = 1.0f;
    tankTapParameters.feedback = 0.0f;
    tankTapParameters.space = 0.75f;
    tankTapParameters.reverbTankTapLevel = 1.0f;
    tankTapParameters.reverbTankTapPan = -0.7f;
    tankTapParameters.reverbTankTapTone = 0.6f;
    tankTapDelay.processBlock(
        {inputLeft.data(), inputRight.data(), sampleCount},
        {outputLeft.data(), outputRight.data(), sampleCount},
        tankTapParameters,
        frame);
    require(peak(outputLeft) > 0.001f, "manual tank tap excites the left reverb tank without input");
    require(peak(outputRight) > 0.001f, "manual tank tap excites the right reverb tank without input");
    require(peak(outputLeft) < 0.6f, "manual tank tap avoids a clipped impulse spike");

    AstralReverbDelay baselineDelay;
    AstralReverbDelay nodeDelay;
    baselineDelay.prepare(48000.0, 512);
    nodeDelay.prepare(48000.0, 512);
    std::fill(inputLeft.begin(), inputLeft.end(), 0.0f);
    std::fill(inputRight.begin(), inputRight.end(), 0.0f);
    std::vector<float> baselineLeft(sampleCount, 0.0f);
    std::vector<float> baselineRight(sampleCount, 0.0f);
    std::vector<float> nodeLeft(sampleCount, 0.0f);
    std::vector<float> nodeRight(sampleCount, 0.0f);
    inputLeft[0] = 1.0f;
    inputRight[0] = 1.0f;

    AstralParameters nodeParameters = parameters;
    nodeParameters.modDepth = 1.0f;
    nodeParameters.astroAmount = 1.0f;
    AstroModulationFrame baselineFrame = frame;
    baselineFrame.delayTapDensity = 0.0f;
    for (auto& tap : baselineFrame.delayTaps) {
        tap.level = 0.0f;
    }
    AstroModulationFrame nodeFrame = baselineFrame;
    nodeFrame.delayTapDensity = 1.0f;
    nodeFrame.delayTaps[static_cast<std::size_t>(astral::astro::PlanetId::Mercury)] = {0.18f, 1.0f, -0.65f};
    nodeFrame.delayTaps[static_cast<std::size_t>(astral::astro::PlanetId::Jupiter)] = {0.82f, 1.0f, 0.65f};

    baselineDelay.processBlock(
        {inputLeft.data(), inputRight.data(), sampleCount},
        {baselineLeft.data(), baselineRight.data(), sampleCount},
        nodeParameters,
        baselineFrame);
    nodeDelay.processBlock(
        {inputLeft.data(), inputRight.data(), sampleCount},
        {nodeLeft.data(), nodeRight.data(), sampleCount},
        nodeParameters,
        nodeFrame);

    require(absoluteDifferenceSum(baselineLeft, nodeLeft) > 0.01f, "planet node taps change the left delay pattern");
    require(absoluteDifferenceSum(baselineRight, nodeRight) > 0.01f, "planet node taps change the right delay pattern");

    PlanetaryDrone drone;
    drone.prepare(48000.0, 512);

    std::fill(inputLeft.begin(), inputLeft.end(), 0.0f);
    std::fill(inputRight.begin(), inputRight.end(), 0.0f);
    std::fill(outputLeft.begin(), outputLeft.end(), 0.0f);
    std::fill(outputRight.begin(), outputRight.end(), 0.0f);

    astral::astro::AstroDroneFrame droneFrame;
    for (std::size_t index = 0; index < droneFrame.layers.size(); ++index) {
        auto& layer = droneFrame.layers[index];
        layer.planet = static_cast<astral::astro::PlanetId>(index);
        layer.ratio = 0.5f + static_cast<float>(index) * 0.2f;
        layer.amplitude = 0.5f;
        layer.detuneCents = static_cast<float>(index) - 4.0f;
        layer.pan = index % 2 == 0 ? -0.3f : 0.3f;
        layer.tapePosition = static_cast<float>(index) / static_cast<float>(droneFrame.layers.size());
    }
    droneFrame.rootReinforcement = 0.5f;
    droneFrame.consonantBloom = 0.4f;
    droneFrame.tensionBeating = 0.2f;
    droneFrame.phaseSplit = 0.3f;

    PlanetaryDroneParameters droneParameters;
    droneParameters.droneLevel = 0.7f;
    droneParameters.captureLevel = 0.0f;
    droneParameters.rootFrequencyHz = 110.0f;
    droneParameters.gate = false;

    drone.processBlock(
        {inputLeft.data(), inputRight.data(), sampleCount},
        {outputLeft.data(), outputRight.data(), sampleCount},
        {inputLeft.data(), inputRight.data(), sampleCount},
        droneParameters,
        droneFrame);

    require(peak(outputLeft) < 0.000001f, "closed MIDI gate keeps drone silent on left");
    require(peak(outputRight) < 0.000001f, "closed MIDI gate keeps drone silent on right");

    drone.reset();
    std::fill(outputLeft.begin(), outputLeft.end(), 0.0f);
    std::fill(outputRight.begin(), outputRight.end(), 0.0f);
    droneParameters.gate = false;
    droneParameters.captureLevel = 0.0f;
    droneParameters.captureEnabled = false;
    droneParameters.organMode = true;
    droneParameters.organLayerGates.fill(false);
    droneParameters.organLayerGates[static_cast<std::size_t>(astral::astro::PlanetId::Sun)] = true;

    drone.processBlock(
        {inputLeft.data(), inputRight.data(), sampleCount},
        {outputLeft.data(), outputRight.data(), sampleCount},
        {inputLeft.data(), inputRight.data(), sampleCount},
        droneParameters,
        droneFrame);

    require(peak(outputLeft) > 0.001f, "organ mode gates only held planet drone on left");
    require(peak(outputRight) > 0.001f, "organ mode gates only held planet drone on right");

    std::fill(outputLeft.begin(), outputLeft.end(), 0.0f);
    std::fill(outputRight.begin(), outputRight.end(), 0.0f);
    droneParameters.organLayerGates.fill(false);

    drone.processBlock(
        {inputLeft.data(), inputRight.data(), sampleCount},
        {outputLeft.data(), outputRight.data(), sampleCount},
        {inputLeft.data(), inputRight.data(), sampleCount},
        droneParameters,
        droneFrame);

    require(peak(outputLeft) < 0.000001f, "organ mode silences drone when no keys are held");
    require(peak(outputRight) < 0.000001f, "organ mode silences drone when no keys are held");
    droneParameters.organMode = false;

    std::fill(outputLeft.begin(), outputLeft.end(), 0.0f);
    std::fill(outputRight.begin(), outputRight.end(), 0.0f);
    droneParameters.gate = true;
    droneParameters.captureLevel = 0.4f;
    droneParameters.captureEnabled = true;
    inputLeft[0] = 0.8f;
    inputRight[0] = -0.5f;

    drone.processBlock(
        {inputLeft.data(), inputRight.data(), sampleCount},
        {outputLeft.data(), outputRight.data(), sampleCount},
        {inputLeft.data(), inputRight.data(), sampleCount},
        droneParameters,
        droneFrame);

    require(allFinite(outputLeft), "drone left output remains finite");
    require(allFinite(outputRight), "drone right output remains finite");
    require(peak(outputLeft) > 0.001f, "open MIDI gate creates audible left drone");
    require(peak(outputRight) > 0.001f, "open MIDI gate creates audible right drone");
    require(peak(outputLeft) < 2.0f, "left drone output is bounded");
    require(peak(outputRight) < 2.0f, "right drone output is bounded");

    drone.resetCapture();
    std::fill(outputLeft.begin(), outputLeft.end(), 0.0f);
    std::fill(outputRight.begin(), outputRight.end(), 0.0f);
    std::fill(inputLeft.begin(), inputLeft.end(), 0.0f);
    std::fill(inputRight.begin(), inputRight.end(), 0.0f);
    droneParameters.droneLevel = 0.0f;
    droneParameters.captureLevel = 1.0f;
    droneParameters.captureEnabled = false;

    drone.processBlock(
        {inputLeft.data(), inputRight.data(), sampleCount},
        {outputLeft.data(), outputRight.data(), sampleCount},
        {inputLeft.data(), inputRight.data(), sampleCount},
        droneParameters,
        droneFrame);

    require(peak(outputLeft) < 0.000001f, "reset clears captured left drone source");
    require(peak(outputRight) < 0.000001f, "reset clears captured right drone source");

    drone.reset();
    std::fill(inputLeft.begin(), inputLeft.end(), 0.0f);
    std::fill(inputRight.begin(), inputRight.end(), 0.0f);
    std::fill(outputLeft.begin(), outputLeft.end(), 0.0f);
    std::fill(outputRight.begin(), outputRight.end(), 0.0f);
    inputLeft[0] = 0.9f;
    inputRight[0] = -0.7f;

    for (auto& layer : droneFrame.layers) {
        layer.amplitude = 0.65f;
        layer.tailSeconds = 2.0f;
    }
    droneFrame.layers[static_cast<std::size_t>(astral::astro::PlanetId::Sun)].tailSeconds = 2.0f;
    droneFrame.layers[static_cast<std::size_t>(astral::astro::PlanetId::Pluto)].tailSeconds = 18.0f;

    droneParameters.droneLevel = 0.0f;
    droneParameters.captureLevel = 0.8f;
    droneParameters.captureEnabled = true;
    droneParameters.gate = false;
    droneParameters.manualLayerGates.fill(false);
    droneParameters.manualLayerGates[static_cast<std::size_t>(astral::astro::PlanetId::Sun)] = true;

    drone.processBlock(
        {inputLeft.data(), inputRight.data(), sampleCount},
        {outputLeft.data(), outputRight.data(), sampleCount},
        {inputLeft.data(), inputRight.data(), sampleCount},
        droneParameters,
        droneFrame);

    require(allFinite(outputLeft), "manual orbit tail left output remains finite");
    require(allFinite(outputRight), "manual orbit tail right output remains finite");
    require(peak(outputLeft) > 0.001f, "active manual orbit tail captures audible left input");
    require(peak(outputRight) > 0.001f, "active manual orbit tail captures audible right input");

    std::fill(inputLeft.begin(), inputLeft.end(), 0.0f);
    std::fill(inputRight.begin(), inputRight.end(), 0.0f);
    std::fill(outputLeft.begin(), outputLeft.end(), 0.0f);
    std::fill(outputRight.begin(), outputRight.end(), 0.0f);
    droneParameters.captureEnabled = false;
    droneParameters.manualLayerGates.fill(false);

    drone.processBlock(
        {inputLeft.data(), inputRight.data(), sampleCount},
        {outputLeft.data(), outputRight.data(), sampleCount},
        {inputLeft.data(), inputRight.data(), sampleCount},
        droneParameters,
        droneFrame);

    require(peak(outputLeft) < 0.15f, "released short manual orbit tail decays toward silence");

    drone.reset();
    std::fill(inputLeft.begin(), inputLeft.end(), 0.0f);
    std::fill(inputRight.begin(), inputRight.end(), 0.0f);
    std::fill(outputLeft.begin(), outputLeft.end(), 0.0f);
    std::fill(outputRight.begin(), outputRight.end(), 0.0f);
    inputLeft[0] = 1.0f;
    inputRight[0] = 1.0f;
    droneParameters.captureEnabled = true;
    droneParameters.manualLayerGates.fill(false);
    droneParameters.manualLayerGates[static_cast<std::size_t>(astral::astro::PlanetId::Sun)] = true;
    drone.processBlock(
        {inputLeft.data(), inputRight.data(), sampleCount},
        {outputLeft.data(), outputRight.data(), sampleCount},
        {inputLeft.data(), inputRight.data(), sampleCount},
        droneParameters,
        droneFrame);

    drone.reset();
    std::fill(inputLeft.begin(), inputLeft.end(), 0.0f);
    std::fill(inputRight.begin(), inputRight.end(), 0.0f);
    std::fill(outputLeft.begin(), outputLeft.end(), 0.0f);
    std::fill(outputRight.begin(), outputRight.end(), 0.0f);
    droneParameters.captureEnabled = false;
    droneParameters.manualLayerGates.fill(false);
    drone.processBlock(
        {inputLeft.data(), inputRight.data(), sampleCount},
        {outputLeft.data(), outputRight.data(), sampleCount},
        {inputLeft.data(), inputRight.data(), sampleCount},
        droneParameters,
        droneFrame);

    require(peak(outputLeft) < 0.000001f, "reset clears manual orbit tail left state");
    require(peak(outputRight) < 0.000001f, "reset clears manual orbit tail right state");

    auto renderTailRemainder = [&](astral::astro::PlanetId planet) {
        drone.reset();
        std::fill(inputLeft.begin(), inputLeft.end(), 0.0f);
        std::fill(inputRight.begin(), inputRight.end(), 0.0f);
        std::fill(outputLeft.begin(), outputLeft.end(), 0.0f);
        std::fill(outputRight.begin(), outputRight.end(), 0.0f);
        inputLeft[0] = 1.0f;
        inputRight[0] = 1.0f;
        droneParameters.captureEnabled = true;
        droneParameters.manualLayerGates.fill(false);
        droneParameters.manualLayerGates[static_cast<std::size_t>(planet)] = true;
        drone.processBlock(
            {inputLeft.data(), inputRight.data(), sampleCount},
            {outputLeft.data(), outputRight.data(), sampleCount},
            {inputLeft.data(), inputRight.data(), sampleCount},
            droneParameters,
            droneFrame);

        std::fill(inputLeft.begin(), inputLeft.end(), 0.0f);
        std::fill(inputRight.begin(), inputRight.end(), 0.0f);
        std::fill(outputLeft.begin(), outputLeft.end(), 0.0f);
        std::fill(outputRight.begin(), outputRight.end(), 0.0f);
        droneParameters.captureEnabled = false;
        droneParameters.manualLayerGates.fill(false);
        drone.processBlock(
            {inputLeft.data(), inputRight.data(), sampleCount},
            {outputLeft.data(), outputRight.data(), sampleCount},
            {inputLeft.data(), inputRight.data(), sampleCount},
            droneParameters,
            droneFrame);
        return peak(outputLeft);
    };

    const float shortTailRemainder = renderTailRemainder(astral::astro::PlanetId::Sun);
    const float longTailRemainder = renderTailRemainder(astral::astro::PlanetId::Pluto);
    require(longTailRemainder > shortTailRemainder, "longer orbit tail decays more slowly than shorter tail");

    drone.reset();
    std::fill(inputLeft.begin(), inputLeft.end(), 0.0f);
    std::fill(inputRight.begin(), inputRight.end(), 0.0f);
    std::fill(outputLeft.begin(), outputLeft.end(), 0.0f);
    std::fill(outputRight.begin(), outputRight.end(), 0.0f);
    for (std::size_t index = 0; index < droneFrame.layers.size(); ++index) {
        auto& layer = droneFrame.layers[index];
        layer.ratio = 1.0f;
        layer.amplitude = index == 0 ? 1.0f : 0.0f;
        layer.detuneCents = 0.0f;
        layer.pan = 0.0f;
    }
    droneParameters.droneLevel = 1.0f;
    droneParameters.captureLevel = 0.8f;
    droneParameters.captureEnabled = true;
    droneParameters.gate = true;
    droneParameters.manualLayerGates.fill(false);
    droneParameters.manualLayerGates[static_cast<std::size_t>(astral::astro::PlanetId::Sun)] = true;
    drone.processBlock(
        {inputLeft.data(), inputRight.data(), sampleCount},
        {outputLeft.data(), outputRight.data(), sampleCount},
        {inputLeft.data(), inputRight.data(), sampleCount},
        droneParameters,
        droneFrame);
    const float capturedDronePeak = peak(outputLeft);
    require(capturedDronePeak > 0.001f, "held capture routes the planet oscillator into its tail instead of silence");
    require(peak(outputRight) > 0.001f, "held capture routes the planet oscillator into its tail on right");

    drone.reset();
    std::fill(inputLeft.begin(), inputLeft.end(), 0.0f);
    std::fill(inputRight.begin(), inputRight.end(), 0.0f);
    std::fill(outputLeft.begin(), outputLeft.end(), 0.0f);
    std::fill(outputRight.begin(), outputRight.end(), 0.0f);
    for (std::size_t index = 0; index < droneFrame.layers.size(); ++index) {
        auto& layer = droneFrame.layers[index];
        layer.ratio = 1.0f;
        layer.amplitude = index == 0 ? 1.0f : 0.0f;
        layer.detuneCents = 0.0f;
        layer.pan = 0.0f;
        layer.tailSeconds = 2.0f;
    }
    droneParameters.droneLevel = 0.7f;
    droneParameters.captureLevel = 0.35f;
    droneParameters.captureEnabled = true;
    droneParameters.gate = true;
    droneParameters.organMode = false;
    droneParameters.manualLayerGates.fill(false);
    droneParameters.organLayerGates.fill(false);
    drone.processBlock(
        {inputLeft.data(), inputRight.data(), sampleCount},
        {outputLeft.data(), outputRight.data(), sampleCount},
        {inputLeft.data(), inputRight.data(), sampleCount},
        droneParameters,
        droneFrame);
    const float droneOnlyPeak = peak(outputLeft);

    drone.reset();
    std::fill(outputLeft.begin(), outputLeft.end(), 0.0f);
    std::fill(outputRight.begin(), outputRight.end(), 0.0f);
    droneParameters.manualLayerGates[static_cast<std::size_t>(astral::astro::PlanetId::Sun)] = true;
    drone.processBlock(
        {inputLeft.data(), inputRight.data(), sampleCount},
        {outputLeft.data(), outputRight.data(), sampleCount},
        {inputLeft.data(), inputRight.data(), sampleCount},
        droneParameters,
        droneFrame);
    require(peak(outputLeft) <= droneOnlyPeak * 1.15f, "held capture does not stack on top of the planet drone");


    drone.reset();
    std::fill(inputLeft.begin(), inputLeft.end(), 0.0f);
    std::fill(inputRight.begin(), inputRight.end(), 0.0f);
    std::fill(outputLeft.begin(), outputLeft.end(), 0.0f);
    std::fill(outputRight.begin(), outputRight.end(), 0.0f);
    for (std::size_t index = 0; index < droneFrame.layers.size(); ++index) {
        auto& layer = droneFrame.layers[index];
        layer.ratio = 1.0f;
        layer.amplitude = index == 0 ? 1.0f : 0.0f;
        layer.detuneCents = 0.0f;
        layer.pan = 0.0f;
    }
    droneParameters.droneLevel = 1.0f;
    droneParameters.captureLevel = 0.0f;
    droneParameters.captureEnabled = false;
    droneParameters.gate = true;
    droneParameters.rootFrequencyHz = 110.0f;
    drone.processBlock(
        {inputLeft.data(), inputRight.data(), sampleCount},
        {outputLeft.data(), outputRight.data(), sampleCount},
        {inputLeft.data(), inputRight.data(), sampleCount},
        droneParameters,
        droneFrame);
    const int lowRootCrossings = zeroCrossings(outputLeft);

    drone.reset();
    std::fill(outputLeft.begin(), outputLeft.end(), 0.0f);
    std::fill(outputRight.begin(), outputRight.end(), 0.0f);
    droneParameters.rootFrequencyHz = 220.0f;
    drone.processBlock(
        {inputLeft.data(), inputRight.data(), sampleCount},
        {outputLeft.data(), outputRight.data(), sampleCount},
        {inputLeft.data(), inputRight.data(), sampleCount},
        droneParameters,
        droneFrame);
    const int highRootCrossings = zeroCrossings(outputLeft);
    require(highRootCrossings > lowRootCrossings, "higher manual root renders a faster drone waveform");

    drone.reset();
    std::fill(outputLeft.begin(), outputLeft.end(), 0.0f);
    std::fill(outputRight.begin(), outputRight.end(), 0.0f);
    droneParameters.rootFrequencyHz = 110.0f;
    droneParameters.mutedLayers.fill(true);
    drone.processBlock(
        {inputLeft.data(), inputRight.data(), sampleCount},
        {outputLeft.data(), outputRight.data(), sampleCount},
        {inputLeft.data(), inputRight.data(), sampleCount},
        droneParameters,
        droneFrame);
    require(peak(outputLeft) < 0.000001f, "muted planet layers silence left drone contribution");
    require(peak(outputRight) < 0.000001f, "muted planet layers silence right drone contribution");
}
