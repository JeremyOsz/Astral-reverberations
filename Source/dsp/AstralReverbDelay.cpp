#include "dsp/AstralReverbDelay.h"

#include <algorithm>
#include <cmath>
#include <limits>

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

float softClip(float value)
{
    return std::tanh(value);
}

float millisecondsToSamples(float milliseconds, double sampleRate)
{
    return milliseconds * static_cast<float>(sampleRate) * 0.001f;
}

float onePoleCoefficient(float cutoffHz, double sampleRate)
{
    const float normalized = std::clamp(cutoffHz / static_cast<float>(sampleRate), 0.0001f, 0.49f);
    return 1.0f - std::exp(-kTwoPi * normalized);
}

void advanceWrapped(float& phase, float increment)
{
    phase += increment;
    if (phase >= kTwoPi) {
        phase -= kTwoPi;
    }
}

float equalPowerDry(float mix)
{
    return std::cos(clamp01(mix) * kPi * 0.5f);
}

float equalPowerWet(float mix)
{
    return std::sin(clamp01(mix) * kPi * 0.5f);
}

} // namespace

void AstralReverbDelay::DelayBuffer::prepare(int sampleCount)
{
    samples.assign(std::max(2, sampleCount), 0.0f);
    writeIndex = 0;
}

void AstralReverbDelay::DelayBuffer::reset()
{
    std::fill(samples.begin(), samples.end(), 0.0f);
    writeIndex = 0;
}

float AstralReverbDelay::DelayBuffer::read(float delaySamples) const
{
    if (samples.empty()) {
        return 0.0f;
    }

    const auto size = static_cast<int>(samples.size());
    const float safeDelay = std::clamp(delaySamples, 1.0f, static_cast<float>(size - 2));
    float readPosition = static_cast<float>(writeIndex) - safeDelay;
    while (readPosition < 0.0f) {
        readPosition += static_cast<float>(size);
    }

    const int index0 = static_cast<int>(readPosition) % size;
    const int index1 = (index0 + 1) % size;
    const float fraction = readPosition - std::floor(readPosition);
    return samples[index0] + (samples[index1] - samples[index0]) * fraction;
}

void AstralReverbDelay::DelayBuffer::write(float value)
{
    if (!samples.empty()) {
        samples[writeIndex] = sanitize(value);
    }
}

void AstralReverbDelay::DelayBuffer::advance()
{
    if (!samples.empty()) {
        writeIndex = (writeIndex + 1) % static_cast<int>(samples.size());
    }
}

void AstralReverbDelay::OnePoleLowPass::reset()
{
    state = 0.0f;
}

float AstralReverbDelay::OnePoleLowPass::process(float input, float coefficient)
{
    state += std::clamp(coefficient, 0.0f, 1.0f) * (input - state);
    return state;
}

void AstralReverbDelay::prepare(double newSampleRate, int maximumExpectedBlockSize)
{
    sampleRate = std::max(1.0, newSampleRate);
    maxBlockSize = std::max(1, maximumExpectedBlockSize);

    const int maxDelaySamples = static_cast<int>(sampleRate * 3.0);
    delayLeft.prepare(maxDelaySamples);
    delayRight.prepare(maxDelaySamples);

    const std::array<float, kReverbLineCount> baseLineMs{73.0f, 91.0f, 127.0f, 149.0f};
    for (int index = 0; index < kReverbLineCount; ++index) {
        const int lineSamples = static_cast<int>(millisecondsToSamples(baseLineMs[index] * 2.5f, sampleRate));
        reverbLinesLeft[index].prepare(lineSamples);
        reverbLinesRight[index].prepare(lineSamples + 23);
    }

    reset();
}

void AstralReverbDelay::reset()
{
    delayLeft.reset();
    delayRight.reset();
    for (auto& line : reverbLinesLeft) {
        line.reset();
    }
    for (auto& line : reverbLinesRight) {
        line.reset();
    }
    feedbackFilterLeft.reset();
    feedbackFilterRight.reset();
    for (auto& filter : dampingFiltersLeft) {
        filter.reset();
    }
    for (auto& filter : dampingFiltersRight) {
        filter.reset();
    }
    smoothedDelaySamples = millisecondsToSamples(420.0f, sampleRate);
    wowPhase = 0.0f;
    flutterPhase = 0.0f;
    reverbModPhase = 0.0f;
}

