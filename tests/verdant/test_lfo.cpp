// Lfo (CONTRACT §3 LFOs, §8 LFO tests).

#include "TestHarness.h"

#include "engine/Common.h"
#include "engine/Lfo.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace verdant;

namespace
{
    constexpr double kSr = 48000.0;

    LfoParams lfoParams (LfoWave w, float hz, LfoMode mode = LfoMode::Retrig)
    {
        LfoParams p;
        p.shape = w;
        p.rateHz = hz;
        p.mode = mode;
        return p;
    }

    /** Upward zero-crossing positions (fractional samples) of a per-sample LFO run. */
    std::vector<double> upCrossings (Lfo& lfo, int n)
    {
        std::vector<double> xs;
        float prev = lfo.next (1);
        for (int i = 1; i < n; ++i)
        {
            const float v = lfo.next (1);
            if (prev < 0.0f && v >= 0.0f)
                xs.push_back ((double) (i - 1) + (double) (-prev / (v - prev)));
            prev = v;
        }
        return xs;
    }

    std::vector<float> runBlocks (Lfo& lfo, int blocks, int blockSize = kControlInterval)
    {
        std::vector<float> v ((size_t) blocks);
        for (auto& x : v)
            x = lfo.next (blockSize);
        return v;
    }
}

AP_TEST (lfo_sine_period_and_sync_rate)
{
    // Sine at 2 Hz: zero crossings every 0.5 s (24000 samples) +- 1 sample.
    {
        Lfo lfo;
        lfo.prepare (kSr);
        lfo.set (lfoParams (LfoWave::Sine, 2.0f));
        lfo.noteOn (1);
        const auto xs = upCrossings (lfo, (int) (2.2 * kSr));
        CHECK (xs.size() >= 4);
        bool ok = xs.size() >= 2;
        for (size_t i = 1; i < xs.size(); ++i)
            ok = ok && std::fabs ((xs[i] - xs[i - 1]) - 24000.0) <= 1.0;
        CHECK (ok);
    }

    // Sync 1/4 at 120 bpm = 2 Hz, measured the same way.
    {
        Lfo lfo;
        lfo.prepare (kSr);
        LfoParams p = lfoParams (LfoWave::Sine, 7.0f);
        p.sync = true;
        p.division = ap::NoteDivision::Quarter;
        lfo.set (p);
        Transport t;
        t.bpm = 120.0;
        t.hasTempo = true;
        lfo.syncToTransport (t);
        lfo.noteOn (1);
        CHECK_NEAR (lfo.rateHz(), 2.0, 1.0e-9);
        const auto xs = upCrossings (lfo, (int) (1.2 * kSr));
        bool ok = xs.size() >= 2;
        for (size_t i = 1; i < xs.size(); ++i)
            ok = ok && std::fabs ((xs[i] - xs[i - 1]) - 24000.0) <= 1.0;
        CHECK (ok);

        // Other divisions / tempi; no tempo -> 120 bpm.
        p.division = ap::NoteDivision::EighthTriplet;
        lfo.set (p);
        CHECK_NEAR (lfo.rateHz(), 6.0, 1.0e-9);
        t.bpm = 90.0;
        p.division = ap::NoteDivision::Quarter;
        lfo.set (p);
        lfo.syncToTransport (t);
        CHECK_NEAR (lfo.rateHz(), 1.5, 1.0e-9);
        t.hasTempo = false;
        lfo.syncToTransport (t);
        CHECK_NEAR (lfo.rateHz(), 2.0, 1.0e-9);
    }
}

