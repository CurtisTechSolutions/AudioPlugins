#pragma once

#include "PluginProcessor.h"

#include <ap_ui/EditorBase.h>
#include <ap_ui/Meters.h>

#include <array>
#include <memory>

namespace intone
{

/** Twelve pitch-class toggles for the custom scale. */
class NoteToggleRow : public juce::Component
{
public:
    explicit NoteToggleRow (juce::AudioProcessorValueTreeState& apvts);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    std::array<std::unique_ptr<juce::TextButton>, 12> buttons;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>, 12> attachments;
};

class Editor : public ap::ui::EditorBase
{
public:
    explicit Editor (Processor&);
    ~Editor() override = default;

private:
    Processor& processor;
    ap::ui::PitchReadout readout;
    NoteToggleRow notes;
    ap::ui::GainReductionMeter grMeter;
    ap::ui::LevelMeter meter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Editor)
};

} // namespace intone
