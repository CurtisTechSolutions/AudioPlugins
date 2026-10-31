#pragma once

// The living sound engine: per-note seeded variation ("Alive") and slow drift
// over held notes ("Evolve"). Deterministic for a given seed.
//
// At alive = 1: per-note random pitch +-10 ct, position +-0.08, cutoff +-0.5 oct,
// attack x[0.7, 1.3], pan +-0.25 (uniform, seeded by hash (globalSeed, noteCounter)); linear in alive.
// At evolve = 1: ap::DriftSource (fastest 0.5 Hz) adds pitch +-5 ct, position +-0.15,
// cutoff +-0.5 oct, pan +-0.2 over the held note; linear in evolve.
// `drift` (the 'Alive' mod source) is a fifth drift source scaled by max (alive, evolve).
// alive = evolve = 0 gives exactly the identity offsets (0 / attackMul 1).
//
// Owner: W-E. Audio thread: noteOn / advance never allocate.

#include "Common.h"

#include <ap/PinkNoise.h>

#include <array>
#include <cstdint>

namespace verdant
{

struct LivingOffsets
{
    float pitchCents = 0, position = 0 /* 0..1 units */, cutoffOctaves = 0, attackMul = 1, pan = 0 /* -1..1 */, drift = 0 /* -1..1 'Alive' source */;
};

class LivingVoice
{
public:
    // Bounds at alive = 1 / evolve = 1 (each part is linear in its amount).
    static constexpr float  kAlivePitchCents  = 10.0f;
    static constexpr float  kAlivePosition    = 0.08f;
    static constexpr float  kAliveCutoffOct   = 0.5f;
    static constexpr float  kAliveAttackRange = 0.3f;
    static constexpr float  kAlivePan         = 0.25f;
    static constexpr float  kEvolvePitchCents = 5.0f;
    static constexpr float  kEvolvePosition   = 0.15f;
    static constexpr float  kEvolveCutoffOct  = 0.5f;
    static constexpr float  kEvolvePan        = 0.2f;
    static constexpr double kDriftFastestHz   = 0.5;

    void prepare (double sampleRate);
    void noteOn (uint32_t globalSeed, uint32_t noteCounter, float alive01, float evolve01);
    void advance (int numSamples) noexcept;
    const LivingOffsets& offsets() const noexcept;

    /** The per-note seed noteOn derives from (globalSeed, noteCounter). */
    static uint32_t noteSeed (uint32_t globalSeed, uint32_t noteCounter) noexcept;

private:
    enum { kDriftPitch, kDriftPosition, kDriftCutoff, kDriftPan, kDriftAlive, kNumDrifts };

    void stepDrifts() noexcept;

    double sr = 44100.0;
    float alive = 0.0f, evolve = 0.0f;
    int sampleAccum = 0;
    LivingOffsets perNote;     // the seeded per-note part (Alive)
    LivingOffsets current;     // per-note + drift: what offsets() returns
    std::array<ap::DriftSource, kNumDrifts> drifts;
};

} // namespace verdant