AP_TEST (lfo_shapes_at_quarter_phases)
{
    struct Expect { LfoWave w; float v[4]; };
    const Expect cases[] = {
        { LfoWave::Sine,     { 0.0f, 1.0f, 0.0f, -1.0f } },
        { LfoWave::Triangle, { 0.0f, 1.0f, 0.0f, -1.0f } },
        { LfoWave::SawUp,    { -1.0f, -0.5f, 0.0f, 0.5f } },
        { LfoWave::SawDown,  { 1.0f, 0.5f, 0.0f, -0.5f } },
        { LfoWave::Square,   { 1.0f, 1.0f, -1.0f, -1.0f } },
    };
    for (const auto& c : cases)
    {
        Lfo lfo;
        lfo.prepare (kSr);
        lfo.set (lfoParams (c.w, 1.0f));
        lfo.noteOn (3);
        for (int q = 0; q < 4; ++q)
        {
            CHECK_NEAR (lfo.phase(), 0.25 * q, 1.0e-9);
            CHECK_NEAR (lfo.next ((int) kSr / 4), c.v[q], 1.0e-5);
        }
    }

    // Start phase: Retrig restarts there on every note-on.
    Lfo lfo;
    lfo.prepare (kSr);
    LfoParams p = lfoParams (LfoWave::SawUp, 3.0f);
    p.startPhase = 0.25f;
    lfo.set (p);
    lfo.noteOn (1);
    CHECK_NEAR (lfo.next (1000), -0.5, 1.0e-6);
    lfo.noteOn (2);
    CHECK_NEAR (lfo.next (32), -0.5, 1.0e-6);
}

AP_TEST (lfo_sample_and_hold)
{
    const auto run = [] (uint32_t seed, std::vector<double>* phases)
    {
        Lfo lfo;
        lfo.prepare (kSr);
        lfo.set (lfoParams (LfoWave::SampleHold, 10.0f));
        lfo.noteOn (seed);
        std::vector<float> v;
        for (int b = 0; b < 1500; ++b)
        {
            if (phases != nullptr) phases->push_back (lfo.phase());
            v.push_back (lfo.next (kControlInterval));
        }
        return v;
    };

    std::vector<double> phases;
    const auto a = run (42, &phases);
    // Constant within a cycle; changes exactly at the blocks that follow a phase wrap.
    int changes = 0, misplaced = 0;
    for (size_t i = 1; i < a.size(); ++i)
    {
        const bool wrapped = phases[i] < phases[i - 1];
        const bool changed = a[i] != a[i - 1];
        if (changed) ++changes;
        if (changed && ! wrapped) ++misplaced;
    }
    CHECK_EQ (misplaced, 0);
    CHECK (changes >= 8);   // 1500 x 32 samples = 1 s at 10 Hz: ~10 wraps, nearly all new values
    bool inRange = true;
    for (float v : a) inRange = inRange && v >= -1.0f && v <= 1.0f;
    CHECK (inRange);

    // Deterministic for a seed, different for another.
    CHECK (run (42, nullptr) == a);
    CHECK (run (43, nullptr) != a);
}

AP_TEST (lfo_custom_shape_passes_through_points)
{
    LfoShape shape;
    shape.points = { { 0.0f, 0.2f, 0.0f }, { 0.3f, 1.0f, 0.0f }, { 0.6f, 0.5f, 0.5f }, { 1.0f, 0.0f, 0.0f } };

    Lfo lfo;
    lfo.prepare (1000.0);
    lfo.set (lfoParams (LfoWave::Custom, 1.0f));
    lfo.setCustomShape (&shape);
    lfo.noteOn (1);
    CHECK_NEAR (lfo.next (300), 2.0 * 0.2 - 1.0, 1.0e-5);   // phase 0
    CHECK_NEAR (lfo.next (300), 2.0 * 1.0 - 1.0, 1.0e-5);   // phase 0.3
    CHECK_NEAR (lfo.next (200), 2.0 * 0.5 - 1.0, 1.0e-5);   // phase 0.6
    // Phase 0.8: halfway through the curved last segment (curve 0.5 of point 2).
    const double t = std::pow (0.5, 1.0 / 2.5);
    CHECK_NEAR (lfo.next (200), 2.0 * (0.5 - 0.5 * t) - 1.0, 1.0e-4);
    CHECK_NEAR (evaluateLfoShape (shape, 0.15f), 0.6, 1.0e-5);   // linear segment midpoint
    CHECK_NEAR (evaluateLfoShape (shape, 1.0f), 0.0, 1.0e-6);

    // No shape / a degenerate one: the default triangle, always finite.
    lfo.setCustomShape (nullptr);
    lfo.noteOn (1);
    CHECK_NEAR (lfo.next (500), -1.0, 1.0e-5);
    CHECK_NEAR (lfo.next (1), 1.0, 1.0e-2);
    LfoShape broken;
    broken.points = { { 0.7f, std::nanf (""), 3.0f }, { 0.2f, 5.0f, -9.0f } };
    lfo.setCustomShape (&broken);
    bool finite = true;
    for (int i = 0; i < 100; ++i)
    {
        const float v = lfo.next (13);
        finite = finite && std::isfinite (v) && v >= -1.0f && v <= 1.0f;
    }
    CHECK (finite);
}

