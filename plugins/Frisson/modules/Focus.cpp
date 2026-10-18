#include "modules/Focus.h"

#include <ap/Math.h>

#include <cmath>
#include <complex>

namespace frisson
{

namespace
{
    // R2 static shape.
    constexpr float kMudHz        = 330.0f;
    constexpr float kMudQ         = 1.0f;
    constexpr float kMudDb        = -3.0f;
    constexpr float kPresenceHz   = 2200.0f;
    constexpr float kPresenceQ    = 1.2f;
    constexpr float kPresenceDb   = 2.0f;

    // R3 / R4 guards.
    constexpr float kHarshLowHz   = 3000.0f;
    constexpr float kHarshHighHz  = 4000.0f;
    constexpr float kHarshR0      = 0.30f;
    constexpr float kHarshCutHz   = 3500.0f;
    constexpr float kHarshCutQ    = 1.5f;
    constexpr float kSibLowHz     = 6000.0f;
    constexpr float kSibHighHz    = 8000.0f;
    constexpr float kSibR0        = 0.25f;
    constexpr float kSibCutHz     = 7000.0f;
    constexpr float kSibCutQ      = 2.0f;

    constexpr float kEnvAttackMs  = 2.0f;
    constexpr float kEnvReleaseMs = 80.0f;
    constexpr float kRatioEps     = 1.0e-9f;
    constexpr float kSilenceFloor = 1.0e-7f;   // -140 dBFS: below this the ratio is meaningless

    constexpr float kAmountSmoothMs = 20.0f;
    constexpr float kFreqSmoothMs   = 30.0f;
    constexpr float kDepthSmoothMs  = 10.0f;  // guard depth glide; also rides out onset spikes on broadband material

    constexpr float kFreqMulMin = 0.8f;
    constexpr float kFreqMulMax = 1.25f;

    constexpr float kAmountEpsilon  = 1.0e-5f;  // static re-design threshold
    constexpr float kFreqEpsilon    = 1.0e-5f;
    constexpr float kDepthEpsilonDb = 1.0e-3f;
}

//==============================================================================
void FocusModule::BandDetector::design (double sampleRate, float lowHz, float highHz) noexcept
{
    // 4th-order Butterworth band-pass = bilinear transform of the 2nd-order
    // Butterworth low-pass prototype under s -> (s^2 + W0^2) / (B s), with the
    // band edges prewarped. The four poles split into two conjugate pairs; each
    // pair is exactly an RBJ band-pass section (which is itself a prewarped
    // bilinear design), so we only need each pair's frequency and Q.
    const double nyq = 0.5 * sampleRate;
    const double wl = std::tan (ap::kPiD * std::min ((double) lowHz,  0.45 * sampleRate) / sampleRate);
    const double wh = std::tan (ap::kPiD * std::min ((double) highHz, 0.48 * sampleRate) / sampleRate);
    const double w0 = std::sqrt (wl * wh);
    const double bw = std::max (1.0e-6, wh - wl);

    const std::complex<double> p (-std::sqrt (0.5), std::sqrt (0.5));   // prototype pole
    const std::complex<double> pb = p * bw;
    const std::complex<double> disc = std::sqrt (pb * pb - 4.0 * w0 * w0);
    const std::complex<double> poles[2] = { 0.5 * (pb + disc), 0.5 * (pb - disc) };

    double gainAcc = bw * bw;
    ap::Biquad* sections[2] = { &s1, &s2 };

    for (int i = 0; i < 2; ++i)
    {
        const double wi = std::abs (poles[i]);
        const double qi = wi / std::max (1.0e-12, -2.0 * poles[i].real());
        const double fi = std::atan (wi) * nyq / (0.5 * ap::kPiD);
        sections[i]->configure (ap::Biquad::Type::BandPass, sampleRate, (float) fi, (float) qi);
        // RBJ band-pass numerator is (wi / qi) s; the analog BP numerator is bw s.
        gainAcc *= qi / wi;
    }

    gain = (float) gainAcc;
    reset();
}

//==============================================================================
void FocusModule::Guard::applyCut (float cutDb) noexcept
{
    // RBJ peak with fixed centre frequency: only A changes, cos(w0) and alpha are cached.
    const float A = std::pow (10.0f, -cutDb / 40.0f);
    const float b0 = 1.0f + alpha * A, b1 = -2.0f * cosW, b2 = 1.0f - alpha * A;
    const float a0 = 1.0f + alpha / A, a1 = -2.0f * cosW, a2 = 1.0f - alpha / A;
    for (auto& f : cut)
        f.setCoefficients (b0, b1, b2, a0, a1, a2);
    appliedDb = cutDb;
}

//==============================================================================
void FocusModule::prepare (double sampleRate, int /*maxBlockSize*/)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    const double controlRate = sr / (double) kControlInterval;

    amountSm.prepare (controlRate, kAmountSmoothMs);
    freqMulSm.prepare (controlRate, kFreqSmoothMs);

    wideEnv.setMode (ap::EnvelopeFollower::Mode::Peak);
    wideEnv.prepare (sr);
    wideEnv.setTimes (kEnvAttackMs, kEnvReleaseMs);

    auto setupGuard = [this, controlRate] (Guard& g, float lowHz, float highHz, float r0,
                                           float cutHz, float cutQ, bool capByAmount)
    {
        g.detector.design (sr, lowHz, highHz);
        g.env.setMode (ap::EnvelopeFollower::Mode::Peak);
        g.env.prepare (sr);
        g.env.setTimes (kEnvAttackMs, kEnvReleaseMs);
        g.depthDb.prepare (controlRate, kDepthSmoothMs);
        g.r0 = r0;
        g.cutHz = std::min (cutHz, (float) (sr * 0.45));
        g.cutQ = cutQ;
        g.capByAmount = capByAmount;
        const float w0 = ap::kTwoPi * g.cutHz / (float) sr;
        g.cosW  = std::cos (w0);
        g.alpha = std::sin (w0) / (2.0f * g.cutQ);
    };

