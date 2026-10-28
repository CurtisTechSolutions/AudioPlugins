#include "Filter.h"

#include <cmath>

namespace verdant
{

namespace
{
    bool isLowPass (FilterType t) noexcept
    {
        return t == FilterType::LP12 || t == FilterType::LP24 || t == FilterType::Ladder;
    }
}

void VoiceFilter::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    cutoff = resonance = -1.0f;
    reset();
}

void VoiceFilter::reset()
{
    left.reset();
    right.reset();
}

void VoiceFilter::set (FilterType t, float cutoffHz, float resonance01, float)
{
    if (t != type)
    {
        type = t;
        cutoff = -1.0f;   // force a coefficient update
    }

    if (! isLowPass (type))
        return;

    if (std::fabs (cutoffHz - cutoff) > 1.0e-3f || std::fabs (resonance01 - resonance) > 1.0e-4f)
    {
        cutoff = cutoffHz;
        resonance = resonance01;
        const float q = 0.7071f + 4.0f * resonance01 * resonance01;
        left.configure (ap::Biquad::Type::LowPass, sr, cutoffHz, q);
        right.configure (ap::Biquad::Type::LowPass, sr, cutoffHz, q);
    }
}

void VoiceFilter::process (float* L, float* R, int numSamples)
{
    if (! isLowPass (type))
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        L[i] = left.process (L[i]);
        R[i] = right.process (R[i]);
    }
}

float filterMagnitudeDb (FilterType, float, float, double, float)
{
    return 0.0f;
}

} // namespace verdant
