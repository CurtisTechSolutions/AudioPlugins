#include "TestHarness.h"
#include "TestSignals.h"

#include "modules/Space.h"

#include <ap/Math.h>

#include <cmath>
#include <vector>

using namespace fsig;

namespace
{
    constexpr size_t kLen    = 96000;   // 2 s at 48 kHz
    constexpr size_t kSettle = 12000;   // skip the 250 ms engage ramp and filter warm-up

    std::vector<float> sideOf (const Stereo& s)
    {
        std::vector<float> d (s.size());
        for (size_t i = 0; i < s.size(); ++i) d[i] = s.l[i] - s.r[i];
        return d;
    }

    std::vector<float> sumOf (const Stereo& s)
    {
        std::vector<float> d (s.size());
        for (size_t i = 0; i < s.size(); ++i) d[i] = s.l[i] + s.r[i];
        return d;
    }

    double ratioDb (double a, double b) { return 20.0 * std::log10 (a / b); }

    double bandGainDb (const std::vector<float>& out, const std::vector<float>& in, double lo, double hi)
    {
        return ratioDb (bandRms (out, lo, hi, kSettle), bandRms (in, lo, hi, kSettle));
    }

    /** Prepares `m` with every setting applied and all smoothers snapped. */
    void setup (frisson::SpaceModule& m, float amount, float room, float widthMul = 1.0f, float monoHz = 120.0f)
    {
        m.prepare (kSampleRate, 256);
        m.setRoom (room);
        m.setMonoBelowHz (monoHz);
        frisson::Modulation mod;
        mod.widthMul = widthMul;
        m.setModulation (mod);
        m.setAmount (amount);
        m.reset();
    }

    /** Pure-side stereo pair: L = x, R = -x. */
    Stereo pureSide (const std::vector<float>& x)
    {
        std::vector<float> neg (x.size());
        for (size_t i = 0; i < x.size(); ++i) neg[i] = -x[i];
        return stereo (x, neg);
    }
}

//==============================================================================
AP_TEST (space_t1_amount_zero_is_exact_bypass)
{
    const Stereo in = stereoPink (kLen);

    frisson::SpaceModule m;
    setup (m, 0.0f, 0.3f, 2.0f);
    const Stereo out = run (m, in);
    CHECK_EQ (maxDiff (out.l, in.l), 0.0f);
    CHECK_EQ (maxDiff (out.r, in.r), 0.0f);
    CHECK_NEAR (m.activity(), 0.0, 1e-9);

    // Engage, then turn back down: once the ramp has settled the bypass is exact again.
    m.setAmount (1.0f);
    (void) run (m, in);
    m.setAmount (0.0f);
    (void) run (m, in);                               // 2 s >> the 20 ms amount ramp
    const Stereo again = run (m, in);
    CHECK_EQ (maxDiff (again.l, in.l), 0.0f);
    CHECK_EQ (maxDiff (again.r, in.r), 0.0f);
    CHECK_NEAR (m.activity(), 0.0, 1e-9);
}

AP_TEST (space_t2_dual_mono_without_room_passes_unchanged)
{
    const Stereo in = dualMono (pink (kLen));

    frisson::SpaceModule m;
    setup (m, 1.0f, 0.0f, 2.0f);
    const Stereo out = run (m, in);
    CHECK (maxDiff (out.l, in.l) < 1e-5f);
    CHECK (maxDiff (out.r, in.r) < 1e-5f);

    // Also through the engage ramp (amount set after prepare, ramps from 0).
    frisson::SpaceModule ramped;
    ramped.prepare (kSampleRate, 256);
    ramped.setRoom (0.0f);
    ramped.setAmount (1.0f);
    const Stereo out2 = run (ramped, in);
    CHECK (maxDiff (out2.l, in.l) < 1e-5f);
    CHECK (maxDiff (out2.r, in.r) < 1e-5f);
}

AP_TEST (space_t3_widens_above_split_only)
{
    const Stereo in = stereoPink (kLen);

    frisson::SpaceModule m;
    setup (m, 1.0f, 0.0f);
    const Stereo out = run (m, in);

    const auto sIn = sideOf (in), sOut = sideOf (out);
    const double highDb = bandGainDb (sOut, sIn, 1000.0, 8000.0);
    const double lowDb  = bandGainDb (sOut, sIn, 30.0, 80.0);
    std::printf ("    side 1-8 kHz %+.2f dB, side 30-80 Hz %+.2f dB\n", highDb, lowDb);
    CHECK_NEAR (highDb, 4.5, 1.0);
    CHECK_NEAR (lowDb, 0.0, 0.5);

    const double sumDb = ratioDb (rms (sumOf (out), kSettle), rms (sumOf (in), kSettle));
    CHECK_NEAR (sumDb, 0.0, 0.3);

    // Linear, so level independent: the same gain at -30 dB.
    const Stereo quiet = stereoPink (kLen, 0.2f * ap::dbToGain (-30.0f));
    frisson::SpaceModule mq;
    setup (mq, 1.0f, 0.0f);
    const Stereo quietOut = run (mq, quiet);
    CHECK_NEAR (bandGainDb (sideOf (quietOut), sideOf (quiet), 1000.0, 8000.0), highDb, 0.01);
}

