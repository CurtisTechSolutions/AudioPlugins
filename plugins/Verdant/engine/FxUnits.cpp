#include "FxUnits.h"

#include <ap/Dynamics.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace verdant
{

namespace
{
    constexpr float kParamRampMs   = 20.0f;   // mix, depth, gain ramps
    constexpr float kSwitchFadeMs  = 10.0f;   // type / stage-count crossfades
    constexpr int   kCoeffInterval = 32;      // samples between filter-coefficient updates while a ramp moves

    inline float log2Hz (float hz) noexcept   { return std::log2 (std::max (1.0f, hz)); }
    inline float maxFilterHz (double sr) noexcept { return (float) (sr * 0.45); }
}

//==============================================================================
// Distortion
//==============================================================================
void Distortion::prepare (double sampleRate, int)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    drive.prepare (sr, kParamRampMs);
    toneLog2.prepare (sr, kParamRampMs);
    mix.prepare (sr, kParamRampMs);
    typeFade.prepare (sr, kSwitchFadeMs);
    dcL.prepare (sr);
    dcR.prepare (sr);
    reset();
}

void Distortion::reset()
{
    fresh = true;
    toneL.reset();
    toneR.reset();
    dcL.reset();
    dcR.reset();
    holdL = holdR = 0.0f;
    holdPhase = 1.0f;
    configuredTone = -1.0f;
    typeFade.snap (1.0f);
    previousType = type;
}

void Distortion::configureTone (float hz) noexcept
{
    hz = ap::clamp (hz, 20.0f, maxFilterHz (sr));
    toneL.configure (ap::Biquad::Type::LowPass, sr, hz, 0.7071f);
    toneR.configure (ap::Biquad::Type::LowPass, sr, hz, 0.7071f);
    configuredTone = hz;
}

void Distortion::set (const DistortionParams& p)
{
    const float d = ap::clamp (p.drive, 0.0f, 1.0f);
    const float t = log2Hz (ap::clamp (p.toneHz, 20.0f, 20000.0f));
    const float m = ap::clamp (p.mix, 0.0f, 1.0f);
    const DistType newType = (int) p.type < (int) DistType::Count ? p.type : DistType::Soft;

    if (fresh)
    {
        drive.snap (d);
        toneLog2.snap (t);
        mix.snap (m);
        type = previousType = newType;
        typeFade.snap (1.0f);
        configureTone (std::exp2 (t));
        fresh = false;
        return;
    }

    drive.setTarget (d);
    toneLog2.setTarget (t);
    mix.setTarget (m);

    if (newType != type)
    {
        // Crossfade from whatever is audible now: if a fade is still running, the
        // outgoing curve is the one that dominates.
        if (typeFade.value() >= 0.5f) previousType = type;
        type = newType;
        typeFade.snap (0.0f);
        typeFade.setTarget (1.0f);
    }
}

float Distortion::shape (DistType t, float x, float g) noexcept
{
    switch (t)
    {
        case DistType::Soft:
            return std::tanh (g * x) / g;

        case DistType::Hard:
            return ap::clamp (g * x, -1.0f, 1.0f) / g;

        case DistType::Tube:
        {
            ap::TubeShaper tube;
            tube.drive = g;
            return tube.process (x);
        }

        case DistType::Fold:
        {
            // Triangle wavefolder: slope 1 through the origin, folds back at ±1.
            const float u = g * x;
            float ph = (u + 1.0f) * 0.25f;
            ph -= std::floor (ph);
            return (1.0f - 4.0f * std::fabs (ph - 0.5f)) / g;
        }

        case DistType::Diode:
        {
            // Asymmetric exponential: the positive half conducts up to 1, the negative
            // half saturates early at -0.4 (even harmonics + DC, removed afterwards).
            const float u = g * x;
            const float y = u >= 0.0f ? 1.0f - std::exp (-u) : (std::exp (2.5f * std::max (u, -40.0f)) - 1.0f) * 0.4f;
            return y / g;
        }

        case DistType::Crush:
        case DistType::Count:
        default:
            return x;
    }
}

float Distortion::shapeSample (DistType t, float x, float g, float drive01, float hold) const noexcept
{
    if (t != DistType::Crush)
        return shape (t, x, g);

    // Sample-and-hold decimation (1..16x, `hold` is the held input) and bit reduction
    // (16..4 bits), both driven by drive.
    const float levels = std::exp2 (15.0f - 12.0f * drive01);   // 2^(bits - 1)
    return std::round (hold * levels) / levels;
}

