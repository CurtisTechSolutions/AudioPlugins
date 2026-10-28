#include "ModSourceChip.h"

#include "../PluginProcessor.h"

namespace verdant::ui
{

namespace
{
    juce::Colour col (juce::uint32 c) { return juce::Colour (c); }
    constexpr float chipFont = 10.0f;
    constexpr int clusterLabelGap = 5, clusterPad = 4, chipGap = 1, clusterGap = 6;
}

//==============================================================================
ModSourceChip::ModSourceChip (ModSource s, const juce::String& t)
    : source (s), text (t.isNotEmpty() ? t : modSourceShortName (s))
{
    setTooltip (modSourceDisplayName (s) + " - drag onto a knob to modulate it");
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

juce::String ModSourceChip::dragDescription (ModSource s)
{
    return "modsrc:" + juce::String ((int) s);
}

ModSource ModSourceChip::sourceFromDescription (const juce::var& d)
{
    const auto s = d.toString();
    if (! s.startsWith ("modsrc:"))
        return ModSource::None;
    const int v = s.fromFirstOccurrenceOf (":", false, false).getIntValue();
    if (v <= 0 || v >= (int) ModSource::Count)
        return ModSource::None;
    return (ModSource) v;
}

void ModSourceChip::setActivity (float a)
{
    a = juce::jlimit (0.0f, 1.0f, a);
    if (std::abs (a - activity) > 0.04f)
    {
        activity = a;
        repaint();
    }
}

void ModSourceChip::setEmbedded (bool e) { embedded = e; repaint(); }

int ModSourceChip::preferredWidth() const
{
    return juce::jmax (embedded ? 15 : 18, (int) std::ceil (textWidth (font (chipFont, true), text)) + (embedded ? 7 : 14));
}

void ModSourceChip::paint (juce::Graphics& g)
{
    const auto colour = modSourceColour (source);
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool hover = isMouseOver (true);
    const float radius = embedded ? Theme::radiusSmall : r.getHeight() * 0.5f;

    g.setColour (colour.withAlpha (0.08f + 0.1f * (hover ? 1.0f : 0.0f) + 0.32f * activity));
    g.fillRoundedRectangle (r, radius);
    if (! embedded || hover)
    {
        g.setColour (colour.withAlpha (hover ? 0.9f : 0.45f));
        g.drawRoundedRectangle (r, radius, 1.0f);
    }
    g.setColour (hover || activity > 0.5f ? colour.brighter (0.25f) : colour);
    g.setFont (font (chipFont, true));
    g.drawText (text, getLocalBounds(), juce::Justification::centred, false);
}

void ModSourceChip::mouseDrag (const juce::MouseEvent& e)
{
    if (e.getDistanceFromDragStart() < 4)
        return;
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
    {
        if (container->isDragAndDropActive())
            return;
        const float scale = 2.0f;
        auto image = createComponentSnapshot (getLocalBounds(), true, scale);
        container->startDragging (dragDescription (source), this, juce::ScaledImage (image, scale));
    }
}

//==============================================================================
ModSourceStrip::ModSourceStrip (Processor& p) : processor (p)
{
    auto addCluster = [this] (const juce::String& label, juce::Colour c, std::initializer_list<std::pair<ModSource, const char*>> list)
    {
        Cluster cl;
        cl.label = label;
        cl.colour = c;
        for (const auto& [src, txt] : list)
        {
            chips.push_back (std::make_unique<ModSourceChip> (src, juce::String (txt)));
            chips.back()->setEmbedded (true);
            addAndMakeVisible (*chips.back());
            cl.chips.push_back (chips.back().get());
        }
        clusters.push_back (std::move (cl));
    };

    addCluster ("LFO", col (palette::modLfo), { { ModSource::Lfo1, "1" }, { ModSource::Lfo2, "2" }, { ModSource::Lfo3, "3" }, { ModSource::Lfo4, "4" } });
    addCluster ("ENV", col (palette::modEnv), { { ModSource::Env1, "1" }, { ModSource::Env2, "2" }, { ModSource::Env3, "3" } });
    addCluster ("MACRO", col (palette::modMacro), { { ModSource::Macro1, "1" }, { ModSource::Macro2, "2" }, { ModSource::Macro3, "3" }, { ModSource::Macro4, "4" },
                                                    { ModSource::Macro5, "5" }, { ModSource::Macro6, "6" }, { ModSource::Macro7, "7" }, { ModSource::Macro8, "8" } });
    addCluster ({}, col (palette::modMacro), { { ModSource::Velocity, "VEL" }, { ModSource::Aftertouch, "AT" }, { ModSource::ModWheel, "MW" },
                                               { ModSource::PitchBend, "PB" }, { ModSource::Note, "KEY" }, { ModSource::Random, "RND" }, { ModSource::SeqMod, "SEQ" } });
    addCluster ({}, col (palette::modPhysics), { { ModSource::Ball, "BALL" }, { ModSource::Spring, "SPR" }, { ModSource::Orbit, "ORB" }, { ModSource::Alive, "ALV" } });
}

ModSourceStrip::~ModSourceStrip() = default;

int ModSourceStrip::preferredWidth() const
{
    int w = 0;
    for (const auto& cl : clusters)
    {
        int cw = clusterPad * 2;
        if (cl.label.isNotEmpty())
            cw += (int) std::ceil (textWidth (font (chipFont, true), cl.label)) + clusterLabelGap;
        for (auto* c : cl.chips)
            cw += c->preferredWidth() + chipGap;
        w += cw + clusterGap;
    }
    return w - clusterGap;
}

void ModSourceStrip::resized()
{
    // Labels keep their width; when space is short the chips shrink (proportionally, never below 12 px).
    const int natural = preferredWidth();
    int chipTotal = 0;
    for (const auto& cl : clusters)
        for (auto* c : cl.chips)
            chipTotal += c->preferredWidth();
    const float chipScale = natural > getWidth() && chipTotal > 0
                              ? juce::jmax (0.5f, 1.0f - (float) (natural - getWidth()) / (float) chipTotal) : 1.0f;

    auto area = getLocalBounds();
    for (auto& cl : clusters)
    {
        const int labelW = cl.label.isNotEmpty() ? (int) std::ceil (textWidth (font (chipFont, true), cl.label)) + clusterLabelGap : 0;
        std::vector<int> widths;
        int cw = clusterPad * 2 + labelW;
        for (auto* c : cl.chips)
        {
            widths.push_back (juce::jmax (12, (int) std::floor ((float) c->preferredWidth() * chipScale)));
            cw += widths.back() + chipGap;
        }
        cw -= chipGap;
        cl.bounds = area.removeFromLeft (juce::jmin (cw, area.getWidth()));
        area.removeFromLeft (clusterGap);

        auto inner = cl.bounds.reduced (clusterPad, 3).withTrimmedLeft (labelW);
        for (size_t i = 0; i < cl.chips.size(); ++i)
        {
            cl.chips[i]->setBounds (inner.removeFromLeft (widths[i]));
            inner.removeFromLeft (chipGap);
        }
    }
}

void ModSourceStrip::paint (juce::Graphics& g)
{
    for (const auto& cl : clusters)
    {
        const auto r = cl.bounds.toFloat().reduced (0.5f);
        g.setColour (col (palette::well));
        g.fillRoundedRectangle (r, Theme::radius);
        g.setColour (col (palette::panelEdge));
        g.drawRoundedRectangle (r, Theme::radius, 1.0f);
        if (cl.label.isNotEmpty())
        {
            g.setColour (cl.colour.withAlpha (0.85f));
            g.setFont (font (chipFont, true).withExtraKerningFactor (0.06f));
            g.drawText (cl.label, cl.bounds.withTrimmedLeft (clusterPad + 3), juce::Justification::centredLeft, false);
        }
    }
}

void ModSourceStrip::tick()
{
    const auto& v = processor.engine().visuals();
    for (auto& c : chips)
    {
        const auto s = c->getSource();
        float a = 0.0f;
        if (s >= ModSource::Lfo1 && s <= ModSource::Lfo4)
            a = std::abs (v.lfoValue[(int) s - (int) ModSource::Lfo1].load (std::memory_order_relaxed));
        else if (s >= ModSource::Env1 && s <= ModSource::Env3)
            a = v.envValue[(int) s - (int) ModSource::Env1].load (std::memory_order_relaxed);
        else if (s >= ModSource::Macro1 && s <= ModSource::Macro8)
        {
            if (auto* raw = processor.apvts.getRawParameterValue (info (macroParam ((int) s - (int) ModSource::Macro1)).id))
                a = raw->load (std::memory_order_relaxed) * 0.01f;
        }
        else if (s == ModSource::Ball)
            a = v.physics[0].load (std::memory_order_relaxed);
        else if (s == ModSource::Spring)
            a = std::abs (v.physics[1].load (std::memory_order_relaxed));
        else if (s == ModSource::Orbit)
            a = std::abs (v.physics[2].load (std::memory_order_relaxed));
        c->setActivity (a * 0.8f);
    }
}

} // namespace verdant::ui
