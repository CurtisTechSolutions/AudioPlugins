#pragma once

#include "Math.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ap
{

/**
    Monophonic fundamental-frequency estimator (YIN: de Cheveigné & Kawahara, 2002).

    Works on a block of `requiredSamples()` consecutive samples: an integration
    window of `windowSize` samples plus `tauMax` samples of look-ahead.
    Tuned for voice: default range 50 Hz – 1500 Hz.
*/
class YinPitchDetector
{
public:
    struct Result
    {
        float frequency  = 0.0f;   // Hz, 0 when nothing usable was found
        float confidence = 0.0f;   // 0..1, 1 = perfectly periodic
        bool  voiced     = false;  // confidence passed the threshold
    };

    void prepare (double sampleRate, int windowSize = 1024, float minHz = 50.0f, float maxHz = 1500.0f)
    {
        sr      = sampleRate;
        window  = std::max (64, windowSize);
        tauMin  = std::max (2, (int) std::floor (sampleRate / (double) std::max (maxHz, 20.0f)));
        tauMax  = std::max (tauMin + 2, (int) std::ceil  (sampleRate / (double) std::max (minHz, 10.0f)));
        diff.assign ((size_t) tauMax + 1, 0.0f);
        cmnd.assign ((size_t) tauMax + 1, 1.0f);
    }

    /** Absolute threshold on the normalised difference; 0.1–0.2 is typical. Lower = stricter. */
    void setThreshold (float t) noexcept { threshold = clamp (t, 0.01f, 0.9f); }

    int requiredSamples() const noexcept { return window + tauMax; }
    int windowSize()      const noexcept { return window; }
    int maxLag()          const noexcept { return tauMax; }

    /** Sample index (within the analysed block) that the estimate best describes. */
    int analysisCentre() const noexcept { return (window + tauMax) / 2; }

    Result analyze (const float* x) noexcept
    {
        Result r;
        if (diff.empty()) return r;

        // Difference function d(tau) over the integration window.
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            float acc = 0.0f;
            const float* a = x;
            const float* b = x + tau;
            for (int j = 0; j < window; ++j)
            {
                const float d = a[j] - b[j];
                acc += d * d;
            }
            diff[(size_t) tau] = acc;
        }

        // Cumulative-mean-normalised difference d'(tau).
        cmnd[0] = 1.0f;
        float running = 0.0f;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            running += diff[(size_t) tau];
            cmnd[(size_t) tau] = running > 1.0e-12f ? diff[(size_t) tau] * (float) tau / running : 1.0f;
        }

        // Absolute threshold: first dip below the threshold, then walk to its local minimum.
        int best = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
        {
            if (cmnd[(size_t) tau] < threshold)
            {
                while (tau + 1 < tauMax && cmnd[(size_t) tau + 1] < cmnd[(size_t) tau])
                    ++tau;
                best = tau;
                break;
            }
        }

        bool voiced = best >= 0;
        if (! voiced)
        {
            // Fall back to the global minimum so callers still get a (low-confidence) estimate.
            best = tauMin;
            for (int tau = tauMin + 1; tau < tauMax; ++tau)
                if (cmnd[(size_t) tau] < cmnd[(size_t) best]) best = tau;
        }

        // Parabolic interpolation around the chosen lag.
        float refined = (float) best;
        if (best > tauMin && best + 1 < tauMax)
        {
            const float s0 = cmnd[(size_t) best - 1];
            const float s1 = cmnd[(size_t) best];
            const float s2 = cmnd[(size_t) best + 1];
            const float denom = s0 - 2.0f * s1 + s2;
            if (std::fabs (denom) > 1.0e-9f)
                refined = (float) best + 0.5f * (s0 - s2) / denom;
        }

        const float minVal = cmnd[(size_t) best];
        r.confidence = clamp (1.0f - minVal, 0.0f, 1.0f);
        r.frequency  = refined > 0.0f ? (float) (sr / (double) refined) : 0.0f;
        r.voiced     = voiced && r.frequency > 0.0f;
        return r;
    }

private:
    double sr        = 44100.0;
    int    window    = 1024;
    int    tauMin    = 29;
    int    tauMax    = 882;
    float  threshold = 0.15f;

    std::vector<float> diff, cmnd;
};

} // namespace ap
