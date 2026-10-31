#include "Widgets.h"

namespace verdant::ui
{

namespace
{
    juce::Colour col (juce::uint32 c) { return juce::Colour (c); }

    void drawCellLabel (juce::Graphics& g, const juce::String& label, juce::Rectangle<int> bounds, bool enabled)
    {
        if (label.isEmpty())
            return;
        g.setColour (enabled ? col (palette::textDim) : col (palette::textFaint));
        g.setFont (font (Theme::fontSmall));
        g.drawFittedText (label, bounds.removeFromTop (Cell::labelHeight), juce::Justification::centred, 1, 0.85f);
    }
}

//==============================================================================
void paintSection (juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& title, juce::Colour marker, bool active)
{
    const auto r = bounds.toFloat().reduced (0.5f);
    g.setColour (col (palette::panel));
    g.fillRoundedRectangle (r, Theme::cardRadius);
    g.setColour (col (palette::panelEdge));
    g.drawRoundedRectangle (r, Theme::cardRadius, 1.0f);

    // Faint 1 px highlight along the top edge (inside the corners).
    {
        juce::ColourGradient hl (col (palette::text).withAlpha (0.0f), r.getX() + Theme::cardRadius, 0.0f,
                                 col (palette::text).withAlpha (0.0f), r.getRight() - Theme::cardRadius, 0.0f, false);
        hl.addColour (0.5, col (palette::text).withAlpha (0.07f));
        g.setGradientFill (hl);
        g.fillRect (juce::Rectangle<float> (r.getX() + Theme::cardRadius, r.getY() + 1.0f, r.getWidth() - 2.0f * Theme::cardRadius, 1.0f));
    }

    if (title.isNotEmpty())
    {
        auto header = bounds.removeFromTop (Theme::sectionHeader).withTrimmedTop (8).withTrimmedLeft (SectionPanel::padding);
        g.setColour (active ? marker : marker.withAlpha (0.35f));
        g.fillRoundedRectangle (juce::Rectangle<float> (3.0f, 12.0f).withCentre ({ (float) header.getX() + 1.5f, (float) header.getCentreY() }), 1.5f);
        drawTitleText (g, title, header.withTrimmedLeft (10), active ? col (palette::text) : col (palette::textDim));
    }
}

void paintDisplay (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour accent)
{
    g.setGradientFill (juce::ColourGradient (col (palette::well), 0.0f, r.getY(), col (palette::display), 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, Theme::radius);

    g.saveState();
    juce::Path clip;
    clip.addRoundedRectangle (r, Theme::radius);
    g.reduceClipRegion (clip);
    for (int i = 0; i < 4; ++i)
    {
        g.setColour (accent.withAlpha (0.045f - 0.01f * (float) i));
        g.drawRoundedRectangle (r.reduced (1.0f + 1.5f * (float) i), Theme::radius, 1.5f);
    }
    g.restoreState();

    g.setColour (col (0xff2c323c));
    g.drawRoundedRectangle (r.reduced (0.5f), Theme::radius, 1.0f);
}

SectionPanel::SectionPanel (const juce::String& t, juce::Colour m) : title (t), marker (m)
{
    setInterceptsMouseClicks (false, true);
}

void SectionPanel::setTitle (const juce::String& t)    { title = t; repaint(); }
void SectionPanel::setMarkerColour (juce::Colour c)     { marker = c; repaint(); }
void SectionPanel::setActive (bool a)                   { if (active != a) { active = a; repaint(); } }

juce::Rectangle<int> SectionPanel::headerArea() const
{
    const auto f = font (Theme::fontTitle, true).withExtraKerningFactor (0.08f);
    const int titleW = title.isEmpty() ? 0 : (int) std::ceil (textWidth (f, title.toUpperCase())) + 10 + Theme::spaceM;
    return getLocalBounds().removeFromTop (Theme::sectionHeader).withTrimmedTop (8).withTrimmedLeft (padding)
                           .withTrimmedRight (padding).withTrimmedLeft (titleW);
}

juce::Rectangle<int> SectionPanel::contentArea() const
{
    auto r = getLocalBounds();
    if (title.isNotEmpty())
        r.removeFromTop (Theme::sectionHeader);
    else
        r.removeFromTop (padding);
    return r.withTrimmedLeft (padding).withTrimmedRight (padding).withTrimmedBottom (padding - 4);
}

void SectionPanel::clearCaptions()                                         { captions.clear(); dividers.clear(); repaint(); }
void SectionPanel::addCaption (const juce::String& t, juce::Rectangle<int> line) { captions.emplace_back (t, line); repaint(); }
void SectionPanel::addDivider (juce::Rectangle<int> line)                    { dividers.push_back (line); repaint(); }

void SectionPanel::paint (juce::Graphics& g)
{
    paintSection (g, getLocalBounds(), title, marker, active);

    const auto f = font (10.0f, true).withExtraKerningFactor (0.1f);
    for (const auto& [text, line] : captions)
    {
        const auto upper = text.toUpperCase();
        const int tw = (int) std::ceil (textWidth (f, upper));
        g.setColour (col (palette::textFaint));
        g.setFont (f);
        g.drawText (upper, line.withWidth (tw + 2), juce::Justification::centredLeft, false);
        g.setColour (col (palette::panelEdge));
        g.fillRect (line.withTrimmedLeft (tw + 8).withSizeKeepingCentre (juce::jmax (0, line.getWidth() - tw - 8), 1));
    }
    g.setColour (col (palette::panelEdge));
    for (const auto& d : dividers)
        g.fillRect (d.withSizeKeepingCentre (1, d.getHeight()));
}

//==============================================================================
ChoiceButton::ChoiceButton (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId, const juce::String& l)
    : label (l)
{
    param = apvts.getParameter (paramId);
    jassert (param != nullptr);
    if (param == nullptr)
        return;

    int offset = 0;
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (param))
        items = c->choices;
    else if (auto* i = dynamic_cast<juce::AudioParameterInt*> (param))
    {
        offset = i->getRange().getStart();
        for (int v = i->getRange().getStart(); v <= i->getRange().getEnd(); ++v)
            items.add (juce::String (v));
    }
    else
        items = { "Off", "On" };

    attachment = std::make_unique<juce::ParameterAttachment> (*param, [this, offset] (float v)
    {
        index = juce::jlimit (0, juce::jmax (0, items.size() - 1), (int) std::lround (v) - offset);
        repaint();
        if (onChange) onChange (index);
    });
    attachment->sendInitialUpdate();
    setTooltip (param->getName (64));
}

ChoiceButton::ChoiceButton (const juce::StringArray& list, const juce::String& l)
    : items (list), label (l)
{
}

ChoiceButton::~ChoiceButton() = default;

void ChoiceButton::setLabel (const juce::String& l)  { label = l; repaint(); }
void ChoiceButton::setAccentColour (juce::Colour c)  { accent = c; repaint(); }
void ChoiceButton::setInlineLabel (const juce::String& l) { inlineLabel = l; repaint(); }
void ChoiceButton::setGhost (bool g)                    { ghost = g; repaint(); }
void ChoiceButton::setItems (const juce::StringArray& l) { items = l; index = juce::jlimit (0, juce::jmax (0, items.size() - 1), index); repaint(); }

juce::String ChoiceButton::getText() const
{
    if (textForIndex) return textForIndex (index);
    return items[index];
}

void ChoiceButton::setIndex (int i, juce::NotificationType n)
{
    i = juce::jlimit (0, juce::jmax (0, items.size() - 1), i);
    if (attachment != nullptr)
    {
        int offset = 0;
        if (auto* p = dynamic_cast<juce::AudioParameterInt*> (param))
            offset = p->getRange().getStart();
        if (i != index)
            attachment->setValueAsCompleteGesture ((float) (i + offset));   // the callback updates index + onChange
        return;
    }
    if (i == index)
        return;
    index = i;
    repaint();
    if (n != juce::dontSendNotification && onChange)
        onChange (index);
}

void ChoiceButton::choose (int newIndex) { setIndex (newIndex); }

juce::Rectangle<int> ChoiceButton::buttonBounds() const
{
    if (label.isEmpty())
        return getLocalBounds();
    return Cell::middle (getLocalBounds()).withSizeKeepingCentre (getWidth(), Theme::controlHeight);
}

void ChoiceButton::paint (juce::Graphics& g)
{
    const bool enabled = isEnabled();
    drawCellLabel (g, label, getLocalBounds(), enabled);

    const auto r = buttonBounds().toFloat().reduced (0.5f);
    const bool hover = isMouseOver (true) && enabled;
    if (! ghost || hover)
    {
        g.setColour (col (hover ? palette::controlHover : palette::control));
        g.fillRoundedRectangle (r, Theme::radius);
        g.setColour (col (palette::panelEdge).brighter (0.1f));
        g.drawRoundedRectangle (r, Theme::radius, 1.0f);
    }

    const float cx = r.getRight() - 9.0f, cy = r.getCentreY();
    juce::Path chevron;
    chevron.startNewSubPath (cx - 3.0f, cy - 1.5f);
    chevron.lineTo (cx, cy + 1.5f);
    chevron.lineTo (cx + 3.0f, cy - 1.5f);
    g.setColour (col (palette::textDim));
    g.strokePath (chevron, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    auto textArea = r.toNearestInt().withTrimmedLeft (8).withTrimmedRight (17);
    if (inlineLabel.isNotEmpty())
    {
        g.setColour (col (palette::textDim));
        g.setFont (font (Theme::fontSmall));
        g.drawText (inlineLabel, textArea, juce::Justification::centredLeft, false);
        textArea = textArea.withTrimmedLeft ((int) std::ceil (textWidth (font (Theme::fontSmall), inlineLabel)) + 6);
    }
    g.setColour (enabled ? accent : col (palette::textFaint));
    g.setFont (font (Theme::fontBody));
    g.drawFittedText (getText(), textArea, inlineLabel.isNotEmpty() ? juce::Justification::centredRight : juce::Justification::centredLeft, 1, 0.8f);
}

void ChoiceButton::mouseDown (const juce::MouseEvent& e)
{
    if (isEnabled() && buttonBounds().contains (e.getPosition()))
        showMenu();
}

void ChoiceButton::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (! isEnabled() || items.isEmpty())
        return;
    const float d = std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : -w.deltaX;
    if (std::abs (d) > 0.0f)
        choose (juce::jlimit (0, items.size() - 1, index + (d > 0.0f ? -1 : 1)));
}

void ChoiceButton::showMenu()
{
    juce::PopupMenu menu;
    if (menuBuilder)
        menu = menuBuilder (index);
    else
        for (int i = 0; i < items.size(); ++i)
            menu.addItem (i + 1, items[i], true, i == index);

    menu.setLookAndFeel (&getLookAndFeel());
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                                  .withMinimumWidth (buttonBounds().getWidth())
                                                  .withStandardItemHeight (24),
                        [safe = juce::Component::SafePointer<ChoiceButton> (this)] (int result)
                        {
                            if (safe != nullptr && result > 0)
                                safe->choose (result - 1);
                        });
}

