#pragma once

// LfoEditor: one cycle of an LFO's shape with a phase dot (CONTRACT §7).
// For the Custom shape it is a breakpoint editor on the patch's LfoShape:
//   click empty space = add a point, drag a point = move it (end points move
//   vertically only), double-click a point = remove it, drag vertically on a
//   segment = bend it (LfoShapePoint::curve of the segment's left point).
// Edits are written with Processor::setLfoShape on mouse-up.
//
// Segment curve convention (shared with the engine's LFO): between points a
// and b, y = a.y + (b.y - a.y) * shape (t, a.curve) with the envelope shape
// function, t^(1/(1+3c)) for c > 0 (fast start) and t^(1+3|c|) for c < 0.
//
// The phase dot rides the curve at the position whose value matches
// Engine::visuals().lfoValue (searched forward from the last position, so it
// moves continuously); it shows only while a voice plays.

#include "Widgets.h"

#include "engine/Lfo.h"

#include <array>

namespace verdant
{
class Processor;
}

namespace verdant::ui
{

class LfoEditor : public juce::Component, public Tickable, private juce::ChangeListener
{
public:
    LfoEditor (Processor&, int lfo, juce::Colour colour = juce::Colour (palette::modLfo));
    ~LfoEditor() override;

    void setLfo (int lfo);
    int getLfo() const noexcept { return lfo; }

    /** Shape value -1..1 at phase 0..1 (S&H uses a fixed pseudo-random pattern for display). */
    static float shapeValue (LfoWave, const LfoShape&, float phase) noexcept;

    void tick() override;
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void reloadShape();
    void rebuildTable();
    LfoWave wave() const;
    bool isCustom() const { return wave() == LfoWave::Custom; }
    juce::Rectangle<float> plotArea() const;
    juce::Point<float> pointPos (int i) const;
    int pointAt (juce::Point<float>) const;
    int segmentAt (float x) const;
    void commit();

    Processor& processor;
    int lfo;
    juce::Colour colour;
    LfoShape custom;
    static constexpr int tableSize = 256;
    std::array<float, tableSize> table {};
    int tableWave = -1;

    int hoverPoint = -1, dragPoint = -1, bendSegment = -1;
    bool pendingAdd = false, changed = false;
    float bendStart = 0.0f;
    juce::Point<float> downPos;

    float dotPhase = 0.0f, shownValue = 0.0f;
    bool dotVisible = false, unipolar = false;
};

} // namespace verdant::ui
