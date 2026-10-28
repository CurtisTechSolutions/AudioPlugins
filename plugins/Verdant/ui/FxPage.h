#pragma once

// The FX page (CONTRACT §7, DESIGN.md):
//   top     the rack: FxSlotStrip, eight reorderable slot tiles in signal order
//   left    the selected slot's effect: a hero display that draws what the effect does
//           (transfer curve, comb / notch response, echo taps, reverb tail, EQ curve with
//           draggable bands, compressor curve) above its knobs, toggles and choices;
//           an empty slot shows a picker of the eight effect types
//   right   the Sympathetic body (amount, memory, decay, brightness, damping, strings,
//           snap) with a string display
//
// The Sympathetic stage's per-string state is not exposed by the engine, so the
// string display is stylised: one line per string (symp_strings), excited by the
// output level x Amount, ringing out with Decay / Damping, tinted by Brightness,
// plus the Memory slots.

#include "Pages.h"

#include <memory>

namespace verdant::ui
{

class FxPage : public Page
{
public:
    explicit FxPage (Processor&);
    ~FxPage() override;

    /** Selects the slot (0..7) whose effect panel is shown. */
    void selectSlot (int slot);

    void pageShown() override;
    void tick() override;
    void resized() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace verdant::ui
