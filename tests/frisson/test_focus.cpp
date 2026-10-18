// FOCUS acceptance tests (docs/prd/frisson-focus.md, section 5).

#include "TestHarness.h"
#include "TestSignals.h"

#include "modules/Focus.h"

#include <ap/Filters.h>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace fsig;

namespace
{
    constexpr size_t kOneSec  = 48000;
    constexpr size_t kTwoSec  = 96000;
    constexpr size_t kSettle  = 24000;   // skip the first 0.5 s (filter / envelope settling)

    double toDb (double ratio) { return 20.0 * std::log10 (std::max (1.0e-12, ratio)); }

    std::vector<float> add (std::vector<float> a, const std::vector<float>& b)
    {
        for (size_t i = 0; i < a.size(); ++i) a[i] += b[i];
        return a;
    }

    std::vector<float> scaled (std::vector<float> v, double gain)
    {
        for (auto& s : v) s = (float) ((double) s * gain);
        return v;
    }

    /** Pink noise at a given RMS level in dBFS. */
    std::vector<float> pinkAt (double rmsDbfs, size_t n, uint32_t seed)
    {
        auto v = pink (n, 1.0f, seed);
        return scaled (v, std::pow (10.0, rmsDbfs / 20.0) / rms (v));
    }

    /** Sine with peak level in dBFS. */
    std::vector<float> sineAt (double hz, double peakDbfs, size_t n)
    {
        return sine (hz, n, (float) std::pow (10.0, peakDbfs / 20.0));
    }

    /** Scales a stereo signal so the louder channel's RMS hits `rmsDbfs`. */
    Stereo normalisedTo (const Stereo& s, double rmsDbfs)
    {
        const double g = std::pow (10.0, rmsDbfs / 20.0) / std::max (rms (s.l), rms (s.r));
        return stereo (scaled (s.l, g), scaled (s.r, g));
    }

    struct RunResult
    {
        Stereo out;
        float  maxActivity = 0.0f;
        float  minActivityLastSec = 1.0f;
        float  finalActivity = 0.0f;
    };

    /** Runs a fresh FocusModule over `in`, tracking activity() after each block. */
    RunResult runFocus (const Stereo& in, float amount, float eqFreqMul = 1.0f, int blockSize = 256)
    {
        frisson::FocusModule m;
        m.prepare (kSampleRate, blockSize);
        frisson::Modulation mod;
        mod.eqFreqMul = eqFreqMul;
        m.setModulation (mod);
        m.setAmount (amount);

        RunResult r;
        r.out = in;
        const size_t n = in.size();
        const size_t lastSec = n > kOneSec ? n - kOneSec : 0;
        for (size_t pos = 0; pos < n; pos += (size_t) blockSize)
        {
            const int len = (int) std::min ((size_t) blockSize, n - pos);
            m.process (r.out.l.data() + pos, r.out.r.data() + pos, len);
            const float a = m.activity();
            r.maxActivity = std::max (r.maxActivity, a);
            if (pos >= lastSec)
                r.minActivityLastSec = std::min (r.minActivityLastSec, a);
        }
        r.finalActivity = m.activity();
        return r;
    }

    /** Level change in dB of a single tone, processed (amount) vs dry, left channel. */
    double toneGainDb (double hz, float amount, float eqFreqMul = 1.0f)
    {
        const auto in = dualMono (sine (hz, kOneSec, 0.5f));
        const auto r = runFocus (in, amount, eqFreqMul);
        return toDb (toneLevel (r.out.l, hz, kSettle) / toneLevel (in.l, hz, kSettle));
    }

    /** The R2 static shape alone (mud cut + presence lift at full amount), as plain biquads. */
    Stereo staticReference (const Stereo& in)
    {
        Stereo out = in;
        for (auto* ch : { &out.l, &out.r })
        {
            ap::Biquad mud, pres;
            mud.configure (ap::Biquad::Type::Peak, kSampleRate, 330.0f, 1.0f, -3.0f);
            pres.configure (ap::Biquad::Type::Peak, kSampleRate, 2200.0f, 1.2f, 2.0f);
            for (auto& s : *ch) s = pres.process (mud.process (s));
        }
        return out;
    }

    /** T3 signal: decorrelated stereo pink at -20 dBFS RMS plus a 3.5 kHz sine at -14 dBFS peak. */
    Stereo harshSignal (size_t n)
    {
        const auto tone = sineAt (3500.0, -14.0, n);
        return stereo (add (pinkAt (-20.0, n, 11u), tone), add (pinkAt (-20.0, n, 12u), tone));
    }

    Stereo pinkOnly (size_t n) { return stereo (pinkAt (-20.0, n, 11u), pinkAt (-20.0, n, 12u)); }

    /** Cut at `hz` in dB (amount 1 vs amount 0), averaged over both channels. */
    double cutAtDb (const Stereo& in, double hz, float* activityOut = nullptr)
    {
        const auto wet = runFocus (in, 1.0f);
        const auto dry = runFocus (in, 0.0f);
        if (activityOut != nullptr) *activityOut = wet.minActivityLastSec;
        return 0.5 * (toDb (toneLevel (wet.out.l, hz, kSettle) / toneLevel (dry.out.l, hz, kSettle))
                    + toDb (toneLevel (wet.out.r, hz, kSettle) / toneLevel (dry.out.r, hz, kSettle)));
    }
}

