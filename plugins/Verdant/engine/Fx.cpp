#include "Fx.h"

#include <algorithm>
#include <cmath>

namespace verdant
{

namespace
{
    constexpr float kEngageMs = 10.0f;

    inline float pv (const ParamValues& v, P p) noexcept     { return v[(size_t) p]; }
    inline float pct (const ParamValues& v, P p) noexcept    { return v[(size_t) p] * 0.01f; }
    inline int choice (const ParamValues& v, P p) noexcept   { return (int) std::lround (v[(size_t) p]); }
    inline bool flag (const ParamValues& v, P p) noexcept    { return v[(size_t) p] > 0.5f; }

    template <typename T>
    inline T enumFrom (int i, T fallback) noexcept
    {
        return i >= 0 && i < (int) T::Count ? (T) i : fallback;
    }
}

//==============================================================================
DistortionParams distortionParamsFrom (const ParamValues& v) noexcept
{
    DistortionParams p;
    p.type = enumFrom (choice (v, P::DistType), DistType::Soft);
    p.drive = pct (v, P::DistDrive);
    p.toneHz = pv (v, P::DistTone);
    p.mix = pct (v, P::DistMix);
    return p;
}

ChorusParams chorusParamsFrom (const ParamValues& v) noexcept
{
    ChorusParams p;
    p.rateHz = pv (v, P::ChorusRate);
    p.depth = pct (v, P::ChorusDepth);
    p.feedback = pct (v, P::ChorusFeedback);
    p.mix = pct (v, P::ChorusMix);
    return p;
}

PhaserParams phaserParamsFrom (const ParamValues& v) noexcept
{
    constexpr int kStages[4] = { 4, 6, 8, 12 };
    PhaserParams p;
    p.rateHz = pv (v, P::PhaserRate);
    p.depth = pct (v, P::PhaserDepth);
    p.feedback = pct (v, P::PhaserFeedback);
    p.stages = kStages[std::clamp (choice (v, P::PhaserStages), 0, 3)];
    p.mix = pct (v, P::PhaserMix);
    return p;
}

FlangerParams flangerParamsFrom (const ParamValues& v) noexcept
{
    FlangerParams p;
    p.rateHz = pv (v, P::FlangerRate);
    p.depth = pct (v, P::FlangerDepth);
    p.feedback = pct (v, P::FlangerFeedback);
    p.mix = pct (v, P::FlangerMix);
    return p;
}

DelayParams delayParamsFrom (const ParamValues& v) noexcept
{
    DelayParams p;
    p.timeMs = pv (v, P::DelayTime);
    p.sync = flag (v, P::DelaySync);
    p.division = enumFrom (choice (v, P::DelayDiv), ap::NoteDivision::Eighth);
    p.feedback = pct (v, P::DelayFeedback);
    p.pingPong = flag (v, P::DelayPingPong);
    p.lowCutHz = pv (v, P::DelayLowCut);
    p.highCutHz = pv (v, P::DelayHighCut);
    p.mix = pct (v, P::DelayMix);
    return p;
}

ReverbParams reverbParamsFrom (const ParamValues& v) noexcept
{
    ReverbParams p;
    p.size = pct (v, P::ReverbSize);
    p.decay = pct (v, P::ReverbDecay);
    p.damp = pct (v, P::ReverbDamp);
    p.predelayMs = pv (v, P::ReverbPredelay);
    p.width = pct (v, P::ReverbWidth);
    p.mix = pct (v, P::ReverbMix);
    return p;
}

EqParams eqParamsFrom (const ParamValues& v) noexcept
{
    EqParams p;
    p.lowGainDb = pv (v, P::EqLowGain);
    p.lowHz = pv (v, P::EqLowFreq);
    p.midGainDb = pv (v, P::EqMidGain);
    p.midHz = pv (v, P::EqMidFreq);
    p.midQ = pv (v, P::EqMidQ);
    p.highGainDb = pv (v, P::EqHighGain);
    p.highHz = pv (v, P::EqHighFreq);
    return p;
}

CompressorParams compressorParamsFrom (const ParamValues& v) noexcept
{
    CompressorParams p;
    p.thresholdDb = pv (v, P::CompThreshold);
    p.ratio = pv (v, P::CompRatio);
    p.attackMs = pv (v, P::CompAttack);
    p.releaseMs = pv (v, P::CompRelease);
    p.makeupDb = pv (v, P::CompMakeup);
    p.mix = pct (v, P::CompMix);
    return p;
}

//==============================================================================
void FxRack::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    maxBlockSize = std::max (1, maxBlock);

