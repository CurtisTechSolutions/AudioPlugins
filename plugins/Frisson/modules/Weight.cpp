#include "modules/Weight.h"

#include <ap/Math.h>

#include <algorithm>
#include <cmath>

namespace frisson
{

namespace
{
    constexpr float kParamSmoothMs   = 25.0f;   // amount and monoBelowHz (R8: >= 20 ms)
    constexpr float kEngageFadeMs    = 20.0f;   // linear crossfade into / out of exact bypass
    constexpr int   kControlInterval = 32;      // samples between coefficient updates while smoothing

    constexpr float kBandLowHz  = 40.0f;        // harmonic source band
    constexpr float kBandHighHz = 90.0f;
    constexpr float kHarmLowHz  = 80.0f;        // harmonic output band
    constexpr float kHarmHighHz = 250.0f;

    constexpr float kEnvAttackMs  = 5.0f;
    constexpr float kEnvReleaseMs = 80.0f;
    constexpr float kEnvFloor     = 1.0e-4f;    // divide-by-zero guard only (-80 dBFS)

    constexpr float kSecondWeight = 0.7f;
    constexpr float kThirdWeight  = 0.3f;

    // Activity ring: harmonic RMS relative to input RMS, on a dB scale so it moves
    // visibly at typical (small) effective depths. -54 dB reads 0, -18 dB reads 1;
    // a pure 50 Hz tone at amount 1 measures about -22 dB.
    constexpr float kActivityFloorDb = -54.0f;
    constexpr float kActivityFullDb  = -18.0f;
    constexpr float kActivityMs      = 80.0f;

    /** RBJ shelf midpoint that puts the shelf's turnover (pole) frequency at
        `cornerHz` for any gain: f0 = corner * 10^(gainDb / 80). The turnover stays
        put as the knob moves, like an analog shelf. */
    inline float shelfMidpointHz (float cornerHz, float gainDb) noexcept
    {
        return cornerHz * std::pow (10.0f, gainDb / 80.0f);
    }
}

void WeightModule::prepare (double newSampleRate, int /*maxBlockSize*/)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;

    amountSm.prepare (sampleRate, kParamSmoothMs);
    monoHzSm.prepare (sampleRate, kParamSmoothMs);
    engageStep = 1.0f / (0.001f * kEngageFadeMs * (float) sampleRate);

    sideSplit.prepare (sampleRate, targetMonoHz);
    shelfL.configure (ap::Biquad::Type::LowShelf, sampleRate, kShelfHz, kShelfQ, 0.0f);
    shelfR.configure (ap::Biquad::Type::LowShelf, sampleRate, kShelfHz, kShelfQ, 0.0f);

    bandHp.configure (ap::Biquad::Type::HighPass, sampleRate, kBandLowHz);
    bandLp.configure (ap::Biquad::Type::LowPass,  sampleRate, kBandHighHz);
    harmHp.configure (ap::Biquad::Type::HighPass, sampleRate, kHarmLowHz);
    harmLp.configure (ap::Biquad::Type::LowPass,  sampleRate, kHarmHighHz);

    bandEnv.prepare (sampleRate);
    bandEnv.setMode (ap::EnvelopeFollower::Mode::Peak);
    bandEnv.setTimes (kEnvAttackMs, kEnvReleaseMs);
    dcBlock.prepare (sampleRate);

    configuredShelfDb = -1.0f;
    configuredMonoHz  = -1.0f;
    reset();
}

void WeightModule::reset()
{
    resetDsp();
    fresh = true;
    activitySmoothed = 0.0f;
    activityLevel.store (0.0f, std::memory_order_relaxed);
}

void WeightModule::resetDsp() noexcept
{
    sideSplit.reset();
    shelfL.reset();
    shelfR.reset();
    bandHp.reset();
    bandLp.reset();
    harmHp.reset();
    harmLp.reset();
    bandEnv.reset();
    dcBlock.reset();
}

void WeightModule::setAmount (float amount)
{
    targetAmount = util::clamp01 (std::isfinite (amount) ? amount : 0.0f);
}

void WeightModule::setMonoBelowHz (float hz)
{
    if (! std::isfinite (hz))
        return;
    targetMonoHz = ap::clamp (hz, kMonoMinHz, kMonoMaxHz);
}

void WeightModule::updateFilters (bool force) noexcept
{
    const float shelfDb = kShelfMaxDb * amountSm.getCurrent();
    if (force || std::fabs (shelfDb - configuredShelfDb) > 1.0e-3f)
    {
        const float f0 = shelfMidpointHz (kShelfHz, shelfDb);
        shelfL.configure (ap::Biquad::Type::LowShelf, sampleRate, f0, kShelfQ, shelfDb);
        shelfR.configure (ap::Biquad::Type::LowShelf, sampleRate, f0, kShelfQ, shelfDb);
        configuredShelfDb = shelfDb;
    }

    const float monoHz = monoHzSm.getCurrent();
    if (force || std::fabs (monoHz - configuredMonoHz) > 1.0e-2f)
    {
        sideSplit.setFrequency (monoHz);
        configuredMonoHz = monoHz;
    }
}

