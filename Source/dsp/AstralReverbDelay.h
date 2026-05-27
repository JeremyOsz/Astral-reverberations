#pragma once

#include "astro/AstroTypes.h"

#include <array>
#include <vector>

namespace astral::dsp {

struct AstralParameters {
    float mix = 0.35f;
    float delayLevel = 0.45f;
    float reverbLevel = 0.55f;
    float delayTimeMs = 420.0f;
    float feedback = 0.42f;
    float space = 0.55f;
    float tone = 0.55f;
    float modDepth = 0.35f;
    float wowFlutter = 0.25f;
    float drive = 0.15f;
    float astroAmount = 0.5f;
    float astroSpeed = 1.0f;
    bool freeze = false;
    float outputGain = 1.0f;
};

struct StereoBufferView {
    float* left = nullptr;
    float* right = nullptr;
    int numSamples = 0;
};

class AstralReverbDelay {
public:
    void prepare(double newSampleRate, int maximumExpectedBlockSize);
    void reset();
    void processBlock(
        const StereoBufferView& input,
        const StereoBufferView& output,
        const AstralParameters& parameters,
        const astro::AstroModulationFrame& astroFrame);

private:
    struct DelayBuffer {
        std::vector<float> samples;
        int writeIndex = 0;

        void prepare(int sampleCount);
        void reset();
        float read(float delaySamples) const;
        void write(float value);
        void advance();
    };

    struct OnePoleLowPass {
        float state = 0.0f;

        void reset();
        float process(float input, float coefficient);
    };

    static constexpr int kReverbLineCount = 4;

    double sampleRate = 44100.0;
    int maxBlockSize = 0;
    DelayBuffer delayLeft;
    DelayBuffer delayRight;
    std::array<DelayBuffer, kReverbLineCount> reverbLinesLeft;
    std::array<DelayBuffer, kReverbLineCount> reverbLinesRight;
    OnePoleLowPass feedbackFilterLeft;
    OnePoleLowPass feedbackFilterRight;
    std::array<OnePoleLowPass, kReverbLineCount> dampingFiltersLeft;
    std::array<OnePoleLowPass, kReverbLineCount> dampingFiltersRight;
    float smoothedDelaySamples = 1.0f;
    float wowPhase = 0.0f;
    float flutterPhase = 0.0f;
    float reverbModPhase = 0.0f;

    void processReverbSample(
        float inputLeft,
        float inputRight,
        float feedback,
        float dampingCoefficient,
        float stereoMotion,
        float& outputLeft,
        float& outputRight);
};

} // namespace astral::dsp

