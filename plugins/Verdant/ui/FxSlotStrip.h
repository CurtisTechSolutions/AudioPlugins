#pragma once

// FxSlotStrip: the FX page's rack (CONTRACT §7), eight slot tiles in signal
// order. Each tile shows its slot's effect (type glyph + name), pulses with the
// output level while audio passes, and marks a type that already sits in an
// earlier slot (the engine processes only the first one) as bypassed.
//
//   click            select the slot (onSelect); an empty slot opens the type menu
//   chevron / right  type menu: None + the eight effect types
//   drag a tile      reorder: the tile moves to the gap under the mouse and the
//                    fx{n}_type values are rewritten so the engine order follows
//
// Reads the slot types straight from the APVTS in tick(); writes them with
// host gestures.

#include "Widgets.h"

#include <array>
#include <functional>

namespace verdant
{
class Processor;
}

namespace verdant::ui
{

class FxSlotStrip : public juce::Component, public juce::SettableTooltipClient, public Tickable
{
public:
    explicit FxSlotStrip (Processor&);
    ~FxSlotStrip() override;

    int getSelected() const noexcept { return selected; }
    void setSelected (int slot, juce::NotificationType = juce::sendNotificationSync);
    std::function<void (int slot)> onSelect;

    /** FxType index (0 = None) of a slot, as last read. */
    int typeOf (int slot) const noexcept { return types[(size_t) juce::jlimit (0, kNumFxSlots - 1, slot)]; }
    /** True when the slot's type also sits in an earlier slot (the engine bypasses this one). */
    bool isDuplicate (int slot) const noexcept;

    /** Moves the effect in slot `from` to position `to` (the slots in between shift), as one undo step per param. */
    void moveSlot (int from, int to);
    void setSlotType (int slot, int type);
    void showTypeMenu (int slot);

    /** Display names of the FxType choices ("None", "Distortion", ...). */
    static juce::String typeName (int type);
    /** A small line glyph for an effect type, drawn into `area`. */
    static void drawTypeGlyph (juce::Graphics&, int type, juce::Rectangle<float> area, juce::Colour, float stroke = 1.4f);

    void tick() override;
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    static constexpr int kSlots = 8;

private:
    juce::Rectangle<float> tileBounds (int slot) const;
    juce::Rectangle<float> menuZone (int slot) const;
    int tileAt (juce::Point<float>) const;
    int gapAt (float x) const;        // insertion gap 0..8 nearest to x
    float gapX (int gap) const;
    void readTypes();
    void paintTile (juce::Graphics&, int slot, juce::Rectangle<float>, bool floating);

    Processor& processor;
    std::array<juce::RangedAudioParameter*, kNumFxSlots> params {};
    std::array<int, kNumFxSlots> types {};
    std::array<float, kNumFxSlots> pulse {};
    int selected = 0, hover = -1, hoverMenu = -1;
    int pressed = -1, dropGap = -1;
    bool dragging = false;
    juce::Point<float> dragPos, grabOffset;
};

} // namespace verdant::ui
