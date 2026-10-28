#include "FxSlotStrip.h"

#include "../PluginProcessor.h"

namespace verdant::ui
{

namespace
{
    juce::Colour col (juce::uint32 c) { return juce::Colour (c); }

    constexpr float kGap = 14.0f;
    constexpr const char* kTypeNames[] = { "None", "Distortion", "Chorus", "Phaser", "Flanger", "Delay", "Reverb", "EQ", "Compressor" };
    constexpr int kNumTypes = (int) std::size (kTypeNames);
}

FxSlotStrip::FxSlotStrip (Processor& p) : processor (p)
{
    for (int s = 0; s < kNumFxSlots; ++s)
        params[(size_t) s] = p.apvts.getParameter (info (fxSlotType (s)).id);
    readTypes();
    setTooltip ("Click to edit, drag to reorder, chevron or right-click to change the effect");
}

FxSlotStrip::~FxSlotStrip() = default;

juce::String FxSlotStrip::typeName (int type)
{
    return juce::isPositiveAndBelow (type, kNumTypes) ? juce::String (kTypeNames[type]) : juce::String();
}

bool FxSlotStrip::isDuplicate (int slot) const noexcept
{
    const int t = typeOf (slot);
    if (t == 0) return false;
    for (int s = 0; s < slot; ++s)
        if (types[(size_t) s] == t) return true;
    return false;
}

void FxSlotStrip::readTypes()
{
    for (int s = 0; s < kNumFxSlots; ++s)
        if (auto* prm = params[(size_t) s])
            types[(size_t) s] = juce::jlimit (0, kNumTypes - 1, (int) std::lround (prm->convertFrom0to1 (prm->getValue())));
}

void FxSlotStrip::setSelected (int slot, juce::NotificationType n)
{
    slot = juce::jlimit (0, kNumFxSlots - 1, slot);
    if (slot == selected) return;
    selected = slot;
    repaint();
    if (n != juce::dontSendNotification && onSelect) onSelect (selected);
}

void FxSlotStrip::setSlotType (int slot, int type)
{
    if (auto* prm = params[(size_t) juce::jlimit (0, kNumFxSlots - 1, slot)])
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 ((float) type));
        prm->endChangeGesture();
    }
    readTypes();
    repaint();
    if (onSelect) onSelect (selected);   // the page re-shows the slot's panel
}

void FxSlotStrip::moveSlot (int from, int to)
{
    from = juce::jlimit (0, kNumFxSlots - 1, from);
    to = juce::jlimit (0, kNumFxSlots - 1, to);
    if (from == to) return;

    readTypes();
    std::vector<int> order (types.begin(), types.end());
    const int moved = order[(size_t) from];
    order.erase (order.begin() + from);
    order.insert (order.begin() + to, moved);
    for (int s = 0; s < kNumFxSlots; ++s)
        if (order[(size_t) s] != types[(size_t) s])
            if (auto* prm = params[(size_t) s])
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (prm->convertTo0to1 ((float) order[(size_t) s]));
                prm->endChangeGesture();
            }
    readTypes();
    selected = to;
    repaint();
    if (onSelect) onSelect (selected);
}

void FxSlotStrip::showTypeMenu (int slot)
{
    readTypes();
    juce::PopupMenu m;
    m.addSectionHeader ("Slot " + juce::String (slot + 1));
    m.addItem (1, "None (empty)", true, types[(size_t) slot] == 0);
    m.addSeparator();
    for (int t = 1; t < kNumTypes; ++t)
    {
        juce::String text (kTypeNames[t]);
        for (int s = 0; s < kNumFxSlots; ++s)
            if (s != slot && types[(size_t) s] == t)
            {
                text << "   (slot " << (s + 1) << ")";
                break;
            }
        m.addItem (t + 1, text, true, types[(size_t) slot] == t);
    }
    m.setLookAndFeel (&getLookAndFeel());
    const auto area = tileBounds (slot).toNearestInt();
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (area))
                                               .withMinimumWidth (area.getWidth()).withStandardItemHeight (24),
                     [safe = juce::Component::SafePointer<FxSlotStrip> (this), slot] (int result)
                     {
                         if (safe == nullptr || result <= 0) return;
                         safe->selected = slot;
                         safe->setSlotType (slot, result - 1);
                     });
}

