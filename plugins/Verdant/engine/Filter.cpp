#include "Filter.h"

#include "Common.h"

#include <ap/Math.h>

#include <algorithm>
#include <cmath>
#include <complex>

namespace verdant
{

namespace
{
    constexpr int   kRampSamples   = kControlInterval;   // set() is called once per control block
    constexpr float kMinCutoffHz   = 20.0f;
    constexpr double kMaxCutoffRatio = 0.45;               // of the sample rate

    // Comb damping: one-pole LP in the feedback loop, y = p * y + (1 - p) * x. Its
    // DC group delay p / (1 - p) is taken off the delay line so the comb stays in tune.
    constexpr float kCombPole    = 0.3f;
    constexpr float kCombLpDelay = kCombPole / (1.0f - kCombPole);
    constexpr float kCombMaxFb   = 0.98f;

    enum class Family : uint8_t { Svf, Comb, Formant, Ladder };

    Family familyOf (FilterType t) noexcept
    {
        switch (t)
        {
            case FilterType::CombPlus:
            case FilterType::CombMinus: return Family::Comb;
            case FilterType::Formant:   return Family::Formant;
            case FilterType::Ladder:    return Family::Ladder;
            case FilterType::LP12: case FilterType::LP24: case FilterType::HP12: case FilterType::HP24:
            case FilterType::BP12: case FilterType::BP24: case FilterType::Notch:
            case FilterType::Count:
            default:                    return Family::Svf;
        }
    }

    bool isFourPole (FilterType t) noexcept
    {
        return t == FilterType::LP24 || t == FilterType::HP24 || t == FilterType::BP24;
    }

    FilterType sanitiseType (FilterType t) noexcept
    {
        return (unsigned) t < (unsigned) FilterType::Count ? t : FilterType::LP24;
    }

    // Vowel formants A, E, I, O, U: F1..F4 (Hz) and their levels (dB), after the
    // Peterson & Barney (1952) adult-male averages, F4 and the upper levels filled
    // in from the usual singing-formant tables.
    struct Vowel { float hz[4]; float db[4]; };
    constexpr Vowel kVowels[5] = {
        { { 730.0f, 1090.0f, 2440.0f, 3400.0f }, { 0.0f,  -4.0f, -27.0f, -33.0f } },   // A
        { { 530.0f, 1840.0f, 2480.0f, 3500.0f }, { 0.0f, -15.0f, -22.0f, -32.0f } },   // E
        { { 270.0f, 2290.0f, 3010.0f, 3700.0f }, { 0.0f, -20.0f, -24.0f, -30.0f } },   // I
        { { 570.0f,  840.0f, 2410.0f, 3300.0f }, { 0.0f,  -7.0f, -34.0f, -40.0f } },   // O
        { { 300.0f,  870.0f, 2240.0f, 3200.0f }, { 0.0f, -16.0f, -40.0f, -45.0f } },   // U
    };

    /** Soft limiter for filter states: linear up to |2|, then saturates towards |4|
        with a continuous slope, so resonance can ring hard but never blow up. */
    inline float softLimit (float x) noexcept
    {
        const float a = std::fabs (x);
        if (a <= 2.0f)
            return x;
        return std::copysign (2.0f + 2.0f * std::tanh ((a - 2.0f) * 0.5f), x);
    }

    inline float flushTiny (float x) noexcept
    {
        return std::fabs (x) < 1.0e-15f ? 0.0f : x;
    }

    inline float clamp01 (float v) noexcept
    {
        return std::isfinite (v) ? std::clamp (v, 0.0f, 1.0f) : 0.0f;
    }

    inline float clampCutoff (float hz, double sr) noexcept
    {
        if (! std::isfinite (hz))
            hz = 1000.0f;
        return std::clamp (hz, kMinCutoffHz, (float) (sr * kMaxCutoffRatio));
    }

    /** TPT prewarped integrator gain for a cutoff. */
    inline float prewarp (double hz, double sr) noexcept
    {
        return (float) std::tan (ap::kPiD * std::min (hz, sr * kMaxCutoffRatio) / sr);
    }

