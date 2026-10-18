#include "TestHarness.h"
#include "TestSignals.h"

#include "modules/Chills.h"

#include <ap/Math.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>

using frisson::ChillsModulator;
using frisson::Modulation;

namespace
{
    constexpr double kSr    = fsig::kSampleRate;
    constexpr int    kBlock = 512;

    size_t samples (double seconds) { return (size_t) std::lround (seconds * kSr); }

    /** Deterministic pink noise with an RMS of `dbfs`. */
    std::vector<float> pinkAt (size_t n, double dbfs, uint32_t seed = 1234u)
    {
        auto v = fsig::pink (n, 1.0f, seed);
        const double g = std::pow (10.0, dbfs / 20.0) / fsig::rms (v);
        for (auto& s : v) s = (float) (s * g);
        return v;
    }

    /** Multiplies by a gain curve given in dB as a function of time (s). */
    void applyDb (std::vector<float>& v, const std::function<double (double)>& dbAt)
    {
        for (size_t i = 0; i < v.size(); ++i)
            v[i] = (float) (v[i] * std::pow (10.0, dbAt ((double) i / kSr) / 20.0));
    }

    /** The T2 crescendo: pink at `baseDb` for 4 s, a linear-in-dB ramp of `riseDb`
        over `rampSeconds`, then steady to `totalSeconds`. */
    std::vector<float> crescendo (double baseDb, double totalSeconds, double riseDb = 12.0,
                                  double rampStart = 4.0, double rampSeconds = 2.0, uint32_t seed = 1234u)
    {
        auto v = pinkAt (samples (totalSeconds), 0.0, seed);
        applyDb (v, [=] (double t)
        {
            return baseDb + riseDb * ap::clamp ((t - rampStart) / rampSeconds, 0.0, 1.0);
        });
        return v;
    }

    bool isIdentity (const Modulation& m)
    {
        const Modulation id = Modulation::identity();
        return m.airDb == id.airDb && m.widthMul == id.widthMul && m.densityMul == id.densityMul
            && m.driveMul == id.driveMul && m.eqFreqMul == id.eqFreqMul && m.transientMul == id.transientMul
            && m.gainL == id.gainL && m.gainR == id.gainR;
    }

    bool isFinite (const Modulation& m)
    {
        return std::isfinite (m.airDb) && std::isfinite (m.widthMul) && std::isfinite (m.densityMul)
            && std::isfinite (m.driveMul) && std::isfinite (m.eqFreqMul) && std::isfinite (m.transientMul)
            && std::isfinite (m.gainL) && std::isfinite (m.gainR);
    }

    /** One entry per analysed block; `time` is the end of the block in seconds. */
    struct Trace
    {
        std::vector<double>     time;
        std::vector<float>      activity;
        std::vector<Modulation> mod;
        int triggers = 0;

        size_t size() const { return time.size(); }

        /** First block at or after `from` whose activity exceeds `level`; size() if none. */
        size_t firstAbove (float level, double from = 0.0) const
        {
            for (size_t i = 0; i < size(); ++i)
                if (time[i] >= from && activity[i] > level) return i;
            return size();
        }

        float maxActivity (double from, double to = 1.0e9) const
        {
            float m = 0.0f;
            for (size_t i = 0; i < size(); ++i)
                if (time[i] >= from && time[i] <= to) m = std::max (m, activity[i]);
            return m;
        }

        /** Activity at the block that ends closest to `t`. */
        float activityAt (double t) const
        {
            size_t best = 0;
            for (size_t i = 0; i < size(); ++i)
                if (std::fabs (time[i] - t) < std::fabs (time[best] - t)) best = i;
            return activity[best];
        }
    };

