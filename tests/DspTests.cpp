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
        droneParameters,
        droneFrame);

    require(peak(outputLeft) < 0.000001f, "closed MIDI gate keeps drone silent on left");
    require(peak(outputRight) < 0.000001f, "closed MIDI gate keeps drone silent on right");

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
        droneParameters,
        droneFrame);

    require(peak(outputLeft) < 0.000001f, "reset clears captured left drone source");
    require(peak(outputRight) < 0.000001f, "reset clears captured right drone source");
}
