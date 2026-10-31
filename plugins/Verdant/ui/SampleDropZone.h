#pragma once

// SampleDropZone: "Drop a sample to match" (CONTRACT §7). Accepts audio files
// dropped on it (or handed over by the editor from anywhere in the window, or
// chosen with the async file chooser on click), runs
// BackgroundTasks::analyzeSample with a progress bar, then shows what was found
// (pitch, voicing, the analysis notes) with Apply / Discard. Apply installs
// patchFromSampleMatch (result, current patch) through Processor::applyPatch.

#include "Widgets.h"

#include "engine/SampleMatch.h"

#include <memory>

namespace verdant
{
class Processor;
}

namespace verdant::ui
{

class SampleDropZone : public juce::Component, public juce::SettableTooltipClient, public juce::FileDragAndDropTarget
{
public:
    explicit SampleDropZone (Processor&);
    ~SampleDropZone() override;

    /** Starts the analysis of `file` (any decodable audio file). */
    void analyse (const juce::File&);

    static bool isAudioFile (const juce::String& path);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    enum class State { Idle, Analysing, Ready, Failed };
    void setState (State);
    void apply();
    void discard();

    Processor& processor;
    State state = State::Idle;
    float progress = 0.0f;
    bool dragOver = false;
    juce::File file;
    juce::String error;
    std::shared_ptr<SampleMatchResult> result;
    std::shared_ptr<int> generation = std::make_shared<int> (0);   // drops callbacks of superseded analyses
    std::unique_ptr<juce::FileChooser> chooser;
    IconButton applyButton { IconButton::Icon::None, "Install the matched patch", "Apply" };
    IconButton discardButton { IconButton::Icon::None, "Forget this match", "Discard" };
};

} // namespace verdant::ui
