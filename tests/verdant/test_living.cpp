// Living offsets: exact identity at alive = evolve = 0, per-note values inside
// the CONTRACT bounds and spanning them, linear in alive, seeded determinism,
// evolve drift bounded and slow over a minute, 'Alive' source silent at 0.

#include "TestHarness.h"

#include "engine/Living.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

using namespace verdant;

namespace
{
    constexpr double kSr = 48000.0;

    struct Range
    {
        float lo = 1.0e9f, hi = -1.0e9f;
        void add (float v) { lo = std::min (lo, v); hi = std::max (hi, v); }
    };

    bool sameOffsets (const LivingOffsets& a, const LivingOffsets& b)
    {
        return std::memcmp (&a, &b, sizeof (LivingOffsets)) == 0;
    }

    bool isIdentity (const LivingOffsets& o)
    {
        return o.pitchCents == 0.0f && o.position == 0.0f && o.cutoffOctaves == 0.0f
            && o.attackMul == 1.0f && o.pan == 0.0f && o.drift == 0.0f;
    }
}

AP_TEST (living_identity_at_zero)
{
    LivingVoice v;
    v.prepare (kSr);
    bool identity = true;
    for (uint32_t n = 0; n < 1000; ++n)
    {
        v.noteOn (0x5EED1234u, n, 0.0f, 0.0f);
        identity = identity && isIdentity (v.offsets());
        v.advance (4096);
        identity = identity && isIdentity (v.offsets());
    }
    CHECK (identity);
}

AP_TEST (living_alive_bounds_and_span)
{
    for (float alive : { 1.0f, 0.5f })
    {
        LivingVoice v;
        v.prepare (kSr);
        Range pitch, pos, cut, atk, pan;
        bool inside = true;
        for (uint32_t n = 0; n < 2000; ++n)
        {
            v.noteOn (0xC0FFEEu, n, alive, 0.0f);
            const auto& o = v.offsets();
            pitch.add (o.pitchCents);
            pos.add (o.position);
            cut.add (o.cutoffOctaves);
            atk.add (o.attackMul);
            pan.add (o.pan);
            inside = inside && std::fabs (o.pitchCents) <= 10.0f * alive
                            && std::fabs (o.position) <= 0.08f * alive
                            && std::fabs (o.cutoffOctaves) <= 0.5f * alive
                            && o.attackMul >= 1.0f - 0.3f * alive && o.attackMul <= 1.0f + 0.3f * alive
                            && std::fabs (o.pan) <= 0.25f * alive;
        }
        CHECK (inside);

        // The draws cover at least 80 % of each range.
        CHECK (pitch.hi - pitch.lo >= 0.8f * 20.0f * alive);
        CHECK (pos.hi - pos.lo >= 0.8f * 0.16f * alive);
        CHECK (cut.hi - cut.lo >= 0.8f * 1.0f * alive);
        CHECK (atk.hi - atk.lo >= 0.8f * 0.6f * alive);
        CHECK (pan.hi - pan.lo >= 0.8f * 0.5f * alive);
    }

    // Linear in alive: the same note at 0.5 is exactly half of 1.0.
    LivingVoice a, b;
    a.prepare (kSr);
    b.prepare (kSr);
    for (uint32_t n = 0; n < 100; ++n)
    {
        a.noteOn (7u, n, 1.0f, 0.0f);
        b.noteOn (7u, n, 0.5f, 0.0f);
        CHECK_NEAR (b.offsets().pitchCents, 0.5 * a.offsets().pitchCents, 1.0e-5);
        CHECK_NEAR (b.offsets().attackMul - 1.0, 0.5 * (a.offsets().attackMul - 1.0), 1.0e-6);
    }
}

AP_TEST (living_seeded_determinism)
{
    LivingVoice a, b;
    a.prepare (kSr);
    b.prepare (44100.0);   // per-note values do not depend on the sample rate
    a.noteOn (0x5EED1234u, 42, 0.8f, 0.0f);
    b.noteOn (0x5EED1234u, 42, 0.8f, 0.0f);
    CHECK (sameOffsets (a.offsets(), b.offsets()));

    // Different counter or different global seed -> different variation.
    int differs = 0;
    for (uint32_t n = 0; n < 100; ++n)
    {
        a.noteOn (0x5EED1234u, n, 1.0f, 0.0f);
        b.noteOn (0x5EED1234u, n + 1, 1.0f, 0.0f);
        if (a.offsets().pitchCents != b.offsets().pitchCents && a.offsets().pan != b.offsets().pan) ++differs;
    }
    CHECK_EQ (differs, 100);
    a.noteOn (1u, 5, 1.0f, 0.0f);
    b.noteOn (2u, 5, 1.0f, 0.0f);
    CHECK (! sameOffsets (a.offsets(), b.offsets()));
    CHECK (LivingVoice::noteSeed (1u, 5u) != LivingVoice::noteSeed (2u, 5u));

    // Whole trajectories (with evolve) are bit-identical for the same seed and
    // block pattern, and independent of how advance() is chunked.
    LivingVoice c, d;
    c.prepare (kSr);
    d.prepare (kSr);
    c.noteOn (99u, 3u, 0.6f, 1.0f);
    d.noteOn (99u, 3u, 0.6f, 1.0f);
    bool same = true;
    for (int i = 0; i < 2000; ++i)
    {
        c.advance (96);
        d.advance (50);
        d.advance (46);
        same = same && sameOffsets (c.offsets(), d.offsets());
    }
    CHECK (same);
}