//==============================================================================
TogglePill::TogglePill (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId, const juce::String& t, Style s)
    : text (t), style (s)
{
    if (auto* p = apvts.getParameter (paramId))
    {
        attachment = std::make_unique<juce::ParameterAttachment> (*p, [this] (float v)
        {
            state = v >= 0.5f;
            repaint();
            if (onChange) onChange (state);
        });
        attachment->sendInitialUpdate();
        setTooltip (p->getName (64));
    }
    else
        jassertfalse;
}

TogglePill::TogglePill (const juce::String& t, Style s) : text (t), style (s) {}
TogglePill::~TogglePill() = default;

void TogglePill::setText (const juce::String& t)     { text = t; repaint(); }
void TogglePill::setLabel (const juce::String& l)    { label = l; repaint(); }
void TogglePill::setAccentColour (juce::Colour c)    { accent = c; repaint(); }

void TogglePill::setState (bool s, juce::NotificationType n)
{
    if (attachment != nullptr)
    {
        if (s != state)
            attachment->setValueAsCompleteGesture (s ? 1.0f : 0.0f);
        return;
    }
    if (s == state)
        return;
    state = s;
    repaint();
    if (n != juce::dontSendNotification && onChange)
        onChange (state);
}

void TogglePill::mouseUp (const juce::MouseEvent& e)
{
    if (isEnabled() && getLocalBounds().contains (e.getPosition()) && ! e.mouseWasDraggedSinceMouseDown())
        setState (! state);
}

