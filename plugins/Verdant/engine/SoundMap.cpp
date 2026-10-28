#include "SoundMap.h"

#include "FFT.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace verdant
{

namespace
{
    constexpr int kFftSize = 4096;
    constexpr int kHop = 2048;
    constexpr int kDims = 5;   // log centroid, log attack, noisiness, movement, loudness
    constexpr int kPowerIterations = 200;

    using Vec = std::array<double, kDims>;
    using Mat = std::array<Vec, kDims>;

    double dot (const Vec& a, const Vec& b) noexcept
    {
        double s = 0.0;
        for (int d = 0; d < kDims; ++d) s += a[(size_t) d] * b[(size_t) d];
        return s;
    }

    Vec mul (const Mat& m, const Vec& v) noexcept
    {
        Vec out {};
        for (int r = 0; r < kDims; ++r) out[(size_t) r] = dot (m[(size_t) r], v);
        return out;
    }

    double norm (const Vec& v) noexcept { return std::sqrt (dot (v, v)); }

    /** Attack: first time the centred 10 ms RMS (1 ms hop) reaches 90 % of its maximum. */
    float attackTimeMs (const float* x, int n, double sr)
    {
        std::vector<double> prefix ((size_t) n + 1, 0.0);
        for (int i = 0; i < n; ++i)
            prefix[(size_t) i + 1] = prefix[(size_t) i] + (double) x[i] * x[i];

        const int win = std::max (1, (int) std::lround (0.010 * sr));
        const int hop = std::max (1, (int) std::lround (0.001 * sr));
        std::vector<double> env;
        env.reserve ((size_t) (n / hop + 1));
        for (int c = 0; c < n; c += hop)
        {
            const int lo = std::max (0, c - win / 2), hi = std::min (n, c - win / 2 + win);
            env.push_back (std::sqrt (std::max (0.0, prefix[(size_t) hi] - prefix[(size_t) lo]) / win));
        }
        const double peak = env.empty() ? 0.0 : *std::max_element (env.begin(), env.end());
        if (peak <= 1.0e-12)
            return 0.0f;
        for (size_t k = 0; k < env.size(); ++k)
            if (env[k] >= 0.9 * peak)
                return (float) (1000.0 * (double) k * hop / sr);
        return 0.0f;
    }

    Vec featureVector (const SoundFeatures& f)
    {
        return { std::log (std::max (1.0, (double) f.centroidHz)),
                 std::log (std::max (0.1, (double) f.attackMs)),
                 (double) f.noisiness, (double) f.movement, (double) f.loudness };
    }

    /** Power iteration from the fixed start vector; `against` (may be null) is projected out every step. */
    Vec principalComponent (const Mat& c, const Vec* against)
    {
        auto orthogonalise = [against] (Vec& v)
        {
            if (against == nullptr) return;
            const double k = dot (v, *against);
            for (int d = 0; d < kDims; ++d) v[(size_t) d] -= k * (*against)[(size_t) d];
        };

        // Start (1,1,1,1,1)/sqrt 5; if that has no component left after deflation, fall back to the
        // unit vectors in order (still deterministic).
        std::array<Vec, kDims + 1> starts {};
        starts[0].fill (1.0 / std::sqrt ((double) kDims));
        for (int d = 0; d < kDims; ++d) starts[(size_t) d + 1][(size_t) d] = 1.0;

        Vec v {};
        for (const auto& s : starts)
        {
            v = s;
            orthogonalise (v);
            if (norm (v) > 1.0e-6) break;
        }
        double nv = norm (v);
        if (nv <= 0.0) return v;
        for (auto& e : v) e /= nv;

        for (int it = 0; it < kPowerIterations; ++it)
        {
            Vec w = mul (c, v);
            orthogonalise (w);
            const double nw = norm (w);
            if (nw < 1.0e-12) break;   // no variance left in this direction: keep the current vector
            for (int d = 0; d < kDims; ++d) v[(size_t) d] = w[(size_t) d] / nw;
        }
        return v;
    }

    void minMax (std::vector<SoundMapPoint>& points, bool isX)
    {
        double lo = 1.0e300, hi = -1.0e300;
        for (const auto& p : points) { const double v = isX ? p.x : p.y; lo = std::min (lo, v); hi = std::max (hi, v); }
        for (auto& p : points)
        {
            float& v = isX ? p.x : p.y;
            v = hi - lo > 1.0e-6 ? (float) (0.05 + 0.9 * ((double) v - lo) / (hi - lo)) : 0.5f;
        }
    }
}

//==============================================================================
SoundFeatures extractFeatures (const float* mono, int n, double sampleRate)
{
    SoundFeatures f { 0.0f, 0.0f, 0.0f, 0.0f, -120.0f };
    if (mono == nullptr || n <= 1 || ! (sampleRate > 0.0))
        return f;

    // ---- Loudness
    double sumSq = 0.0;
    for (int i = 0; i < n; ++i) sumSq += (double) mono[i] * mono[i];
    const double rms = std::sqrt (sumSq / n);
    f.loudness = rms > 1.0e-6 ? (float) (20.0 * std::log10 (rms)) : -120.0f;
    if (! (rms > 0.0) || ! std::isfinite (rms))
        return f;

    f.attackMs = attackTimeMs (mono, n, sampleRate);

    // ---- Spectral frames
    const FFT fft (kFftSize);
    std::vector<float> window ((size_t) kFftSize), frame ((size_t) kFftSize), re ((size_t) kFftSize / 2 + 1), im ((size_t) kFftSize / 2 + 1);
    for (int i = 0; i < kFftSize; ++i)
        window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (6.283185307179586 * i / kFftSize));

    const int numFrames = n <= kFftSize ? 1 : 1 + (n - kFftSize + kHop - 1) / kHop;
    const double binHz = sampleRate / kFftSize;
    const int lastBin = kFftSize / 2 - 1;   // DC and Nyquist excluded

    std::vector<double> frameEnergy ((size_t) numFrames), frameCentroid ((size_t) numFrames);
    double weightedSum = 0.0, totalPower = 0.0, flatnessSum = 0.0;
    std::vector<double> power ((size_t) lastBin + 1);

    for (int fr = 0; fr < numFrames; ++fr)
    {
        const int start = fr * kHop;
        for (int i = 0; i < kFftSize; ++i)
        {
            const int idx = start + i;
            frame[(size_t) i] = idx < n ? mono[idx] * window[(size_t) i] : 0.0f;
        }
        fft.realForward (frame.data(), re.data(), im.data());

        double e = 0.0, fe = 0.0, maxP = 0.0;
        for (int k = 1; k <= lastBin; ++k)
        {
            const double pk = (double) re[(size_t) k] * re[(size_t) k] + (double) im[(size_t) k] * im[(size_t) k];
            power[(size_t) k] = pk;
            e += pk;
            fe += pk * k * binHz;
            maxP = std::max (maxP, pk);
        }
        frameEnergy[(size_t) fr] = e;
        frameCentroid[(size_t) fr] = e > 0.0 ? fe / e : 0.0;
        weightedSum += fe;
        totalPower += e;

        if (e > 0.0)
        {
            // Floor far below anything audible so log() stays finite; it barely moves a tone's flatness.
            const double floorP = maxP * 1.0e-12 + 1.0e-300;
            double logSum = 0.0, linSum = 0.0;
            for (int k = 1; k <= lastBin; ++k)
            {
                const double pk = power[(size_t) k] + floorP;
                logSum += std::log (pk);
                linSum += pk;
            }
            const double flat = std::exp (logSum / lastBin) / (linSum / lastBin);
            flatnessSum += std::clamp (flat, 0.0, 1.0) * e;
        }
    }

    if (totalPower > 0.0)
    {
        f.centroidHz = (float) (weightedSum / totalPower);
        f.noisiness = (float) std::clamp (flatnessSum / totalPower, 0.0, 1.0);

        // Movement over the frames within 40 dB of the loudest one.
        const double maxE = *std::max_element (frameEnergy.begin(), frameEnergy.end());
        double mean = 0.0, meanSq = 0.0;
        int count = 0;
        for (int fr = 0; fr < numFrames; ++fr)
        {
            if (frameEnergy[(size_t) fr] < maxE * 1.0e-4) continue;
            mean += frameCentroid[(size_t) fr];
            meanSq += frameCentroid[(size_t) fr] * frameCentroid[(size_t) fr];
            ++count;
        }
        if (count > 1)
        {
            mean /= count;
            const double var = std::max (0.0, meanSq / count - mean * mean);
            f.movement = mean > 1.0e-9 ? (float) (std::sqrt (var) / mean) : 0.0f;
        }
    }
    return f;
}

