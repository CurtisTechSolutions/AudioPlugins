// WARMTH acceptance tests (docs/prd/frisson-warmth.md, section 5).

#include "TestHarness.h"
#include "TestSignals.h"

#include "modules/Warmth.h"

#include <cmath>
#include <vector>

namespace
{
    constexpr double kSr = fsig::kSampleRate;
    constexpr size_t kSecond = (size_t) kSr;

    void prepareWarmth (frisson::WarmthModule& w, float amount)
    {
        w.prepare (kSr, 256);
        w.setAmount (amount);
        w.reset();
    }

    double db (double ratio) { return 20.0 * std::log10 (std::max (ratio, 1.0e-12)); }

    std::vector<float> slice (const std::vector<float>& v, size_t from, size_t to)
    {
        return std::vector<float> (v.begin() + (std::ptrdiff_t) from, v.begin() + (std::ptrdiff_t) to);
    }

    /** 2nd harmonic of a 200 Hz sine relative to the fundamental, dB, after a 1 s settle. */
    struct Harmonics { double h2Db, h3Db, h2Abs; };

    Harmonics measureHarmonics (float amount, float amp, float driveMul = 1.0f)
    {
        frisson::WarmthModule w;
        prepareWarmth (w, amount);
        frisson::Modulation m;
        m.driveMul = driveMul;
        w.setModulation (m);
        const auto out = fsig::run (w, fsig::dualMono (fsig::sine (200.0, 2 * kSecond, amp)));
        const double h1 = fsig::toneLevel (out.l, 200.0, kSecond);
        const double h2 = fsig::toneLevel (out.l, 400.0, kSecond);
        const double h3 = fsig::toneLevel (out.l, 600.0, kSecond);
        return { db (h2 / h1), db (h3 / h1), h2 };
    }
}

AP_TEST (warmth_t1_amount_zero_is_exact_bypass)
{
    const auto in = fsig::stereoPink (2 * kSecond);

    frisson::WarmthModule w;
    prepareWarmth (w, 0.0f);
    const auto out = fsig::run (w, in);
    CHECK (fsig::maxDiff (in.l, out.l) == 0.0f);
    CHECK (fsig::maxDiff (in.r, out.r) == 0.0f);

    // Turned up and back down to 0: after the 30 ms fade it is bit-exact again.
    frisson::WarmthModule w2;
    prepareWarmth (w2, 0.7f);
    fsig::run (w2, in);
    w2.setAmount (0.0f);
    fsig::run (w2, fsig::Stereo (kSecond / 10));
    const auto out2 = fsig::run (w2, in);
    CHECK (fsig::maxDiff (in.l, out2.l) == 0.0f);
    CHECK (fsig::maxDiff (in.r, out2.r) == 0.0f);
    CHECK (w2.activity() == 0.0f);
}

AP_TEST (warmth_t2_even_harmonic_on_200hz_sine)
{
    const float amp = ap::dbToGain (-6.0f);
    const auto full = measureHarmonics (1.0f, amp);
    const auto low  = measureHarmonics (0.3f, amp);
    std::printf ("    amount 1: h2 %.1f dB, h3 %.1f dB re 200 Hz; amount 0.3: h2 %.1f dB\n", full.h2Db, full.h3Db, low.h2Db);

    CHECK (full.h2Db >= -35.0);
    CHECK (full.h2Db > full.h3Db);
    CHECK (low.h2Abs < full.h2Abs);

    // R3: 2nd above 3rd at every amount (and with full drive modulation).
    for (float a : { 0.05f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f })
    {
        const auto h = measureHarmonics (a, amp);
        CHECK (h.h2Db > h.h3Db);
    }
    const auto maxDrive = measureHarmonics (1.0f, amp, 2.0f);
    CHECK (maxDrive.h2Db > maxDrive.h3Db);

    // R7: the colour does not depend on the input level.
    const auto quiet = measureHarmonics (1.0f, ap::dbToGain (-30.0f));
    CHECK_NEAR (quiet.h2Db, full.h2Db, 1.0);
}

AP_TEST (warmth_t3_no_dc_on_200hz_sine)
{
    frisson::WarmthModule w;
    prepareWarmth (w, 1.0f);
    const auto out = fsig::run (w, fsig::dualMono (fsig::sine (200.0, 3 * kSecond, ap::dbToGain (-6.0f))));
    const double dc = fsig::mean (out.l, kSecond);
    std::printf ("    mean %.2e\n", dc);
    CHECK (std::fabs (dc) < 1.0e-3);
    CHECK (std::fabs (fsig::mean (out.r, kSecond)) < 1.0e-3);
}

AP_TEST (warmth_t4_band_level_is_preserved)
{
    const auto in = fsig::stereoPink (3 * kSecond);
    const double inBand = fsig::bandRms (in.l, 80.0, 800.0, kSecond);

    for (float amount : { 0.5f, 1.0f })
    {
        frisson::WarmthModule w;
        prepareWarmth (w, amount);
        const auto out = fsig::run (w, in);
        const double diffL = db (fsig::bandRms (out.l, 80.0, 800.0, kSecond) / inBand);
        const double diffR = db (fsig::bandRms (out.r, 80.0, 800.0, kSecond) / fsig::bandRms (in.r, 80.0, 800.0, kSecond));
        std::printf ("    amount %.1f: band level %+.2f dB (L), %+.2f dB (R)\n", (double) amount, diffL, diffR);
        CHECK_NEAR (diffL, 0.0, 1.5);
        CHECK_NEAR (diffR, 0.0, 1.5);
    }
}