    /** Feeds `l`/`r` (r may be null = mono) through analyze() in blocks. */
    Trace analyse (ChillsModulator& c, const std::vector<float>& l, const std::vector<float>* r = nullptr,
                   int blockSize = kBlock)
    {
        Trace tr;
        const size_t n = l.size();
        for (size_t pos = 0; pos < n; pos += (size_t) blockSize)
        {
            const int len = (int) std::min ((size_t) blockSize, n - pos);
            const Modulation m = c.analyze (l.data() + pos, r != nullptr ? r->data() + pos : l.data() + pos, len);
            tr.time.push_back ((double) (pos + (size_t) len) / kSr);
            tr.activity.push_back (c.activity());
            tr.mod.push_back (m);
        }
        tr.triggers = c.triggerCount();
        return tr;
    }

    void prepare (ChillsModulator& c, float amount, float sensitivity = 0.5f)
    {
        c.prepare (kSr, kBlock);
        c.setAmount (amount);
        c.setSensitivity (sensitivity);
    }
}

//==============================================================================
AP_TEST (chills_t1_amount_zero_is_identity)
{
    // A strong crescendo: −40 → −10 dBFS in 1 s.
    const auto x = crescendo (-40.0, 12.0, 30.0, 4.0, 1.0);

    ChillsModulator off;
    prepare (off, 0.0f, 1.0f);
    const Trace t = analyse (off, x);

    bool allIdentity = true, allZero = true;
    for (size_t i = 0; i < t.size(); ++i)
    {
        allIdentity = allIdentity && isIdentity (t.mod[i]);
        allZero = allZero && t.activity[i] == 0.0f;
    }
    CHECK (allIdentity);
    CHECK (allZero);
    CHECK_EQ (off.getAmount(), 0.0f);

    // Control: the same material at amount 1 does trigger, so the identity above is the knob.
    ChillsModulator on;
    prepare (on, 1.0f, 1.0f);
    const Trace u = analyse (on, x);
    CHECK (u.maxActivity (4.0) > 0.9f);

    // Turning the knob to 0 mid-swell lands on exact identity within the 100 ms de-zipper,
    // and the ramp down has no jumps (≤ 3 dB × 512 / 4800 per block).
    ChillsModulator mid;
    prepare (mid, 1.0f);
    const size_t cut = samples (4.6);
    const std::vector<float> head (x.begin(), x.begin() + (long) cut);
    const Trace a = analyse (mid, head);
    CHECK (a.activity.back() > 0.5f);

    mid.setAmount (0.0f);
    float prevAir = a.mod.back().airDb;
    float maxStep = 0.0f;
    bool identityAfterRamp = true;
    for (size_t pos = cut, k = 0; pos + (size_t) kBlock <= x.size(); pos += (size_t) kBlock, ++k)
    {
        const Modulation m = mid.analyze (x.data() + pos, x.data() + pos, kBlock);
        maxStep = std::max (maxStep, std::fabs (m.airDb - prevAir));
        prevAir = m.airDb;
        if ((double) (k * (size_t) kBlock) / kSr > 0.1 + (double) kBlock / kSr)
            identityAfterRamp = identityAfterRamp && isIdentity (m) && mid.activity() == 0.0f;
    }
    CHECK (identityAfterRamp);
    CHECK (maxStep <= 3.0f * (float) kBlock / 4800.0f + 1.0e-4f);
}

AP_TEST (chills_t2_crescendo_triggers_then_relaxes)
{
    // −30 dBFS for 4 s, linear-in-dB ramp to −18 dBFS over 2 s (4..6 s), steady to 16.5 s.
    const auto x = crescendo (-30.0, 16.5);

    ChillsModulator c;
    prepare (c, 1.0f);
    const Trace t = analyse (c, x);

    // activity > 0.5 during the ramp or within 1 s after it.
    const size_t i = t.firstAbove (0.5f);
    CHECK (i < t.size());
    if (i < t.size())
    {
        CHECK (t.time[i] >= 4.0 && t.time[i] <= 7.0);
        CHECK (t.mod[i].airDb >= 1.5f);

        // R5 shape at that moment: everything follows e (= activity) at amount 1.
        const float e = t.activity[i];
        CHECK_NEAR (t.mod[i].airDb, 3.0f * e, 1.0e-5);
        CHECK_NEAR (t.mod[i].widthMul, 1.0f + 0.3f * e, 1.0e-6);
        CHECK_NEAR (t.mod[i].densityMul, 1.0f + 0.25f * e, 1.0e-6);
        CHECK_EQ (t.mod[i].driveMul, 1.0f);
        CHECK_EQ (t.mod[i].eqFreqMul, 1.0f);
        CHECK_EQ (t.mod[i].transientMul, 1.0f);
        CHECK_EQ (t.mod[i].gainL, 1.0f);
        CHECK_EQ (t.mod[i].gainR, 1.0f);
    }

    // Nothing before the build, peak reaches 1, and after 10 s of steady level it has relaxed.
    CHECK (t.maxActivity (0.0, 4.0) == 0.0f);
    CHECK (t.maxActivity (4.0, 8.0) > 0.99f);
    CHECK (t.maxActivity (16.0) < 0.1f);
}

