#include "modules/Warmth.h"

#include <ap/Math.h>

#include <cmath>

namespace frisson
{

namespace
{
    constexpr float  kLowSplitHz     = 80.0f;
    constexpr float  kHighSplitHz    = 800.0f;
    constexpr float  kEqHz           = 200.0f;
    constexpr float  kEqQ            = 0.9f;
    constexpr float  kEqMaxDb        = 1.5f;
    constexpr float  kTubeBias       = 0.5f;
    constexpr float  kAlignDb        = -18.0f;      // band RMS the shaper sees (0 VU alignment)
    constexpr float  kAlignMinDb     = -24.0f;
    constexpr float  kAlignMaxDb     = 24.0f;
    constexpr float  kCorrLimit      = 1.4125375f;  // ±3 dB
    constexpr float  kMakeupMaxDb    = 12.0f;
    constexpr float  kThreshBelowDb  = 3.0f;        // threshold = long-term band RMS − 3 dB
    constexpr float  kKneeDb         = 6.0f;
    constexpr float  kRefRatio       = 2.0f;        // reference slope 0.5; actual GR = k · reference
    constexpr double kGateRel        = 0.01;        // −20 dB re the long-term band power
    constexpr double kGateAbs        = 1.0e-10;     // −100 dBFS
    constexpr float  kActivityFullDb = 6.0f;

