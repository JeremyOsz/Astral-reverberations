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
    float reverbDecay = 0.5f;
    float reverbDamping = 0.5f;
    float reverbModDepth = 0.35f;
    float pluckSend = 1.0f;
    float modDepth = 0.35f;
    float wowFlutter = 0.25f;
    float drive = 0.15f;
    float astroAmount = 0.5f;
    float astroSpeed = 1.0f;
    float reverbTankTapLevel = 0.0f;
    float reverbTankTapPan = 0.0f;
    float reverbTankTapTone = 0.45f;
    float reverbTankTapFrequencyHz = 220.0f;
    float reverbTankTapWet = 1.0f;
    float pluckLevel = 1.0f;
    float pluckTimbre = 0.5f;
    bool freeze = false;
    bool inputMonitor = false;
    float eqLowGainDb = 0.0f;
    float eqMidGainDb = 0.0f;
    float eqHighGainDb = 0.0f;
    int filterRoute = 0;
    int filterMode = 0;
    float filterCutoffHz = 1200.0f;
    float filterResonance = 0.25f;
    float filterRadiate = 0.35f;
    float outputGain = 1.0f;
};

struct StereoBufferView {
    float* left = nullptr;
    float* right = nullptr;
    int numSamples = 0;
};

/// Stereo delay/reverb core that applies astro-derived modulation, node taps, and sign effects.
class AstralReverbDelay {
public:
    /// Allocates delay/reverb storage for a sample rate and maximum expected block size.
    void prepare(double newSampleRate, int maximumExpectedBlockSize);
    /// Clears all delay, reverb, filter, and modulation state.
    void reset();
    /// Processes a stereo block from input to output using normal parameters plus astro modulation.
    void processBlock(
        const StereoBufferView& input,
        const StereoBufferView& output,
        const AstralParameters& parameters,
        const astro::AstroModulationFrame& astroFrame);

private:
    struct DelayBuffer {
        std::vector<float> samples;
        int writeIndex = 0;

        /// Allocates a circular delay line with at least two samples.
        void prepare(int sampleCount);
        /// Clears stored samples and returns the write head to the start.
        void reset();
        /// Reads a fractional delay in samples using linear interpolation.
        float read(float delaySamples) const;
        /// Writes one sanitized sample at the current write head.
        void write(float value);
        /// Advances the circular write head by one sample.
        void advance();
    };

    struct OnePoleLowPass {
        float state = 0.0f;

        /// Clears the filter state.
        void reset();
        /// Processes one sample using a normalized one-pole coefficient.
        float process(float input, float coefficient);
    };

    struct KarplusPluck {
        std::vector<float> samples;
        int size = 0;
        int index = 0;
        float level = 0.0f;
        float feedback = 0.0f;

        void prepare(int maxSamples);
        void reset();
        void trigger(float newLevel, float tone, float frequencyHz, double sampleRate);
        float process();
    };

    struct MultimodeFilterCore {
        float ic1eq = 0.0f;
        float ic2eq = 0.0f;

        void reset();
        float process(float input, float cutoffHz, float resonance, int mode, double sampleRate);
    };

    struct StereoMultimodeFilter {
        MultimodeFilterCore leftA;
        MultimodeFilterCore leftB;
        MultimodeFilterCore rightA;
        MultimodeFilterCore rightB;

        void reset();
        void process(
            float inputLeft,
            float inputRight,
            float cutoffHz,
            float resonance,
            float radiate,
            int mode,
            double sampleRate,
            float& outputLeft,
            float& outputRight);
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
    OnePoleLowPass masterLowLeft;
    OnePoleLowPass masterLowRight;
    OnePoleLowPass masterHighLeft;
    OnePoleLowPass masterHighRight;
    StereoMultimodeFilter outputFilter;
    KarplusPluck reverbTankPluck;
    std::array<astro::AstroModulationFrame::DelayTap, static_cast<std::size_t>(astro::PlanetId::Count)> smoothedDelayTaps{};
    float smoothedDelaySamples = 1.0f;
    float wowPhase = 0.0f;
    float flutterPhase = 0.0f;
    float reverbModPhase = 0.0f;
    bool delayTapsInitialized = false;

    /// Processes the internal four-line reverb network for one stereo sample.
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