//==============================================================================
juce::Rectangle<float> FxSlotStrip::tileBounds (int slot) const
{
    const auto r = getLocalBounds().toFloat();
    const float w = (r.getWidth() - kGap * (float) (kNumFxSlots - 1)) / (float) kNumFxSlots;
    return { r.getX() + (float) slot * (w + kGap), r.getY(), w, r.getHeight() };
}

juce::Rectangle<float> FxSlotStrip::menuZone (int slot) const
{
    const auto t = tileBounds (slot);
    return { t.getRight() - 26.0f, t.getY(), 26.0f, 26.0f };
}

int FxSlotStrip::tileAt (juce::Point<float> p) const
{
    for (int s = 0; s < kNumFxSlots; ++s)
        if (tileBounds (s).contains (p)) return s;
    return -1;
}

float FxSlotStrip::gapX (int gap) const
{
    if (gap <= 0) return tileBounds (0).getX() - 1.0f;
    if (gap >= kNumFxSlots) return tileBounds (kNumFxSlots - 1).getRight() + 1.0f;
    return tileBounds (gap).getX() - kGap * 0.5f;
}

int FxSlotStrip::gapAt (float x) const
{
    int best = 0;
    float bestD = 1.0e9f;
    for (int g = 0; g <= kNumFxSlots; ++g)
    {
        const float d = std::abs (gapX (g) - x);
        if (d < bestD) { bestD = d; best = g; }
    }
    return best;
}

//==============================================================================
void FxSlotStrip::tick()
{
    const auto old = types;
    readTypes();
    bool changed = old != types;

    // Active slots pulse with the output level (fast attack, ~200 ms release).
    const float peak = juce::jlimit (0.0f, 1.0f, processor.outputPeak.load (std::memory_order_relaxed) * 2.5f);
    for (int s = 0; s < kNumFxSlots; ++s)
    {
        const bool active = types[(size_t) s] != 0 && ! isDuplicate (s);
        const float target = active ? std::sqrt (peak) : 0.0f;
        auto& p = pulse[(size_t) s];
        const float next = target > p ? target : p * 0.82f;
        if (std::abs (next - p) > 0.01f || (next < 0.01f && p >= 0.01f))
            changed = true;
        p = next < 0.01f ? 0.0f : next;
    }
    if (changed)
        repaint();
}

