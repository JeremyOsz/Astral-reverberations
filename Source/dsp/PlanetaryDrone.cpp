#include "dsp/PlanetaryDrone.h"

#include <algorithm>
#include <cmath>

namespace astral::dsp {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;

float clamp01(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

float sanitize(float value)
{
    return std::isfinite(value) ? value : 0.0f;
}

float centsToRatio(float cents)
{
    return std::pow(2.0f, cents / 1200.0f);
}

void advancePhase(float& phase, float increment)
{
    phase += increment;
    while (phase >= kTwoPi) {
        phase -= kTwoPi;
    }
}

float softSine(float phase, float bloom)
{
    const float fundamental = std::sin(phase);
    const float upper = std::sin(phase * 2.0f + bloom * 0.6f) * 0.24f;
    const float shimmer = std::sin(phase * 3.0f - bloom * 0.4f) * 0.11f;
    return fundamental + upper + shimmer;
}

} // namespace

void PlanetaryDrone::CaptureBuffer::prepare(int sampleCount)
{
    left.assign(std::max(2, sampleCount), 0.0f);
    right.assign(std::max(2, sampleCount), 0.0f);
    writeIndex = 0;
    hasSignal = false;
}

void PlanetaryDrone::CaptureBuffer::reset()
{
    std::fill(left.begin(), left.end(), 0.0f);
    std::fill(right.begin(), right.end(), 0.0f);
    writeIndex = 0;
    hasSignal = false;
}

void PlanetaryDrone::CaptureBuffer::write(float leftSample, float rightSample)
{
    if (left.empty() || right.empty()) {
        return;
    }

    left[writeIndex] = sanitize(leftSample);
    right[writeIndex] = sanitize(rightSample);
    hasSignal = hasSignal || std::abs(leftSample) > 0.00001f || std::abs(rightSample) > 0.00001f;
    writeIndex = (writeIndex + 1) % static_cast<int>(left.size());
}

float PlanetaryDrone::CaptureBuffer::readLeft(float position01) const
{
    if (!hasSignal || left.empty()) {
        return 0.0f;
    }
    return PlanetaryDrone::readInterpolated(left, clamp01(position01) * static_cast<float>(left.size() - 1));
}

float PlanetaryDrone::CaptureBuffer::readRight(float position01) const
{
    if (!hasSignal || right.empty()) {
        return 0.0f;
    }
    return PlanetaryDrone::readInterpolated(right, clamp01(position01) * static_cast<float>(right.size() - 1));
}

void PlanetaryDrone::prepare(double newSampleRate, int)
{
    sampleRate = std::max(1.0, newSampleRate);
    capture.prepare(static_cast<int>(sampleRate * 2.0));
    reset();
}

void PlanetaryDrone::reset()
{
    phases.fill(0.0f);
    beatPhases.fill(0.0f);
    gateEnvelope = 0.0f;
    resetCapture();
}

void PlanetaryDrone::resetCapture()
{
    capture.reset();
}

void PlanetaryDrone::processBlock(
    const StereoBufferView& input,
    const StereoBufferView& output,
    const PlanetaryDroneParameters& parameters,
    const astro::AstroDroneFrame& frame)
{
    if (input.left == nullptr || input.right == nullptr || output.left == nullptr || output.right == nullptr) {
        return;
    }

    const int samplesToProcess = std::max(0, input.numSamples);
    const float droneLevel = clamp01(parameters.droneLevel);
    const float captureLevel = clamp01(parameters.captureLevel);
    const float harmonicSpread = clamp01(parameters.harmonicSpread);
    const float aspectDepth = clamp01(parameters.aspectDepth);
    const float safeRoot = std::clamp(parameters.rootFrequencyHz, 8.0f, 16000.0f);
    const float targetGate = parameters.gate ? 1.0f : 0.0f;
    const float gateStep = parameters.gate ? 0.0025f : 0.0045f;
    const float aspectGain = 1.0f
        + frame.rootReinforcement * aspectDepth * 0.18f
        + frame.consonantBloom * aspectDepth * 0.22f;
    const float beatingDepth = frame.tensionBeating * aspectDepth;

    for (int sample = 0; sample < samplesToProcess; ++sample) {
        const float inLeft = sanitize(input.left[sample]);
        const float inRight = sanitize(input.right[sample]);
        if (parameters.captureEnabled) {
            capture.write(inLeft, inRight);
        }

        gateEnvelope += (targetGate - gateEnvelope) * gateStep;
        if (gateEnvelope < 0.000001f && !parameters.gate) {
            continue;
        }

        float droneLeft = 0.0f;
        float droneRight = 0.0f;

        for (std::size_t index = 0; index < frame.layers.size(); ++index) {
            const auto& layer = frame.layers[index];
            const float ratio = std::clamp(1.0f + (layer.ratio - 1.0f) * harmonicSpread, 0.125f, 12.0f);
            const float frequency = std::clamp(safeRoot * ratio * centsToRatio(layer.detuneCents), 8.0f, 18000.0f);
            const float increment = kTwoPi * frequency / static_cast<float>(sampleRate);
            const float beatRate = 0.04f + layer.driftRate * 1.8f + frame.aspectTapDensity * 0.6f;
            const float beatIncrement = kTwoPi * beatRate / static_cast<float>(sampleRate);
            const float beat = 1.0f + std::sin(beatPhases[index]) * beatingDepth * 0.18f;
            const float voice = softSine(phases[index], frame.consonantBloom) * layer.amplitude * beat;
            const float pan = std::clamp(layer.pan + frame.phaseSplit * 0.18f, -1.0f, 1.0f);
            const float leftGain = std::sqrt(0.5f * (1.0f - pan));
            const float rightGain = std::sqrt(0.5f * (1.0f + pan));

            droneLeft += voice * leftGain;
            droneRight += voice * rightGain;

            if (captureLevel > 0.0f) {
                const float scan = std::fmod(layer.tapePosition + phases[index] / kTwoPi * 0.015f, 1.0f);
                droneLeft += capture.readLeft(scan) * captureLevel * layer.amplitude * 0.22f;
                droneRight += capture.readRight(scan) * captureLevel * layer.amplitude * 0.22f;
            }

            advancePhase(phases[index], increment);
            advancePhase(beatPhases[index], beatIncrement);
        }

        const float layerScale = 1.0f / static_cast<float>(frame.layers.size());
        output.left[sample] = sanitize(output.left[sample] + std::tanh(droneLeft * layerScale * aspectGain) * droneLevel * gateEnvelope);
        output.right[sample] = sanitize(output.right[sample] + std::tanh(droneRight * layerScale * aspectGain) * droneLevel * gateEnvelope);
    }
}

float PlanetaryDrone::readInterpolated(const std::vector<float>& buffer, float index)
{
    if (buffer.empty()) {
        return 0.0f;
    }

    const auto size = static_cast<int>(buffer.size());
    const int index0 = static_cast<int>(std::floor(index)) % size;
    const int index1 = (index0 + 1) % size;
    const float fraction = index - std::floor(index);
    return buffer[index0] + (buffer[index1] - buffer[index0]) * fraction;
}

} // namespace astral::dsp