//==============================================================================
std::vector<SoundMapPoint> buildSoundMapFromFeatures (const std::vector<SoundFeatures>& features)
{
    const int m = (int) features.size();
    std::vector<SoundMapPoint> points;
    points.reserve ((size_t) m);
    for (int i = 0; i < m; ++i)
        points.push_back ({ i, 0.5f, 0.5f, features[(size_t) i] });
    if (m == 0)
        return points;

    std::vector<Vec> x ((size_t) m);
    for (int i = 0; i < m; ++i) x[(size_t) i] = featureVector (features[(size_t) i]);

    if (m < 3)
    {
        // Too few points for a meaningful PCA: a horizontal line, darker to brighter.
        if (m == 2)
        {
            const bool firstBrighter = x[0][0] > x[1][0];
            points[0].x = firstBrighter ? 0.95f : 0.05f;
            points[1].x = firstBrighter ? 0.05f : 0.95f;
        }
        return points;
    }

    // ---- Standardise
    for (int d = 0; d < kDims; ++d)
    {
        double mean = 0.0;
        for (const auto& v : x) mean += v[(size_t) d];
        mean /= m;
        double var = 0.0;
        for (const auto& v : x) var += (v[(size_t) d] - mean) * (v[(size_t) d] - mean);
        const double sd = std::sqrt (var / m);
        const bool constant = ! (sd > 1.0e-9 * std::max (1.0, std::fabs (mean)));
        for (auto& v : x)
            v[(size_t) d] = constant ? 0.0 : (v[(size_t) d] - mean) / sd;
    }

    // ---- Covariance (of the z-scores)
    Mat c {};
    for (int r = 0; r < kDims; ++r)
        for (int k = 0; k < kDims; ++k)
        {
            double s = 0.0;
            for (const auto& v : x) s += v[(size_t) r] * v[(size_t) k];
            c[(size_t) r][(size_t) k] = s / m;
        }

    // ---- First two principal components (power iteration + deflation)
    const Vec pc1 = principalComponent (c, nullptr);
    const double lambda1 = dot (pc1, mul (c, pc1));
    Mat deflated = c;
    for (int r = 0; r < kDims; ++r)
        for (int k = 0; k < kDims; ++k)
            deflated[(size_t) r][(size_t) k] -= lambda1 * pc1[(size_t) r] * pc1[(size_t) k];
    const Vec pc2 = principalComponent (deflated, &pc1);

    // ---- Project, fix signs (x rises with log centroid, y with log attack), scale to the map
    double sx = 0.0, sy = 0.0;
    for (int i = 0; i < m; ++i)
    {
        const double px = dot (x[(size_t) i], pc1), py = dot (x[(size_t) i], pc2);
        points[(size_t) i].x = (float) px;
        points[(size_t) i].y = (float) py;
        sx += px * x[(size_t) i][0];
        sy += py * x[(size_t) i][1];
    }
    for (auto& p : points)
    {
        if (sx < 0.0) p.x = -p.x;
        if (sy < 0.0) p.y = -p.y;
    }
    minMax (points, true);
    minMax (points, false);
    return points;
}

