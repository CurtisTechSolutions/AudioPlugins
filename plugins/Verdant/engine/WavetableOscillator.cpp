#include "WavetableOscillator.h"

#include <ap/Math.h>

#include <algorithm>
#include <cmath>

namespace verdant
{

namespace
{
    // Continuous mip level: y = log2 (2 x 1023 x inc / kMipTopHarmonic). floor (y) is the level
    // mipLevelFor() picks (harmonicLimit (k) x inc < kMipTopHarmonic, the headroom that keeps the
    // linear-interpolation images from folding back audibly); over the upper half of each octave
    // (frac (y) 0.5 .. 1) the read crossfades into the next (darker) level and reaches it exactly
    // where floor (y) steps up, so the switch is seamless.
    const float kLevelBias          = (float) std::log2 (2.0 * 1023.0 / (double) kMipTopHarmonic);
    constexpr float kMinLevel       = -1.0f;                  // clamp for very low / zero increments
    constexpr float kMaxLevel       = (float) (kMaxMipLevels - 1);
    constexpr double kMaxIncrement  = 0.4999;                 // just below Nyquist
    constexpr float kSyncFade       = 1.0f / 32.0f;           // sync: blend the last 1/32 of the master cycle into the next reset
    constexpr float kSqrt2          = 1.41421356237309505f;

    inline float sane (float x, float lo, float hi, float fallback) noexcept
    {
        return std::isfinite (x) ? std::clamp (x, lo, hi) : fallback;
    }

    inline float wrap01 (float x) noexcept
    {
        return x - std::floor (x);   // may return exactly 1.0f for tiny negative x: the reader masks that to 0
    }

    // sin (2 pi x) / (2 pi) over one cycle, for Remap (no trig per sample).
    constexpr int kSinSize = 1024;
    struct SinTable
    {
        std::array<float, kSinSize + 1> v {};
        SinTable()
        {
            for (int i = 0; i <= kSinSize; ++i)
                v[(size_t) i] = (float) (std::sin (ap::kTwoPiD * (double) i / (double) kSinSize) / ap::kTwoPiD);
        }
    };
    const SinTable sinTable;

    inline float sinOverTwoPi (float p) noexcept
    {
        const float idx = p * (float) kSinSize;
        int i = (int) idx;
        const float t = idx - (float) i;
        i &= kSinSize - 1;
        return sinTable.v[(size_t) i] + t * (sinTable.v[(size_t) i + 1] - sinTable.v[(size_t) i]);
    }

    // One mip level of the current table, resolved once per render.
    struct LevelView
    {
        const float* base = nullptr;
        int stride = 0, mask = 0;
        float size = 0.0f;
    };

    struct TableView
    {
        std::array<LevelView, kMaxMipLevels> levels {};
        int lastLevel = 0;
        int lastFrame = 0;
    };

    TableView makeView (const Wavetable& t) noexcept
    {
        TableView tv;
        tv.lastLevel = std::max (0, t.numMipLevels() - 1);
        tv.lastFrame = std::max (0, t.numFrames() - 1);
        for (int l = 0; l <= tv.lastLevel; ++l)
        {
            auto& lv = tv.levels[(size_t) l];
            const int size = Wavetable::mipSize (l);
            lv.base = t.mipFrame (l, 0);   // frames of a level are contiguous, stride size + 1
            lv.stride = size + 1;
            lv.mask = size - 1;
            lv.size = (float) size;
        }
        return tv;
    }

    inline int levelIndex (const TableView& tv, float y) noexcept
    {
        return (int) std::clamp (y, 0.0f, (float) tv.lastLevel);
    }

    inline int frameIndex (const TableView& tv, float fpos) noexcept
    {
        return std::clamp ((int) fpos, 0, tv.lastFrame);
    }

