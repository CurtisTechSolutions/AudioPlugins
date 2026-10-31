#pragma once

#include "PluginProcessor.h"

#include <ap_ui/EditorBase.h>

#include <vector>

namespace voxnote
{

/**
    The last few seconds as a piano roll: the detected pitch as a line (bright
    where it passes the gate) and the notes VoxNote sent as bars, against a
    keyboard. When Snap To Scale is on the scale's rows are tinted.
*/
class PitchRoll : public ap::ui::Display
{
public:
    explicit PitchRoll (Processor&);
    void tick() override;

    /** The newest analysis hop (for the status line). */
    const Processor::UiFrame& latest() const noexcept { return frames.back(); }

protected:
    void paintDisplay (juce::Graphics&, juce::Rectangle<float>) override;

private:
    Processor& processor;
    std::vector<Processor::UiFrame> frames;   // oldest first, fixed length
    std::vector<Processor::UiFrame> incoming; // scratch for one tick's new hops
    float centre = 57.0f;                     // MIDI note at the middle of the view (follows the singer)
    static constexpr int kFrames = 1100;      // ≈ 6 s at 48 kHz
    static constexpr float kRange = 22.0f;    // semitones visible
};

/** Input level against the gate, plus a one-line diagnosis (no input / below gate / tracking). */
class InputStatus : public juce::Component, public ap::ui::Animated
{
public:
    InputStatus (Processor&, const PitchRoll&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick() override;

private:
    Processor& processor;
    const PitchRoll& roll;
    ap::ui::LevelMeter meter;
    juce::String status;
    bool good = false;
};

/** Direct MIDI port choice, its status and how to use it from hosts that drop plugin MIDI. */
class MidiPortPanel : public juce::Component, public ap::ui::Animated
{
public:
    explicit MidiPortPanel (Processor&);
    ~MidiPortPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick() override;

private:
    /** Refreshes the port list every time it opens, so new loopMIDI / IAC ports show up. */
    struct PortBox : juce::ComboBox
    {
        std::function<void()> beforePopup;
        void showPopup() override { if (beforePopup) beforePopup(); juce::ComboBox::showPopup(); }
    };

    void rebuildList();
    void choose (int itemId);

    Processor& processor;
    PortBox box;
    std::vector<MidiPortOut::Port> ports;   // item id = index + 2 (1 = off)
    juce::String status;
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
    ap::ui::ActivityLed midiLed;
    PitchRoll roll;
    InputStatus input;
    MidiPortPanel portPanel;

    ap::ui::ParamSection *detection = nullptr, *notes = nullptr, *scale = nullptr, *bend = nullptr,
                         *velocity = nullptr, *output = nullptr, *midiOut = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Editor)
};

} // namespace voxnote