    distortion.prepare (sr, maxBlockSize);
    chorus.prepare (sr, maxBlockSize);
    phaser.prepare (sr, maxBlockSize);
    flanger.prepare (sr, maxBlockSize);
    delay.prepare (sr, maxBlockSize);
    reverb.prepare (sr, maxBlockSize);
    eq.prepare (sr, maxBlockSize);
    compressor.prepare (sr, maxBlockSize);

    for (auto& e : engage)
        e.prepare (sr, kEngageMs);

    dryL.assign ((size_t) maxBlockSize, 0.0f);
    dryR.assign ((size_t) maxBlockSize, 0.0f);
    reset();
}

void FxRack::reset()
{
    distortion.reset();
    chorus.reset();
    phaser.reset();
    flanger.reset();
    delay.reset();
    reverb.reset();
    eq.reset();
    compressor.reset();

    for (auto& e : engage)
        e.snap (0.0f);
    chainSize = 0;
    pendingSize = 0;
    hasPending = false;
    fresh = true;
    tail.store (0.0f, std::memory_order_relaxed);
}

int FxRack::resolveChain (const ParamValues& v, FxType* out) noexcept
{
    int n = 0;
    bool seen[(size_t) FxType::Count] = {};
    for (int s = 0; s < kNumFxSlots; ++s)
    {
        const auto t = enumFrom (choice (v, fxSlotType (s)), FxType::None);
        if (t == FxType::None || seen[(size_t) t])
            continue;
        seen[(size_t) t] = true;
        out[n++] = t;
    }
    return n;
}

int FxRack::currentChain (FxType* out) const noexcept
{
    for (int i = 0; i < chainSize; ++i)
        out[i] = chain[(size_t) i];
    return chainSize;
}

void FxRack::setParams (const ParamValues& v, const Transport& transport)
{
    distortion.set (distortionParamsFrom (v));
    chorus.set (chorusParamsFrom (v));
    phaser.set (phaserParamsFrom (v));
    flanger.set (flangerParamsFrom (v));
    delay.set (delayParamsFrom (v), transport);
    reverb.set (reverbParamsFrom (v));
    eq.set (eqParamsFrom (v));
    compressor.set (compressorParamsFrom (v));

    std::array<FxType, kMaxChain> desired {};
    const int count = resolveChain (v, desired.data());

    if (fresh)
    {
        // First block after prepare / reset: start with the chain fully engaged, no fade-in.
        for (auto& e : engage) e.snap (0.0f);
        for (int i = 0; i < count; ++i)
        {
            chain[(size_t) i] = desired[(size_t) i];
            engage[(size_t) desired[(size_t) i]].snap (1.0f);
        }
        chainSize = count;
        fresh = false;
    }
    else
    {
        applyDesired (desired.data(), count);
    }
    computeTail();
}

void FxRack::applyDesired (const FxType* desired, int count) noexcept
{
    if (hasPending)
    {
        // Still fading the old order out: just retarget what comes next.
        for (int i = 0; i < count; ++i) pending[(size_t) i] = desired[i];
        pendingSize = count;
        return;
    }

    bool inDesired[(size_t) FxType::Count] = {}, inChain[(size_t) FxType::Count] = {};
    for (int i = 0; i < count; ++i) inDesired[(size_t) desired[i]] = true;
    for (int i = 0; i < chainSize; ++i) inChain[(size_t) chain[(size_t) i]] = true;

    // Units present on both sides must keep their relative order, else we cannot morph in place.
    bool compatible = true;
    {
        int j = 0;
        for (int i = 0; i < chainSize && compatible; ++i)
        {
            const auto t = chain[(size_t) i];
            if (! inDesired[(size_t) t]) continue;
            while (j < count && ! inChain[(size_t) desired[j]]) ++j;
            compatible = j < count && desired[j] == t;
            ++j;
        }
    }

    if (! compatible)
    {
        for (int i = 0; i < chainSize; ++i)
            engage[(size_t) chain[(size_t) i]].setTarget (0.0f);
        for (int i = 0; i < count; ++i) pending[(size_t) i] = desired[i];
        pendingSize = count;
        hasPending = true;
        return;
    }

    // Merge: fading-out units stay where they are, new units are inserted where the slots put them.
    std::array<FxType, kMaxChain> merged {};
    int n = 0, i = 0, j = 0;
    while ((i < chainSize || j < count) && n < kMaxChain)
    {
        if (i < chainSize && ! inDesired[(size_t) chain[(size_t) i]])
        {
            engage[(size_t) chain[(size_t) i]].setTarget (0.0f);
            merged[(size_t) n++] = chain[(size_t) i++];
        }
        else if (j < count && ! inChain[(size_t) desired[j]])
        {
            engage[(size_t) desired[j]].snap (0.0f);
            engage[(size_t) desired[j]].setTarget (1.0f);
            merged[(size_t) n++] = desired[j++];
        }
        else if (i < chainSize && j < count)
        {
            engage[(size_t) desired[j]].setTarget (1.0f);
            merged[(size_t) n++] = desired[j++];
            ++i;
        }
        else
        {
            break;   // unreachable for a compatible pair
        }
    }
    chain = merged;
    chainSize = n;
}

