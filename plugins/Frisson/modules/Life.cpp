#include "modules/Life.h"

#include <ap/Math.h>

#include <algorithm>
#include <cmath>

namespace frisson
{

namespace
{
    // Second smoothing pole, as a fraction of the fastest-row rate. DriftSource
    // already has one pole at the fastest rate; this makes it second order so
    // the lockstep row updates (several octaves flipping on the same step)
    // become curves instead of corners, and tilts the top octave further
    // toward 1/f² — slow drift dominates, jitter is rare.
    constexpr double kLowpassCornerRatio = 0.5;

    // Slew bound in drift units per second at rate 1 (full ±1 swing in ≥ 0.5 s).
    // A safety net that guarantees no step is ever large, whatever the seed.
    constexpr double kSlewPerSecond = 4.0;

    // Amount de-zipper: linear, full 0→1 travel in this time. Linear (not
    // exponential) so turning the knob to 0 lands on exact identity in bounded
    // time. 250 ms keeps the per-block change from a knob move below the
    // drift's own per-block motion (≤ 0.01 dB of trim per 512-sample block).
    constexpr double kAmountRampSeconds = 0.25;

    // Control-step guards: at least 32 samples per update (cheap with tiny host
    // blocks) and at most 50 ms (keeps the control rate ≥ 20 Hz, well above
    // the 4 Hz fastest row at rate 2).
    constexpr int    kMinStepSamples  = 32;
    constexpr double kMaxStepSeconds  = 0.05;

    // Blocks in a row that must be > 2× away from the nominal size before the
    // control rate is re-derived (avoids thrashing on jittery hosts).
    constexpr int kAdaptAfterBlocks = 8;
}

LifeModulator::LifeModulator()
{
    configure();
    restartSources();
}

uint32_t LifeModulator::sourceSeed (uint32_t base, int index) noexcept
{
    // Murmur3 finaliser over (seed, index): six well-separated, non-zero seeds.
    uint32_t h = base + 0x9E3779B9u * (uint32_t) (index + 1);
    h ^= h >> 16; h *= 0x85EBCA6Bu;
    h ^= h >> 13; h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h != 0u ? h : 0x6D2B79F5u;
}

void LifeModulator::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    preparedBlock = std::max (1, maxBlockSize);
    reset();
}

void LifeModulator::reset()
{
    nominalBlock = preparedBlock;
    pendingSamples = 0;
    mismatchBlocks = 0;
    appliedRate = ap::clamp (rateTarget.load (std::memory_order_relaxed), kMinRate, kMaxRate);
    seed = seedTarget.load (std::memory_order_relaxed);

    configure();
    restartSources();
    lowpass.fill (0.0f);
    drift.fill (0.0f);

    snapAmount = true;
    activityLevel.store (0.0f, std::memory_order_relaxed);
}

void LifeModulator::configure() noexcept
{
    const int maxStep = std::max (kMinStepSamples, (int) (sampleRate * kMaxStepSeconds));
    stepSamples = ap::clamp (nominalBlock, kMinStepSamples, maxStep);

    const double controlRate = sampleRate / (double) stepSamples;
    const double fastestHz = kFastestHz * (double) appliedRate;

    for (auto& s : sources)
        s.prepare (controlRate, fastestHz);

    lowpassCoeff = (float) std::exp (-ap::kTwoPiD * fastestHz * kLowpassCornerRatio / controlRate);
    maxStepPerUpdate = (float) (kSlewPerSecond * (double) appliedRate / controlRate);
    amountStepPerUpdate = (float) std::min (1.0, 1.0 / (kAmountRampSeconds * controlRate));
}

void LifeModulator::restartSources() noexcept
{
    for (int k = 0; k < kNumSources; ++k)
        sources[(size_t) k].reset (sourceSeed (seed, k));
}

void LifeModulator::setAmount (float amount)
{
    amountTarget.store (util::clamp01 (amount), std::memory_order_relaxed);
}

