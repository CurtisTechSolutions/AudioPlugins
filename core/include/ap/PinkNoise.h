#pragma once

#include "Math.h"

#include <array>
#include <cmath>

namespace ap
{

/** Audio-rate pink (1/f) noise, Paul Kellet's economy filter on white noise. Output roughly ±1. */
class PinkNoise
{
public:
    explicit PinkNoise (uint32_t seed = 0x1234567u) noexcept { rng.state = seed; }

    void reset (uint32_t seed) noexcept { rng.state = seed; b0 = b1 = b2 = 0.0f; }

    inline float next() noexcept
    {
        const float white = rng.nextBipolar();
        b0 = 0.99765f * b0 + white * 0.0990460f;
        b1 = 0.96300f * b1 + white * 0.2965164f;
        b2 = 0.57000f * b2 + white * 1.0526913f;
        return (b0 + b1 + b2 + white * 0.1848f) * 0.25f;
    }

private:
    Rng rng;
    float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;
};

/**
    Slow 1/f drift for control signals (0.05–2 Hz region): Voss–McCartney
    octave-banded random walk, smoothed. Output stays within about ±1, is
    strongly autocorrelated over seconds, and has pink-ish spectral tilt.
    Call next() once per control step (e.g. every 64–512 audio samples).
*/
class DriftSource
{
public:
    static constexpr int kRows = 7;

    explicit DriftSource (uint32_t seed = 0xA5A5A5A5u) noexcept { reset (seed); }

    /** `controlRateHz` = how often next() is called; `fastestHz` = the highest
        row's update rate (default 2 Hz; lower rows are octaves below). */
    void prepare (double controlRateHz, double fastestHz = 2.0) noexcept
    {
        rate = controlRateHz;
        fastest = fastestHz;
        for (int r = 0; r < kRows; ++r)
            period[(size_t) r] = std::max (1, (int) std::lround (controlRateHz / (fastestHz / std::pow (2.0, r))));
        // Smooth the stair-steps away: one-pole at the fastest rate.
        smoothCoeff = (float) std::exp (-2.0 * kPiD * fastestHz / controlRateHz);
    }

    void reset (uint32_t seed) noexcept
    {
        rng.state = seed | 1u;
        for (int r = 0; r < kRows; ++r)
        {
            rows[(size_t) r] = rng.nextBipolar();
            counters[(size_t) r] = 0;
        }
        smoothed = 0.0f;
    }

    inline float next() noexcept
    {
        float sum = 0.0f;
        for (int r = 0; r < kRows; ++r)
        {
            if (++counters[(size_t) r] >= period[(size_t) r])
            {
                counters[(size_t) r] = 0;
                rows[(size_t) r] = rng.nextBipolar();
            }
            sum += rows[(size_t) r];
        }
        const float target = sum / (float) kRows * 1.6f;   // rough normalisation to ±1
        smoothed = target + smoothCoeff * (smoothed - target);
        return clamp (smoothed, -1.0f, 1.0f);
    }

private:
    Rng rng;
    double rate = 100.0, fastest = 2.0;
    std::array<float, kRows> rows {};
    std::array<int, kRows> counters {}, period {};
    float smoothCoeff = 0.9f, smoothed = 0.0f;
};

} // namespace ap
