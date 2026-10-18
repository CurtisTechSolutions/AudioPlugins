#include "modules/Rush.h"

#include <ap/Math.h>

#include <algorithm>
#include <cmath>

namespace frisson
{

namespace
{
    constexpr float  kLookaheadMs     = 1.5f;
    constexpr float  kLowSplitHz      = 120.0f;
    constexpr float  kHighSplitHz     = 2500.0f;
    constexpr float  kDetectWindowMs  = 10.0f;    // RMS averaging window
    constexpr float  kDetectAttackMs  = 10.0f;    // ballistics on the dB level
    constexpr float  kDetectReleaseMs = 120.0f;
    constexpr double kLongTermSec     = 3.0;
    constexpr float  kThresholdBelow  = 6.0f;     // dB under the band's long-term level
    constexpr float  kRatio           = 4.0f;
    constexpr float  kKneeDb          = 6.0f;
    constexpr float  kMaxMakeup       = 3.98107f; // +12 dB
    constexpr float  kMinTrim         = 0.25119f; // -12 dB
    constexpr float  kReleaseMs       = 60.0f;
    constexpr float  kEngageMs        = 20.0f;
    constexpr float  kParamSmoothMs   = 20.0f;
    constexpr float  kAutoGainFadeMs  = 50.0f;    // autoGain on/off crossfade
    constexpr double kGateRatio       = 1.0e-6;   // ignore steps 60 dB under the running level
    constexpr double kSilencePow      = 1.0e-20;  // -200 dB: digital silence

    /** True when a control step's power should update a gated long-term tracker. */
    inline bool gatedAccept (double stepPow, double trackedPow, double norm) noexcept
    {
        if (stepPow <= kSilencePow)
            return false;
        const double estimate = norm > 0.0 ? trackedPow / norm : 0.0;
        return stepPow > estimate * kGateRatio;
    }
}

void RushModule::prepare (double sampleRate, int /*maxBlockSize*/)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    latency = std::max (1, (int) std::lround (0.001 * (double) kLookaheadMs * sr));
    window = latency + 1;
    engageStep = 1.0f / std::max (1.0f, (float) (0.001 * (double) kEngageMs * sr));
    ltCoeff = std::exp (-(double) kControlInterval / (kLongTermSec * sr));
    autoGainCoeff = (float) std::exp (-(double) kControlInterval / (0.001 * (double) kAutoGainFadeMs * sr));
    limiterRelease = ap::onePoleCoeff (kReleaseMs, sr);
    rmsCoeff = ap::onePoleCoeff (kDetectWindowMs, sr);
    const double controlRate = sr / (double) kControlInterval;
    attackCoeff = ap::onePoleCoeff (kDetectAttackMs, controlRate);
    releaseCoeff = ap::onePoleCoeff (kDetectReleaseMs, controlRate);

    for (auto& s : splitters)
        s.prepare (sr, kLowSplitHz, kHighSplitHz);

    gainComputer.ratio = kRatio;
    gainComputer.kneeDb = kKneeDb;

    for (auto& d : delays)
        d.prepare (window + 1);
    engageDelay.prepare (window + 1);

    dqCapacity = window + 1;
    dqValue.assign ((size_t) dqCapacity, 1.0f);
    dqIndex.assign ((size_t) dqCapacity, 0);
    boxRing.assign ((size_t) window, 1.0f);

    mixSmoother.prepare (sr, kParamSmoothMs);
    ceilingSmoother.prepare (sr, kParamSmoothMs);

    reset();
}