void WeightModule::process (float* left, float* right, int numSamples) noexcept
{
    if (numSamples <= 0 || left == nullptr || right == nullptr)
        return;

    const bool wantOn = ! util::isOff (targetAmount);
    const float blockActivityCoeff = std::exp (-(float) numSamples / (0.001f * kActivityMs * (float) sampleRate));

    if (fresh)
    {
        // First block after prepare/reset: start at the targets, no glide, no fade.
        fresh = false;
        resetDsp();
        amountSm.snap (wantOn ? targetAmount : 0.0f);
        monoHzSm.snap (targetMonoHz);
        engageGain = wantOn ? 1.0f : 0.0f;
        engaged = wantOn;
        updateFilters (true);
    }
    else if (! engaged && wantOn)
    {
        // Leaving bypass: start the DSP from silence and fade it in from dry.
        resetDsp();
        amountSm.snap (0.0f);
        monoHzSm.snap (targetMonoHz);
        engageGain = 0.0f;
        engaged = true;
        updateFilters (true);
    }

    if (! engaged)
    {
        // Exact bypass: the buffer is not touched.
        activitySmoothed *= blockActivityCoeff;
        activityLevel.store (activitySmoothed, std::memory_order_relaxed);
        return;
    }

    amountSm.setTarget (wantOn ? targetAmount : 0.0f);
    monoHzSm.setTarget (targetMonoHz);
    const float engageTarget = wantOn ? 1.0f : 0.0f;
    const float harmGain = ap::dbToGain (kHarmonicMixDb);

    double harmEnergy = 0.0, inputEnergy = 0.0;

    for (int start = 0; start < numSamples; start += kControlInterval)
    {
        const int end = std::min (numSamples, start + kControlInterval);

        if (amountSm.isSmoothing() || monoHzSm.isSmoothing())
            updateFilters (false);

        for (int i = start; i < end; ++i)
        {
            const float a = amountSm.next();
            monoHzSm.next();

            if (engageGain != engageTarget)
                engageGain = engageGain < engageTarget ? std::min (engageTarget, engageGain + engageStep)
                                                       : std::max (engageTarget, engageGain - engageStep);

            const float inL = left[i];
            const float inR = right[i];   // read both before writing: a mono host passes left == right
            const float mid  = 0.5f * (inL + inR);
            const float side = 0.5f * (inL - inR);

            // 1. Mono below monoBelowHz: remove the side channel's low band. The
            //    LR4 halves sum to an all-pass, so partial depth has no notch.
            float sideLo, sideHi;
            sideSplit.process (side, sideLo, sideHi);
            const float depth = std::min (1.0f, 4.0f * a);
            const float sideOut = sideHi + (1.0f - depth) * sideLo;

            // 3. Harmonics of the 40-90 Hz band, rescaled by its own envelope.
            const float band = bandLp.process (bandHp.process (mid));
            const float env  = bandEnv.process (band);
            const float n    = ap::clamp (band / std::max (env, kEnvFloor), -1.0f, 1.0f);
            const float n2   = n * n;
            const float t2   = 2.0f * n2 - 1.0f;            // Chebyshev T2: pure 2nd harmonic of a unit sine
            const float t3   = n * (4.0f * n2 - 3.0f);      // Chebyshev T3: pure 3rd harmonic
            const float gen  = env * (kSecondWeight * t2 + kThirdWeight * t3);
            const float harm = harmLp.process (harmHp.process (dcBlock.process (gen))) * (a * harmGain);

            // 2. Sub lift on each channel, then the harmonics summed equally into L and R.
            const float wetL = shelfL.process (mid + sideOut) + harm;
            const float wetR = shelfR.process (mid - sideOut) + harm;

            harmEnergy  += (double) harm * harm;
            inputEnergy += (double) mid * mid + (double) side * side;

            if (engageGain >= 1.0f)
            {
                left[i]  = wetL;
                right[i] = wetR;
            }
            else
            {
                left[i]  = inL + engageGain * (wetL - inL);
                right[i] = inR + engageGain * (wetR - inR);
            }
        }
    }

    // R7: harmonic level relative to the input, so the ring reads the same at any level.
    const float ratio = inputEnergy > 1.0e-18 ? (float) std::sqrt (harmEnergy / inputEnergy) : 0.0f;
    const float ratioDb = ap::gainToDb (ratio, kActivityFloorDb);
    const float blockActivity = util::clamp01 ((ratioDb - kActivityFloorDb) / (kActivityFullDb - kActivityFloorDb));
    activitySmoothed = blockActivity + blockActivityCoeff * (activitySmoothed - blockActivity);
    activityLevel.store (activitySmoothed, std::memory_order_relaxed);

    if (! wantOn && engageGain <= 0.0f)
        engaged = false;   // faded out: next block is an exact bypass
}

} // namespace frisson