void Distortion::process (float* L, float* R, int numSamples) noexcept
{
    if (isBypassed() || numSamples <= 0)
        return;

    float g = std::exp2 (5.0f * drive.value());

    for (int i = 0; i < numSamples; ++i)
    {
        if (drive.isRamping())
        {
            drive.next();
            g = std::exp2 (5.0f * drive.value());
        }
        const float d = drive.value();
        const float m = mix.next();
        const float tf = typeFade.next();

        if (++toneCounter >= kCoeffInterval)
        {
            toneCounter = 0;
            if (toneLog2.isRamping() || configuredTone < 0.0f)
            {
                toneLog2.skip (kCoeffInterval);
                configureTone (std::exp2 (toneLog2.value()));
            }
        }

        bool sampleNow = false;
        holdPhase += 1.0f / (1.0f + 15.0f * d);
        if (holdPhase >= 1.0f)
        {
            holdPhase -= std::floor (holdPhase);
            sampleNow = true;
        }

        const float xl = L[i], xr = R[i];
        if (sampleNow)
        {
            holdL = xl;
            holdR = xr;
        }
        float wl = shapeSample (type, xl, g, d, holdL);
        float wr = shapeSample (type, xr, g, d, holdR);
        if (tf < 1.0f)
        {
            const float ol = shapeSample (previousType, xl, g, d, holdL);
            const float orr = shapeSample (previousType, xr, g, d, holdR);
            wl = ol + tf * (wl - ol);
            wr = orr + tf * (wr - orr);
        }

        wl = toneL.process (dcL.process (wl));
        wr = toneR.process (dcR.process (wr));
        L[i] = xl + m * (wl - xl);
        R[i] = xr + m * (wr - xr);
    }
}

//==============================================================================
// Chorus
//==============================================================================
void Chorus::prepare (double sampleRate, int)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    for (auto* r : { &rate, &depth, &feedback, &mix })
        r->prepare (sr, kParamRampMs);
    const int maxDelay = (int) std::ceil (0.030 * sr) + 4;
    lineL.prepare (maxDelay);
    lineR.prepare (maxDelay);
    reset();
}

void Chorus::reset()
{
    fresh = true;
    lineL.reset();
    lineR.reset();
    phase = 0.0;
    lastL = lastR = 0.0f;
}

void Chorus::set (const ChorusParams& p)
{
    const float r = ap::clamp (p.rateHz, 0.01f, 20.0f);
    const float d = ap::clamp (p.depth, 0.0f, 1.0f);
    const float f = ap::clamp (p.feedback, 0.0f, 1.0f);
    const float m = ap::clamp (p.mix, 0.0f, 1.0f);
    if (fresh)
    {
        rate.snap (r); depth.snap (d); feedback.snap (f); mix.snap (m);
        fresh = false;
        return;
    }
    rate.setTarget (r); depth.setTarget (d); feedback.setTarget (f); mix.setTarget (m);
}

void Chorus::process (float* L, float* R, int numSamples) noexcept
{
    if (isBypassed() || numSamples <= 0)
        return;

    constexpr float kCentreMs[3] = { 7.0f, 12.0f, 19.0f };
    constexpr float kVoicePhase[3] = { 0.0f, 1.0f / 3.0f, 2.0f / 3.0f };
    const float msToSamples = (float) (sr * 0.001);
    const double invSr = 1.0 / sr;

    for (int i = 0; i < numSamples; ++i)
    {
        phase += (double) rate.next() * invSr;
        if (phase >= 1.0) phase -= 1.0;
        const float depthMs = 5.0f * depth.next();
        const float fb = 0.75f * feedback.next();
        const float m = mix.next();

        float wl = 0.0f, wr = 0.0f;
        for (int v = 0; v < 3; ++v)
        {
            const float ph = (float) phase + kVoicePhase[v];
            const float sl = std::sin (ap::kTwoPi * ph);
            const float sr90 = std::sin (ap::kTwoPi * (ph + 0.25f));
            wl += lineL.readFractional ((kCentreMs[v] + depthMs * sl) * msToSamples - 1.0f);
            wr += lineR.readFractional ((kCentreMs[v] + depthMs * sr90) * msToSamples - 1.0f);
        }
        wl *= (1.0f / 3.0f);
        wr *= (1.0f / 3.0f);

        const float xl = L[i], xr = R[i];
        lineL.push (fxdetail::sanitise (fxdetail::loopSafety (xl + fb * wl)));
        lineR.push (fxdetail::sanitise (fxdetail::loopSafety (xr + fb * wr)));
        L[i] = xl + m * (wl - xl);
        R[i] = xr + m * (wr - xr);
    }
}

//==============================================================================
// Phaser
//==============================================================================
void Phaser::prepare (double sampleRate, int)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    for (auto* r : { &rate, &depth, &feedback, &mix })
        r->prepare (sr, kParamRampMs);
    stageFade.prepare (sr, kSwitchFadeMs);
    reset();
}

