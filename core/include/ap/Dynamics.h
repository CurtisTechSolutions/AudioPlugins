#pragma once

#include "Math.h"

#include <cmath>

namespace ap
{

/** Peak / RMS envelope follower with separate attack and release, output is linear level. */
class EnvelopeFollower
{
public:
    enum class Mode { Peak, Rms };

    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate;
        setTimes (attackMs, releaseMs);
        reset();
    }

    void reset() noexcept { env = 0.0f; rmsState = 0.0f; }

    void setMode (Mode m) noexcept { mode = m; }

    void setTimes (float attack, float release) noexcept
    {
        attackMs  = attack;
        releaseMs = release;
        aCoeff = onePoleCoeff (attack, sr);
        rCoeff = onePoleCoeff (release, sr);
        // RMS window ~ twice the attack time, minimum 1 ms.
        rmsCoeff = onePoleCoeff (std::max (1.0f, 2.0f * attack), sr);
    }

    inline float process (float x) noexcept
    {
        float level;
        if (mode == Mode::Rms)
        {
            rmsState = x * x + rmsCoeff * (rmsState - x * x);
            level = std::sqrt (rmsState);
        }
        else
        {
            level = std::fabs (x);
        }

        const float c = level > env ? aCoeff : rCoeff;
        env = level + c * (env - level);
        return env;
    }

    float current() const noexcept { return env; }

private:
    double sr = 44100.0;
    Mode   mode = Mode::Peak;
    float  attackMs = 10.0f, releaseMs = 100.0f;
    float  aCoeff = 0.0f, rCoeff = 0.0f, rmsCoeff = 0.0f;
    float  env = 0.0f, rmsState = 0.0f;
};

/**
    Static compressor gain computer with a soft knee.
    Input/output in dB; returns gain reduction (<= 0 dB) for a given level.
*/
struct GainComputer
{
    float thresholdDb = -20.0f;
    float ratio       = 4.0f;      // >= 1; very large = limiter
    float kneeDb      = 6.0f;

    float gainReductionDb (float levelDb) const noexcept
    {
        const float over = levelDb - thresholdDb;
        const float slope = 1.0f - 1.0f / std::max (1.0f, ratio);
        if (kneeDb <= 0.0f || over <= -kneeDb * 0.5f)
            return over <= 0.0f ? 0.0f : -slope * over;

        if (over >= kneeDb * 0.5f)
            return -slope * over;

        // Inside the knee: quadratic interpolation between the two linear pieces.
        const float x = over + kneeDb * 0.5f;
        return -slope * (x * x) / (2.0f * kneeDb);
    }
};

/** Sum-to-average helper for stereo detection. */
inline float stereoMax (float l, float r) noexcept { return std::max (std::fabs (l), std::fabs (r)); }

} // namespace ap
