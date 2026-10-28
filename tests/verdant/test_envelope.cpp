// Envelope (CONTRACT §3 Envelopes, §8 envelope tests).

#include "TestHarness.h"

#include "engine/Envelope.h"

#include <ap/Math.h>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace verdant;

namespace
{
    constexpr double kSr = 48000.0;

    int msToSamples (double ms) { return (int) std::lround (ms * 0.001 * kSr); }

    EnvParams params (float a, float h, float d, float s, float r, float ac = 0.0f, float dc = -0.4f)
    {
        EnvParams p;
        p.attackMs = a; p.holdMs = h; p.decayMs = d; p.sustain = s; p.releaseMs = r;
        p.attackCurve = ac; p.decayCurve = dc;
        return p;
    }

    /** Renders `total` samples, note-off at `offAt` (or never when < 0). */
    std::vector<float> render (const EnvParams& p, int total, int offAt, std::vector<Envelope::Stage>* stages = nullptr)
    {
        Envelope e;
        e.prepare (kSr);
        e.set (p);
        e.noteOn();
        std::vector<float> v ((size_t) total);
        if (stages != nullptr) stages->resize ((size_t) total);
        for (int i = 0; i < total; ++i)
        {
            if (i == offAt) e.noteOff();
            v[(size_t) i] = e.next();
            if (stages != nullptr) (*stages)[(size_t) i] = e.stage();
        }
        return v;
    }

    /** Sample index (1-based time in samples) of the first value matching `pred`, or -1. */
    template <typename Pred>
    int firstIndex (const std::vector<float>& v, int from, Pred pred)
    {
        for (int i = from; i < (int) v.size(); ++i)
            if (pred (v[(size_t) i])) return i + 1;
        return -1;
    }
}

AP_TEST (envelope_stage_times_within_5_percent)
{
    // A 100 / H 50 / D 200 / S 50 % / R 300 ms, note-off at 1 s.
    std::vector<Envelope::Stage> st;
    const auto v = render (params (100, 50, 200, 0.5f, 300), msToSamples (1400), msToSamples (1000), &st);

    const int attackDone = firstIndex (v, 0, [] (float x) { return x >= 0.999f; });
    CHECK_NEAR (attackDone, msToSamples (100), msToSamples (5));

    // Holds at 1 through the hold time.
    bool held = true;
    for (int i = msToSamples (100); i < msToSamples (150) - 1; ++i)
        held = held && v[(size_t) i] == 1.0f;
    CHECK (held);

    // Decay reaches the sustain level at 350 ms and stays there.
    const int sustainReached = firstIndex (v, msToSamples (150), [] (float x) { return std::fabs (x - 0.5f) <= 0.01f; });
    CHECK_NEAR (sustainReached, msToSamples (350), msToSamples (17.5));
    CHECK_NEAR (v[(size_t) msToSamples (360)], 0.5, 1.0e-6);
    CHECK (st[(size_t) msToSamples (400)] == Envelope::Stage::Sustain);

    // Release reaches 0 at 1.3 s, then Idle.
    const int zero = firstIndex (v, msToSamples (1000), [] (float x) { return x <= 1.0e-4f; });
    CHECK_NEAR (zero, msToSamples (1300), msToSamples (15));
    CHECK (st[(size_t) msToSamples (1300) + 2] == Envelope::Stage::Idle);
    CHECK (st[(size_t) msToSamples (1250)] == Envelope::Stage::Release);
    CHECK_EQ (v.back(), 0.0f);

    // Exact stage boundaries: attack ends on its sample, release on its sample.
    CHECK_EQ (v[(size_t) msToSamples (100) - 1], 1.0f);
    CHECK (v[(size_t) msToSamples (100) - 2] < 1.0f);
}

