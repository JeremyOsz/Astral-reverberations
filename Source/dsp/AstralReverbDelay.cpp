#include "dsp/AstralReverbDelay.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

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

// Keeps delay/reverb feedback bounded without a hard clipping edge.
float softClip(float value)
{
    return std::tanh(value);
}

// Converts a musical delay time into a delay-line read distance.
float millisecondsToSamples(float milliseconds, double sampleRate)
{
    return milliseconds * static_cast<float>(sampleRate) * 0.001f;
}

// Produces a stable one-pole low-pass coefficient from a cutoff frequency.
float onePoleCoefficient(float cutoffHz, double sampleRate)
{
    const float normalized = std::clamp(cutoffHz / static_cast<float>(sampleRate), 0.0001f, 0.49f);
    return 1.0f - std::exp(-kTwoPi * normalized);
}

// Advances an oscillator phase and wraps it into a single cycle.
void advanceWrapped(float& phase, float increment)
{
    phase += increment;
    if (phase >= kTwoPi) {
        phase -= kTwoPi;
    }
}

// Computes the wet side of the equal-power wet/dry crossfade.
float equalPowerWet(float mix)
{
    return std::sin(clamp01(mix) * kPi * 0.5f);
}

float dbToGain(float db)
{
    return std::pow(10.0f, db / 20.0f);
}

// Reads one zodiac effect amount from a mapped modulation frame.
float signEffect(const astro::AstroModulationFrame& frame, astro::ZodiacSign sign)
{
    return std::clamp(frame.signEffects[static_cast<std::size_t>(sign)], 0.0f, 1.0f);
}