void TogglePill::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    const bool hover = isMouseOver (true) && isEnabled();
    if (label.isNotEmpty())
    {
        drawCellLabel (g, label, bounds, isEnabled());
        bounds = Cell::middle (bounds);
    }
    const auto area = bounds.toFloat();

    switch (style)
    {
        case Style::Power:
        {
            const float d = juce::jmin (18.0f, area.getHeight(), area.getWidth());
            const auto circle = juce::Rectangle<float> (d, d).withCentre (area.getCentre());
            g.setColour (state ? accent.withAlpha (0.18f) : col (hover ? palette::controlHover : palette::control));
            g.fillEllipse (circle);
            g.setColour (state ? accent : col (palette::panelEdge).brighter (0.2f));
            g.drawEllipse (circle.reduced (0.5f), 1.0f);
            IconButton::drawIcon (g, IconButton::Icon::Power, circle.reduced (d * 0.24f),
                                  state ? accent : col (palette::textDim), 1.5f);
            break;
        }
        case Style::Text:
        {
            const auto r = area.reduced (0.5f);
            g.setColour (state ? accent.withAlpha (0.16f) : col (hover ? palette::controlHover : palette::control));
            g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
            g.setColour (state ? accent.withAlpha (0.85f) : col (palette::panelEdge).brighter (0.15f));
            g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
            g.setColour (state ? accent : col (palette::textDim));
            g.setFont (font (Theme::fontSmall, true));
            g.drawFittedText (text, r.toNearestInt().reduced (6, 0), juce::Justification::centred, 1, 0.8f);
            break;
        }
        case Style::Switch:
        default:
        {
            const float h = juce::jmin (14.0f, area.getHeight()), w = h * 1.85f;
            const bool hasText = text.isNotEmpty();
            const auto pill = hasText ? juce::Rectangle<float> (area.getX(), area.getCentreY() - h * 0.5f, w, h)
                                      : juce::Rectangle<float> (w, h).withCentre (area.getCentre());
            g.setColour (state ? accent : col (hover ? palette::controlHover : palette::knobTrack));
            g.fillRoundedRectangle (pill, h * 0.5f);
            const float d = h - 4.0f;
            g.setColour (state ? col (palette::background) : col (palette::textDim));
            g.fillEllipse (state ? pill.getRight() - 2.0f - d : pill.getX() + 2.0f, pill.getY() + 2.0f, d, d);
            if (hasText)
            {
                g.setColour (isEnabled() ? col (palette::text) : col (palette::textDim));
                g.setFont (font (Theme::fontBody));
                g.drawFittedText (text, area.withTrimmedLeft (w + 6.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);
            }
            break;
        }
    }
}

