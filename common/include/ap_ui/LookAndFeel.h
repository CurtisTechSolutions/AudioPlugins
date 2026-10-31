#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ap::ui
{

/**
    Colour palette shared by all plugins; `accent` is set per plugin.

    Three depths: the page (`background`), cards (`panel`) and wells
    (`well`: displays, buttons, choice boxes). Text comes in three strengths.
*/
struct Theme
{
    juce::Colour background { 0xff0e1013 };
    juce::Colour panel      { 0xff171a1f };
    juce::Colour panelEdge  { 0xff252a32 };
    juce::Colour knobTrack  { 0xff2a3039 };
    juce::Colour text       { 0xffe9ecef };
    juce::Colour textDim    { 0xff8b939d };
    juce::Colour accent     { 0xff5ac8fa };
    juce::Colour accentDim  { 0xff2d6478 };
    juce::Colour meterLow   { 0xff4cd964 };
    juce::Colour meterHigh  { 0xffff453a };

    juce::Colour well       { 0xff101317 };   // displays and control wells
    juce::Colour wellEdge   { 0xff20252c };
    juce::Colour grid       { 0xff1c2128 };   // display grid lines
    juce::Colour textFaint  { 0xff5c646e };   // grid labels, disabled text
    juce::Colour highlight  { 0x0dffffff };   // 1 px top edge of cards
    juce::Colour accent2    { 0xffa1a9b4 };   // second trace colour in displays

    static Theme withAccent (juce::Colour accent)
    {
        Theme t;
        t.accent = accent;
        t.accentDim = accent.withMultipliedBrightness (0.5f).withMultipliedSaturation (0.85f);
        t.accent2 = accent.withRotatedHue (0.5f).withSaturation (0.35f).withBrightness (0.85f);
        return t;
    }
};

/** Type scale used by every ap::ui component (JUCE's default sans). */
struct Type
{
    static juce::Font title()   { return juce::Font (juce::FontOptions (21.0f, juce::Font::bold)).withExtraKerningFactor (0.02f); }
    static juce::Font section() { return juce::Font (juce::FontOptions (11.0f, juce::Font::bold)).withExtraKerningFactor (0.12f); }
    static juce::Font label()   { return juce::Font (juce::FontOptions (11.5f)); }
    static juce::Font value()   { return juce::Font (juce::FontOptions (11.0f)); }
    static juce::Font small()   { return juce::Font (juce::FontOptions (10.0f)); }
    static juce::Font big (float size) { return juce::Font (juce::FontOptions (size, juce::Font::bold)); }
};

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    explicit LookAndFeel (Theme theme = {});

    const Theme& theme() const noexcept { return th; }

    /** Knobs: track arc, glowing value arc (bipolar ranges fill from the centre), shaded body, pointer.
        A slider's "accent" property (int ARGB) overrides the accent for that slider. */
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float sliderPos,
                           float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    juce::Label* createSliderTextBox (juce::Slider&) override;
    juce::Font   getLabelFont (juce::Label&) override;

    /** A pill switch followed by the button text. */
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
    juce::Font getPopupMenuFont() override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& bg,
                               bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;

    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    void drawCornerResizer (juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;

    // Kept for existing callers; new code uses ap::ui::Type.
    static juce::Font titleFont()   { return Type::title(); }
    static juce::Font sectionFont() { return Type::section(); }
    static juce::Font labelFont()   { return Type::label(); }
    static juce::Font valueFont()   { return Type::value(); }

private:
    Theme th;
};

/** The theme of whatever ap::ui::LookAndFeel `c` uses (a default theme otherwise). */
const Theme& themeOf (const juce::Component& c);

/** Fills a card: shaded panel, 1 px edge and top highlight, soft drop shadow. */
void drawCard (juce::Graphics&, juce::Rectangle<float> area, const Theme&, float radius = 10.0f);

/** Fills a display well: dark recessed panel with an inner top shadow. */
void drawWell (juce::Graphics&, juce::Rectangle<float> area, const Theme&, float radius = 6.0f);

/** Draws upper-case tracked text (section titles, small captions). */
void drawCaps (juce::Graphics&, const juce::String& text, juce::Rectangle<float> area, juce::Colour,
               juce::Justification = juce::Justification::centredLeft, juce::Font = Type::section());

} // namespace ap::ui