AP_TEST (chills_t3_novelty_new_voice_triggers_fast)
{
    const size_t n = samples (8.0);
    const auto bed  = pinkAt (n, -24.0);
    const auto tone = fsig::sine (12000.0, n, 0.1f);    // −20 dBFS

    // The 12 kHz voice enters at 5 s.
    std::vector<float> x = bed;
    for (size_t i = samples (5.0); i < n; ++i) x[i] += tone[i];

    ChillsModulator c;
    prepare (c, 1.0f);
    const Trace t = analyse (c, x);

    CHECK (t.maxActivity (0.0, 5.0) == 0.0f);
    const size_t i = t.firstAbove (0.3f, 5.0);
    CHECK (i < t.size());
    if (i < t.size())
        CHECK (t.time[i] - 5.0 <= 0.4);

    // The same voice present from the start is not new: no trigger after the first 4 s.
    const size_t m = samples (12.0);
    auto y = pinkAt (m, -24.0);
    const auto tone2 = fsig::sine (12000.0, m, 0.1f);
    for (size_t k = 0; k < m; ++k) y[k] += tone2[k];

    ChillsModulator d;
    prepare (d, 1.0f);
    const Trace u = analyse (d, y);
    bool rises = false;
    for (size_t k = 1; k < u.size(); ++k)
        if (u.time[k] > 4.0 && u.activity[k] > u.activity[k - 1]) rises = true;
    CHECK (! rises);
    CHECK (u.maxActivity (4.0) < 0.1f);
}

AP_TEST (chills_t4_steady_pink_never_triggers)
{
    const size_t n = samples (20.0);

    // Dual mono.
    {
        ChillsModulator c;
        prepare (c, 1.0f);
        const auto x = pinkAt (n, -20.0);
        const Trace t = analyse (c, x);
        CHECK (t.maxActivity (3.0) <= 0.1f);
        CHECK_EQ (t.triggers, 0);
    }

    // Decorrelated stereo, different seeds.
    {
        ChillsModulator c;
        prepare (c, 1.0f);
        const auto l = pinkAt (n, -20.0, 1u);
        const auto r = pinkAt (n, -20.0, 2u);
        const Trace t = analyse (c, l, &r);
        CHECK (t.maxActivity (3.0) <= 0.1f);
        CHECK_EQ (t.triggers, 0);
    }
}

