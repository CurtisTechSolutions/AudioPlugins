// Physics mod sources: bounded, deterministic, physically plausible.
// Ball: drop time sqrt (2h/g), apexes shrink by e^2 per bounce, comes to rest.
// Spring: bounded, first peak = velocity, decays in ~5 time constants, no
// overshoot when critically damped. Orbit: stays on its ellipse, period 1/f.

#include "TestHarness.h"

#include "engine/Common.h"
#include "engine/Physics.h"

#include <ap/Math.h>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace verdant;

namespace
{
    constexpr double kSr = 48000.0;
    constexpr float kDt = (float) (kControlInterval / kSr);
}

AP_TEST (physics_ball_drop_time_and_apexes)
{
    for (float gravity : { 0.0f, 0.3f, 1.0f })
    {
        BouncingBall ball;
        ball.set (gravity, 0.8f);
        ball.trigger (1.0f);
        CHECK_NEAR (ball.height(), 1.0, 1.0e-6);

        const double g = ball.gravityUnits();
        const double e = ball.restitution();
        const double expectedFall = std::sqrt (2.0 * 1.0 / g);

        // Run until rest, recording the first impact (first local minimum) and
        // every apex (local maximum) of the sampled height.
        double t = 0.0, h1 = ball.height(), h2 = h1;
        double impact = -1.0;
        bool bounded = true;
        std::vector<double> apexes;
        int steps = 0;
        while (! ball.atRest() && steps < 2000000)
        {
            ball.advance (kDt);
            t += kDt;
            ++steps;
            const double h = ball.height();
            bounded = bounded && std::isfinite (h) && h >= 0.0 && h <= 1.0;
            if (impact < 0.0 && h > h1 && h1 <= h2)
                impact = t - kDt;
            if (h1 > h2 && h1 >= h)
                apexes.push_back (h1);
            h2 = h1;
            h1 = h;
        }
        CHECK (bounded);
        CHECK (ball.atRest());
        CHECK (impact > 0.0);
        // Impact is detected within a couple of control steps of the true time.
        CHECK_NEAR (impact, expectedFall, 0.1 * expectedFall);

        // Apex ratio ~ e^2 for the first few bounces (large enough to sample well).
        CHECK (apexes.size() >= 3);
        double prevApex = 1.0;
        for (size_t k = 0; k < std::min<size_t> (apexes.size(), 4); ++k)
        {
            if (apexes[k] < 0.02) break;
            CHECK_NEAR (apexes[k] / prevApex, e * e, 0.03);
            prevApex = apexes[k];
        }

        // At rest it stays at 0.
        for (int i = 0; i < 1000; ++i) ball.advance (kDt);
        CHECK_EQ (ball.height(), 0.0f);
    }

    // Velocity sets the drop height: 0.3 + 0.7 v.
    BouncingBall b2;
    b2.set (0.5f, 0.5f);
    b2.trigger (0.0f);
    CHECK_NEAR (b2.height(), 0.3, 1.0e-6);
    b2.trigger (0.5f);
    CHECK_NEAR (b2.height(), 0.65, 1.0e-6);

    // Before any trigger it rests at 0.
    BouncingBall fresh;
    fresh.advance (kDt);
    CHECK_EQ (fresh.height(), 0.0f);
}

AP_TEST (physics_ball_large_dt_and_determinism)
{
    // One 10 ms step or 15 control steps of ~0.67 ms land on the same exact arc.
    BouncingBall a, b;
    a.set (0.6f, 0.9f);
    b.set (0.6f, 0.9f);
    a.trigger (0.7f);
    b.trigger (0.7f);
    bool same = true;
    for (int i = 0; i < 300; ++i)
    {
        a.advance (0.01f);
        for (int k = 0; k < 10; ++k) b.advance (0.001f);
        same = same && std::fabs (a.height() - b.height()) < 1.0e-4f;
        CHECK (a.height() >= 0.0f && a.height() <= 1.0f);
    }
    CHECK (same);

    // Garbage dt does not break it.
    a.trigger (1.0f);
    a.advance (-1.0f);
    a.advance (std::nanf (""));
    a.advance (1.0e9f);
    CHECK (std::isfinite (a.height()) && a.height() >= 0.0f && a.height() <= 1.0f);
}

AP_TEST (physics_spring_bounded_and_decays)
{
    for (float stiffness : { 0.0f, 0.5f, 1.0f })
    {
        for (float damping : { 0.0f, 0.3f, 0.7f })
        {
            Spring s;
            s.set (stiffness, damping);
            s.trigger (1.0f);
            CHECK_EQ (s.displacement(), 0.0f);   // impulse: no jump in the output

            const double tau = 1.0 / (s.dampingRatio() * s.omega());
            double t = 0.0, peak = 0.0, firstPeak = -1.0, prev = 0.0, maxAfter = 0.0;
            bool bounded = true;
            while (t < 8.0 * tau + 0.1)
            {
                s.advance (kDt);
                t += kDt;
                const double x = s.displacement();
                bounded = bounded && std::isfinite (x) && x >= -1.0 && x <= 1.0;
                if (firstPeak < 0.0 && x < prev) firstPeak = prev;
                peak = std::max (peak, std::fabs (x));
                if (t > 6.0 * tau) maxAfter = std::max (maxAfter, std::fabs (x));
                prev = x;
            }
            CHECK (bounded);
            CHECK_NEAR (firstPeak, 1.0, 0.01);       // normalised: first peak = velocity
            CHECK (peak <= 1.0 + 1.0e-6);
            CHECK (maxAfter < 0.01);                  // ~5-6 time constants
        }
    }

    // Half velocity, half the peak.
    Spring h;
    h.set (0.5f, 0.3f);
    h.trigger (0.5f);
    double pk = 0.0;
    for (int i = 0; i < 2000; ++i) { h.advance (kDt); pk = std::max (pk, (double) h.displacement()); }
    CHECK_NEAR (pk, 0.5, 0.01);
}

