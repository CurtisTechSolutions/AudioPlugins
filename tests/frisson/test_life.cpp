#include "TestHarness.h"
#include "TestSignals.h"

#include "modules/Life.h"

#include <ap/Math.h>

#include <cmath>
#include <cstdint>
#include <vector>

// LIFE acceptance tests (docs/prd/frisson-life.md §5): 60 s of 512-sample
// blocks at 48 kHz, one Modulation per block.

using frisson::LifeModulator;
using frisson::Modulation;

namespace
{
    constexpr int    kBlock       = 512;
    constexpr double kSeconds     = 60.0;
    constexpr double kBlockRate   = fsig::kSampleRate / (double) kBlock;          // 93.75 Hz
    constexpr size_t kOneSecond   = 94;                                            // blocks in 1 s (rounded)
    constexpr size_t kTwoSeconds  = 187;                                           // floor (2 s × block rate)

    struct Series
    {
        std::vector<float> eqFreqMul, driveMul, widthMul, gainL, gainR, transientMul, airDb, densityMul, activity;
        std::vector<Modulation> raw;

        void push (const Modulation& m, float act)
        {
            raw.push_back (m);
            eqFreqMul.push_back (m.eqFreqMul);
            driveMul.push_back (m.driveMul);
            widthMul.push_back (m.widthMul);
            gainL.push_back (m.gainL);
            gainR.push_back (m.gainR);
            transientMul.push_back (m.transientMul);
            airDb.push_back (m.airDb);
            densityMul.push_back (m.densityMul);
            activity.push_back (act);
        }
    };

    /** Configures and prepares a LifeModulator (it holds atomics, so it is set up in place). */
    void setup (LifeModulator& life, float amount, float rate = 1.0f, uint32_t seed = LifeModulator::kDefaultSeed)
    {
        life.setSeed (seed);
        life.setRate (rate);
        life.setAmount (amount);
        life.prepare (fsig::kSampleRate, kBlock);
    }

    /** Runs `life` for `seconds` of `blockSize` blocks of quiet pink noise (the
        audio is irrelevant to LIFE, but the chain always passes real buffers). */
    Series collect (LifeModulator& life, double seconds = kSeconds, int blockSize = kBlock)
    {
        static const fsig::Stereo audio = fsig::stereoPink (4096, 0.1f);
        const size_t blocks = (size_t) std::llround (seconds * fsig::kSampleRate / (double) blockSize);
        Series s;
        for (size_t b = 0; b < blocks; ++b)
        {
            const size_t off = (b * 97u) % (audio.size() - (size_t) blockSize);   // vary the buffer
            const Modulation m = life.analyze (audio.l.data() + off, audio.r.data() + off, blockSize);
            s.push (m, life.activity());
        }
        return s;
    }

    std::vector<float> minusMean (const std::vector<float>& v, size_t from = 0, size_t to = (size_t) -1)
    {
        if (to > v.size()) to = v.size();
        double m = 0.0;
        for (size_t i = from; i < to; ++i) m += v[i];
        m /= (double) (to - from);
        std::vector<float> out;
        out.reserve (to - from);
        for (size_t i = from; i < to; ++i) out.push_back ((float) ((double) v[i] - m));
        return out;
    }

    /** Pearson correlation (mean removed), via fsig::correlation. */
    double pearson (const std::vector<float>& a, const std::vector<float>& b)
    {
        return fsig::correlation (minusMean (a), minusMean (b));
    }

    /** Autocorrelation at `lag` samples: Pearson of the series with its shifted self. */
    double autocorr (const std::vector<float>& v, size_t lag)
    {
        return fsig::correlation (minusMean (v, lag), minusMean (v, 0, v.size() - lag));
    }

    double stdDev (const std::vector<float>& v)
    {
        return fsig::rms (minusMean (v));
    }