    /** Frames a and b morphed by ft, then 4-point 3rd-order Lagrange interpolation in phase (p in [0, 1]).
        Cubic rather than linear: linear interpolation leaves images of harmonic h at mipSize +- h with
        amplitude ~ (h / mipSize)^2, which fold back to about -45 dB for a saw on the 2x mips; the cubic's
        images fall as ~ (h / mipSize)^4. Taps wrap with the power-of-two mask (no extra guard samples). */
    inline float readFrames (const float* a, const float* b, float size, int mask, float ft, float p) noexcept
    {
        const float idx = p * size;
        int i = (int) idx;
        const float t = idx - (float) i;
        const int im1 = (i - 1) & mask, i0 = i & mask, i1 = (i + 1) & mask, i2 = (i + 2) & mask;   // p == 1 reads sample 0
        const float ym1 = a[im1] + ft * (b[im1] - a[im1]);
        const float y0  = a[i0]  + ft * (b[i0]  - a[i0]);
        const float y1  = a[i1]  + ft * (b[i1]  - a[i1]);
        const float y2  = a[i2]  + ft * (b[i2]  - a[i2]);
        const float c1 = y1 - (1.0f / 3.0f) * ym1 - 0.5f * y0 - (1.0f / 6.0f) * y2;
        const float c2 = 0.5f * (ym1 + y1) - y0;
        const float c3 = (1.0f / 6.0f) * (y2 - ym1) + 0.5f * (y0 - y1);
        return ((c3 * t + c2) * t + c1) * t + y0;
    }

    /** Reads one copy's table position: frames f0 / f1 of mip level lo, crossfaded into level lo + 1 by w. */
    struct Reader
    {
        const float* a0 = nullptr; const float* b0 = nullptr;   // level lo, frames f0 / f1
        const float* a1 = nullptr; const float* b1 = nullptr;   // level lo + 1
        float size0 = 0.0f, size1 = 0.0f;
        int mask0 = 0, mask1 = 0;
        int lo = 0, f0 = 0;
        bool hasHi = false;
        float ft = 0.0f, w = 0.0f;

        /** Resolves the pointers for a (clamped) level and frame index. */
        void setup (const TableView& tv, int level, int frame) noexcept
        {
            lo = level;
            f0 = frame;
            const int f1 = std::min (frame + 1, tv.lastFrame);
            const auto& l0 = tv.levels[(size_t) level];
            a0 = l0.base + (size_t) frame * (size_t) l0.stride;
            b0 = l0.base + (size_t) f1 * (size_t) l0.stride;
            size0 = l0.size;
            mask0 = l0.mask;
            hasHi = level < tv.lastLevel;
            const auto& l1 = tv.levels[(size_t) (hasHi ? level + 1 : level)];
            a1 = l1.base + (size_t) frame * (size_t) l1.stride;
            b1 = l1.base + (size_t) f1 * (size_t) l1.stride;
            size1 = l1.size;
            mask1 = l1.mask;
        }

        /** Per-sample fractions: the frame morph and the level crossfade over the upper half of the octave. */
        void update (float y, float fpos) noexcept
        {
            ft = fpos - (float) f0;
            w = hasHi ? std::clamp (2.0f * (y - (float) lo) - 1.0f, 0.0f, 1.0f) : 0.0f;
        }

        float read (float p) const noexcept
        {
            const float v = readFrames (a0, b0, size0, mask0, ft, p);
            if (w <= 0.0f)
                return v;
            return v + w * (readFrames (a1, b1, size1, mask1, ft, p) - v);
        }
    };

    /** Everything one copy's loop needs for one render call (values before the first sample + per-sample steps). */
    struct CopyLoop
    {
        const TableView* tv = nullptr;
        float* outL = nullptr;
        float* outR = nullptr;
        const float* fm = nullptr;
        int numSamples = 0;
        double phase = 0.0, inc = 0.0, incStep = 0.0;
        float gl = 0.0f, gr = 0.0f, glStep = 0.0f, grStep = 0.0f;
        float y = 0.0f, yStep = 0.0f;               // mip level incl. the warp stretch
        float fpos = 0.0f, fposStep = 0.0f;         // fractional frame index
        float q = 0.0f, qStep = 0.0f;               // the mode's warp quantity
    };

