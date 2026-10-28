#pragma once

#include "PluginProcessor.h"

#include <ap_ui/EditorBase.h>
#include <ap_ui/Meters.h>

#include <memory>
#include <vector>

namespace humline
{

/**
    Capture controls: record toggle, clear, save as .mid, and a handle that can
    be dragged into a DAW as a .mid file. Shows how many notes / beats the take holds.
*/
class CapturePanel : public juce::Component, private juce::Timer
{
public:
    explicit CapturePanel (Processor&);
    ~CapturePanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class DragHandle : public juce::Component
    {
    public:
        explicit DragHandle (CapturePanel& p) : owner (p) {}
        void paint (juce::Graphics&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
    private:
        CapturePanel& owner;
        bool dragging = false;
    };

    void timerCallback() override;
    void saveAs();
    juce::File writeTempFile();
    void startExternalDrag();

    Processor& processor;
    juce::TextButton captureButton { "CAPTURE" };
    juce::TextButton clearButton { "Clear" };
    juce::TextButton saveButton { "Save .mid..." };
    DragHandle handle;
    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<ap::CapturedNote> notes;
    uint32_t shownGeneration = 0xffffffffu;
    int shownCount = -1;
    double shownBeats = 0.0;
    bool shownCapturing = false;
};

class Editor : public ap::ui::EditorBase, public juce::DragAndDropContainer
{
public:
    explicit Editor (Processor&);
    ~Editor() override = default;

private:
    Processor& processor;
    ap::ui::PitchReadout readout;
    ap::ui::LevelMeter inMeter, outMeter;
    CapturePanel capturePanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Editor)
};

} // namespace humline
