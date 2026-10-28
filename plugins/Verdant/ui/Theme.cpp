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

float textWidth (const juce::Font& f, const juce::String& text)
{
    return juce::GlyphArrangement::getStringWidth (f, text);
}

void drawTitleText (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area,
                    juce::Colour colour, juce::Justification just, float size)
{
    g.setColour (colour);
    g.setFont (font (size, true).withExtraKerningFactor (0.08f));
    g.drawText (text.toUpperCase(), area, just, false);
}

//==============================================================================
juce::Colour modSourceColour (ModSource s) noexcept
{
    switch (s)
    {
        case ModSource::Lfo1: case ModSource::Lfo2: case ModSource::Lfo3: case ModSource::Lfo4:
            return juce::Colour (palette::modLfo);
        case ModSource::Env1: case ModSource::Env2: case ModSource::Env3:
            return juce::Colour (palette::modEnv);
        case ModSource::Ball: case ModSource::Spring: case ModSource::Orbit: case ModSource::Alive:
            return juce::Colour (palette::modPhysics);
        case ModSource::None: case ModSource::Count:
            return juce::Colour (palette::textDim);
        case ModSource::Velocity: case ModSource::Aftertouch: case ModSource::ModWheel: case ModSource::PitchBend:
        case ModSource::Note: case ModSource::Random: case ModSource::SeqMod:
        case ModSource::Macro1: case ModSource::Macro2: case ModSource::Macro3: case ModSource::Macro4:
        case ModSource::Macro5: case ModSource::Macro6: case ModSource::Macro7: case ModSource::Macro8:
        default:
            return juce::Colour (palette::modMacro);
    }
}

juce::String modSourceShortName (ModSource s)
{
    const int i = (int) s;
    static constexpr const char* names[] = { "-", "LFO 1", "LFO 2", "LFO 3", "LFO 4", "ENV 1", "ENV 2", "ENV 3", "VEL", "AT",
                                             "WHEEL", "BEND", "NOTE", "RAND", "M1", "M2", "M3", "M4", "M5", "M6", "M7", "M8",
                                             "BALL", "SPRING", "ORBIT", "ALIVE", "SEQ" };
    static_assert (std::size (names) == (size_t) ModSource::Count);
    return juce::jlimit (0, (int) ModSource::Count - 1, i) == i ? juce::String (names[i]) : juce::String ("-");
}

juce::String modSourceDisplayName (ModSource s)
{
    return juce::String::fromUTF8 (modSourceName (s));
}

//==============================================================================
VerdantLookAndFeel::VerdantLookAndFeel()
    : ap::ui::LookAndFeel (verdantTheme())
{
    const auto c = [] (juce::uint32 v) { return juce::Colour (v); };
    setColour (juce::ResizableWindow::backgroundColourId, c (palette::background));
    setColour (juce::TextButton::buttonColourId, c (palette::control));
    setColour (juce::TextButton::buttonOnColourId, c (palette::accentDim));
    setColour (juce::TextButton::textColourOffId, c (palette::text));
    setColour (juce::TextButton::textColourOnId, c (palette::text));
    setColour (juce::ComboBox::backgroundColourId, c (palette::control));
    setColour (juce::ComboBox::textColourId, c (palette::text));
    setColour (juce::ComboBox::outlineColourId, c (palette::panelEdge));
    setColour (juce::PopupMenu::backgroundColourId, c (palette::panel));
    setColour (juce::PopupMenu::textColourId, c (palette::text));
    setColour (juce::PopupMenu::headerTextColourId, c (palette::textDim));
    setColour (juce::PopupMenu::highlightedBackgroundColourId, c (palette::accentDim));
    setColour (juce::PopupMenu::highlightedTextColourId, c (palette::text));
    setColour (juce::TextEditor::backgroundColourId, c (palette::well));
    setColour (juce::TextEditor::textColourId, c (palette::text));
    setColour (juce::TextEditor::outlineColourId, c (palette::panelEdge));
    setColour (juce::TextEditor::focusedOutlineColourId, c (palette::accent));
    setColour (juce::TextEditor::highlightColourId, c (palette::accentDim));
    setColour (juce::TextEditor::highlightedTextColourId, c (palette::text));
    setColour (juce::CaretComponent::caretColourId, c (palette::accent));
    setColour (juce::Label::textColourId, c (palette::text));
    setColour (juce::Label::textWhenEditingColourId, c (palette::text));
    setColour (juce::Label::backgroundWhenEditingColourId, c (palette::well));
    setColour (juce::Label::outlineWhenEditingColourId, c (palette::accent));
    setColour (juce::ScrollBar::thumbColourId, c (palette::knobTrack));
    setColour (juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);
    setColour (juce::TooltipWindow::backgroundColourId, c (palette::control));
    setColour (juce::TooltipWindow::textColourId, c (palette::text));
    setColour (juce::TooltipWindow::outlineColourId, c (palette::panelEdge));
    setColour (juce::BubbleComponent::backgroundColourId, c (palette::control));
    setColour (juce::BubbleComponent::outlineColourId, c (palette::panelEdge));
    setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
}