//==============================================================================
void FxSlotStrip::drawTypeGlyph (juce::Graphics& g, int type, juce::Rectangle<float> a, juce::Colour c, float sw)
{
    const juce::PathStrokeType stroke (sw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    juce::Path p;
    const float x0 = a.getX(), w = a.getWidth(), cy = a.getCentreY(), h = a.getHeight() * 0.5f;
    const int n = 48;
    auto wave = [&] (auto fn)
    {
        for (int i = 0; i <= n; ++i)
        {
            const float t = (float) i / (float) n;
            const float y = cy - fn (t) * h;
            if (i == 0) p.startNewSubPath (x0 + t * w, y);
            else        p.lineTo (x0 + t * w, y);
        }
    };
    const float tau = juce::MathConstants<float>::twoPi;
    g.setColour (c);

    switch (type)
    {
        case 1:   // distortion: a driven, clipped sine
            wave ([tau] (float t) { return std::tanh (3.0f * std::sin (tau * t)) / std::tanh (3.0f); });
            g.strokePath (p, stroke);
            break;
        case 2:   // chorus: two detuned sines
        {
            wave ([tau] (float t) { return 0.8f * std::sin (tau * t * 1.5f); });
            g.strokePath (p, stroke);
            p.clear();
            wave ([tau] (float t) { return 0.8f * std::sin (tau * t * 1.5f + 0.9f); });
            g.setColour (c.withAlpha (0.5f));
            g.strokePath (p, stroke);
            break;
        }
        case 3:   // phaser: response with notches
            wave ([tau] (float t) { return 0.8f - 1.5f * std::pow (std::abs (std::cos (tau * t * 1.5f)), 12.0f); });
            g.strokePath (p, stroke);
            break;
        case 4:   // flanger: comb teeth
            wave ([tau] (float t) { return 0.85f - 1.7f * std::pow (0.5f + 0.5f * std::cos (tau * t * 5.0f), 6.0f); });
            g.strokePath (p, stroke);
            break;
        case 5:   // delay: decaying taps
            for (int k = 0; k < 5; ++k)
            {
                const float x = x0 + w * (0.08f + 0.2f * (float) k);
                const float amp = std::pow (0.62f, (float) k);
                g.setColour (c.withAlpha (0.4f + 0.6f * amp));
                g.fillRoundedRectangle (juce::Rectangle<float> (x - 1.2f, cy + h - 2.0f * h * amp, 2.4f, 2.0f * h * amp), 1.2f);
            }
            break;
        case 6:   // reverb: dense decaying tail
        {
            juce::Random rng (7);
            for (int k = 0; k < 22; ++k)
            {
                const float t = (float) k / 21.0f;
                const float amp = std::exp (-3.2f * t) * (0.55f + 0.45f * rng.nextFloat());
                g.setColour (c.withAlpha (0.35f + 0.65f * amp));
                g.fillRect (juce::Rectangle<float> (x0 + t * (w - 1.5f), cy + h - 2.0f * h * amp, 1.5f, 2.0f * h * amp));
            }
            break;
        }
        case 7:   // EQ: a curve with a bump and a shelf
            wave ([] (float t) { return 0.15f + 0.65f * std::exp (-std::pow ((t - 0.4f) / 0.12f, 2.0f)) - 0.45f * juce::jlimit (0.0f, 1.0f, (t - 0.7f) * 4.0f); });
            g.strokePath (p, stroke);
            break;
        case 8:   // compressor: transfer curve with a knee
        {
            const auto r = a.withSizeKeepingCentre (juce::jmin (a.getWidth(), a.getHeight() * 1.6f), a.getHeight());
            p.startNewSubPath (r.getX(), r.getBottom());
            p.lineTo (r.getX() + r.getWidth() * 0.55f, r.getBottom() - r.getHeight() * 0.62f);
            p.lineTo (r.getRight(), r.getBottom() - r.getHeight() * 0.84f);
            g.strokePath (p, stroke);
            g.setColour (c.withAlpha (0.3f));
            g.drawLine (r.getX(), r.getBottom(), r.getRight(), r.getY(), 1.0f);
            break;
        }
        default:  // empty: a plus
            IconButton::drawIcon (g, IconButton::Icon::Plus, a.withSizeKeepingCentre (a.getHeight(), a.getHeight()), c, sw);
            break;
    }
}

void FxSlotStrip::paintTile (juce::Graphics& g, int slot, juce::Rectangle<float> r, bool floating)
{
    const int type = types[(size_t) slot];
    const bool sel = slot == selected;
    const bool dup = isDuplicate (slot);
    const bool hov = slot == hover && ! dragging;
    const float level = pulse[(size_t) slot];
    const auto accent = col (palette::accent);
    r = r.reduced (0.5f);

    if (floating)
    {
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (r.translated (0.0f, 4.0f), Theme::radius + 1.0f);
    }

    if (type == 0)
    {
        // Empty slot: dashed outline, plus and "Empty".
        g.setColour (col (hov ? palette::controlHover : palette::well).withAlpha (hov ? 1.0f : 0.55f));
        g.fillRoundedRectangle (r, Theme::radius);
        juce::Path outline;
        outline.addRoundedRectangle (r, Theme::radius);
        juce::Path dashed;
        const float dashes[] = { 4.0f, 3.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (sel ? accent.withAlpha (0.8f) : col (palette::panelEdge).brighter (0.25f));
        g.fillPath (dashed);
    }
    else
    {
        if (sel)
        {
            for (int i = 0; i < 3; ++i)
            {
                g.setColour (accent.withAlpha (0.10f - 0.03f * (float) i));
                g.drawRoundedRectangle (r.expanded (1.5f + 1.5f * (float) i), Theme::radius + 2.0f, 1.5f);
            }
        }
        g.setGradientFill (juce::ColourGradient (col (hov || sel ? 0xff242a33 : palette::controlHover), 0.0f, r.getY(),
                                                 col (palette::well), 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, Theme::radius);
        if (level > 0.0f)
        {
            g.setColour (accent.withAlpha (0.10f * level));
            g.fillRoundedRectangle (r, Theme::radius);
        }
        g.setColour (sel ? accent : col (palette::panelEdge).brighter (hov ? 0.35f : 0.18f).interpolatedWith (accent, 0.5f * level));
        g.drawRoundedRectangle (r, Theme::radius, sel ? 1.5f : 1.0f);
    }

    // Slot number (top-left) and the activity LED.
    auto top = r.withHeight (24.0f).reduced (9.0f, 0.0f);
    g.setFont (font (10.0f, true).withExtraKerningFactor (0.08f));
    g.setColour (sel ? accent : col (palette::textFaint));
    g.drawText (juce::String (slot + 1), top, juce::Justification::centredLeft, false);
    if (type != 0 && ! dup)
    {
        const auto led = juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ top.getX() + 17.0f, top.getCentreY() });
        if (level > 0.0f)
        {
            g.setColour (accent.withAlpha (0.25f * level));
            g.fillEllipse (led.expanded (3.0f));
        }
        g.setColour (level > 0.0f ? accent.withAlpha (0.4f + 0.6f * level) : col (palette::accentDim));
        g.fillEllipse (led);
    }

    // Menu chevron (top-right).
    const auto mz = menuZone (slot).translated (r.getX() + 0.5f - tileBounds (slot).getX(), r.getY() + 0.5f - tileBounds (slot).getY());
    if (slot == hoverMenu && ! dragging)
    {
        g.setColour (col (palette::controlHover).brighter (0.1f));
        g.fillRoundedRectangle (mz.reduced (4.0f), Theme::radiusSmall);
    }
    IconButton::drawIcon (g, IconButton::Icon::ChevronDown, mz.withSizeKeepingCentre (10.0f, 10.0f),
                          col (slot == hoverMenu ? palette::text : palette::textDim), 1.4f);

    // Glyph + name.
    auto body = r.withTrimmedTop (22.0f).reduced (10.0f, 6.0f);
    const auto nameArea = body.removeFromBottom (16.0f);
    const auto glyph = body.withSizeKeepingCentre (juce::jmin (body.getWidth() - 16.0f, 64.0f), juce::jmin (body.getHeight() - 4.0f, 22.0f));
    const auto glyphColour = type == 0 ? col (palette::textFaint)
                                       : (dup ? col (palette::textDim) : (sel ? accent : col (palette::text).withAlpha (0.85f)));
    drawTypeGlyph (g, type, glyph, glyphColour, 1.5f);

    g.setFont (font (type == 0 ? Theme::fontSmall : Theme::fontBody, type != 0));
    g.setColour (type == 0 ? col (palette::textFaint) : (sel ? col (palette::text) : col (palette::text).withAlpha (0.88f)));
    g.drawFittedText (type == 0 ? juce::String ("Empty") : typeName (type), nameArea.toNearestInt(), juce::Justification::centred, 1, 0.8f);

    if (dup)
    {
        g.setColour (col (palette::warning));
        g.setFont (font (9.5f, true));
        g.drawText ("BYPASSED", top.withTrimmedLeft (10.0f).withTrimmedRight (18.0f), juce::Justification::centredLeft, false);
    }
}

void FxSlotStrip::paint (juce::Graphics& g)
{
    // Signal-flow chevrons in the gaps.
    for (int s = 0; s < kNumFxSlots - 1; ++s)
    {
        const auto t = tileBounds (s);
        const auto gap = juce::Rectangle<float> (t.getRight(), t.getY(), kGap, t.getHeight());
        IconButton::drawIcon (g, IconButton::Icon::ChevronRight, gap.withSizeKeepingCentre (8.0f, 8.0f), col (palette::textFaint), 1.3f);
    }

    for (int s = 0; s < kNumFxSlots; ++s)
    {
        if (dragging && s == pressed)
        {
            // The slot being dragged leaves a faint hole.
            const auto r = tileBounds (s).reduced (0.5f);
            g.setColour (col (palette::well).withAlpha (0.5f));
            g.fillRoundedRectangle (r, Theme::radius);
            continue;
        }
        paintTile (g, s, tileBounds (s), false);
    }

    if (dragging && pressed >= 0)
    {
        if (dropGap >= 0)
        {
            const float x = gapX (dropGap);
            const auto t = tileBounds (0);
            g.setColour (col (palette::accent).withAlpha (0.25f));
            g.fillRoundedRectangle (juce::Rectangle<float> (7.0f, t.getHeight() + 6.0f).withCentre ({ x, t.getCentreY() }), 3.5f);
            g.setColour (col (palette::accent));
            g.fillRoundedRectangle (juce::Rectangle<float> (3.0f, t.getHeight() + 2.0f).withCentre ({ x, t.getCentreY() }), 1.5f);
        }
        auto r = tileBounds (pressed);
        r.setPosition (dragPos - grabOffset);
        r.setY (tileBounds (pressed).getY() - 3.0f);
        paintTile (g, pressed, r, true);
    }
}

//==============================================================================
void FxSlotStrip::mouseMove (const juce::MouseEvent& e)
{
    const int h = tileAt (e.position);
    const int hm = h >= 0 && menuZone (h).contains (e.position) ? h : -1;
    if (h != hover || hm != hoverMenu)
    {
        hover = h;
        hoverMenu = hm;
        repaint();
    }
}

void FxSlotStrip::mouseExit (const juce::MouseEvent&)
{
    if (hover != -1 || hoverMenu != -1)
    {
        hover = hoverMenu = -1;
        repaint();
    }
}

void FxSlotStrip::mouseDown (const juce::MouseEvent& e)
{
    pressed = tileAt (e.position);
    dragging = false;
    if (pressed < 0) return;
    grabOffset = e.position - tileBounds (pressed).getPosition();
    dragPos = e.position;

    if (e.mods.isPopupMenu())
    {
        setSelected (pressed);
        showTypeMenu (pressed);
        pressed = -1;
    }
}

void FxSlotStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (pressed < 0 || e.mods.isPopupMenu()) return;
    if (! dragging && e.getDistanceFromDragStart() < 5) return;
    dragging = true;
    dragPos = e.position;
    dropGap = gapAt (e.position.x);
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    repaint();
}

void FxSlotStrip::mouseUp (const juce::MouseEvent& e)
{
    setMouseCursor (juce::MouseCursor::NormalCursor);
    const int from = pressed;
    pressed = -1;
    if (from < 0) return;

    if (dragging)
    {
        dragging = false;
        const int gap = dropGap;
        dropGap = -1;
        const int to = gap > from ? gap - 1 : gap;
        if (gap >= 0 && to != from)
            moveSlot (from, to);
        else
            repaint();
        return;
    }

    setSelected (from);
    if (types[(size_t) from] == 0 || menuZone (from).contains (e.position))
        showTypeMenu (from);
}

} // namespace verdant::ui
