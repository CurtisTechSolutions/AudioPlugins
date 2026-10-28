#include "Lfo.h"

#include <algorithm>
#include <cmath>

namespace verdant
{

void Lfo::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
}

void Lfo::set (const LfoParams& p)
{
    params = p;
}

void Lfo::setCustomShape (const LfoShape* s)
{
    custom = s;
}

void Lfo::noteOn (uint32_t seed)
{
    ph = std::clamp ((double) params.startPhase, 0.0, 1.0);
    if (ph >= 1.0) ph = 0.0;
    elapsed = 0.0;
    rng = seed | 1u;
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    held = (float) (rng >> 8) * (2.0f / 16777216.0f) - 1.0f;
}

void Lfo::syncToTransport (const Transport& t)
{
    transport = t;
}

float Lfo::shapeAt (double p) noexcept
{
    constexpr double twoPi = 6.283185307179586;
    const float x = (float) p;
    switch (params.shape)
    {
        case LfoWave::Sine:       return (float) std::sin (twoPi * p);
        case LfoWave::Triangle:   return x < 0.25f ? 4.0f * x : (x < 0.75f ? 2.0f - 4.0f * x : 4.0f * x - 4.0f);
        case LfoWave::SawUp:      return 2.0f * x - 1.0f;
        case LfoWave::SawDown:    return 1.0f - 2.0f * x;
        case LfoWave::Square:     return x < 0.5f ? 1.0f : -1.0f;
        case LfoWave::SampleHold: return held;
        case LfoWave::Custom:
        {
            if (custom == nullptr || custom->points.size() < 2) return (float) std::sin (twoPi * p);
            const auto& pts = custom->points;
            for (size_t i = 1; i < pts.size(); ++i)
            {
                if (x <= pts[i].x || i + 1 == pts.size())
                {
                    const auto& a = pts[i - 1];
                    const auto& b = pts[i];
                    const float span = b.x - a.x;
                    const float t = span > 1.0e-6f ? std::clamp ((x - a.x) / span, 0.0f, 1.0f) : 1.0f;
                    return 2.0f * (a.y + (b.y - a.y) * t) - 1.0f;
                }
            }
            return 0.0f;
        }
        case LfoWave::Count:
        default:                  return 0.0f;
    }
}

float Lfo::next (int numSamples) noexcept
{
    double hz = (double) params.rateHz;
    if (params.sync)
    {
        const double bpm = transport.bpm > 0.0 ? transport.bpm : 120.0;
        hz = bpm / 60.0 / ap::noteDivisionBeats (params.division);
    }

    ph += hz * (double) numSamples / sr;
    if (ph >= 1.0)
    {
        ph -= std::floor (ph);
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        held = (float) (rng >> 8) * (2.0f / 16777216.0f) - 1.0f;   // new S&H value each cycle
    }

    elapsed += (double) numSamples;
    float v = shapeAt (ph);
    if (params.fadeMs > 0.0f)
        v *= (float) std::min (1.0, elapsed / ((double) params.fadeMs * 0.001 * sr));
    out = params.unipolar ? 0.5f * (v + 1.0f) : v;
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
