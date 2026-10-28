#pragma once

// The MOD page (CONTRACT §7, DESIGN.md), three columns:
//   left   Envelopes (ENV 1/2/3 tabs: EnvelopeEditor + 7 knobs) and LFOs (LFO 1-4 tabs: LfoEditor with
//          custom breakpoints, shape / mode / polarity / sync, rate or division, phase, fade, smooth)
//   middle Macros 1-8 (knob + double-click-to-rename name), Physics (ball / spring / orbit views and
//          their knobs) and Alive (alive, evolve)
//   right  the 24-slot matrix (source, destination grouped by section, bipolar amount, clear), all rows
//          visible at the base size (a viewport scrolls only when the window is too small)
// Mod sources are dragged from the editor's bottom strip onto any knob.

#include "Pages.h"

#include <memory>

namespace verdant::ui
{

class ModPage : public Page, private juce::ChangeListener
{
public:
    explicit ModPage (Processor&);
    ~ModPage() override;

    void pageShown() override;
    void tick() override;
    void resized() override;

    /** Selects the visible envelope (0..2) / LFO (0..3) tab. */
    void showEnvelope (int);
    void showLfo (int);

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace verdant::ui
