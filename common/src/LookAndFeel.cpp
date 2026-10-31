#include "ap_ui/LookAndFeel.h"

namespace ap::ui
{

namespace
{
    juce::Colour sliderAccent (const juce::Slider& s, const Theme& th)
    {
        if (const auto* v = s.getProperties().getVarPointer ("accent"))
            return juce::Colour ((juce::uint32) (juce::int64) *v);
        return th.accent;
    }
}

const Theme& themeOf (const juce::Component& c)
{
    static const Theme fallback;
    if (auto* lf = dynamic_cast<const LookAndFeel*> (&c.getLookAndFeel())) return lf->theme();
    return fallback;
}

void drawCard (juce::Graphics& g, juce::Rectangle<float> r, const Theme& th, float radius)
{
    // Soft shadow under the card.
    juce::Path shape;
    shape.addRoundedRectangle (r, radius);
    juce::DropShadow (juce::Colours::black.withAlpha (0.35f), 10, { 0, 3 }).drawForPath (g, shape);

    g.setGradientFill (juce::ColourGradient (th.panel.brighter (0.035f), 0.0f, r.getY(),
                                             th.panel.darker (0.06f), 0.0f, r.getBottom(), false));
    g.fillPath (shape);
    g.setColour (th.panelEdge);
    g.strokePath (shape, juce::PathStrokeType (1.0f));
    // 1 px highlight along the top edge.
    g.setColour (th.highlight);
    g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + radius, r.getRight() - radius);
}

void drawWell (juce::Graphics& g, juce::Rectangle<float> r, const Theme& th, float radius)
{
    g.setGradientFill (juce::ColourGradient (th.well.darker (0.25f), 0.0f, r.getY(),
                                             th.well, 0.0f, r.getY() + juce::jmin (24.0f, r.getHeight()), false));
    g.fillRoundedRectangle (r, radius);
    g.setColour (th.wellEdge);
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

void drawCaps (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, juce::Colour c,
               juce::Justification j, juce::Font f)
{
    g.setColour (c);
    g.setFont (f);
    g.drawText (text.toUpperCase(), area, j, true);
}

//==============================================================================
LookAndFeel::LookAndFeel (Theme theme) : th (theme)
{
    setColour (juce::ResizableWindow::backgroundColourId, th.background);
    setColour (juce::Slider::textBoxTextColourId, th.text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, th.accentDim);
    setColour (juce::Label::textColourId, th.text);
    setColour (juce::Label::textWhenEditingColourId, th.text);
    setColour (juce::Label::backgroundWhenEditingColourId, th.well);
    setColour (juce::Label::outlineWhenEditingColourId, th.accent);
    setColour (juce::TextEditor::backgroundColourId, th.well);
    setColour (juce::TextEditor::textColourId, th.text);
    setColour (juce::TextEditor::highlightColourId, th.accentDim);
    setColour (juce::TextEditor::focusedOutlineColourId, th.accent);
    setColour (juce::TextEditor::outlineColourId, th.wellEdge);
    setColour (juce::CaretComponent::caretColourId, th.accent);
    setColour (juce::ComboBox::textColourId, th.text);
    setColour (juce::ComboBox::backgroundColourId, th.well);
    setColour (juce::ComboBox::outlineColourId, th.wellEdge);
    setColour (juce::ComboBox::arrowColourId, th.textDim);
    setColour (juce::PopupMenu::backgroundColourId, th.panel);
    setColour (juce::PopupMenu::textColourId, th.text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, th.accentDim);
    setColour (juce::PopupMenu::highlightedTextColourId, th.text);
    setColour (juce::ToggleButton::textColourId, th.text);
    setColour (juce::ToggleButton::tickColourId, th.accent);
    setColour (juce::ToggleButton::tickDisabledColourId, th.textDim);
    setColour (juce::TextButton::buttonColourId, th.well);
    setColour (juce::TextButton::buttonOnColourId, th.accentDim);
    setColour (juce::TextButton::textColourOffId, th.textDim);
    setColour (juce::TextButton::textColourOnId, th.text);
    setColour (juce::TooltipWindow::backgroundColourId, th.panel);
    setColour (juce::TooltipWindow::textColourId, th.text);
    setColour (juce::TooltipWindow::outlineColourId, th.panelEdge);
    setColour (juce::ScrollBar::thumbColourId, th.knobTrack);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos,
                                    float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat();
    const float size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto centre = bounds.getCentre();
    const bool enabled = slider.isEnabled();
    const bool hot = slider.isMouseOverOrDragging() && enabled;
    const float trackW = juce::jlimit (2.5f, 5.0f, size * 0.065f);
    const float arcR = size * 0.5f - trackW * 0.5f - 3.0f;
    auto accent = enabled ? sliderAccent (slider, th) : th.textFaint;
    if (hot) accent = accent.brighter (0.15f);

    const juce::PathStrokeType stroke (trackW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (th.knobTrack);
    g.strokePath (track, stroke);

    // Bipolar ranges fill from the centre, unipolar ones from the start.
    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float zeroPos = bipolar ? (float) slider.valueToProportionOfLength (0.0) : 0.0f;
    const float zeroAngle = startAngle + zeroPos * (endAngle - startAngle);
    const float angle = startAngle + sliderPos * (endAngle - startAngle);

    if (std::abs (angle - zeroAngle) > 0.01f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f,
                             juce::jmin (zeroAngle, angle), juce::jmax (zeroAngle, angle), true);
        if (enabled)
        {
            // Glow: the same arc, wider and translucent, under the crisp one.
            g.setColour (accent.withAlpha (0.10f));
            g.strokePath (value, juce::PathStrokeType (trackW + 7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (accent.withAlpha (0.20f));
            g.strokePath (value, juce::PathStrokeType (trackW + 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (accent);
        g.strokePath (value, stroke);
    }
    if (bipolar)
    {
        // Small tick at the centre detent.
        const juce::Point<float> dir (std::cos (zeroAngle - juce::MathConstants<float>::halfPi),
                                      std::sin (zeroAngle - juce::MathConstants<float>::halfPi));
        g.setColour (th.textFaint);
        g.drawLine (juce::Line<float> (centre + dir * (arcR + trackW * 0.5f + 1.0f), centre + dir * (arcR + trackW * 0.5f + 3.5f)), 1.2f);
    }

    // Body: a shaded disc with a drop shadow and a rim highlight.
    const float bodyR = arcR - trackW * 0.5f - juce::jmax (3.0f, size * 0.075f);
    const auto body = juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (centre);
    juce::Path disc;
    disc.addEllipse (body);
    juce::DropShadow (juce::Colours::black.withAlpha (0.55f), juce::roundToInt (size * 0.10f), { 0, juce::roundToInt (size * 0.04f) }).drawForPath (g, disc);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff353b45), body.getCentreX(), body.getY(),
                                             juce::Colour (0xff1b1f25), body.getCentreX(), body.getBottom(), false));
    g.fillPath (disc);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.16f), body.getCentreX(), body.getY(),
                                             juce::Colours::transparentWhite, body.getCentreX(), body.getCentreY(), false));
    g.drawEllipse (body.reduced (0.6f), 1.0f);

    // Pointer: a light line from near the centre to the rim.
    const juce::Point<float> dir (std::cos (angle - juce::MathConstants<float>::halfPi),
                                  std::sin (angle - juce::MathConstants<float>::halfPi));
    g.setColour (enabled ? (hot ? juce::Colours::white : th.text) : th.textDim);
    g.drawLine (juce::Line<float> (centre + dir * (bodyR * 0.30f), centre + dir * (bodyR * 0.86f)),
                juce::jmax (1.8f, size * 0.045f));
}