    float minOf (const std::vector<float>& v) { float m = v[0]; for (float x : v) m = std::min (m, x); return m; }
    float maxOf (const std::vector<float>& v) { float m = v[0]; for (float x : v) m = std::max (m, x); return m; }

    float maxStep (const std::vector<float>& v)
    {
        float d = 0.0f;
        for (size_t i = 1; i < v.size(); ++i) d = std::max (d, std::fabs (v[i] - v[i - 1]));
        return d;
    }

    /** Longest run of consecutive identical values, in blocks (0 = always moving). */
    size_t longestConstantRun (const std::vector<float>& v)
    {
        size_t run = 0, longest = 0;
        for (size_t i = 1; i < v.size(); ++i)
        {
            run = (v[i] == v[i - 1]) ? run + 1 : 0;
            longest = std::max (longest, run);
        }
        return longest;
    }

    bool isIdentity (const Modulation& m)
    {
        const Modulation id = Modulation::identity();
        return m.airDb == id.airDb && m.widthMul == id.widthMul && m.densityMul == id.densityMul
            && m.driveMul == id.driveMul && m.eqFreqMul == id.eqFreqMul && m.transientMul == id.transientMul
            && m.gainL == id.gainL && m.gainR == id.gainR;
    }

    bool sameModulation (const Modulation& a, const Modulation& b)
    {
        return a.airDb == b.airDb && a.widthMul == b.widthMul && a.densityMul == b.densityMul
            && a.driveMul == b.driveMul && a.eqFreqMul == b.eqFreqMul && a.transientMul == b.transientMul
            && a.gainL == b.gainL && a.gainR == b.gainR;
    }

    bool sameSeries (const Series& a, const Series& b)
    {
        if (a.raw.size() != b.raw.size()) return false;
        for (size_t i = 0; i < a.raw.size(); ++i)
            if (! sameModulation (a.raw[i], b.raw[i])) return false;
        return true;
    }
}

//==============================================================================
// PRD acceptance tests

AP_TEST (life_t1_amount_zero_is_identity)
{
    LifeModulator life;
    setup (life, 0.0f);
    const Series s = collect (life);
    CHECK_EQ (s.raw.size(), (size_t) 5625);

    size_t nonIdentity = 0;
    float maxActivity = 0.0f;
    for (size_t i = 0; i < s.raw.size(); ++i)
    {
        if (! isIdentity (s.raw[i])) ++nonIdentity;
        maxActivity = std::max (maxActivity, s.activity[i]);
    }
    CHECK_EQ (nonIdentity, (size_t) 0);
    CHECK_EQ (maxActivity, 0.0f);
    CHECK_EQ (life.getAmount(), 0.0f);
}

AP_TEST (life_t2_full_amount_stays_in_range)
{
    LifeModulator life;
    setup (life, 1.0f);
    const Series s = collect (life);

    CHECK (minOf (s.eqFreqMul) >= 0.979f);
    CHECK (maxOf (s.eqFreqMul) <= 1.0215f);
    CHECK (minOf (s.driveMul) >= 0.9f);
    CHECK (maxOf (s.driveMul) <= 1.1f);
    CHECK (minOf (s.widthMul) >= 0.95f);
    CHECK (maxOf (s.widthMul) <= 1.05f);
    CHECK (minOf (s.transientMul) >= 0.9f);
    CHECK (maxOf (s.transientMul) <= 1.1f);

    const float gainTolDb = 0.2f + 1.0e-5f;   // float rounding of dB→lin→dB
    CHECK (std::fabs (ap::gainToDb (minOf (s.gainL))) <= gainTolDb);
    CHECK (std::fabs (ap::gainToDb (maxOf (s.gainL))) <= gainTolDb);
    CHECK (std::fabs (ap::gainToDb (minOf (s.gainR))) <= gainTolDb);
    CHECK (std::fabs (ap::gainToDb (maxOf (s.gainR))) <= gainTolDb);

    CHECK_EQ (minOf (s.airDb), 0.0f);
    CHECK_EQ (maxOf (s.airDb), 0.0f);
    CHECK_EQ (minOf (s.densityMul), 1.0f);
    CHECK_EQ (maxOf (s.densityMul), 1.0f);

    // Every route actually moves (no dead outputs), and all values are finite.
    for (const auto* v : { &s.eqFreqMul, &s.driveMul, &s.widthMul, &s.gainL, &s.gainR, &s.transientMul })
    {
        CHECK (fsig::allFinite (*v));
        CHECK (maxOf (*v) - minOf (*v) > 0.0f);
    }

    // Activity is |d1| ∈ 0..1, consistent with eqFreqMul = 2^(0.03 · d1) at amount 1.
    CHECK (minOf (s.activity) >= 0.0f && maxOf (s.activity) <= 1.0f);
    for (size_t i = 0; i < s.raw.size(); i += 250)
        CHECK_NEAR (std::fabs (std::log2 (s.eqFreqMul[i]) / 0.03f), s.activity[i], 1.0e-4);
}

