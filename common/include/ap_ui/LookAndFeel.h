#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ap::ui
{

/** Colour palette shared by all plugins; `accent` is overridden per plugin. */
struct Theme
{
    juce::Colour background { 0xff15171b };
    juce::Colour panel      { 0xff1e2128 };
    juce::Colour panelEdge  { 0xff2c313a };
    juce::Colour knobTrack  { 0xff343a45 };
    juce::Colour text       { 0xffe6e8ec };
    juce::Colour textDim    { 0xff9aa0aa };
    juce::Colour accent     { 0xff5ac8fa };
    juce::Colour accentDim  { 0xff2d6478 };
    juce::Colour meterLow   { 0xff4cd964 };
    juce::Colour meterHigh  { 0xffff3b30 };

    static Theme withAccent (juce::Colour accent)
    {
        Theme t;
        t.accent = accent;
        t.accentDim = accent.withMultipliedBrightness (0.55f).withMultipliedSaturation (0.8f);
        return t;
    }
};

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    explicit LookAndFeel (Theme theme = {});

    const Theme& theme() const noexcept { return th; }

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float sliderPos,
                           float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    juce::Label* createSliderTextBox (juce::Slider&) override;
    juce::Font   getLabelFont (juce::Label&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

    void drawComboBox (juce::Graphics&, int w, int h, bool isDown, int buttonX, int buttonY,
                       int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& bg,
                               bool highlighted, bool down) override;

    static juce::Font titleFont()   { return juce::Font (juce::FontOptions (20.0f, juce::Font::bold)); }
    static juce::Font sectionFont() { return juce::Font (juce::FontOptions (12.5f, juce::Font::bold)); }
    static juce::Font labelFont()   { return juce::Font (juce::FontOptions (12.0f)); }
    static juce::Font valueFont()   { return juce::Font (juce::FontOptions (11.5f)); }

private:
    Theme th;
};

} // namespace ap::ui
