#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ap
{

inline constexpr float  kPi      = 3.14159265358979323846f;
inline constexpr float  kTwoPi   = 2.0f * kPi;
inline constexpr double kPiD     = 3.14159265358979323846;
inline constexpr double kTwoPiD  = 2.0 * kPiD;

/** MIDI note number (fractional) for a frequency, A4 = 440 Hz = 69. */
inline float hzToMidi (float hz, float a4 = 440.0f) noexcept
{
    return 69.0f + 12.0f * std::log2 (hz / a4);
}

/** Frequency of a (possibly fractional) MIDI note number. */
inline float midiToHz (float midi, float a4 = 440.0f) noexcept
{
    return a4 * std::exp2 ((midi - 69.0f) / 12.0f);
}

inline float dbToGain (float db) noexcept   { return std::pow (10.0f, db * 0.05f); }

inline float gainToDb (float gain, float floorDb = -120.0f) noexcept
{
    return gain > 0.0f ? std::max (floorDb, 20.0f * std::log10 (gain)) : floorDb;
}

inline float semitonesToRatio (float semitones) noexcept { return std::exp2 (semitones / 12.0f); }
inline float ratioToSemitones (float ratio) noexcept     { return 12.0f * std::log2 (ratio); }

template <typename T>
inline T clamp (T v, T lo, T hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

template <typename T>
inline T lerp (T a, T b, T t) noexcept { return a + (b - a) * t; }

/** One-pole coefficient for a time constant in milliseconds. */
inline float onePoleCoeff (float ms, double sampleRate) noexcept
{
    if (ms <= 0.0f) return 0.0f;
    return (float) std::exp (-1.0 / (0.001 * ms * sampleRate));
}

/** Hann window value for position t in [-halfLen, halfLen]. */
inline float hannSymmetric (float t, float halfLen) noexcept
{
    if (halfLen <= 0.0f) return 0.0f;
    const float x = std::fabs (t) / halfLen;
    if (x >= 1.0f) return 0.0f;
    return 0.5f + 0.5f * std::cos (kPi * x);
}

inline float fastTanh (float x) noexcept
{
    // Padé-style approximation, good to ~1e-3 over |x| < 3, clamps beyond.
    x = clamp (x, -3.0f, 3.0f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/** Tiny xorshift RNG for noise; deterministic, allocation-free. */
struct Rng
{
    uint32_t state = 0x9E3779B9u;

    inline uint32_t nextU32() noexcept
    {
        uint32_t x = state;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        state = x;
        return x;
    }

    /** Uniform in [-1, 1). */
    inline float nextBipolar() noexcept
    {
        return (float) (nextU32() >> 8) * (1.0f / 8388608.0f) - 1.0f;
    }

    /** Uniform in [0, 1). */
    inline float nextUnipolar() noexcept
    {
        return (float) (nextU32() >> 8) * (1.0f / 16777216.0f);
    }
};

} // namespace ap
