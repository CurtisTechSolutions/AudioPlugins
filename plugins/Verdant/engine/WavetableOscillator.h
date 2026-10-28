#pragma once

// One wavetable oscillator with frame morphing (position), warp modes and up
// to kMaxUnison detuned unison copies. Renders stereo.
//
// Control model: setFrequency() / setParams() are called once per control
// block; render() ramps every continuous value (phase increment, position,
// warp amount, per-copy gains from level / pan / spread / blend, mip level)
// linearly from where the previous render ended to the new target across its
// samples, so modulation never zippers. noteOn() snaps everything to the
// current targets and resets the phases.
//
// Reading: frame morph = linear interpolation between frames floor (f) and
// ceil (f), f = position x (numFrames - 1); 4-point 3rd-order Lagrange
// (cubic) interpolation in phase, whose images fall much faster than linear
// interpolation's on the oversampled mips; the mip level follows each copy's own phase
// increment and crossfades into the next level over the upper half of each
// octave, so both levels it mixes are alias-free and switches are inaudible.
//
// Unison: copy i of N is detuned by detuneCents x (2i/(N-1) - 1) and panned
// by spread x the same offset, with alternating sides per symmetric pair so
// the sharp copies are not all on one side and the centre stays centred.
// The centre copy (N odd) or two centre copies (N even) have gain 1, the
// others `blend`; the sum is scaled by 1/sqrt (1 + blend^2 (N - 1)).
// Copy panning is equal-power, normalised so a centred copy has unity gain
// on both sides (a hard-panned copy reaches sqrt 2 on its side). Then `level`,
// then the oscillator `pan` as a balance control (centre = unity).
//
// Real-time safe: no allocation, locks or system calls after prepare().
//
// Owner: W-A.

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
    void noteOn (float hz, uint32_t seed);                 // resets phases (start phase + random x seed), snaps the ramps
    void setFrequency (float hz);                          // per control block, ramped inside
    void setParams (const OscParams&);                     // per control block, ramped inside
    void render (float* outL, float* outR, int numSamples, const float* fmInput = nullptr) noexcept;   // writes (replaces); fmInput mono -1..1 for FM modes
    float currentPosition() const noexcept;                // the ramped position (for the UI)

private:
    struct Targets
    {
        std::array<double, kMaxUnison> inc {};
        std::array<float, kMaxUnison> gainL {}, gainR {}, level {};
    };

    void computeTargets (Targets&) const noexcept;
    void snapToTargets() noexcept;

    double sr = 44100.0;
    const Wavetable* table = nullptr;
    OscParams params;
    float frequency = 440.0f;

    // Per-copy state (current values; the targets are recomputed at every render).
    std::array<double, kMaxUnison> phases {};
    std::array<double, kMaxUnison> incCur {};
    std::array<float, kMaxUnison> gainLCur {}, gainRCur {}, levelCur {};

    // Shared ramped values.
    float positionCur = 0.0f;
    float warpCur = 0.0f;
};

} // namespace verdant
