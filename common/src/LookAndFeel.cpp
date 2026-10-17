#include "ap_ui/LookAndFeel.h"

namespace ap::ui
{

LookAndFeel::LookAndFeel (Theme theme) : th (theme)
{
    setColour (juce::ResizableWindow::backgroundColourId, th.background);
    setColour (juce::Slider::textBoxTextColourId, th.text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, th.accentDim);
    setColour (juce::Label::textColourId, th.text);
    setColour (juce::Label::textWhenEditingColourId, th.text);
    setColour (juce::TextEditor::highlightColourId, th.accentDim);
    setColour (juce::TextEditor::focusedOutlineColourId, th.accent);
    setColour (juce::ComboBox::textColourId, th.text);
    setColour (juce::ComboBox::backgroundColourId, th.knobTrack);
    setColour (juce::ComboBox::outlineColourId, th.panelEdge);
    setColour (juce::ComboBox::arrowColourId, th.textDim);
    setColour (juce::PopupMenu::backgroundColourId, th.panel);
    setColour (juce::PopupMenu::textColourId, th.text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, th.accentDim);
    setColour (juce::PopupMenu::highlightedTextColourId, th.text);
    setColour (juce::ToggleButton::textColourId, th.text);
    setColour (juce::ToggleButton::tickColourId, th.accent);
    setColour (juce::ToggleButton::tickDisabledColourId, th.textDim);
    setColour (juce::TextButton::buttonColourId, th.knobTrack);
    setColour (juce::TextButton::buttonOnColourId, th.accentDim);
    setColour (juce::TextButton::textColourOffId, th.text);
    setColour (juce::TextButton::textColourOnId, th.text);
    setColour (juce::TooltipWindow::backgroundColourId, th.panel);
    setColour (juce::TooltipWindow::textColourId, th.text);
    setColour (juce::TooltipWindow::outlineColourId, th.panelEdge);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos,
                                    float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (3.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const float lineW = juce::jmax (2.0f, radius * 0.14f);
    const float arcRadius = radius - lineW * 0.5f;
    const bool enabled = slider.isEnabled();

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (th.knobTrack);
    g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Bipolar sliders fill from the centre, unipolar from the start.
    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float zeroPos = bipolar ? (float) ((0.0 - slider.getMinimum()) / (slider.getMaximum() - slider.getMinimum())) : 0.0f;
    const float zeroAngle = startAngle + zeroPos * (endAngle - startAngle);

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                         juce::jmin (zeroAngle, angle), juce::jmax (zeroAngle, angle), true);
    g.setColour (enabled ? th.accent : th.textDim);
    g.strokePath (value, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Knob body.
    const float bodyR = arcRadius - lineW * 1.3f;
    g.setColour (th.panel.brighter (0.08f));
    g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
    g.setColour (th.panelEdge);
    g.drawEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

    // Pointer.
    const juce::Point<float> tip (centre.x + bodyR * 0.85f * std::cos (angle - juce::MathConstants<float>::halfPi),
                                  centre.y + bodyR * 0.85f * std::sin (angle - juce::MathConstants<float>::halfPi));
    const juce::Point<float> base (centre.x + bodyR * 0.35f * std::cos (angle - juce::MathConstants<float>::halfPi),
                                   centre.y + bodyR * 0.35f * std::sin (angle - juce::MathConstants<float>::halfPi));
    g.setColour (enabled ? th.text : th.textDim);
    g.drawLine (juce::Line<float> (base, tip), juce::jmax (2.0f, lineW * 0.8f));
}

void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos,
                                    float, float, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const bool horizontal = style == juce::Slider::LinearHorizontal || style == juce::Slider::LinearBar;
    const auto area = juce::Rectangle<int> (x, y, w, h).toFloat();
    const float thickness = 6.0f;

    juce::Rectangle<float> track = horizontal
        ? juce::Rectangle<float> (area.getX(), area.getCentreY() - thickness * 0.5f, area.getWidth(), thickness)
        : juce::Rectangle<float> (area.getCentreX() - thickness * 0.5f, area.getY(), thickness, area.getHeight());

    g.setColour (th.knobTrack);
    g.fillRoundedRectangle (track, thickness * 0.5f);

    juce::Rectangle<float> fill = horizontal
        ? track.withWidth (sliderPos - track.getX())
        : track.withTop (sliderPos);
    g.setColour (slider.isEnabled() ? th.accent : th.textDim);
    g.fillRoundedRectangle (fill, thickness * 0.5f);

    const float r = 7.0f;
    const juce::Point<float> thumb = horizontal ? juce::Point<float> (sliderPos, track.getCentreY())
                                                : juce::Point<float> (track.getCentreX(), sliderPos);
    g.setColour (th.text);
    g.fillEllipse (thumb.x - r, thumb.y - r, r * 2.0f, r * 2.0f);
}

juce::Label* LookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (slider);
    l->setFont (valueFont());
    l->setJustificationType (juce::Justification::centred);
    l->setColour (juce::Label::textColourId, th.textDim);
    l->setColour (juce::Label::outlineWhenEditingColourId, th.accent);
    return l;
}

juce::Font LookAndFeel::getLabelFont (juce::Label&)
{
    return labelFont();
}

void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    const auto bounds = b.getLocalBounds().toFloat();
    const float boxSize = juce::jmin (18.0f, bounds.getHeight() - 4.0f);
    const auto box = juce::Rectangle<float> (bounds.getX() + 2.0f, bounds.getCentreY() - boxSize * 0.5f, boxSize, boxSize);
    const bool on = b.getToggleState();

    g.setColour (on ? th.accent : (highlighted ? th.knobTrack.brighter (0.2f) : th.knobTrack));
    g.fillRoundedRectangle (box, 4.0f);
    g.setColour (th.panelEdge);
    g.drawRoundedRectangle (box, 4.0f, 1.0f);

    if (on)
    {
        juce::Path tick;
        tick.startNewSubPath (box.getX() + boxSize * 0.25f, box.getCentreY());
        tick.lineTo (box.getX() + boxSize * 0.45f, box.getBottom() - boxSize * 0.28f);
        tick.lineTo (box.getRight() - boxSize * 0.22f, box.getY() + boxSize * 0.28f);
        g.setColour (th.background);
        g.strokePath (tick, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    g.setColour (b.isEnabled() ? th.text : th.textDim);
    g.setFont (labelFont());
    g.drawFittedText (b.getButtonText(), bounds.withLeft (box.getRight() + 6.0f).toNearestInt(),
                      juce::Justification::centredLeft, 1);
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<int> (0, 0, w, h).toFloat().reduced (0.5f);
    g.setColour (th.knobTrack);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (box.hasKeyboardFocus (true) ? th.accent : th.panelEdge);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);

    juce::Path arrow;
    const float ax = r.getRight() - 14.0f, ay = r.getCentreY();
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (th.textDim);
    g.fillPath (arrow);
}

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return valueFont();
}

void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (6, 1, box.getWidth() - 26, box.getHeight() - 2);
    label.setFont (valueFont());
    label.setJustificationType (juce::Justification::centredLeft);
}

void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (th.panel);
    g.setColour (th.panelEdge);
    g.drawRect (0, 0, w, h, 1);
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                        bool highlighted, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    juce::Colour c = b.getToggleState() ? th.accentDim : th.knobTrack;
    if (down) c = c.brighter (0.25f);
    else if (highlighted) c = c.brighter (0.1f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (b.getToggleState() ? th.accent : th.panelEdge);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
}

} // namespace ap::ui