std::vector<SoundMapPoint> buildSoundMap (const std::vector<Patch>& patches, const std::function<std::vector<float> (const Patch&)>& render)
{
    std::vector<SoundFeatures> features;
    features.reserve (patches.size());
    for (const auto& patch : patches)
    {
        const auto audio = render ? render (patch) : std::vector<float>();
        features.push_back (extractFeatures (audio.data(), (int) audio.size(), 44100.0));
    }
    return buildSoundMapFromFeatures (features);
}

//==============================================================================
Patch blendPatches (const Patch& a, const Patch& b, float t)
{
    if (! (t > 0.0f)) return a;   // exact end points (also catches NaN)
    if (t >= 1.0f) return b;
    Patch out = t < 0.5f ? a : b;   // tags, author, macro names, LFO shapes, user tables, seed, Choice / Bool values

    for (int i = 0; i < kNumParams; ++i)
    {
        const P p = (P) i;
        const auto kind = info (p).kind;
        if (kind == ParamKind::Choice || kind == ParamKind::Bool)
            continue;   // already from the chosen side
        const float na = toNormalised (p, a.values[(size_t) i]);
        const float nb = toNormalised (p, b.values[(size_t) i]);
        out.values[(size_t) i] = clampToRange (p, fromNormalised (p, na + (nb - na) * t));   // Int rounds inside fromNormalised
    }
    out.name = a.name + " \xE2\x86\x94 " + b.name + " " + std::to_string (std::lround (t * 100.0f)) + "%";
    return out;
}

} // namespace verdant