void RushModule::reset()
{
    for (auto& s : splitters)
        s.reset();
    for (auto& d : delays)
        d.reset();
    engageDelay.reset();

    for (auto& b : bands)
    {
        b.meanSquare = 0.0f;
        b.levelDb = -200.0f;
        b.bandPow = b.compPow = b.norm = 0.0;
        b.sumBand = b.sumComp = 0.0f;
        b.g = 1.0f;
        b.gStep = 0.0f;
        b.makeup = 1.0f;
        b.makeupStep = 0.0f;
        b.grDb = 0.0f;
    }

    inPow = dryPow = crossPow = wetPow = ioNorm = 0.0;
    sumIn = sumDry = sumCross = sumWet = 0.0f;
    trim = 1.0f;
    trimStep = 0.0f;
    autoGainBlend = autoGainOn ? 1.0f : 0.0f;
    controlCounter = 0;

    std::fill (dqValue.begin(), dqValue.end(), 1.0f);
    std::fill (dqIndex.begin(), dqIndex.end(), (int64_t) 0);
    dqHead = dqSize = 0;
    std::fill (boxRing.begin(), boxRing.end(), 1.0f);
    boxPos = 0;
    boxSum = (double) window;
    released = 1.0f;
    sampleIndex = 0;

    engage = 0.0f;
    needsSnap = true;
    activityValue.store (0.0f, std::memory_order_relaxed);
}

float RushModule::mixTarget() const noexcept
{
    if (util::isOff (amount))
        return 0.0f;
    return ap::clamp (amount * densityMul, 0.0f, 1.5f);
}

void RushModule::setAmount (float newAmount)
{
    amount = std::isfinite (newAmount) ? util::clamp01 (newAmount) : 0.0f;
    mixSmoother.setTarget (mixTarget());
}

void RushModule::setModulation (const Modulation& m)
{
    densityMul = std::isfinite (m.densityMul) ? ap::clamp (m.densityMul, 0.5f, 2.0f) : 1.0f;
    mixSmoother.setTarget (mixTarget());
}

void RushModule::setCeilingDb (float db) noexcept
{
    ceilingDb = std::isfinite (db) ? ap::clamp (db, -3.0f, 0.0f) : -0.3f;
    ceilingSmoother.setTarget (ap::dbToGain (ceilingDb));
}

float RushModule::slidingMin (float value) noexcept
{
    // Monotonic deque: values increase from front to back, the front is the window minimum.
    while (dqSize > 0)
    {
        const int back = (dqHead + dqSize - 1) % dqCapacity;
        if (dqValue[(size_t) back] >= value)
            --dqSize;
        else
            break;
    }

    const int pos = (dqHead + dqSize) % dqCapacity;
    dqValue[(size_t) pos] = value;
    dqIndex[(size_t) pos] = sampleIndex;
    ++dqSize;

    while (dqIndex[(size_t) dqHead] <= sampleIndex - window)
    {
        dqHead = (dqHead + 1) % dqCapacity;
        --dqSize;
    }

    ++sampleIndex;
    return dqValue[(size_t) dqHead];
}

