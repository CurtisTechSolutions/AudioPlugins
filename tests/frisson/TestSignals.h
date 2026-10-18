#pragma once

// Shared helpers for Frisson module tests: signal generators and measurements.
// Everything is deterministic so results are reproducible across platforms.

#include <ap/Math.h>
#include <ap/PinkNoise.h>

#include <cmath>
#include <cstddef>
#include <vector>

namespace fsig
{

inline constexpr double kSampleRate = 48000.0;

struct Stereo
{
    std::vector<float> l, r;
    explicit Stereo (size_t n = 0) : l (n, 0.0f), r (n, 0.0f) {}
    size_t size() const { return l.size(); }
};

inline std::vector<float> sine (double hz, size_t n, float amp = 0.5f, double sr = kSampleRate, double phase = 0.0)
{
    std::vector<float> v (n);
    for (size_t i = 0; i < n; ++i) v[i] = amp * (float) std::sin (ap::kTwoPiD * hz * (double) i / sr + phase);
    return v;
}

/** Harmonic series 1..harmonics with 1/h amplitudes, peak-normalised near `amp`. */
inline std::vector<float> harmonic (double hz, size_t n, int harmonics = 8, float amp = 0.5f, double sr = kSampleRate)
{
    std::vector<float> v (n);
    for (size_t i = 0; i < n; ++i)
    {
        double s = 0.0;
        for (int h = 1; h <= harmonics; ++h) s += std::sin (ap::kTwoPiD * hz * h * (double) i / sr + 0.3 * h) / h;
        v[i] = amp * (float) (s / 1.5);
    }
    return v;
}

inline std::vector<float> pink (size_t n, float amp = 0.2f, uint32_t seed = 1234u)
{
    ap::PinkNoise p (seed);
    std::vector<float> v (n);
    for (auto& s : v) s = amp * p.next();
    return v;
}

inline std::vector<float> white (size_t n, float amp = 0.2f, uint32_t seed = 99u)
{
    ap::Rng rng; rng.state = seed;
    std::vector<float> v (n);
    for (auto& s : v) s = amp * rng.nextBipolar();
    return v;
}

/** Stereo from two mono vectors (same length). */
inline Stereo stereo (std::vector<float> l, std::vector<float> r)
{
    Stereo s;
    s.l = std::move (l);
    s.r = std::move (r);
    return s;
}

inline Stereo dualMono (const std::vector<float>& m) { return stereo (m, m); }

/** Decorrelated stereo noise. */
inline Stereo stereoPink (size_t n, float amp = 0.2f) { return stereo (pink (n, amp, 1u), pink (n, amp, 2u)); }

inline double rms (const std::vector<float>& v, size_t from = 0, size_t to = (size_t) -1)
{
    if (to > v.size()) to = v.size();
    if (to <= from) return 0.0;
    double acc = 0.0;
    for (size_t i = from; i < to; ++i) acc += (double) v[i] * v[i];
    return std::sqrt (acc / (double) (to - from));
}

inline double rmsDb (const std::vector<float>& v, size_t from = 0, size_t to = (size_t) -1)
{
    const double r = rms (v, from, to);
    return r > 1e-12 ? 20.0 * std::log10 (r) : -240.0;
}

inline float peak (const std::vector<float>& v, size_t from = 0)
{
    float p = 0.0f;
    for (size_t i = from; i < v.size(); ++i) p = std::max (p, std::fabs (v[i]));
    return p;
}

inline double mean (const std::vector<float>& v, size_t from = 0)
{
    double acc = 0.0;
    for (size_t i = from; i < v.size(); ++i) acc += v[i];
    return acc / (double) (v.size() - from);
}

inline bool allFinite (const std::vector<float>& v)
{
    for (float s : v) if (! std::isfinite (s)) return false;
    return true;
}

/** Goertzel amplitude estimate of one frequency (same units as a sine's amplitude). */
inline double toneLevel (const std::vector<float>& v, double hz, size_t from = 0, double sr = kSampleRate)
{
    const double w = ap::kTwoPiD * hz / sr;
    const double c = 2.0 * std::cos (w);
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    for (size_t i = from; i < v.size(); ++i)
    {
        s0 = v[i] + c * s1 - s2;
        s2 = s1; s1 = s0;
    }
    const double power = s1 * s1 + s2 * s2 - c * s1 * s2;
    return std::sqrt (std::max (0.0, power)) / (double) (v.size() - from) * 2.0;
}

/** RMS of the part of `v` inside [lowHz, highHz], measured with a bank of Goertzel bins. */
inline double bandRms (const std::vector<float>& v, double lowHz, double highHz, size_t from = 0, int bins = 24, double sr = kSampleRate)
{
    double acc = 0.0;
    for (int b = 0; b < bins; ++b)
    {
        const double hz = lowHz * std::pow (highHz / lowHz, (b + 0.5) / bins);
        const double a = toneLevel (v, hz, from, sr);
        acc += a * a * 0.5;
    }
    return std::sqrt (acc);
}

/** Correlation coefficient between two channels. */
inline double correlation (const std::vector<float>& a, const std::vector<float>& b, size_t from = 0)
{
    double sab = 0.0, saa = 0.0, sbb = 0.0;
    for (size_t i = from; i < a.size() && i < b.size(); ++i)
    {
        sab += (double) a[i] * b[i];
        saa += (double) a[i] * a[i];
        sbb += (double) b[i] * b[i];
    }
    return (saa > 0.0 && sbb > 0.0) ? sab / std::sqrt (saa * sbb) : 0.0;
}

/** Max absolute difference between two vectors. */
inline float maxDiff (const std::vector<float>& a, const std::vector<float>& b, size_t from = 0)
{
    float d = 0.0f;
    for (size_t i = from; i < a.size() && i < b.size(); ++i) d = std::max (d, std::fabs (a[i] - b[i]));
    return d;
}

/** Runs a stereo signal through a module in blocks; returns the processed copy. */
template <typename ModuleT>
inline Stereo run (ModuleT& module, const Stereo& in, int blockSize = 256)
{
    Stereo out = in;
    const size_t n = out.size();
    for (size_t pos = 0; pos < n; pos += (size_t) blockSize)
    {
        const int len = (int) std::min ((size_t) blockSize, n - pos);
        module.process (out.l.data() + pos, out.r.data() + pos, len);
    }
    return out;
}

} // namespace fsig
