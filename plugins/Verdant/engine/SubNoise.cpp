#include "SubNoise.h"

#include <algorithm>
#include <cmath>

namespace verdant
{

namespace
{
    // Every colour is scaled to about this RMS (white noise at full scale is 0.577).
    constexpr float kNoiseRms = 0.4f;

    // Measured RMS of each raw colour (20 s of noise; independent of the sample rate
    // except brown, which is normalised analytically below).
    constexpr float kWhiteRms  = 0.5774f;   // uniform -1..1
    constexpr float kPinkRms   = 0.4332f;   // ap::PinkNoise
    constexpr float kBlueRms   = 0.2584f;   // first difference of ap::PinkNoise
    constexpr float kVioletRms = 0.8165f;   // first difference of uniform white (sqrt (2/3))

    // Brown: a leaky integrator of white noise whose leak corner sits at 20 Hz, so it
    // stays DC-free and its level does not depend on the sample rate.
    constexpr double kBrownCornerHz = 20.0;

    inline ap::Waveform waveformFor (SubWave w) noexcept
    {
        switch (w)
        {
            case SubWave::Triangle: return ap::Waveform::Triangle;
            case SubWave::Square:   return ap::Waveform::Square;
            case SubWave::Saw:      return ap::Waveform::Saw;
            case SubWave::Sine:
            case SubWave::Count:
            default:                return ap::Waveform::Sine;
        }
    }
}

//==============================================================================
void SubOscillator::prepare (double sampleRate) noexcept
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    osc.prepare (sr);
    osc.setWaveform (ap::Waveform::Sine);
}

void SubOscillator::noteOn (float hz) noexcept
{
    currentHz = targetHz = std::max (0.0f, hz);
    osc.setFrequency (currentHz);
    osc.reset (0.0f);
}

void SubOscillator::setWave (SubWave w) noexcept
{
    osc.setWaveform (waveformFor (w));
}

void SubOscillator::setFrequency (float hz) noexcept
{
    targetHz = std::clamp (hz, 0.0f, (float) (sr * 0.45));
}

void SubOscillator::render (float* out, int numSamples) noexcept
{
    if (numSamples <= 0)
        return;

    if (currentHz == targetHz)
    {
        osc.setFrequency (currentHz);
        for (int i = 0; i < numSamples; ++i)
            out[i] = osc.next();
        return;
    }

    // Linear frequency ramp across the block: glide and pitch modulation stay smooth.
    const float step = (targetHz - currentHz) / (float) numSamples;
    for (int i = 0; i < numSamples; ++i)
    {
        currentHz += step;
        osc.setFrequency (currentHz);
        out[i] = osc.next();
    }
    currentHz = targetHz;
}

//==============================================================================
void NoiseGenerator::prepare (double sampleRate) noexcept
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    brownLeak = (float) std::exp (-ap::kTwoPiD * kBrownCornerHz / sr);
    // A leaky integrator of white noise (variance s^2) has variance s^2 / (1 - leak^2).
    brownGain = std::sqrt (1.0f - brownLeak * brownLeak) * (kNoiseRms / kWhiteRms);
    reset (0x6E01u);
}

void NoiseGenerator::reset (uint32_t seed) noexcept
{
    white.state = (seed * 0x9E3779B9u) | 1u;
    pink.reset ((seed ^ 0x51ED27A3u) | 1u);
    brownState = 0.0f;
    lastWhite = lastPink = 0.0f;
}

void NoiseGenerator::setColour (NoiseColour c) noexcept
{
    colour = c < NoiseColour::Count ? c : NoiseColour::White;
}

void NoiseGenerator::render (float* out, int numSamples) noexcept
{
    switch (colour)
    {
        case NoiseColour::Pink:
            for (int i = 0; i < numSamples; ++i)
                out[i] = pink.next() * (kNoiseRms / kPinkRms);
            break;

        case NoiseColour::Brown:
            for (int i = 0; i < numSamples; ++i)
            {
                brownState = brownLeak * brownState + white.nextBipolar();
                out[i] = brownState * brownGain;
            }
            break;

        case NoiseColour::Blue:
            for (int i = 0; i < numSamples; ++i)
            {
                const float p = pink.next();
                out[i] = (p - lastPink) * (kNoiseRms / kBlueRms);
                lastPink = p;
            }
            break;

        case NoiseColour::Violet:
            for (int i = 0; i < numSamples; ++i)
            {
                const float w = white.nextBipolar();
                out[i] = (w - lastWhite) * (kNoiseRms / kVioletRms);
                lastWhite = w;
            }
            break;

        case NoiseColour::White:
        case NoiseColour::Count:
        default:
            for (int i = 0; i < numSamples; ++i)
                out[i] = white.nextBipolar() * (kNoiseRms / kWhiteRms);
            break;
    }
}

} // namespace verdant