std::uint32_t nextNoise(std::uint32_t& state)
{
    state = state * 1664525u + 1013904223u;
    return state;
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

void AstralReverbDelay::KarplusPluck::prepare(int maxSamples)
{
    samples.assign(std::max(2, maxSamples), 0.0f);
    reset();
}

void AstralReverbDelay::KarplusPluck::reset()
{
    std::fill(samples.begin(), samples.end(), 0.0f);
    size = std::min<int>(static_cast<int>(samples.size()), 96);
    index = 0;
    level = 0.0f;
    feedback = 0.0f;
}

void AstralReverbDelay::KarplusPluck::trigger(float newLevel, float tone, float frequencyHz, double currentSampleRate)
{
    if (samples.empty()) {
        return;
    }

    const float clampedTone = clamp01(tone);
    const float frequency = std::clamp(frequencyHz, 35.0f, 2400.0f);
    size = std::clamp(
        static_cast<int>(currentSampleRate / static_cast<double>(frequency)),
        16,
        static_cast<int>(samples.size()));
    index = 0;
    level = std::max(level, std::clamp(newLevel, 0.0f, 2.5f) * 0.46f);
    feedback = 0.982f - clampedTone * 0.026f;

    std::uint32_t noiseState = 0x9e3779b9u ^ static_cast<std::uint32_t>(size * 2654435761u);
    for (int sample = 0; sample < size; ++sample) {
        const float noise = static_cast<float>((nextNoise(noiseState) >> 8) & 0xffffu) / 32767.5f - 1.0f;
        const float window = 1.0f - static_cast<float>(sample) / static_cast<float>(std::max(1, size));
        samples[static_cast<std::size_t>(sample)] = noise * window;
    }
}

float AstralReverbDelay::KarplusPluck::process()
{
    if (level <= 0.00001f || samples.empty() || size <= 1) {
        return 0.0f;
    }

    const int nextIndex = (index + 1) % size;
    const float current = samples[static_cast<std::size_t>(index)];
    const float next = samples[static_cast<std::size_t>(nextIndex)];
    samples[static_cast<std::size_t>(index)] = (current + next) * 0.5f * feedback;
    index = nextIndex;
    level *= 0.9996f;
    return current * level;
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
    reverbTankPluck.prepare(static_cast<int>(sampleRate * 0.03));

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
    masterLowLeft.reset();
    masterLowRight.reset();
    masterHighLeft.reset();
    masterHighRight.reset();
    reverbTankPluck.reset();
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
    const float ariesDrive = signEffect(astroFrame, astro::ZodiacSign::Aries) * astroAmount;
    const float taurusDamping = signEffect(astroFrame, astro::ZodiacSign::Taurus) * astroAmount;
    const float geminiTaps = signEffect(astroFrame, astro::ZodiacSign::Gemini) * astroAmount;
    const float cancerSpace = signEffect(astroFrame, astro::ZodiacSign::Cancer) * astroAmount;
    const float leoShimmer = signEffect(astroFrame, astro::ZodiacSign::Leo) * astroAmount;
    const float virgoTone = signEffect(astroFrame, astro::ZodiacSign::Virgo) * astroAmount;
    const float libraWidth = signEffect(astroFrame, astro::ZodiacSign::Libra) * astroAmount;
    const float scorpioFeedback = signEffect(astroFrame, astro::ZodiacSign::Scorpio) * astroAmount;
    const float sagittariusTime = signEffect(astroFrame, astro::ZodiacSign::Sagittarius) * astroAmount;
    const float capricornRestraint = signEffect(astroFrame, astro::ZodiacSign::Capricorn) * astroAmount;
    const float aquariusMotion = signEffect(astroFrame, astro::ZodiacSign::Aquarius) * astroAmount;
    const float piscesWash = signEffect(astroFrame, astro::ZodiacSign::Pisces) * astroAmount;
    const float bloom = astroFrame.feedbackBloom * astroAmount;
    const float delayDriftMs = astroFrame.delayDrift * 38.0f * astroDepth + sagittariusTime * 90.0f - capricornRestraint * 36.0f;
    const float targetDelaySamples = millisecondsToSamples(
        std::clamp(parameters.delayTimeMs + delayDriftMs, 40.0f, 2000.0f),
        sampleRate);
    const float feedback = std::clamp(parameters.feedback + bloom * 0.22f + scorpioFeedback * 0.10f - capricornRestraint * 0.08f, 0.0f, parameters.freeze ? 0.995f : 0.92f);
    const float reverbDecay = clamp01(parameters.reverbDecay);
    const float reverbDamping = clamp01(parameters.reverbDamping);
    const float reverbModDepth = clamp01(parameters.reverbModDepth);
    const float reverbFeedback = parameters.freeze
        ? 0.998f
        : std::clamp(0.50f + parameters.space * 0.24f + reverbDecay * 0.22f + astroFrame.reverbSize * astroAmount * 0.14f + cancerSpace * 0.06f + piscesWash * 0.05f, 0.35f, 0.985f);
    const float toneCutoff = 700.0f + std::pow(clamp01(parameters.tone), 2.0f) * 11000.0f + astroFrame.shimmer * astroAmount * 3000.0f + leoShimmer * 3200.0f + virgoTone * 1200.0f;
    const float dampingCutoff = 420.0f + (1.0f - clamp01(astroFrame.damping * astroAmount + taurusDamping * 0.35f + reverbDamping * 0.45f + (1.0f - parameters.tone) * 0.35f)) * 10500.0f;
    const float feedbackCoefficient = onePoleCoefficient(toneCutoff, sampleRate);
    const float dampingCoefficient = onePoleCoefficient(dampingCutoff, sampleRate);
    const float driveGain = 1.0f + clamp01(parameters.drive) * 5.0f + ariesDrive * 2.5f;
    // Live input is only passed through when Input Monitor is enabled; mix controls wet effect level.
    const float dryGain = parameters.inputMonitor ? 1.0f : 0.0f;
    const float wetGain = equalPowerWet(parameters.mix);
    const float delayLevel = clamp01(parameters.delayLevel);
    const float reverbLevel = clamp01(parameters.reverbLevel);
    const float lowEqGain = dbToGain(std::clamp(parameters.eqLowGainDb, -12.0f, 12.0f));
    const float midEqGain = dbToGain(std::clamp(parameters.eqMidGainDb, -12.0f, 12.0f));
    const float highEqGain = dbToGain(std::clamp(parameters.eqHighGainDb, -12.0f, 12.0f));
    const float lowEqCoefficient = onePoleCoefficient(240.0f, sampleRate);
    const float highEqCoefficient = onePoleCoefficient(4200.0f, sampleRate);
    const float outputGain = std::clamp(parameters.outputGain, 0.0f, 2.0f);
    const float reverbTankTapLevel = std::clamp(parameters.reverbTankTapLevel, 0.0f, 2.5f);
    const float reverbTankTapPan = std::clamp(parameters.reverbTankTapPan, -1.0f, 1.0f);
    const float reverbTankTapWet = clamp01(parameters.reverbTankTapWet);
    if (reverbTankTapLevel > 0.0001f) {
        reverbTankPluck.trigger(reverbTankTapLevel, parameters.reverbTankTapTone, parameters.reverbTankTapFrequencyHz, sampleRate);
    }
    const float reverbTankTapLeftGain = std::sqrt(0.5f * (1.0f - reverbTankTapPan));
    const float reverbTankTapRightGain = std::sqrt(0.5f * (1.0f + reverbTankTapPan));
    const float wowRate = 0.17f * std::max(0.1f, parameters.astroSpeed) + astroFrame.stereoMotion * astroAmount * 0.06f + aquariusMotion * 0.08f;
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
        const float tankTap = reverbTankPluck.process();
        const float tankTapLeft = tankTap * reverbTankTapLeftGain;
        const float tankTapRight = tankTap * reverbTankTapRightGain;
        const float tankTapWetLeft = tankTapLeft * reverbTankTapWet;
        const float tankTapWetRight = tankTapRight * reverbTankTapWet;
        const float tankTapDryLeft = tankTapLeft * (1.0f - reverbTankTapWet);
        const float tankTapDryRight = tankTapRight * (1.0f - reverbTankTapWet);
        const float effectInputLeft = sanitize(inLeft + tankTapWetLeft);
        const float effectInputRight = sanitize(inRight + tankTapWetRight);
        float delayedLeft = delayLeft.read(modulatedDelay) * 0.62f
            + delayLeft.read(modulatedDelay * 0.67f) * 0.25f
            + delayRight.read(modulatedDelay * 1.31f) * 0.18f;
        float delayedRight = delayRight.read(modulatedDelay * 1.07f) * 0.62f
            + delayRight.read(modulatedDelay * 0.71f) * 0.25f
            + delayLeft.read(modulatedDelay * 1.23f) * 0.18f;

        float nodeTapLeft = 0.0f;
        float nodeTapRight = 0.0f;
        const float nodeTapAmount = astroDepth * std::clamp(0.25f + astroFrame.delayTapDensity * 0.75f + geminiTaps * 0.35f, 0.0f, 1.25f);
        for (const auto& tap : astroFrame.delayTaps) {
            const float tapLevel = std::clamp(tap.level, 0.0f, 1.0f) * nodeTapAmount;
            if (tapLevel <= 0.0001f) {
                continue;
            }

            const float position = std::clamp(tap.position01, 0.0f, 1.0f);
            const float tapDelay = std::max(1.0f, modulatedDelay * (0.22f + position * 1.88f));
            const float pan = std::clamp(tap.pan, -1.0f, 1.0f);
            const float widenedPan = std::clamp(pan * (1.0f + libraWidth * 0.35f), -1.0f, 1.0f);
            const float leftGain = std::sqrt(0.5f * (1.0f - widenedPan));
            const float rightGain = std::sqrt(0.5f * (1.0f + widenedPan));
            const float leftRead = delayLeft.read(tapDelay) * 0.82f + delayRight.read(tapDelay * 1.017f) * 0.18f;
            const float rightRead = delayRight.read(tapDelay * 1.011f) * 0.82f + delayLeft.read(tapDelay * 0.983f) * 0.18f;
            nodeTapLeft += leftRead * leftGain * tapLevel;
            nodeTapRight += rightRead * rightGain * tapLevel;
        }
        delayedLeft += nodeTapLeft * 0.34f;
        delayedRight += nodeTapRight * 0.34f;

        const float filteredFeedbackLeft = feedbackFilterLeft.process(delayedLeft, feedbackCoefficient);
        const float filteredFeedbackRight = feedbackFilterRight.process(delayedRight, feedbackCoefficient);
        const float writeLeft = parameters.freeze ? filteredFeedbackLeft : effectInputLeft + filteredFeedbackLeft * feedback;
        const float writeRight = parameters.freeze ? filteredFeedbackRight : effectInputRight + filteredFeedbackRight * feedback;

        delayLeft.write(softClip(writeLeft * driveGain) / driveGain);
        delayRight.write(softClip(writeRight * driveGain) / driveGain);
        delayLeft.advance();
        delayRight.advance();

        float reverbLeft = 0.0f;
        float reverbRight = 0.0f;
        processReverbSample(
            effectInputLeft * 0.35f + delayedLeft * 0.45f,
            effectInputRight * 0.35f + delayedRight * 0.45f,
            reverbFeedback,
            dampingCoefficient,
            std::clamp(astroFrame.stereoMotion * astroAmount + reverbModDepth * 0.55f + libraWidth * 0.18f + aquariusMotion * 0.16f, 0.0f, 1.0f),
            reverbLeft,
            reverbRight);

        const float wetLeft = delayedLeft * delayLevel + reverbLeft * reverbLevel;
        const float wetRight = delayedRight * delayLevel + reverbRight * reverbLevel;
        const float mixedLeft = sanitize(inLeft * dryGain + tankTapDryLeft + wetLeft * wetGain);
        const float mixedRight = sanitize(inRight * dryGain + tankTapDryRight + wetRight * wetGain);
        const float lowLeft = masterLowLeft.process(mixedLeft, lowEqCoefficient);
        const float lowRight = masterLowRight.process(mixedRight, lowEqCoefficient);
        const float highLowPassedLeft = masterHighLeft.process(mixedLeft, highEqCoefficient);
        const float highLowPassedRight = masterHighRight.process(mixedRight, highEqCoefficient);
        const float highLeft = mixedLeft - highLowPassedLeft;
        const float highRight = mixedRight - highLowPassedRight;
        const float midLeft = mixedLeft - lowLeft - highLeft;
        const float midRight = mixedRight - lowRight - highRight;
        output.left[sample] = sanitize((lowLeft * lowEqGain + midLeft * midEqGain + highLeft * highEqGain) * outputGain);
        output.right[sample] = sanitize((lowRight * lowEqGain + midRight * midEqGain + highRight * highEqGain) * outputGain);

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