    setupGuard (harsh, kHarshLowHz, kHarshHighHz, kHarshR0, kHarshCutHz, kHarshCutQ, false);
    setupGuard (sib,   kSibLowHz,   kSibHighHz,   kSibR0,   kSibCutHz,   kSibCutQ,   true);

    reset();
}

void FocusModule::reset()
{
    amountSm.snap (amountTarget);
    freqMulSm.snap (freqMulTarget);
    resetState();
    applyStatic (amountTarget, freqMulTarget);
    harsh.depthDb.snap (0.0f);
    sib.depthDb.snap (0.0f);
    harsh.applyCut (0.0f);
    sib.applyCut (0.0f);
    controlCountdown = 0;
    bypassed = util::isOff (amountTarget);
    activityValue.store (0.0f, std::memory_order_relaxed);
}

void FocusModule::resetState() noexcept
{
    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        mud[ch].reset();
        presence[ch].reset();
        harsh.cut[ch].reset();
        sib.cut[ch].reset();
    }
    harsh.detector.reset();
    sib.detector.reset();
    harsh.env.reset();
    sib.env.reset();
    wideEnv.reset();
}

void FocusModule::setAmount (float amount)
{
    amountTarget = util::clamp01 (std::isfinite (amount) ? amount : 0.0f);
    amountSm.setTarget (amountTarget);
}

void FocusModule::setModulation (const Modulation& m)
{
    const float mul = std::isfinite (m.eqFreqMul) ? m.eqFreqMul : 1.0f;
    freqMulTarget = ap::clamp (mul, kFreqMulMin, kFreqMulMax);
    freqMulSm.setTarget (freqMulTarget);
}

//==============================================================================
void FocusModule::applyStatic (float amount, float freqMul) noexcept
{
    const float mudHz  = kMudHz * freqMul;
    const float presHz = kPresenceHz * freqMul;
    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        mud[ch].configure (ap::Biquad::Type::Peak, sr, mudHz, kMudQ, kMudDb * amount);
        presence[ch].configure (ap::Biquad::Type::Peak, sr, presHz, kPresenceQ, kPresenceDb * amount);
    }
    appliedAmount  = amount;
    appliedFreqMul = freqMul;
}

float FocusModule::guardTargetDb (const Guard& g, float amount) const noexcept
{
    const float wide = wideEnv.current();
    if (wide < kSilenceFloor)
        return 0.0f;

    const float r = g.env.current() / (wide + kRatioEps);
    if (r <= g.r0)
        return 0.0f;

    const float depth = kMaxCutDb * amount * (r - g.r0) / g.r0;
    const float cap = g.capByAmount ? kMaxCutDb * amount : kMaxCutDb;
    return std::min (cap, depth);
}

void FocusModule::updateControl() noexcept
{
    const float amount  = amountSm.next();
    const float freqMul = freqMulSm.next();

    if (std::fabs (amount - appliedAmount) > kAmountEpsilon
        || std::fabs (freqMul - appliedFreqMul) > kFreqEpsilon)
        applyStatic (amount, freqMul);

    for (Guard* g : { &harsh, &sib })
    {
        const float target = guardTargetDb (*g, amount);
        g->depthDb.setTarget (target);
        float d = g->depthDb.next();

        // Land exactly on 0 dB when the guard releases, so the cut filter is an exact identity.
        if (target == 0.0f && d < kDepthEpsilonDb)
        {
            g->depthDb.snap (0.0f);
            d = 0.0f;
        }

        if (std::fabs (d - g->appliedDb) > kDepthEpsilonDb || (d == 0.0f && g->appliedDb != 0.0f))
            g->applyCut (d);
    }

    const float act = std::max (harsh.appliedDb, sib.appliedDb) / kMaxCutDb;
    activityValue.store (util::clamp01 (act), std::memory_order_relaxed);
}

//==============================================================================
void FocusModule::process (float* left, float* right, int numSamples)
{
    if (left == nullptr || numSamples <= 0)
        return;

    // Exact bypass once the knob has ramped all the way down.
    if (bypassed)
    {
        if (util::isOff (amountTarget))
            return;

        // Waking up: start every filter and detector from silence, ramp in from 0.
        bypassed = false;
        resetState();
        amountSm.snap (0.0f);
        freqMulSm.snap (freqMulTarget);     // inaudible at amount 0, so no glide needed
        applyStatic (0.0f, freqMulTarget);
        harsh.depthDb.snap (0.0f);
        sib.depthDb.snap (0.0f);
        harsh.applyCut (0.0f);
        sib.applyCut (0.0f);
        amountSm.setTarget (amountTarget);
        controlCountdown = 0;
    }

    const bool mono = (right == nullptr || right == left);
    float* r = mono ? left : right;

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
            const float xl = left[i];
            const float xr = r[i];
            const float m = mono ? xl : 0.5f * (xl + xr);

            wideEnv.process (m);
            harsh.env.process (harsh.detector.process (m));
            sib.env.process (sib.detector.process (m));

            left[i] = sib.cut[0].process (harsh.cut[0].process (presence[0].process (mud[0].process (xl))));
            if (! mono)
                r[i] = sib.cut[1].process (harsh.cut[1].process (presence[1].process (mud[1].process (xr))));
        }

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
