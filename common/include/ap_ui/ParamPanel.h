#pragma once

#include "LookAndFeel.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <vector>

namespace ap::ui
{

/**
    A single parameter control that picks the right widget for the parameter
    type: rotary knob for float/int, combo box for choices, toggle for bools.
*/
class ParamControl : public juce::Component
{
public:
    ParamControl (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId);
    ~ParamControl() override = default;

    void resized() override;

    const juce::String& paramId() const noexcept { return id; }
    bool isKnob() const noexcept { return slider != nullptr; }

private:
    juce::String id;
    juce::Label  nameLabel;
    std::unique_ptr<juce::Slider>       slider;
    std::unique_ptr<juce::ComboBox>     combo;
    std::unique_ptr<juce::ToggleButton> toggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   sliderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   buttonAttachment;
};

/** A titled panel holding a row (wrapping if needed) of ParamControls. */
class ParamSection : public juce::Component
{
public:
    static constexpr int knobWidth  = 78;
    static constexpr int knobHeight = 96;
    static constexpr int rowGap     = 6;
    static constexpr int headerHeight = 22;

    ParamSection (juce::AudioProcessorValueTreeState& apvts, const juce::String& title,
                  const juce::StringArray& paramIds, int columns = 0);

    /** Extra custom component laid out to the right of the knobs (meter, display…). */
    void setTrailingComponent (juce::Component* c, int width);

    /** Height needed to show every control at `width`. */
    int preferredHeight (int width) const;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    int columnsFor (int width) const;

    juce::String title;
    std::vector<std::unique_ptr<ParamControl>> controls;
    int forcedColumns = 0;
    juce::Component* trailing = nullptr;
    int trailingWidth = 0;
};

} // namespace ap::ui