    /** Fast = the mip level(s) and the frame pair stay the same for the whole block (the common case),
        so the table pointers are resolved once instead of per sample. */
    template <WarpMode Mode, bool Fast>
    void runCopyLoop (CopyLoop& c) noexcept
    {
        const TableView& tv = *c.tv;
        Reader rd, rm;   // rm: Mirror's second read, one level up (it runs at twice the rate)
        if constexpr (Fast)
        {
            rd.setup (tv, levelIndex (tv, c.y + c.yStep), frameIndex (tv, c.fpos + c.fposStep));
            if constexpr (Mode == WarpMode::Mirror)
                rm.setup (tv, levelIndex (tv, c.y + c.yStep + 1.0f), rd.f0);
        }

        double phase = c.phase, inc = c.inc;
        float gl = c.gl, gr = c.gr, y = c.y, fpos = c.fpos, q = c.q;

        for (int n = 0; n < c.numSamples; ++n)
        {
            inc += c.incStep;
            gl += c.glStep;
            gr += c.grStep;
            y += c.yStep;
            fpos += c.fposStep;
            q += c.qStep;

            if constexpr (! Fast)
            {
                rd.setup (tv, levelIndex (tv, y), frameIndex (tv, fpos));
                if constexpr (Mode == WarpMode::Mirror)
                    rm.setup (tv, levelIndex (tv, y + 1.0f), rd.f0);
            }
            rd.update (y, fpos);
            if constexpr (Mode == WarpMode::Mirror)
                rm.update (y + 1.0f, fpos);

            const float p = (float) phase;
            float v;

            if constexpr (Mode == WarpMode::Sync)
            {
                v = rd.read (wrap01 (p * q));
                if (p > 1.0f - kSyncFade)
                {
                    // Blend toward the slave phase the next master cycle starts from (0), so the
                    // reset is continuous; with q == 1 both reads are identical.
                    const float g = (p - (1.0f - kSyncFade)) * (1.0f / kSyncFade);
                    v += g * (rd.read (wrap01 ((p - 1.0f) * q)) - v);
                }
            }
            else if constexpr (Mode == WarpMode::Bend)
            {
                v = rd.read (q == 1.0f ? p : (p > 0.0f ? std::exp2 (q * std::log2 (p)) : 0.0f));
            }
            else if constexpr (Mode == WarpMode::Squeeze)
            {
                v = rd.read (p < q ? 0.5f * p / q : 0.5f + 0.5f * (p - q) / (1.0f - q));
            }
            else if constexpr (Mode == WarpMode::Mirror)
            {
                v = rd.read (p);
                if (q > 0.0f)
                    v += q * (rm.read (p < 0.5f ? 2.0f * p : 2.0f - 2.0f * p) - v);
            }
            else if constexpr (Mode == WarpMode::FmOsc || Mode == WarpMode::FmSub)
            {
                float m = c.fm[n];
                m = m > -4.0f ? (m < 4.0f ? m : 4.0f) : -4.0f;   // also maps NaN to a finite value
                v = rd.read (wrap01 (p + q * m));
            }
            else if constexpr (Mode == WarpMode::Remap)
            {
                v = rd.read (wrap01 (p + q * sinOverTwoPi (p)));
            }
            else
            {
                v = rd.read (p);
            }

            c.outL[n] += v * gl;
            c.outR[n] += v * gr;

            phase += inc;
            if (phase >= 1.0)
                phase -= 1.0;
        }

        c.phase = phase;
    }

    template <WarpMode Mode>
    void runCopy (CopyLoop& c) noexcept
    {
        const TableView& tv = *c.tv;
        const float n = (float) c.numSamples;
        const float yFirst = c.y + c.yStep, yLast = c.y + c.yStep * n;
        const float fFirst = c.fpos + c.fposStep, fLast = c.fpos + c.fposStep * n;
        bool fast = levelIndex (tv, yFirst) == levelIndex (tv, yLast) && frameIndex (tv, fFirst) == frameIndex (tv, fLast);
        if constexpr (Mode == WarpMode::Mirror)
            fast = fast && levelIndex (tv, yFirst + 1.0f) == levelIndex (tv, yLast + 1.0f);

        if (fast)
            runCopyLoop<Mode, true> (c);
        else
            runCopyLoop<Mode, false> (c);
    }

    /** The per-sample warp quantity ramped across a block (what each mode needs, derived from a). */
    float warpQuantity (WarpMode m, float a) noexcept
    {
        switch (m)
        {
            case WarpMode::Sync:    return 1.0f + 7.0f * std::fabs (a);
            case WarpMode::Bend:    return std::exp2 (2.0f * a);
            case WarpMode::Squeeze: return 0.5f - 0.45f * a;
            case WarpMode::Mirror:  return std::fabs (a);
            case WarpMode::FmOsc:
            case WarpMode::FmSub:   return 0.5f * a;
            case WarpMode::Remap:   return a;
            case WarpMode::Off:
            case WarpMode::Count:
            default:                return 0.0f;
        }
    }