void Phaser::reset()
{
    fresh = true;
    stateL.fill (0.0f);
    stateR.fill (0.0f);
    lastL = lastR = 0.0f;
    phase = 0.0;
    stageFade.snap (1.0f);
    previousStages = stages;
}

void Phaser::set (const PhaserParams& p)
{
    const float r = ap::clamp (p.rateHz, 0.01f, 20.0f);
    const float d = ap::clamp (p.depth, 0.0f, 1.0f);
    const float f = ap::clamp (p.feedback, 0.0f, 1.0f);
    const float m = ap::clamp (p.mix, 0.0f, 1.0f);
    const int n = p.stages <= 4 ? 4 : p.stages <= 6 ? 6 : p.stages <= 8 ? 8 : 12;

    if (fresh)
    {
        rate.snap (r); depth.snap (d); feedback.snap (f); mix.snap (m);
        stages = previousStages = n;
        stageFade.snap (1.0f);
        fresh = false;
        return;
    }
    rate.setTarget (r); depth.setTarget (d); feedback.setTarget (f); mix.setTarget (m);

    if (n != stages)
    {
        if (stageFade.value() >= 0.5f) previousStages = stages;
        stages = n;
        stageFade.snap (0.0f);
        stageFade.setTarget (1.0f);
    }
}

float Phaser::runChain (std::array<float, kMaxStages>& state, float x, float a, float& tapOld, float& tapNew) const noexcept
{
    // All 12 first-order all-passes share one coefficient; the outputs after
    // `previousStages` and `stages` are tapped so a stage-count change crossfades.
    const int last = std::max (stages, previousStages);
    float u = x;
    for (int s = 0; s < last; ++s)
    {
        const float y = a * u + state[(size_t) s];
        state[(size_t) s] = fxdetail::sanitise (u - a * y);
        u = y;
        if (s + 1 == previousStages) tapOld = u;
        if (s + 1 == stages)         tapNew = u;
    }
    return u;
}

void Phaser::process (float* L, float* R, int numSamples) noexcept
{
    if (isBypassed() || numSamples <= 0)
        return;

    const double invSr = 1.0 / sr;
    const float centreHz = std::sqrt (200.0f * 8000.0f);
    const float halfLogRange = 0.5f * std::log (8000.0f / 200.0f);
    const float maxHz = maxFilterHz (sr);

    auto coeffFor = [&] (float lfo, float dep) noexcept
    {
        const float hz = std::min (maxHz, centreHz * std::exp (halfLogRange * dep * lfo));
        const float t = std::tan (ap::kPi * hz / (float) sr);
        return (t - 1.0f) / (t + 1.0f);
    };

    for (int i = 0; i < numSamples; ++i)
    {
        phase += (double) rate.next() * invSr;
        if (phase >= 1.0) phase -= 1.0;
        const float dep = depth.next();
        const float fb = 0.9f * feedback.next();
        const float m = mix.next();
        const float tf = stageFade.next();

        const float lfoL = std::sin (ap::kTwoPi * (float) phase);
        const float lfoR = std::cos (ap::kTwoPi * (float) phase);   // quadrature

        const float xl = L[i], xr = R[i];
        float oldL = 0.0f, newL = 0.0f, oldR = 0.0f, newR = 0.0f;
        runChain (stateL, fxdetail::loopSafety (xl + fb * lastL), coeffFor (lfoL, dep), oldL, newL);
        runChain (stateR, fxdetail::loopSafety (xr + fb * lastR), coeffFor (lfoR, dep), oldR, newR);
        const float wl = oldL + tf * (newL - oldL);
        const float wr = oldR + tf * (newR - oldR);
        lastL = fxdetail::sanitise (wl);
        lastR = fxdetail::sanitise (wr);

        L[i] = xl + m * (wl - xl);
        R[i] = xr + m * (wr - xr);
    }
}

//==============================================================================
// Flanger
//==============================================================================
void Flanger::prepare (double sampleRate, int)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    for (auto* r : { &rate, &depth, &feedback, &mix })
        r->prepare (sr, kParamRampMs);
    const int maxDelay = (int) std::ceil (0.010 * sr) + 4;
    lineL.prepare (maxDelay);
    lineR.prepare (maxDelay);
    reset();
}

void Flanger::reset()
{
    fresh = true;
    lineL.reset();
    lineR.reset();
    phase = 0.0;
}

void Flanger::set (const FlangerParams& p)
{
    const float r = ap::clamp (p.rateHz, 0.01f, 20.0f);
    const float d = ap::clamp (p.depth, 0.0f, 1.0f);
    const float f = ap::clamp (p.feedback, -1.0f, 1.0f);
    const float m = ap::clamp (p.mix, 0.0f, 1.0f);
    if (fresh)
    {
        rate.snap (r); depth.snap (d); feedback.snap (f); mix.snap (m);
        fresh = false;
        return;
    }
    rate.setTarget (r); depth.setTarget (d); feedback.setTarget (f); mix.setTarget (m);
}