AP_TEST (space_t4_width_mul_widens_further)
{
    const Stereo in = stereoPink (kLen);
    const auto sIn = sideOf (in);

    frisson::SpaceModule m1;
    setup (m1, 1.0f, 0.0f, 1.0f);
    frisson::SpaceModule m15;
    setup (m15, 1.0f, 0.0f, 1.5f);
    const double g1  = bandGainDb (sideOf (run (m1, in)),  sIn, 1000.0, 8000.0);
    const double g15 = bandGainDb (sideOf (run (m15, in)), sIn, 1000.0, 8000.0);
    std::printf ("    widthMul 1: %+.2f dB, widthMul 1.5: %+.2f dB\n", g1, g15);
    CHECK (g15 > g1 + 1.0);

    // Activity follows the effective width: 1 + 0.8 x 1.5 = 2.2 -> full scale.
    CHECK_NEAR (m1.activity(), 0.8 / 1.2, 1e-3);
    CHECK_NEAR (m15.activity(), 1.0, 1e-3);
}

AP_TEST (space_t5_room_taps_are_early_quiet_and_decorrelated)
{
    constexpr size_t n0 = 1000;
    std::vector<float> click (4096, 0.0f);
    click[n0] = 1.0f;
    const Stereo in = dualMono (click);

    frisson::SpaceModule m;
    setup (m, 1.0f, 1.0f);
    const Stereo out = run (m, in);

    const size_t tapL = (size_t) std::lround (0.0113 * kSampleRate);   // 542
    const size_t tapR = (size_t) std::lround (0.0137 * kSampleRate);   // 658

    auto windowPeak = [] (const std::vector<float>& v, size_t centre)
    {
        float p = 0.0f;
        for (size_t i = centre - 1; i <= centre + 1; ++i) p = std::max (p, std::fabs (v[i]));
        return p;
    };
    auto rangePeak = [] (const std::vector<float>& v, size_t from, size_t to)
    {
        float p = 0.0f;
        for (size_t i = from; i < to; ++i) p = std::max (p, std::fabs (v[i]));
        return p;
    };

    // Direct path untouched, nothing between it and the first reflection.
    CHECK_NEAR (out.l[n0], 1.0, 1e-6);
    CHECK_NEAR (out.r[n0], 1.0, 1e-6);
    CHECK (rangePeak (out.l, n0 + 1, n0 + tapL - 1) < 1e-9f);
    CHECK (rangePeak (out.r, n0 + 1, n0 + tapR - 1) < 1e-9f);

    const float pl = windowPeak (out.l, n0 + tapL);
    const float pr = windowPeak (out.r, n0 + tapR);
    std::printf ("    first reflection L %.1f dB, R %.1f dB\n", ap::gainToDb (pl), ap::gainToDb (pr));
    CHECK (pl > 0.0f);
    CHECK (pr > 0.0f);
    CHECK (ap::gainToDb (rangePeak (out.l, n0 + 1, out.size())) <= -17.0f);
    CHECK (ap::gainToDb (rangePeak (out.r, n0 + 1, out.size())) <= -17.0f);

    const double corr = correlation (out.l, out.r, n0 + 1);
    std::printf ("    tail correlation %.3f\n", corr);
    CHECK (corr < 0.9);
}

AP_TEST (space_t6_mono_compatible_with_room)
{
    const Stereo in = stereoPink (kLen);

    frisson::SpaceModule m;
    setup (m, 1.0f, 1.0f);
    const Stereo out = run (m, in);

    const double sumDb = ratioDb (rms (sumOf (out), kSettle), rms (sumOf (in), kSettle));
    std::printf ("    RMS(L+R) change %+.2f dB\n", sumDb);
    CHECK_NEAR (sumDb, 0.0, 1.0);

    // Maximum CHILLS width as well: width never touches M.
    frisson::SpaceModule mw;
    setup (mw, 1.0f, 1.0f, 2.0f);
    const Stereo outW = run (mw, in);
    CHECK_NEAR (ratioDb (rms (sumOf (outW), kSettle), rms (sumOf (in), kSettle)), 0.0, 1.0);

    // Without room the fold-down is exact for any input.
    frisson::SpaceModule m0;
    setup (m0, 1.0f, 0.0f, 2.0f);
    const Stereo out0 = run (m0, in);
    CHECK (maxDiff (sumOf (out0), sumOf (in)) < 1e-5f);

    // Narrowband worst case: the four taps comb-filter the mono sum. Measure the
    // L+R magnitude response 20 Hz..20 kHz from a dual-mono impulse response.
    auto foldDownRangeDb = [] (float room, double& lo, double& hi)
    {
        constexpr size_t n = 8192;
        std::vector<float> click (n, 0.0f);
        click[0] = 1.0f;
        frisson::SpaceModule s;
        setup (s, 1.0f, room);
        const Stereo ir = run (s, dualMono (click));
        const auto sum = sumOf (ir);
        lo = 1e9; hi = -1e9;
        for (double hz = 20.0; hz < 20000.0; hz *= 1.002)
        {
            const double mag = toneLevel (sum, hz) * (double) n * 0.5 / 2.0;   // |H|, re the input's L+R
            lo = std::min (lo, 20.0 * std::log10 (mag));
            hi = std::max (hi, 20.0 * std::log10 (mag));
        }
    };
    double lo03, hi03, lo1, hi1;
    foldDownRangeDb (0.3f, lo03, hi03);
    foldDownRangeDb (1.0f, lo1, hi1);
    std::printf ("    tone fold-down range: room 0.3 %+.2f..%+.2f dB, room 1 %+.2f..%+.2f dB\n", lo03, hi03, lo1, hi1);
    CHECK (lo03 > -1.0 && hi03 < 1.0);                // default room: R5 holds for any tone
    CHECK (lo1 > -2.5 && hi1 < 2.0);                  // room 1: comb bound 20 log10 (1 +- 2 x 10^(-18/20))
}

