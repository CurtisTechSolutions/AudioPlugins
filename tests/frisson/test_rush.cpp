// RUSH acceptance tests (docs/prd/frisson-rush.md, section 5).

#include "TestHarness.h"
#include "TestSignals.h"

#include "modules/Rush.h"

#include <ap/Math.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

using namespace fsig;

namespace
{
    constexpr int    kBlock     = 256;
    constexpr size_t kSection   = 19200;   // 400 ms
    constexpr size_t kGuard     = 2400;    // 50 ms excluded around each step
    constexpr size_t kSettle    = 96000;   // 2 s
    constexpr size_t kSr        = 48000;

    void prepareRush (frisson::RushModule& rush, float amount, bool autoGain = true)
    {
        rush.prepare (kSampleRate, kBlock);
        rush.setAutoGain (autoGain);
        rush.setAmount (amount);
    }

    /** Runs `in` through `module` in blocks, calling `beforeBlock (pos)` before each one. */
    Stereo runWith (frisson::RushModule& module, const Stereo& in, const std::function<void (size_t)>& beforeBlock)
    {
        Stereo out = in;
        for (size_t pos = 0; pos < out.size(); pos += (size_t) kBlock)
        {
            beforeBlock (pos);
            const int len = (int) std::min ((size_t) kBlock, out.size() - pos);
            module.process (out.l.data() + pos, out.r.data() + pos, len);
        }
        return out;
    }

    /** Pink noise "at L dBFS" in the fsig convention: pink (n, dbToGain (L)).
        (fsig::pink's RMS is 7.4 dB under its amplitude argument; its peaks ~5 dB over.) */
    std::vector<float> pinkAt (size_t n, float dbfs, uint32_t seed)
    {
        return pink (n, ap::dbToGain (dbfs), seed);
    }

    /** Stereo pink alternating 400 ms sections at `loudDb` / `quietDb` (loud first). */
    Stereo alternatingPink (float loudDb, float quietDb, size_t n)
    {
        const double step = std::pow (10.0, (double) (quietDb - loudDb) / 20.0);
        auto l = pinkAt (n, loudDb, 11u);
        auto r = pinkAt (n, loudDb, 12u);
        for (size_t i = 0; i < n; ++i)
        {
            if ((i / kSection) % 2 == 1)
            {
                l[i] = (float) (l[i] * step);
                r[i] = (float) (r[i] * step);
            }
        }
        return stereo (std::move (l), std::move (r));
    }

    /** Quiet/loud RMS ratio in dB of a processed alternating signal, after settling,
        excluding 50 ms around every step; `latency` aligns output to input sections. */
    double quietLoudRatioDb (const Stereo& out, int latency)
    {
        double loudAcc = 0.0, quietAcc = 0.0;
        size_t loudN = 0, quietN = 0;
        for (size_t s0 = kSettle; s0 + kSection + (size_t) latency <= out.size(); s0 += kSection)
        {
            const bool quiet = (s0 / kSection) % 2 == 1;
            const size_t a = s0 + (size_t) latency + kGuard;
            const size_t b = s0 + (size_t) latency + kSection - kGuard;
            for (size_t i = a; i < b; ++i)
            {
                const double p = (double) out.l[i] * out.l[i] + (double) out.r[i] * out.r[i];
                if (quiet) { quietAcc += p; ++quietN; }
                else       { loudAcc += p;  ++loudN; }
            }
        }
        return 10.0 * std::log10 ((quietAcc / (double) quietN) / (loudAcc / (double) loudN));
    }

    /** Ratio change (dB, positive = the quiet/loud gap shrank) of `amount`/`densityMul` vs amount 0. */
    double densityShrinkDb (float loudDb, float quietDb, float amount, float densityMul, float* maxActivity = nullptr)
    {
        const Stereo in = alternatingPink (loudDb, quietDb, 8 * kSr);

        frisson::RushModule dry;
        prepareRush (dry, 0.0f, false);
        const double ratio0 = quietLoudRatioDb (run (dry, in, kBlock), dry.latencySamples());

        frisson::RushModule rush;
        prepareRush (rush, amount, false);
        frisson::Modulation m;
        m.densityMul = densityMul;
        float act = 0.0f;
        const Stereo out = runWith (rush, in, [&] (size_t)
        {
            act = std::max (act, rush.activity());
            rush.setModulation (m);
        });
        if (maxActivity != nullptr) *maxActivity = act;
        return quietLoudRatioDb (out, rush.latencySamples()) - ratio0;
    }