//==============================================================================
SegmentedControl::SegmentedControl (const juce::StringArray& list) { setItems (list); }
SegmentedControl::~SegmentedControl() = default;

void SegmentedControl::setItems (const juce::StringArray& list)
{
    items = list;
    dots.clearQuick();
    for (int i = 0; i < items.size(); ++i)
        dots.add (juce::Colours::transparentBlack);
    selected = juce::jlimit (0, juce::jmax (0, items.size() - 1), selected);
    repaint();
}

void SegmentedControl::bindToParameter (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId)
{
    param = apvts.getParameter (paramId);
    if (param == nullptr)
    {
        jassertfalse;
        return;
    }
    if (items.isEmpty())
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (param))
            setItems (c->choices);

    attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v)
    {
        selected = juce::jlimit (0, juce::jmax (0, items.size() - 1), (int) std::lround (v));
        repaint();
        if (onChange) onChange (selected);
    });
    attachment->sendInitialUpdate();
}

void SegmentedControl::setSelected (int i, juce::NotificationType n)
{
    i = juce::jlimit (0, juce::jmax (0, items.size() - 1), i);
    if (attachment != nullptr)
    {
        if (i != selected)
            attachment->setValueAsCompleteGesture ((float) i);
        return;
    }
    if (i == selected)
        return;
    selected = i;
    repaint();
    if (n != juce::dontSendNotification && onChange)
        onChange (selected);
}