    /** log2 of how much the warp compresses the cycle at its steepest (shifts the mip choice up). */
    float warpStretch (WarpMode m, float a) noexcept
    {
        switch (m)
        {
            case WarpMode::Sync:    return std::log2 (1.0f + 7.0f * std::fabs (a));
            case WarpMode::Bend:    return 2.0f * std::fabs (a);
            case WarpMode::Squeeze:
            {
                const float w = 0.5f - 0.45f * a;
                return std::log2 (std::max (0.5f / w, 0.5f / (1.0f - w)));
            }
            case WarpMode::Remap:   return std::log2 (1.0f + std::fabs (a));
            case WarpMode::Off:
            case WarpMode::Mirror:
            case WarpMode::FmOsc:
            case WarpMode::FmSub:
            case WarpMode::Count:
            default:                return 0.0f;
        }
    }

    // Seeded unit value for unison copy i: ap::Rng seeded with seed + i x golden ratio. The
    // seed is mixed first because xorshift's first outputs from small states are not random.
    float copyRandom (uint32_t seed, int copy) noexcept
    {
        uint32_t x = seed + (uint32_t) copy * 0x9E3779B9u;
        x ^= x >> 16; x *= 0x7FEB352Du;
        x ^= x >> 15; x *= 0x846CA68Bu;
        x ^= x >> 16;
        ap::Rng rng;
        rng.state = x != 0 ? x : 0x9E3779B9u;
        return rng.nextUnipolar();
    }
}

void WavetableOscillator::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 && std::isfinite (sampleRate) ? sampleRate : 44100.0;
    phases.fill (0.0);
    snapToTargets();
}

void WavetableOscillator::setTable (const Wavetable* t)
{
    table = t;
}

void WavetableOscillator::noteOn (float hz, uint32_t seed)
{
    setFrequency (hz);
    for (int u = 0; u < kMaxUnison; ++u)
    {
        const double p = (double) params.startPhase + (double) params.randomPhase * (double) copyRandom (seed, u);
        phases[(size_t) u] = p - std::floor (p);
    }
    snapToTargets();
}

void WavetableOscillator::setFrequency (float hz)
{
    frequency = sane (hz, 0.0f, 1.0e6f, 0.0f);
}

void WavetableOscillator::setParams (const OscParams& p)
{
    params.position    = sane (p.position, 0.0f, 1.0f, 0.0f);
    params.warp        = (int) p.warp < (int) WarpMode::Count ? p.warp : WarpMode::Off;
    params.warpAmount  = sane (p.warpAmount, -1.0f, 1.0f, 0.0f);
    params.unison      = std::clamp (p.unison, 1, kMaxUnison);
    params.detuneCents = sane (p.detuneCents, 0.0f, 1200.0f, 0.0f);
    params.spread      = sane (p.spread, 0.0f, 1.0f, 0.0f);
    params.blend       = sane (p.blend, 0.0f, 1.0f, 1.0f);
    params.startPhase  = sane (p.startPhase, 0.0f, 1.0f, 0.0f);
    params.randomPhase = sane (p.randomPhase, 0.0f, 1.0f, 0.0f);
    params.level       = sane (p.level, 0.0f, 4.0f, 0.0f);
    params.pan         = sane (p.pan, -1.0f, 1.0f, 0.0f);
}

void WavetableOscillator::computeTargets (Targets& t) const noexcept
{
    const int n = params.unison;
    const float blend = params.blend;
    const float norm = 1.0f / std::sqrt (1.0f + blend * blend * (float) (n - 1));
    const float balL = std::min (1.0f, 1.0f - params.pan);
    const float balR = std::min (1.0f, 1.0f + params.pan);
    const double baseInc = (double) frequency / sr;

    for (int i = 0; i < kMaxUnison; ++i)
    {
        if (i >= n)
        {
            // Inactive copies fade out and keep their pitch (they are skipped once silent).
            t.inc[(size_t) i] = incCur[(size_t) i];
            t.gainL[(size_t) i] = t.gainR[(size_t) i] = 0.0f;
            t.level[(size_t) i] = levelCur[(size_t) i];
            continue;
        }

        const float offset = n > 1 ? 2.0f * (float) i / (float) (n - 1) - 1.0f : 0.0f;   // -1..1
        const double inc = std::clamp (baseInc * std::exp2 ((double) (params.detuneCents * offset) / 1200.0), 0.0, kMaxIncrement);

        const bool centre = (n & 1) != 0 ? i == (n - 1) / 2 : (i == n / 2 - 1 || i == n / 2);
        const float copyGain = centre ? 1.0f : blend;

        // Symmetric pair k = (i, n-1-i): the flatter copy goes left on even pairs, right on odd ones.
        const int pair = std::min (i, n - 1 - i);
        float side = i < n - 1 - i ? -1.0f : 1.0f;
        if ((pair & 1) != 0) side = -side;
        const float pan = (n > 1 && ! (centre && (n & 1) != 0)) ? params.spread * std::fabs (offset) * side : 0.0f;

        // Equal-power, unity at the centre.
        const float x = 0.5f * (pan + 1.0f);
        const float panL = kSqrt2 * std::sin ((1.0f - x) * 0.5f * ap::kPi);
        const float panR = kSqrt2 * std::sin (x * 0.5f * ap::kPi);

        const float g = params.level * norm * copyGain;
        t.inc[(size_t) i] = inc;
        t.gainL[(size_t) i] = g * panL * balL;
        t.gainR[(size_t) i] = g * panR * balR;
        t.level[(size_t) i] = inc > 0.0 ? std::clamp ((float) std::log2 (inc) + kLevelBias, kMinLevel, kMaxLevel) : kMinLevel;
    }
}