AP_TEST (life_t3_it_moves)
{
    LifeModulator life;
    setup (life, 1.0f);
    const Series s = collect (life);

    const double sd = stdDev (s.eqFreqMul);
    std::printf ("    eqFreqMul sd %.5f, longest constant run %zu blocks (%.2f s)\n",
                 sd, longestConstantRun (s.eqFreqMul), (double) longestConstantRun (s.eqFreqMul) / kBlockRate);
    CHECK (sd > 0.004);
    CHECK (longestConstantRun (s.eqFreqMul) <= kTwoSeconds);
}

AP_TEST (life_t4_it_is_slow)
{
    LifeModulator life;
    setup (life, 1.0f);
    const Series s = collect (life);

    const double ac = autocorr (s.eqFreqMul, kOneSecond);
    const float step = maxStep (s.eqFreqMul);
    std::printf ("    eqFreqMul 1 s autocorrelation %.3f, max block step %.5f\n", ac, (double) step);
    CHECK (ac > 0.35);
    CHECK (step <= 0.002f);

    // The other routes are bounded per block the same way (no zipper on any of them).
    CHECK (maxStep (s.driveMul) <= 0.01f);
    CHECK (maxStep (s.transientMul) <= 0.01f);
    CHECK (maxStep (s.widthMul) <= 0.005f);
}

AP_TEST (life_t5_left_and_right_drift_independently)
{
    LifeModulator life;
    setup (life, 1.0f);
    const Series s = collect (life);

    const double c = pearson (s.gainL, s.gainR);
    std::printf ("    gainL/gainR correlation %.3f\n", c);
    CHECK (c < 0.5);
    CHECK (fsig::maxDiff (s.gainL, s.gainR) > 0.001f);   // genuinely different trims
}

AP_TEST (life_t6_deterministic_for_a_seed)
{
    LifeModulator a;
    setup (a, 1.0f);
    LifeModulator b;
    setup (b, 1.0f);
    const Series sa = collect (a);
    const Series sb = collect (b);
    CHECK (sameSeries (sa, sb));

    // reset() restarts the seeded series from the beginning.
    a.reset();
    const Series again = collect (a);
    CHECK (sameSeries (sa, again));

    // prepare() restarts it too (hosts call it on format changes): 10 s after a
    // re-prepare is the exact prefix of the original series.
    b.prepare (fsig::kSampleRate, kBlock);
    const Series tenSeconds = collect (b, 10.0);
    bool prefixMatches = true;
    for (size_t i = 0; i < tenSeconds.raw.size(); ++i)
        prefixMatches = prefixMatches && sameModulation (tenSeconds.raw[i], sa.raw[i]);
    CHECK (prefixMatches);

    // A different seed gives a different series.
    LifeModulator c;
    setup (c, 1.0f, 1.0f, LifeModulator::kDefaultSeed + 1u);
    const Series sc = collect (c, 10.0);
    CHECK (fsig::maxDiff (sc.eqFreqMul, tenSeconds.eqFreqMul) > 0.002f);
}