AP_TEST (physics_spring_critical_no_overshoot_and_stability)
{
    Spring s;
    s.set (0.8f, 1.0f);   // zeta = 1
    CHECK_NEAR (s.dampingRatio(), 1.0, 1.0e-6);
    s.trigger (1.0f);
    bool neverNegative = true;
    for (int i = 0; i < 20000; ++i)
    {
        s.advance (kDt);
        neverNegative = neverNegative && s.displacement() >= 0.0f;
    }
    CHECK (neverNegative);

    // Stable and bounded at dt = 10 ms for the stiffest, least-damped spring.
    Spring stiff;
    stiff.set (1.0f, 0.0f);
    stiff.trigger (1.0f);
    bool ok = true;
    for (int i = 0; i < 5000; ++i)
    {
        stiff.advance (0.01f);
        ok = ok && std::isfinite (stiff.displacement()) && std::fabs (stiff.displacement()) <= 1.0f;
    }
    CHECK (ok);

    // Exact step: one 10 ms step equals ten 1 ms steps.
    Spring a, b;
    a.set (0.4f, 0.2f);
    b.set (0.4f, 0.2f);
    a.trigger (0.9f);
    b.trigger (0.9f);
    double maxDiff = 0.0;
    for (int i = 0; i < 200; ++i)
    {
        a.advance (0.01f);
        for (int k = 0; k < 10; ++k) b.advance (0.001f);
        maxDiff = std::max (maxDiff, (double) std::fabs (a.displacement() - b.displacement()));
    }
    CHECK (maxDiff < 1.0e-4);

    // Deterministic: same sequence, same output.
    Spring c, d;
    for (Spring* sp : { &c, &d }) { sp->set (0.3f, 0.1f); sp->trigger (0.7f); }
    bool same = true;
    for (int i = 0; i < 5000; ++i)
    {
        c.advance (kDt);
        d.advance (kDt);
        same = same && c.displacement() == d.displacement();
    }
    CHECK (same);
}

AP_TEST (physics_orbit_ellipse_and_period)
{
    for (float speed : { 0.0f, 0.3f, 1.0f })
    {
        for (float shape : { 0.0f, 0.5f, 1.0f })
        {
            Orbit o;
            o.set (speed, shape);
            o.trigger();
            const double e = o.eccentricity();
            const double f = o.frequencyHz();
            CHECK_NEAR (e, 0.9 * shape, 1.0e-6);

            double t = 0.0, prevAngle = o.angle(), firstWrap = -1.0, secondWrap = -1.0;
            bool onEllipse = true, bounded = true;
            const double total = 2.5 / f;
            while (t < total)
            {
                o.advance (kDt);
                t += kDt;
                const double x = o.x(), y = o.y();
                onEllipse = onEllipse && std::fabs (x * x + y * y / (1.0 - e * e) - 1.0) < 1.0e-4;
                bounded = bounded && std::fabs (x) <= 1.0 && std::fabs (y) <= 1.0;
                const double a = o.angle();
                if (a < prevAngle)   // wrapped through 2 pi
                {
                    // Interpolate the crossing inside this step.
                    const double rate = (a + ap::kTwoPiD - prevAngle) / kDt;
                    const double cross = t - a / rate;
                    if (firstWrap < 0.0) firstWrap = cross;
                    else if (secondWrap < 0.0) secondWrap = cross;
                }
                prevAngle = a;
            }
            CHECK (onEllipse);
            CHECK (bounded);
            CHECK (firstWrap > 0.0 && secondWrap > 0.0);
            CHECK_NEAR (firstWrap, 1.0 / f, 0.02 / f);
            CHECK_NEAR (secondWrap - firstWrap, 1.0 / f, 0.02 / f);
        }
    }

    // Kepler-like: much faster near theta = 0 than on the far side.
    Orbit o;
    o.set (0.2f, 1.0f);
    o.trigger();
    const float a0 = o.angle();
    o.advance (0.001f);
    const double nearRate = (o.angle() - a0) / 0.001;
    while (o.angle() < 3.0f) o.advance (kDt);
    const float a1 = o.angle();
    o.advance (0.001f);
    const double farRate = (o.angle() - a1) / 0.001;
    CHECK (nearRate > 50.0 * farRate);   // ((1 + .9) / (1 - .9))^2 = 361 at the extremes

    // Large dt stays on the ellipse and matches small steps; deterministic.
    Orbit p, q;
    p.set (0.7f, 0.6f);
    q.set (0.7f, 0.6f);
    p.trigger();
    q.trigger();
    double maxDiff = 0.0;
    for (int i = 0; i < 300; ++i)
    {
        p.advance (0.01f);
        for (int k = 0; k < 10; ++k) q.advance (0.001f);
        maxDiff = std::max (maxDiff, (double) std::fabs (p.x() - q.x()) + std::fabs (p.y() - q.y()));
    }
    CHECK (maxDiff < 2.0e-3);

    Orbit r, s;
    for (Orbit* ob : { &r, &s }) { ob->set (0.5f, 0.3f); ob->trigger(); }
    bool same = true;
    for (int i = 0; i < 10000; ++i) { r.advance (kDt); s.advance (kDt); same = same && r.x() == s.x() && r.y() == s.y(); }
    CHECK (same);

    // trigger() resets to theta = 0 (x = 1, y = 0).
    r.trigger();
    CHECK_EQ (r.x(), 1.0f);
    CHECK_EQ (r.y(), 0.0f);
}
