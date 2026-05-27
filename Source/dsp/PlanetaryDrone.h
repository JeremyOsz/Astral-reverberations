#pragma once

#include "astro/AstroTypes.h"
#include "dsp/AstralReverbDelay.h"

#include <array>
#include <vector>

namespace astral::dsp {

struct PlanetaryDroneParameters {
    float droneLevel = 0.0f;
    float captureLevel = 0.0f;
    float harmonicSpread = 0.65f;
    float aspectDepth = 0.5f;
    float rootFrequencyHz = 110.0f;
    bool gate = false;
    bool captureEnabled = false;
};

class PlanetaryDrone {
public:
    void prepare(double newSampleRate, int maximumExpectedBlockSize);
    void reset();
    void resetCapture();

    void processBlock(
        const StereoBufferView& input,
        const StereoBufferView& output,
        const PlanetaryDroneParameters& parameters,
        const astro::AstroDroneFrame& frame);

private:
    static constexpr std::size_t kLayerCount = static_cast<std::size_t>(astro::PlanetId::Count);

    struct CaptureBuffer {
        std::vector<float> left;
        std::vector<float> right;
        int writeIndex = 0;
        bool hasSignal = false;

        void prepare(int sampleCount);
        void reset();
        void write(float leftSample, float rightSample);
        float readLeft(float position01) const;
        float readRight(float position01) const;
    };

    double sampleRate = 44100.0;
    std::array<float, kLayerCount> phases{};
    std::array<float, kLayerCount> beatPhases{};
    CaptureBuffer capture;
    float gateEnvelope = 0.0f;

    static float readInterpolated(const std::vector<float>& buffer, float index);
};

} // namespace astral::dsp