AP_TEST (living_evolve_bounded_and_slow)
{
    LivingVoice v;
    v.prepare (kSr);
    v.noteOn (0xABCDu, 1u, 0.0f, 1.0f);
    // Drift starts at zero: no jump at note-on.
    CHECK (isIdentity (v.offsets()));

    Range pitch, pos, cut, pan, drift;
    LivingOffsets prev = v.offsets();
    float maxDPitch = 0, maxDPos = 0, maxDCut = 0, maxDPan = 0;
    bool inside = true;
    const int blocks = (int) (60.0 * kSr / kControlInterval);   // 60 s of control blocks
    for (int i = 0; i < blocks; ++i)
    {
        v.advance (kControlInterval);
        const auto& o = v.offsets();
        inside = inside && std::fabs (o.pitchCents) <= 5.0f && std::fabs (o.position) <= 0.15f
                        && std::fabs (o.cutoffOctaves) <= 0.5f && std::fabs (o.pan) <= 0.2f
                        && std::fabs (o.drift) <= 1.0f && o.attackMul == 1.0f
                        && std::isfinite (o.pitchCents) && std::isfinite (o.drift);
        maxDPitch = std::max (maxDPitch, std::fabs (o.pitchCents - prev.pitchCents));
        maxDPos   = std::max (maxDPos, std::fabs (o.position - prev.position));
        maxDCut   = std::max (maxDCut, std::fabs (o.cutoffOctaves - prev.cutoffOctaves));
        maxDPan   = std::max (maxDPan, std::fabs (o.pan - prev.pan));
        pitch.add (o.pitchCents);
        pos.add (o.position);
        cut.add (o.cutoffOctaves);
        pan.add (o.pan);
        drift.add (o.drift);
        prev = o;
    }
    CHECK (inside);

    // Slow: per control block (0.67 ms) every offset moves < 1 % of its range.
    CHECK (maxDPitch < 0.01f * 10.0f);
    CHECK (maxDPos   < 0.01f * 0.30f);
    CHECK (maxDCut   < 0.01f * 1.0f);
    CHECK (maxDPan   < 0.01f * 0.40f);

    // ...but it does move over a minute (uses a good part of its range).
    CHECK (pitch.hi - pitch.lo > 0.25f * 10.0f);
    CHECK (pos.hi - pos.lo > 0.25f * 0.30f);
    CHECK (cut.hi - cut.lo > 0.25f * 1.0f);
    CHECK (pan.hi - pan.lo > 0.25f * 0.40f);
    CHECK (drift.hi - drift.lo > 0.25f * 2.0f);

    // Evolve 0.5 halves the bounds (alive 0 keeps the per-note part at zero).
    LivingVoice half;
    half.prepare (kSr);
    half.noteOn (0xABCDu, 1u, 0.0f, 0.5f);
    bool halfInside = true;
    for (int i = 0; i < blocks / 4; ++i)
    {
        half.advance (kControlInterval);
        const auto& o = half.offsets();
        halfInside = halfInside && std::fabs (o.pitchCents) <= 2.5f && std::fabs (o.position) <= 0.075f
                                && std::fabs (o.cutoffOctaves) <= 0.25f && std::fabs (o.pan) <= 0.1f
                                && std::fabs (o.drift) <= 0.5f;
    }
    CHECK (halfInside);
}

AP_TEST (living_alive_plus_evolve_and_drift_source)
{
    // Both at 1: per-note + drift stays inside the summed bounds.
    LivingVoice v;
    v.prepare (kSr);
    bool inside = true;
    for (uint32_t n = 0; n < 50; ++n)
    {
        v.noteOn (0x1234u, n, 1.0f, 1.0f);
        const float attack = v.offsets().attackMul;
        for (int i = 0; i < 3000; ++i)
        {
            v.advance (kControlInterval);
            const auto& o = v.offsets();
            inside = inside && std::fabs (o.pitchCents) <= 15.0f && std::fabs (o.position) <= 0.23f
                            && std::fabs (o.cutoffOctaves) <= 1.0f && std::fabs (o.pan) <= 0.45f
                            && o.attackMul == attack && std::fabs (o.drift) <= 1.0f;
        }
    }
    CHECK (inside);

    // The 'Alive' mod source moves with alive alone (scaled by max (alive, evolve)).
    LivingVoice a;
    a.prepare (kSr);
    a.noteOn (0x77u, 9u, 0.4f, 0.0f);
    Range r;
    for (int i = 0; i < 90000; ++i)
    {
        a.advance (kControlInterval);
        r.add (a.offsets().drift);
        CHECK (std::fabs (a.offsets().drift) <= 0.4f);
        if (std::fabs (a.offsets().drift) > 0.4f) break;
    }
    CHECK (r.hi - r.lo > 0.05f);
    // ...and only the drift moves: pitch etc. keep their per-note values.
    LivingVoice ref;
    ref.prepare (kSr);
    ref.noteOn (0x77u, 9u, 0.4f, 0.0f);
    CHECK_EQ (a.offsets().pitchCents, ref.offsets().pitchCents);
    CHECK_EQ (a.offsets().pan, ref.offsets().pan);
}
