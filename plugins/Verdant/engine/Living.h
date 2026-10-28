#pragma once

// The living sound engine: per-note seeded variation ("Alive") and slow drift
// over held notes ("Evolve"). Deterministic for a given seed.
//
// At alive = 1: per-note random pitch +-10 ct, position +-0.08, cutoff +-0.5 oct,
// attack x[0.7, 1.3], pan +-0.25 (uniform, seeded by hash (globalSeed, noteCounter)); linear in alive.
// At evolve = 1: ap::DriftSource (fastest 0.5 Hz) adds pitch +-5 ct, position +-0.15,
// cutoff +-0.5 oct, pan +-0.2 over the held note; linear in evolve.
//
// Owner: W-E. FOUNDATION STUB: identity offsets.

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
    void prepare (double sampleRate);
    void noteOn (uint32_t globalSeed, uint32_t noteCounter, float alive01, float evolve01);
    void advance (int numSamples) noexcept;
    const LivingOffsets& offsets() const noexcept;

private:
    double sr = 44100.0;
    LivingOffsets current;
};

} // namespace verdant
