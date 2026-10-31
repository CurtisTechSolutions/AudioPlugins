#pragma once

// StepGrid: the step sequencer's 16 steps x 4 lanes (CONTRACT §7).
//
//   Pitch     -24..+24 bars from the centre line (semitones, or scale degrees with Snap); click / drag
//             paints values across steps, double-click resets a step to 0
//   Velocity  0..100 % bars, painted the same way
//   Gate      on / off per step; drag paints the state of the first step touched
//   Mod       0..100 % bars (the Seq Mod source)
//
// The step-number row sets the step count (click a number); steps beyond the
// count are dimmed. While the sequencer runs, the playing step
// (Engine::visuals().seqStep) is highlighted. Values are read from the APVTS
// each tick; edits are host gestures (one per parameter per drag).

#include "Widgets.h"

#include <array>
#include <set>

namespace verdant
{
class Processor;
}

namespace verdant::ui
{

class StepGrid : public juce::Component, public juce::SettableTooltipClient, public Tickable
{
public:
    explicit StepGrid (Processor&);
    ~StepGrid() override;

    enum Lane { Pitch, Velocity, Gate, Mod, NumLanes };

    void tick() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    static constexpr int kSteps = 16;

private:
    juce::Rectangle<float> laneArea (int lane) const;
    juce::Rectangle<float> numberRow() const;
    juce::Rectangle<float> column (int step, juce::Rectangle<float> lane) const;
    int stepAt (float x) const;
    int laneAt (juce::Point<float>) const;
    float valueFor (int lane, float y) const;
    void setValue (int step, int lane, float value);
    void endGestures();
    void read();
    juce::RangedAudioParameter* param (int step, int lane) const;

    Processor& processor;
    std::array<std::array<float, kSteps>, NumLanes> values {};
    int steps = 8, playing = -1, hoverStep = -1, hoverLane = -1;
    bool snap = true, running = false;
    int dragLane = -1, lastDragStep = -1;
    float gatePaint = 1.0f;
    std::set<juce::RangedAudioParameter*> touched;
    juce::Rectangle<float> grid;
};

} // namespace verdant::ui
