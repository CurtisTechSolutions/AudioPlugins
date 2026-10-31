#pragma once

// Drag-to-modulate sources (CONTRACT §7).
//
//   ModSourceChip   a small draggable pill in its source family colour. Dragging it starts a
//                   juce::DragAndDropContainer drag (the editor is the container) with the
//                   description "modsrc:<ModSource int>"; ModKnob accepts it.
//   ModSourceStrip  every source as compact chips, grouped in family clusters
//                   (LFO 1-4 | ENV 1-3 | MACRO 1-8 | VEL AT MW PB KEY RND SEQ | BALL SPR ORB ALV),
//                   with live activity glow from Engine::visuals(). Fits ~650 px at 22 px high;
//                   it sits in the editor's bottom strip on every page.

#include "Widgets.h"

#include "engine/Params.h"

#include <memory>
#include <vector>

namespace verdant
{
class Processor;
}

namespace verdant::ui
{

class ModSourceChip : public juce::Component, public juce::SettableTooltipClient
{
public:
    /** Empty text = modSourceShortName (source). */
    explicit ModSourceChip (ModSource, const juce::String& text = {});

    ModSource getSource() const noexcept { return source; }

    /** "modsrc:<n>" */
    static juce::String dragDescription (ModSource);
    /** The source in a drag description, or None if it is not a chip drag. */
    static ModSource sourceFromDescription (const juce::var&);

    /** 0..1 glow for live activity (repaints only when it changes visibly). */
    void setActivity (float);
    /** Width that fits the text at the strip's font. */
    int preferredWidth() const;
    /** Draws the chip without its own pill (inside a ModSourceStrip cluster). */
    void setEmbedded (bool);

    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    ModSource source;
    juce::String text;
    float activity = 0.0f;
    bool embedded = false;
};

class ModSourceStrip : public juce::Component, public Tickable
{
public:
    explicit ModSourceStrip (Processor&);
    ~ModSourceStrip() override;

    /** Width the strip needs at its natural chip sizes. */
    int preferredWidth() const;

    void tick() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Cluster
    {
        juce::String label;
        juce::Colour colour;
        std::vector<ModSourceChip*> chips;
        juce::Rectangle<int> bounds;
    };

    Processor& processor;
    std::vector<std::unique_ptr<ModSourceChip>> chips;
    std::vector<Cluster> clusters;
};

} // namespace verdant::ui