void WavetableOscillator::snapToTargets() noexcept
{
    Targets t;
    computeTargets (t);
    incCur = t.inc;
    gainLCur = t.gainL;
    gainRCur = t.gainR;
    levelCur = t.level;
    positionCur = params.position;
    warpCur = params.warpAmount;
}

void WavetableOscillator::render (float* outL, float* outR, int numSamples, const float* fmInput) noexcept
{
    if (outL == nullptr || outR == nullptr || numSamples <= 0)
        return;

    std::fill (outL, outL + numSamples, 0.0f);
    std::fill (outR, outR + numSamples, 0.0f);

    Targets t;
    computeTargets (t);

    WarpMode mode = params.warp;
    if ((mode == WarpMode::FmOsc || mode == WarpMode::FmSub) && fmInput == nullptr)
        mode = WarpMode::Off;

    if (table != nullptr && table->numFrames() > 0)
    {
        const auto tv = makeView (*table);
        const float inv = 1.0f / (float) numSamples;
        const float lastFrame = (float) tv.lastFrame;
        const float warpStart = warpQuantity (mode, warpCur);
        const float warpStep = (warpQuantity (mode, params.warpAmount) - warpStart) * inv;
        const float stretchStart = warpStretch (mode, warpCur);
        const float stretchStep = (warpStretch (mode, params.warpAmount) - stretchStart) * inv;

        for (size_t i = 0; i < (size_t) kMaxUnison; ++i)
        {
            if (gainLCur[i] == 0.0f && gainRCur[i] == 0.0f && t.gainL[i] == 0.0f && t.gainR[i] == 0.0f)
                continue;

            CopyLoop c;
            c.tv = &tv;
            c.outL = outL;
            c.outR = outR;
            c.fm = fmInput;
            c.numSamples = numSamples;
            c.phase = phases[i];
            c.inc = incCur[i];
            c.incStep = (t.inc[i] - incCur[i]) / (double) numSamples;
            c.gl = gainLCur[i];
            c.gr = gainRCur[i];
            c.glStep = (t.gainL[i] - gainLCur[i]) * inv;
            c.grStep = (t.gainR[i] - gainRCur[i]) * inv;
            c.y = levelCur[i] + stretchStart;
            c.yStep = (t.level[i] - levelCur[i]) * inv + stretchStep;
            c.fpos = positionCur * lastFrame;
            c.fposStep = (params.position - positionCur) * inv * lastFrame;
            c.q = warpStart;
            c.qStep = warpStep;

            switch (mode)
            {
                case WarpMode::Sync:    runCopy<WarpMode::Sync>    (c); break;
                case WarpMode::Bend:    runCopy<WarpMode::Bend>    (c); break;
                case WarpMode::Squeeze: runCopy<WarpMode::Squeeze> (c); break;
                case WarpMode::Mirror:  runCopy<WarpMode::Mirror>  (c); break;
                case WarpMode::FmOsc:   runCopy<WarpMode::FmOsc>   (c); break;
                case WarpMode::FmSub:   runCopy<WarpMode::FmSub>   (c); break;
                case WarpMode::Remap:   runCopy<WarpMode::Remap>   (c); break;
                case WarpMode::Off:
                case WarpMode::Count:
                default:                runCopy<WarpMode::Off>     (c); break;
            }
            phases[i] = c.phase;
        }
    }

    incCur = t.inc;
    gainLCur = t.gainL;
    gainRCur = t.gainR;
    levelCur = t.level;
    positionCur = params.position;
    warpCur = params.warpAmount;
}

float WavetableOscillator::currentPosition() const noexcept
{
    return positionCur;
}

} // namespace verdant
