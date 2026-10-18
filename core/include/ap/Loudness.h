#pragma once

#include "Filters.h"
#include "Math.h"

#include <array>
#include <cmath>
#include <vector>

namespace ap
{

/**
    ITU-R BS.1770 loudness meter (K-weighting + mean square) giving momentary
    (400 ms) and short-term (3 s) LUFS for mono or stereo. Integrated loudness
    gating is intentionally left out: the plugins only need running values.

    Resolution is 10 ms; results are updated once per 10 ms bin.
*/
class LoudnessMeter
{
public:
    void prepare (double sampleRate, int numChannels) noexcept
    {
        sr = sampleRate;
        channels = numChannels < 1 ? 1 : (numChannels > 2 ? 2 : numChannels);
        binSize = (int) std::lround (sampleRate * 0.010);
        for (int ch = 0; ch < 2; ++ch)
        {
            // Stage 1: high shelf (+~4 dB above ~1.7 kHz). Stage 2: RLB high-pass.
            shelf[(size_t) ch].configure (Biquad::Type::HighShelf, sampleRate, 1681.97f, 0.7071752f, 3.999843f);
            hpf[(size_t) ch].configure (Biquad::Type::HighPass, sampleRate, 38.13547f, 0.5003270f);
        }
        momentary.assign (40, 0.0);     // 40 x 10 ms
        shortTerm.assign (300, 0.0);    // 300 x 10 ms
        reset();
    }

    void reset() noexcept
    {
        for (auto& f : shelf) f.reset();
        for (auto& f : hpf) f.reset();
        std::fill (momentary.begin(), momentary.end(), 0.0);
        std::fill (shortTerm.begin(), shortTerm.end(), 0.0);
        momSum = shortSum = 0.0;
        momPos = shortPos = 0;
        binAcc = 0.0;
        binCount = 0;
        filledBins = 0;
    }

    /** Push a block; `right` may be nullptr for mono. */
    void process (const float* left, const float* right, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float l = hpf[0].process (shelf[0].process (left[i]));
            double ms = (double) l * l;
            if (right != nullptr && channels > 1)
            {
                const float r = hpf[1].process (shelf[1].process (right[i]));
                ms += (double) r * r;
            }
            binAcc += ms;
            if (++binCount >= binSize)
            {
                const double binMean = binAcc / (double) binSize;
                momSum   += binMean - momentary[(size_t) momPos];
                momentary[(size_t) momPos] = binMean;
                momPos = (momPos + 1) % (int) momentary.size();
                shortSum += binMean - shortTerm[(size_t) shortPos];
                shortTerm[(size_t) shortPos] = binMean;
                shortPos = (shortPos + 1) % (int) shortTerm.size();
                binAcc = 0.0;
                binCount = 0;
                if (filledBins < (int) shortTerm.size()) ++filledBins;
            }
        }
    }

    /** LUFS over the last 400 ms (−inf-ish floor at -120). */
    float momentaryLufs() const noexcept   { return toLufs (momSum / (double) momentary.size()); }
    /** LUFS over the last 3 s. */
    float shortTermLufs() const noexcept   { return toLufs (shortSum / (double) shortTerm.size()); }
    /** True once at least 400 ms have been measured. */
    bool hasMomentary() const noexcept     { return filledBins >= (int) momentary.size(); }
    bool hasShortTerm() const noexcept     { return filledBins >= (int) shortTerm.size(); }

private:
    static float toLufs (double meanSquare) noexcept
    {
        if (meanSquare <= 1.0e-12) return -120.0f;
        return (float) (-0.691 + 10.0 * std::log10 (meanSquare));
    }

    double sr = 48000.0;
    int channels = 2, binSize = 480;
    std::array<Biquad, 2> shelf, hpf;
    std::vector<double> momentary, shortTerm;
    double momSum = 0.0, shortSum = 0.0, binAcc = 0.0;
    int momPos = 0, shortPos = 0, binCount = 0, filledBins = 0;
};

} // namespace ap