void Flanger::process (float* L, float* R, int numSamples) noexcept
{
    if (isBypassed() || numSamples <= 0)
        return;

    const float msToSamples = (float) (sr * 0.001);
    const double invSr = 1.0 / sr;

    for (int i = 0; i < numSamples; ++i)
    {
        phase += (double) rate.next() * invSr;
        if (phase >= 1.0) phase -= 1.0;
        const float sweepMs = 7.5f * depth.next();
        const float fb = 0.95f * feedback.next();
        const float m = mix.next();

        const float lfo = 0.5f + 0.5f * std::sin (ap::kTwoPi * (float) phase);   // 0..1; right = inverted
        const float dl = (0.5f + sweepMs * lfo) * msToSamples;
        const float dr = (0.5f + sweepMs * (1.0f - lfo)) * msToSamples;

        const float wl = lineL.readFractional (dl - 1.0f);
        const float wr = lineR.readFractional (dr - 1.0f);
        const float xl = L[i], xr = R[i];
        lineL.push (fxdetail::sanitise (fxdetail::loopSafety (xl + fb * wl)));
        lineR.push (fxdetail::sanitise (fxdetail::loopSafety (xr + fb * wr)));
        L[i] = xl + m * (wl - xl);
        R[i] = xr + m * (wr - xr);
    }
}

//==============================================================================
// StereoDelay
//==============================================================================
void StereoDelay::prepare (double sampleRate, int)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    delaySamples.prepare (sr, 50.0f);
    for (auto* r : { &feedback, &pingPong, &mix, &lowLog2, &highLog2 })
        r->prepare (sr, kParamRampMs);
    const int maxDelay = (int) std::ceil ((double) kMaxSeconds * sr) + 4;
    lineL.prepare (maxDelay);
    lineR.prepare (maxDelay);
    reset();
}

void StereoDelay::reset()
{
    fresh = true;
    lineL.reset();
    lineR.reset();
    hpL.reset(); hpR.reset(); lpL.reset(); lpR.reset();
    configuredLow = configuredHigh = -1.0f;
}

float StereoDelay::resolveTimeMs (const DelayParams& p, const Transport& t) noexcept
{
    float ms = p.timeMs;
    if (p.sync)
    {
        const double bpm = t.hasTempo && t.bpm > 0.0 ? t.bpm : 120.0;
        ms = (float) (ap::noteDivisionBeats (p.division) * 60.0 / bpm * 1000.0);
    }
    return ap::clamp (std::isfinite (ms) ? ms : 375.0f, 1.0f, 2000.0f);
}

void StereoDelay::configureFilters (float lowHz, float highHz) noexcept
{
    lowHz  = ap::clamp (lowHz, 10.0f, maxFilterHz (sr));
    highHz = ap::clamp (highHz, 10.0f, maxFilterHz (sr));
    hpL.configure (ap::Biquad::Type::HighPass, sr, lowHz, 0.7071f);
    hpR.configure (ap::Biquad::Type::HighPass, sr, lowHz, 0.7071f);
    lpL.configure (ap::Biquad::Type::LowPass, sr, highHz, 0.7071f);
    lpR.configure (ap::Biquad::Type::LowPass, sr, highHz, 0.7071f);
    configuredLow = lowHz;
    configuredHigh = highHz;
}

void StereoDelay::set (const DelayParams& p, const Transport& t)
{
    params = p;
    const double samples = (double) resolveTimeMs (p, t) * 0.001 * sr;
    const float fb = ap::clamp (p.feedback, 0.0f, 0.98f);
    const float pp = p.pingPong ? 1.0f : 0.0f;
    const float m = ap::clamp (p.mix, 0.0f, 1.0f);
    const float lo = log2Hz (ap::clamp (p.lowCutHz, 10.0f, 20000.0f));
    const float hi = log2Hz (ap::clamp (p.highCutHz, 10.0f, 22000.0f));

    if (fresh)
    {
        delaySamples.snap (samples);
        feedback.snap (fb); pingPong.snap (pp); mix.snap (m); lowLog2.snap (lo); highLog2.snap (hi);
        configureFilters (std::exp2 (lo), std::exp2 (hi));
        fresh = false;
        return;
    }
    delaySamples.setTarget (samples);
    feedback.setTarget (fb); pingPong.setTarget (pp); mix.setTarget (m); lowLog2.setTarget (lo); highLog2.setTarget (hi);
}

