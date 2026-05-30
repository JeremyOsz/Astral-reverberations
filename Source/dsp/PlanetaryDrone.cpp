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

// Converts invalid floating-point values to silence before they enter feedback paths.
float sanitize(float value)
{
    return std::isfinite(value) ? value : 0.0f;
}

float softLimit(float value)
{
    return std::tanh(sanitize(value));
}

float softLimitAbove(float value, float knee, float headroom)
{
    const float sanitized = sanitize(value);
    const float magnitude = std::abs(sanitized);
    if (magnitude <= knee) {
        return sanitized;
    }

    const float limited = knee + std::tanh((magnitude - knee) / std::max(0.001f, headroom)) * headroom;
    return std::copysign(limited, sanitized);
}

// Converts detune cents into a frequency multiplier.
float centsToRatio(float cents)
{
    return std::pow(2.0f, cents / 1200.0f);
}

// Advances an oscillator phase and wraps it into a single cycle.
void advancePhase(float& phase, float increment)
{
    phase += increment;
    while (phase >= kTwoPi) {
        phase -= kTwoPi;
    }
}

// Produces a warm harmonic oscillator voice with subtle upper partials.
float softSine(float phase, float bloom)
{
    const float fundamental = std::sin(phase);
    const float upper = std::sin(phase * 2.0f + bloom * 0.6f) * 0.24f;
    const float shimmer = std::sin(phase * 3.0f - bloom * 0.4f) * 0.11f;
    return fundamental + upper + shimmer;
}

float oscillatorForShape(float phase, float bloom, int shape)
{
    switch (std::clamp(shape, 0, 4)) {
    case 1:
        return std::sin(phase) >= 0.0f ? 0.82f : -0.82f;
    case 2:
        return (2.0f * (phase / kTwoPi)) - 1.0f;
    case 3:
        return 1.0f - 4.0f * std::abs((phase / kTwoPi) - 0.5f);
    case 4:
        return std::tanh(softSine(phase, bloom) * 1.8f);
    default:
        return softSine(phase, bloom);
    }
}

float onePoleCoefficient(float cutoffHz, double sampleRate)
{
    const float safeCutoff = std::clamp(cutoffHz, 20.0f, 18000.0f);
    const float x = std::exp(-kTwoPi * safeCutoff / static_cast<float>(std::max(1.0, sampleRate)));
    return std::clamp(1.0f - x, 0.0f, 1.0f);
}

// Chooses a short feedback-loop delay that supports the requested long decay time.
int manualTailDelaySamples(float tailSeconds, double sampleRate, int bufferSize)
{
    const float clampedTail = std::clamp(tailSeconds, 1.0f, 180.0f);
    const float delaySeconds = std::clamp(0.10f + std::sqrt(clampedTail) * 0.12f, 0.12f, 0.82f);
    return std::clamp(static_cast<int>(delaySeconds * static_cast<float>(sampleRate)), 1, std::max(1, bufferSize - 2));
}

// Computes feedback gain so a tail falls by roughly 60 dB over tailSeconds.
float feedbackForTail(float tailSeconds, int delaySamples, double sampleRate, float tailRegen)
{
    const float clampedTail = std::clamp(tailSeconds, 0.1f, 180.0f);
    const float delaySeconds = static_cast<float>(delaySamples) / static_cast<float>(std::max(1.0, sampleRate));
    const float baseFeedback = std::pow(0.001f, delaySeconds / clampedTail);
    const float regenLift = std::pow(std::clamp(tailRegen, 0.0f, 1.0f), 1.7f) * 0.18f;
    return std::clamp(baseFeedback + regenLift, 0.0f, 0.9996f);
}

