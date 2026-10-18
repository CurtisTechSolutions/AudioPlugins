#pragma once

#include "Filters.h"
#include "Math.h"

#include <cmath>

namespace ap
{

/**
    4th-order Linkwitz-Riley crossover (two cascaded 2nd-order Butterworth
    sections per band). low + high sums to an all-pass response: flat
    magnitude, with the phase of a 2nd-order all-pass at fc.
*/
class LinkwitzRiley4
{
public:
    void prepare (double sampleRate, float crossoverHz) noexcept
    {
        sr = sampleRate;
        setFrequency (crossoverHz);
        reset();
    }

    void setFrequency (float hz) noexcept
    {
        fc = hz;
        const float q = 0.70710678f;   // Butterworth; two cascaded = LR4
        lp1.configure (Biquad::Type::LowPass,  sr, hz, q);
        lp2.configure (Biquad::Type::LowPass,  sr, hz, q);
        hp1.configure (Biquad::Type::HighPass, sr, hz, q);
        hp2.configure (Biquad::Type::HighPass, sr, hz, q);
        ap1.configure (Biquad::Type::AllPass,  sr, hz, q);
        ap2.configure (Biquad::Type::AllPass,  sr, hz, q);
    }

    float frequency() const noexcept { return fc; }

    void reset() noexcept { lp1.reset(); lp2.reset(); hp1.reset(); hp2.reset(); ap1.reset(); ap2.reset(); }

    inline void process (float x, float& low, float& high) noexcept
    {
        low  = lp2.process (lp1.process (x));
        high = hp2.process (hp1.process (x));
    }

    /** The all-pass with the same phase response as low+high; use it to phase-align
        another band that bypasses this crossover. */
    inline float allpass (float x) noexcept { return ap2.process (ap1.process (x)); }

private:
    double sr = 44100.0;
    float fc = 1000.0f;
    Biquad lp1, lp2, hp1, hp2, ap1, ap2;
};

/**
    Phase-coherent 3-band splitter: low < f1 < mid < f2 < high.
    low + mid + high reconstructs the input through an all-pass (flat magnitude).
*/
class ThreeBandSplitter
{
public:
    void prepare (double sampleRate, float lowHz, float highHz) noexcept
    {
        x1.prepare (sampleRate, lowHz);
        x2.prepare (sampleRate, highHz);
    }

    void setFrequencies (float lowHz, float highHz) noexcept
    {
        x1.setFrequency (lowHz);
        x2.setFrequency (highHz);
    }

    void reset() noexcept { x1.reset(); x2.reset(); }

    inline void process (float x, float& low, float& mid, float& high) noexcept
    {
        float lo, rest;
        x1.process (x, lo, rest);
        x2.process (rest, mid, high);
        low = x2.allpass (lo);   // match the phase shift the mid/high path picked up at f2
    }

private:
    LinkwitzRiley4 x1, x2;
};

} // namespace ap