AP_TEST (envelope_curves_and_monotonicity)
{
    // Attack curve +1 (fast start) above linear at t = 0.25, -1 below, 0 on it.
    const int quarter = msToSamples (25);
    const float fast   = render (params (100, 0, 100, 1, 100,  1.0f), quarter, -1).back();
    const float slow   = render (params (100, 0, 100, 1, 100, -1.0f), quarter, -1).back();
    const float linear = render (params (100, 0, 100, 1, 100,  0.0f), quarter, -1).back();
    CHECK_NEAR (linear, 0.25, 0.001);
    CHECK (fast > linear + 0.1f);
    CHECK (slow < linear - 0.1f);
    CHECK_NEAR (Envelope::shape (0.25f, 1.0f), std::pow (0.25, 0.25), 1.0e-6);
    CHECK_NEAR (Envelope::shape (0.25f, -1.0f), std::pow (0.25, 4.0), 1.0e-6);

    // Decay curve -1 falls below the linear decay at 25 % of the segment.
    const int decayQuarter = msToSamples (50);   // attack 0 takes one sample
    const float expo = render (params (0, 0, 200, 0.2f, 100, 0.0f, -1.0f), decayQuarter + 1, -1).back();
    const float lin  = render (params (0, 0, 200, 0.2f, 100, 0.0f,  0.0f), decayQuarter + 1, -1).back();
    const float inv  = render (params (0, 0, 200, 0.2f, 100, 0.0f,  1.0f), decayQuarter + 1, -1).back();
    CHECK_NEAR (lin, 1.0 - 0.25 * 0.8, 0.002);
    CHECK (expo < lin - 0.1f);
    CHECK (inv > lin + 0.1f);

    // Every segment is monotonic for any curve.
    bool mono = true;
    for (float ac : { -1.0f, -0.4f, 0.0f, 0.5f, 1.0f })
        for (float dc : { -1.0f, -0.4f, 0.0f, 0.5f, 1.0f })
        {
            const int a = msToSamples (20), d = msToSamples (30), off = msToSamples (80);
            const auto v = render (params (20, 0, 30, 0.4f, 40, ac, dc), msToSamples (130), off);
            for (int i = 1; i < a; ++i)                mono = mono && v[(size_t) i] >= v[(size_t) i - 1];
            for (int i = a + 1; i < a + d; ++i)        mono = mono && v[(size_t) i] <= v[(size_t) i - 1];
            for (int i = off + 1; i < (int) v.size(); ++i) mono = mono && v[(size_t) i] <= v[(size_t) i - 1];
            for (float x : v)                          mono = mono && x >= 0.0f && x <= 1.0f;
        }
    CHECK (mono);
}

AP_TEST (envelope_retrigger_sustain0_attack0)
{
    // Retrigger mid-decay: the attack restarts from the current level, no step > 0.01.
    {
        Envelope e;
        e.prepare (kSr);
        e.set (params (10, 0, 300, 0.5f, 200));
        e.noteOn();
        float prev = 0.0f, maxStep = 0.0f;
        for (int i = 0; i < msToSamples (600); ++i)
        {
            if (i == msToSamples (150)) e.noteOn();
            const float v = e.next();
            if (i > 0) maxStep = std::max (maxStep, std::fabs (v - prev));
            prev = v;
        }
        CHECK (maxStep < 0.01f);
        CHECK_NEAR (prev, 0.5, 1.0e-6);
    }

    // Sustain 0: Idle when the decay ends (frees the voice).
    {
        std::vector<Envelope::Stage> st;
        const auto v = render (params (5, 0, 50, 0.0f, 300), msToSamples (100), -1, &st);
        CHECK (st[(size_t) msToSamples (50)] == Envelope::Stage::Decay);
        CHECK (st[(size_t) msToSamples (56)] == Envelope::Stage::Idle);
        CHECK_EQ (v[(size_t) msToSamples (60)], 0.0f);
    }

    // Attack 0: the first sample is already 1.
    {
        Envelope e;
        e.prepare (kSr);
        e.set (params (0, 0, 100, 0.5f, 100));
        e.noteOn();
        CHECK_EQ (e.next(), 1.0f);
    }

    // kill() -> Idle immediately; noteOff when idle does nothing.
    {
        Envelope e;
        e.prepare (kSr);
        e.set (params (5, 0, 100, 0.5f, 100));
        e.noteOn();
        e.skip (1000);
        CHECK (e.isActive());
        e.kill();
        CHECK (! e.isActive());
        CHECK_EQ (e.value(), 0.0f);
        e.noteOff();
        CHECK (e.stage() == Envelope::Stage::Idle);
    }
}

