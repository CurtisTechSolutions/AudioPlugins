#include "Physics.h"

#include <ap/Math.h>

#include <algorithm>
#include <cmath>

namespace verdant
{

namespace
{
    // Longest dt a single advance() honours: a host stall must not make the
    // simulations spin for long (they are exact or substepped below this).
    constexpr float kMaxDt = 1.0f;

    inline float sanitiseDt (float dt) noexcept
    {
        return std::isfinite (dt) ? ap::clamp (dt, 0.0f, kMaxDt) : 0.0f;
    }

    inline float clamp01 (float v) noexcept
    {
        return std::isfinite (v) ? ap::clamp (v, 0.0f, 1.0f) : 0.0f;
    }
}

//==============================================================================
void BouncingBall::set (float gravity01, float bounce01)
{
    g = ap::lerp (2.0f, 40.0f, clamp01 (gravity01));
    e = ap::lerp (0.2f, 0.95f, clamp01 (bounce01));
}

void BouncingBall::trigger (float velocity01)
{
    h = 0.3f + 0.7f * clamp01 (velocity01);
    v = 0.0f;
    resting = false;
}

void BouncingBall::advance (float dt) noexcept
{
    if (resting)
        return;

    // Exact ballistic motion: between impacts the height is a parabola, so solve
    // for the impact time instead of integrating (no energy drift, any dt).
    double remaining = sanitiseDt (dt);
    double hh = h, vv = v;
    const double gg = g;

    for (int bounce = 0; bounce < 64 && remaining > 0.0; ++bounce)
    {
        const double tHit = (vv + std::sqrt (std::max (0.0, vv * vv + 2.0 * gg * hh))) / gg;
        if (tHit > remaining)
        {
            hh += vv * remaining - 0.5 * gg * remaining * remaining;
            vv -= gg * remaining;
            break;
        }

        const double vImpact = vv - gg * tHit;   // negative (falling)
        vv = -(double) e * vImpact;
        hh = 0.0;
        remaining -= tHit;

        if (vv * vv / (2.0 * gg) < (double) kRestApex)
        {
            resting = true;
            hh = vv = 0.0;
            break;
        }
    }

    h = (float) ap::clamp (hh, 0.0, 1.0);
    v = (float) vv;
}

float BouncingBall::height() const noexcept { return h; }

//==============================================================================
void Spring::set (float stiffness01, float damping01)
{
    w    = ap::lerp (ap::kTwoPi * 0.5f, ap::kTwoPi * 12.0f, clamp01 (stiffness01));
    zeta = ap::lerp (0.02f, 1.0f, clamp01 (damping01));
}

void Spring::trigger (float velocity01)
{
    // A velocity impulse (no jump in the output). From rest the first peak of
    // x'' + 2 zeta w x' + w^2 x = 0 with x(0) = 0, x'(0) = v0 is
    // (v0 / w) * exp (-zeta / sqrt (1 - zeta^2) * atan2 (sqrt (1 - zeta^2), zeta))
    // (-> v0 / (w e) when critically damped); pick v0 so that peak = velocity.
    const double z = zeta;
    const double peakFactor = z < 0.9999 ? std::exp (-z / std::sqrt (1.0 - z * z) * std::atan2 (std::sqrt (1.0 - z * z), z))
                                         : std::exp (-1.0);
    v = (double) clamp01 (velocity01) * (double) w / peakFactor;
}

void Spring::updateCoefficients (double dt) noexcept
{
    if (dt == cachedDt && (double) w == cachedW && (double) zeta == cachedZeta)
        return;

    cachedDt = dt;
    cachedW = w;
    cachedZeta = zeta;

    // Exact state transition of the damped oscillator over dt (one exp and one
    // sin/cos pair, only when dt or the settings change).
    const double om = w, z = zeta;
    const double decay = std::exp (-z * om * dt);
    if (z < 0.9999)
    {
        const double wd = om * std::sqrt (1.0 - z * z);
        const double cs = std::cos (wd * dt), sn = std::sin (wd * dt);
        m00 = decay * (cs + z * om / wd * sn);
        m01 = decay * sn / wd;
        m10 = -decay * om * om / wd * sn;
        m11 = decay * (cs - z * om / wd * sn);
    }
    else
    {
        m00 = decay * (1.0 + om * dt);
        m01 = decay * dt;
        m10 = -decay * om * om * dt;
        m11 = decay * (1.0 - om * dt);
    }
}

void Spring::advance (float dt) noexcept
{
    const float step = sanitiseDt (dt);
    if (step <= 0.0f || w <= 0.0f)
        return;

    updateCoefficients (step);
    const double nx = m00 * x + m01 * v;
    const double nv = m10 * x + m11 * v;
    x = nx;
    v = nv;

    // Flush denormal-range tails to an exact rest.
    if (std::fabs (x) < 1.0e-12 && std::fabs (v) < 1.0e-9)
        x = v = 0.0;
    if (! std::isfinite (x) || ! std::isfinite (v))
        x = v = 0.0;
}

float Spring::displacement() const noexcept
{
    return (float) ap::clamp (x, -1.0, 1.0);
}

//==============================================================================
void Orbit::set (float speed01, float shape01)
{
    freq = ap::lerp (0.1f, 8.0f, clamp01 (speed01));
    ecc  = 0.9f * clamp01 (shape01);
}

void Orbit::trigger()
{
    theta = 0.0;
    c = 1.0;
    s = 0.0;
}

void Orbit::advance (float dt) noexcept
{
    const double total = sanitiseDt (dt);
    if (total <= 0.0)
        return;

    // dtheta/dt = k (1 + e cos theta)^2 with k = 2 pi f / (1 - e^2)^(3/2): since
    // the integral of dtheta / (1 + e cos theta)^2 over 2 pi is 2 pi / (1 - e^2)^(3/2), one revolution takes exactly 1 / f.
    const double e = ecc;
    const double k = ap::kTwoPiD * (double) freq / std::pow (1.0 - e * e, 1.5);

    // Heun (2nd order) substeps of at most 0.05 rad, rotating (cos, sin) by
    // Taylor series so the only trig call is the final re-normalisation.
    const double maxRate = k * (1.0 + e) * (1.0 + e);
    const int numSteps = std::clamp ((int) std::ceil (maxRate * total / 0.05), 1, 256);
    const double h = total / (double) numSteps;

    auto rotate = [] (double cs, double sn, double d, double& outC, double& outS) noexcept
    {
        const double d2 = d * d;
        const double cd = 1.0 - d2 * 0.5 + d2 * d2 * (1.0 / 24.0);
        const double sd = d - d2 * d * (1.0 / 6.0) + d2 * d2 * d * (1.0 / 120.0);
        outC = cs * cd - sn * sd;
        outS = sn * cd + cs * sd;
    };

    double cs = c, sn = s;
    for (int i = 0; i < numSteps; ++i)
    {
        const double r1 = k * (1.0 + e * cs) * (1.0 + e * cs);
        double pc, ps;
        rotate (cs, sn, r1 * h, pc, ps);
        const double r2 = k * (1.0 + e * pc) * (1.0 + e * pc);
        const double d = 0.5 * (r1 + r2) * h;
        theta += d;
        rotate (cs, sn, d, cs, sn);
    }

    theta = std::fmod (theta, ap::kTwoPiD);
    if (theta < 0.0) theta += ap::kTwoPiD;
    c = std::cos (theta);
    s = std::sin (theta);
}

float Orbit::x() const noexcept { return (float) c; }
float Orbit::y() const noexcept { return (float) (std::sqrt (1.0 - (double) ecc * (double) ecc) * s); }

} // namespace verdant