    double windowRmsDb (const Stereo& s, size_t from, size_t to)
    {
        double acc = 0.0;
        for (size_t i = from; i < to; ++i) acc += (double) s.l[i] * s.l[i] + (double) s.r[i] * s.r[i];
        return 10.0 * std::log10 (acc / (double) (2 * (to - from)));
    }
}

AP_TEST (rush_t1_amount_zero_is_delayed_bitwise_bypass)
{
    const Stereo in = stereoPink (2 * kSr, 0.3f);

    frisson::RushModule rush;
    prepareRush (rush, 0.0f);
    CHECK_EQ (rush.latencySamples(), 72);
    CHECK_EQ (rush.latencySamples(), (int) std::lround (0.0015 * 48000.0));

    const Stereo out = run (rush, in, kBlock);
    const int lat = rush.latencySamples();

    std::vector<float> shiftedL (in.size(), 0.0f), shiftedR (in.size(), 0.0f);
    for (size_t i = (size_t) lat; i < in.size(); ++i)
    {
        shiftedL[i] = in.l[i - (size_t) lat];
        shiftedR[i] = in.r[i - (size_t) lat];
    }
    CHECK (maxDiff (out.l, shiftedL) == 0.0f);
    CHECK (maxDiff (out.r, shiftedR) == 0.0f);
    CHECK (rush.activity() == 0.0f);

    // Latency is a prepare-time constant, independent of amount and settings.
    for (float a : { 0.0f, 0.3f, 1.0f })
    {
        frisson::RushModule other;
        other.prepare (kSampleRate, kBlock);
        other.setAmount (a);
        other.setCeilingDb (-3.0f);
        other.setAutoGain (false);
        CHECK_EQ (other.latencySamples(), lat);
    }
    frisson::RushModule at44;
    at44.prepare (44100.0, kBlock);
    CHECK_EQ (at44.latencySamples(), (int) std::lround (0.0015 * 44100.0));

    // Engage for 1 s, then back to 0: after the 20 ms fade it is a bitwise bypass again.
    frisson::RushModule toggled;
    prepareRush (toggled, 1.0f);
    const Stereo out2 = runWith (toggled, in, [&] (size_t pos) { toggled.setAmount (pos < kSr ? 1.0f : 0.0f); });
    const size_t from = kSr + 4800;   // 100 ms after the knob returned to 0
    float diff = 0.0f;
    for (size_t i = from; i < in.size(); ++i)
    {
        diff = std::max (diff, std::fabs (out2.l[i] - in.l[i - (size_t) lat]));
        diff = std::max (diff, std::fabs (out2.r[i] - in.r[i - (size_t) lat]));
    }
    CHECK (diff == 0.0f);
}

AP_TEST (rush_t2_limiter_holds_ceiling)
{
    const float amp = ap::dbToGain (3.0f);   // +3 dBFS peak
    const Stereo in = dualMono (sine (1000.0, 2 * kSr, amp));

    for (bool autoGain : { true, false })
    {
        frisson::RushModule rush;
        prepareRush (rush, 0.5f, autoGain);
        rush.setCeilingDb (-0.3f);
        const Stereo out = run (rush, in, kBlock);

        const float limit = ap::dbToGain (-0.3f + 0.05f);
        const float pk = std::max (peak (out.l, (size_t) kBlock), peak (out.r, (size_t) kBlock));
        std::printf ("    T2 autoGain=%d: peak after first block %.3f dBFS\n", (int) autoGain, ap::gainToDb (pk));
        CHECK (pk <= limit);
        CHECK (pk >= ap::dbToGain (-1.5f));          // limiting, not muting
        CHECK (allFinite (out.l) && allFinite (out.r));
        CHECK (rush.activity() > 0.0f);
    }

    // The ceiling setter is honoured at its range end too.
    frisson::RushModule rush;
    prepareRush (rush, 0.5f);
    rush.setCeilingDb (-3.0f);
    const Stereo out = run (rush, in, kBlock);
    CHECK (peak (out.l, (size_t) kBlock) <= ap::dbToGain (-3.0f + 0.05f));
}