AP_TEST (envelope_skip_matches_next)
{
    const EnvParams p = params (30, 10, 80, 0.6f, 120, 0.5f, -0.7f);
    Envelope a, b;
    a.prepare (kSr); b.prepare (kSr);
    a.set (p); b.set (p);
    a.noteOn(); b.noteOn();

    // Varied chunk sizes, a note-off, a retrigger and a live sustain change.
    const int total = msToSamples (600);
    int i = 0, chunk = 1;
    double maxDiff = 0.0;
    bool stagesMatch = true;
    while (i < total)
    {
        const int n = std::min (chunk, total - i);
        for (int k = 0; k < n; ++k)
        {
            const int s = i + k;
            if (s == msToSamples (200)) { EnvParams q = p; q.sustain = 0.3f; a.set (q); }
            if (s == msToSamples (300)) a.noteOff();
            if (s == msToSamples (350)) a.noteOn();
            if (s == msToSamples (500)) a.noteOff();
            a.next();
        }
        // Mirror the same events on b at chunk granularity (events land on chunk starts below).
        b.skip (n);
        i += n;
        maxDiff = std::max (maxDiff, (double) std::fabs (a.value() - b.value()));
        stagesMatch = stagesMatch && a.stage() == b.stage();
        if (i == msToSamples (200)) { EnvParams q = p; q.sustain = 0.3f; b.set (q); }
        if (i == msToSamples (300)) b.noteOff();
        if (i == msToSamples (350)) b.noteOn();
        if (i == msToSamples (500)) b.noteOff();
        chunk = chunk % 37 + 1;
        // Keep event samples on chunk boundaries so both see them at the same time.
        for (int ev : { msToSamples (200), msToSamples (300), msToSamples (350), msToSamples (500) })
            if (i < ev && i + chunk > ev) chunk = ev - i;
    }
    CHECK (maxDiff < 1.0e-6);
    CHECK (stagesMatch);
}

AP_TEST (envelope_set_during_note_does_not_jump)
{
    // Times latch at note-on: changing them mid-attack changes nothing until the next note.
    const EnvParams p = params (200, 0, 100, 0.5f, 100);
    Envelope a, b;
    a.prepare (kSr); b.prepare (kSr);
    a.set (p); b.set (p);
    a.noteOn(); b.noteOn();
    double maxDiff = 0.0;
    for (int i = 0; i < msToSamples (300); ++i)
    {
        if (i == msToSamples (50)) b.set (params (5, 0, 2000, 0.5f, 3000, 1.0f, 1.0f));
        maxDiff = std::max (maxDiff, (double) std::fabs (a.next() - b.next()));
    }
    CHECK (maxDiff < 1.0e-7);

    // Sustain follows live, slewed (no step bigger than the 10 ms full-scale slew).
    Envelope e;
    e.prepare (kSr);
    e.set (params (1, 0, 10, 0.8f, 100));
    e.noteOn();
    e.skip (msToSamples (50));
    CHECK_NEAR (e.value(), 0.8, 1.0e-6);
    e.set (params (1, 0, 10, 0.2f, 100));
    float prev = e.value(), maxStep = 0.0f;
    for (int i = 0; i < msToSamples (20); ++i)
    {
        const float v = e.next();
        maxStep = std::max (maxStep, std::fabs (v - prev));
        prev = v;
    }
    CHECK_NEAR (prev, 0.2, 1.0e-6);
    CHECK (maxStep <= 1.0f / (0.01f * (float) kSr) + 1.0e-6f);
}

AP_TEST (envelope_finite_for_hostile_params)
{
    bool ok = true;
    const float hostile[] = { 0.0f, -10.0f, 1.0e9f, std::nanf (""), INFINITY };
    for (float t : hostile)
        for (float s : { -1.0f, 0.5f, 2.0f, std::nanf ("") })
        {
            Envelope e;
            e.prepare (kSr);
            e.set (params (t, t, t, s, t, t, t));
            e.noteOn();
            for (int i = 0; i < 2000; ++i)
            {
                if (i == 1000) e.noteOff();
                const float v = e.next();
                ok = ok && std::isfinite (v) && v >= 0.0f && v <= 1.0f;
            }
        }
    CHECK (ok);
}