//==============================================================================
void VerdantLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos,
                                           float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat();
    const float size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto centre = bounds.getCentre();
    const float trackW = size >= 50.0f ? 3.5f : (size >= 38.0f ? 3.0f : 2.5f);
    const float arcR = size * 0.5f - trackW * 0.5f - 0.5f;
    const bool enabled = slider.isEnabled();

    auto accent = juce::Colour (palette::accent);
    if (const auto* v = slider.getProperties().getVarPointer ("knobColour"))
        accent = juce::Colour ((juce::uint32) (juce::int64) *v);
    if (! enabled)
        accent = juce::Colour (palette::textFaint);

    const juce::PathStrokeType stroke (trackW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (juce::Colour (palette::knobTrack));
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
            // Soft glow: the same arc, wider and translucent, under the crisp one.
            g.setColour (accent.withAlpha (0.12f));
            g.strokePath (value, juce::PathStrokeType (trackW + 5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (accent.withAlpha (0.18f));
            g.strokePath (value, juce::PathStrokeType (trackW + 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (accent);
        g.strokePath (value, stroke);
    }

    // Centre disc: very subtle vertical gradient with a 1 px rim.
    const float bodyR = arcR - trackW * 0.5f - juce::jmax (2.5f, size * 0.07f);
    const auto body = juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (centre);
    g.setGradientFill (juce::ColourGradient (juce::Colour (palette::knobBodyTop), body.getCentreX(), body.getY(),
                                             juce::Colour (palette::knobBodyBot), body.getCentreX(), body.getBottom(), false));
    g.fillEllipse (body);
    g.setColour (juce::Colour (0xff343a44));
    g.drawEllipse (body.reduced (0.5f), 1.0f);

    // Pointer: a short light line from the rim inwards.
    const float a = angle - juce::MathConstants<float>::halfPi;
    const juce::Point<float> dir (std::cos (a), std::sin (a));
    juce::Path pointer;
    pointer.startNewSubPath (centre + dir * (bodyR * 0.42f));
    pointer.lineTo (centre + dir * (bodyR * 0.86f));
    g.setColour (enabled ? juce::Colour (palette::text) : juce::Colour (palette::textDim));
    g.strokePath (pointer, juce::PathStrokeType (juce::jmax (1.6f, size * 0.04f), juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
}

void VerdantLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos,
                                           float, float, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const bool horizontal = style != juce::Slider::LinearVertical && style != juce::Slider::LinearBarVertical;
    const auto area = juce::Rectangle<int> (x, y, w, h).toFloat();
    const float thickness = 4.0f;

    auto accent = juce::Colour (palette::accent);
    if (const auto* v = slider.getProperties().getVarPointer ("knobColour"))
        accent = juce::Colour ((juce::uint32) (juce::int64) *v);
    if (! slider.isEnabled())
        accent = juce::Colour (palette::textFaint);

    const auto track = horizontal
        ? juce::Rectangle<float> (area.getX(), area.getCentreY() - thickness * 0.5f, area.getWidth(), thickness)
        : juce::Rectangle<float> (area.getCentreX() - thickness * 0.5f, area.getY(), thickness, area.getHeight());

    g.setColour (juce::Colour (palette::knobTrack));
    g.fillRoundedRectangle (track, thickness * 0.5f);

    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float zero = horizontal
        ? (bipolar ? track.getX() + track.getWidth() * (float) slider.valueToProportionOfLength (0.0) : track.getX())
        : (bipolar ? track.getBottom() - track.getHeight() * (float) slider.valueToProportionOfLength (0.0) : track.getBottom());

    const auto fill = horizontal
        ? juce::Rectangle<float>::leftTopRightBottom (juce::jmin (zero, sliderPos), track.getY(), juce::jmax (zero, sliderPos), track.getBottom())
        : juce::Rectangle<float>::leftTopRightBottom (track.getX(), juce::jmin (zero, sliderPos), track.getRight(), juce::jmax (zero, sliderPos));
    g.setColour (accent);
    g.fillRoundedRectangle (fill, thickness * 0.5f);

    if (bipolar)
    {
        g.setColour (juce::Colour (palette::textFaint));
        if (horizontal) g.fillRect (juce::Rectangle<float> (zero - 0.5f, track.getY() - 2.0f, 1.0f, thickness + 4.0f));
        else            g.fillRect (juce::Rectangle<float> (track.getX() - 2.0f, zero - 0.5f, thickness + 4.0f, 1.0f));
    }

    const float r = 5.0f;
    const juce::Point<float> thumb = horizontal ? juce::Point<float> (sliderPos, track.getCentreY())
                                                : juce::Point<float> (track.getCentreX(), sliderPos);
    g.setColour (juce::Colour (palette::text));
    g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (thumb));
}

//==============================================================================
void VerdantLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                               bool highlighted, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    auto fill = on ? juce::Colour (palette::accentDim) : juce::Colour (palette::control);
    if (down)             fill = fill.brighter (0.18f);
    else if (highlighted) fill = on ? fill.brighter (0.08f) : juce::Colour (palette::controlHover);

    g.setColour (fill);
    g.fillRoundedRectangle (r, Theme::radius);
    g.setColour (on ? juce::Colour (palette::accent).withAlpha (0.8f) : juce::Colour (palette::panelEdge).brighter (0.1f));
    g.drawRoundedRectangle (r, Theme::radius, 1.0f);
}

void VerdantLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool highlighted, bool)
{
    const bool on = b.getToggleState();
    auto colour = on || highlighted ? juce::Colour (palette::text) : juce::Colour (palette::text).withAlpha (0.85f);
    if (! b.isEnabled())
        colour = juce::Colour (palette::textFaint);
    g.setColour (colour);
    g.setFont (font (Theme::fontBody, on));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (6, 2), juce::Justification::centred, 1, 0.9f);
}

void VerdantLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    // A pill switch followed by the button text.
    const auto bounds = b.getLocalBounds().toFloat();
    const float h = juce::jmin (14.0f, bounds.getHeight() - 4.0f), w = h * 1.8f;
    const auto pill = juce::Rectangle<float> (bounds.getX() + 1.0f, bounds.getCentreY() - h * 0.5f, w, h);
    const bool on = b.getToggleState();

    g.setColour (on ? juce::Colour (palette::accent) : juce::Colour (highlighted ? palette::controlHover : palette::knobTrack));
    g.fillRoundedRectangle (pill, h * 0.5f);
    const float d = h - 4.0f;
    const float tx = on ? pill.getRight() - 2.0f - d : pill.getX() + 2.0f;
    g.setColour (on ? juce::Colour (palette::background) : juce::Colour (palette::textDim));
    g.fillEllipse (tx, pill.getY() + 2.0f, d, d);

    g.setColour (b.isEnabled() ? juce::Colour (palette::text) : juce::Colour (palette::textDim));
    g.setFont (font (Theme::fontBody));
    g.drawFittedText (b.getButtonText(), bounds.withLeft (pill.getRight() + 6.0f).toNearestInt(),
                      juce::Justification::centredLeft, 1);
}

//==============================================================================
void VerdantLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool isDown, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<int> (0, 0, w, h).toFloat().reduced (0.5f);
    g.setColour (juce::Colour (isDown || box.isMouseOver (true) ? palette::controlHover : palette::control));
    g.fillRoundedRectangle (r, Theme::radius);
    g.setColour (box.hasKeyboardFocus (true) ? juce::Colour (palette::accent) : juce::Colour (palette::panelEdge).brighter (0.1f));
    g.drawRoundedRectangle (r, Theme::radius, 1.0f);

    // Chevron.
    const float cx = r.getRight() - 11.0f, cy = r.getCentreY();
    juce::Path chevron;
    chevron.startNewSubPath (cx - 3.5f, cy - 1.5f);
    chevron.lineTo (cx, cy + 2.0f);
    chevron.lineTo (cx + 3.5f, cy - 1.5f);
    g.setColour (juce::Colour (palette::textDim));
    g.strokePath (chevron, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font VerdantLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return font (Theme::fontBody);
}

void VerdantLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (4, 1, box.getWidth() - 22, box.getHeight() - 2);
    label.setFont (font (Theme::fontBody));
    label.setJustificationType (juce::Justification::centredLeft);
}

//==============================================================================
void VerdantLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (juce::Colour (palette::panel));
    g.setColour (juce::Colour (palette::panelEdge).brighter (0.15f));
    g.drawRect (0, 0, w, h, 1);
}

void VerdantLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                                            bool isSeparator, bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                                            const juce::String& text, const juce::String& shortcutKeyText,
                                            const juce::Drawable*, const juce::Colour* textColour)
{
    if (isSeparator)
    {
        g.setColour (juce::Colour (palette::panelEdge));
        g.fillRect (area.reduced (8, 0).withSizeKeepingCentre (area.getWidth() - 16, 1));
        return;
    }

    auto r = area.reduced (3, 1);
    if (isHighlighted && isActive)
    {
        g.setColour (juce::Colour (palette::accentDim));
        g.fillRoundedRectangle (r.toFloat(), Theme::radiusSmall);
    }

    auto textCol = textColour != nullptr ? *textColour : juce::Colour (palette::text);
    if (! isActive) textCol = juce::Colour (palette::textFaint);

    auto tickArea = r.removeFromLeft (20);
    if (isTicked)
    {
        g.setColour (isHighlighted ? juce::Colour (palette::text) : juce::Colour (palette::accent));
        g.fillEllipse (tickArea.toFloat().withSizeKeepingCentre (6.0f, 6.0f));
    }

    if (hasSubMenu)
    {
        auto arrowArea = r.removeFromRight (16).toFloat();
        const float cx = arrowArea.getCentreX(), cy = arrowArea.getCentreY();
        juce::Path chevron;
        chevron.startNewSubPath (cx - 1.5f, cy - 3.5f);
        chevron.lineTo (cx + 2.0f, cy);
        chevron.lineTo (cx - 1.5f, cy + 3.5f);
        g.setColour (juce::Colour (palette::textDim));
        g.strokePath (chevron, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    g.setFont (getPopupMenuFont());
    if (shortcutKeyText.isNotEmpty())
    {
        g.setColour (juce::Colour (palette::textDim));
        g.drawText (shortcutKeyText, r.removeFromRight (64).withTrimmedRight (6), juce::Justification::centredRight, true);
    }
    g.setColour (textCol);
    g.drawFittedText (text, r.withTrimmedRight (6), juce::Justification::centredLeft, 1);
}

void VerdantLookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& sectionName)
{
    drawTitleText (g, sectionName, area.withTrimmedLeft (10).withTrimmedTop (4), juce::Colour (palette::textDim),
                   juce::Justification::centredLeft, Theme::fontSmall);
}

void VerdantLookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int,
                                                    int& idealWidth, int& idealHeight)
{
    if (isSeparator)
    {
        idealWidth = 50;
        idealHeight = 9;
        return;
    }
    idealHeight = 24;
    idealWidth = (int) std::ceil (textWidth (getPopupMenuFont(), text)) + 20 + 16 + 16;
}

juce::Font VerdantLookAndFeel::getPopupMenuFont()
{
    return font (Theme::fontBody);
}

