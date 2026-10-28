#pragma once

// Physics mod sources: deterministic, bounded, allocation-free small
// simulations advanced at control rate (dt = kControlInterval / sampleRate).
//
// Owner: W-E. FOUNDATION STUB: constant outputs (0).

namespace verdant
{

class BouncingBall
{
public:
    void set (float gravity01, float bounce01);
    void trigger (float velocity01);              // velocity sets the drop height
    void advance (float dt) noexcept;
    float height() const noexcept;                // 0..1

private:
    float gravity = 0.5f, bounce = 0.7f, h = 0.0f;
};

class Spring
{
public:
    void set (float stiffness01, float damping01);
    void trigger (float velocity01);
    void advance (float dt) noexcept;
    float displacement() const noexcept;          // -1..1

private:
    float stiffness = 0.5f, damping = 0.3f, x = 0.0f;
};

class Orbit
{
public:
    void set (float speed01, float shape01);
    void trigger();
    void advance (float dt) noexcept;
    float x() const noexcept;                     // -1..1
    float y() const noexcept;                     // -1..1

private:
    float speed = 0.5f, shape = 0.3f, px = 0.0f, py = 0.0f;
};

} // namespace verdant
