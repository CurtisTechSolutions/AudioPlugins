#include "Theme.h"

namespace verdant::ui
{

ap::ui::Theme verdantTheme()
{
    ap::ui::Theme t;
    t.background = juce::Colour (palette::background);
    t.panel      = juce::Colour (palette::panel);
    t.panelEdge  = juce::Colour (palette::panelEdge);
    t.knobTrack  = juce::Colour (palette::knobTrack);
    t.text       = juce::Colour (palette::text);
    t.textDim    = juce::Colour (palette::textDim);
    t.accent     = juce::Colour (palette::accent);
    t.accentDim  = juce::Colour (palette::accentDim);
    t.meterLow   = juce::Colour (palette::accent);
    t.meterHigh  = juce::Colour (palette::meterHigh);
    return t;
}

juce::Font font (float size, bool bold)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

VerdantLookAndFeel::VerdantLookAndFeel()
    : ap::ui::LookAndFeel (verdantTheme())
{
    setColour (juce::TextButton::buttonColourId, juce::Colour (palette::panel));
    setColour (juce::TextButton::buttonOnColourId, juce::Colour (palette::accentDim));
    setColour (juce::TextButton::textColourOffId, juce::Colour (palette::textDim));
    setColour (juce::TextButton::textColourOnId, juce::Colour (palette::text));
}

void VerdantLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                        bool highlighted, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    auto fill = on ? juce::Colour (palette::accentDim) : juce::Colour (palette::panel);
    if (down) fill = fill.brighter (0.2f);
    else if (highlighted) fill = fill.brighter (0.08f);

    g.setColour (fill);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (on ? juce::Colour (palette::accent) : juce::Colour (palette::panelEdge));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
}

void VerdantLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool highlighted, bool)
{
    const bool on = b.getToggleState();
    g.setColour (on || highlighted ? juce::Colour (palette::text) : juce::Colour (palette::textDim));
    g.setFont (font (12.5f, on));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (6, 2), juce::Justification::centred, 1);
}

} // namespace verdant::ui
