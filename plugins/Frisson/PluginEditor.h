#pragma once

#include "PluginProcessor.h"

#include <ap_ui/EditorBase.h>

#include <array>
#include <atomic>
#include <memory>

namespace frisson
{

/**
    One of the nine knobs as a small card: name, what it does to the listener,
    the knob, and a ring around it that glows with what the module is doing.
    With `large` it is the DOSE master instead (no activity).
*/
class RingKnob : public juce::Component, public juce::SettableTooltipClient, public ap::ui::Animated
{
public:
    RingKnob (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId, const juce::String& title,
              const juce::String& tagline, std::atomic<float>* activitySource, bool large = false);

    void paint (juce::Graphics&) override;
    void resized() override;
    void tick() override;

private:
    juce::Rectangle<float> knobArea() const;

    juce::Slider slider;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
    juce::String name, line;
    std::atomic<float>* activity;
    float shown = 0.0f;
    bool big;
};

/** MATCH (level-matched bypass) plus the loudness difference and the output meter. */
class MatchPanel : public juce::Component, public ap::ui::Animated
{
public:
    explicit MatchPanel (Processor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick() override;

private:
    Processor& processor;
    juce::TextButton button { "MATCH" };
    juce::AudioProcessorValueTreeState::ButtonAttachment attachment;
    ap::ui::LevelMeter meter;
    float delta = 0.0f;
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
    std::array<std::unique_ptr<RingKnob>, kNumKnobs> knobs;
    std::unique_ptr<RingKnob> dose;
    MatchPanel matchPanel;
    ap::ui::ParamSection *match = nullptr, *advanced = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Editor)
};

} // namespace frisson
