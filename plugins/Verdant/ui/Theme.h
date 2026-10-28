#pragma once

// Verdant's look: a dark theme with one strong accent (Verdant green), built
// on the shared ap::ui::LookAndFeel so the generic ap::ui widgets pick it up.

#include <ap_ui/LookAndFeel.h>

namespace verdant::ui
{

/** CONTRACT §7 palette (ARGB). */
namespace palette
{
    inline constexpr juce::uint32 background = 0xff0f1114;
    inline constexpr juce::uint32 panel      = 0xff171a1f;
    inline constexpr juce::uint32 panelEdge  = 0xff242932;
    inline constexpr juce::uint32 text       = 0xffe8ecef;
    inline constexpr juce::uint32 textDim    = 0xff8a939e;
    inline constexpr juce::uint32 accent     = 0xff3ddc97;   // Verdant green
    inline constexpr juce::uint32 accentDim  = 0xff1e6b4b;
    inline constexpr juce::uint32 warning    = 0xfff2b134;
    inline constexpr juce::uint32 modLfo     = 0xff5ad0ff;   // modulation ring colours by source family
    inline constexpr juce::uint32 modEnv     = 0xfff28cb1;
    inline constexpr juce::uint32 modMacro   = 0xfff2b134;   // macros and performance sources
    inline constexpr juce::uint32 modPhysics = 0xffb48cf2;   // physics and Alive
    inline constexpr juce::uint32 knobTrack  = 0xff2a3039;
    inline constexpr juce::uint32 meterHigh  = 0xfff25f5c;
}

/** The palette mapped onto the shared ap::ui::Theme. */
ap::ui::Theme verdantTheme();

/** Font sizes 11..20 px, JUCE default sans. */
juce::Font font (float size, bool bold = false);

class VerdantLookAndFeel : public ap::ui::LookAndFeel
{
public:
    VerdantLookAndFeel();

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& background,
                               bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
};

} // namespace verdant::ui
