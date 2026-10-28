#include "WavetableOscillator.h"

#include <algorithm>
#include <cmath>

namespace verdant
{

namespace
{
    // Deterministic 0..1 value for (seed, index).
    float hash01 (uint32_t seed, uint32_t index) noexcept
    {
        uint32_t x = seed ^ (index * 0x9E3779B9u);
        x ^= x >> 16; x *= 0x7FEB352Du;
        x ^= x >> 15; x *= 0x846CA68Bu;
        x ^= x >> 16;
        return (float) (x >> 8) * (1.0f / 16777216.0f);
    }

    inline float readFrame (const float* f, double phase) noexcept
    {
        const double idx = phase * (double) kFrameSize;
        const int i0 = std::min ((int) idx, kFrameSize - 1);
        const float t = (float) (idx - (double) i0);
        return f[i0] + (f[i0 + 1] - f[i0]) * t;   // f has the wrap guard at kFrameSize
    }
}

void WavetableOscillator::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
}

void WavetableOscillator::setTable (const Wavetable* t)
{
    table = t;
}

void WavetableOscillator::noteOn (float hz, uint32_t seed)
{
    frequency = hz;
    for (int u = 0; u < kMaxUnison; ++u)
    {
        double p = (double) params.startPhase + (double) params.randomPhase * (double) hash01 (seed, (uint32_t) u);
        phases[(size_t) u] = p - std::floor (p);
    }
}

void WavetableOscillator::setFrequency (float hz)
{
    frequency = hz;
}

void WavetableOscillator::setParams (const OscParams& p)
{
    params = p;
    params.unison = std::clamp (p.unison, 1, kMaxUnison);
}

void WavetableOscillator::render (float* outL, float* outR, int numSamples, const float*)
{
    std::fill (outL, outL + numSamples, 0.0f);
    std::fill (outR, outR + numSamples, 0.0f);
    if (table == nullptr || table->numFrames() < 1 || numSamples <= 0)
        return;

    const int frames = table->numFrames();
    const float framePos = std::clamp (params.position, 0.0f, 1.0f) * (float) (frames - 1);
    const int f0 = std::min ((int) framePos, frames - 1);
    const int f1 = std::min (f0 + 1, frames - 1);
    const float morph = framePos - (float) f0;
    const float* a = table->mipFrame (0, f0);
    const float* b = table->mipFrame (0, f1);

    const int voices = params.unison;
    const float voiceGain = params.level / std::sqrt ((float) voices);
    const float pan = std::clamp (params.pan, -1.0f, 1.0f);

    for (int u = 0; u < voices; ++u)
    {
        const float offset = voices > 1 ? 2.0f * (float) u / (float) (voices - 1) - 1.0f : 0.0f;   // -1..1
        const double inc = (double) frequency * std::exp2 ((double) (params.detuneCents * offset) / 1200.0) / sr;
        const float voicePan = std::clamp (pan + params.spread * offset, -1.0f, 1.0f);
        const float gl = voiceGain * std::min (1.0f, 1.0f - voicePan);   // balance law: centre = unity on both sides
        const float gr = voiceGain * std::min (1.0f, 1.0f + voicePan);

        double phase = phases[(size_t) u];
        for (int i = 0; i < numSamples; ++i)
        {
            const float sa = readFrame (a, phase);
            const float s = sa + (readFrame (b, phase) - sa) * morph;
            outL[i] += s * gl;
            outR[i] += s * gr;
            phase += inc;
            phase -= std::floor (phase);
        }
        phases[(size_t) u] = phase;
    }
}

float WavetableOscillator::currentPosition() const noexcept
{
    return params.position;
}

} // namespace verdant