AP_TEST (chills_t5_refractory_blocks_second_crescendo)
{
    // Two +9 dB / 1 s crescendos starting 2 s apart (4 s and 6 s).
    auto x = pinkAt (samples (14.0), 0.0);
    applyDb (x, [] (double t)
    {
        return -33.0 + 9.0 * ap::clamp (t - 4.0, 0.0, 1.0) + 9.0 * ap::clamp (t - 6.0, 0.0, 1.0);
    });

    ChillsModulator c;
    prepare (c, 1.0f);
    const Trace t = analyse (c, x);

    const size_t first = t.firstAbove (0.0f);
    CHECK (first < t.size());
    if (first < t.size())
    {
        const double t1 = t.time[first];
        CHECK (t1 >= 4.0 && t1 <= 6.0);

        // Once the first envelope has peaked and fallen below 0.8, it never climbs back
        // above 0.8 within 4 s of the first trigger.
        const size_t peak = t.firstAbove (0.8f);
        CHECK (peak < t.size());
        bool fallen = false, reRise = false;
        for (size_t i = peak; i < t.size() && t.time[i] <= t1 + 4.0; ++i)
        {
            if (t.activity[i] < 0.8f) fallen = true;
            else if (fallen) reRise = true;
        }
        CHECK (fallen);
        CHECK (! reRise);
        CHECK (t.activityAt (t1 + 3.0) < 0.5f);   // released, not re-held
    }

    // Control: the second crescendo on its own (same level, same shape) does trigger,
    // so the single trigger above is the refractory period at work.
    auto y = pinkAt (samples (14.0), 0.0);
    applyDb (y, [] (double tt) { return -24.0 + 9.0 * ap::clamp (tt - 6.0, 0.0, 1.0); });
    ChillsModulator d;
    prepare (d, 1.0f);
    const Trace u = analyse (d, y);
    CHECK (u.maxActivity (6.0, 8.0) > 0.8f);
}

AP_TEST (chills_t6_sensitivity_scales_crescendo_threshold)
{
    // A "2 dB crescendo" measured the way R3 measures it: a +4 dB / 2 s build lifts
    // L_fast − L_slow to ~2 dB (between the 1.5 dB threshold at sensitivity 1 and
    // 4.5 dB at sensitivity 0). A literal +2 dB level step can only reach 1.22 dB on
    // the 400 ms / 3 s envelopes of R2, so it cannot exceed even the 1.5 dB threshold.
    const auto x = crescendo (-24.0, 12.0, 4.0, 5.0, 2.0);

    ChillsModulator hot;
    prepare (hot, 1.0f, 1.0f);
    float peakDelta = -100.0f;
    Trace t;
    {
        for (size_t pos = 0; pos < x.size(); pos += (size_t) kBlock)
        {
            const int len = (int) std::min ((size_t) kBlock, x.size() - pos);
            hot.analyze (x.data() + pos, x.data() + pos, len);
            const double tt = (double) (pos + (size_t) len) / kSr;
            t.time.push_back (tt);
            t.activity.push_back (hot.activity());
            if (tt > 5.0) peakDelta = std::max (peakDelta, hot.levelDeltaDb());
        }
    }
    CHECK (peakDelta > 1.6f && peakDelta < 2.6f);      // it is a ~2 dB crescendo
    CHECK (t.maxActivity (5.0) > 0.9f);                // sensitivity 1 triggers
    CHECK_EQ (t.maxActivity (0.0, 5.0), 0.0f);         // ... on the build, not before it

    ChillsModulator cold;
    prepare (cold, 1.0f, 0.0f);
    const Trace u = analyse (cold, x);
    CHECK_EQ (u.maxActivity (0.0), 0.0f);              // sensitivity 0 ignores it
    CHECK_EQ (u.triggers, 0);

    CHECK_NEAR (hot.getSensitivity(), 1.0f, 1.0e-7);
    CHECK_NEAR (cold.getSensitivity(), 0.0f, 1.0e-7);
}

AP_TEST (chills_t7_level_independent)
{
    ChillsModulator ref;
    prepare (ref, 1.0f);
    const Trace a = analyse (ref, crescendo (-30.0, 16.5));

    for (double base : { -50.0, -18.0 })   // −50 → −38 dBFS (PRD), and a hot −18 → −6 dBFS mix
    {
        ChillsModulator c;
        prepare (c, 1.0f);
        const Trace b = analyse (c, crescendo (base, 16.5));

        const size_t ia = a.firstAbove (0.5f), ib = b.firstAbove (0.5f);
        CHECK (ib < b.size());
        if (ib < b.size() && ia < a.size())
        {
            CHECK (b.time[ib] >= 4.0 && b.time[ib] <= 7.0);
            CHECK (b.mod[ib].airDb >= 1.5f);
            CHECK_NEAR (b.time[ib], a.time[ia], 1.0e-9);   // same block
        }
        CHECK (b.maxActivity (16.0) < 0.1f);
        CHECK_EQ (b.triggers, a.triggers);

        float diff = 0.0f;
        for (size_t i = 0; i < a.size() && i < b.size(); ++i)
            diff = std::max (diff, std::fabs (a.activity[i] - b.activity[i]));
        CHECK (diff < 1.0e-3f);
    }
}

