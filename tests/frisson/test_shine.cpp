// SHINE acceptance tests (docs/prd/frisson-shine.md, section 5).

#include "TestHarness.h"
#include "TestSignals.h"

#include "modules/Shine.h"

#include <ap/Filters.h>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace fsig;

namespace
{
    constexpr size_t kOneSec = 48000;
    constexpr size_t kSettle = 12000;    // skip 0.25 s: amount ramp and filter settling

    const float kMinus12dB = (float) std::pow (10.0, -12.0 / 20.0);

    double toDb (double ratio) { return 20.0 * std::log10 (std::max (1.0e-12, ratio)); }

    std::vector<float> add (std::vector<float> a, const std::vector<float>& b)
    {
        for (size_t i = 0; i < a.size(); ++i) a[i] += b[i];
        return a;
    }

    /** A freshly prepared module at `amount` with airDb / eqFreqMul applied. */
    struct Fixture
    {
        frisson::ShineModule m;

        explicit Fixture (float amount, float airDb = 0.0f, float eqFreqMul = 1.0f)
        {
            m.prepare (kSampleRate, 256);
            frisson::Modulation mod;
            mod.airDb = airDb;
            mod.eqFreqMul = eqFreqMul;
            m.setModulation (mod);
            m.setAmount (amount);
        }
    };

    /** Gain in dB of the `hz` component, output vs input (left channel), after settling. */
    double toneGainDb (const Stereo& in, const Stereo& out, double hz)
    {
        return toDb (toneLevel (out.l, hz, kSettle) / toneLevel (in.l, hz, kSettle));
    }

    /** Level of the 14 kHz exciter harmonic relative to the 7 kHz fundamental at the output, dB. */
    double harmonicReDb (float amount, float sineAmp)
    {
        Fixture f (amount);
        const auto in = dualMono (sine (7000.0, kOneSec, sineAmp));
        const auto out = run (f.m, in);
        return toDb (toneLevel (out.l, 14000.0, kSettle) / toneLevel (out.l, 7000.0, kSettle));
    }
}

//==============================================================================
AP_TEST (shine_t1_amount_zero_is_exact_bypass)
{
    const auto in = stereoPink (kOneSec);

    {
        Fixture f (0.0f);
        const auto out = run (f.m, in);
        CHECK (maxDiff (out.l, in.l) == 0.0f);
        CHECK (maxDiff (out.r, in.r) == 0.0f);
        CHECK (f.m.activity() == 0.0f);
    }

    {
        // airDb (and eqFreqMul) are ignored at amount 0.
        Fixture f (0.0f, 4.0f, 0.8f);
        const auto out = run (f.m, in);
        CHECK (maxDiff (out.l, in.l) == 0.0f);
        CHECK (maxDiff (out.r, in.r) == 0.0f);
    }

    {
        // Turning the knob down from 1 ramps out, then is bit-exact again.
        Fixture f (1.0f, 4.0f);
        auto wet = run (f.m, in);
        CHECK (maxDiff (wet.l, in.l) > 1.0e-3f);   // it was doing something

        f.m.setAmount (0.0f);
        (void) run (f.m, in);                      // 1 s: the 20 ms ramp is long finished
        const auto out = run (f.m, in);
        CHECK (maxDiff (out.l, in.l) == 0.0f);
        CHECK (maxDiff (out.r, in.r) == 0.0f);
        CHECK (f.m.activity() == 0.0f);
    }
}