float shapedTailSeconds(float baseSeconds, float tailSize)
{
    const float size = std::clamp(tailSize, 0.0f, 1.0f);
    const float multiplier = 1.0f + size * size * 9.0f;
    return std::clamp(baseSeconds * multiplier, 0.35f, 180.0f);
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

void PlanetaryDrone::ManualTailBuffer::prepare(int sampleCount)
{
    left.assign(std::max(2, sampleCount), 0.0f);
    right.assign(std::max(2, sampleCount), 0.0f);
    reset();
}

void PlanetaryDrone::ManualTailBuffer::reset()
{
    std::fill(left.begin(), left.end(), 0.0f);
    std::fill(right.begin(), right.end(), 0.0f);
    writeIndex = 0;
    envelope = 0.0f;
    displayLevel = 0.0f;
    smoothedDelaySamples = 0.0f;
    shimmerDcLeft = 0.0f;
    shimmerDcRight = 0.0f;
}

float PlanetaryDrone::ManualTailBuffer::process(
    float inputLeft,
    float inputRight,
    float tailSeconds,
    float level,
    float pan,
    bool active,
    float tailRegen,
    int tailMode,
    float& rightOut,
    double sampleRate)
{
    rightOut = 0.0f;
    if (left.empty() || right.empty()) {
        return 0.0f;
    }

    const int size = static_cast<int>(left.size());
    const float targetDelaySamples = static_cast<float>(manualTailDelaySamples(tailSeconds, sampleRate, size));
    if (smoothedDelaySamples <= 1.0f) {
        smoothedDelaySamples = targetDelaySamples;
    } else {
        smoothedDelaySamples += (targetDelaySamples - smoothedDelaySamples) * 0.0008f;
    }
    const float readPosition = static_cast<float>(writeIndex) + static_cast<float>(size) - smoothedDelaySamples;
    const float regen = std::clamp(tailRegen, 0.0f, 1.0f);
    const int mode = std::clamp(tailMode, 0, 2);
    const float feedback = feedbackForTail(tailSeconds, static_cast<int>(std::round(smoothedDelaySamples)), sampleRate, regen);
    const float leftDelayed = sanitize(PlanetaryDrone::readInterpolated(left, readPosition));
    const float rightDelayed = sanitize(PlanetaryDrone::readInterpolated(right, readPosition));
    const float panClamped = std::clamp(pan, -1.0f, 1.0f);
    const float leftGain = std::sqrt(0.5f * (1.0f - panClamped));
    const float rightGain = std::sqrt(0.5f * (1.0f + panClamped));
    const float targetEnvelope = active ? 1.0f : 0.0f;
    const float envelopeStep = active ? 0.008f : 1.0f / (std::clamp(tailSeconds, 1.0f, 180.0f) * static_cast<float>(sampleRate));

    envelope += (targetEnvelope - envelope) * envelopeStep;

    const float sourceLeft = mode == 0 ? softLimitAbove(inputLeft, 0.92f, 0.30f) : sanitize(inputLeft);
    const float sourceRight = mode == 0 ? softLimitAbove(inputRight, 0.92f, 0.30f) : sanitize(inputRight);
    const float excitationLeft = active ? sourceLeft * level * leftGain : 0.0f;
    const float excitationRight = active ? sourceRight * level * rightGain : 0.0f;
    float feedbackLeft = leftDelayed;
    float feedbackRight = rightDelayed;
    if (mode == 1) {
        const float drive = 1.0f + regen * 5.5f;
        feedbackLeft = softLimit(leftDelayed * drive);
        feedbackRight = softLimit(rightDelayed * drive);
    } else if (mode == 2) {
        const float rawShimmerLeft = std::abs(rightDelayed + leftDelayed * 0.35f);
        const float rawShimmerRight = std::abs(leftDelayed + rightDelayed * 0.35f);
        shimmerDcLeft += (rawShimmerLeft - shimmerDcLeft) * 0.0012f;
        shimmerDcRight += (rawShimmerRight - shimmerDcRight) * 0.0012f;
        const float shimmerAmount = 0.12f + regen * 0.34f;
        feedbackLeft += softLimit(rawShimmerLeft - shimmerDcLeft) * shimmerAmount;
        feedbackRight += softLimit(rawShimmerRight - shimmerDcRight) * shimmerAmount;
    }

    float writeLeft = excitationLeft + feedbackLeft * feedback;
    float writeRight = excitationRight + feedbackRight * feedback;
    if (mode == 0) {
        const float limiterDrive = 1.0f + regen * 0.9f;
        writeLeft = softLimitAbove(writeLeft * limiterDrive, 0.94f, 0.28f);
        writeRight = softLimitAbove(writeRight * limiterDrive, 0.94f, 0.28f);
    } else if (mode == 1) {
        const float outputTrim = 0.78f - regen * 0.08f;
        writeLeft = softLimit(writeLeft * (1.0f + regen * 2.2f)) * outputTrim;
        writeRight = softLimit(writeRight * (1.0f + regen * 2.2f)) * outputTrim;
    }

    left[writeIndex] = sanitize(writeLeft);
    right[writeIndex] = sanitize(writeRight);
    writeIndex = (writeIndex + 1) % size;

    if (active) {
        const float capturedEnergy = std::max(std::abs(sourceLeft), std::abs(sourceRight)) * level;
        displayLevel = std::max(displayLevel, std::clamp(capturedEnergy * 4.5f, 0.0f, 1.0f));
    } else {
        const float decayPerSample = std::pow(0.01f, 1.0f / (std::clamp(tailSeconds, 1.0f, 180.0f) * static_cast<float>(std::max(1.0, sampleRate))));
        displayLevel *= decayPerSample;
        if (displayLevel < 0.000001f) {
            displayLevel = 0.0f;
        }
    }

    const float outputDrive = mode == 1 ? 1.0f + regen * 1.6f : 1.0f;
    const float outputLeft = mode == 0 ? softLimitAbove(leftDelayed * outputDrive, 0.98f, 0.25f)
                                       : softLimit(leftDelayed * outputDrive);
    const float outputRight = mode == 0 ? softLimitAbove(rightDelayed * outputDrive, 0.98f, 0.25f)
                                        : softLimit(rightDelayed * outputDrive);
    rightOut = outputRight * level * envelope;
    return outputLeft * level * envelope;
}

void PlanetaryDrone::prepare(double newSampleRate, int)
{
    sampleRate = std::max(1.0, newSampleRate);
    capture.prepare(static_cast<int>(sampleRate * 2.0));
    const auto manualTailSamples = static_cast<int>(sampleRate * 0.9);
    for (auto& tail : manualTails) {
        tail.prepare(manualTailSamples);
    }
    reset();
}

void PlanetaryDrone::reset()
{
    phases.fill(0.0f);
    beatPhases.fill(0.0f);
    voiceFilterStates.fill(0.0f);
    tailLevels.fill(0.0f);
    gateEnvelope = 0.0f;
    for (auto& tail : manualTails) {
        tail.reset();
    }
    resetCapture();
}

void PlanetaryDrone::resetCapture()
{
    capture.reset();
}

void PlanetaryDrone::processBlock(
    const StereoBufferView& input,
    const StereoBufferView& output,
    const StereoBufferView& captureInput,
    const PlanetaryDroneParameters& parameters,
    const astro::AstroDroneFrame& frame)
{
    if (input.left == nullptr || input.right == nullptr || output.left == nullptr || output.right == nullptr) {
        return;
    }

    const int samplesToProcess = std::max(0, input.numSamples);
    const float droneLevel = std::clamp(parameters.droneLevel, 0.0f, 1.5f);
    const float captureLevel = clamp01(parameters.captureLevel);
    const float harmonicSpread = clamp01(parameters.harmonicSpread);
    const float aspectDepth = clamp01(parameters.aspectDepth);
    const float safeRoot = std::clamp(parameters.rootFrequencyHz, 8.0f, 16000.0f);
    bool anyOrganLayerHeld = false;
    if (parameters.organMode) {
        for (std::size_t index = 0; index < parameters.organLayerGates.size(); ++index) {
            if (parameters.organLayerGates[index] && !parameters.mutedLayers[index]) {
                anyOrganLayerHeld = true;
                break;
            }
        }
    }
    const bool gateOpen = parameters.gate || anyOrganLayerHeld;
    const float targetGate = gateOpen ? 1.0f : 0.0f;
    const float gateStep = gateOpen ? 0.0025f : 0.0045f;
    const float aspectGain = 1.0f
        + frame.rootReinforcement * aspectDepth * 0.18f
        + frame.consonantBloom * aspectDepth * 0.22f;
    const float beatingDepth = frame.tensionBeating * aspectDepth;
    bool anyManualCaptureHeld = false;
    if (parameters.captureEnabled) {
        for (std::size_t index = 0; index < parameters.manualLayerGates.size(); ++index) {
            if (parameters.manualLayerGates[index] && !parameters.mutedLayers[index]) {
                anyManualCaptureHeld = true;
                break;
            }
        }
    }
    const bool captureInputValid = captureInput.left != nullptr && captureInput.right != nullptr
        && captureInput.numSamples >= samplesToProcess;
    float captureInputPeak = 0.0f;
    if (captureInputValid) {
        for (int index = 0; index < samplesToProcess; ++index) {
            captureInputPeak = std::max(
                captureInputPeak,
                std::max(std::abs(captureInput.left[index]), std::abs(captureInput.right[index])));
        }
    }
    const bool blockUseExternalCapture = captureInputPeak > 0.002f;

    for (int sample = 0; sample < samplesToProcess; ++sample) {
        const float inLeft = sanitize(input.left[sample]);
        const float inRight = sanitize(input.right[sample]);
        const float captureInLeft = captureInputValid ? sanitize(captureInput.left[sample]) : inLeft;
        const float captureInRight = captureInputValid ? sanitize(captureInput.right[sample]) : inRight;
        gateEnvelope += (targetGate - gateEnvelope) * gateStep;
        if (gateEnvelope < 0.000001f && !gateOpen && captureLevel <= 0.0f) {
            continue;
        }

        float droneLeft = 0.0f;
        float droneRight = 0.0f;
        float manualLeft = 0.0f;
        float manualRight = 0.0f;

        for (std::size_t index = 0; index < frame.layers.size(); ++index) {
            if (parameters.mutedLayers[index]) {
                continue;
            }

            const auto& layer = frame.layers[index];
            const float ratio = std::clamp(1.0f + (layer.ratio - 1.0f) * harmonicSpread, 0.125f, 12.0f);
            const float frequency = std::clamp(safeRoot * ratio * centsToRatio(layer.detuneCents), 8.0f, 18000.0f);
            const float increment = kTwoPi * frequency / static_cast<float>(sampleRate);
            const float beatRate = 0.04f + layer.driftRate * 1.8f + frame.aspectTapDensity * 0.6f;
            const float beatIncrement = kTwoPi * beatRate / static_cast<float>(sampleRate);
            const float beat = 1.0f + std::sin(beatPhases[index]) * beatingDepth * 0.18f;
            const float rawVoice = oscillatorForShape(phases[index], frame.consonantBloom, parameters.waveShapes[index]);
            const float filter01 = clamp01(parameters.filterCutoffs[index]);
            const float cutoffHz = 180.0f + filter01 * filter01 * 11820.0f;
            const float voiceFilterCoefficient = onePoleCoefficient(cutoffHz, sampleRate);
            voiceFilterStates[index] += (rawVoice - voiceFilterStates[index]) * voiceFilterCoefficient;
            const float voice = voiceFilterStates[index] * layer.amplitude * beat;
            const float pan = std::clamp(layer.pan + frame.phaseSplit * 0.18f, -1.0f, 1.0f);
            const float leftGain = std::sqrt(0.5f * (1.0f - pan));
            const float rightGain = std::sqrt(0.5f * (1.0f + pan));

            const bool manualCaptureHeld = parameters.captureEnabled && parameters.manualLayerGates[index];
            const float tailSourceLeft = manualCaptureHeld
                ? (blockUseExternalCapture ? captureInLeft : voice * leftGain)
                : captureInLeft;
            const float tailSourceRight = manualCaptureHeld
                ? (blockUseExternalCapture ? captureInRight : voice * rightGain)
                : captureInRight;
            bool tailReplacingLayer = manualCaptureHeld;
            if (captureLevel > 0.0f || tailLevels[index] > 0.000001f) {
                float tailRight = 0.0f;
                const float tailSeconds = shapedTailSeconds(layer.tailSeconds, parameters.tailSize);
                const float tailLeft = manualTails[index].process(
                    tailSourceLeft,
                    tailSourceRight,
                    tailSeconds,
                    captureLevel * layer.amplitude,
                    pan,
                    manualCaptureHeld,
                    parameters.tailRegen,
                    parameters.tailMode,
                    tailRight,
                    sampleRate);
                manualLeft += tailLeft;
                manualRight += tailRight;
                const float delayedEnergy = std::max(std::abs(tailLeft), std::abs(tailRight));
                tailReplacingLayer = manualCaptureHeld || manualTails[index].isReplacingLayer() || delayedEnergy > 0.000001f;
            }

            const bool layerDroneAllowed = !parameters.organMode || parameters.organLayerGates[index];
            if (!tailReplacingLayer && layerDroneAllowed) {
                droneLeft += voice * leftGain;
                droneRight += voice * rightGain;

                if (captureLevel > 0.0f && parameters.captureEnabled && !anyManualCaptureHeld) {
                    const float scan = std::fmod(layer.tapePosition + phases[index] / kTwoPi * 0.015f, 1.0f);
                    droneLeft += capture.readLeft(scan) * captureLevel * layer.amplitude * 0.22f;
                    droneRight += capture.readRight(scan) * captureLevel * layer.amplitude * 0.22f;
                }
            }

            advancePhase(phases[index], increment);
            advancePhase(beatPhases[index], beatIncrement);
        }

        if (parameters.captureEnabled) {
            capture.write(
                blockUseExternalCapture ? captureInLeft : droneLeft,
                blockUseExternalCapture ? captureInRight : droneRight);
        }

        const float layerScale = 1.35f / static_cast<float>(frame.layers.size());
        const float manualScale = anyManualCaptureHeld ? layerScale : (0.55f / static_cast<float>(frame.layers.size()));
        const float synthGain = droneLevel * gateEnvelope * 1.18f;
        const float manualGain = 1.0f;
        output.left[sample] = sanitize(output.left[sample]
            + std::tanh(droneLeft * layerScale * aspectGain) * synthGain
            + std::tanh(manualLeft * manualScale * aspectGain) * manualGain);
        output.right[sample] = sanitize(output.right[sample]
            + std::tanh(droneRight * layerScale * aspectGain) * synthGain
            + std::tanh(manualRight * manualScale * aspectGain) * manualGain);

    }

    for (std::size_t index = 0; index < tailLevels.size(); ++index) {
        const float target = std::clamp(manualTails[index].getDisplayLevel(), 0.0f, 1.0f);
        const float coefficient = target > tailLevels[index] ? 0.72f : 0.14f;
        tailLevels[index] += (target - tailLevels[index]) * coefficient;
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
