#include "TestHarness.h"

#include <ap/Dynamics.h>
#include <ap/Tempo.h>

#include <cmath>

using namespace ap;

AP_TEST (gain_computer_hard_knee)
{
    GainComputer gc;
    gc.thresholdDb = -20.0f;
    gc.ratio = 4.0f;
    gc.kneeDb = 0.0f;
    CHECK_NEAR (gc.gainReductionDb (-30.0f), 0.0f, 1e-6);
    CHECK_NEAR (gc.gainReductionDb (-20.0f), 0.0f, 1e-6);
    CHECK_NEAR (gc.gainReductionDb (-10.0f), -7.5f, 1e-5);   // 10 dB over, 4:1 -> 2.5 out, 7.5 reduction
    CHECK_NEAR (gc.gainReductionDb (0.0f), -15.0f, 1e-5);
}

AP_TEST (gain_computer_soft_knee_is_smooth_and_monotonic)
{
    GainComputer gc;
    gc.thresholdDb = -18.0f;
    gc.ratio = 3.0f;
    gc.kneeDb = 8.0f;

    float prev = gc.gainReductionDb (-40.0f);
    for (float level = -40.0f; level <= 0.0f; level += 0.25f)
    {
        const float gr = gc.gainReductionDb (level);
        CHECK (gr <= prev + 1e-6f);       // more level never means less reduction
        CHECK (gr <= 0.0f);
        prev = gr;
    }
    // Knee edges coincide with the hard-knee curve.
    CHECK_NEAR (gc.gainReductionDb (-22.0f), 0.0f, 1e-5);
    CHECK_NEAR (gc.gainReductionDb (-14.0f), -(1.0f - 1.0f / 3.0f) * 4.0f, 1e-4);
    // Midpoint of the knee is between the two linear pieces.
    const float mid = gc.gainReductionDb (-18.0f);
    CHECK (mid < 0.0f && mid > -(1.0f - 1.0f / 3.0f) * 4.0f);
}

AP_TEST (gain_computer_limiter_ratio)
{
    GainComputer gc;
    gc.thresholdDb = -6.0f;
    gc.ratio = 1000.0f;
    gc.kneeDb = 0.0f;
    CHECK_NEAR (gc.gainReductionDb (0.0f), -6.0f, 0.01f);
}

AP_TEST (envelope_follower_tracks)
{
    EnvelopeFollower ef;
    ef.prepare (48000.0);
    ef.setTimes (1.0f, 50.0f);

    float v = 0.0f;
    for (int i = 0; i < 480; ++i) v = ef.process (0.5f);   // 10 ms of a 0.5 step
    CHECK_NEAR (v, 0.5f, 0.001f);

    for (int i = 0; i < 2400; ++i) v = ef.process (0.0f);  // 50 ms release = one time constant
    CHECK_NEAR (v, 0.5f * std::exp (-1.0f), 0.01f);
}

AP_TEST (ghost_envelope_shape)
{
    GhostDuckEnvelope env;
    GhostDuckEnvelope::Shape s;
    s.attackMs = 10.0f;
    s.holdMs = 20.0f;
    s.releaseMs = 100.0f;
    s.depthDb = 12.0f;
    s.curve = 0.0f;
    env.setShape (s);

    const float floorGain = dbToGain (-12.0f);
    CHECK_NEAR (env.gainAt (-1.0), 1.0f, 1e-6);
    CHECK_NEAR (env.gainAt (0.0), 1.0f, 1e-6);
    CHECK_NEAR (env.gainAt (10.0), floorGain, 1e-5);
    CHECK_NEAR (env.gainAt (25.0), floorGain, 1e-5);
    CHECK_NEAR (env.gainAt (80.0), lerp (1.0f, floorGain, 0.5f), 1e-4);   // halfway through linear release
    CHECK_NEAR (env.gainAt (200.0), 1.0f, 1e-6);

    // Beat-locked lookup: 120 bpm, quarter notes -> one cycle = 500 ms.
    CHECK_NEAR (env.gainAtBeat (4.0, 120.0, NoteDivision::Quarter), 1.0f, 1e-6);          // on the beat
    CHECK_NEAR (env.gainAtBeat (4.02, 120.0, NoteDivision::Quarter), floorGain, 1e-5);    // 10 ms in -> full depth
    CHECK_NEAR (env.gainAtBeat (4.9, 120.0, NoteDivision::Quarter), 1.0f, 1e-6);          // 450 ms in -> recovered
}

AP_TEST (note_division_table)
{
    CHECK_NEAR (noteDivisionBeats (NoteDivision::Quarter), 1.0, 1e-12);
    CHECK_NEAR (noteDivisionBeats (NoteDivision::EighthTriplet), 1.0 / 3.0, 1e-12);
    CHECK_NEAR (noteDivisionBeats (NoteDivision::QuarterDotted), 1.5, 1e-12);
    for (int i = 0; i < (int) NoteDivision::Count; ++i)
        CHECK (noteDivisionName ((NoteDivision) i)[0] != '?');
}