void FxRack::computeTail() noexcept
{
    float t = 0.0f;
    for (int i = 0; i < chainSize; ++i)
    {
        const auto type = chain[(size_t) i];
        if (engage[(size_t) type].targetValue() <= 0.0f)
            continue;
        switch (type)
        {
            case FxType::Delay:  t = std::max (t, delay.tailSeconds()); break;
            case FxType::Reverb: t = std::max (t, reverb.tailSeconds()); break;
            case FxType::None:
            case FxType::Count:  break;
            case FxType::Distortion:
            case FxType::Chorus:
            case FxType::Phaser:
            case FxType::Flanger:
            case FxType::Eq:
            case FxType::Compressor:
            default:             t = std::max (t, 0.1f); break;
        }
    }
    tail.store (std::min (20.0f, t), std::memory_order_relaxed);
}

float FxRack::tailSeconds() const
{
    return tail.load (std::memory_order_relaxed);
}

void FxRack::processUnit (FxType t, float* L, float* R, int n) noexcept
{
    switch (t)
    {
        case FxType::Distortion: distortion.process (L, R, n); break;
        case FxType::Chorus:     chorus.process (L, R, n); break;
        case FxType::Phaser:     phaser.process (L, R, n); break;
        case FxType::Flanger:    flanger.process (L, R, n); break;
        case FxType::Delay:      delay.process (L, R, n); break;
        case FxType::Reverb:     reverb.process (L, R, n); break;
        case FxType::Eq:         eq.process (L, R, n); break;
        case FxType::Compressor: compressor.process (L, R, n); break;
        case FxType::None:
        case FxType::Count:
        default:                 break;
    }
}

void FxRack::process (float* L, float* R, int numSamples) noexcept
{
    if (L == nullptr || R == nullptr)
        return;
    for (int pos = 0; pos < numSamples; pos += maxBlockSize)
        processChunk (L + pos, R + pos, std::min (maxBlockSize, numSamples - pos));
}

void FxRack::adoptPendingIfReady() noexcept
{
    // A reorder waits until every running unit has faded out (the chain is then an
    // exact bypass), so swapping the order cannot click.
    if (! hasPending || chainSize != 0)
        return;
    for (int i = 0; i < pendingSize; ++i)
    {
        chain[(size_t) i] = pending[(size_t) i];
        engage[(size_t) pending[(size_t) i]].snap (0.0f);
        engage[(size_t) pending[(size_t) i]].setTarget (1.0f);
    }
    chainSize = pendingSize;
    hasPending = false;
}

void FxRack::processChunk (float* L, float* R, int n) noexcept
{
    adoptPendingIfReady();
    if (n <= 0 || chainSize == 0)
        return;

    for (int c = 0; c < chainSize; ++c)
    {
        const auto t = chain[(size_t) c];
        auto& e = engage[(size_t) t];

        if (e.isSettledAt (0.0f))
            continue;

        if (e.isSettledAt (1.0f))
        {
            processUnit (t, L, R, n);
            continue;
        }

        // Fading in or out: crossfade from the unit's own input (an exact bypass at 0).
        std::copy (L, L + n, dryL.begin());
        std::copy (R, R + n, dryR.begin());
        processUnit (t, L, R, n);
        for (int i = 0; i < n; ++i)
        {
            const float g = e.next();
            L[i] = dryL[(size_t) i] + g * (L[i] - dryL[(size_t) i]);
            R[i] = dryR[(size_t) i] + g * (R[i] - dryR[(size_t) i]);
        }
    }

    // Drop units that finished fading out.
    int w = 0;
    for (int c = 0; c < chainSize; ++c)
    {
        const auto t = chain[(size_t) c];
        if (! engage[(size_t) t].isSettledAt (0.0f))
            chain[(size_t) w++] = t;
    }
    chainSize = w;
    adoptPendingIfReady();
}

} // namespace verdant
