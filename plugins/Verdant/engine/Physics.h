#pragma once

// Physics mod sources: deterministic, bounded, allocation-free small
// simulations advanced at control rate (dt = kControlInterval / sampleRate,
// or shorter at sub-block boundaries). set() is called per control block.
//
// BouncingBall: dropped from h = 0.3 + 0.7 * velocity, gravity lerp (2, 40, gravity01)
//   units/s^2, restitution lerp (0.2, 0.95, bounce01); comes to rest (h = 0) once a
//   bounce apex would be below 0.002. Integrated exactly (ballistic arcs with the
//   impact time solved per bounce), so any dt is accurate.
// Spring: mass-spring-damper, w = lerp (2 pi 0.5, 2 pi 12, stiffness01) rad/s,
//   damping ratio lerp (0.02, 1, damping01). trigger() is a velocity impulse from the
//   current displacement (a pluck with no output jump), scaled so a spring at rest
//   reaches its first peak at exactly `velocity` (1 at full velocity). Exact analytic
//   step for any dt; output clamped to -1..1.
// Orbit: angle rate proportional to (1 + e cos theta)^2 (Kepler-like: fast near
//   theta = 0, slow on the far side), e = 0.9 * shape01, normalised so one revolution
//   takes 1 / lerp (0.1, 8, speed01) seconds; x = cos theta, y = sqrt (1 - e^2) sin theta.
//
// Owner: W-E.

namespace verdant
{

class BouncingBall
{
public:
    void set (float gravity01, float bounce01);
    void trigger (float velocity01);              // velocity sets the drop height
    void advance (float dt) noexcept;
    float height() const noexcept;                // 0..1

    bool atRest() const noexcept { return resting; }
    float gravityUnits() const noexcept { return g; }       // units / s^2
    float restitution() const noexcept { return e; }

    static constexpr float kRestApex = 0.002f;

private:
    float g = 21.0f, e = 0.725f;   // set (0.5, 0.7)
    float h = 0.0f, v = 0.0f;
    bool resting = true;
};

class Spring
{
public:
    void set (float stiffness01, float damping01);
    void trigger (float velocity01);
    void advance (float dt) noexcept;
    float displacement() const noexcept;          // -1..1

    float omega() const noexcept { return w; }              // rad/s
    float dampingRatio() const noexcept { return zeta; }

private:
    void updateCoefficients (double dt) noexcept;

    float w = 39.2699082f, zeta = 0.314f;   // set (0.5, 0.3)
    double x = 0.0, v = 0.0;
    // Cached exact state-transition matrix for (dt, w, zeta).
    double cachedDt = -1.0, cachedW = -1.0, cachedZeta = -1.0;
    double m00 = 1.0, m01 = 0.0, m10 = 0.0, m11 = 1.0;
};

class Orbit
{
public:
    void set (float speed01, float shape01);
    void trigger();
    void advance (float dt) noexcept;
    float x() const noexcept;                     // -1..1
    float y() const noexcept;                     // -1..1

    float frequencyHz() const noexcept { return freq; }
    float eccentricity() const noexcept { return ecc; }
    float angle() const noexcept { return (float) theta; }  // 0..2 pi

private:
    float freq = 4.05f, ecc = 0.27f;   // set (0.5, 0.3)
    double theta = 0.0, c = 1.0, s = 0.0;
};

} // namespace verdant