    inline float svfQ (float res01) noexcept
    {
        return ap::lerp (0.5f, 25.0f, res01 * res01);
    }

    /** Formant frequencies and linear levels for a cutoff (log-mapped onto the vowel path). */
    void formantsFor (float cutoffHz, float* hz, float* gain) noexcept
    {
        const float t = std::clamp (std::log (cutoffHz / kMinCutoffHz) / std::log (1000.0f), 0.0f, 1.0f);
        const float pos = t * 4.0f;
        const int i = std::min (3, (int) pos);
        const float f = pos - (float) i;
        for (int b = 0; b < 4; ++b)
        {
            hz[b]   = ap::lerp (kVowels[i].hz[b], kVowels[i + 1].hz[b], f);
            gain[b] = ap::dbToGain (ap::lerp (kVowels[i].db[b], kVowels[i + 1].db[b], f));
        }
    }

    /** Type coefficients (slots 1..9, see Filter.h) for clamped inputs. Shared by
        the filter and filterMagnitudeDb so the drawn curve is what plays. */
    void design (FilterType t, float cutoffHz, float res01, double sr, float* c) noexcept
    {
        switch (familyOf (t))
        {
            case Family::Svf:
            {
                float q = svfQ (res01);
                if (isFourPole (t))
                    q = std::sqrt (q);   // two equal stages: combined peak ≈ the 2-pole target
                c[1] = prewarp (cutoffHz, sr);
                c[2] = 1.0f / q;
                break;
            }

            case Family::Comb:
            {
                const float fb = kCombMaxFb * res01 * (t == FilterType::CombMinus ? -1.0f : 1.0f);
                c[1] = std::max (2.0f, (float) (sr / cutoffHz) - kCombLpDelay);
                c[2] = fb;
                c[3] = std::sqrt (1.0f - fb * fb);   // keeps the broadband (noise) level constant
                break;
            }

            case Family::Formant:
            {
                float hz[4], gain[4];
                formantsFor (cutoffHz, hz, gain);
                for (int b = 0; b < 4; ++b)
                {
                    c[1 + b] = prewarp (hz[b], sr);
                    c[5 + b] = gain[b];
                }
                c[9] = 1.0f / ap::lerp (3.0f, 20.0f, res01);
                break;
            }

            case Family::Ladder:
            {
                // One-pole stages at 2x the sample rate.
                c[1] = (float) (1.0 - std::exp (-ap::kTwoPiD * (double) cutoffHz / (2.0 * sr)));
                c[2] = 4.0f * res01;
                break;
            }
        }
    }

    // SVF outputs.
    enum SvfMode { kLow = 0, kHigh = 1, kBand = 2, kNotch = 3 };

    template <int Mode>
    inline float svfTick (float& ic1, float& ic2, float v0, float a1, float a2, float a3, float k) noexcept
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = softLimit (2.0f * v1 - ic1);
        ic2 = softLimit (2.0f * v2 - ic2);
        if constexpr (Mode == kLow)        return v2;
        else if constexpr (Mode == kHigh)  return v0 - k * v1 - v2;
        else if constexpr (Mode == kBand)  return k * v1;           // unity gain at the peak
        else                               return v0 - k * v1;
    }

    /** Ladder input nonlinearity: tanh with headroom (linear-ish up to about +-1, limits at +-2). */
    inline float ladderSat (float v) noexcept
    {
        return 2.0f * ap::fastTanh (0.5f * v);
    }
}

//==============================================================================
void VoiceFilter::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 && std::isfinite (sampleRate) ? sampleRate : 44100.0;

    // Longest comb delay: 20 Hz at this rate, plus interpolation headroom.
    const int needed = (int) std::ceil (sr / (double) kMinCutoffHz) + 8;
    int size = 1;
    while (size < needed)
        size <<= 1;
    for (auto& c : ch)
        c.comb.assign ((size_t) size, 0.0f);
    combMask = size - 1;
    combWrite = 0;
    combDirty = false;

    type = FilterType::LP24;
    primed = false;
    set (FilterType::LP24, 12000.0f, 0.15f, 0.0f);   // usable defaults until the voice sets its own
    reset();
}