AP_TEST (life_t7_rate_two_is_faster_than_rate_half)
{
    LifeModulator fast;
    setup (fast, 1.0f, 2.0f);
    LifeModulator slow;
    setup (slow, 1.0f, 0.5f);
    const double acFast = autocorr (collect (fast).eqFreqMul, kOneSecond);
    const double acSlow = autocorr (collect (slow).eqFreqMul, kOneSecond);
    std::printf ("    1 s autocorrelation: rate 2 %.3f, rate 0.5 %.3f\n", acFast, acSlow);
    CHECK (acFast < acSlow);

    // Out-of-range rates clamp to 0.5..2.
    LifeModulator life;
    life.setRate (10.0f);
    CHECK_EQ (life.getRate(), 2.0f);
    life.setRate (0.01f);
    CHECK_EQ (life.getRate(), 0.5f);
}

//==============================================================================
// Contract / requirement checks beyond §5

AP_TEST (life_r6_ignores_the_audio)
{
    // Same seed, wildly different (or no) input: identical modulation.
    LifeModulator quiet;
    setup (quiet, 1.0f);
    LifeModulator loud;
    setup (loud, 1.0f);
    LifeModulator none;
    setup (none, 1.0f);
    const fsig::Stereo silence (kBlock);
    const fsig::Stereo hot = fsig::stereo (fsig::white (kBlock, 1.0f, 5u), fsig::sine (60.0, kBlock, 1.0f));
    bool same = true;
    for (int b = 0; b < 2000; ++b)
    {
        const Modulation mq = quiet.analyze (silence.l.data(), silence.r.data(), kBlock);
        const Modulation ml = loud.analyze (hot.l.data(), hot.r.data(), kBlock);
        const Modulation mn = none.analyze (nullptr, nullptr, kBlock);
        same = same && sameModulation (mq, ml) && sameModulation (mq, mn);
    }
    CHECK (same);
}

AP_TEST (life_r2_block_size_does_not_change_the_drift_clock)
{
    LifeModulator ref;
    setup (ref, 1.0f);
    const Series s512 = collect (ref, 20.0, 512);

    // 256-sample blocks (within 2× of nominal): the time accumulator advances the
    // drift every second block, landing exactly on the 512-block series.
    LifeModulator half;
    setup (half, 1.0f);
    const Series s256 = collect (half, 20.0, 256);
    bool aligned = true;
    for (size_t i = 0; i < s512.raw.size(); ++i)
        aligned = aligned && sameModulation (s256.raw[2 * i + 1], s512.raw[i]);
    CHECK (aligned);

    // Jittery host blocks around the nominal size keep the same clock: after
    // ~60 s of 448/576 alternation (5624 × 512 samples) the drift sits exactly
    // where the 512-sample series does at the same time.
    LifeModulator jitter;
    setup (jitter, 1.0f);
    Modulation last;
    for (int b = 0; b < 5624; ++b)
        last = jitter.analyze (nullptr, nullptr, (b & 1) ? 576 : 448);
    LifeModulator ref60;
    setup (ref60, 1.0f);
    const Series s60 = collect (ref60);
    CHECK (sameModulation (last, s60.raw[5623]));

    // Much smaller blocks (> 2× off): the control rate is re-derived, and the
    // drift keeps its time scale (1 s autocorrelation still high, still bounded).
    LifeModulator tiny;
    setup (tiny, 1.0f);
    const Series s64 = collect (tiny, kSeconds, 64);
    CHECK_EQ (tiny.getControlStepSamples(), 64);
    const size_t oneSecond64 = (size_t) (fsig::kSampleRate / 64.0);
    const double ac64 = autocorr (s64.eqFreqMul, oneSecond64);
    std::printf ("    64-sample blocks: 1 s autocorrelation %.3f, max block step %.6f\n", ac64, (double) maxStep (s64.eqFreqMul));
    CHECK (ac64 > 0.35);
    CHECK (maxStep (s64.eqFreqMul) <= 0.002f);
    CHECK (minOf (s64.eqFreqMul) >= 0.979f && maxOf (s64.eqFreqMul) <= 1.0215f);
}