AP_TEST (rush_t3_density_shrinks_quiet_loud_gap)
{
    float act = 0.0f;
    const double shrink = densityShrinkDb (-12.0f, -24.0f, 1.0f, 1.0f, &act);
    std::printf ("    T3 quiet/loud ratio shrinks by %.2f dB (needs >= 3), max activity %.2f\n", shrink, act);
    CHECK (shrink >= 3.0);
    CHECK (act > 0.0f && act <= 1.0f);
}

AP_TEST (rush_t4_auto_gain_matches_input_rms)
{
    const Stereo in = stereo (pinkAt (8 * kSr, -20.0f, 21u), pinkAt (8 * kSr, -20.0f, 22u));

    frisson::RushModule on;
    prepareRush (on, 1.0f, true);
    const Stereo outOn = run (on, in, kBlock);

    frisson::RushModule off;
    prepareRush (off, 1.0f, false);
    const Stereo outOff = run (off, in, kBlock);

    const size_t lat = (size_t) on.latencySamples();
    const size_t a = 6 * kSr, b = 7 * kSr;
    const double inDb  = windowRmsDb (in, a, b);
    const double onDb  = windowRmsDb (outOn, a + lat, b + lat);
    const double offDb = windowRmsDb (outOff, a + lat, b + lat);
    std::printf ("    T4 input %.2f dB, autoGain on %.2f dB, off %.2f dB\n", inDb, onDb, offDb);
    CHECK_NEAR (onDb, inDb, 1.0);
    CHECK (offDb > inDb + 1.0);
}

AP_TEST (rush_t5_transients_survive)
{
    // Click train: 3 ms decaying broadband bursts every 250 ms over a quiet pink bed.
    const size_t n = 6 * kSr, period = 12000;
    auto noise = white (n, 1.0f, 5u);
    auto bed = pinkAt (n, -40.0f, 6u);
    std::vector<float> x (n);
    for (size_t i = 0; i < n; ++i)
    {
        const size_t t = i % period;
        const float click = t < 1440 ? 0.5f * noise[i] * std::exp (-(float) t / 144.0f) : 0.0f;
        x[i] = click + bed[i];
    }
    const Stereo in = dualMono (x);

    frisson::RushModule dry;
    prepareRush (dry, 0.0f);
    const Stereo ref = run (dry, in, kBlock);

    frisson::RushModule rush;
    prepareRush (rush, 1.0f);
    const Stereo out = run (rush, in, kBlock);

    double worst = 1.0e9;
    int clicks = 0;
    for (size_t c = 3 * kSr; c + period <= n; c += period)
    {
        const size_t a = c, b = c + 2400;   // the first 50 ms of each click (outputs share the latency)
        float pRef = 0.0f, pOut = 0.0f;
        for (size_t i = a; i < b; ++i)
        {
            pRef = std::max (pRef, std::fabs (ref.l[i]));
            pOut = std::max (pOut, std::fabs (out.l[i]));
        }
        worst = std::min (worst, 20.0 * std::log10 ((double) pOut / (double) pRef));
        ++clicks;
    }
    std::printf ("    T5 worst click peak vs amount 0: %+.2f dB over %d clicks\n", worst, clicks);
    CHECK (clicks >= 10);
    CHECK (worst >= -3.0);
}

AP_TEST (rush_t6_density_modulation_compresses_more)
{
    const double base   = densityShrinkDb (-12.0f, -24.0f, 0.5f, 1.0f);
    const double pushed = densityShrinkDb (-12.0f, -24.0f, 0.5f, 2.0f);
    const double full   = densityShrinkDb (-12.0f, -24.0f, 1.0f, 1.0f);
    std::printf ("    T6 shrink at amount 0.5: densityMul 1 %.2f dB, densityMul 2 %.2f dB (amount 1: %.2f dB)\n",
                 base, pushed, full);
    CHECK (pushed > base + 0.5);
    CHECK_NEAR (pushed, full, 0.05);   // mix = amount × densityMul
}

AP_TEST (rush_t7_level_independent)
{
    const double hot  = densityShrinkDb (-12.0f, -24.0f, 1.0f, 1.0f);
    const double cold = densityShrinkDb (-40.0f, -52.0f, 1.0f, 1.0f);
    std::printf ("    T7 shrink at -12/-24: %.2f dB, at -40/-52: %.2f dB\n", hot, cold);
    CHECK_NEAR (cold, hot, 1.0);
}