void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos,
                                    float, float, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const bool horizontal = style == juce::Slider::LinearHorizontal || style == juce::Slider::LinearBar;
    const auto area = juce::Rectangle<int> (x, y, w, h).toFloat();
    const float thickness = 4.0f;

    juce::Rectangle<float> track = horizontal
        ? juce::Rectangle<float> (area.getX(), area.getCentreY() - thickness * 0.5f, area.getWidth(), thickness)
        : juce::Rectangle<float> (area.getCentreX() - thickness * 0.5f, area.getY(), thickness, area.getHeight());

    g.setColour (th.knobTrack);
    g.fillRoundedRectangle (track, thickness * 0.5f);

    juce::Rectangle<float> fill = horizontal ? track.withWidth (sliderPos - track.getX()) : track.withTop (sliderPos);
    g.setColour (slider.isEnabled() ? sliderAccent (slider, th) : th.textFaint);
    g.fillRoundedRectangle (fill, thickness * 0.5f);

    const float r = 6.5f;
    const juce::Point<float> thumb = horizontal ? juce::Point<float> (sliderPos, track.getCentreY())
                                                : juce::Point<float> (track.getCentreX(), sliderPos);
    juce::Path knob;
    knob.addEllipse (thumb.x - r, thumb.y - r, r * 2.0f, r * 2.0f);
    juce::DropShadow (juce::Colours::black.withAlpha (0.5f), 4, { 0, 1 }).drawForPath (g, knob);
    g.setColour (th.text);
    g.fillPath (knob);
}

juce::Label* LookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (slider);
    l->setFont (Type::value());
    l->setJustificationType (juce::Justification::centred);
    l->setColour (juce::Label::textColourId, th.textDim);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::outlineWhenEditingColourId, th.accent);
    l->setColour (juce::Label::backgroundWhenEditingColourId, th.well);
    return l;
}

juce::Font LookAndFeel::getLabelFont (juce::Label&)
{
    return Type::label();
}