void AstralReverbDelay::processBlock(
    const StereoBufferView& input,
    const StereoBufferView& output,
    const AstralParameters& parameters,
    const astro::AstroModulationFrame& astroFrame)
{
    if (input.left == nullptr || input.right == nullptr || output.left == nullptr || output.right == nullptr) {
        return;
    }

    const int samplesToProcess = std::max(0, input.numSamples);
    const float astroAmount = clamp01(parameters.astroAmount);
    const float astroDepth = parameters.modDepth * astroAmount;
    const float bloom = astroFrame.feedbackBloom * astroAmount;
    const float delayDriftMs = astroFrame.delayDrift * 38.0f * astroDepth;
    const float targetDelaySamples = millisecondsToSamples(
        std::clamp(parameters.delayTimeMs + delayDriftMs, 40.0f, 2000.0f),
        sampleRate);
    const float feedback = std::clamp(parameters.feedback + bloom * 0.22f, 0.0f, parameters.freeze ? 0.995f : 0.92f);
    const float reverbFeedback = parameters.freeze
        ? 0.998f
        : std::clamp(0.55f + parameters.space * 0.28f + astroFrame.reverbSize * astroAmount * 0.14f, 0.35f, 0.94f);
    const float toneCutoff = 700.0f + std::pow(clamp01(parameters.tone), 2.0f) * 11000.0f + astroFrame.shimmer * astroAmount * 3000.0f;
    const float dampingCutoff = 600.0f + (1.0f - clamp01(astroFrame.damping * astroAmount + (1.0f - parameters.tone) * 0.5f)) * 8500.0f;
    const float feedbackCoefficient = onePoleCoefficient(toneCutoff, sampleRate);
    const float dampingCoefficient = onePoleCoefficient(dampingCutoff, sampleRate);
    const float driveGain = 1.0f + clamp01(parameters.drive) * 5.0f;
    const float dryGain = equalPowerDry(parameters.mix);
    const float wetGain = equalPowerWet(parameters.mix);
    const float delayLevel = clamp01(parameters.delayLevel);
    const float reverbLevel = clamp01(parameters.reverbLevel);
    const float outputGain = std::clamp(parameters.outputGain, 0.0f, 2.0f);
    const float wowRate = 0.17f * std::max(0.1f, parameters.astroSpeed) + astroFrame.stereoMotion * astroAmount * 0.06f;
    const float flutterRate = 4.7f + astroFrame.delayDrift * astroAmount * 1.2f;
    const float wowIncrement = kTwoPi * wowRate / static_cast<float>(sampleRate);
    const float flutterIncrement = kTwoPi * flutterRate / static_cast<float>(sampleRate);

    for (int sample = 0; sample < samplesToProcess; ++sample) {
        smoothedDelaySamples += 0.0025f * (targetDelaySamples - smoothedDelaySamples);

        const float wow = std::sin(wowPhase) * parameters.wowFlutter * 18.0f;
        const float flutter = std::sin(flutterPhase) * parameters.wowFlutter * 4.0f;
        const float modulatedDelay = std::max(1.0f, smoothedDelaySamples + wow + flutter);

        const float inLeft = sanitize(input.left[sample]);
        const float inRight = sanitize(input.right[sample]);
        const float delayedLeft = delayLeft.read(modulatedDelay) * 0.62f
            + delayLeft.read(modulatedDelay * 0.67f) * 0.25f
            + delayRight.read(modulatedDelay * 1.31f) * 0.18f;
        const float delayedRight = delayRight.read(modulatedDelay * 1.07f) * 0.62f
            + delayRight.read(modulatedDelay * 0.71f) * 0.25f
            + delayLeft.read(modulatedDelay * 1.23f) * 0.18f;

        const float filteredFeedbackLeft = feedbackFilterLeft.process(delayedLeft, feedbackCoefficient);
        const float filteredFeedbackRight = feedbackFilterRight.process(delayedRight, feedbackCoefficient);
        const float writeLeft = parameters.freeze ? filteredFeedbackLeft : inLeft + filteredFeedbackLeft * feedback;
        const float writeRight = parameters.freeze ? filteredFeedbackRight : inRight + filteredFeedbackRight * feedback;

        delayLeft.write(softClip(writeLeft * driveGain) / driveGain);
        delayRight.write(softClip(writeRight * driveGain) / driveGain);
        delayLeft.advance();
        delayRight.advance();

        float reverbLeft = 0.0f;
        float reverbRight = 0.0f;
        processReverbSample(
            inLeft * 0.35f + delayedLeft * 0.45f,
            inRight * 0.35f + delayedRight * 0.45f,
            reverbFeedback,
            dampingCoefficient,
            astroFrame.stereoMotion * astroAmount,
            reverbLeft,
            reverbRight);

        const float wetLeft = delayedLeft * delayLevel + reverbLeft * reverbLevel;
        const float wetRight = delayedRight * delayLevel + reverbRight * reverbLevel;
        output.left[sample] = sanitize((inLeft * dryGain + wetLeft * wetGain) * outputGain);
        output.right[sample] = sanitize((inRight * dryGain + wetRight * wetGain) * outputGain);

        advanceWrapped(wowPhase, wowIncrement);
        advanceWrapped(flutterPhase, flutterIncrement);
        advanceWrapped(reverbModPhase, kTwoPi * 0.031f / static_cast<float>(sampleRate));
    }
}