AP_TEST (chills_t8_silence_and_odd_blocks)
{
    // Silence: identity every block, activity 0, nothing non-finite — 512 and 37 sample blocks.
    for (int bs : { kBlock, 37 })
    {
        ChillsModulator c;
        prepare (c, 1.0f, 1.0f);
        const std::vector<float> zeros (samples (10.0), 0.0f);
        const Trace t = analyse (c, zeros, nullptr, bs);
        bool ok = true;
        for (size_t i = 0; i < t.size(); ++i)
            ok = ok && isIdentity (t.mod[i]) && isFinite (t.mod[i]) && t.activity[i] == 0.0f;
        CHECK (ok);
        CHECK (std::isfinite (c.levelDeltaDb()) && std::isfinite (c.noveltyFluxDb()));
    }

    // Pressing play: 3 s of silence, then steady music, is not an arrival.
    {
        auto x = pinkAt (samples (12.0), -20.0);
        std::fill (x.begin(), x.begin() + (long) samples (3.0), 0.0f);
        ChillsModulator c;
        prepare (c, 1.0f);
        const Trace t = analyse (c, x);
        CHECK (t.maxActivity (0.0) <= 0.1f);
    }

    // Odd block size 37: T3 still fires within 400 ms, T2 fires on the same 5 ms hop as with
    // 512-sample blocks (so within one 512 block of it), and the mono (null right) path works.
    {
        const size_t n = samples (8.0);
        auto x = pinkAt (n, -24.0);
        const auto tone = fsig::sine (12000.0, n, 0.1f);
        for (size_t i = samples (5.0); i < n; ++i) x[i] += tone[i];

        ChillsModulator c;
        prepare (c, 1.0f);
        Trace t;
        for (size_t pos = 0; pos < n; pos += 37)
        {
            const int len = (int) std::min ((size_t) 37, n - pos);
            const Modulation m = c.analyze (x.data() + pos, nullptr, len);
            t.time.push_back ((double) (pos + (size_t) len) / kSr);
            t.activity.push_back (c.activity());
            t.mod.push_back (m);
        }
        const size_t i = t.firstAbove (0.3f, 5.0);
        CHECK (i < t.size());
        if (i < t.size())
            CHECK (t.time[i] - 5.0 <= 0.4);
        CHECK_EQ (t.maxActivity (0.0, 5.0), 0.0f);
    }
    {
        const auto x = crescendo (-30.0, 10.0);
        ChillsModulator a, b;
        prepare (a, 1.0f);
        prepare (b, 1.0f);
        const Trace ta = analyse (a, x, nullptr, kBlock);
        const Trace tb = analyse (b, x, nullptr, 37);
        const size_t ia = ta.firstAbove (0.5f), ib = tb.firstAbove (0.5f);
        CHECK (ia < ta.size() && ib < tb.size());
        if (ia < ta.size() && ib < tb.size())
            CHECK (std::fabs (ta.time[ia] - tb.time[ib]) <= (double) kBlock / kSr);
        CHECK_EQ (ta.triggers, tb.triggers);
    }

    // Garbage in (NaN / inf from upstream) never produces a non-finite modulation.
    {
        ChillsModulator c;
        prepare (c, 1.0f);
        auto x = pinkAt (samples (2.0), -20.0);
        x[1000] = std::numeric_limits<float>::quiet_NaN();
        x[2000] = std::numeric_limits<float>::infinity();
        const Trace t = analyse (c, x);
        bool ok = true;
        for (size_t i = 0; i < t.size(); ++i) ok = ok && isFinite (t.mod[i]) && std::isfinite (t.activity[i]);
        CHECK (ok);
    }
}
