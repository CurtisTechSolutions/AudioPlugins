#include "modules/Chills.h"

#include <ap/Math.h>

#include <algorithm>
#include <cmath>

namespace frisson
{

namespace
{
    // Analysis grid: one decision per 5 ms regardless of host block size.
    constexpr double kHopSeconds = 0.005;
    constexpr int    kMinHop     = 16;

    // R2 loudness envelopes.
    constexpr double kFastSeconds = 0.4;
    constexpr double kSlowSeconds = 3.0;

    // R2 novelty bands (Q 1) and their peak envelopes.
    constexpr float kBandHz[ChillsModulator::kNumBands] = { 100.0f, 300.0f, 1000.0f, 3000.0f, 8000.0f, 14000.0f };
    constexpr float kBandQ          = 1.0f;
    constexpr float kBandAttackMs   = 0.0f;      // instant attack: a peak envelope
    constexpr float kBandReleaseMs  = 50.0f;
    constexpr float kBandFloorBelow = 60.0f;     // band floor relative to L_fast, dB

    // Novelty baseline: max of each band from kNoveltyLag to kNoveltyLag + kNoveltySpan ago.
    constexpr double kSegmentSeconds     = 0.05;
    constexpr double kNoveltyLagSeconds  = 0.3;
    constexpr double kNoveltySpanSeconds = 7.7;   // 8 s of memory: a 4-bar loop at 120 bpm is familiar after one pass

    // Crescendo "rising over the last 500 ms": strictly increasing at these checkpoints.
    constexpr double kRiseSeconds     = 0.5;
    constexpr int    kRiseCheckpoints = 4;              // intervals: 125 ms each
    constexpr float  kRiseFraction    = 1.0f / 6.0f;    // total rise ≥ thr / 6 (0.5 dB at default)

    // Silence handling (R7).
    constexpr double kSilenceMeanSquare = 1.0e-10;      // −100 dBFS over a hop
    constexpr double kSilenceSeconds    = 2.0;          // this long under it = forget the context (a 1-bar break keeps it)
    constexpr double kWarmupSeconds     = 1.0;          // listen-only time when sound (re)starts
    constexpr double kPowerFloor        = 1.0e-30;

    // Envelope snaps to exactly 0 below this, so the output is exact identity again.
    constexpr float kEnvelopeOff = 1.0e-3f;

    // Amount de-zipper: linear, full travel in this time (lands exactly on the target).
    constexpr double kAmountRampSeconds = 0.1;

    inline float powerToDb (double p) noexcept
    {
        return (float) (10.0 * std::log10 (std::max (p, kPowerFloor)));
    }