float LifeModulator::getAmount() const
{
    return amountTarget.load (std::memory_order_relaxed);
}

void LifeModulator::setRate (float rateMultiplier)
{
    if (std::isfinite (rateMultiplier))
        rateTarget.store (ap::clamp (rateMultiplier, kMinRate, kMaxRate), std::memory_order_relaxed);
}

float LifeModulator::getRate() const
{
    return rateTarget.load (std::memory_order_relaxed);
}

void LifeModulator::setSeed (uint32_t newSeed)
{
    seedTarget.store (newSeed, std::memory_order_relaxed);
}

uint32_t LifeModulator::getSeed() const
{
    return seedTarget.load (std::memory_order_relaxed);
}

float LifeModulator::activity() const
{
    return activityLevel.load (std::memory_order_relaxed);
}

void LifeModulator::applyPendingSettings() noexcept
{
    const float rate = rateTarget.load (std::memory_order_relaxed);
    if (std::fabs (rate - appliedRate) > 1.0e-4f)
    {
        appliedRate = rate;
        configure();
    }

    const uint32_t wanted = seedTarget.load (std::memory_order_relaxed);
    if (wanted != seed)
    {
        // Only the generators restart; the smoothing poles and slew keep their
        // state, so the output glides onto the new series.
        seed = wanted;
        restartSources();
    }
}

void LifeModulator::trackBlockSize (int numSamples) noexcept
{
    const bool farOff = numSamples > 2 * nominalBlock || 2 * numSamples < nominalBlock;
    if (! farOff)
    {
        mismatchBlocks = 0;
        return;
    }

    if (++mismatchBlocks >= kAdaptAfterBlocks)
    {
        mismatchBlocks = 0;
        nominalBlock = numSamples;
        configure();
    }
}

void LifeModulator::stepSources (float targetAmount) noexcept
{
    for (size_t k = 0; k < (size_t) kNumSources; ++k)
    {
        const float x = sources[k].next();
        lowpass[k] = x + lowpassCoeff * (lowpass[k] - x);
        const float delta = ap::clamp (lowpass[k] - drift[k], -maxStepPerUpdate, maxStepPerUpdate);
        drift[k] = ap::clamp (drift[k] + delta, -1.0f, 1.0f);
    }

    amountCurrent += ap::clamp (targetAmount - amountCurrent, -amountStepPerUpdate, amountStepPerUpdate);
}

Modulation LifeModulator::analyze (const float* /*left*/, const float* /*right*/, int numSamples) noexcept
{
    // LIFE is a clock: the audio is never read.
    applyPendingSettings();

    const float target = amountTarget.load (std::memory_order_relaxed);
    if (snapAmount)
    {
        amountCurrent = target;
        snapAmount = false;
    }

    if (numSamples > 0)
    {
        trackBlockSize (numSamples);
        pendingSamples += numSamples;
        while (pendingSamples >= stepSamples)
        {
            pendingSamples -= stepSamples;
            stepSources (target);
        }
    }

    const bool knobOff = util::isOff (target);
    activityLevel.store (knobOff ? 0.0f : std::fabs (drift[0]), std::memory_order_relaxed);

    // Off: the drift keeps running (so turning it back up resumes the same
    // series), but the output is exact identity once the ramp has landed.
    if (knobOff && util::isOff (amountCurrent))
        return Modulation::identity();

    const float a = amountCurrent;

    Modulation m;
    m.eqFreqMul    = std::exp2 (kEqOctaves * a * drift[0]);
    m.driveMul     = 1.0f + kDriveDepth * a * drift[1];
    m.widthMul     = 1.0f + kWidthDepth * a * drift[2];
    m.gainL        = ap::dbToGain (kGainDb * a * drift[3]);
    m.gainR        = ap::dbToGain (kGainDb * a * drift[4]);
    m.transientMul = 1.0f + kTransientDepth * a * drift[5];
    m.airDb        = 0.0f;
    m.densityMul   = 1.0f;
    return m;
}

} // namespace frisson
