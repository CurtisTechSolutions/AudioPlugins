#include "TestHarness.h"
#include "TestSignals.h"

#include "modules/Weight.h"

#include <cmath>
#include <vector>

using namespace fsig;

namespace
{
    constexpr size_t kLen    = 48000;   // 1 s
    // Skip the first 300 ms (filters, envelope, smoothers). 14400 samples is a whole
    // number of periods of every test tone, so means and Goertzel bins see no partial cycle.
    constexpr size_t kSettle = 14400;

    double db (double ratio) { return 20.0 * std::log10 (std::max (ratio, 1e-30)); }

    void setup (frisson::WeightModule& w, float amount, float monoHz = frisson::WeightModule::kMonoDefaultHz)
    {
        w.setAmount (amount);
        w.setMonoBelowHz (monoHz);
        w.prepare (kSampleRate, 256);
    }

    Stereo process (float amount, const Stereo& in, float monoHz = frisson::WeightModule::kMonoDefaultHz)
    {
        frisson::WeightModule w;
        setup (w, amount, monoHz);
        return run (w, in);
    }

    std::vector<float> sideOf (const Stereo& s)
    {
        std::vector<float> v (s.size());
        for (size_t i = 0; i < v.size(); ++i) v[i] = 0.5f * (s.l[i] - s.r[i]);
        return v;
    }

    std::vector<float> sumOf (const Stereo& s)
    {
        std::vector<float> v (s.size());
        for (size_t i = 0; i < v.size(); ++i) v[i] = s.l[i] + s.r[i];
        return v;
    }
}

AP_TEST (weight_t1_amount_zero_is_exact_bypass)
{
    const auto in = dualMono (pink (kLen));
    const auto out = process (0.0f, in);
    CHECK (maxDiff (out.l, in.l) == 0.0f);
    CHECK (maxDiff (out.r, in.r) == 0.0f);

    // Also bit-exact on decorrelated stereo, and after the knob was turned down to 0
    // (internal fade-out finishes, then the buffer is left untouched).
    const auto st = stereoPink (kLen);
    CHECK (maxDiff (process (0.0f, st).l, st.l) == 0.0f);

    frisson::WeightModule w;
    setup (w, 1.0f);
    run (w, st);
    w.setAmount (0.0f);
    run (w, st);                       // fades out within this pass
    const auto after = run (w, st);
    CHECK (maxDiff (after.l, st.l) == 0.0f);
    CHECK (maxDiff (after.r, st.r) == 0.0f);
}

AP_TEST (weight_t2_generates_second_and_third_harmonics)
{
    const auto in = dualMono (sine (50.0, kLen, 0.3f));
    const auto full = process (1.0f, in);
    const auto half = process (0.5f, in);

    const double f1 = toneLevel (full.l, 50.0, kSettle);
    const double h2 = toneLevel (full.l, 100.0, kSettle);
    const double h3 = toneLevel (full.l, 150.0, kSettle);
    std::printf ("    50 Hz amp 0.3, amount 1: 100 Hz %.1f dB, 150 Hz %.1f dB re 50 Hz\n", db (h2 / f1), db (h3 / f1));
    CHECK (db (h2 / f1) >= -30.0);
    CHECK (db (h3 / f1) >= -40.0);

    const double h2Half = toneLevel (half.l, 100.0, kSettle);
    std::printf ("    amount 0.5: 100 Hz %.1f dB re amount 1\n", db (h2Half / h2));
    CHECK (h2Half < h2);

    // R3 / R6: harmonic level tracks the sub level as a ratio, the same at -30 and -6 dBFS.
    const auto quietIn = dualMono (sine (50.0, kLen, 0.3f * ap::dbToGain (-24.0f)));
    const auto quiet = process (1.0f, quietIn);
    const double quietRel = toneLevel (quiet.l, 100.0, kSettle) / toneLevel (quiet.l, 50.0, kSettle);
    CHECK_NEAR (db (quietRel), db (h2 / f1), 0.5);
}

