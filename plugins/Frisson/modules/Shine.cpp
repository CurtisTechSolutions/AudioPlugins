#include "modules/Shine.h"

#include <ap/Math.h>
#include <ap/Saturation.h>

#include <algorithm>
#include <cmath>

namespace frisson
{

namespace
{
    constexpr float kButterworthQ    = 0.70710678f;   // two cascaded = LR4

    constexpr float kEnvReleaseMs    = 50.0f;         // normaliser: instant attack, so |e| <= env
    constexpr float kEnvFloor        = 1.0e-7f;       // -140 dBFS: quieter bands are not boosted further

    constexpr float kAmountSmoothMs  = 20.0f;
    constexpr float kAirSmoothMs     = 20.0f;
    constexpr float kFreqSmoothMs    = 30.0f;

    constexpr float kActivityMs      = 100.0f;
    constexpr float kActivityLowDb   = -80.0f;        // exciter / band ratio mapped to 0
    constexpr float kActivityHighDb  = -20.0f;        // ... and to 1
    constexpr float kSilentPower     = 1.0e-14f;      // mono-sum band power below this = silent (-140 dBFS)
    constexpr float kBandGatePower   = 1.0e-6f;       // band more than 60 dB under the input = nothing to excite

    constexpr float kShelfEpsilonDb  = 1.0e-4f;
    constexpr float kFreqEpsilon     = 1.0e-6f;
}

//==============================================================================
void ShineModule::HighPass4::configure (double sampleRate, float hz) noexcept
{
    s1.configure (ap::Biquad::Type::HighPass, sampleRate, hz, kButterworthQ);
    s2.configure (ap::Biquad::Type::HighPass, sampleRate, hz, kButterworthQ);
}

float ShineModule::mixForAmount (float amount) noexcept
{
    return amount * ap::dbToGain (kMixBaseDb + kMixRangeDb * amount);
}

//==============================================================================
void ShineModule::prepare (double sampleRate, int /*maxBlockSize*/)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    const double controlRate = sr / (double) kControlInterval;

    amountSm.prepare (controlRate, kAmountSmoothMs);
    airSm.prepare (controlRate, kAirSmoothMs);
    freqMulSm.prepare (controlRate, kFreqSmoothMs);
    activityCoeff = ap::onePoleCoeff (kActivityMs, controlRate);

    for (auto& c : channels)
    {
        c.split.configure (sr, kExciteSplitHz);
        c.clean.configure (sr, kExciteCleanHz);
    }

    bandEnv.setMode (ap::EnvelopeFollower::Mode::Peak);
    bandEnv.prepare (sr);
    bandEnv.setTimes (0.0f, kEnvReleaseMs);

    reset();
}

void ShineModule::reset()
{
    amountSm.snap (amountTarget);
    airSm.snap (airTarget);
    freqMulSm.snap (freqMulTarget);
    resetState();
    applyShelf ((kShelfBaseDb + airTarget) * amountTarget, freqMulTarget);
    mix = mixForAmount (amountTarget);
    drive = 1.0f + kDriveRange * amountTarget;
    mixStep = driveStep = 0.0f;
    controlCountdown = 0;
    bypassed = util::isOff (amountTarget);
}

void ShineModule::resetState() noexcept
{
    for (auto& c : channels)
    {
        c.split.reset();
        c.clean.reset();
        c.shelf.reset();
    }
    bandEnv.reset();
    inputAcc = bandAcc = exciterAcc = 0.0f;
    inputPow = bandPow = exciterPow = 0.0f;
    accCount = 0;
    activityValue.store (0.0f, std::memory_order_relaxed);
}

void ShineModule::wakeUp (float firstL, float firstR) noexcept
{
    // Ramp in from amount 0: the shelf starts as an exact identity and the
    // exciter mix at 0, so the modulation targets need no glide. The shelf's
    // low-pass integrator starts at the current input (its steady state for
    // low-frequency content); from zero it would tick as the gain moves off 0.
    resetState();
    channels[0].shelf.ic2 = firstL;
    channels[1].shelf.ic2 = firstR;
    amountSm.snap (0.0f);
    amountSm.setTarget (amountTarget);
    airSm.snap (airTarget);
    freqMulSm.snap (freqMulTarget);
    applyShelf (0.0f, freqMulTarget);
    mix = 0.0f;
    drive = 1.0f;
    mixStep = driveStep = 0.0f;
    controlCountdown = 0;
    bypassed = false;
}

void ShineModule::setAmount (float amount)
{
    amountTarget = util::clamp01 (std::isfinite (amount) ? amount : 0.0f);
    amountSm.setTarget (amountTarget);
}

void ShineModule::setModulation (const Modulation& m)
{
    const float air = std::isfinite (m.airDb) ? m.airDb : 0.0f;
    airTarget = ap::clamp (air, 0.0f, kMaxAirDb);
    airSm.setTarget (airTarget);

    const float mul = std::isfinite (m.eqFreqMul) ? m.eqFreqMul : 1.0f;
    freqMulTarget = ap::clamp (mul, kFreqMulMin, kFreqMulMax);
    freqMulSm.setTarget (freqMulTarget);
}