void SegmentedControl::setAccentColour (juce::Colour c) { accent = c; repaint(); }
void SegmentedControl::setFontSize (float s)            { fontSize = s; repaint(); }
void SegmentedControl::setFilledSelection (bool f)      { filled = f; repaint(); }

void SegmentedControl::setSegmentDot (int segment, juce::Colour c)
{
    if (juce::isPositiveAndBelow (segment, dots.size()) && dots[segment] != c)
    {
        dots.set (segment, c);
        repaint();
    }
}

juce::Rectangle<float> SegmentedControl::segmentBounds (int i) const
{
    const auto r = getLocalBounds().toFloat().reduced (2.0f);
    const float w = r.getWidth() / (float) juce::jmax (1, items.size());
    return { r.getX() + w * (float) i, r.getY(), w, r.getHeight() };
}

int SegmentedControl::segmentAt (juce::Point<int> p) const
{
    for (int i = 0; i < items.size(); ++i)
        if (segmentBounds (i).contains (p.toFloat()))
            return i;
    return -1;
}

void SegmentedControl::paint (juce::Graphics& g)
{
    const auto frame = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (col (palette::well));
    g.fillRoundedRectangle (frame, Theme::radius);
    g.setColour (col (palette::panelEdge));
    g.drawRoundedRectangle (frame, Theme::radius, 1.0f);

    for (int i = 0; i < items.size(); ++i)
    {
        const auto r = segmentBounds (i);
        const bool on = i == selected;
        if (on && filled)
        {
            g.setColour (accent.withAlpha (0.22f));
            g.fillRoundedRectangle (r.expanded (1.5f), Theme::radiusSmall + 1.0f);
            g.setColour (accent);
            g.fillRoundedRectangle (r, Theme::radiusSmall);
        }
        else if (on)
        {
            g.setColour (accent.withAlpha (0.16f));
            g.fillRoundedRectangle (r, Theme::radiusSmall);
            g.setColour (accent.withAlpha (0.7f));
            g.drawRoundedRectangle (r.reduced (0.5f), Theme::radiusSmall, 1.0f);
        }
        else if (i == hover)
        {
            g.setColour (col (palette::controlHover));
            g.fillRoundedRectangle (r, Theme::radiusSmall);
        }

        auto textArea = r.toNearestInt();
        const auto dot = dots[i];
        const auto f = font (fontSize, on);
        if (dot.getAlpha() > 0)
        {
            const float tw = textWidth (f, items[i]);
            const float x = r.getCentreX() - (tw + 10.0f) * 0.5f;
            g.setColour (dot);
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ x + 2.5f, r.getCentreY() }));
            textArea = textArea.withTrimmedLeft (10);
        }
        g.setColour (on ? (filled ? col (palette::background) : col (palette::text)) : col (palette::textDim));
        g.setFont (f);
        g.drawFittedText (items[i], textArea.reduced (2, 0), juce::Justification::centred, 1, 0.8f);
    }
}

void SegmentedControl::mouseDown (const juce::MouseEvent& e)
{
    const int i = segmentAt (e.getPosition());
    if (i >= 0 && isEnabled())
        setSelected (i);
}

void SegmentedControl::mouseMove (const juce::MouseEvent& e)
{
    const int h = segmentAt (e.getPosition());
    if (h != hover) { hover = h; repaint(); }
}

void SegmentedControl::mouseExit (const juce::MouseEvent&)
{
    if (hover != -1) { hover = -1; repaint(); }
}

