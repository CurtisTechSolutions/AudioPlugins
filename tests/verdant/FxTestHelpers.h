#pragma once

// Small analysis helpers shared by test_fx.cpp and test_sympathetic.cpp (W-G).

#include "engine/FFT.h"

#include <ap/Math.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace fxtest
{

inline double rms (const std::vector<float>& v, size_t from, size_t to)
{
    to = std::min (to, v.size());
    if (to <= from) return 0.0;
    double acc = 0.0;
    for (size_t i = from; i < to; ++i) acc += (double) v[i] * v[i];
    return std::sqrt (acc / (double) (to - from));
}

inline double peakAbs (const std::vector<float>& v, size_t from = 0, size_t to = (size_t) -1)
{
    to = std::min (to, v.size());
    double p = 0.0;
    for (size_t i = from; i < to; ++i) p = std::max (p, (double) std::fabs (v[i]));
    return p;
}

inline bool allFinite (const std::vector<float>& v)
{
    for (float x : v) if (! std::isfinite (x)) return false;
    return true;
}

/** Largest |v[i] - v[i-1]| in [from, to). */
inline double maxStep (const std::vector<float>& v, size_t from = 1, size_t to = (size_t) -1)
{
    to = std::min (to, v.size());
    double s = 0.0;
    for (size_t i = std::max<size_t> (from, 1); i < to; ++i) s = std::max (s, (double) std::fabs (v[i] - v[i - 1]));
    return s;
}

inline double db (double ratio) { return 20.0 * std::log10 (std::max (ratio, 1.0e-12)); }

/** Amplitude of the sinusoid at `hz` in v[from, to), Hann-windowed Goertzel (amplitude-calibrated). */
inline double toneAmp (const std::vector<float>& v, size_t from, size_t to, double hz, double sampleRate)
{
    to = std::min (to, v.size());
    if (to <= from + 2) return 0.0;
    const size_t n = to - from;
    const double w = ap::kTwoPiD * hz / sampleRate;
    const double c = 2.0 * std::cos (w);
    double s1 = 0.0, s2 = 0.0, wsum = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double win = 0.5 - 0.5 * std::cos (ap::kTwoPiD * (double) i / (double) (n - 1));
        wsum += win;
        const double s0 = (double) v[from + i] * win + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double power = std::max (0.0, s1 * s1 + s2 * s2 - c * s1 * s2);
    return 2.0 * std::sqrt (power) / wsum;
}

/** Hann-windowed magnitude spectrum of v[from, from + fftSize) (zero-padded past the end). */
inline std::vector<float> magnitudeSpectrum (const std::vector<float>& v, size_t from, int fftSize)
{
    verdant::FFT fft (fftSize);
    const int n = fft.size();
    std::vector<float> in ((size_t) n, 0.0f), re ((size_t) n / 2 + 1), im ((size_t) n / 2 + 1);
    for (int i = 0; i < n; ++i)
    {
        const size_t idx = from + (size_t) i;
        const float win = 0.5f - 0.5f * std::cos (ap::kTwoPi * (float) i / (float) (n - 1));
        in[(size_t) i] = idx < v.size() ? v[idx] * win : 0.0f;
    }
    fft.realForward (in.data(), re.data(), im.data());
    std::vector<float> mag (re.size());
    for (size_t k = 0; k < re.size(); ++k) mag[k] = std::sqrt (re[k] * re[k] + im[k] * im[k]);
    return mag;
}

inline std::vector<float> sine (size_t n, double hz, double sampleRate, float amp)
{
    std::vector<float> v (n);
    for (size_t i = 0; i < n; ++i) v[i] = amp * (float) std::sin (ap::kTwoPiD * hz * (double) i / sampleRate);
    return v;
}

/** Runs `fn (L, R, n)` over the whole buffers in blocks of `block` samples. */
template <typename Fn>
inline void inBlocks (std::vector<float>& L, std::vector<float>& R, int block, Fn&& fn)
{
    for (size_t pos = 0; pos < L.size(); pos += (size_t) block)
    {
        const int n = (int) std::min ((size_t) block, L.size() - pos);
        fn (L.data() + pos, R.data() + pos, n, pos);
    }
}

} // namespace fxtest
