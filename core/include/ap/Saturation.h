#pragma once

#include "Math.h"

#include <cmath>

namespace ap
{

/** Removes the DC that asymmetric waveshapers introduce. ~5 Hz corner. */
class DcBlocker
{
public:
    void prepare (double sampleRate) noexcept
    {
        r = (float) std::exp (-2.0 * kPiD * 5.0 / sampleRate);
        reset();
    }
    void reset() noexcept { x1 = y1 = 0.0f; }
    inline float process (float x) noexcept
    {
        const float y = x - x1 + r * y1;
        x1 = x; y1 = y;
        return y;
    }
private:
    float r = 0.999f, x1 = 0.0f, y1 = 0.0f;
};

/**
    "Tube" shaper: tanh with a bias so the transfer curve is asymmetric,
    which produces even harmonics (2nd dominant) as well as odd. Output is
    normalised so a small signal passes at unity gain for any drive.
    Follow with a DcBlocker.
*/
struct TubeShaper
{
    float drive = 1.0f;   // >= 1
    float bias  = 0.5f;   // 0 = symmetric (odd only); 0.5 keeps the 2nd harmonic above the 3rd up to drive ~3

    inline float process (float x) const noexcept
    {
        const float d = std::max (1.0f, drive);
        // Slope at the origin of tanh(d x + b) is d * (1 - tanh(b)^2); divide it out.
        const float tb = std::tanh (bias);
        const float slope = d * (1.0f - tb * tb);
        return (std::tanh (d * x + bias) - tb) / slope;
    }
};

/**
    Exciter shaper (Aphex-style): x + k·x² then a soft clip. The squared term
    is even, so a single high-frequency partial gains a strong 2nd harmonic
    (plus DC, which the caller's high-pass removes). Unity gain for small signals.
*/
struct ExciterShaper
{
    float drive = 1.0f;   // >= 1
    float even  = 0.4f;   // weight of the x² term

    inline float process (float x) const noexcept
    {
        const float d = std::max (1.0f, drive);
        const float u = d * x;
        const float v = u + even * u * u;
        return fastTanh (v) / d;
    }
};

/** Plain soft clipper with unity small-signal gain. */
inline float softClip (float x, float drive) noexcept
{
    const float d = std::max (1.0f, drive);
    return fastTanh (d * x) / d;
}

} // namespace ap