AP_TEST (weight_t3_sub_lift_at_60hz)
{
    const auto in = dualMono (sine (60.0, kLen, 0.3f));
    const auto dry = process (0.0f, in);
    const auto wet = process (1.0f, in);
    const double liftDb = db (toneLevel (wet.l, 60.0, kSettle) / toneLevel (dry.l, 60.0, kSettle));
    std::printf ("    60 Hz lift at amount 1: %+.2f dB\n", liftDb);
    // The 70 Hz corner is the shelf's turnover (pole) frequency, analytically +2.97 dB
    // at 60 Hz. (Reading "corner" as the RBJ half-gain midpoint would give only
    // +2.59 dB, outside this tolerance.)
    CHECK_NEAR (liftDb, 3.5, 0.8);

    // Shelf shape: more lift deeper down, none in the mids.
    const auto in40 = dualMono (sine (40.0, kLen, 0.3f));
    const double lift40 = db (toneLevel (process (1.0f, in40).l, 40.0, kSettle) / toneLevel (in40.l, 40.0, kSettle));
    CHECK (lift40 > liftDb);
    CHECK (lift40 < 4.2);

    // Scaled by amount: half the knob, about half the dB.
    const double liftHalf = db (toneLevel (process (0.5f, in).l, 60.0, kSettle) / toneLevel (dry.l, 60.0, kSettle));
    CHECK (liftHalf > 0.3 * liftDb && liftHalf < 0.7 * liftDb);
}

AP_TEST (weight_t4_wide_bass_folded_to_mono)
{
    const auto s = sine (60.0, kLen, 0.3f);
    std::vector<float> neg (s.size());
    for (size_t i = 0; i < s.size(); ++i) neg[i] = -s[i];
    const auto in = stereo (s, neg);
    const auto out = process (1.0f, in);

    const double sideIn  = bandRms (sideOf (in),  20.0, 100.0, kSettle);
    const double sideOut = bandRms (sideOf (out), 20.0, 100.0, kSettle);
    std::printf ("    side below 100 Hz: %.1f dB\n", db (sideOut / sideIn));
    CHECK (db (sideOut / sideIn) <= -20.0);

    // The input's mono sum is silent; removing the wide bass must not fold it into
    // the centre, so the output's mono sum stays silent.
    CHECK (rms (sumOf (out), kSettle) < 1.0e-6 * rms (s));

    // Mono sum unchanged within 0.5 dB when there is centred bass as well: the
    // mono sum with the wide component present equals the mono-only result.
    const auto m = sine (60.0, kLen, 0.2f, kSampleRate, 0.7);
    std::vector<float> l (kLen), r (kLen);
    for (size_t i = 0; i < kLen; ++i) { l[i] = m[i] + s[i]; r[i] = m[i] - s[i]; }
    const auto mixed = process (1.0f, stereo (l, r));
    const auto monoOnly = process (1.0f, dualMono (m));
    const double sumDiffDb = db (rms (sumOf (mixed), kSettle) / rms (sumOf (monoOnly), kSettle));
    CHECK (std::fabs (sumDiffDb) <= 0.5);
}

AP_TEST (weight_t5_mono_below_frequency_moves_the_split)
{
    const auto s = sine (150.0, kLen, 0.3f);
    std::vector<float> neg (s.size());
    for (size_t i = 0; i < s.size(); ++i) neg[i] = -s[i];
    const auto in = stereo (s, neg);   // pure side at 150 Hz

    const double sideIn = toneLevel (sideOf (in), 150.0, kSettle);
    const double at60  = db (toneLevel (sideOf (process (1.0f, in, 60.0f)),  150.0, kSettle) / sideIn);
    const double at200 = db (toneLevel (sideOf (process (1.0f, in, 200.0f)), 150.0, kSettle) / sideIn);
    std::printf ("    side 150 Hz: mono-below 60 Hz %+.2f dB, 200 Hz %+.2f dB\n", at60, at200);
    CHECK (std::fabs (at60) <= 1.0);   // survives
    CHECK (at200 <= -12.0);            // attenuated >= 12 dB

    // Out-of-range values clamp to 60..300 Hz.
    frisson::WeightModule w;
    w.setMonoBelowHz (10.0f);
    CHECK_NEAR (w.getMonoBelowHz(), 60.0f, 1e-6);
    w.setMonoBelowHz (1000.0f);
    CHECK_NEAR (w.getMonoBelowHz(), 300.0f, 1e-6);
}

