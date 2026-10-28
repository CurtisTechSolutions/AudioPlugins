#pragma once

// WavetableView: the oscillator's table as a pseudo-3D stack of frames
// (CONTRACT §7). Far frames are thin and dim, the current frame (from
// Engine::visuals().oscPosition while a voice plays, else the Position knob)
// is drawn bright with a glow at its depth. The static stack is cached in an
// Image per table / size / display scale; ticks only move the highlight.
// Dragging vertically edits {o}_pos; double-click resets it.

#include "Widgets.h"

#include <memory>
#include <vector>

namespace verdant
{
class Processor;
}

namespace verdant::ui
{

class WavetableView : public juce::Component, public Tickable, private juce::ChangeListener
{
public:
    WavetableView (Processor&, int osc);
    ~WavetableView() override;

    void setAccentColour (juce::Colour);

    /** A light-weight copy of a table for drawing: up to 40 frames x 160 points, normalised to peak 1. */
    struct Sketch
    {
        juce::String name;
        int numFrames = 0;      // frames in the real table
        int rows = 0;           // frames in the sketch (evenly spaced over the table)
        static constexpr int points = 160;
        std::vector<float> data;   // rows x points
        const float* row (int r) const noexcept { return data.data() + (size_t) r * points; }
    };

    /** Factory sketches are built once per process (message thread) and shared. */
    static std::shared_ptr<const Sketch> factorySketch (int index);
    static std::shared_ptr<const Sketch> makeSketch (const float* frames, int numFrames, int frameSize, const juce::String& name);

    void tick() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void reloadTable();
    float currentPosition() const;
    void renderStack (float scale);

    struct Geometry
    {
        juce::Rectangle<float> area;
        float frameW = 0, depthX = 0, depthY = 0, amp = 0;
        juce::Point<float> origin (float depth) const noexcept;   // left end of the frame's centre line
    };
    Geometry geometry() const;
    juce::Path framePath (const float* row, float depth, const Geometry&) const;

    Processor& processor;
    int osc;
    juce::Colour accent { palette::accent };
    juce::RangedAudioParameter* posParam = nullptr;

    std::shared_ptr<const Sketch> sketch;
    int tableIndex = -1;
    juce::Image stack;
    float stackScale = 0.0f;
    float shownPosition = -1.0f;
    float dragStartValue = 0.0f;
    std::vector<float> highlight;
};

} // namespace verdant::ui
