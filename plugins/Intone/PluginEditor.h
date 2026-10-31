#pragma once

#include "PluginProcessor.h"

#include <ap_ui/EditorBase.h>

#include <array>
#include <memory>

namespace intone
{

/** The last few seconds: detected pitch (grey) and the corrected pitch (accent) over the scale's rows. */
class PitchHistory : public ap::ui::Display
{
public:
    explicit PitchHistory (Processor&);
    void tick() override;

protected:
    void paintDisplay (juce::Graphics&, juce::Rectangle<float>) override;

private:
    Processor& processor;
    ap::ui::History detected { 180 }, corrected { 180 };   // fractional MIDI at the 30 Hz tick, 0 = none
    float centre = 60.0f;
};

/**
    One octave of keys for the custom scale. With Scale = Custom the keys are
    toggles (lit = allowed); with any other scale they show that scale's notes.
*/
class ScaleKeys : public juce::Component, public ap::ui::Animated
{
public:
    explicit ScaleKeys (Processor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void tick() override;

private:
    juce::Rectangle<float> keyBounds (int pc) const;
    int keyAt (juce::Point<float>) const;

    Processor& processor;
    std::array<std::unique_ptr<juce::ParameterAttachment>, 12> attachments;
    std::array<bool, 12> custom {};
    juce::uint16 shownMask = 0;
    bool customMode = false;
    int shownRoot = -1;
};

class Editor : public ap::ui::EditorBase
{
public:
    explicit Editor (Processor&);
    ~Editor() override = default;

protected:
    void layoutContent (juce::Rectangle<int>) override;
    void paintLogo (juce::Graphics&, juce::Rectangle<float>) override;

private:
    Processor& processor;
    ap::ui::PitchReadout readout;
    PitchHistory history;
    ScaleKeys scaleKeys;
    ap::ui::GainReductionMeter grMeter;
    ap::ui::LevelMeter meter;
    std::unique_ptr<ap::ui::ResponseDisplay> eqCurve;

    ap::ui::ParamSection *correction = nullptr, *notes = nullptr, *deess = nullptr, *comp = nullptr,
                         *tone = nullptr, *output = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Editor)
};

} // namespace intone