void RushModule::controlUpdate (float mix) noexcept
{
    const float inv = 1.0f / (float) kControlInterval;

    for (auto& b : bands)
    {
        const double pb = (double) b.sumBand * inv;
        const double pc = (double) b.sumComp * inv;
        b.sumBand = b.sumComp = 0.0f;

        if (gatedAccept (pb, b.bandPow, b.norm))
        {
            b.bandPow = pb + ltCoeff * (b.bandPow - pb);
            b.compPow = pc + ltCoeff * (b.compPow - pc);
            b.norm    = 1.0 + ltCoeff * (b.norm - 1.0);
        }

        // RMS detector: 10 ms window, then 10 ms attack / 120 ms release on the dB level
        // (log-domain ballistics treat rises and falls of equal dB symmetrically).
        const float levelNow = b.meanSquare > 1.0e-20f ? 10.0f * std::log10 (b.meanSquare) : -200.0f;
        const float c = levelNow > b.levelDb ? attackCoeff : releaseCoeff;
        b.levelDb = levelNow + c * (b.levelDb - levelNow);

        float gTarget = 1.0f, makeupTarget = b.makeup;
        b.grDb = 0.0f;
        if (b.norm > 0.0 && b.bandPow > 0.0)
        {
            // Threshold relative to the band's own 3 s RMS: level independent.
            const float longTermDb = (float) (10.0 * std::log10 (b.bandPow / b.norm));
            ap::GainComputer gc = gainComputer;
            gc.thresholdDb = longTermDb - kThresholdBelow;
            const float grDb = gc.gainReductionDb (b.levelDb);
            gTarget = ap::dbToGain (grDb);
            b.grDb = -grDb;

            makeupTarget = b.compPow > 0.0
                         ? ap::clamp ((float) std::sqrt (b.bandPow / b.compPow), 1.0f, kMaxMakeup)
                         : 1.0f;
        }

        b.gStep = (gTarget - b.g) * inv;
        b.makeupStep = (makeupTarget - b.makeup) * inv;
    }

    // Auto gain. The processed signal is dry + mix · comp, so its 3 s power at any mix is
    // E[dry²] + 2·mix·E[dry·comp] + mix²·E[comp²]; tracking those three terms lets the trim
    // follow the knob at once while the signal statistics stay 3 s averages (no reaction to
    // short transients).
    const double pin = (double) sumIn * inv;
    const double pd  = (double) sumDry * inv;
    const double px  = (double) sumCross * inv;
    const double pw  = (double) sumWet * inv;
    sumIn = sumDry = sumCross = sumWet = 0.0f;
    if (gatedAccept (pin, inPow, ioNorm))
    {
        inPow    = pin + ltCoeff * (inPow - pin);
        dryPow   = pd  + ltCoeff * (dryPow - pd);
        crossPow = px  + ltCoeff * (crossPow - px);
        wetPow   = pw  + ltCoeff * (wetPow - pw);
        ioNorm   = 1.0 + ltCoeff * (ioNorm - 1.0);
    }

    float match = 1.0f;
    const double m = (double) mix;
    const double procPow = dryPow + 2.0 * m * crossPow + m * m * wetPow;
    if (inPow > 0.0 && procPow > 0.0)
        match = ap::clamp ((float) std::sqrt (inPow / procPow), kMinTrim, 1.0f);

    // Switching auto gain on/off fades over 50 ms; the match itself needs no extra smoothing
    // (3 s statistics, and `mix` is already smoothed), so knob moves stay loudness-matched.
    const float blendTarget = autoGainOn ? 1.0f : 0.0f;
    autoGainBlend = blendTarget + autoGainCoeff * (autoGainBlend - blendTarget);
    if (std::fabs (autoGainBlend - blendTarget) < 1.0e-4f)
        autoGainBlend = blendTarget;

    const float target = 1.0f + autoGainBlend * (match - 1.0f);
    trimStep = (target - trim) * inv;
}

