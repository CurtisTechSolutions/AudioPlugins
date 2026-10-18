#pragma once

#include "PluginProcessor.h"

#include <ap_ui/EditorBase.h>
#include <ap_ui/Meters.h>

#include <array>
#include <atomic>
#include <memory>

namespace frisson
{

/** A rotary knob with a name and an activity ring that shows what the module is doing. */
class RingKnob : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    RingKnob (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId,
              const juce::String& title, std::atomic<float>* activitySource, bool large = false);
    ~RingKnob() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    juce::Slider slider;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
    juce::String name;
    std::atomic<float>* activity;
    float shown = 0.0f;
    bool  big;
};

/** MATCH button plus the loudness-difference readout. */
class MatchPanel : public juce::Component, private juce::Timer
{
public:
    MatchPanel (Processor&);
    ~MatchPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    Processor& processor;
    juce::TextButton button { "MATCH" };
    juce::AudioProcessorValueTreeState::ButtonAttachment attachment;
    float delta = 0.0f;
};

class Editor : public ap::ui::EditorBase
{
public:
    explicit Editor (Processor&);
    ~Editor() override = default;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void updateAdvancedVisibility();

    Processor& processor;
    std::array<std::unique_ptr<RingKnob>, kNumKnobs> knobs;
    std::unique_ptr<RingKnob> dose;
    MatchPanel matchPanel;
    juce::TextButton advancedToggle { "Advanced" };
    std::unique_ptr<ap::ui::ParamSection> advanced;
    ap::ui::LevelMeter meter;
    bool showAdvanced = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Editor)
};

} // namespace frisson
