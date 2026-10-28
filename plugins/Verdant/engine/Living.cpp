#include "Living.h"

#include <ap/Math.h>

#include <algorithm>

namespace verdant
{

uint32_t LivingVoice::noteSeed (uint32_t globalSeed, uint32_t noteCounter) noexcept
{
    // splitmix64 finaliser over both words: flipping any input bit changes about
    // half the output bits, so consecutive note counters give unrelated variation.
    uint64_t z = (((uint64_t) globalSeed << 32) | (uint64_t) noteCounter) + 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return (uint32_t) (z ^ (z >> 32));
}

void LivingVoice::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;

    // The drift sources step once per control block.
    const double controlRate = sr / (double) kControlInterval;
    for (auto& d : drifts)
        d.prepare (controlRate, kDriftFastestHz);

    alive = evolve = 0.0f;
    sampleAccum = 0;
    perNote = LivingOffsets();
    current = LivingOffsets();
}

void LivingVoice::noteOn (uint32_t globalSeed, uint32_t noteCounter, float alive01, float evolve01)
{
    alive  = ap::clamp (alive01, 0.0f, 1.0f);
    evolve = ap::clamp (evolve01, 0.0f, 1.0f);
    sampleAccum = 0;

    const uint32_t seed = noteSeed (globalSeed, noteCounter);
    ap::Rng rng;
    rng.state = seed | 1u;   // xorshift must never hold 0

    // Always draw all five so a note's variation does not depend on the amounts.
    const float uPitch = rng.nextBipolar();
    const float uPos   = rng.nextBipolar();
    const float uCut   = rng.nextBipolar();
    const float uAtk   = rng.nextBipolar();
    const float uPan   = rng.nextBipolar();

    perNote = LivingOffsets();
    if (alive > 0.0f)
    {
        perNote.pitchCents    = uPitch * kAlivePitchCents * alive;
        perNote.position      = uPos * kAlivePosition * alive;
        perNote.cutoffOctaves = uCut * kAliveCutoffOct * alive;
        perNote.attackMul     = 1.0f + uAtk * kAliveAttackRange * alive;
        perNote.pan           = uPan * kAlivePan * alive;
    }

    for (int k = 0; k < kNumDrifts; ++k)
        drifts[(size_t) k].reset (seed + 0x9E3779B9u * (uint32_t) (k + 1));

    // reset() zeroes each drift source's smoother, so evolve fades in over the
    // first few hundred ms of the note instead of jumping at note-on.
    current = perNote;
}

void LivingVoice::stepDrifts() noexcept
{
    const float dPitch = drifts[kDriftPitch].next();
    const float dPos   = drifts[kDriftPosition].next();
    const float dCut   = drifts[kDriftCutoff].next();
    const float dPan   = drifts[kDriftPan].next();
    const float dAlive = drifts[kDriftAlive].next();

    current = perNote;
    if (evolve > 0.0f)
    {
        current.pitchCents    += dPitch * kEvolvePitchCents * evolve;
        current.position      += dPos * kEvolvePosition * evolve;
        current.cutoffOctaves += dCut * kEvolveCutoffOct * evolve;
        current.pan           += dPan * kEvolvePan * evolve;
    }
    current.drift = dAlive * std::max (alive, evolve);
}

void LivingVoice::advance (int numSamples) noexcept
{
    // Both amounts zero: nothing moves, the offsets stay exactly identity.
    if (numSamples <= 0 || (alive <= 0.0f && evolve <= 0.0f))
        return;

    // One drift step per kControlInterval samples, carrying partial blocks over.
    sampleAccum += numSamples;
    while (sampleAccum >= kControlInterval)
    {
        sampleAccum -= kControlInterval;
        stepDrifts();
    }
}

const LivingOffsets& LivingVoice::offsets() const noexcept
{
    return current;
}

} // namespace verdant