//==============================================================================
void VerdantLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto r = juce::Rectangle<int> (width, height).toFloat();
    g.setColour (juce::Colour (palette::control));
    g.fillRoundedRectangle (r, Theme::radiusSmall);
    g.setColour (juce::Colour (palette::panelEdge).brighter (0.2f));
    g.drawRoundedRectangle (r.reduced (0.5f), Theme::radiusSmall, 1.0f);
    g.setColour (juce::Colour (palette::text));
    g.setFont (font (Theme::fontBody));
    g.drawFittedText (text, juce::Rectangle<int> (width, height).reduced (8, 4), juce::Justification::centredLeft, 4);
}

juce::Rectangle<int> VerdantLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                                           juce::Rectangle<int> parentArea)
{
    const int w = juce::jmin (320, (int) std::ceil (textWidth (font (Theme::fontBody), tipText)) + 18);
    const int lines = (int) std::ceil ((textWidth (font (Theme::fontBody), tipText) + 18.0f) / 320.0f);
    const int h = 10 + 16 * juce::jmax (1, lines);
    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6)  : screenPos.y + 6,
                                 w, h).constrainedWithin (parentArea);
}

void VerdantLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height,
                                        bool vertical, int thumbStart, int thumbSize, bool isMouseOver, bool isMouseDown)
{
    const auto thumb = vertical ? juce::Rectangle<int> (x, thumbStart, width, thumbSize)
                                : juce::Rectangle<int> (thumbStart, y, thumbSize, height);
    const auto r = thumb.toFloat().reduced (vertical ? 2.0f : 1.0f, vertical ? 1.0f : 2.0f);
    g.setColour (juce::Colour (isMouseDown ? palette::textDim : (isMouseOver ? palette::textFaint : palette::knobTrack)));
    g.fillRoundedRectangle (r, juce::jmin (r.getWidth(), r.getHeight()) * 0.5f);
}

void VerdantLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (juce::Rectangle<int> (width, height).toFloat().reduced (0.5f), Theme::radiusSmall);
}

void VerdantLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    if (! editor.isEnabled())
        return;
    const bool focused = editor.hasKeyboardFocus (true) && ! editor.isReadOnly();
    g.setColour (focused ? juce::Colour (palette::accent) : juce::Colour (palette::panelEdge).brighter (0.1f));
    g.drawRoundedRectangle (juce::Rectangle<int> (width, height).toFloat().reduced (0.5f), Theme::radiusSmall, 1.0f);
}

void VerdantLookAndFeel::drawBubble (juce::Graphics& g, juce::BubbleComponent&, const juce::Point<float>&,
                                     const juce::Rectangle<float>& body)
{
    g.setColour (juce::Colour (palette::control));
    g.fillRoundedRectangle (body, Theme::radiusSmall);
    g.setColour (juce::Colour (palette::accent).withAlpha (0.6f));
    g.drawRoundedRectangle (body.reduced (0.5f), Theme::radiusSmall, 1.0f);
}

void VerdantLookAndFeel::drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics& g, const juce::Path& path, juce::Image&)
{
    g.setColour (juce::Colour (palette::panel));
    g.fillPath (path);
    g.setColour (juce::Colour (palette::panelEdge).brighter (0.2f));
    g.strokePath (path, juce::PathStrokeType (1.0f));
}

void VerdantLookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h, bool isMouseOver, bool isMouseDragging)
{
    // Six small dots in a triangle, brighter on hover.
    g.setColour (juce::Colour (isMouseOver || isMouseDragging ? palette::textDim : palette::textFaint));
    const float s = (float) juce::jmin (w, h);
    const float step = s / 4.0f, d = juce::jmax (1.5f, s / 9.0f);
    for (int row = 1; row <= 3; ++row)
        for (int k = 0; k < row; ++k)
            g.fillEllipse ((float) w - step * (float) (k + 1) + step * 0.5f - d * 0.5f - 1.0f,
                           (float) h - step * (float) (4 - row) + step * 0.5f - d * 0.5f - 1.0f, d, d);
}

} // namespace verdant::ui