AP_TEST (rush_t8_finite_silence_and_activity_range)
{
    // Silence in, silence out, at every amount.
    for (float a : { 0.0f, 0.5f, 1.0f })
    {
        frisson::RushModule rush;
        prepareRush (rush, a);
        const Stereo out = run (rush, Stereo (kSr), kBlock);
        CHECK (peak (out.l) == 0.0f && peak (out.r) == 0.0f);
        CHECK (rush.activity() == 0.0f);
    }

    // Hostile material: hot white noise, DC, Nyquist, impulses, then silence; amount (never 0,
    // where the PRD makes RUSH a bypass with no ceiling) and modulation swept every block.
    // Everything stays finite, under the ceiling, and activity stays in 0..1.
    const size_t n = 4 * kSr;
    std::vector<float> x (n, 0.0f);
    const auto hot = white (kSr, 8.0f, 77u);
    for (size_t i = 0; i < kSr; ++i)               x[i] = hot[i];
    for (size_t i = kSr; i < kSr + kSr / 2; ++i)   x[i] = 0.7f;
    for (size_t i = kSr + kSr / 2; i < 2 * kSr; ++i) x[i] = (i % 2 == 0) ? 0.5f : -0.5f;
    for (size_t i = 2 * kSr; i < 3 * kSr; i += 4800) x[i] = 1.0f;
    const Stereo in = dualMono (x);

    frisson::RushModule rush;
    prepareRush (rush, 1.0f);
    bool activityOk = true;
    int blockIndex = 0;
    float maxAct = 0.0f;
    const Stereo out = runWith (rush, in, [&] (size_t)
    {
        const float act = rush.activity();
        activityOk = activityOk && act >= 0.0f && act <= 1.0f && std::isfinite (act);
        maxAct = std::max (maxAct, act);
        frisson::Modulation m;
        m.densityMul = (blockIndex % 7 == 0) ? 4.0f : 0.25f + 0.1f * (float) (blockIndex % 5);
        rush.setModulation (m);
        rush.setAmount ((float) (1 + (blockIndex * 37) % 100) / 100.0f);
        ++blockIndex;
    });
    CHECK (allFinite (out.l) && allFinite (out.r));
    CHECK (activityOk);
    CHECK (maxAct > 0.1f);
    CHECK (peak (out.l, 3 * kSr + kSr / 2) < 1.0e-6f);   // decays back to silence
    // The limiter keeps even the hot noise under the default ceiling.
    CHECK (peak (out.l, (size_t) kBlock) <= ap::dbToGain (-0.3f + 0.05f));

    // A mono host passes one buffer as left and right: same result as dual mono.
    const Stereo pinkIn = dualMono (pink (kSr, 0.3f, 3u));
    frisson::RushModule stereoRush, monoRush;
    prepareRush (stereoRush, 0.7f);
    prepareRush (monoRush, 0.7f);
    const Stereo ref = run (stereoRush, pinkIn, kBlock);
    std::vector<float> mono = pinkIn.l;
    for (size_t pos = 0; pos < mono.size(); pos += (size_t) kBlock)
        monoRush.process (mono.data() + pos, mono.data() + pos, (int) std::min ((size_t) kBlock, mono.size() - pos));
    CHECK (maxDiff (mono, ref.l) == 0.0f);

    // Knob automation does not click: toggling 0 <-> 1 every 100 ms on a sine moves the
    // output no faster per sample than a full-scale-ish sine at the processed level would.
    const auto tone = sine (200.0, 2 * kSr, 0.25f);
    frisson::RushModule autom;
    prepareRush (autom, 0.0f);
    const Stereo toggled = runWith (autom, dualMono (tone), [&] (size_t pos)
    {
        autom.setAmount (((pos / 4800) % 2 == 1) ? 1.0f : 0.0f);
    });
    float maxStep = 0.0f, toneStep = 0.0f;
    for (size_t i = 1; i < toggled.size(); ++i)
    {
        maxStep  = std::max (maxStep, std::fabs (toggled.l[i] - toggled.l[i - 1]));
        toneStep = std::max (toneStep, std::fabs (tone[i] - tone[i - 1]));
    }
    std::printf ("    T8 automation: max per-sample step %.4f vs dry tone %.4f\n", maxStep, toneStep);
    CHECK (maxStep < 2.5f * toneStep);
}
