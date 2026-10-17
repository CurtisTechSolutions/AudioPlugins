#pragma once

#include "Math.h"

#include <cmath>

namespace ap
{

enum class Waveform : int { Sine = 0, Triangle, Saw, Square, Count };

inline constexpr const char* waveformName (Waveform w) noexcept
{
    switch (w)
    {
        case Waveform::Sine:     return "Sine";
        case Waveform::Triangle: return "Triangle";
        case Waveform::Saw:      return "Saw";
        case Waveform::Square:   return "Square";
        case Waveform::Count:
        default:                 return "?";
    }
}

/**
    Band-limited oscillator using PolyBLEP corrections for saw and square
    (triangle is a leaky integration of the band-limited square).
*/
class PolyBlepOscillator
{
public:
    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate;
        setFrequency (freq);
    }

    void reset (float initialPhase = 0.0f) noexcept
    {
        phase = initialPhase - std::floor (initialPhase);
        // Seed the triangle integrator with the ideal value for this phase so a
        // note does not start with a DC bump.
        const float tri = phase < 0.5f ? 4.0f * phase - 1.0f : 3.0f - 4.0f * phase;
        triState = 0.25f * tri;
    }

    void setWaveform (Waveform w) noexcept   { wave = w; }
    void setPulseWidth (float pw) noexcept   { pulseWidth = clamp (pw, 0.05f, 0.95f); }

    void setFrequency (float hz) noexcept
    {
        freq = hz;
        inc = sr > 0.0 ? (float) (hz / sr) : 0.0f;
        if (inc > 0.5f) inc = 0.5f;
        if (inc < 0.0f) inc = 0.0f;
    }

    float next() noexcept
    {
        float out = 0.0f;
        switch (wave)
        {
            case Waveform::Sine:
                out = std::sin (kTwoPi * phase);
                break;

            case Waveform::Saw:
                out = 2.0f * phase - 1.0f;
                out -= polyBlep (phase, inc);
                break;

            case Waveform::Square:
                out = squareNow();
                break;

            case Waveform::Triangle:
            {
                // Integrate the band-limited square: a leaky integrator scaled by 4*inc
                // gives a triangle of roughly unit amplitude.
                const float sq = squareNow();
                triState = inc * sq + (1.0f - 0.25f * inc) * triState;   // slow leak removes DC drift
                out = clamp (4.0f * triState, -1.0f, 1.0f);
                break;
            }

            case Waveform::Count:
            default: break;
        }

        phase += inc;
        if (phase >= 1.0f) phase -= 1.0f;
        return out;
    }

    float currentPhase() const noexcept { return phase; }

private:
    float squareNow() const noexcept
    {
        float out = phase < pulseWidth ? 1.0f : -1.0f;
        out += polyBlep (phase, inc);
        float p2 = phase + 1.0f - pulseWidth;
        if (p2 >= 1.0f) p2 -= 1.0f;
        out -= polyBlep (p2, inc);
        return out;
    }

    static float polyBlep (float t, float dt) noexcept
    {
        if (dt <= 0.0f) return 0.0f;
        if (t < dt)
        {
            t /= dt;
            return t + t - t * t - 1.0f;
        }
        if (t > 1.0f - dt)
        {
            t = (t - 1.0f) / dt;
            return t * t + t + t + 1.0f;
        }
        return 0.0f;
    }

    double   sr = 44100.0;
    float    freq = 440.0f, inc = 0.01f;
    float    phase = 0.0f;
    float    pulseWidth = 0.5f;
    float    triState = 0.0f;
    Waveform wave = Waveform::Saw;
};

/** Low-frequency oscillator with a few shapes, output in [-1, 1]. */
class Lfo
{
public:
    enum class Shape : int { Sine = 0, Triangle, SawUp, SawDown, Square, SampleHold, Count };

    static constexpr const char* shapeName (Shape s) noexcept
    {
        switch (s)
        {
            case Shape::Sine:       return "Sine";
            case Shape::Triangle:   return "Triangle";
            case Shape::SawUp:      return "Saw Up";
            case Shape::SawDown:    return "Saw Down";
            case Shape::Square:     return "Square";
            case Shape::SampleHold: return "S&H";
            case Shape::Count:
            default:                return "?";
        }
    }

    void prepare (double sampleRate) noexcept { sr = sampleRate; setRate (rate); }
    void reset() noexcept { phase = 0.0f; held = rng.nextBipolar(); }
    void setShape (Shape s) noexcept { shape = s; }
    void setRate (float hz) noexcept { rate = hz; inc = sr > 0 ? (float) (hz / sr) : 0.0f; }

    float next() noexcept
    {
        float out = 0.0f;
        switch (shape)
        {
            case Shape::Sine:       out = std::sin (kTwoPi * phase); break;
            case Shape::Triangle:   out = phase < 0.5f ? 4.0f * phase - 1.0f : 3.0f - 4.0f * phase; break;
            case Shape::SawUp:      out = 2.0f * phase - 1.0f; break;
            case Shape::SawDown:    out = 1.0f - 2.0f * phase; break;
            case Shape::Square:     out = phase < 0.5f ? 1.0f : -1.0f; break;
            case Shape::SampleHold: out = held; break;
            case Shape::Count:
            default: break;
        }
        phase += inc;
        if (phase >= 1.0f)
        {
            phase -= 1.0f;
            held = rng.nextBipolar();
        }
        return out;
    }

private:
    double sr = 44100.0;
    float  rate = 1.0f, inc = 0.0f, phase = 0.0f, held = 0.0f;
    Shape  shape = Shape::Sine;
    Rng    rng;
};

} // namespace ap