AP_TEST (space_t7_mono_below_moves_the_split)
{
    std::vector<float> x = sine (200.0, kLen, 0.25f);
    const std::vector<float> hi = sine (1000.0, kLen, 0.25f);
    for (size_t i = 0; i < x.size(); ++i) x[i] += hi[i];
    const Stereo in = pureSide (x);
    const auto sIn = sideOf (in);

    frisson::SpaceModule m;
    setup (m, 1.0f, 0.0f, 1.0f, 300.0f);
    const auto sOut = sideOf (run (m, in));

    const double at200 = ratioDb (toneLevel (sOut, 200.0, kSettle), toneLevel (sIn, 200.0, kSettle));
    const double at1k  = ratioDb (toneLevel (sOut, 1000.0, kSettle), toneLevel (sIn, 1000.0, kSettle));
    std::printf ("    side @200 Hz %+.3f dB, @1 kHz %+.2f dB\n", at200, at1k);
    // Tight: R2 alone predicts |1 + 0.8 x LR4_HP(200 Hz / 300 Hz)| = -0.508 dB; the
    // R3 side shelf's phase at 200 Hz brings it to -0.499 dB.
    CHECK_NEAR (at200, 0.0, 0.5);
    CHECK (at1k > 3.0);

    // The split really moved: with the default 120 Hz split the same 200 Hz side is
    // in the widened band (+0.46 dB predicted) and 600 Hz is far above it.
    frisson::SpaceModule m120;
    setup (m120, 1.0f, 0.0f, 1.0f, 120.0f);
    const auto sOut120 = sideOf (run (m120, in));
    const double at200w = ratioDb (toneLevel (sOut120, 200.0, kSettle), toneLevel (sIn, 200.0, kSettle));
    std::printf ("    split 120 Hz: side @200 Hz %+.3f dB\n", at200w);
    CHECK (at200w > at200 + 0.8);
}

AP_TEST (space_t8_no_latency_and_finite)
{
    frisson::SpaceModule m;
    m.prepare (kSampleRate, 256);
    CHECK_EQ (m.latencySamples(), 0);
    m.setAmount (1.0f);
    CHECK_EQ (m.latencySamples(), 0);

    // Extremes: full-scale decorrelated noise, max width, max room, both split limits,
    // parameters moving mid-stream, odd block sizes, then silence.
    frisson::Modulation mod;
    mod.widthMul = 5.0f;                               // clamped to 2, width clamped to 2.2
    m.setModulation (mod);
    m.setRoom (1.0f);
    m.setMonoBelowHz (10.0f);                          // clamped to 60
    CHECK_NEAR (m.getMonoBelowHz(), 60.0, 1e-6);
    Stereo loud = stereoPink (kLen / 2, 1.0f);
    Stereo out = run (m, loud, 97);
    CHECK (allFinite (out.l) && allFinite (out.r));

    m.setMonoBelowHz (1000.0f);                        // clamped to 300
    CHECK_NEAR (m.getMonoBelowHz(), 300.0, 1e-6);
    m.setRoom (0.0f);
    mod.widthMul = 0.0f;                               // clamped to 0.5
    m.setModulation (mod);
    out = run (m, loud, 1);
    CHECK (allFinite (out.l) && allFinite (out.r));

    const Stereo silent = run (m, Stereo (kLen / 4), 512);
    CHECK (allFinite (silent.l) && allFinite (silent.r));
    CHECK (peak (silent.l, kLen / 8) < 1e-6f);

    // A mono host passing one buffer for both sides stays finite and unchanged without room.
    std::vector<float> mono = pink (8192);
    const std::vector<float> monoIn = mono;
    m.reset();
    m.process (mono.data(), mono.data(), (int) mono.size());
    CHECK (allFinite (mono));
    CHECK (maxDiff (mono, monoIn) < 1e-5f);
    CHECK_EQ (m.latencySamples(), 0);
}
