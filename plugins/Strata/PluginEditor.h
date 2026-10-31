#pragma once

#include "PluginProcessor.h"

#include <ap_ui/EditorBase.h>

#include <memory>

namespace strata
{

/** Two cycles of an oscillator's waveform (shape + pulse width), drawn from its parameters. */
class WaveDisplay : public ap::ui::Display
{
public:
    WaveDisplay (juce::AudioProcessorValueTreeState&, const char* waveId, const char* widthId, const char* levelId);
    void tick() override;

protected:
    void paintDisplay (juce::Graphics&, juce::Rectangle<float>) override;

private:
    std::atomic<float> *wave, *width, *level;
    float shown[3] { -1.0f, -1.0f, -1.0f };
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
    ap::ui::LevelMeter meter;
    WaveDisplay waveA, waveB;
    std::unique_ptr<ap::ui::ResponseDisplay> filterCurve;
    ap::ui::AdsrDisplay ampEnv, filterEnv;
    ap::ui::Keyboard keyboard;

    ap::ui::ParamSection *oscA = nullptr, *oscB = nullptr, *sub = nullptr, *filter = nullptr, *amp = nullptr,
                         *fenv = nullptr, *lfo1 = nullptr, *lfo2 = nullptr, *global = nullptr, *chorus = nullptr,
                         *delay = nullptr, *reverb = nullptr, *play = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Editor)
};

} // namespace strata
