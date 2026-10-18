#include "modules/Groove.h"

#include <ap/Math.h>

#include <cmath>

namespace frisson
{

namespace
{
    // dB -> linear via exp (cheaper than pow, same result to float precision).
    inline float dbToLin (float db) noexcept { return std::exp (db * 0.11512925464970229f); }

    // Below this the knob has faded out far enough that the processed signal is
    // indistinguishable from the input (gain error < 1e-5 dB), so we can hand
    // over to the exact bypass path.
    constexpr float kBypassAmountEpsilon = 1.0e-6f;

    // Shelf redesign is skipped while its gain moves less than this (keeps the
    // steady state cheap; small enough that the bypass hand-over is inaudible).
    constexpr float kShelfEpsilonDb = 1.0e-4f;
}

void GrooveModule::prepare (double sampleRate, int maxBlockSize)
{
    (void) maxBlockSize;   // nothing block-sized to allocate: all state is per sample
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;

    detector.prepare (sr);
    amountSmoother.prepare (sr, kAmountSmoothMs);
    mulSmoother.prepare (sr, kAmountSmoothMs);
    gainSmoother.prepare (sr, kGainSmoothMs);
    shelfSmoother.prepare (sr, kGainSmoothMs);

    reset();
}

void GrooveModule::reset()
{
    detector.reset();
    amountSmoother.snap (effectiveAmount());
    mulSmoother.snap (transientMul);
    weight = 1.0f;
    samplesSinceTransport = 0;
    activityValue.store (0.0f, std::memory_order_relaxed);
    enterBypass();
    fresh = true;
}

void GrooveModule::enterBypass() noexcept
{
    bypassed = true;
    gainSmoother.snap (1.0f);
    shelfSmoother.snap (0.0f);
    lastShelfDb = 0.0f;
    shelfCountdown = 0;
    shelfL.configure (ap::Biquad::Type::HighShelf, sr, kSnapShelfHz, kSnapShelfQ, 0.0f);
    shelfR.configure (ap::Biquad::Type::HighShelf, sr, kSnapShelfHz, kSnapShelfQ, 0.0f);
    shelfL.reset();
    shelfR.reset();
}

float GrooveModule::effectiveAmount() const noexcept
{
    // Anything isOff() treats as zero fades to exactly zero, so the bypass hand-over is reached.
    return util::isOff (amountTarget) ? 0.0f : amountTarget;
}

void GrooveModule::setAmount (float amount)
{
    amountTarget = util::clamp01 (std::isfinite (amount) ? amount : 0.0f);
    amountSmoother.setTarget (effectiveAmount());
    if (fresh)
        amountSmoother.snap (effectiveAmount());   // first setting after reset: no fade-in
}

void GrooveModule::setModulation (const Modulation& m)
{
    const float mul = std::isfinite (m.transientMul) ? m.transientMul : 1.0f;
    transientMul = ap::clamp (mul, kMinTransientMul, kMaxTransientMul);
    mulSmoother.setTarget (transientMul);
    if (fresh)
        mulSmoother.snap (transientMul);
}

void GrooveModule::setTransport (const Transport& t)
{
    transport = t;
    samplesSinceTransport = 0;
}

bool GrooveModule::pocketActive() const noexcept
{
    return pocketEnabled && transport.hasTempo && transport.playing && transport.bpm > 0.0;
}

float GrooveModule::gridWeight (double ppq, double bpm) noexcept
{
    if (! (bpm > 0.0) || ! std::isfinite (ppq))
        return 1.0f;

    const double nearest = std::round (ppq / kGridBeats) * kGridBeats;
    const double distSec = std::fabs (ppq - nearest) * 60.0 / bpm;
    return distSec <= kPocketWindowSec ? kOnGridWeight : kOffGridWeight;
}

void GrooveModule::process (float* left, float* right, int numSamples)
{
    if (left == nullptr || numSamples <= 0)
        return;

    // A mono host passes the same buffer twice: process it once.
    const bool mono = (right == nullptr || right == left);
    const bool wantOn = ! util::isOff (amountTarget);
    const bool pocket = pocketActive();
    const double beatsPerSample = pocket ? transport.bpm / (60.0 * sr) : 0.0;

    if (bypassed && wantOn)
    {
        // Leaving bypass: filters and gains start from identity, the knob fades in.
        enterBypass();
        bypassed = false;
        amountSmoother.snap (fresh ? effectiveAmount() : 0.0f);
        amountSmoother.setTarget (effectiveAmount());
    }
    fresh = false;

    if (bypassed)
    {
        // Exact bypass. The detector keeps listening (read-only) so that turning
        // the knob up never acts on a stale envelope or a stale pocket weight.
        for (int i = 0; i < numSamples; ++i)
        {
            const float x = mono ? left[i] : 0.5f * (left[i] + right[i]);
            if (detector.process (x))
                weight = pocket ? gridWeight (transport.ppqPosition + (double) samplesSinceTransport * beatsPerSample,
                                              transport.bpm)
                                : 1.0f;
            ++samplesSinceTransport;
        }

        const float decay = std::exp (-(float) numSamples / (float) (0.15 * sr));
        activityValue.store (activityValue.load (std::memory_order_relaxed) * decay, std::memory_order_relaxed);
        return;
    }

    float blockPeakAttackDb = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const float x = mono ? left[i] : 0.5f * (left[i] + right[i]);

        if (detector.process (x))
            weight = pocket ? gridWeight (transport.ppqPosition + (double) samplesSinceTransport * beatsPerSample,
                                          transport.bpm)
                            : 1.0f;
        ++samplesSinceTransport;

        const float amt = amountSmoother.next();
        const float mul = mulSmoother.next();
        const float w   = pocket ? weight : 1.0f;   // transport stopped / pocket off: neutral at once

        const float attackDb  = kAttackRangeDb * amt * mul * detector.attackAmount() * w;
        const float sustainDb = kSustainRangeDb * amt * detector.sustainAmount();
        blockPeakAttackDb = std::max (blockPeakAttackDb, attackDb);

        gainSmoother.setTarget (dbToLin (attackDb + sustainDb));
        shelfSmoother.setTarget (kSnapRatio * attackDb);
        const float g = gainSmoother.next();
        const float shelfDb = shelfSmoother.next();

        if (--shelfCountdown <= 0)
        {
            shelfCountdown = kShelfUpdateSamples;
            if (std::fabs (shelfDb - lastShelfDb) > kShelfEpsilonDb)
            {
                // Both channels always, so a mono -> stereo switch never finds a stale right shelf.
                lastShelfDb = shelfDb;
                shelfL.configure (ap::Biquad::Type::HighShelf, sr, kSnapShelfHz, kSnapShelfQ, shelfDb);
                shelfR.configure (ap::Biquad::Type::HighShelf, sr, kSnapShelfHz, kSnapShelfQ, shelfDb);
            }
        }

        left[i] = shelfL.process (left[i]) * g;
        if (! mono)
            right[i] = shelfR.process (right[i]) * g;
    }

    // Activity: peak attack emphasis of this block (normalised by the 6 dB range),
    // with a 150 ms fall so a UI polling at 20 Hz still sees short hits.
    const float decay = std::exp (-(float) numSamples / (float) (0.15 * sr));
    const float now = ap::clamp (blockPeakAttackDb / kAttackRangeDb, 0.0f, 1.0f);
    activityValue.store (std::max (now, activityValue.load (std::memory_order_relaxed) * decay),
                         std::memory_order_relaxed);

    // Knob faded out: hand over to the exact bypass path.
    if (! wantOn && amountSmoother.getCurrent() < kBypassAmountEpsilon)
        enterBypass();
}

} // namespace frisson