AP_TEST (weight_t6_mid_band_untouched)
{
    const auto in = dualMono (sine (1000.0, kLen, 0.3f));
    const auto out = process (1.0f, in);
    const double diffDb = rmsDb (out.l, kSettle) - rmsDb (in.l, kSettle);
    std::printf ("    1 kHz change: %+.4f dB\n", diffDb);
    CHECK (std::fabs (diffDb) <= 0.1);
    CHECK (std::fabs (rmsDb (out.r, kSettle) - rmsDb (in.r, kSettle)) <= 0.1);
}

AP_TEST (weight_t7_no_dc_and_finite)
{
    const auto in = dualMono (sine (50.0, kLen * 2, 0.3f));
    const auto out = process (1.0f, in);
    std::printf ("    output mean: %.2e\n", mean (out.l, kSettle));
    CHECK (std::fabs (mean (out.l, kSettle)) < 1e-3);
    CHECK (std::fabs (mean (out.r, kSettle)) < 1e-3);
    CHECK (allFinite (out.l) && allFinite (out.r));

    // Finite on silence and on full-scale noise as well.
    const auto silent = process (1.0f, Stereo (kLen));
    CHECK (allFinite (silent.l) && peak (silent.l) == 0.0f);
    const auto loud = process (1.0f, stereo (white (kLen, 1.0f, 5u), white (kLen, 1.0f, 6u)));
    CHECK (allFinite (loud.l) && allFinite (loud.r));
}

AP_TEST (weight_t8_no_latency)
{
    frisson::WeightModule w;
    setup (w, 0.0f);
    CHECK_EQ (w.latencySamples(), 0);
    w.setAmount (1.0f);
    w.setMonoBelowHz (250.0f);
    CHECK_EQ (w.latencySamples(), 0);
}

// Beyond the PRD's T-list: R7 activity and R8 click-free parameter changes.

AP_TEST (weight_r7_activity_is_relative_harmonic_level)
{
    frisson::WeightModule w;
    setup (w, 1.0f);
    run (w, dualMono (sine (50.0, kLen, 0.3f)));
    const float loudAct = w.activity();
    CHECK (loudAct > 0.3f && loudAct <= 1.0f);

    frisson::WeightModule q;
    setup (q, 1.0f);
    run (q, dualMono (sine (50.0, kLen, 0.3f * ap::dbToGain (-24.0f))));
    CHECK_NEAR (q.activity(), loudAct, 0.05);   // level independent

    frisson::WeightModule hi;
    setup (hi, 1.0f);
    run (hi, dualMono (sine (1000.0, kLen, 0.3f)));
    CHECK (hi.activity() < 0.01f);              // nothing in the sub band, nothing generated

    frisson::WeightModule off;
    setup (off, 0.0f);
    run (off, dualMono (sine (50.0, kLen, 0.3f)));
    CHECK (off.activity() == 0.0f);
}

AP_TEST (weight_r8_parameter_changes_do_not_click)
{
    // Pure side at 80 Hz with the split at 120 Hz, so engaging/disengaging and moving
    // monoBelowHz all change the output a lot; the largest sample-to-sample step must
    // stay close to what the signal itself does (0.3 * 2 pi * 80 / 48000 = 0.0031).
    const auto s = sine (80.0, kLen * 3, 0.3f);
    std::vector<float> neg (s.size());
    for (size_t i = 0; i < s.size(); ++i) neg[i] = -s[i];
    Stereo buf = stereo (s, neg);

    frisson::WeightModule w;
    w.prepare (kSampleRate, 256);
    float maxStep = 0.0f, prev = 0.0f;
    for (size_t pos = 0; pos < buf.size(); pos += 256)
    {
        const size_t block = pos / 256;
        if (block == 20)  w.setAmount (1.0f);         // engage from exact bypass
        if (block == 80)  w.setMonoBelowHz (60.0f);   // move the split
        if (block == 140) w.setAmount (0.0f);         // back to bypass
        const int len = (int) std::min ((size_t) 256, buf.size() - pos);
        w.process (buf.l.data() + pos, buf.r.data() + pos, len);
        for (int i = 0; i < len; ++i)
        {
            maxStep = std::max (maxStep, std::fabs (buf.l[pos + (size_t) i] - prev));
            prev = buf.l[pos + (size_t) i];
        }
    }
    std::printf ("    max sample step: %.4f\n", maxStep);
    CHECK (maxStep < 0.006f);
    CHECK (maxDiff (buf.l, s, (size_t) 170 * 256) == 0.0f);   // exact bypass again at the end
}