void AstralReverbDelay::processReverbSample(
    float inputLeft,
    float inputRight,
    float feedback,
    float dampingCoefficient,
    float stereoMotion,
    float& outputLeft,
    float& outputRight)
{
    const std::array<float, kReverbLineCount> baseDelays{2113.0f, 2477.0f, 2897.0f, 3253.0f};
    const float motion = std::sin(reverbModPhase) * stereoMotion * 11.0f;
    std::array<float, kReverbLineCount> left{};
    std::array<float, kReverbLineCount> right{};

    for (int index = 0; index < kReverbLineCount; ++index) {
        const float lineScale = static_cast<float>(sampleRate) / 44100.0f;
        left[index] = dampingFiltersLeft[index].process(
            reverbLinesLeft[index].read(baseDelays[index] * lineScale + motion),
            dampingCoefficient);
        right[index] = dampingFiltersRight[index].process(
            reverbLinesRight[index].read((baseDelays[kReverbLineCount - 1 - index] + 37.0f) * lineScale - motion),
            dampingCoefficient);
    }

    // A small Hadamard-style feedback matrix diffuses energy between lines.
    const float l0 = left[0] + left[1] - left[2] - left[3];
    const float l1 = left[0] - left[1] + left[2] - left[3];
    const float l2 = left[0] - left[1] - left[2] + left[3];
    const float l3 = left[0] + left[1] + left[2] + left[3];
    const float r0 = right[0] + right[1] - right[2] - right[3];
    const float r1 = right[0] - right[1] + right[2] - right[3];
    const float r2 = right[0] - right[1] - right[2] + right[3];
    const float r3 = right[0] + right[1] + right[2] + right[3];
    const std::array<float, kReverbLineCount> matrixLeft{l0, l1, l2, l3};
    const std::array<float, kReverbLineCount> matrixRight{r0, r1, r2, r3};

    for (int index = 0; index < kReverbLineCount; ++index) {
        reverbLinesLeft[index].write(softClip(inputLeft + matrixLeft[index] * feedback * 0.5f));
        reverbLinesRight[index].write(softClip(inputRight + matrixRight[index] * feedback * 0.5f));
        reverbLinesLeft[index].advance();
        reverbLinesRight[index].advance();
    }

    outputLeft = (left[0] - left[1] + left[2] - left[3] + right[1] * 0.25f) * 0.28f;
    outputRight = (right[0] - right[1] + right[2] - right[3] + left[2] * 0.25f) * 0.28f;
}

} // namespace astral::dsp

