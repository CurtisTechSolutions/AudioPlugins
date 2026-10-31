#pragma once

#include "Math.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace ap
{

/** Transposed direct-form II biquad with RBJ cookbook coefficient setters. */
class Biquad
{
public:
    enum class Type { LowPass, HighPass, BandPass, Notch, Peak, LowShelf, HighShelf, AllPass };

    void reset() noexcept { z1 = z2 = 0.0f; }

    void setCoefficients (float b0_, float b1_, float b2_, float a0, float a1_, float a2_) noexcept
    {
        const float inv = 1.0f / a0;
        b0 = b0_ * inv; b1 = b1_ * inv; b2 = b2_ * inv;
        a1 = a1_ * inv; a2 = a2_ * inv;
    }

    /** gainDb only matters for Peak / shelves. */
    void configure (Type type, double sampleRate, float freqHz, float q, float gainDb = 0.0f) noexcept
    {
        freqHz = clamp (freqHz, 10.0f, (float) (sampleRate * 0.49));
        q = std::max (0.05f, q);
        const float w0 = kTwoPi * freqHz / (float) sampleRate;
        const float cw = std::cos (w0), sw = std::sin (w0);
        const float alpha = sw / (2.0f * q);
        const float A = std::pow (10.0f, gainDb / 40.0f);

        switch (type)
        {
            case Type::LowPass:
                setCoefficients ((1 - cw) * 0.5f, 1 - cw, (1 - cw) * 0.5f, 1 + alpha, -2 * cw, 1 - alpha);
                break;
            case Type::HighPass:
                setCoefficients ((1 + cw) * 0.5f, -(1 + cw), (1 + cw) * 0.5f, 1 + alpha, -2 * cw, 1 - alpha);
                break;
            case Type::BandPass:
                setCoefficients (alpha, 0.0f, -alpha, 1 + alpha, -2 * cw, 1 - alpha);
                break;
            case Type::Notch:
                setCoefficients (1.0f, -2 * cw, 1.0f, 1 + alpha, -2 * cw, 1 - alpha);
                break;
            case Type::AllPass:
                setCoefficients (1 - alpha, -2 * cw, 1 + alpha, 1 + alpha, -2 * cw, 1 - alpha);
                break;
            case Type::Peak:
                setCoefficients (1 + alpha * A, -2 * cw, 1 - alpha * A, 1 + alpha / A, -2 * cw, 1 - alpha / A);
                break;
            case Type::LowShelf:
            {
                const float sqA2a = 2.0f * std::sqrt (A) * alpha;
                setCoefficients (A * ((A + 1) - (A - 1) * cw + sqA2a),
                                 2 * A * ((A - 1) - (A + 1) * cw),
                                 A * ((A + 1) - (A - 1) * cw - sqA2a),
                                 (A + 1) + (A - 1) * cw + sqA2a,
                                 -2 * ((A - 1) + (A + 1) * cw),
                                 (A + 1) + (A - 1) * cw - sqA2a);
                break;
            }
            case Type::HighShelf:
            {
                const float sqA2a = 2.0f * std::sqrt (A) * alpha;
                setCoefficients (A * ((A + 1) + (A - 1) * cw + sqA2a),
                                 -2 * A * ((A - 1) + (A + 1) * cw),
                                 A * ((A + 1) + (A - 1) * cw - sqA2a),
                                 (A + 1) - (A - 1) * cw + sqA2a,
                                 2 * ((A - 1) - (A + 1) * cw),
                                 (A + 1) - (A - 1) * cw - sqA2a);
                break;
            }
        }
    }

    inline float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    /** Magnitude response in dB at `hz` for the current coefficients (for response displays). */
    float magnitudeDb (double hz, double sampleRate) const noexcept
    {
        const std::complex<double> z1inv = std::polar (1.0, -kTwoPiD * hz / sampleRate);
        const std::complex<double> z2inv = z1inv * z1inv;
        const auto h = ((double) b0 + (double) b1 * z1inv + (double) b2 * z2inv)
                     / (1.0 + (double) a1 * z1inv + (double) a2 * z2inv);
        return (float) (20.0 * std::log10 (std::max (1.0e-9, std::abs (h))));
    }

private:
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;
};

/** One-pole parameter smoother (exponential). */
class Smoother
{
public:
    void prepare (double sampleRate, float timeMs) noexcept
    {
        coeff = onePoleCoeff (timeMs, sampleRate);
    }

    void setTarget (float t) noexcept    { target = t; }
    void snap (float v) noexcept         { target = v; current = v; }
    float getTarget() const noexcept     { return target; }
    float getCurrent() const noexcept    { return (float) current; }

    inline float next() noexcept
    {
        // Double state: in float the step eventually rounds to zero and the
        // smoother stalls a few 1e-5 short of the target.
        current = (double) target + coeff * (current - (double) target);
        return (float) current;
    }

    bool isSmoothing() const noexcept { return std::fabs (current - (double) target) > 1.0e-6; }

private:
    double coeff = 0.0, current = 0.0;
    float  target = 0.0f;
};

} // namespace ap