void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    const auto bounds = b.getLocalBounds().toFloat();
    const float h = juce::jmin (16.0f, bounds.getHeight() - 4.0f), w = h * 1.85f;
    const auto pill = juce::Rectangle<float> (bounds.getX() + 1.0f, bounds.getCentreY() - h * 0.5f, w, h);
    const bool on = b.getToggleState();
    const bool enabled = b.isEnabled();

    if (on && enabled)
    {
        g.setColour (th.accent.withAlpha (0.18f));
        g.fillRoundedRectangle (pill.expanded (2.0f), (h + 4.0f) * 0.5f);
    }
    g.setColour (on ? (enabled ? th.accent : th.textFaint) : (highlighted ? th.knobTrack.brighter (0.15f) : th.knobTrack));
    g.fillRoundedRectangle (pill, h * 0.5f);

    const float d = h - 4.0f;
    const float tx = on ? pill.getRight() - 2.0f - d : pill.getX() + 2.0f;
    juce::Path thumb;
    thumb.addEllipse (tx, pill.getY() + 2.0f, d, d);
    juce::DropShadow (juce::Colours::black.withAlpha (0.4f), 3, { 0, 1 }).drawForPath (g, thumb);
    g.setColour (on ? juce::Colours::white : th.textDim);
    g.fillPath (thumb);

    if (b.getButtonText().isNotEmpty())
    {
        g.setColour (enabled ? (on ? th.text : th.textDim) : th.textFaint);
        g.setFont (Type::label());
        g.drawFittedText (b.getButtonText(), bounds.withLeft (pill.getRight() + 7.0f).toNearestInt(),
                          juce::Justification::centredLeft, 1, 0.9f);
    }
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool isDown, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<int> (0, 0, w, h).toFloat().reduced (0.5f);
    const bool hot = box.isMouseOver (true) || isDown;
    g.setColour (hot ? th.well.brighter (0.06f) : th.well);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (box.hasKeyboardFocus (true) || isDown ? th.accent.withAlpha (0.8f) : (hot ? th.panelEdge.brighter (0.25f) : th.wellEdge));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);

    // Chevron.
    juce::Path chevron;
    const float cx = r.getRight() - 12.0f, cy = r.getCentreY();
    chevron.startNewSubPath (cx - 3.5f, cy - 1.5f);
    chevron.lineTo (cx, cy + 2.0f);
    chevron.lineTo (cx + 3.5f, cy - 1.5f);
    g.setColour (box.isEnabled() ? th.textDim : th.textFaint);
    g.strokePath (chevron, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return Type::label();
}

void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 26, box.getHeight() - 2);
    label.setFont (Type::label());
    label.setJustificationType (juce::Justification::centredLeft);
}

void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (th.panel);
    g.setColour (th.panelEdge);
    g.drawRect (0, 0, w, h, 1);
}

void LookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                                     bool isSeparator, bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                                     const juce::String& text, const juce::String& shortcutKeyText,
                                     const juce::Drawable* icon, const juce::Colour* textColour)
{
    if (isSeparator)
    {
        g.setColour (th.panelEdge);
        g.fillRect (area.reduced (8, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }
    auto r = area.reduced (3, 1);
    if (isHighlighted && isActive)
    {
        g.setColour (th.accentDim);
        g.fillRoundedRectangle (r.toFloat(), 4.0f);
    }
    if (isTicked)
    {
        g.setColour (th.accent);
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ (float) r.getX() + 9.0f, (float) r.getCentreY() }));
    }
    g.setColour (textColour != nullptr ? *textColour : (isActive ? th.text : th.textFaint));
    g.setFont (getPopupMenuFont());
    g.drawFittedText (text, r.withTrimmedLeft (20).withTrimmedRight (hasSubMenu ? 18 : 6), juce::Justification::centredLeft, 1);
    if (shortcutKeyText.isNotEmpty())
    {
        g.setColour (th.textDim);
        g.drawText (shortcutKeyText, r.withTrimmedRight (8), juce::Justification::centredRight);
    }
    juce::ignoreUnused (icon);
}

juce::Font LookAndFeel::getPopupMenuFont()
{
    return juce::Font (juce::FontOptions (13.0f));
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                        bool highlighted, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    juce::Colour c = on ? th.accent.withAlpha (0.22f).overlaidWith (th.accentDim.withAlpha (0.4f)) : th.well;
    if (down) c = c.brighter (0.2f);
    else if (highlighted) c = c.brighter (0.07f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (on ? th.accent.withAlpha (0.9f) : (highlighted ? th.panelEdge.brighter (0.3f) : th.wellEdge));
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool highlighted, bool)
{
    const bool on = b.getToggleState();
    g.setColour (! b.isEnabled() ? th.textFaint : (on || highlighted ? th.text : th.textDim));
    g.setFont (on ? Type::label().boldened() : Type::label());
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (6, 2), juce::Justification::centred, 1, 0.85f);
}

void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto r = juce::Rectangle<int> (width, height).toFloat();
    g.setColour (th.panel.brighter (0.06f));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (th.panelEdge.brighter (0.2f));
    g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
    g.setColour (th.text);
    g.setFont (Type::label());
    g.drawFittedText (text, r.toNearestInt().reduced (8, 4), juce::Justification::centredLeft, 4);
}

void LookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h, bool isMouseOver, bool isMouseDragging)
{
    g.setColour ((isMouseOver || isMouseDragging) ? th.accent : th.textFaint);
    for (float i = 0.3f; i < 1.0f; i += 0.3f)
        g.drawLine ((float) w * i, (float) h, (float) w, (float) h * i, 1.2f);
}

} // namespace ap::ui