//==============================================================================
AP_TEST (focus_t1_amount_zero_is_exact_bypass)
{
    const auto in = stereoPink (kOneSec);

    // Fresh module at 0.
    const auto r = runFocus (in, 0.0f);
    CHECK (maxDiff (r.out.l, in.l) == 0.0f);
    CHECK (maxDiff (r.out.r, in.r) == 0.0f);
    CHECK (r.maxActivity == 0.0f);

    // Odd block size and a mono (aliased) buffer too.
    const auto rOdd = runFocus (in, 0.0f, 1.0f, 333);
    CHECK (maxDiff (rOdd.out.l, in.l) == 0.0f);
    CHECK (maxDiff (rOdd.out.r, in.r) == 0.0f);

    frisson::FocusModule m;
    m.prepare (kSampleRate, 256);
    m.setAmount (0.0f);
    auto mono = in.l;
    m.process (mono.data(), mono.data(), (int) mono.size());
    CHECK (maxDiff (mono, in.l) == 0.0f);
}

AP_TEST (focus_t2_static_shape)
{
    const double mud  = toneGainDb (330.0, 1.0f);
    const double pres = toneGainDb (2200.0, 1.0f);
    const double k1   = toneGainDb (1000.0, 1.0f);
    const double k10  = toneGainDb (10000.0, 1.0f);

    CHECK_NEAR (mud, -3.0, 0.4);
    CHECK_NEAR (pres, 2.0, 0.4);
    CHECK_NEAR (k1, 0.0, 0.3);
    CHECK_NEAR (k10, 0.0, 0.3);

    // Scales with amount: half the knob is about half the dB.
    CHECK_NEAR (toneGainDb (330.0, 0.5f), -1.5, 0.25);
    CHECK_NEAR (toneGainDb (2200.0, 0.5f), 1.0, 0.25);
}

AP_TEST (focus_t3_harsh_guard)
{
    float activity = 0.0f;
    const double harshCut = cutAtDb (harshSignal (kTwoSec), 3500.0, &activity);
    CHECK (harshCut <= -2.0);
    CHECK (activity > 0.3f);

    // Pink alone: the guard must not fire (R5: cut < 0.3 dB over 2 s). Small blocks so every
    // 32-sample control update is observed.
    const auto pinkIn = pinkOnly (kTwoSec);
    const auto wet = runFocus (pinkIn, 1.0f, 1.0f, 32);
    CHECK (wet.maxActivity * 4.0f < 0.3f);

    // The 3.5 kHz band of pink matches the static EQ alone within 0.3 dB, i.e. no dynamic cut.
    // (Against amount 0 it rises ~+0.8 dB, which is the R2 presence peak's skirt, not the guard.)
    const auto ref = staticReference (pinkIn);
    CHECK_NEAR (toDb (toneLevel (wet.out.l, 3500.0, kSettle) / toneLevel (ref.l, 3500.0, kSettle)), 0.0, 0.3);
    CHECK_NEAR (toDb (bandRms (wet.out.l, 3000.0, 4000.0, kSettle) / bandRms (ref.l, 3000.0, 4000.0, kSettle)), 0.0, 0.3);
    CHECK_NEAR (toDb (bandRms (wet.out.r, 3000.0, 4000.0, kSettle) / bandRms (ref.r, 3000.0, 4000.0, kSettle)), 0.0, 0.3);
}

AP_TEST (focus_t4_sibilance_guard)
{
    const auto tone = sineAt (7000.0, -14.0, kTwoSec);
    const Stereo sibilant = stereo (add (pinkAt (-20.0, kTwoSec, 11u), tone), add (pinkAt (-20.0, kTwoSec, 12u), tone));

    float activity = 0.0f;
    CHECK (cutAtDb (sibilant, 7000.0, &activity) <= -2.0);
    CHECK (activity > 0.3f);

    // Pink alone: 7 kHz unchanged within 0.3 dB against amount 0, and against the static EQ.
    const auto pinkIn = pinkOnly (kTwoSec);
    CHECK_NEAR (cutAtDb (pinkIn, 7000.0), 0.0, 0.3);
    const auto wet = runFocus (pinkIn, 1.0f, 1.0f, 32);
    CHECK (wet.maxActivity * 4.0f < 0.3f);
    const auto ref = staticReference (pinkIn);
    CHECK_NEAR (toDb (bandRms (wet.out.l, 6000.0, 8000.0, kSettle) / bandRms (ref.l, 6000.0, 8000.0, kSettle)), 0.0, 0.3);
}

AP_TEST (focus_t5_level_independent)
{
    const auto loud = harshSignal (kTwoSec);
    const auto quiet = normalisedTo (loud, -40.0);

    const double loudCut  = cutAtDb (loud, 3500.0);
    const double quietCut = cutAtDb (quiet, 3500.0);
    CHECK_NEAR (quietCut, loudCut, 0.5);
    CHECK (quietCut <= -2.0);

    // Pink alone stays untouched at -40 dBFS too.
    const auto r = runFocus (normalisedTo (pinkOnly (kTwoSec), -40.0), 1.0f, 1.0f, 32);
    CHECK (r.maxActivity * 4.0f < 0.3f);
}