AP_TEST (life_amount_and_rate_changes_never_jump)
{
    LifeModulator life;
    setup (life, 0.0f);
    std::vector<float> eq, gl;
    float act = 0.0f;
    for (int b = 0; b < 3000; ++b)
    {
        // 0 → 1 → 0.3 → 0 with rate and seed changes along the way.
        if (b == 200)  life.setAmount (1.0f);
        if (b == 900)  life.setRate (2.0f);
        if (b == 1400) life.setSeed (12345u);
        if (b == 1800) life.setAmount (0.3f);
        if (b == 2200) life.setRate (0.5f);
        if (b == 2600) life.setAmount (0.0f);
        const Modulation m = life.analyze (nullptr, nullptr, kBlock);
        eq.push_back (m.eqFreqMul);
        gl.push_back (m.gainL);
        if (b == 2999) CHECK (isIdentity (m));    // back to exact identity
        if (b == 1000) act = life.activity();
    }
    CHECK (act > 0.0f && act <= 1.0f);
    CHECK_EQ (life.activity(), 0.0f);
    CHECK (maxStep (eq) <= 0.002f);
    CHECK (maxStep (gl) <= 0.0015f);              // < 0.013 dB of trim per block
    CHECK_EQ (life.getSeed(), 12345u);
}

AP_TEST (life_design_holds_across_seeds)
{
    // The PRD statistics are measured on a 60 s window of a source whose slowest
    // row only updates every 32 s, so any single seed is a noisy sample. Check the
    // design, not just the default seed: across 24 seeds the bounds and step
    // limits hold for every one, and the slow/independent/rate behaviour holds
    // on average with clear margin.
    double acSum = 0.0, corrSum = 0.0, fastSum = 0.0, slowSum = 0.0;
    float worstStep = 0.0f, lo = 2.0f, hi = 0.0f;
    double worstSd = 1.0;
    constexpr int kSeeds = 24;
    for (int k = 0; k < kSeeds; ++k)
    {
        const uint32_t seed = 0x1000u + 7919u * (uint32_t) k;
        LifeModulator life;
        setup (life, 1.0f, 1.0f, seed);
        const Series s = collect (life);
        acSum += autocorr (s.eqFreqMul, kOneSecond);
        corrSum += pearson (s.gainL, s.gainR);
        worstStep = std::max (worstStep, maxStep (s.eqFreqMul));
        worstSd = std::min (worstSd, stdDev (s.eqFreqMul));
        lo = std::min (lo, minOf (s.eqFreqMul));
        hi = std::max (hi, maxOf (s.eqFreqMul));

        LifeModulator fast;
        setup (fast, 1.0f, 2.0f, seed);
        LifeModulator slow;
        setup (slow, 1.0f, 0.5f, seed);
        fastSum += autocorr (collect (fast).eqFreqMul, kOneSecond);
        slowSum += autocorr (collect (slow).eqFreqMul, kOneSecond);
    }
    std::printf ("    %d seeds: mean 1 s ac %.3f (rate 2 %.3f, rate 0.5 %.3f), mean L/R corr %.3f, min sd %.5f, worst step %.5f\n",
                 kSeeds, acSum / kSeeds, fastSum / kSeeds, slowSum / kSeeds, corrSum / kSeeds, worstSd, (double) worstStep);
    CHECK (lo >= 0.979f && hi <= 1.0215f);
    CHECK (worstStep <= 0.002f);
    CHECK (worstSd > 0.003);
    CHECK (acSum / kSeeds > 0.45);
    CHECK (std::fabs (corrSum / kSeeds) < 0.2);
    CHECK (fastSum / kSeeds + 0.1 < slowSum / kSeeds);
}