float StereoDelay::tailSeconds() const noexcept
{
    if (isBypassed())
        return 0.0f;
    const float seconds = (float) (delaySamples.targetValue() / sr);
    const float fb = feedback.targetValue();
    if (fb < 1.0e-3f)
        return std::min (20.0f, seconds + 0.1f);
    // Echo k has gain fb^k: count the echoes until -60 dB.
    const float echoes = std::log (0.001f) / std::log (fb);
    return std::min (20.0f, seconds * (1.0f + echoes));
}

void StereoDelay::process (float* L, float* R, int numSamples) noexcept
{
    if (isBypassed() || numSamples <= 0)
        return;

    const double maxD = (double) std::min (lineL.maxDelay(), lineR.maxDelay()) - 1.0;

    for (int i = 0; i < numSamples; ++i)
    {
        if (++filterCounter >= kCoeffInterval)
        {
            filterCounter = 0;
            if (lowLog2.isRamping() || highLog2.isRamping())
            {
                lowLog2.skip (kCoeffInterval);
                highLog2.skip (kCoeffInterval);
                configureFilters (std::exp2 (lowLog2.value()), std::exp2 (highLog2.value()));
            }
        }

        const float d = (float) ap::clamp (delaySamples.next(), 1.0, maxD);
        const float fb = feedback.next();
        const float pp = pingPong.next();
        const float m = mix.next();

        const float ol = lineL.readFractional (d - 1.0f);
        const float orr = lineR.readFractional (d - 1.0f);
        const float xl = L[i], xr = R[i];

        // Ping-pong: the (mono) input enters on the left and the feedback crosses
        // sides, L -> R -> L. pp crossfades between straight and crossed routing.
        const float inL = xl + pp * (0.5f * (xl + xr) - xl);
        const float inR = xr * (1.0f - pp);
        const float fbL = fb * (ol + pp * (orr - ol));
        const float fbR = fb * (orr + pp * (ol - orr));

        const float wl = lpL.process (hpL.process (inL + fbL));
        const float wr = lpR.process (hpR.process (inR + fbR));
        lineL.push (fxdetail::sanitise (fxdetail::loopSafety (wl)));
        lineR.push (fxdetail::sanitise (fxdetail::loopSafety (wr)));

        L[i] = xl + m * ol;
        R[i] = xr + m * orr;
    }
}

//==============================================================================
// Reverb
//==============================================================================
namespace
{
    // Prime-ish line lengths (ms) at size 0.5 (scale 1.0); mutually incommensurate so
    // the modes spread evenly and small sizes do not flutter.
    constexpr float kLineMs[Reverb::kLines] = { 31.3f, 37.9f, 41.1f, 46.3f, 53.9f, 59.3f, 67.1f, 73.7f };
    constexpr float kModHz[Reverb::kLines]  = { 0.11f, 0.17f, 0.23f, 0.29f, 0.37f, 0.43f, 0.53f, 0.61f };
    constexpr float kDiffMsL[Reverb::kDiffusers] = { 4.77f, 3.59f, 12.73f, 9.31f };
    constexpr float kDiffMsR[Reverb::kDiffusers] = { 4.97f, 3.37f, 12.11f, 9.83f };
    constexpr float kDiffGain = 0.6f;
    constexpr float kSignL[Reverb::kLines] = { 1, -1, 1, -1, 1, -1, 1, -1 };
    constexpr float kSignR[Reverb::kLines] = { 1, 1, -1, -1, 1, 1, -1, -1 };
    constexpr int kReverbControl = 16;   // samples between length / gain / modulation updates
    constexpr float kMaxSizeScale = 1.5f;

    inline float sizeScale (float size01) noexcept { return 0.5f + ap::clamp (size01, 0.0f, 1.0f); }
}

float Reverb::rt60Seconds (float decay01) noexcept
{
    return 0.3f * std::pow (40.0f, ap::clamp (decay01, 0.0f, 1.0f));
}

void Reverb::prepare (double sampleRate, int)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    modDepth = (float) (0.00025 * sr);   // 0.25 ms
    const double controlRate = sr / (double) kReverbControl;
    for (int k = 0; k < kLines; ++k)
    {
        lines[(size_t) k].prepare ((int) std::ceil (kLineMs[k] * kMaxSizeScale * 0.001 * sr + modDepth) + 8);
        lengths[(size_t) k].prepare (controlRate, 150.0f);
    }
    for (int k = 0; k < kDiffusers; ++k)
    {
        diffL[(size_t) k].length = std::max (1, (int) std::lround (kDiffMsL[k] * 0.001 * sr));
        diffR[(size_t) k].length = std::max (1, (int) std::lround (kDiffMsR[k] * 0.001 * sr));
        diffL[(size_t) k].line.prepare (diffL[(size_t) k].length + 2);
        diffR[(size_t) k].line.prepare (diffR[(size_t) k].length + 2);
    }
    const int maxPre = (int) std::ceil (0.2 * sr) + 4;
    predelayL.prepare (maxPre);
    predelayR.prepare (maxPre);
    predelay.prepare (sr, 50.0f);
    for (auto* r : { &decayRamp, &dampRamp, &width, &mix })
        r->prepare (sr, kParamRampMs);
    reset();
}