    inline double powerToDb (double p) noexcept { return 10.0 * std::log10 (p > 1.0e-30 ? p : 1.0e-30); }
}

void WarmthModule::PowerTracker::setTime (double updateRateHz, double seconds) noexcept
{
    span = std::max (1.0, updateRateHz * seconds);
}

void WarmthModule::prepare (double sampleRate, int maxBlockSize)
{
    (void) maxBlockSize;   // everything runs per sample; nothing is block-sized

    sr = sampleRate;
    controlRate = sampleRate / (double) kControlInterval;

    for (auto& s : splitter) s.prepare (sr, kLowSplitHz, kHighSplitHz);
    for (auto& d : dcBlock)  d.prepare (sr);

    amountSm.prepare (sr, 50.0f);
    driveMulSm.prepare (sr, 50.0f);
    preGainSm.prepare (sr, 100.0f);
    corrSm.prepare (sr, 20.0f);
    grSm.prepare (sr, 1.0f);
    makeupSm.prepare (sr, 50.0f);
    eqFreqMulSm.prepare (controlRate, 50.0f);

    detector.prepare (sr);
    detector.setMode (ap::EnvelopeFollower::Mode::Rms);
    detector.setTimes (30.0f, 150.0f);

    shortTerm.setTime (controlRate, 0.05);
    bandLevel.setTime (controlRate, 3.0);
    corrIn.setTime (controlRate, 0.3);
    corrOut.setTime (controlRate, 0.3);
    gluePow.setTime (controlRate, 3.0);
    for (auto& t : glueOutPow) t.setTime (controlRate, 3.0);

    activityAttack  = ap::onePoleCoeff (10.0f, controlRate);
    activityRelease = ap::onePoleCoeff (150.0f, controlRate);
    wetStep = (float) (1.0 / (0.030 * sr));

    reset();
}

void WarmthModule::reset()
{
    resetDsp();
    amountSm.snap (amountTarget);
    driveMulSm.snap (driveMulTarget);
    eqFreqMulSm.snap (eqFreqMulTarget);
    active = ! util::isOff (amountTarget);
    wet = active ? 1.0f : 0.0f;
    updateEq (true);
}

void WarmthModule::resetDsp() noexcept
{
    for (auto& s : splitter) s.reset();
    for (auto& d : dcBlock)  d.reset();
    for (auto& f : fullness) f.reset();
    detector.reset();

    shortTerm.reset();
    bandLevel.reset();
    corrIn.reset();
    corrOut.reset();
    gluePow.reset();
    for (auto& t : glueOutPow) t.reset();
    grRefDb = 0.0f;

    preGainSm.snap (1.0f);
    corrSm.snap (1.0f);
    grSm.snap (1.0f);
    makeupSm.snap (1.0f);

    accMid = accSat = accEq = 0.0;
    accCount = 0;
    controlCountdown = 0;
    activityState = 0.0f;
    activityValue.store (0.0f, std::memory_order_relaxed);
}

void WarmthModule::activate() noexcept
{
    resetDsp();
    amountSm.snap (amountTarget);
    driveMulSm.snap (driveMulTarget);
    eqFreqMulSm.snap (eqFreqMulTarget);
    updateEq (true);
    wet = 0.0f;
    active = true;
}

void WarmthModule::setAmount (float amount)
{
    amountTarget = std::isfinite (amount) ? util::clamp01 (amount) : 0.0f;
    amountSm.setTarget (amountTarget);
}

void WarmthModule::setModulation (const Modulation& m)
{
    driveMulTarget  = std::isfinite (m.driveMul)  ? ap::clamp (m.driveMul, 0.5f, 2.0f)   : 1.0f;
    eqFreqMulTarget = std::isfinite (m.eqFreqMul) ? ap::clamp (m.eqFreqMul, 0.8f, 1.25f) : 1.0f;
    driveMulSm.setTarget (driveMulTarget);
    eqFreqMulSm.setTarget (eqFreqMulTarget);
}

void WarmthModule::updateEq (bool force) noexcept
{
    const float freq = kEqHz * eqFreqMulSm.getCurrent();
    const float gain = kEqMaxDb * amountSm.getCurrent();
    if (! force && std::fabs (freq - eqFreqApplied) < 0.01f && std::fabs (gain - eqGainApplied) < 0.002f)
        return;

    for (auto& f : fullness)
        f.configure (ap::Biquad::Type::Peak, sr, freq, kEqQ, gain);
    eqFreqApplied = freq;
    eqGainApplied = gain;
}

void WarmthModule::updateControl() noexcept
{
    // 1. Fold the last control period's stereo-linked band powers into the trackers.
    if (accCount > 0)
    {
        const double inv = 1.0 / (double) accCount;
        const double mMid = accMid * inv, mSat = accSat * inv, mEq = accEq * inv;
        accMid = accSat = accEq = 0.0;
        accCount = 0;

        if (! std::isfinite (mMid + mSat + mEq))
        {
            // Non-finite input: start the analysis (and the filters) over.
            const float keepWet = wet;
            resetDsp();
            wet = keepWet;
            controlCountdown = kControlInterval;
            return;
        }

        shortTerm.update (mMid);
        const bool audible  = shortTerm.value > kGateAbs;
        const bool gateOpen = audible && (! bandLevel.hasData() || shortTerm.value > kGateRel * bandLevel.value);

        if (audible)
        {
            corrIn.update (mMid);
            corrOut.update (mSat);
        }

        // Output power the glue would have produced over this period at k = (j+1)/4
        // (its gain reduction is exactly k times the reference reduction).
        if (gateOpen)
        {
            bandLevel.update (mMid);
            gluePow.update (mEq);
            for (int j = 0; j < kMakeupGrid; ++j)
                glueOutPow[(size_t) j].update (mEq * std::pow (10.0, 0.1 * (double) grRefDb * (double) (j + 1) / kMakeupGrid));
        }
        else if (audible)
        {
            bandLevel.leak (mMid);
            gluePow.leak (mEq);
            for (int j = 0; j < kMakeupGrid; ++j)
                glueOutPow[(size_t) j].leak (mEq * std::pow (10.0, 0.1 * (double) grRefDb * (double) (j + 1) / kMakeupGrid));
        }
    }

    const float amt = amountSm.getCurrent();

    // 2. Shaper alignment: drive the shaper as if the band's long-term RMS were kAlignDb.
    if (bandLevel.hasData() && bandLevel.value > kGateAbs)
    {
        const float alignDb = ap::clamp (kAlignDb - (float) powerToDb (bandLevel.value), kAlignMinDb, kAlignMaxDb);
        preGainSm.setTarget (ap::dbToGain (alignDb));
    }

    // 3. R6: feed-forward level correction of the saturated band (±3 dB).
    if (corrIn.hasData() && corrOut.value > 1.0e-20)
        corrSm.setTarget (ap::clamp ((float) std::sqrt (corrIn.value / corrOut.value), 1.0f / kCorrLimit, kCorrLimit));

    // 4. R5: glue gain reduction and its automatic makeup.
    const float k = 2.0f * amt / (1.0f + amt);   // slope(1 + amt) / slope(2)
    float grDb = 0.0f, makeupDb = 0.0f, steadyGrDb = 0.0f;
    grRefDb = 0.0f;
    if (gluePow.hasData() && gluePow.value > kGateAbs)
    {
        ap::GainComputer gc;
        gc.thresholdDb = (float) powerToDb (gluePow.value) - kThreshBelowDb;
        gc.ratio       = kRefRatio;
        gc.kneeDb      = kKneeDb;
        grRefDb    = gc.gainReductionDb (ap::gainToDb (detector.current(), -200.0f));
        grDb       = k * grRefDb;
        steadyGrDb = k * gc.gainReductionDb (gc.thresholdDb + kThreshBelowDb);   // a signal at its long-term level

        // Makeup = long-term in/out power ratio at the current k, interpolated
        // between the tracked grid points (k = 0 needs none).
        const float pos = k * (float) kMakeupGrid;
        const int   j   = std::min (kMakeupGrid - 1, (int) pos);
        float lower = 0.0f, upper = 0.0f;
        if (j > 0 && glueOutPow[(size_t) (j - 1)].value > 1.0e-30)
            lower = (float) (powerToDb (gluePow.value) - powerToDb (glueOutPow[(size_t) (j - 1)].value));
        if (glueOutPow[(size_t) j].value > 1.0e-30)
            upper = (float) (powerToDb (gluePow.value) - powerToDb (glueOutPow[(size_t) j].value));
        makeupDb = ap::clamp (lower + (upper - lower) * (pos - (float) j), 0.0f, kMakeupMaxDb);
    }
    grSm.setTarget (ap::dbToGain (grDb));
    makeupSm.setTarget (ap::dbToGain (makeupDb));

    // 5. R8: activity = glue gain reduction beyond the constant reduction a steady
    //    signal at its long-term level receives (and the makeup cancels).
    const float act = ap::clamp ((steadyGrDb - grDb) / kActivityFullDb, 0.0f, 1.0f);
    activityState = act + (act > activityState ? activityAttack : activityRelease) * (activityState - act);
    activityValue.store (activityState * wet, std::memory_order_relaxed);

    // 6. R4: fullness EQ follows the amount and the modulated centre frequency.
    eqFreqMulSm.next();
    updateEq (false);
}

void WarmthModule::process (float* left, float* right, int numSamples)
{
    if (left == nullptr || numSamples <= 0)
        return;

    if (! active)
    {
        if (util::isOff (amountTarget))
            return;                       // exact bypass
        activate();
    }

    const bool  hasRight  = right != nullptr;
    const float wetTarget = util::isOff (amountTarget) ? 0.0f : 1.0f;

    ap::TubeShaper shaper;
    shaper.bias = kTubeBias;

    for (int i = 0; i < numSamples; ++i)
    {
        if (--controlCountdown <= 0)
        {
            updateControl();
            controlCountdown = kControlInterval;
        }

        const float amt    = amountSm.next();
        const float drvMul = driveMulSm.next();
        const float pre    = preGainSm.next();
        const float corr   = corrSm.next();
        const float gr     = grSm.next();
        const float makeup = makeupSm.next();

        shaper.drive = 1.0f + 2.0f * amt * drvMul;
        const float mix      = std::sqrt (amt);      // harmonics fade in continuously from 0
        const float invPre   = 1.0f / pre;
        const float bandGain = gr * makeup;

        const float in[2] = { left[i], hasRight ? right[i] : left[i] };
        float lo[2], mid[2], hi[2], sat[2], eq[2];

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto c = (size_t) ch;
            splitter[c].process (in[ch], lo[ch], mid[ch], hi[ch]);
            const float residual = shaper.process (mid[ch] * pre) * invPre - mid[ch];
            sat[ch] = mid[ch] + mix * dcBlock[c].process (residual);
            eq[ch]  = fullness[c].process (sat[ch] * corr);
        }

        const float linked = 0.5f * (eq[0] * eq[0] + eq[1] * eq[1]);
        detector.process (std::sqrt (linked));

        accMid += 0.5 * ((double) mid[0] * mid[0] + (double) mid[1] * mid[1]);
        accSat += 0.5 * ((double) sat[0] * sat[0] + (double) sat[1] * sat[1]);
        accEq  += (double) linked;
        ++accCount;

        if (wet != wetTarget)
            wet = wetTarget > wet ? std::min (wetTarget, wet + wetStep) : std::max (wetTarget, wet - wetStep);

        float out[2];
        for (int ch = 0; ch < 2; ++ch)
        {
            out[ch] = lo[ch] + eq[ch] * bandGain + hi[ch];
            if (wet < 1.0f)
                out[ch] = in[ch] + wet * (out[ch] - in[ch]);
        }

        left[i] = out[0];
        if (hasRight)
            right[i] = out[1];
    }

    if (wet <= 0.0f && wetTarget <= 0.0f)
    {
        active = false;
        activityState = 0.0f;
        activityValue.store (0.0f, std::memory_order_relaxed);
    }
}

} // namespace frisson