//==============================================================================
void ShineModule::applyShelf (float gainDb, float freqMul) noexcept
{
    // Simper's trapezoidal SVF high shelf: the same transfer function as the RBJ
    // cookbook shelf at (designHz, Q, gainDb). gainDb == 0 gives A == 1 exactly,
    // so m0 = 1, m1 = m2 = 0 and the shelf is a bit-exact identity.
    const float designHz = std::min (kShelfCornerHz * kShelfMidRatio * freqMul, (float) (sr * 0.49));
    const float A = std::pow (10.0f, gainDb / 40.0f);
    const float g = (float) std::tan (ap::kPiD * (double) designHz / sr) * std::sqrt (A);
    const float k = 1.0f / kShelfQ;

    ShelfCoeffs& c = shelfCoeffs;
    c.a1 = 1.0f / (1.0f + g * (g + k));
    c.a2 = g * c.a1;
    c.a3 = g * c.a2;
    c.m0 = A * A;
    c.m1 = k * (1.0f - A) * A;
    c.m2 = 1.0f - A * A;

    appliedShelfDb = gainDb;
    appliedFreqMul = freqMul;
}

void ShineModule::updateControl() noexcept
{
    const float amount  = amountSm.next();
    const float air     = airSm.next();
    const float freqMul = freqMulSm.next();

    const float shelfDb = (kShelfBaseDb + air) * amount;
    if (std::fabs (shelfDb - appliedShelfDb) > kShelfEpsilonDb
        || std::fabs (freqMul - appliedFreqMul) > kFreqEpsilon)
        applyShelf (shelfDb, freqMul);

    // Mix and drive glide linearly to this step's values over the next 32 samples.
    constexpr float invInterval = 1.0f / (float) kControlInterval;
    mixStep   = (mixForAmount (amount) - mix) * invInterval;
    driveStep = (1.0f + kDriveRange * amount - drive) * invInterval;

    // Activity from the step that just finished.
    if (accCount > 0)
    {
        const float inv = 1.0f / (float) accCount;
        const float inNow   = inputAcc * inv;
        const float bandNow = bandAcc * inv;
        const float excNow  = exciterAcc * inv;
        inputPow   = inNow   + activityCoeff * (inputPow - inNow);
        bandPow    = bandNow + activityCoeff * (bandPow - bandNow);
        exciterPow = excNow  + activityCoeff * (exciterPow - excNow);
        inputAcc = bandAcc = exciterAcc = 0.0f;
        accCount = 0;

        // The exciter path counts as silent when the high band is (relatively)
        // empty: the normaliser would otherwise report the shaped rounding noise
        // of a bass-only signal as activity.
        float act = 0.0f;
        if (bandPow > kSilentPower && bandPow > kBandGatePower * inputPow && exciterPow > 0.0f)
        {
            const float ratioDb = 10.0f * std::log10 (exciterPow / bandPow);
            act = util::clamp01 ((ratioDb - kActivityLowDb) / (kActivityHighDb - kActivityLowDb));
        }
        activityValue.store (act, std::memory_order_relaxed);
    }
}

//==============================================================================
void ShineModule::process (float* left, float* right, int numSamples)
{
    if (left == nullptr || numSamples <= 0)
        return;

    // Exact bypass once the knob has ramped all the way down.
    if (bypassed && util::isOff (amountTarget))
        return;

    const bool mono = (right == nullptr || right == left);
    float* r = mono ? left : right;

    if (bypassed)
        wakeUp (left[0], r[0]);

    Channel& cl = channels[0];
    Channel& cr = channels[1];
    const ShelfCoeffs& sc = shelfCoeffs;

    ap::ExciterShaper shaper;
    shaper.even = kEvenWeight;

    // Even part of the shaper: what it generated (2nd harmonics, even intermod),
    // without the source band itself.
    auto evenPart = [&shaper] (float u) noexcept
    {
        return 0.5f * (shaper.process (u) + shaper.process (-u));
    };

    int pos = 0;
    while (pos < numSamples)
    {
        if (controlCountdown <= 0)
        {
            updateControl();
            controlCountdown = kControlInterval;
        }

        const int n = std::min (controlCountdown, numSamples - pos);

        for (int i = pos; i < pos + n; ++i)
        {
            mix   += mixStep;
            drive += driveStep;
            shaper.drive = drive;

            const float xl = left[i];
            const float xr = r[i];

            const float el = cl.split.process (xl);
            const float er = mono ? el : cr.split.process (xr);

            // Stereo-linked level normalisation: the shaper sees the band peaking at kDriveRef.
            const float env = std::max (kEnvFloor, bandEnv.process (std::max (std::fabs (el), std::fabs (er))));
            const float toShaper   = kDriveRef / env;
            const float fromShaper = env * (1.0f / kDriveRef);

            const float hl = mix * cl.clean.process (evenPart (el * toShaper) * fromShaper);
            left[i] = cl.shelf.process (xl, sc) + hl;

            if (mono)
            {
                inputAcc   += xl * xl;
                bandAcc    += el * el;
                exciterAcc += hl * hl;
            }
            else
            {
                const float hr = mix * cr.clean.process (evenPart (er * toShaper) * fromShaper);
                r[i] = cr.shelf.process (xr, sc) + hr;

                const float mx = 0.5f * (xl + xr);
                const float me = 0.5f * (el + er);
                const float mh = 0.5f * (hl + hr);
                inputAcc   += mx * mx;
                bandAcc    += me * me;
                exciterAcc += mh * mh;
            }
        }

        accCount += n;
        pos += n;
        controlCountdown -= n;
    }

    // Fully ramped down: switch to exact bypass from the next block on.
    if (util::isOff (amountTarget) && amountSm.getCurrent() < 1.0e-4f)
    {
        bypassed = true;
        activityValue.store (0.0f, std::memory_order_relaxed);
    }
}

} // namespace frisson
