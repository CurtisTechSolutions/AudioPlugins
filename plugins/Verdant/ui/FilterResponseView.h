#pragma once

// FilterResponseView: Filter 1 (accent) and Filter 2 (secondary hue) magnitude
// responses over a log-frequency 20 Hz .. 20 kHz x +-24 dB grid (CONTRACT §7).
// Curves come from verdant::filterMagnitudeDb at 160 points, recomputed only
// when a filter parameter (or, while a voice plays, the modulated cutoff /
// resonance) changes; mix blends the dry level in. Each enabled filter has a
// handle at its cutoff: drag x = cutoff, y = resonance (shift = fine).

#include "Widgets.h"

#include "engine/Filter.h"

#include <array>

namespace verdant
{
class Processor;
}

namespace verdant::ui
{

class FilterResponseView : public juce::Component, public Tickable
{
public:
    explicit FilterResponseView (Processor&);

    /** Response used for drawing: the engine's filterMagnitudeDb, or (when the engine reports a perfectly
        flat curve for a non-bypass type, i.e. the foundation stub) an analytic approximation. */
    static float responseDb (FilterType, float cutoffHz, float res01, float atHz, bool& usedFallback);
    static float approxMagnitudeDb (FilterType, float cutoffHz, float res01, float atHz);

    void tick() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    static constexpr int numPoints = 160;

private:
    struct State
    {
        bool on = false;
        int type = 0;
        float cutoff = 1000, res = 0, mix = 1;
        bool operator== (const State&) const = default;
    };
    struct Curve
    {
        State state;
        std::array<float, numPoints> db {};
        bool valid = false;
    };

    State readState (int f) const;
    void compute (int f);
    juce::Rectangle<float> plotArea() const;
    float xForHz (float hz) const;
    float hzForX (float x) const;
    float yForDb (float db) const;
    juce::Point<float> handlePos (int f) const;
    int handleAt (juce::Point<float>) const;

    Processor& processor;
    std::array<Curve, 2> curves;
    int hover = -1, dragging = -1;
    float dragCutoff = 0, dragRes = 0;
};

} // namespace verdant::ui
