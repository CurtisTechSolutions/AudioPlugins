#pragma once

// PhysicsView: animates one of the physics modulators from
// Engine::visuals().physics (CONTRACT §7):
//   Ball   - the ball's height over a floor, with a scrolling trace of recent heights
//   Spring - a coil hanging from an anchor with the mass displaced, plus its trace
//   Orbit  - the orbit path (shape from orbit_shape) and the moving body with a fading trail
// While no voice plays it shows the resting pose.

#include "Widgets.h"

#include <array>

namespace verdant
{
class Processor;
}

namespace verdant::ui
{

class PhysicsView : public juce::Component, public Tickable
{
public:
    enum class Kind { Ball, Spring, Orbit };

    PhysicsView (Processor&, Kind);

    void tick() override;
    void paint (juce::Graphics&) override;

private:
    void push (float a, float b);
    juce::Point<float> history (int age) const;   // age 0 = newest

    Processor& processor;
    Kind kind;
    static constexpr int historySize = 48;
    std::array<juce::Point<float>, historySize> trail {};
    int head = 0, filled = 0;
    float valueA = 0, valueB = 0;
    bool active = false;
};

} // namespace verdant::ui
