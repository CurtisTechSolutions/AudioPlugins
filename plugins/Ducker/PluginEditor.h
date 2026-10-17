#pragma once

#include "PluginProcessor.h"

#include <ap_ui/EditorBase.h>
#include <ap_ui/Meters.h>

namespace ducker
{

/** Small status line: whether the host has routed a sidechain into us. */
class SidechainStatus : public juce::Component, private juce::Timer
{
public:
    explicit SidechainStatus (Processor&);
    ~SidechainStatus() override;
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    Processor& processor;
    bool shown = false;
};

class Editor : public ap::ui::EditorBase
{
public:
    explicit Editor (Processor&);
    ~Editor() override = default;

private:
    Processor& processor;
    ap::ui::GainReductionMeter grMeter;
    SidechainStatus status;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Editor)
};

} // namespace ducker
