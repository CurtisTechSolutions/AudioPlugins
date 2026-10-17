#pragma once

#include "Math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ap
{

/**
    Monophonic, formant-preserving pitch shifter based on pitch-synchronous
    overlap-add (PSOLA).

    The caller feeds samples with `process()` and, per block or per analysis
    frame, tells the shifter the input period (in samples) and the desired
    ratio (output f0 / input f0). Two-period Hann grains are taken from the
    input on a grid spaced by the input period and laid down on a grid spaced
    by (period / ratio). Because the grain keeps the input's spectral
    envelope, formants stay where they are.

    Output is delayed by `latencySamples()` relative to the input.
*/
class PsolaShifter
{
public:
    void prepare (double sampleRate, float minHz = 50.0f, int maxBlockSize = 4096)
    {
        sr = sampleRate;
        maxPeriod = (int) std::ceil (sampleRate / (double) std::max (minHz, 20.0f));
        maxGrainHalf = maxPeriod;

        // Output lags input far enough that every grain we may need is already written.
        latency = (int) std::ceil (2.5 * maxPeriod);

        int size = 1;
        while (size < latency + 4 * maxPeriod + maxBlockSize) size <<= 1;
        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;

        reset();
    }

    void reset() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
        readTime = -latency;          // output time lags input time
        nextSynth = 0;
        lastAnalysis = 0;
        analysisCount = 0;
        for (auto& g : grains) g.active = false;
        period = (float) (sr / 200.0);
        ratio = 1.0f;
        voiced = false;
    }

    int latencySamples() const noexcept { return latency; }

    /** Absolute input-sample index that the next output sample corresponds to
        (counts from prepare/reset; negative while the latency buffer fills). */
    int64_t outputTime() const noexcept { return readTime; }

    /** Input period in samples (0 or negative = unvoiced). */
    void setPeriod (float periodSamples) noexcept
    {
        if (periodSamples >= 2.0f)
        {
            period = std::min (periodSamples, (float) maxPeriod);
            voiced = true;
        }
        else
        {
            voiced = false;
        }
    }

    /** Desired f0 ratio: 2 = octave up, 0.5 = octave down. */
    void setRatio (float r) noexcept { ratio = clamp (r, 0.25f, 4.0f); }

    void process (const float* in, float* out, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            buffer[(size_t) (writePos & mask)] = in[i];
            ++writePos;

            const int64_t t = readTime;

            // Start any grain whose window begins at or before this output sample.
            const int64_t halfNow = (int64_t) std::lround (clamp (period, 2.0f, (float) maxPeriod));
            while (nextSynth - halfNow <= t)
                startGrain (nextSynth);

            float acc = 0.0f;
            for (auto& g : grains)
            {
                if (! g.active) continue;
                const int64_t rel = t - g.synthCentre;
                if (rel >= g.halfLen) { g.active = false; continue; }
                if (rel < -g.halfLen) continue;
                const float w = hannSymmetric ((float) rel, (float) g.halfLen);
                acc += w * g.gain * buffer[(size_t) ((g.analysisCentre + rel) & mask)];
            }
            out[i] = acc;
            ++readTime;
        }
    }

private:
    struct Grain
    {
        bool    active = false;
        int64_t analysisCentre = 0;
        int64_t synthCentre = 0;
        int     halfLen = 0;
        float   gain = 1.0f;
    };

    void startGrain (int64_t synthMark) noexcept
    {
        const float p = clamp (period, 2.0f, (float) maxPeriod);
        const int halfLen = std::max (2, std::min (maxGrainHalf, (int) std::lround (p)));

        int64_t analysis;
        float   gain;
        float   synthHop;

        if (voiced && std::fabs (ratio - 1.0f) > 1.0e-4f)
        {
            // Extend the analysis grid until it reaches past this synthesis mark.
            if (analysisCount == 0)
            {
                lastAnalysis = synthMark;
                analysisCount = 1;
            }
            while (lastAnalysis < synthMark)
            {
                lastAnalysis += (int64_t) std::lround (p);
                ++analysisCount;
            }
            // Nearest analysis mark: lastAnalysis or the one before it.
            const int64_t prev = lastAnalysis - (int64_t) std::lround (p);
            analysis = (lastAnalysis - synthMark) <= (synthMark - prev) ? lastAnalysis : prev;
            synthHop = p / ratio;
            gain = 1.0f / ratio;     // Hann grains of length 2p at hop p/ratio sum to `ratio`
        }
        else
        {
            // Unvoiced or unity: identity mapping, grains overlap-add back to the input.
            analysis = synthMark;
            lastAnalysis = synthMark;
            analysisCount = 1;
            synthHop = p;
            gain = 1.0f;
        }

        // Never read past what has been written.
        const int64_t newest = writePos - 1;
        if (analysis + halfLen > newest)
            analysis = newest - halfLen;

        Grain* slot = nullptr;
        for (auto& g : grains)
            if (! g.active) { slot = &g; break; }
        if (slot == nullptr) slot = &grains[0];

        slot->active = true;
        slot->analysisCentre = analysis;
        slot->synthCentre = synthMark;
        slot->halfLen = halfLen;
        slot->gain = gain;

        nextSynth = synthMark + std::max<int64_t> (1, (int64_t) std::lround (synthHop));
    }

    double sr = 44100.0;
    int    maxPeriod = 882, maxGrainHalf = 882, latency = 2205;
    std::vector<float> buffer;
    int    mask = 0;
    int64_t writePos = 0, readTime = 0, nextSynth = 0, lastAnalysis = 0;
    int    analysisCount = 0;
    float  period = 220.0f, ratio = 1.0f;
    bool   voiced = false;

    std::array<Grain, 12> grains {};
};

} // namespace ap
