#include "Lfo.h"

#include "Envelope.h"

#include <algorithm>
#include <cmath>

namespace verdant
{

namespace
{
    constexpr double kMaxRateHz = 1000.0;

    inline double frac (double x) noexcept
    {
        return x - std::floor (x);
    }

    inline float finiteOr (float v, float fallback) noexcept
    {
        return std::isfinite (v) ? v : fallback;
    }

    /** Avalanche hash so neighbouring seeds give unrelated S&H sequences. */
    inline uint32_t hashSeed (uint32_t x) noexcept
    {
        x ^= x >> 16; x *= 0x7FEB352Du;
        x ^= x >> 15; x *= 0x846CA68Bu;
        x ^= x >> 16;
        return x != 0 ? x : 0x9E3779B9u;
    }

    /** The default LfoShape (0 -> 1 -> 0 triangle), without touching a static vector. */
    inline float defaultShapeY (float x) noexcept
    {
        return x < 0.5f ? 2.0f * x : 2.0f - 2.0f * x;
    }
}

float evaluateLfoShape (const LfoShape& shape, float x) noexcept
{
    const auto& pts = shape.points;
    x = std::clamp (finiteOr (x, 0.0f), 0.0f, 1.0f);

    if (pts.size() < 2)
        return pts.empty() ? defaultShapeY (x) : std::clamp (finiteOr (pts[0].y, 0.0f), 0.0f, 1.0f);

    float y = x < pts.front().x ? pts.front().y : pts.back().y;
    for (size_t i = 0; i + 1 < pts.size(); ++i)
    {
        const auto& a = pts[i];
        const auto& b = pts[i + 1];
        if (x >= a.x && x <= b.x)
        {
            const float span = b.x - a.x;
            const float t = span > 1.0e-6f ? (x - a.x) / span : 1.0f;
            y = a.y + (b.y - a.y) * Envelope::shape (t, a.curve);
            break;
        }
    }
    return std::clamp (finiteOr (y, 0.0f), 0.0f, 1.0f);
}

//==============================================================================
void Lfo::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 && std::isfinite (sampleRate) ? sampleRate : 44100.0;
    ph = 0.0;
    elapsed = 0.0;
    fadeStarted = finished = smoothPrimed = false;
    smoothed = 0.0;
    out = 0.0f;
    rng.state = hashSeed (0);
    drawSampleHold();
}

void Lfo::set (const LfoParams& p) noexcept
{
    params = p;
    params.rateHz     = std::max (0.0f, finiteOr (p.rateHz, 1.0f));
    params.startPhase = std::clamp (finiteOr (p.startPhase, 0.0f), 0.0f, 1.0f);
    params.fadeMs     = std::max (0.0f, finiteOr (p.fadeMs, 0.0f));
    params.smooth     = std::clamp (finiteOr (p.smooth, 0.0f), 0.0f, 1.0f);
    if ((unsigned) params.shape >= (unsigned) LfoWave::Count)
        params.shape = LfoWave::Sine;
    if ((int) params.division < 0 || (int) params.division >= (int) ap::NoteDivision::Count)
        params.division = ap::NoteDivision::Quarter;

    if (params.mode != LfoMode::OneShot && finished)
    {
        finished = false;   // left one-shot mode: run again from the start of the cycle
        ph = 0.0;
    }
}

void Lfo::setCustomShape (const LfoShape* s) noexcept
{
    custom = s;
}

double Lfo::rateHz() const noexcept
{
    double hz = (double) params.rateHz;
    if (params.sync)
    {
        const double bpm = transport.hasTempo && transport.bpm > 0.0 && std::isfinite (transport.bpm) ? transport.bpm : 120.0;
        hz = bpm / 60.0 / ap::noteDivisionBeats (params.division);
    }
    return std::clamp (hz, 0.0, kMaxRateHz);
}

void Lfo::drawSampleHold() noexcept
{
    held = rng.nextBipolar();
}

void Lfo::noteOn (uint32_t seed) noexcept
{
    rng.state = hashSeed (seed);
    drawSampleHold();

    if (params.mode == LfoMode::Free)
    {
        if (! fadeStarted)
        {
            fadeStarted = true;
            elapsed = 0.0;
        }
        return;
    }

    ph = frac ((double) params.startPhase);
    elapsed = 0.0;
    fadeStarted = true;
    finished = false;
    smoothPrimed = false;
}

void Lfo::syncToTransport (const Transport& t) noexcept
{
    transport = t;
    if (params.mode != LfoMode::Free)
        return;

    if (! fadeStarted)
    {
        fadeStarted = true;
        elapsed = 0.0;
    }

    double newPh;
    if (params.sync && t.playing && t.hasTempo)
        newPh = frac (t.ppq / ap::noteDivisionBeats (params.division) + (double) params.startPhase);
    else
        newPh = frac ((double) t.sampleTime * rateHz() / sr + (double) params.startPhase);

    if (! std::isfinite (newPh))
        return;
    if (newPh < ph - 0.5)
        drawSampleHold();   // the transport wrapped the cycle before next() did
    ph = newPh;
}

float Lfo::shapeAt (double p) const noexcept
{
    const float x = (float) p;
    switch (params.shape)
    {
        case LfoWave::Sine:       return (float) std::sin (ap::kTwoPiD * p);
        case LfoWave::Triangle:   return x < 0.25f ? 4.0f * x : (x < 0.75f ? 2.0f - 4.0f * x : 4.0f * x - 4.0f);
        case LfoWave::SawUp:      return 2.0f * x - 1.0f;
        case LfoWave::SawDown:    return 1.0f - 2.0f * x;
        case LfoWave::Square:     return x < 0.5f ? 1.0f : -1.0f;
        case LfoWave::SampleHold: return held;
        case LfoWave::Custom:
            if (custom == nullptr || custom->points.size() < 2)
                return 2.0f * defaultShapeY (x) - 1.0f;
            return 2.0f * evaluateLfoShape (*custom, x) - 1.0f;
        case LfoWave::Count:
        default:                  return 0.0f;
    }
}

float Lfo::next (int numSamples) noexcept
{
    numSamples = std::max (0, numSamples);
    if (! fadeStarted)
    {
        fadeStarted = true;
        elapsed = 0.0;
    }

    // Shape -> smooth -> polarity -> fade.
    const double raw = (double) shapeAt (ph);
    if (! smoothPrimed || params.smooth <= 0.0f)
    {
        smoothed = raw;
        smoothPrimed = true;
    }
    else
    {
        const double tau = (double) params.smooth * 0.1 * sr;   // smooth x 100 ms, in samples
        smoothed = raw + (smoothed - raw) * std::exp (-(double) numSamples / tau);
    }

    double v = smoothed;
    if (params.unipolar)
        v = 0.5 * (v + 1.0);
    if (params.fadeMs > 0.0f)
        v *= std::min (1.0, elapsed / ((double) params.fadeMs * 0.001 * sr));
    out = std::isfinite (v) ? (float) v : 0.0f;

    // Advance.
    if (! finished)
    {
        ph += (double) numSamples * rateHz() / sr;
        if (ph >= 1.0)
        {
            if (params.mode == LfoMode::OneShot)
            {
                ph = 1.0;   // hold the end of the cycle
                finished = true;
            }
            else
            {
                ph = frac (ph);
                drawSampleHold();
            }
        }
    }
    elapsed += (double) numSamples;
    return out;
}

float Lfo::value() const noexcept
{
    return out;
}

float Lfo::phase() const noexcept
{
    return (float) ph;
}

} // namespace verdant