void VoiceFilter::reset() noexcept
{
    clearState();
    primed = false;   // next set() snaps instead of ramping from the previous note
}

void VoiceFilter::clearState() noexcept
{
    for (auto& c : ch)
    {
        for (auto& s : c.svf)
            s = {};
        for (auto& l : c.ladder)
            l = 0.0f;
        c.ladderPrevIn = 0.0f;
        c.combLp = 0.0f;
        if (combDirty)
            std::fill (c.comb.begin(), c.comb.end(), 0.0f);
    }
    combDirty = false;
}

void VoiceFilter::sanitiseState() noexcept
{
    bool ok = true;
    for (auto& c : ch)
    {
        for (auto& s : c.svf)
        {
            s.ic1 = flushTiny (s.ic1);
            s.ic2 = flushTiny (s.ic2);
            ok = ok && std::isfinite (s.ic1) && std::isfinite (s.ic2);
        }
        for (auto& l : c.ladder)
        {
            l = flushTiny (l);
            ok = ok && std::isfinite (l);
        }
        c.combLp = flushTiny (c.combLp);
        ok = ok && std::isfinite (c.combLp) && std::isfinite (c.ladderPrevIn);
    }
    if (! ok)
    {
        combDirty = true;   // a NaN may sit in the delay line too
        clearState();
    }
}

void VoiceFilter::set (FilterType t, float cutoffHz, float resonance01, float drive01) noexcept
{
    t = sanitiseType (t);
    const bool typeChanged = t != type;
    if (typeChanged)
        clearState();   // stale state from another topology would click or ring
    type = t;

    const float cutoff = clampCutoff (cutoffHz, sr);
    const float res    = clamp01 (resonance01);
    const float drive  = clamp01 (drive01);

    for (auto& v : tgt)
        v = 0.0f;
    tgt[0] = 1.0f + 6.0f * drive;
    design (type, cutoff, res, sr, tgt);

    if (! primed || typeChanged)
    {
        std::copy (std::begin (tgt), std::end (tgt), std::begin (cur));
        std::fill (std::begin (stp), std::end (stp), 0.0f);
        rampLeft = 0;
        primed = true;
    }
    else
    {
        for (int i = 0; i < kNumCoeffs; ++i)
            stp[i] = (tgt[i] - cur[i]) / (float) kRampSamples;
        rampLeft = kRampSamples;
    }

    deriveCoefficients();
}

void VoiceFilter::deriveCoefficients() noexcept
{
    const Family fam = familyOf (type);
    if (fam == Family::Svf)
    {
        const float g = cur[1], k = cur[2];
        a1[0] = 1.0f / (1.0f + g * (g + k));
        a2[0] = g * a1[0];
        a3[0] = g * a2[0];
    }
    else if (fam == Family::Formant)
    {
        const float k = cur[9];
        for (int b = 0; b < 4; ++b)
        {
            const float g = cur[1 + b];
            a1[b] = 1.0f / (1.0f + g * (g + k));
            a2[b] = g * a1[b];
            a3[b] = g * a2[b];
        }
    }
}

inline void VoiceFilter::stepRamp() noexcept
{
    if (--rampLeft > 0)
    {
        for (int i = 0; i < kNumCoeffs; ++i)
            cur[i] += stp[i];
    }
    else
    {
        std::copy (std::begin (tgt), std::end (tgt), std::begin (cur));
    }
}

inline float VoiceFilter::driveSample (float x) const noexcept
{
    if (! driveOn)
        return x;
    const float d = cur[0];
    return ap::fastTanh (x * d) / std::sqrt (d);
}

