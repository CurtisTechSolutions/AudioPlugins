#pragma once

// The voice's sub oscillator and noise generator (CONTRACT §3 "Sub & Noise").
//
//  * SubOscillator: a band-limited ap::PolyBlepOscillator (sine / triangle /
//    square / saw, in the sub_wave choice order) one or two octaves below the
//    note. The frequency is set once per control block and ramped linearly per
//    sample inside render(), like the wavetable oscillators.
//  * NoiseGenerator: White (uniform ap::Rng), Pink (ap::PinkNoise), Brown
//    (leaky-integrated white), Blue (differentiated pink), Violet
//    (differentiated white), each normalised to about the same RMS (~0.4, so
//    peaks stay near +-1) and seeded per voice for determinism.
//
// Both are allocation-free and noexcept after prepare(). Owner: W-D.

#include <ap/Math.h>
#include <ap/Oscillator.h>
#include <ap/PinkNoise.h>

#include <cstdint>

namespace verdant
{

enum class SubWave : uint8_t { Sine, Triangle, Square, Saw, Count };            // sub_wave choice order
enum class NoiseColour : uint8_t { White, Pink, Brown, Blue, Violet, Count };   // noise_color choice order

class SubOscillator
{
public:
    void prepare (double sampleRate) noexcept;

    /** Starts a note: phase 0 (deterministic, click-free for the sine) at `hz`. */
    void noteOn (float hz) noexcept;

    void setWave (SubWave) noexcept;

    /** Per control block; render() ramps from the previous frequency to this one. */
    void setFrequency (float hz) noexcept;

    /** Writes (replaces) numSamples of mono output, -1..1. */
    void render (float* out, int numSamples) noexcept;

private:
    double sr = 44100.0;
    ap::PolyBlepOscillator osc;
    float currentHz = 0.0f, targetHz = 0.0f;
};

class NoiseGenerator
{
public:
    void prepare (double sampleRate) noexcept;

    /** Re-seeds every colour's state (call at note-on with the voice seed). */
    void reset (uint32_t seed) noexcept;

    void setColour (NoiseColour) noexcept;

    /** Writes (replaces) numSamples of mono noise in the current colour. */
    void render (float* out, int numSamples) noexcept;

private:
    double sr = 44100.0;
    NoiseColour colour = NoiseColour::White;
    ap::Rng white;
    ap::PinkNoise pink;
    float brownState = 0.0f, brownLeak = 0.997f, brownGain = 0.05f;
    float lastWhite = 0.0f, lastPink = 0.0f;
};

} // namespace verdant