AP_TEST (focus_t6_eq_freq_mul_moves_mud_cut)
{
    const double at330Base  = toneGainDb (330.0, 1.0f, 1.0f);
    const double at396Base  = toneGainDb (396.0, 1.0f, 1.0f);
    const double at330Moved = toneGainDb (330.0, 1.0f, 1.2f);
    const double at396Moved = toneGainDb (396.0, 1.0f, 1.2f);

    CHECK (at330Moved > at330Base + 0.2);   // 330 Hz is cut less
    CHECK (at396Moved < at396Base - 0.2);   // 396 Hz is cut more
    CHECK_NEAR (at396Moved, -3.0, 0.4);     // the cut now centres on 396 Hz

    // eqFreqMul is clamped to 0.8..1.25.
    CHECK_NEAR (toneGainDb (412.5, 1.0f, 5.0f), toneGainDb (412.5, 1.0f, 1.25f), 1.0e-4);
    CHECK_NEAR (toneGainDb (264.0, 1.0f, 0.1f), toneGainDb (264.0, 1.0f, 0.8f), 1.0e-4);
}

AP_TEST (focus_t7_no_latency_finite_silence)
{
    frisson::FocusModule m;
    CHECK_EQ (m.latencySamples(), 0);
    m.prepare (kSampleRate, 512);
    CHECK_EQ (m.latencySamples(), 0);
    m.setAmount (1.0f);
    CHECK_EQ (m.latencySamples(), 0);

    // Silence in -> silence out, no NaN, no guard activity.
    Stereo silence (kOneSec);
    const auto s = runFocus (silence, 1.0f);
    CHECK (allFinite (s.out.l) && allFinite (s.out.r));
    CHECK (peak (s.out.l) == 0.0f && peak (s.out.r) == 0.0f);
    CHECK (s.maxActivity == 0.0f);

    // Program -> silence -> program, full-scale square-ish bursts and an impulse: all finite.
    const size_t n = kTwoSec;
    std::vector<float> x (n, 0.0f);
    const auto p = pink (n, 0.9f, 5u);
    for (size_t i = 0; i < n / 3; ++i) x[i] = p[i];
    for (size_t i = 2 * n / 3; i < n; ++i) x[i] = (i / 24) % 2 ? 1.0f : -1.0f;
    x[n / 2] = 1.0f;
    const auto r = runFocus (dualMono (x), 1.0f, 1.0f, 64);
    CHECK (allFinite (r.out.l) && allFinite (r.out.r));
    CHECK (std::isfinite (r.maxActivity) && r.maxActivity <= 1.0f);

    // Tiny levels (-150 dBFS) neither blow up nor engage the guards.
    const auto tiny = runFocus (dualMono (sine (3500.0, kOneSec, 3.0e-8f)), 1.0f);
    CHECK (allFinite (tiny.out.l));
    CHECK (tiny.maxActivity == 0.0f);
}

AP_TEST (focus_r8_amount_changes_are_click_free_and_return_to_bypass)
{
    // A 1 kHz sine sits where the static shape is flat, so any click shows up as a sample step
    // larger than the sine's own maximum slope.
    const size_t n = kTwoSec;
    const auto in = dualMono (sine (1000.0, n, 0.5f));
    const double sineSlope = 0.5 * ap::kTwoPiD * 1000.0 / kSampleRate;

    frisson::FocusModule m;
    m.prepare (kSampleRate, 256);
    m.setAmount (0.0f);

    Stereo out = in;
    const size_t blocks = n / 256;
    for (size_t b = 0; b < blocks; ++b)
    {
        if (b % 40 == 10) m.setAmount (1.0f);
        if (b % 40 == 30) m.setAmount (0.0f);
        frisson::Modulation mod;
        mod.eqFreqMul = (b / 20) % 2 ? 1.25f : 0.8f;   // jumps are smoothed internally
        m.setModulation (mod);
        m.process (out.l.data() + b * 256, out.r.data() + b * 256, 256);
    }

    double maxStep = 0.0;
    for (size_t i = 1; i < n; ++i)
        maxStep = std::max (maxStep, (double) std::fabs (out.l[i] - out.l[i - 1]));
    CHECK (maxStep < sineSlope * 1.05);
    CHECK (allFinite (out.l));

    // After a ramp down to 0 the module is a bit-exact bypass again.
    m.setAmount (0.0f);
    auto settle = in;
    m.process (settle.l.data(), settle.r.data(), (int) kOneSec);   // ramp completes in here
    auto after = stereoPink (kOneSec);
    const auto ref = after;
    m.process (after.l.data(), after.r.data(), (int) kOneSec);
    CHECK (maxDiff (after.l, ref.l) == 0.0f);
    CHECK (maxDiff (after.r, ref.r) == 0.0f);
    CHECK (m.activity() == 0.0f);
}