void RushModule::process (float* left, float* right, int numSamples)
{
    if (left == nullptr || numSamples <= 0)
        return;
    if (right == nullptr)
        right = left;

    const bool off = util::isOff (amount);
    const float engageTarget = off ? 0.0f : 1.0f;

    if (needsSnap)
    {
        needsSnap = false;
        mixSmoother.snap (mixTarget());
        ceilingSmoother.snap (ap::dbToGain (ceilingDb));
        engage = engageTarget;
        for (int i = 0; i <= window; ++i)
            engageDelay.push (engage);
    }

    float blockMinGain = 1.0f;
    float mix = mixSmoother.getCurrent();

    for (int i = 0; i < numSamples; ++i)
    {
        const float xl = left[i];
        const float xr = right[i];

        // 1. Three-band parallel compression: band + mix · makeup · g · band.
        //    The analysis always runs, so engaging the knob starts from a settled state.
        float bl[kBands], br[kBands];
        splitters[0].process (xl, bl[0], bl[1], bl[2]);
        splitters[1].process (xr, br[0], br[1], br[2]);

        mix = mixSmoother.next();
        float dl = 0.0f, dr = 0.0f, cl = 0.0f, cr = 0.0f;
        for (int k = 0; k < kBands; ++k)
        {
            Band& b = bands[(size_t) k];
            const float mono = 0.5f * (bl[k] + br[k]);
            const float monoSq = mono * mono;
            b.meanSquare = monoSq + rmsCoeff * (b.meanSquare - monoSq);
            if (b.meanSquare < 1.0e-30f)
                b.meanSquare = 0.0f;   // no denormal tail after the input stops
            const float monoComp = mono * b.g;
            b.sumBand += monoSq;
            b.sumComp += monoComp * monoComp;

            const float cg = b.g * b.makeup;
            dl += bl[k];
            dr += br[k];
            cl += bl[k] * cg;
            cr += br[k] * cg;

            b.g += b.gStep;
            b.makeup += b.makeupStep;
        }

        sumIn    += xl * xl + xr * xr;
        sumDry   += dl * dl + dr * dr;
        sumCross += dl * cl + dr * cr;
        sumWet   += cl * cl + cr * cr;

        // 2. Sum, auto gain.
        const float pl = (dl + mix * cl) * trim;
        const float pr = (dr + mix * cr) * trim;
        trim += trimStep;

        // 3. Engage crossfade: only moves when the knob crosses zero. At engage 0 the
        //    signal is the input itself (no arithmetic), which makes the bypass exact.
        if (engage != engageTarget)
            engage = engageTarget > engage ? std::min (1.0f, engage + engageStep)
                                           : std::max (0.0f, engage - engageStep);

        const float yl = engage == 0.0f ? xl : (engage == 1.0f ? pl : xl + engage * (pl - xl));
        const float yr = engage == 0.0f ? xr : (engage == 1.0f ? pr : xr + engage * (pr - xr));

        // 4. Lookahead limiter. The delay lines hold the signal being limited; the newest
        //    sample is the detector's look 1.5 ms into the future.
        delays[0].push (yl);
        delays[1].push (yr);

        const float ceiling = ceilingSmoother.next();
        const float pk = std::max (std::fabs (yl), std::fabs (yr));
        const float required = pk > ceiling ? ceiling / pk : 1.0f;
        const float windowMin = slidingMin (required);

        if (windowMin <= released)
            released = windowMin;
        else if (windowMin - released < 1.0e-7f)
            released = windowMin;   // finish the release; the one-pole would stall an ulp short
        else
            released = windowMin + limiterRelease * (released - windowMin);

        boxSum += (double) released - (double) boxRing[(size_t) boxPos];
        boxRing[(size_t) boxPos] = released;
        if (++boxPos >= window)
        {
            // Re-sum once per lap so rounding in the running sum cannot drift; doing it
            // on the lap (not per block) keeps the output independent of the block size.
            boxPos = 0;
            double exact = 0.0;
            for (float v : boxRing)
                exact += (double) v;
            boxSum = exact;
        }
        const float limiterGain = std::min (1.0f, (float) (boxSum / (double) window));

        engageDelay.push (engage);
        const float engageOut = engageDelay.read (latency);
        const float outL = delays[0].read (latency);
        const float outR = delays[1].read (latency);

        if (engageOut == 0.0f)
        {
            left[i]  = outL;
            right[i] = outR;
        }
        else
        {
            const float gain = 1.0f + engageOut * (limiterGain - 1.0f);
            left[i]  = outL * gain;
            right[i] = outR * gain;
            blockMinGain = std::min (blockMinGain, gain);
        }

        if (++controlCounter >= kControlInterval)
        {
            controlCounter = 0;
            controlUpdate (mix);
        }
    }

    float bandGr = 0.0f;
    for (const auto& b : bands)
        bandGr += b.grDb;
    bandGr /= (float) kBands;
    const float limiterGr = -ap::gainToDb (blockMinGain);
    const float act = std::max (limiterGr, bandGr) / 12.0f;
    activityValue.store (util::clamp01 (act * engage), std::memory_order_relaxed);
}

} // namespace frisson
