#pragma once

#include "LookAndFeel.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <memory>
#include <vector>

namespace ap::ui
{

/** Something the editor steps at ~30 Hz: meters, displays, controls that follow other parameters. */
class Animated
{
public:
    virtual ~Animated() = default;
    virtual void tick() = 0;

    /** Called when an editor takes over ticking; stop any timer of your own. */
    virtual void driveExternally() {}
};

/** How a ParamControl shows its parameter. Auto: knob for numbers, switch for
    bools, segmented buttons for choices with few short names, a combo box otherwise. */
enum class ControlStyle { Auto, Knob, SmallKnob, LargeKnob, Switch, Segmented, Combo };

/** One parameter: label, the right widget for its type, and a value read-out for knobs. */
class ParamControl : public juce::Component, public Animated
{
public:
    ParamControl (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId,
                  ControlStyle style = ControlStyle::Auto, const juce::String& label = {});
    ~ParamControl() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void tick() override;
    void mouseEnter (const juce::MouseEvent&) override { updateHot(); }
    void mouseExit (const juce::MouseEvent&) override  { updateHot(); }
    void mouseUp (const juce::MouseEvent&) override    { updateHot(); }

    int preferredWidth() const noexcept;
    int preferredHeight() const noexcept;

    /** Dims the control (it stays usable) unless `predicate` holds for the plain value of `otherParamId`. */
    void activeWhen (const juce::String& otherParamId, std::function<bool (float)> predicate);

    const juce::String& paramId() const noexcept { return id; }
    ControlStyle style() const noexcept { return kind; }
    bool isKnob() const noexcept { return slider != nullptr; }
    juce::Slider* getSlider() noexcept { return slider.get(); }

private:
    class Segments;
    void updateHot();

    juce::AudioProcessorValueTreeState& state;
    juce::String id, name;
    ControlStyle kind;
    std::unique_ptr<juce::Slider>       slider;
    std::unique_ptr<juce::ComboBox>     combo;
    std::unique_ptr<juce::ToggleButton> toggle;
    std::unique_ptr<Segments>           segments;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   sliderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   buttonAttachment;

    std::atomic<float>* condition = nullptr;
    std::function<bool (float)> conditionTest;
    bool dimmed = false;
    bool hot = false;   // mouse over / dragging the knob
};

/**
    A titled card holding ParamControls (and optionally custom components) in
    a flow layout: rows of controls spread across the card's width.
    An optional switch in the title row (a bool parameter) dims the body when off.
*/
class ParamSection : public juce::Component, public Animated
{
public:
    static constexpr int headerHeight = 30;
    static constexpr int padding = 12;
    static constexpr int rowGap = 8;

    ParamSection (juce::AudioProcessorValueTreeState& apvts, const juce::String& title,
                  const juce::StringArray& paramIds = {}, int columns = 0);
    ~ParamSection() override;

    /** Adds a control at the end of the flow. */
    ParamControl& add (const juce::String& paramId, ControlStyle style = ControlStyle::Auto, const juce::String& label = {});

    /** Adds a custom component (a display, a readout…) to the flow with a fixed size;
        width <= 0 makes it a row of its own spanning the card. Not owned. */
    void addComponent (juce::Component* c, int width, int height);

    /** Starts a new row in the flow. */
    void newRow();

    /** Puts a switch for this bool parameter in the title row; the body dims while it is off. */
    void setHeaderSwitch (const juce::String& boolParamId);

    /** Dims the whole body unless `predicate` holds for the plain value of `paramId` (e.g. a mode choice). */
    void setActiveWhen (const juce::String& paramId, std::function<bool (float)> predicate);

    /** A component laid out to the right of the controls, full body height (meters). Not owned. */
    void setTrailingComponent (juce::Component* c, int width);

    /** Optional dim text on the right of the title row. */
    void setCaption (const juce::String& text) { caption = text; repaint(); }

    ParamControl* control (const juce::String& paramId) const;

    /** Height needed to show everything at `width`. */
    int preferredHeight (int width) const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void tick() override;

private:
    struct Item
    {
        juce::Component* comp = nullptr;
        ParamControl* control = nullptr;   // when it is one of ours
        int width = 0, height = 0;
        bool rowBreak = false;             // newRow() marker
    };

    struct Placement { juce::Component* comp; juce::Rectangle<int> bounds; };
    std::vector<Placement> computeLayout (juce::Rectangle<int> body) const;
    juce::Rectangle<int> bodyArea (juce::Rectangle<int> bounds) const;

    juce::AudioProcessorValueTreeState& state;
    juce::String title, caption;
    std::vector<std::unique_ptr<ParamControl>> controls;
    std::vector<Item> items;
    int forcedColumns = 0;
    juce::Component* trailing = nullptr;
    int trailingWidth = 0;

    std::unique_ptr<juce::ToggleButton> headerSwitch;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> headerAttachment;
    std::atomic<float>* activeParam = nullptr;
    std::function<bool (float)> activeTest;
    bool bodyDimmed = false;
};

} // namespace ap::ui
