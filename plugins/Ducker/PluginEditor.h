#pragma once

#include "PluginProcessor.h"

#include <ap_ui/EditorBase.h>

#include <memory>

namespace ducker
{

/** Scrolling picture of the last few seconds: input level (from the bottom) and gain reduction (from the top). */
class DuckHistory : public ap::ui::Display
{
public:
    explicit DuckHistory (Processor&);
    void tick() override;

protected:
    void paintDisplay (juce::Graphics&, juce::Rectangle<float>) override;

private:
    Processor& processor;
    ap::ui::History input { 240, -90.0f }, reduction { 240, 0.0f };   // dB per 30 Hz tick
};

/** Static input -> output curve of the compressor (threshold / ratio / knee), with the current operating point. */
class TransferCurve : public ap::ui::Display
{
public:
    explicit TransferCurve (Processor&);
    void tick() override;

protected:
    void paintDisplay (juce::Graphics&, juce::Rectangle<float>) override;

private:
    Processor& processor;
    float levelDb = -90.0f;
};

/** One division of the ghost-duck envelope, with the playhead. */
class GhostShape : public ap::ui::Display
{
public:
    explicit GhostShape (Processor&);
    void tick() override { repaint(); }

protected:
    void paintDisplay (juce::Graphics&, juce::Rectangle<float>) override;

private:
    Processor& processor;
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
    /** Keeps the detector card's caption in step with the sidechain routing. */
    struct SidechainCaption : ap::ui::Animated
    {
        Processor* processor = nullptr;
        ap::ui::ParamSection* section = nullptr;
        int shown = -1;
        void tick() override;
    };

    Processor& processor;
    ap::ui::ParamControl modeControl;
    ap::ui::GainReductionMeter grMeter;
    DuckHistory history;
    TransferCurve transfer;
    GhostShape ghostShape;
    SidechainCaption scCaption;

    ap::ui::ParamSection *detector = nullptr, *output = nullptr, *ghost = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Editor)
};

} // namespace ducker