AP_TEST (lfo_one_shot_holds)
{
    // Saw Up one-shot at 2 Hz: rises once over 0.5 s, then holds +1.
    Lfo lfo;
    lfo.prepare (kSr);
    lfo.set (lfoParams (LfoWave::SawUp, 2.0f, LfoMode::OneShot));
    lfo.noteOn (1);
    const auto v = runBlocks (lfo, (int) (1.0 * kSr / kControlInterval));
    const size_t cycleEnd = (size_t) (0.5 * kSr / kControlInterval);
    bool rising = true;
    for (size_t i = 1; i < cycleEnd; ++i) rising = rising && v[i] > v[i - 1];
    CHECK (rising);
    bool holding = true;
    for (size_t i = cycleEnd + 2; i < v.size(); ++i) holding = holding && v[i] == 1.0f;
    CHECK (holding);
    CHECK_NEAR (lfo.phase(), 1.0, 1.0e-12);

    // Retriggered by the next note; Custom holds y[last]; S&H holds its last value.
    lfo.noteOn (2);
    CHECK_NEAR (lfo.next (32), -1.0, 1.0e-6);

    LfoShape shape;
    shape.points = { { 0.0f, 0.0f, 0.0f }, { 0.5f, 1.0f, 0.0f }, { 1.0f, 0.75f, 0.0f } };
    Lfo custom;
    custom.prepare (kSr);
    custom.setCustomShape (&shape);
    custom.set (lfoParams (LfoWave::Custom, 4.0f, LfoMode::OneShot));
    custom.noteOn (5);
    const auto cv = runBlocks (custom, 2000);
    CHECK_NEAR (cv.back(), 0.5, 1.0e-6);   // 2 * 0.75 - 1

    Lfo sh;
    sh.prepare (kSr);
    sh.set (lfoParams (LfoWave::SampleHold, 8.0f, LfoMode::OneShot));
    sh.noteOn (9);
    const auto sv = runBlocks (sh, 3000);
    bool shHolds = true;
    for (size_t i = (size_t) (0.2 * kSr / kControlInterval); i < sv.size(); ++i) shHolds = shHolds && sv[i] == sv.back();
    CHECK (shHolds);
}

AP_TEST (lfo_free_mode_follows_transport)
{
    LfoParams p = lfoParams (LfoWave::Sine, 1.0f, LfoMode::Free);
    p.sync = true;
    p.division = ap::NoteDivision::Quarter;

    Lfo a, b;
    a.prepare (kSr); b.prepare (kSr);
    a.set (p); b.set (p);
    a.noteOn (1);   // different note history must not matter
    a.next (5000);

    Transport t;
    t.bpm = 120.0; t.hasTempo = true; t.playing = true; t.ppq = 3.25;
    a.syncToTransport (t);
    b.syncToTransport (t);
    CHECK_NEAR (a.phase(), 0.25, 1.0e-9);
    CHECK_NEAR (b.phase(), 0.25, 1.0e-9);

    // The two agree over many blocks of a playing transport, and noteOn does not reset.
    bool agree = true;
    for (int blk = 0; blk < 200; ++blk)
    {
        a.syncToTransport (t);
        b.syncToTransport (t);
        if (blk == 50) b.noteOn (77);
        for (int c = 0; c < 512 / kControlInterval; ++c)
            agree = agree && a.next (kControlInterval) == b.next (kControlInterval);
        t.ppq += 512.0 / kSr * (t.bpm / 60.0);
        t.sampleTime += 512;
    }
    CHECK (agree);
    CHECK_NEAR (a.phase(), std::fmod (t.ppq, 1.0), 1.0e-6);

    // Start phase offsets the transport phase; ppq 1/2 division.
    p.startPhase = 0.5f;
    p.division = ap::NoteDivision::Half;
    a.set (p);
    t.ppq = 1.0;
    a.syncToTransport (t);
    CHECK_NEAR (a.phase(), 0.0, 1.0e-9);   // 1 beat / 2 beats = 0.5, + 0.5 -> wraps to 0

    // Stopped / unsynced: a running clock from the sample time.
    p = lfoParams (LfoWave::Sine, 1.0f, LfoMode::Free);
    a.set (p);
    t.playing = false;
    t.sampleTime = 12000;
    a.syncToTransport (t);
    CHECK_NEAR (a.phase(), 0.25, 1.0e-9);
    a.noteOn (3);
    CHECK_NEAR (a.phase(), 0.25, 1.0e-9);
}