void Reverb::reset()
{
    fresh = true;
    for (auto& l : lines) l.reset();
    for (auto& d : diffL) d.line.reset();
    for (auto& d : diffR) d.line.reset();
    predelayL.reset();
    predelayR.reset();
    lp.fill (0.0f);
    for (int k = 0; k < kLines; ++k)
    {
        modPhase[(size_t) k] = (float) k / (float) kLines;
        modRate[(size_t) k] = kModHz[k];
    }
    gainCounter = 0;
}

void Reverb::set (const ReverbParams& p)
{
    params = p;
    const float scale = sizeScale (p.size);
    const double pre = std::max (1.0, (double) ap::clamp (p.predelayMs, 0.0f, 200.0f) * 0.001 * sr);
    const float dec = ap::clamp (p.decay, 0.0f, 1.0f);
    const float dmp = ap::clamp (p.damp, 0.0f, 1.0f);
    const float w = ap::clamp (p.width, 0.0f, 1.0f);
    const float m = ap::clamp (p.mix, 0.0f, 1.0f);

    if (fresh)
    {
        for (int k = 0; k < kLines; ++k)
            lengths[(size_t) k].snap (kLineMs[k] * scale * 0.001 * sr);
        predelay.snap (pre);
        decayRamp.snap (dec); dampRamp.snap (dmp); width.snap (w); mix.snap (m);
        updateLoopGains();
        fresh = false;
        return;
    }
    for (int k = 0; k < kLines; ++k)
        lengths[(size_t) k].setTarget (kLineMs[k] * scale * 0.001 * sr);
    predelay.setTarget (pre);
    decayRamp.setTarget (dec); dampRamp.setTarget (dmp); width.setTarget (w); mix.setTarget (m);
}

void Reverb::updateLoopGains() noexcept
{
    // Per-line gain for a -60 dB decay after RT60 seconds: g = 10^(-3 len / (RT60 sr)).
    const float rt60 = rt60Seconds (decayRamp.value());
    for (int k = 0; k < kLines; ++k)
    {
        const float len = (float) lengths[(size_t) k].value();
        loopGain[(size_t) k] = std::min (0.9995f, std::pow (10.0f, -3.0f * len / (rt60 * (float) sr)));
    }
    // Damping: in-loop one-pole low-pass from 18 kHz (damp 0) down to 1.2 kHz (damp 1).
    const float hz = std::min (maxFilterHz (sr), 18000.0f * std::pow (1200.0f / 18000.0f, dampRamp.value()));
    dampCoeff = std::exp (-ap::kTwoPi * hz / (float) sr);
}

float Reverb::tailSeconds() const noexcept
{
    if (isBypassed())
        return 0.0f;
    return std::min (20.0f, rt60Seconds (decayRamp.targetValue()) + (float) (predelay.targetValue() / sr));
}