AP_TEST (shine_t2_exciter_adds_second_harmonic)
{
    const double full = harmonicReDb (1.0f, kMinus12dB);
    const double low  = harmonicReDb (0.3f, kMinus12dB);
    std::printf ("    14 kHz re 7 kHz: amount 1 %.1f dB, amount 0.3 %.1f dB\n", full, low);

    CHECK (full >= -40.0);
    CHECK (low < full);

    // Level independent (umbrella R6): the same harmonic ratio at -30 dBFS.
    const double quiet = harmonicReDb (1.0f, (float) std::pow (10.0, -30.0 / 20.0));
    CHECK_NEAR (quiet, full, 1.0);

    // R6: activity reflects the exciter path, and is 0 when it is silent.
    Fixture f (1.0f);
    (void) run (f.m, dualMono (sine (7000.0, kOneSec, kMinus12dB)));
    const float actFull = f.m.activity();
    Fixture g (0.3f);
    (void) run (g.m, dualMono (sine (7000.0, kOneSec, kMinus12dB)));
    Fixture bass (1.0f);
    (void) run (bass.m, dualMono (sine (100.0, kOneSec, 0.5f)));
    std::printf ("    activity: 7 kHz amount 1 %.2f, amount 0.3 %.2f; 100 Hz only %.2f\n",
                 actFull, g.m.activity(), bass.m.activity());

    CHECK (actFull > 0.3f && actFull <= 1.0f);
    CHECK (g.m.activity() < actFull);
    CHECK (bass.m.activity() == 0.0f);
}

AP_TEST (shine_t3_air_shelf_boost)
{
    const auto in = dualMono (sine (12000.0, kOneSec, 0.25f));

    double gains[3] = {};
    const float airs[3] = { 0.0f, 4.0f, 10.0f };
    for (int k = 0; k < 3; ++k)
    {
        Fixture f (1.0f, airs[k]);
        const auto out = run (f.m, in);
        gains[k] = toneGainDb (in, out, 12000.0);
        CHECK (maxDiff (out.l, out.r) == 0.0f);   // dual mono stays dual mono
    }
    std::printf ("    12 kHz boost: airDb 0 %+.2f dB, airDb 4 %+.2f dB, airDb 10 %+.2f dB\n",
                 gains[0], gains[1], gains[2]);

    CHECK_NEAR (gains[0], 2.5, 0.5);
    CHECK (gains[1] >= 5.5);
    CHECK (gains[2] <= 7.0);
    CHECK_NEAR (gains[2], gains[1], 0.01);         // clamped to +4 dB
}

AP_TEST (shine_t4_below_3k_unchanged)
{
    {
        const auto in = dualMono (sine (1000.0, kOneSec, 0.5f));
        Fixture f (1.0f);
        const auto out = run (f.m, in);
        const double g = toneGainDb (in, out, 1000.0);
        std::printf ("    1 kHz sine: %+.3f dB\n", g);
        CHECK_NEAR (g, 0.0, 0.2);
    }

    // R4 "at any amount": also with the full air modulation on top.
    const auto in = stereoPink (2 * kOneSec);
    const double ref = bandRms (in.l, 100.0, 3000.0, kSettle);
    for (float air : { 0.0f, 4.0f })
    {
        for (float amount : { 0.3f, 1.0f })
        {
            Fixture f (amount, air);
            const auto out = run (f.m, in);
            const double d = toDb (bandRms (out.l, 100.0, 3000.0, kSettle) / ref);
            std::printf ("    pink 100-3000 Hz, amount %.1f airDb %.0f: %+.3f dB\n", amount, air, d);
            CHECK_NEAR (d, 0.0, 0.2);
        }
    }
}

AP_TEST (shine_t5_no_low_intermod)
{
    const auto in = dualMono (add (sine (7000.0, kOneSec, 0.25f), sine (7500.0, kOneSec, 0.25f)));
    Fixture f (1.0f);
    const auto out = run (f.m, in);

    const double imDbfs = toDb (toneLevel (out.l, 500.0, kSettle));
    const double sumDb  = toDb (toneLevel (out.l, 14500.0, kSettle));   // the intermod we do want
    std::printf ("    500 Hz difference tone %.1f dBFS (14.5 kHz sum tone %.1f dBFS)\n", imDbfs, sumDb);

    CHECK (imDbfs <= -60.0);
    CHECK (sumDb > imDbfs + 40.0);
}

