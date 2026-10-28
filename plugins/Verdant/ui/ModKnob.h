#pragma once

// ModKnob: Verdant's parameter knob (CONTRACT §7) and the per-page model of the
// modulation matrix it draws from.
//
//   * Bound to an APVTS parameter (SliderAttachment): short label above, the
//     unit-aware value below (no box; a dark pill on hover / drag), 270° arc,
//     double-click = default, shift-drag = fine, mouse wheel, tooltip "name: value".
//   * Right-click menu: Reset, Enter value..., Add modulation (source list),
//     one submenu per existing modulation ("LFO 1 -> 45 %": Invert / Remove),
//     Remove all modulations.
//   * Modulation rings: one 2 px arc per (source, amount) routed to the
//     parameter, in the source family colour, starting at the knob value
//     (both directions for bipolar sources). A live dot shows the modulated
//     value (Engine::visuals().modOffset) while modulation exists.
//   * Drag-to-modulate: drop a ModSourceChip ("modsrc:<n>") on the knob ->
//     processor.setModulation (dest, source, 0.5) (or the existing route is
//     reused) and the knob enters depth edit: the next vertical drag on the
//     knob sets that route's amount (shown in a ValuePopup) until mouse-up.
//     Alt-drag on a modulated knob edits the amount of the ring nearest the
//     mouse at any time.
//   * Non-modulatable parameters get the same knob without rings.
//
// ModulationModel reads the 24 matrix slots straight from the APVTS once per
// tick (no Processor API needed) and is shared by every knob of a page.

#include "Widgets.h"

#include "engine/Params.h"

#include <array>
#include <vector>

namespace verdant
{
class Processor;
}

namespace verdant::ui
{

//==============================================================================
class ModulationModel : public Tickable
{
public:
    struct Route
    {
        int slot = -1;
        ModSource source = ModSource::None;
        int dest = 0;           // destination index (1..68)
        float amount = 0.0f;    // -1..1 (= -100..100 %)
    };

    explicit ModulationModel (Processor&);

    /** Re-reads the matrix; returns true when anything changed (version() then increments). */
    bool refresh();
    void tick() override { refresh(); }

    /** Routes targeting `destIndex`, in slot order. */
    std::vector<Route> routesFor (int destIndex) const;
    /** The route for (source, dest), or slot -1. */
    Route find (int destIndex, ModSource) const;
    const Route& slot (int s) const noexcept { return slots[(size_t) s]; }

    /** LFOs follow their Polarity parameter; Pitch Bend, Note, Spring, Orbit and Alive are bipolar; the rest unipolar. */
    bool isBipolar (ModSource) const;

    uint32_t version() const noexcept { return ver; }
    Processor& processor() noexcept { return proc; }

    /** Sets a route's amount as part of a gesture (begin / set / end), for depth dragging. */
    void beginAmountGesture (int slot);
    void setAmountDuringGesture (int slot, float amount);
    void endAmountGesture (int slot);

private:
    Processor& proc;
    std::array<Route, kModSlots> slots {};
    std::array<bool, 4> lfoUnipolar {};
    uint32_t ver = 1;
};

//==============================================================================
class ModKnob : public juce::Component,
                public juce::DragAndDropTarget,
                public Tickable
{
public:
    /** `diameter` is the knob's size (Theme::knobPrimary / knobSecondary / knobTertiary). Empty label = shortName. */
    ModKnob (Processor&, ModulationModel&, P param, int diameter = Theme::knobSecondary, const juce::String& label = {});
    ModKnob (Processor&, ModulationModel&, const juce::String& paramId, int diameter = Theme::knobSecondary, const juce::String& label = {});
    ~ModKnob() override;

    /** Component height for a knob of `diameter`: label + ring margin + knob + ring margin + value. */
    static int heightFor (int diameter) noexcept { return Cell::labelHeight + 2 * ringMargin + diameter + Cell::valueHeight; }
    /** Suggested minimum width (knob + rings, at least 52 px for the value text). */
    static int widthFor (int diameter) noexcept { return juce::jmax (52, diameter + 2 * ringMargin + 8); }

    void setLabel (const juce::String&);
    void setDiameter (int);
    int getDiameter() const noexcept { return diameter; }
    /** Arc colour (default accent; e.g. palette::secondary for Filter 2). */
    void setKnobColour (juce::Colour);
    void setValueVisible (bool);

    juce::Slider& getSlider() noexcept;
    P getParam() const noexcept { return param; }
    int getDestIndex() const noexcept { return dest; }   // 0 = not modulatable
    juce::String getValueText() const;

    /** Starts depth editing of the route (source -> this knob), as after a chip drop. */
    void beginDepthEdit (ModSource);

    // Tickable: reads the live modulation offset and the model's routes; repaints only on change.
    void tick() override;

    // DragAndDropTarget
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    static constexpr int ringMargin = 3;

private:
    class KnobSlider;
    friend class KnobSlider;

    void init (int diameter, const juce::String& label);
    juce::Rectangle<float> knobArea() const;
    juce::Rectangle<int> valueArea() const;
    float angleFor (float normalised) const noexcept;
    void refreshRoutes();
    void showMenu();
    void showValueEditor();

    // amount dragging (depth edit / alt-drag)
    bool wantsAmountDrag (const juce::MouseEvent&);
    void amountDragStart (const juce::MouseEvent&);
    void amountDrag (const juce::MouseEvent&);
    void amountDragEnd();
    int routeIndexNear (juce::Point<float>) const;
    void showRoutePopup (const ModulationModel::Route&);

    Processor& processor;
    ModulationModel& model;
    P param;
    int dest = 0;
    const ParamInfo& paramInfo;
    juce::RangedAudioParameter* parameter = nullptr;

    std::unique_ptr<KnobSlider> slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    std::unique_ptr<juce::TextEditor> valueEditor;
    ValuePopup popup;

    juce::String label;
    int diameter = Theme::knobSecondary;
    bool valueVisible = true;
    juce::Colour knobColour { palette::accent };

    std::vector<ModulationModel::Route> routes;
    uint32_t seenVersion = 0;
    float liveOffset = 0.0f;
    bool dragHover = false, dragging = false;

    // depth edit
    ModSource depthSource = ModSource::None;
    int depthTicks = 0;
    int amountSlot = -1;
    float amountStart = 0.0f, amountNow = 0.0f;
    int popupTicks = 0;

    static constexpr float startAngle = juce::MathConstants<float>::pi * 1.25f;
    static constexpr float endAngle   = juce::MathConstants<float>::pi * 2.75f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModKnob)
};

/** Creates a ModKnob for `param`, adds it to `parent` and registers it with `page` for the shared tick. */
std::unique_ptr<ModKnob> makeKnob (Processor&, ModulationModel&, Page& page, juce::Component& parent, P param,
                                   int diameter = Theme::knobSecondary, const juce::String& label = {},
                                   juce::Colour colour = juce::Colour (palette::accent));

/** Creates a ChoiceButton bound to `param` (cell label above and / or inline label) and adds it to `parent`. */
std::unique_ptr<ChoiceButton> makeChoice (Processor&, juce::Component& parent, P param,
                                          const juce::String& label = {}, const juce::String& inlineLabel = {});

} // namespace verdant::ui