AP_TEST (lfo_unipolar_fade_smooth)
{
    // Unipolar range 0..1.
    {
        Lfo lfo;
        lfo.prepare (kSr);
        LfoParams p = lfoParams (LfoWave::Sine, 5.0f);
        p.unipolar = true;
        lfo.set (p);
        lfo.noteOn (1);
        const auto v = runBlocks (lfo, 3000);
        const auto [mn, mx] = std::minmax_element (v.begin(), v.end());
        CHECK (*mn >= 0.0f && *mx <= 1.0f);
        CHECK_NEAR (*mn, 0.0, 0.01);
        CHECK_NEAR (*mx, 1.0, 0.01);
    }

    // Fade reaches full depth at fadeMs +- 5 % (square: |value| is the fade gain).
    for (float fadeMs : { 50.0f, 200.0f, 1000.0f })
    {
        Lfo lfo;
        lfo.prepare (kSr);
        LfoParams p = lfoParams (LfoWave::Square, 3.0f);
        p.fadeMs = fadeMs;
        lfo.set (p);
        lfo.noteOn (1);
        const auto v = runBlocks (lfo, (int) (2.0 * fadeMs * 0.001 * kSr / kControlInterval) + 10);
        int full = -1;
        for (size_t i = 0; i < v.size(); ++i)
            if (std::fabs (v[i]) >= 0.999f) { full = (int) i; break; }
        const double fullMs = (double) full * kControlInterval / kSr * 1000.0;
        CHECK_NEAR (fullMs, fadeMs, fadeMs * 0.05);
        CHECK (std::fabs (v[0]) < 1.0e-6f);
        // Halfway: half depth.
        const size_t half = (size_t) std::lround (0.5 * fadeMs * 0.001 * kSr / kControlInterval);
        CHECK_NEAR (std::fabs (v[half]), 0.5, 0.02);
    }

    // Smooth removes S&H steps: max |delta| drops by more than half.
    {
        const auto maxDelta = [] (float smooth)
        {
            Lfo lfo;
            lfo.prepare (kSr);
            LfoParams p = lfoParams (LfoWave::SampleHold, 5.0f);
            p.smooth = smooth;
            lfo.set (p);
            lfo.noteOn (11);
            const auto v = runBlocks (lfo, (int) (3.0 * kSr / kControlInterval));
            float m = 0.0f;
            for (size_t i = 1; i < v.size(); ++i) m = std::max (m, std::fabs (v[i] - v[i - 1]));
            return m;
        };
        const float raw = maxDelta (0.0f);
        const float smoothed = maxDelta (0.5f);
        CHECK (raw > 0.2f);
        CHECK (smoothed < 0.5f * raw);
    }

    // Changing the rate mid-run does not reset the phase.
    {
        Lfo lfo;
        lfo.prepare (kSr);
        lfo.set (lfoParams (LfoWave::SawUp, 1.0f));
        lfo.noteOn (1);
        lfo.next (12000);
        const double before = lfo.phase();
        lfo.set (lfoParams (LfoWave::SawUp, 4.0f));
        CHECK_NEAR (lfo.phase(), before, 1.0e-12);
        CHECK_NEAR (lfo.next (1), 2.0 * before - 1.0, 1.0e-6);
    }
}