    inline int roundedCount (double seconds, double unitSeconds) noexcept
    {
        return std::max (1, (int) std::lround (seconds / unitSeconds));
    }
}

//==============================================================================
void ChillsModulator::prepare (double sampleRate, int /*maxBlockSize*/)
{
    sr = (sampleRate > 0.0 && std::isfinite (sampleRate)) ? sampleRate : 48000.0;
    configure();

    levelRing.assign ((size_t) (riseStepHops * riseSteps + 2), 0.0f);
    segRing.assign ((size_t) (lagSegs + spanSegs + 1), BandValues {});

    resetState();
}

void ChillsModulator::configure() noexcept
{
    hopSize    = std::max (kMinHop, (int) std::lround (sr * kHopSeconds));
    hopSeconds = (double) hopSize / sr;

    fastWeight = 1.0 - std::exp (-hopSeconds / kFastSeconds);
    slowWeight = 1.0 - std::exp (-hopSeconds / kSlowSeconds);

    for (int b = 0; b < kNumBands; ++b)
    {
        bands[(size_t) b].configure (ap::Biquad::Type::BandPass, sr, kBandHz[b], kBandQ);
        auto& e = bandEnv[(size_t) b];
        e.prepare (sr);
        e.setMode (ap::EnvelopeFollower::Mode::Peak);
        e.setTimes (kBandAttackMs, kBandReleaseMs);
    }

    riseSteps    = kRiseCheckpoints;
    riseStepHops = roundedCount (kRiseSeconds / (double) kRiseCheckpoints, hopSeconds);

    segHops  = roundedCount (kSegmentSeconds, hopSeconds);
    const double segSeconds = (double) segHops * hopSeconds;
    lagSegs  = roundedCount (kNoveltyLagSeconds, segSeconds);
    spanSegs = roundedCount (kNoveltySpanSeconds, segSeconds);

    silenceResetHops = roundedCount (kSilenceSeconds, hopSeconds);
    warmupHops       = roundedCount (kWarmupSeconds, hopSeconds);

    attackStep   = (float) (hopSeconds / kAttackSeconds);
    releaseCoeff = (float) std::exp (-hopSeconds / (kReleaseSeconds / 3.0));
}

void ChillsModulator::reset()
{
    resetState();
}

void ChillsModulator::resetState() noexcept
{
    for (auto& b : bands) b.reset();
    for (auto& e : bandEnv) e.reset();

    hopFill = 0;
    hopSumSq = 0.0;
    hopBandPeak.fill (0.0f);

    restartContext();
    inSilence = true;      // a cold start is treated like sound returning after silence
    silentHops = 0;

    forceIdle();
    refractoryLeft = 0.0;
    triggers = 0;

    snapAmount = true;
    lastDeltaDb = lastFluxDb = 0.0f;
    activityValue.store (0.0f, std::memory_order_relaxed);
}

void ChillsModulator::restartContext() noexcept
{
    fastPow = slowPow = 0.0;
    levelHops = 0;

    std::fill (levelRing.begin(), levelRing.end(), 0.0f);
    levelPos = levelValid = 0;

    std::fill (segRing.begin(), segRing.end(), BandValues {});
    segPos = segValid = 0;
    segFill = 0;
    segMax.fill (-1.0e30f);
    baselineValid = false;

    warmupLeft = warmupHops;
}

void ChillsModulator::forceIdle() noexcept
{
    stage = Stage::Idle;
    env = 0.0f;
    holdLeft = 0.0;
}

//==============================================================================
void ChillsModulator::setAmount (float amount)
{
    if (std::isfinite (amount))
        amountTarget.store (util::clamp01 (amount), std::memory_order_relaxed);
}

float ChillsModulator::getAmount() const
{
    return amountTarget.load (std::memory_order_relaxed);
}

void ChillsModulator::setSensitivity (float s)
{
    if (std::isfinite (s))
        sensitivity.store (util::clamp01 (s), std::memory_order_relaxed);
}

float ChillsModulator::getSensitivity() const
{
    return sensitivity.load (std::memory_order_relaxed);
}

float ChillsModulator::activity() const
{
    return activityValue.load (std::memory_order_relaxed);
}

//==============================================================================
bool ChillsModulator::risingFor (float lFast, float minRiseDb) const noexcept
{
    // levelRing holds previous hops' L_fast; index 0 back = the previous hop.
    const int needed = riseStepHops * riseSteps;
    if (levelValid < needed)
        return false;

    const int size = (int) levelRing.size();
    auto ago = [&] (int hops) noexcept
    {
        int idx = (levelPos - hops) % size;
        if (idx < 0) idx += size;
        return levelRing[(size_t) idx];
    };

    float newer = lFast;
    for (int k = 1; k <= riseSteps; ++k)
    {
        const float older = ago (k * riseStepHops);
        if (! (newer > older))
            return false;
        newer = older;
    }
    return lFast - newer >= minRiseDb;
}

void ChillsModulator::pushSegment() noexcept
{
    segRing[(size_t) segPos] = segMax;
    segPos = (segPos + 1) % (int) segRing.size();
    segValid = std::min (segValid + 1, (int) segRing.size());
    segMax.fill (-1.0e30f);

    // The baseline only changes when a segment completes: max over the segments
    // that ended at least lagSegs segments ago, back to lagSegs + spanSegs.
    const int size = (int) segRing.size();
    const int first = lagSegs;                                  // 0 = the segment just stored
    const int last  = std::min (lagSegs + spanSegs, segValid) - 1;
    baselineValid = last >= first;
    if (! baselineValid)
        return;

    baseline.fill (-1.0e30f);
    for (int k = first; k <= last; ++k)
    {
        int idx = (segPos - 1 - k) % size;
        if (idx < 0) idx += size;
        const BandValues& s = segRing[(size_t) idx];
        for (int b = 0; b < kNumBands; ++b)
            baseline[(size_t) b] = std::max (baseline[(size_t) b], s[(size_t) b]);
    }
}

void ChillsModulator::startTrigger() noexcept
{
    stage = Stage::Attack;              // attack from wherever e is
    refractoryLeft = kRefractorySeconds;
    ++triggers;
}

void ChillsModulator::endHop() noexcept
{
    const double meanSquare = hopSumSq / (double) hopSize;

    // R7: silence is never analysed. Short gaps keep the context (a breath
    // before the drop); a full second of silence forgets it.
    if (meanSquare < kSilenceMeanSquare)
    {
        if (! inSilence && ++silentHops >= silenceResetHops)
            inSilence = true;
        lastDeltaDb = lastFluxDb = 0.0f;
        advanceEnvelope();
        return;
    }

    silentHops = 0;
    if (inSilence)
    {
        inSilence = false;
        restartContext();
    }

    // R2 loudness. Until a time constant has been heard the envelopes are
    // running means, so the first seconds of a song do not read as a crescendo.
    ++levelHops;
    const double wf = std::max (fastWeight, 1.0 / (double) levelHops);
    const double ws = std::max (slowWeight, 1.0 / (double) levelHops);
    fastPow += wf * (meanSquare - fastPow);
    slowPow += ws * (meanSquare - slowPow);

    const float lFast = powerToDb (fastPow);
    const float lSlow = powerToDb (slowPow);
    const float delta = lFast - lSlow;

    // R2 novelty against the baseline (see header for why it is a max).
    const float bandFloor = lFast - kBandFloorBelow;
    BandValues now;
    float flux = 0.0f;
    for (int b = 0; b < kNumBands; ++b)
    {
        now[(size_t) b] = std::max (bandFloor, ap::gainToDb (hopBandPeak[(size_t) b], -300.0f));
        if (baselineValid)
            flux += std::max (0.0f, now[(size_t) b] - baseline[(size_t) b]);
    }

    // R3.
    const float thrScale = 1.5f - sensitivity.load (std::memory_order_relaxed);
    const float crescThr = kCrescendoBaseDb * thrScale;
    const float novThr   = kNoveltyBaseDb * thrScale;

    const bool crescendo = delta > crescThr && risingFor (lFast, crescThr * kRiseFraction);
    const bool novelty   = baselineValid && flux > novThr;

    if (warmupLeft > 0)
        --warmupLeft;
    else if ((crescendo || novelty) && refractoryLeft <= 0.0)
        startTrigger();

    // History.
    levelRing[(size_t) levelPos] = lFast;
    levelPos = (levelPos + 1) % (int) levelRing.size();
    levelValid = std::min (levelValid + 1, (int) levelRing.size());

    for (int b = 0; b < kNumBands; ++b)
        segMax[(size_t) b] = std::max (segMax[(size_t) b], now[(size_t) b]);
    if (++segFill >= segHops)
    {
        segFill = 0;
        pushSegment();
    }

    lastDeltaDb = delta;
    lastFluxDb  = flux;

    advanceEnvelope();
}

void ChillsModulator::advanceEnvelope() noexcept
{
    if (refractoryLeft > 0.0)
        refractoryLeft -= hopSeconds;

    switch (stage)
    {
        case Stage::Attack:
            env += attackStep;
            if (env >= 1.0f)
            {
                env = 1.0f;
                stage = Stage::Hold;
                holdLeft = kHoldSeconds;
            }
            break;

        case Stage::Hold:
            holdLeft -= hopSeconds;
            if (holdLeft <= 0.0)
                stage = Stage::Release;
            break;

        case Stage::Release:
            env *= releaseCoeff;
            if (env < kEnvelopeOff)
                forceIdle();
            break;

        case Stage::Idle:
        default:
            break;
    }
}

//==============================================================================
Modulation ChillsModulator::analyze (const float* left, const float* right, int numSamples) noexcept
{
    if (levelRing.empty())
        return Modulation::identity();   // not prepared

    if (left != nullptr && numSamples > 0)
    {
        if (right == nullptr) right = left;

        for (int i = 0; i < numSamples; ++i)
        {
            float m = 0.5f * (left[i] + right[i]);
            if (! (std::fabs (m) < 1.0e6f)) m = 0.0f;   // NaN / inf from upstream must not poison the filters

            hopSumSq += (double) m * (double) m;
            for (int b = 0; b < kNumBands; ++b)
            {
                const float e = bandEnv[(size_t) b].process (bands[(size_t) b].process (m));
                hopBandPeak[(size_t) b] = std::max (hopBandPeak[(size_t) b], e);
            }

            if (++hopFill >= hopSize)
            {
                endHop();
                hopFill = 0;
                hopSumSq = 0.0;
                hopBandPeak.fill (0.0f);
            }
        }
    }

    // Amount: linear ramp at block rate, snapped on the first block after reset.
    const float target = amountTarget.load (std::memory_order_relaxed);
    if (snapAmount)
    {
        amountCurrent = target;
        snapAmount = false;
    }
    else if (numSamples > 0)
    {
        const float step = (float) std::min (1.0, (double) numSamples / (kAmountRampSeconds * sr));
        amountCurrent += ap::clamp (target - amountCurrent, -step, step);
    }

    // R1: knob off → exact identity and no activity.
    if (util::isOff (amountCurrent) && util::isOff (target))
    {
        amountCurrent = target;
        activityValue.store (0.0f, std::memory_order_relaxed);
        return Modulation::identity();
    }

    // R5.
    const float depth = amountCurrent * env;
    Modulation m;
    m.airDb      = kAirDb * depth;
    m.widthMul   = 1.0f + kWidthDepth * depth;
    m.densityMul = 1.0f + kDensityDepth * depth;

    activityValue.store (env, std::memory_order_relaxed);   // R8
    return m;
}

} // namespace frisson