AP_TEST (shine_t6_eqfreqmul_lowers_corner)
{
    const auto in = dualMono (sine (9000.0, kOneSec, 0.25f));

    Fixture nominal (1.0f, 0.0f, 1.0f);
    Fixture lowered (1.0f, 0.0f, 0.8f);
    const double gNominal = toneGainDb (in, run (nominal.m, in), 9000.0);
    const double gLowered = toneGainDb (in, run (lowered.m, in), 9000.0);
    std::printf ("    9 kHz boost: eqFreqMul 1.0 %+.2f dB, 0.8 %+.2f dB\n", gNominal, gLowered);

    CHECK (gLowered > gNominal + 0.2);
}

AP_TEST (shine_t7_latency_finite_silence)
{
    {
        Fixture f (1.0f, 4.0f);
        CHECK_EQ (f.m.latencySamples(), 0);

        // Hot, broadband material at full settings, with odd block sizes.
        const auto in = stereo (white (kOneSec, 1.0f, 7u), add (sine (11000.0, kOneSec, 0.9f), white (kOneSec, 0.1f, 8u)));
        const auto out = run (f.m, in, 117);
        CHECK (allFinite (out.l));
        CHECK (allFinite (out.r));
        CHECK (peak (out.l) < 4.0f);
        CHECK (f.m.activity() >= 0.0f && f.m.activity() <= 1.0f);
    }

    {
        Fixture f (1.0f, 4.0f);
        const Stereo silent (kOneSec);
        const auto out = run (f.m, silent);
        CHECK (peak (out.l) == 0.0f);
        CHECK (peak (out.r) == 0.0f);
        CHECK (f.m.activity() == 0.0f);
    }

    {
        // R5 smoothing: a 200 Hz tone is untouched by SHINE, so anything above
        // 2 kHz in (out - in) while amount / airDb / eqFreqMul jump around is a
        // parameter-change click.
        const auto in = dualMono (sine (200.0, 2 * kOneSec, 0.5f));
        frisson::ShineModule m;
        m.prepare (kSampleRate, 256);
        Stereo out = in;
        for (size_t pos = 0, blk = 0; pos < out.size(); pos += 256, ++blk)
        {
            if (blk % 20 == 5)  m.setAmount (1.0f);
            if (blk % 20 == 15) m.setAmount (0.0f);
            frisson::Modulation mod;
            mod.airDb     = (blk % 7 < 3)  ? 4.0f : 0.0f;
            mod.eqFreqMul = (blk % 11 < 5) ? 0.8f : 1.25f;
            m.setModulation (mod);
            m.process (out.l.data() + pos, out.r.data() + pos, 256);
        }

        ap::Biquad hp1, hp2;
        hp1.configure (ap::Biquad::Type::HighPass, kSampleRate, 2000.0f, 0.7071f);
        hp2 = hp1;
        float clickPeak = 0.0f;
        for (size_t i = 0; i < out.size(); ++i)
        {
            const float d = std::fabs (hp2.process (hp1.process (out.l[i] - in.l[i])));
            if (i > 2000) clickPeak = std::max (clickPeak, d);
        }
        const double clickDb = toDb (clickPeak / 0.5);
        std::printf ("    automation residue above 2 kHz: %.1f dB re tone\n", clickDb);
        CHECK (clickDb < -60.0);
    }

    {
        // Mono host: the same buffer as left and right.
        Fixture f (1.0f);
        auto buf = sine (7000.0, kOneSec, kMinus12dB);
        const auto ref = run (f.m, dualMono (buf));
        Fixture g (1.0f);
        for (size_t pos = 0; pos < buf.size(); pos += 256)
            g.m.process (buf.data() + pos, buf.data() + pos, (int) std::min ((size_t) 256, buf.size() - pos));
        CHECK (maxDiff (buf, ref.l) < 1.0e-6f);
    }
}
