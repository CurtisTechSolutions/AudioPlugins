#pragma once

// Verdant's look: a dark theme with one strong accent (Verdant green), built
// on the shared ap::ui::LookAndFeel so the generic ap::ui widgets pick it up.
//
// Everything visual that more than one component needs lives here: the
// CONTRACT §7 palette, the spacing / radius / font scale (Theme), the colour
// family of every modulation source, and VerdantLookAndFeel, which draws the
// stock JUCE widgets (sliders, combo boxes, buttons, menus, tooltips,
// scrollbars, text editors) in that style. The Verdant widgets in Widgets.h,
// ModKnob.h and ModSourceChip.h draw themselves with the same tokens.

#include <ap_ui/LookAndFeel.h>

#include "engine/Params.h"

namespace verdant::ui
{

/** CONTRACT §7 palette (ARGB). */
namespace palette
{
    inline constexpr juce::uint32 background  = 0xff0f1114;
    inline constexpr juce::uint32 panel       = 0xff171a1f;
    inline constexpr juce::uint32 panelEdge   = 0xff242932;
    inline constexpr juce::uint32 text        = 0xffe8ecef;
    inline constexpr juce::uint32 textDim     = 0xff8a939e;
    inline constexpr juce::uint32 accent      = 0xff3ddc97;   // Verdant green
    inline constexpr juce::uint32 accentDim   = 0xff1e6b4b;
    inline constexpr juce::uint32 warning     = 0xfff2b134;
    inline constexpr juce::uint32 modLfo      = 0xff5ad0ff;   // modulation ring colours by source family
    inline constexpr juce::uint32 modEnv      = 0xfff28cb1;
    inline constexpr juce::uint32 modMacro    = 0xfff2b134;   // macros and performance sources
    inline constexpr juce::uint32 modPhysics  = 0xffb48cf2;   // physics and Alive
    inline constexpr juce::uint32 knobTrack   = 0xff242932;
    inline constexpr juce::uint32 meterHigh   = 0xfff25f5c;

    // Derived shades (not in the contract; all built from the tokens above).
    // Three depths (DESIGN.md): page = background, card = panel, control well = well.
    inline constexpr juce::uint32 well        = 0xff1c2026;   // control wells: buttons, choices, segmented controls
    inline constexpr juce::uint32 control     = 0xff1c2026;
    inline constexpr juce::uint32 controlHover= 0xff232832;
    inline constexpr juce::uint32 display     = 0xff121519;   // visualiser backgrounds (bottom of their gradient)
    inline constexpr juce::uint32 knobBodyTop = 0xff2a2f38;   // knob centre disc gradient
    inline constexpr juce::uint32 knobBodyBot = 0xff1c2026;
    inline constexpr juce::uint32 cardHighlight = 0xff20252c; // 1 px top-edge highlight of cards
    inline constexpr juce::uint32 textFaint   = 0xff5d6570;   // grid labels, disabled text
    inline constexpr juce::uint32 grid        = 0xff1e232a;   // display grid lines
    inline constexpr juce::uint32 secondary   = 0xff5ad0ff;   // second hue: Filter 2 / Osc B highlights
}

/** Spacing, radius and type scale (8 / 12 / 16 px spacing, 6 px radii, 11..20 px fonts). */
struct Theme
{
    static constexpr int spaceXS = 4, spaceS = 8, spaceM = 12, spaceL = 16;
    static constexpr float radius = 6.0f, radiusSmall = 4.0f, cardRadius = 8.0f;
    static constexpr int pageMargin = 14, gutter = 12;   // page grid: outer margin, column / card gutters

    static constexpr float fontSmall = 11.0f;    // values, grid labels, chips
    static constexpr float fontBody  = 12.0f;    // labels, buttons
    static constexpr float fontTitle = 12.0f;    // section titles (bold, upper case)
    static constexpr float fontLarge = 20.0f;    // logo

    static constexpr int controlHeight = 24;     // buttons, combo boxes, choice buttons
    static constexpr int sectionHeader = 28;     // card title row (20 px) incl. its 8 px top padding

    // Knob diameters (DESIGN.md hierarchy): primary (Position, Warp, Cutoff, Res, Level), secondary, tertiary.
    static constexpr int knobPrimary = 56, knobSecondary = 40, knobTertiary = 32;

    static juce::Colour c (juce::uint32 argb) noexcept { return juce::Colour (argb); }
};

/** The palette mapped onto the shared ap::ui::Theme. */
ap::ui::Theme verdantTheme();

/** Font sizes 11..20 px, JUCE default sans. */
juce::Font font (float size, bool bold = false);

/** Width of `text` in `f` (float pixels). */
float textWidth (const juce::Font& f, const juce::String& text);

/** Draws upper-case section-title style text with a little tracking. */
void drawTitleText (juce::Graphics&, const juce::String& text, juce::Rectangle<int> area,
                    juce::Colour, juce::Justification = juce::Justification::centredLeft, float size = Theme::fontTitle);

//==============================================================================
/** The CONTRACT §7 family colour of a modulation source (LFO blue, Env pink, macro / performance amber, physics / Alive violet). */
juce::Colour modSourceColour (ModSource) noexcept;

/** Short chip label: "LFO 1", "ENV 2", "M3", "VEL", "AT", "WHEEL", "BEND", "NOTE", "RAND", "BALL", "SPRING", "ORBIT", "ALIVE", "SEQ". */
juce::String modSourceShortName (ModSource);

/** Full display name ("LFO 1", "Env 2", "Macro 3", "Mod Wheel", ...) from the engine table. */
juce::String modSourceDisplayName (ModSource);

//==============================================================================
class VerdantLookAndFeel : public ap::ui::LookAndFeel
{
public:
    VerdantLookAndFeel();

    // Knobs: thin arc track, accent value arc (bipolar ranges fill from the centre), flat body, short pointer.
    // A slider's "knobColour" property (juce::var int ARGB) overrides the accent for that slider.
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float sliderPos,
                           float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& background,
                               bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

    void drawComboBox (juce::Graphics&, int w, int h, bool isDown, int buttonX, int buttonY,
                       int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area,
                            bool isSeparator, bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                            const juce::String& text, const juce::String& shortcutKeyText,
                            const juce::Drawable* icon, const juce::Colour* textColour) override;
    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>& area, const juce::String& sectionName) override;
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                    int& idealWidth, int& idealHeight) override;
    juce::Font getPopupMenuFont() override;
    int getPopupMenuBorderSize() override { return 4; }

    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                           juce::Rectangle<int> parentArea) override;

    int getDefaultScrollbarWidth() override { return 8; }
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool isScrollbarVertical,
                        int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown) override;

    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;

    void drawBubble (juce::Graphics&, juce::BubbleComponent&, const juce::Point<float>& tip,
                     const juce::Rectangle<float>& body) override;

    void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics&, const juce::Path&, juce::Image&) override;
    void drawCornerResizer (juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;
};

} // namespace verdant::ui