//==============================================================================
IconButton::IconButton (Icon i, const juce::String& tooltip, const juce::String& buttonText)
    : juce::Button (buttonText), icon (i)
{
    setTooltip (tooltip);
}

void IconButton::setIcon (Icon i)            { icon = i; repaint(); }
void IconButton::setFrameless (bool f)        { frameless = f; repaint(); }
void IconButton::setIconColour (juce::Colour c) { iconColour = c; repaint(); }

void IconButton::drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> a, juce::Colour c, float sw)
{
    const juce::PathStrokeType stroke (sw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    const float s = juce::jmin (a.getWidth(), a.getHeight());
    a = a.withSizeKeepingCentre (s, s);
    const float x = a.getX(), y = a.getY(), cx = a.getCentreX(), cy = a.getCentreY();
    juce::Path p;
    g.setColour (c);

    switch (icon)
    {
        case Icon::ChevronLeft:
            p.startNewSubPath (cx + s * 0.15f, y + s * 0.2f); p.lineTo (cx - s * 0.17f, cy); p.lineTo (cx + s * 0.15f, y + s * 0.8f);
            g.strokePath (p, stroke); break;
        case Icon::ChevronRight:
            p.startNewSubPath (cx - s * 0.15f, y + s * 0.2f); p.lineTo (cx + s * 0.17f, cy); p.lineTo (cx - s * 0.15f, y + s * 0.8f);
            g.strokePath (p, stroke); break;
        case Icon::ChevronDown:
            p.startNewSubPath (x + s * 0.22f, cy - s * 0.12f); p.lineTo (cx, cy + s * 0.16f); p.lineTo (x + s * 0.78f, cy - s * 0.12f);
            g.strokePath (p, stroke); break;
        case Icon::Lock:
        case Icon::Unlock:
        {
            const auto body = juce::Rectangle<float> (x + s * 0.18f, y + s * 0.45f, s * 0.64f, s * 0.47f);
            g.fillRoundedRectangle (body, s * 0.08f);
            juce::Path shackle;
            const float r = s * 0.2f;
            const float left = cx - r, right = cx + r, top = y + s * 0.1f;
            shackle.startNewSubPath (left, body.getY());
            shackle.lineTo (left, top + r);
            shackle.addArc (left, top, r * 2.0f, r * 2.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi);
            if (icon == Icon::Lock)
                shackle.lineTo (right, body.getY());
            else
                shackle.lineTo (right, top + r * 0.9f);
            g.strokePath (shackle, stroke);
            break;
        }
        case Icon::Reroll:
        {
            const float r = s * 0.34f;
            p.addCentredArc (cx, cy, r, r, 0.0f, 0.6f, juce::MathConstants<float>::twoPi - 0.2f, true);
            g.strokePath (p, stroke);
            const float ea = juce::MathConstants<float>::twoPi - 0.2f - juce::MathConstants<float>::halfPi;
            const juce::Point<float> end (cx + r * std::cos (ea), cy + r * std::sin (ea));
            juce::Path head;
            head.addTriangle (end.x - s * 0.14f, end.y - s * 0.1f, end.x + s * 0.12f, end.y - s * 0.12f, end.x - 0.0f, end.y + s * 0.14f);
            g.fillPath (head);
            break;
        }
        case Icon::Brush:
        {
            p.startNewSubPath (x + s * 0.82f, y + s * 0.14f);
            p.lineTo (x + s * 0.42f, y + s * 0.56f);
            g.strokePath (p, juce::PathStrokeType (sw * 1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            juce::Path tip;
            tip.startNewSubPath (x + s * 0.4f, y + s * 0.5f);
            tip.quadraticTo (x + s * 0.18f, y + s * 0.52f, x + s * 0.16f, y + s * 0.86f);
            tip.quadraticTo (x + s * 0.46f, y + s * 0.84f, x + s * 0.5f, y + s * 0.6f);
            tip.closeSubPath();
            g.fillPath (tip);
            break;
        }
        case Icon::Import:
        {
            p.startNewSubPath (cx, y + s * 0.1f); p.lineTo (cx, y + s * 0.62f);
            p.startNewSubPath (cx - s * 0.2f, y + s * 0.42f); p.lineTo (cx, y + s * 0.62f); p.lineTo (cx + s * 0.2f, y + s * 0.42f);
            p.startNewSubPath (x + s * 0.14f, y + s * 0.66f); p.lineTo (x + s * 0.14f, y + s * 0.88f);
            p.lineTo (x + s * 0.86f, y + s * 0.88f); p.lineTo (x + s * 0.86f, y + s * 0.66f);
            g.strokePath (p, stroke);
            break;
        }
        case Icon::Power:
        {
            const float r = s * 0.4f;
            p.addCentredArc (cx, cy + s * 0.04f, r, r, 0.0f, 0.65f, juce::MathConstants<float>::twoPi - 0.65f, true);
            p.startNewSubPath (cx, y); p.lineTo (cx, cy);
            g.strokePath (p, stroke);
            break;
        }
        case Icon::Close:
            p.startNewSubPath (x + s * 0.25f, y + s * 0.25f); p.lineTo (x + s * 0.75f, y + s * 0.75f);
            p.startNewSubPath (x + s * 0.75f, y + s * 0.25f); p.lineTo (x + s * 0.25f, y + s * 0.75f);
            g.strokePath (p, stroke); break;
        case Icon::Plus:
            p.startNewSubPath (cx, y + s * 0.2f); p.lineTo (cx, y + s * 0.8f);
            p.startNewSubPath (x + s * 0.2f, cy); p.lineTo (x + s * 0.8f, cy);
            g.strokePath (p, stroke); break;
        case Icon::Save:
        {
            const auto r = a.reduced (s * 0.14f);
            p.startNewSubPath (r.getX(), r.getY());
            p.lineTo (r.getRight() - s * 0.16f, r.getY());
            p.lineTo (r.getRight(), r.getY() + s * 0.16f);
            p.lineTo (r.getRight(), r.getBottom());
            p.lineTo (r.getX(), r.getBottom());
            p.closeSubPath();
            g.strokePath (p, stroke);
            g.fillRect (juce::Rectangle<float> (r.getX() + s * 0.16f, r.getY(), s * 0.34f, s * 0.2f));
            break;
        }
        case Icon::Dice:
        {
            const auto r = a.reduced (s * 0.12f);
            g.drawRoundedRectangle (r, s * 0.14f, sw);
            const float d = s * 0.12f;
            for (auto pt : { r.getCentre(), r.getTopLeft() + juce::Point<float> (s * 0.2f, s * 0.2f),
                             r.getBottomRight() - juce::Point<float> (s * 0.2f, s * 0.2f) })
                g.fillEllipse (juce::Rectangle<float> (d, d).withCentre (pt));
            break;
        }
        case Icon::None:
        default:
            break;
    }
}

void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool on = getToggleState();
    if (! frameless || highlighted || down || on)
    {
        auto fill = on ? col (palette::accentDim).withAlpha (0.55f) : col (frameless ? palette::controlHover : palette::control);
        if (highlighted && ! frameless && ! on) fill = col (palette::controlHover);
        if (down) fill = fill.brighter (0.15f);
        g.setColour (fill);
        g.fillRoundedRectangle (r, Theme::radius);
        if (! frameless)
        {
            g.setColour (on ? col (palette::accent).withAlpha (0.7f) : col (palette::panelEdge).brighter (0.1f));
            g.drawRoundedRectangle (r, Theme::radius, 1.0f);
        }
    }

    auto c = on ? col (palette::accent) : iconColour;
    if (! isEnabled()) c = col (palette::textFaint);
    else if (! highlighted && ! on) c = c.withAlpha (0.82f);

    const auto label = getButtonText();
    const float iconSize = juce::jmin (14.0f, r.getHeight() - 8.0f);
    if (label.isEmpty())
    {
        drawIcon (g, icon, juce::Rectangle<float> (iconSize, iconSize).withCentre (r.getCentre()), c);
        return;
    }

    const auto f = font (Theme::fontBody);
    const float tw = textWidth (f, label);
    const float total = (icon != Icon::None ? iconSize + 6.0f : 0.0f) + tw;
    float x0 = r.getCentreX() - total * 0.5f;
    if (icon != Icon::None)
    {
        drawIcon (g, icon, { x0, r.getCentreY() - iconSize * 0.5f, iconSize, iconSize }, c);
        x0 += iconSize + 6.0f;
    }
    g.setColour (c);
    g.setFont (f);
    g.drawText (label, juce::Rectangle<float> (x0, r.getY(), tw + 2.0f, r.getHeight()), juce::Justification::centredLeft, false);
}

//==============================================================================
ValuePopup::ValuePopup()
{
    setInterceptsMouseClicks (false, false);
    setAlwaysOnTop (true);
}

ValuePopup::~ValuePopup()
{
    hide();
}

void ValuePopup::show (juce::Component& anchor, const juce::String& t, juce::Colour a)
{
    auto* host = anchor.findParentComponentOfClass<Page>();
    if (host == nullptr)
        host = dynamic_cast<Page*> (anchor.getParentComponent());
    juce::Component* parent = host != nullptr ? static_cast<juce::Component*> (host) : anchor.getParentComponent();
    if (parent == nullptr)
        return;

    text = t;
    accent = a;
    if (getParentComponent() != parent)
        parent->addAndMakeVisible (this);

    const int w = (int) std::ceil (textWidth (font (Theme::fontBody, true), text)) + 18;
    const int h = 22;
    const auto anchorArea = parent->getLocalArea (&anchor, anchor.getLocalBounds());
    auto r = juce::Rectangle<int> (w, h).withCentre ({ anchorArea.getCentreX(), anchorArea.getY() - h / 2 - 2 });
    r = r.constrainedWithin (parent->getLocalBounds().reduced (2));
    setBounds (r);
    setVisible (true);
    toFront (false);
    repaint();
}

void ValuePopup::hide()
{
    if (auto* p = getParentComponent())
        p->removeChildComponent (this);
}

void ValuePopup::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (col (palette::background).withAlpha (0.96f));
    g.fillRoundedRectangle (r, Theme::radiusSmall);
    g.setColour (accent.withAlpha (0.8f));
    g.drawRoundedRectangle (r, Theme::radiusSmall, 1.0f);
    g.setColour (col (palette::text));
    g.setFont (font (Theme::fontBody, true));
    g.drawText (text, getLocalBounds(), juce::Justification::centred, false);
}

//==============================================================================
namespace layout
{
    void row (std::initializer_list<juce::Component*> comps, juce::Rectangle<int> area, int cellWidth, int gap)
    {
        for (auto* c : comps)
        {
            auto cell = area.removeFromLeft (cellWidth);
            area.removeFromLeft (gap);
            if (c != nullptr)
                c->setBounds (cell);
        }
    }

    void spread (std::initializer_list<juce::Component*> comps, juce::Rectangle<int> area)
    {
        const int n = (int) comps.size();
        if (n == 0) return;
        int i = 0;
        for (auto* c : comps)
        {
            const int x0 = area.getX() + area.getWidth() * i / n;
            const int x1 = area.getX() + area.getWidth() * (i + 1) / n;
            if (c != nullptr)
                c->setBounds (x0, area.getY(), x1 - x0, area.getHeight());
            ++i;
        }
    }

    void grid (std::initializer_list<juce::Component*> comps, juce::Rectangle<int> area, int columns, int rowHeight, int rowGap)
    {
        columns = juce::jmax (1, columns);
        int i = 0;
        for (auto* c : comps)
        {
            const int r = i / columns, k = i % columns;
            const int x0 = area.getX() + area.getWidth() * k / columns;
            const int x1 = area.getX() + area.getWidth() * (k + 1) / columns;
            if (c != nullptr)
                c->setBounds (x0, area.getY() + r * (rowHeight + rowGap), x1 - x0, rowHeight);
            ++i;
        }
    }
}

} // namespace verdant::ui
