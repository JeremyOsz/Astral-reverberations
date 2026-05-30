#pragma once

#include "astro/AstroTypes.h"
#include "dsp/AstralReverbDelay.h"

#include <array>
#include <vector>

namespace astral::dsp {

struct PlanetaryDroneParameters {
    static constexpr std::size_t kManualLayerCount = static_cast<std::size_t>(astro::PlanetId::Count);

    float droneLevel = 0.0f;
    float captureLevel = 0.0f;
    float harmonicSpread = 0.65f;
    float aspectDepth = 0.5f;
    float rootFrequencyHz = 110.0f;
    float tailSize = 0.0f;
    float tailRegen = 0.0f;
    int tailMode = 0;
    bool gate = false;
    bool organMode = false;
    bool captureEnabled = false;
    std::array<int, kManualLayerCount> waveShapes{};
    std::array<float, kManualLayerCount> filterCutoffs{{0.82f, 0.82f, 0.82f, 0.82f, 0.82f, 0.82f, 0.82f, 0.82f, 0.82f, 0.82f}};
    std::array<bool, kManualLayerCount> manualLayerGates{};
    std::array<bool, kManualLayerCount> organLayerGates{};
    std::array<bool, kManualLayerCount> mutedLayers{};
};

/// Planetary harmonic drone plus captured input scan and orbit-tail buffers.
class PlanetaryDrone {
public:
    static constexpr std::size_t kLayerCount = static_cast<std::size_t>(astro::PlanetId::Count);

    /// Allocates capture and tail buffers for the active sample rate.
    void prepare(double newSampleRate, int maximumExpectedBlockSize);
    /// Clears oscillator phases, capture storage, tail buffers, meters, and gate state.
    void reset();
    /// Clears only the short captured-input scan buffer.
    void resetCapture();

    /// Renders drone oscillators, captured buffer scan, and keyed orbit tails into the output buffer.
    void processBlock(
        const StereoBufferView& input,
        const StereoBufferView& output,
        const StereoBufferView& captureInput,
        const PlanetaryDroneParameters& parameters,
        const astro::AstroDroneFrame& frame);
    /// Returns smoothed per-planet tail energy for UI feedback.
    std::array<float, kLayerCount> getTailLevels() const { return tailLevels; }

private:
    struct CaptureBuffer {
        std::vector<float> left;
        std::vector<float> right;
        int writeIndex = 0;
        bool hasSignal = false;

        /// Allocates the circular capture buffer.
        void prepare(int sampleCount);
        /// Clears captured audio and signal state.
        void reset();
        /// Writes one stereo input sample into the circular buffer.
        void write(float leftSample, float rightSample);
        /// Reads the left channel at a normalized tape-head position.
        float readLeft(float position01) const;
        /// Reads the right channel at a normalized tape-head position.
        float readRight(float position01) const;
    };

    struct ManualTailBuffer {
        std::vector<float> left;
        std::vector<float> right;
        int writeIndex = 0;
        float envelope = 0.0f;
        float displayLevel = 0.0f;
        float smoothedDelaySamples = 0.0f;
        float shimmerDcLeft = 0.0f;
        float shimmerDcRight = 0.0f;

        /// Allocates the per-planet feedback tail buffer.
        void prepare(int sampleCount);
        /// Clears buffered tail audio and envelope state.
        void reset();
        /// Processes one sample of a keyed orbit tail and returns left output, writing right output by reference.
        float process(float inputLeft, float inputRight, float tailSeconds, float level, float pan, bool active, float tailRegen, int tailMode, float& rightOut, double sampleRate);
        /// Returns true while the tail is being excited or still audibly decaying.
        bool isReplacingLayer() const noexcept { return envelope > 0.0008f; }
        /// Returns a smooth UI level that tracks captured energy rather than sparse delay taps.
        float getDisplayLevel() const noexcept { return displayLevel; }
    };

    double sampleRate = 44100.0;
    std::array<float, kLayerCount> phases{};
    std::array<float, kLayerCount> beatPhases{};
    std::array<float, kLayerCount> voiceFilterStates{};
    std::array<float, kLayerCount> tailLevels{};
    std::array<ManualTailBuffer, kLayerCount> manualTails{};
    CaptureBuffer capture;
    float gateEnvelope = 0.0f;

    /// Reads from any float buffer using linear interpolation.
    static float readInterpolated(const std::vector<float>& buffer, float index);
};

} // namespace astral::dsp