void Reverb::process (float* L, float* R, int numSamples) noexcept
{
    if (isBypassed() || numSamples <= 0)
        return;

    const float invSqrtLines = 1.0f / std::sqrt ((float) kLines);
    std::array<float, kLines> readPos {};
    std::array<float, kLines> v {};

    auto refreshControl = [&]
    {
        decayRamp.skip (kReverbControl);
        dampRamp.skip (kReverbControl);
        for (int k = 0; k < kLines; ++k)
        {
            lengths[(size_t) k].next();
            float ph = modPhase[(size_t) k] + modRate[(size_t) k] * (float) kReverbControl / (float) sr;
            ph -= std::floor (ph);
            modPhase[(size_t) k] = ph;
            readPos[(size_t) k] = (float) lengths[(size_t) k].value() + modDepth * std::sin (ap::kTwoPi * ph) - 1.0f;
        }
        updateLoopGains();
    };

    // Positions for the first sub-block.
    for (int k = 0; k < kLines; ++k)
        readPos[(size_t) k] = (float) lengths[(size_t) k].value() + modDepth * std::sin (ap::kTwoPi * modPhase[(size_t) k]) - 1.0f;

    // Wet gain: the network's energy grows with the decay time, so lean it back a little for long tails.
    const float outGain = 0.9f * invSqrtLines / std::sqrt (1.0f + 0.25f * rt60Seconds (decayRamp.value()));

    for (int i = 0; i < numSamples; ++i)
    {
        if (++gainCounter >= kReverbControl)
        {
            gainCounter = 0;
            refreshControl();
        }

        const float m = mix.next();
        const float w = width.next();
        const float pre = (float) predelay.next();
        const float xl = L[i], xr = R[i];

        float il = predelayL.readFractional (pre - 1.0f);
        float ir = predelayR.readFractional (pre - 1.0f);
        predelayL.push (xl);
        predelayR.push (xr);
        for (int k = 0; k < kDiffusers; ++k)
        {
            il = diffL[(size_t) k].process (il, kDiffGain);
            ir = diffR[(size_t) k].process (ir, kDiffGain);
        }

        float outL = 0.0f, outR = 0.0f;
        for (int k = 0; k < kLines; ++k)
        {
            const float o = lines[(size_t) k].readFractional (readPos[(size_t) k]);
            outL += kSignL[k] * o;
            outR += kSignR[k] * o;
            float& s = lp[(size_t) k];
            s = fxdetail::sanitise (o + dampCoeff * (s - o));
            v[(size_t) k] = loopGain[(size_t) k] * s;
        }

        // Orthogonal 8x8 Hadamard mixing (fast Walsh-Hadamard), energy preserving.
        for (int h = 1; h < kLines; h <<= 1)
            for (int a = 0; a < kLines; a += 2 * h)
                for (int b = a; b < a + h; ++b)
                {
                    const float p = v[(size_t) b], q = v[(size_t) (b + h)];
                    v[(size_t) b] = p + q;
                    v[(size_t) (b + h)] = p - q;
                }

        for (int k = 0; k < kLines; ++k)
            lines[(size_t) k].push (fxdetail::sanitise (v[(size_t) k] * invSqrtLines + ((k & 1) != 0 ? ir : il) * 0.5f));

        outL *= outGain;
        outR *= outGain;
        const float mid = 0.5f * (outL + outR);
        const float side = 0.5f * (outL - outR) * w;
        L[i] = xl + m * (mid + side);
        R[i] = xr + m * (mid - side);
    }
}

//==============================================================================
// ThreeBandEq
//==============================================================================
float ThreeBandEq::shelfMidpointHz (float cornerHz, float gainDb, bool highShelf) noexcept
{
    // RBJ shelves are specified at their half-gain point; keep the turnover corner where
    // the knob says (same idea as frisson's Weight shelf).
    return cornerHz * std::pow (10.0f, (highShelf ? -std::fabs (gainDb) : std::fabs (gainDb)) / 80.0f);
}

void ThreeBandEq::prepare (double sampleRate, int)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    for (auto& b : bands)
    {
        b.gainDb.prepare (sr, kParamRampMs);
        b.freqLog2.prepare (sr, kParamRampMs);
        b.q.prepare (sr, kParamRampMs);
    }
    reset();
}

void ThreeBandEq::reset()
{
    fresh = true;
    for (auto& b : bands)
    {
        b.l.reset();
        b.r.reset();
        b.cfgGain = 1.0e9f;
        b.cfgFreq = b.cfgQ = -1.0f;
    }
}

bool ThreeBandEq::isBypassed() const noexcept
{
    for (auto& b : bands)
        if (! b.settledAtZero()) return false;
    return true;
}

void ThreeBandEq::set (const EqParams& p)
{
    const float gains[3] = { ap::clamp (p.lowGainDb, -24.0f, 24.0f), ap::clamp (p.midGainDb, -24.0f, 24.0f), ap::clamp (p.highGainDb, -24.0f, 24.0f) };
    const float freqs[3] = { log2Hz (ap::clamp (p.lowHz, 20.0f, 20000.0f)), log2Hz (ap::clamp (p.midHz, 20.0f, 20000.0f)), log2Hz (ap::clamp (p.highHz, 20.0f, 20000.0f)) };
    const float qs[3]    = { 0.7071f, ap::clamp (p.midQ, 0.1f, 20.0f), 0.7071f };

    for (int k = 0; k < 3; ++k)
    {
        auto& b = bands[(size_t) k];
        if (fresh)
        {
            b.gainDb.snap (gains[k]);
            b.freqLog2.snap (freqs[k]);
            b.q.snap (qs[k]);
        }
        else
        {
            b.gainDb.setTarget (gains[k]);
            b.freqLog2.setTarget (freqs[k]);
            b.q.setTarget (qs[k]);
        }
    }
    fresh = false;
}

