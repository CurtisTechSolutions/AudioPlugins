#pragma once

// One wavetable oscillator with frame morphing (position), warp modes and up
// to kMaxUnison detuned unison voices. Renders stereo; per-control-block
// settings are ramped inside render().
//
// Owner: W-A. FOUNDATION STUB: linear interpolation in phase and between
// frames at `position`, level / pan / start phase, unison as plain detuned
// copies spread across the stereo field (1 / sqrt(unison) headroom); warp,
// blend and mip levels are ignored.

#include "Common.h"
#include "Wavetable.h"

#include <array>
#include <cstdint>

namespace verdant
{

enum class WarpMode : uint8_t { Off, Sync, Bend, Squeeze, Mirror, FmOsc, FmSub, Remap, Count };

struct OscParams
{
    float position = 0;                        // 0..1 (fractional frame index = position x (numFrames - 1))
    WarpMode warp = WarpMode::Off;
    float warpAmount = 0;                      // -1..1
    int unison = 1;
    float detuneCents = 15, spread = 0.5f, blend = 0.75f;   // spread / blend 0..1
    float startPhase = 0;                      // 0..1
    float randomPhase = 1;                     // 0..1
    float level = 1, pan = 0;                  // pan -1..1
};

class WavetableOscillator
{
public:
    void prepare (double sampleRate);
    void setTable (const Wavetable*);
    void noteOn (float hz, uint32_t seed);                 // resets phases (start phase + random x seed)
    void setFrequency (float hz);                          // per control block, ramped inside
    void setParams (const OscParams&);                     // per control block, ramped inside
    void render (float* outL, float* outR, int numSamples, const float* fmInput = nullptr);   // writes (replaces); fmInput mono -1..1 for FM modes
    float currentPosition() const noexcept;                // for the UI

private:
    double sr = 44100.0;
    const Wavetable* table = nullptr;
    OscParams params;
    float frequency = 440.0f;
    std::array<double, kMaxUnison> phases {};
};

} // namespace verdant
