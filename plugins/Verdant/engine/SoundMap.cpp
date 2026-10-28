#include "SoundMap.h"

#include <algorithm>
#include <cmath>

namespace verdant
{

SoundFeatures extractFeatures (const float* mono, int n, double sampleRate)
{
    SoundFeatures f { 0.0f, 0.0f, 0.0f, 0.0f, -120.0f };
    if (mono == nullptr || n <= 1 || sampleRate <= 0.0)
        return f;

    // Loudness (RMS dB), zero-crossing based brightness / noisiness, attack time to 90 % of peak,
    // movement = spread of short-time RMS (20 ms windows).
    double sum = 0.0, diffSum = 0.0;
    int crossings = 0;
    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        sum += (double) mono[i] * mono[i];
        peak = std::max (peak, std::fabs (mono[i]));
        if (i > 0)
        {
            diffSum += std::fabs ((double) mono[i] - mono[i - 1]);
            if ((mono[i] >= 0.0f) != (mono[i - 1] >= 0.0f)) ++crossings;
        }
    }
    const double rms = std::sqrt (sum / n);
    f.loudness = rms > 1.0e-9 ? (float) (20.0 * std::log10 (rms)) : -120.0f;
    f.centroidHz = (float) (0.5 * crossings * sampleRate / n);

    double absSum = 0.0;
    for (int i = 0; i < n; ++i) absSum += std::fabs (mono[i]);
    f.noisiness = absSum > 1.0e-12 ? (float) std::clamp (diffSum / (absSum * 2.0), 0.0, 1.0) : 0.0f;

    for (int i = 0; i < n; ++i)
        if (std::fabs (mono[i]) >= 0.9f * peak) { f.attackMs = (float) (1000.0 * i / sampleRate); break; }

    const int win = std::max (1, (int) (0.02 * sampleRate));
    double mean = 0.0, meanSq = 0.0;
    int windows = 0;
    for (int start = 0; start + win <= n; start += win, ++windows)
    {
        double acc = 0.0;
        for (int i = start; i < start + win; ++i) acc += (double) mono[i] * mono[i];
        const double r = std::sqrt (acc / win);
        mean += r;
        meanSq += r * r;
    }
    if (windows > 1)
    {
        mean /= windows;
        const double var = std::max (0.0, meanSq / windows - mean * mean);
        f.movement = mean > 1.0e-9 ? (float) (std::sqrt (var) / mean) : 0.0f;
    }
    return f;
}

std::vector<SoundMapPoint> buildSoundMap (const std::vector<Patch>& patches, const std::function<std::vector<float> (const Patch&)>& render)
{
    std::vector<SoundMapPoint> points;
    points.reserve (patches.size());
    for (size_t i = 0; i < patches.size(); ++i)
    {
        const auto audio = render ? render (patches[i]) : std::vector<float>();
        SoundMapPoint p { (int) i, 0.5f, 0.5f, extractFeatures (audio.data(), (int) audio.size(), 44100.0) };
        points.push_back (p);
    }

    // Stub placement: x = normalised log centroid, y = normalised log attack.
    auto normalise = [&points] (auto get, auto setter)
    {
        float lo = 1.0e9f, hi = -1.0e9f;
        for (auto& p : points) { const float v = get (p); lo = std::min (lo, v); hi = std::max (hi, v); }
        for (auto& p : points) setter (p, hi - lo > 1.0e-9f ? (get (p) - lo) / (hi - lo) : 0.5f);
    };
    normalise ([] (const SoundMapPoint& p) { return std::log2 (1.0f + p.features.centroidHz); },
               [] (SoundMapPoint& p, float v) { p.x = v; });
    normalise ([] (const SoundMapPoint& p) { return std::log2 (1.0f + p.features.attackMs); },
               [] (SoundMapPoint& p, float v) { p.y = v; });
    return points;
}

Patch blendPatches (const Patch& a, const Patch& b, float t)
{
    if (! (t > 0.0f)) return a;   // exact end points (also catches NaN)
    if (t >= 1.0f) return b;
    Patch out = t < 0.5f ? a : b;   // names, tags, macro names, LFO shapes, user tables, seed

    for (int i = 0; i < kNumParams; ++i)
    {
        const P p = (P) i;
        const auto& in = info (p);
        if (in.kind == ParamKind::Choice || in.kind == ParamKind::Bool)
            continue;   // already from the chosen side
        const float na = toNormalised (p, a.values[(size_t) i]);
        const float nb = toNormalised (p, b.values[(size_t) i]);
        out.values[(size_t) i] = fromNormalised (p, na + (nb - na) * t);   // Int rounds inside fromNormalised
    }
    return out;
}

} // namespace verdant
