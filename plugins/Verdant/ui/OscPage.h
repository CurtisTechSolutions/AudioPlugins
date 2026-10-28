#pragma once

// The OSC page (CONTRACT §7, DESIGN.md): Osc A and Osc B cards (table, hero
// WavetableView, position / warp / level, pitch, unison, phase and routing),
// and a right column with the shared filter curve + Filter 1 / Filter 2
// controls, Sub & Noise, and the amp envelope (Env 1).
//
// WAV files dropped on an oscillator card (or chosen with Import) are
// imported into that oscillator through BackgroundTasks::importWavetable.
// The Paint button calls onPaintRequested (osc); the PaintCanvas owner
// connects it (nothing happens while it is unset).

#include "Pages.h"

#include <functional>
#include <memory>

namespace verdant::ui
{

class OscPage : public Page, public juce::FileDragAndDropTarget
{
public:
    explicit OscPage (Processor&);
    ~OscPage() override;

    /** Set by the PaintCanvas owner: open the paint overlay for oscillator `osc` (0 = A, 1 = B). */
    std::function<void (int osc)> onPaintRequested;

    /** Opens the async WAV chooser for oscillator `osc`. */
    void importWavetable (int osc);

    void pageShown() override;
    void tick() override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int x, int y) override;
    void fileDragMove (const juce::StringArray&, int x, int y) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace verdant::ui