//==============================================================================
void VoiceFilter::process (float* L, float* R, int numSamples) noexcept
{
    if (numSamples <= 0 || L == nullptr || R == nullptr)
        return;

    driveOn = cur[0] > 1.0001f || (rampLeft > 0 && tgt[0] > 1.0001f);

    switch (type)
    {
        case FilterType::LP12:      runSvf<kLow, 1>   (L, R, numSamples); break;
        case FilterType::LP24:      runSvf<kLow, 2>   (L, R, numSamples); break;
        case FilterType::HP12:      runSvf<kHigh, 1>  (L, R, numSamples); break;
        case FilterType::HP24:      runSvf<kHigh, 2>  (L, R, numSamples); break;
        case FilterType::BP12:      runSvf<kBand, 1>  (L, R, numSamples); break;
        case FilterType::BP24:      runSvf<kBand, 2>  (L, R, numSamples); break;
        case FilterType::Notch:     runSvf<kNotch, 1> (L, R, numSamples); break;
        case FilterType::CombPlus:
        case FilterType::CombMinus: runComb (L, R, numSamples); break;
        case FilterType::Formant:   runFormant (L, R, numSamples); break;
        case FilterType::Ladder:    runLadder (L, R, numSamples); break;
        case FilterType::Count:
        default:                    break;
    }

    sanitiseState();
}

template <int Mode, int Stages>
void VoiceFilter::runSvf (float* L, float* R, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        if (rampLeft > 0)
        {
            stepRamp();
            deriveCoefficients();
        }
        const float k = cur[2];
        float* io[2] = { L + i, R + i };
        for (int c = 0; c < 2; ++c)
        {
            float x = driveSample (*io[c]);
            for (int s = 0; s < Stages; ++s)
                x = svfTick<Mode> (ch[c].svf[s].ic1, ch[c].svf[s].ic2, x, a1[0], a2[0], a3[0], k);
            *io[c] = x;
        }
    }
}

void VoiceFilter::runComb (float* L, float* R, int n) noexcept
{
    combDirty = true;
    for (int i = 0; i < n; ++i)
    {
        if (rampLeft > 0)
            stepRamp();

        const float delay = cur[1], fb = cur[2], norm = cur[3];
        const float rp = (float) combWrite - delay;
        const float fl = std::floor (rp);
        const float fr = rp - fl;
        const int i0 = (int) fl;
        const int idx0 = i0 & combMask;
        const int idx1 = (i0 + 1) & combMask;

        float* io[2] = { L + i, R + i };
        for (int c = 0; c < 2; ++c)
        {
            auto& st = ch[c];
            const float delayed = st.comb[(size_t) idx0] * (1.0f - fr) + st.comb[(size_t) idx1] * fr;
            st.combLp = kCombPole * st.combLp + (1.0f - kCombPole) * delayed;
            const float x = driveSample (*io[c]);
            const float y = x + fb * st.combLp;
            st.comb[(size_t) combWrite] = flushTiny (softLimit (y));
            *io[c] = y * norm;
        }
        combWrite = (combWrite + 1) & combMask;
    }
}

void VoiceFilter::runFormant (float* L, float* R, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        if (rampLeft > 0)
        {
            stepRamp();
            deriveCoefficients();
        }
        const float k = cur[9];
        float* io[2] = { L + i, R + i };
        for (int c = 0; c < 2; ++c)
        {
            const float x = driveSample (*io[c]);
            float y = 0.0f;
            for (int b = 0; b < 4; ++b)
                y += cur[5 + b] * svfTick<kBand> (ch[c].svf[b].ic1, ch[c].svf[b].ic2, x, a1[b], a2[b], a3[b], k);
            *io[c] = y;
        }
    }
}