AP_TEST (warmth_t5_outside_bands_untouched)
{
    const auto in = fsig::stereoPink (3 * kSecond);
    frisson::WarmthModule w;
    prepareWarmth (w, 1.0f);
    const auto out = fsig::run (w, in);

    for (const auto* ch : { &in.l, &in.r })
    {
        const auto& x = *ch;
        const auto& y = ch == &in.l ? out.l : out.r;
        const double lowDiff  = db (fsig::bandRms (y, 30.0, 60.0, kSecond) / fsig::bandRms (x, 30.0, 60.0, kSecond));
        const double highDiff = db (fsig::bandRms (y, 1500.0, 10000.0, kSecond) / fsig::bandRms (x, 1500.0, 10000.0, kSecond));
        std::printf ("    30-60 Hz %+.3f dB, 1.5-10 kHz %+.3f dB\n", lowDiff, highDiff);
        CHECK_NEAR (lowDiff, 0.0, 0.3);
        CHECK_NEAR (highDiff, 0.0, 0.3);
    }
}

AP_TEST (warmth_t6_glue_evens_out_a_loud_section)
{
    // 2 s quiet, 500 ms 10 dB louder, 2 s quiet.
    const size_t loudFrom = 2 * kSecond, loudTo = loudFrom + kSecond / 2, n = loudTo + 2 * kSecond;
    auto in = fsig::stereoPink (n, 0.1f);
    const float boost = ap::dbToGain (10.0f);
    for (size_t i = loudFrom; i < loudTo; ++i)
    {
        in.l[i] *= boost;
        in.r[i] *= boost;
    }

    const size_t blockSize = 256;
    struct Result { double ratioDb; float minLoudActivity, maxTailActivity; };

    auto measure = [&] (float amount)
    {
        frisson::WarmthModule w;
        prepareWarmth (w, amount);
        auto out = in;
        Result r { 0.0, 1.0f, 0.0f };
        for (size_t pos = 0; pos < n; pos += blockSize)
        {
            const int len = (int) std::min (blockSize, n - pos);
            w.process (out.l.data() + pos, out.r.data() + pos, len);
            const size_t end = pos + (size_t) len;
            // Loud: once the 30 ms attack (60 ms RMS window) has acted. Quiet tail: its
            // last second, after the 150 ms detector and meter releases have settled.
            if (end > loudFrom + kSecond / 10 && end <= loudTo)
                r.minLoudActivity = std::min (r.minLoudActivity, w.activity());
            if (pos >= n - kSecond)
                r.maxTailActivity = std::max (r.maxTailActivity, w.activity());
        }
        const auto loud  = slice (out.l, loudFrom, loudTo);
        const auto quiet = slice (out.l, loudFrom - kSecond / 2, loudFrom);
        r.ratioDb = db (fsig::bandRms (loud, 80.0, 800.0) / fsig::bandRms (quiet, 80.0, 800.0));
        return r;
    };

    const auto dry = measure (0.0f);
    const auto wet = measure (1.0f);
    std::printf ("    loud/quiet band ratio: %.2f dB at amount 0, %.2f dB at amount 1; activity loud min %.2f, tail max %.3f\n",
                 dry.ratioDb, wet.ratioDb, (double) wet.minLoudActivity, (double) wet.maxTailActivity);

    CHECK (dry.ratioDb - wet.ratioDb >= 1.5);
    CHECK (wet.minLoudActivity > 0.2f);
    CHECK (wet.maxTailActivity < 0.05f);
}

AP_TEST (warmth_t7_drive_modulation_adds_harmonics)
{
    const float amp = ap::dbToGain (-6.0f);
    const auto normal  = measureHarmonics (0.5f, amp, 1.0f);
    const auto boosted = measureHarmonics (0.5f, amp, 2.0f);
    std::printf ("    400 Hz: %.1f dB (driveMul 1), %.1f dB (driveMul 2)\n", normal.h2Db, boosted.h2Db);
    CHECK (boosted.h2Abs > normal.h2Abs);
}

AP_TEST (warmth_t8_no_latency_and_finite_output)
{
    frisson::WarmthModule w;
    prepareWarmth (w, 0.0f);
    CHECK_EQ (w.latencySamples(), 0);
    w.setAmount (1.0f);
    CHECK_EQ (w.latencySamples(), 0);

    // Every kind of input, with the knob and the modulation moving between blocks.
    std::vector<float> l, r;
    for (const auto& part : { fsig::pink (kSecond, 0.2f, 7u), std::vector<float> (kSecond / 2, 0.0f),
                              fsig::sine (200.0, kSecond / 2, 4.0f), fsig::harmonic (110.0, kSecond / 2, 8, 0.5f),
                              fsig::white (kSecond / 2, 1.0e-6f), std::vector<float> (kSecond / 4, 1.0f) })
    {
        l.insert (l.end(), part.begin(), part.end());
        r.insert (r.end(), part.rbegin(), part.rend());
    }

    const int blockSize = 173;
    int block = 0;
    for (size_t pos = 0; pos < l.size(); pos += (size_t) blockSize, ++block)
    {
        w.setAmount (0.5f + 0.5f * (float) std::sin (block * 0.05));
        frisson::Modulation m;
        m.driveMul  = 0.25f + 2.0f * (float) (block % 7) / 6.0f;
        m.eqFreqMul = 0.7f + 0.1f * (float) (block % 7);
        w.setModulation (m);
        const int len = (int) std::min ((size_t) blockSize, l.size() - pos);
        w.process (l.data() + pos, r.data() + pos, len);
        CHECK (std::isfinite (w.activity()) && w.activity() >= 0.0f && w.activity() <= 1.0f);
    }
    CHECK (fsig::allFinite (l));
    CHECK (fsig::allFinite (r));

    // Mono host: right == nullptr processes the left channel alone.
    auto mono = fsig::pink (kSecond);
    w.process (mono.data(), nullptr, (int) mono.size());
    CHECK (fsig::allFinite (mono));
}