void ThreeBandEq::configure (Band& b, ap::Biquad::Type t, bool force) noexcept
{
    const float g = b.gainDb.value(), f = b.freqLog2.value(), q = b.q.value();
    if (! force && g == b.cfgGain && f == b.cfgFreq && q == b.cfgQ)
        return;
    float hz = std::exp2 (f);
    if (t == ap::Biquad::Type::LowShelf)  hz = shelfMidpointHz (hz, g, false);
    if (t == ap::Biquad::Type::HighShelf) hz = shelfMidpointHz (hz, g, true);
    hz = ap::clamp (hz, 10.0f, maxFilterHz (sr));
    b.l.configure (t, sr, hz, q, g);
    b.r.configure (t, sr, hz, q, g);
    b.cfgGain = g;
    b.cfgFreq = f;
    b.cfgQ = q;
}

void ThreeBandEq::process (float* L, float* R, int numSamples) noexcept
{
    if (numSamples <= 0)
        return;

    constexpr ap::Biquad::Type kTypes[3] = { ap::Biquad::Type::LowShelf, ap::Biquad::Type::Peak, ap::Biquad::Type::HighShelf };

    for (int start = 0; start < numSamples; start += kCoeffInterval)
    {
        const int n = std::min (kCoeffInterval, numSamples - start);
        for (int k = 0; k < 3; ++k)
        {
            auto& b = bands[(size_t) k];
            const bool wasActive = b.cfgGain < 1.0e8f;
            if (b.settledAtZero())
            {
                // An exact 0 dB band is skipped; clear it so it restarts cleanly.
                if (wasActive) { b.l.reset(); b.r.reset(); b.cfgGain = 1.0e9f; }
                continue;
            }
            configure (b, kTypes[k], ! wasActive);
            b.gainDb.skip (n);
            b.freqLog2.skip (n);
            b.q.skip (n);

            for (int i = start; i < start + n; ++i)
            {
                L[i] = b.l.process (L[i]);
                R[i] = b.r.process (R[i]);
            }
        }
    }
}

//==============================================================================
// Compressor
//==============================================================================
void Compressor::prepare (double sampleRate, int)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    for (auto* r : { &threshold, &ratio, &makeup, &mix })
        r->prepare (sr, kParamRampMs);
    rmsCoeff = ap::onePoleCoeff (5.0f, sr);
    peakCoeff = ap::onePoleCoeff (10.0f, sr);
    reset();
}

void Compressor::reset()
{
    fresh = true;
    meanSquare = peakEnv = 0.0f;
    grDb = 0.0f;
}

void Compressor::set (const CompressorParams& p)
{
    const float t = ap::clamp (p.thresholdDb, -80.0f, 0.0f);
    const float r = ap::clamp (p.ratio, 1.0f, 50.0f);
    const float mk = ap::clamp (p.makeupDb, -24.0f, 36.0f);
    const float m = ap::clamp (p.mix, 0.0f, 1.0f);
    attackCoeff = ap::onePoleCoeff (ap::clamp (p.attackMs, 0.01f, 1000.0f), sr);
    releaseCoeff = ap::onePoleCoeff (ap::clamp (p.releaseMs, 1.0f, 5000.0f), sr);
    if (fresh)
    {
        threshold.snap (t); ratio.snap (r); makeup.snap (mk); mix.snap (m);
        fresh = false;
        return;
    }
    threshold.setTarget (t); ratio.setTarget (r); makeup.setTarget (mk); mix.setTarget (m);
}

void Compressor::process (float* L, float* R, int numSamples) noexcept
{
    if (isBypassed() || numSamples <= 0)
        return;

    ap::GainComputer gc;
    gc.kneeDb = 6.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        gc.thresholdDb = threshold.next();
        gc.ratio = ratio.next();
        const float mk = makeup.next();
        const float m = mix.next();
        const float xl = L[i], xr = R[i];

        // Stereo-linked hybrid detector: 5 ms RMS scaled by √2 (a sine reads its peak), and a
        // fast peak follower that catches transients the RMS window would smear.
        const float sq = std::max (xl * xl, xr * xr);
        meanSquare = fxdetail::sanitise (sq + rmsCoeff * (meanSquare - sq));
        const float a = std::max (std::fabs (xl), std::fabs (xr));
        peakEnv = a > peakEnv ? a : fxdetail::sanitise (a + peakCoeff * (peakEnv - a));
        const float level = std::max (std::sqrt (2.0f * meanSquare), peakEnv);

        const float target = gc.gainReductionDb (ap::gainToDb (level, -120.0f));
        const float c = target < grDb ? attackCoeff : releaseCoeff;
        grDb = target + c * (grDb - target);
        if (! std::isfinite (grDb)) grDb = 0.0f;

        const float g = ap::dbToGain (grDb + mk);
        L[i] = xl + m * (xl * g - xl);
        R[i] = xr + m * (xr * g - xr);
    }
}

} // namespace verdant