void VoiceFilter::runLadder (float* L, float* R, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        if (rampLeft > 0)
            stepRamp();

        const float g = cur[1], k = cur[2];
        const float comp = 1.0f + 0.5f * k;
        float* io[2] = { L + i, R + i };
        for (int c = 0; c < 2; ++c)
        {
            auto& st = ch[c];
            const float x = driveSample (*io[c]);
            // 2x: linear-interpolated half-sample, then the sample itself.
            const float sub[2] = { 0.5f * (st.ladderPrevIn + x), x };
            st.ladderPrevIn = x;
            float acc = 0.0f;
            for (float in : sub)
            {
                const float u = ladderSat (in - k * st.ladder[3]);
                st.ladder[0] += g * (u - st.ladder[0]);
                st.ladder[1] += g * (st.ladder[0] - st.ladder[1]);
                st.ladder[2] += g * (st.ladder[1] - st.ladder[2]);
                st.ladder[3] += g * (st.ladder[2] - st.ladder[3]);
                acc += st.ladder[3];
            }
            *io[c] = 0.5f * acc * comp;   // average = 2:1 decimation
        }
    }
}

//==============================================================================
float filterMagnitudeDb (FilterType t, float cutoffHz, float resonance01, double sampleRate, float atHz)
{
    using cd = std::complex<double>;

    const double sr = sampleRate > 0.0 && std::isfinite (sampleRate) ? sampleRate : 44100.0;
    t = sanitiseType (t);
    const float cutoff = clampCutoff (cutoffHz, sr);
    const float res    = clamp01 (resonance01);
    const double f     = std::isfinite (atHz) ? std::clamp ((double) atHz, 1.0e-3, sr * 0.4999) : 1000.0;

    float c[10] {};
    design (t, cutoff, res, sr, c);

    const double w = ap::kTwoPiD * f / sr;
    const cd j (0.0, 1.0);
    cd h (1.0, 0.0);

    switch (familyOf (t))
    {
        case Family::Svf:
        {
            // TPT SVF == bilinear transform of the analog prototype with prewarped g.
            const cd s = j * std::tan (0.5 * w) / (double) c[1];
            const double k = (double) c[2];
            const cd den = s * s + k * s + 1.0;
            switch (t)
            {
                case FilterType::LP12: case FilterType::LP24: h = 1.0 / den; break;
                case FilterType::HP12: case FilterType::HP24: h = s * s / den; break;
                case FilterType::BP12: case FilterType::BP24: h = k * s / den; break;
                case FilterType::Notch:                       h = (s * s + 1.0) / den; break;
                case FilterType::CombPlus: case FilterType::CombMinus: case FilterType::Formant:
                case FilterType::Ladder: case FilterType::Count:
                default: break;
            }
            if (isFourPole (t))
                h *= h;
            break;
        }

        case Family::Comb:
        {
            const double delay = (double) c[1];
            const double dInt = std::floor (delay), df = delay - dInt;
            const cd z1 = std::exp (-j * w);
            const cd line = std::exp (-j * w * dInt) * ((1.0 - df) + df * z1);
            const cd lp = (1.0 - (double) kCombPole) / (1.0 - (double) kCombPole * z1);
            h = (double) c[3] / (1.0 - (double) c[2] * lp * line);
            break;
        }

        case Family::Formant:
        {
            const double k = (double) c[9];
            const cd tw = j * std::tan (0.5 * w);
            h = 0.0;
            for (int b = 0; b < 4; ++b)
            {
                const cd s = tw / (double) c[1 + b];
                h += (double) c[5 + b] * k * s / (s * s + k * s + 1.0);
            }
            break;
        }

        case Family::Ladder:
        {
            const double w2 = 0.5 * w;   // the ladder runs at 2x
            const double g = (double) c[1], k = (double) c[2];
            const cd z1 = std::exp (-j * w2);
            const cd stage = g / (1.0 - (1.0 - g) * z1);
            const cd g4 = stage * stage * stage * stage;
            h = g4 / (1.0 + k * z1 * g4) * (1.0 + 0.5 * k);
            const double interp = std::cos (0.5 * w2);   // linear-interp up, pair-average down
            h *= interp * interp * interp;
            break;
        }
    }

    const double mag = std::abs (h);
    if (! std::isfinite (mag))
        return 60.0f;
    const double db = 20.0 * std::log10 (std::max (mag, 1.0e-6));
    return (float) std::clamp (db, -120.0, 60.0);
}

} // namespace verdant
