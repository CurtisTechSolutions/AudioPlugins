#include "SoundMapView.h"

#include "PresetList.h"

#include "../PluginProcessor.h"
#include "../PresetManager.h"

#include <algorithm>

namespace verdant::ui
{

namespace
{
    juce::Colour col (juce::uint32 c) { return juce::Colour (c); }

    constexpr int kAuditionNote = 48;            // C3 (PRD), the note the map was rendered with
    constexpr float kAuditionSeconds = 0.4f;
    constexpr juce::int64 kHoverDelayMs = 300, kBlendIntervalMs = 50;
    constexpr float kLegendH = 18.0f;
}

SoundMapView::SoundMapView (Processor& p) : processor (p)
{
    setWantsKeyboardFocus (true);
}

SoundMapView::~SoundMapView() = default;

void SoundMapView::setPoints (std::vector<SoundMapPoint> pts)
{
    closeBlend();
    points = std::move (pts);

    // Display spread: the PCA layout bunches most presets in the middle with a few outliers, which
    // makes the crowd hard to point at. Blend each axis with its rank (order kept, so x still rises
    // with brightness and y with attack).
    const size_t n = points.size();
    if (n > 2)
    {
        for (int axis = 0; axis < 2; ++axis)
        {
            std::vector<size_t> order (n);
            for (size_t i = 0; i < n; ++i) order[i] = i;
            auto value = [this, axis] (size_t i) { return axis == 0 ? points[i].x : points[i].y; };
            std::stable_sort (order.begin(), order.end(), [&value] (size_t a, size_t b) { return value (a) < value (b); });
            std::vector<float> spread (n);
            for (size_t r = 0; r < n; ++r)
                spread[order[r]] = 0.04f + 0.92f * (float) r / (float) (n - 1);
            for (size_t i = 0; i < n; ++i)
            {
                auto& v = axis == 0 ? points[i].x : points[i].y;
                v = 0.3f * v + 0.7f * spread[i];
            }
        }
    }
    hover = -1;
    updateLayout();
    repaint();
}

void SoundMapView::setBuildProgress (float p)
{
    if (std::abs (p - buildProgress) > 0.004f || (p < 0.0f) != (buildProgress < 0.0f))
    {
        buildProgress = p;
        repaint();
    }
}

void SoundMapView::setAuditionEnabled (bool e)
{
    auditionEnabled = e;
    if (! e) endPreview (true);
}

void SoundMapView::setHighlightedEntry (int entry)
{
    if (entry != highlighted) { highlighted = entry; repaint(); }
}

//==============================================================================
juce::StringArray SoundMapView::legendCategories() const
{
    juce::StringArray cats;
    const auto& entries = processor.presets().entries();
    for (const auto& p : points)
        if (juce::isPositiveAndBelow (p.presetIndex, (int) entries.size()))
            cats.addIfNotAlreadyThere (entries[(size_t) p.presetIndex].category);
    if (cats.isEmpty())
        cats = processor.presets().categories();
    return cats;
}

std::vector<std::pair<juce::String, juce::Rectangle<float>>> SoundMapView::legendLayout() const
{
    // Centred rows of "dot name" items, wrapping as needed, bottom-aligned.
    const auto f = font (10.5f);
    const auto area = getLocalBounds().toFloat().reduced (14.0f, 10.0f);
    std::vector<std::vector<std::pair<juce::String, float>>> rows (1);
    float x = 0.0f;
    for (const auto& c : legendCategories())
    {
        const float w = textWidth (f, c) + 15.0f;
        if (x > 0.0f && x + w > area.getWidth()) { rows.emplace_back(); x = 0.0f; }
        rows.back().emplace_back (c, w);
        x += w + 10.0f;
    }
    std::vector<std::pair<juce::String, juce::Rectangle<float>>> out;
    float y = area.getBottom() - (float) rows.size() * kLegendH;
    for (const auto& row : rows)
    {
        float total = -10.0f;
        for (const auto& [c, w] : row) total += w + 10.0f;
        float rx = area.getCentreX() - total * 0.5f;
        for (const auto& [c, w] : row)
        {
            out.push_back ({ c, { rx, y, w, kLegendH } });
            rx += w + 10.0f;
        }
        y += kLegendH;
    }
    return out;
}

void SoundMapView::updateLayout()
{
    legend = legendLayout();
    const float top = legend.empty() ? (float) getHeight() - 10.0f : legend.front().second.getY();
    auto a = getLocalBounds().toFloat().reduced (18.0f, 16.0f);
    map = a.withBottom (juce::jmin (a.getBottom(), top - 6.0f));
}

void SoundMapView::resized()
{
    updateLayout();
}

juce::Rectangle<float> SoundMapView::mapArea() const
{
    return map;
}

juce::Point<float> SoundMapView::position (const SoundMapPoint& p) const
{
    const auto a = mapArea();
    return { a.getX() + a.getWidth() * juce::jlimit (0.0f, 1.0f, p.x), a.getBottom() - a.getHeight() * juce::jlimit (0.0f, 1.0f, p.y) };
}

int SoundMapView::pointAt (juce::Point<float> pos, float radius) const
{
    int best = -1;
    float bestD = radius;
    for (size_t i = 0; i < points.size(); ++i)
    {
        const float d = position (points[i]).getDistanceFrom (pos);
        if (d < bestD) { bestD = d; best = (int) i; }
    }
    return best;
}

juce::String SoundMapView::nameOf (int i) const
{
    const auto& entries = processor.presets().entries();
    const int e = juce::isPositiveAndBelow (i, (int) points.size()) ? points[(size_t) i].presetIndex : -1;
    return juce::isPositiveAndBelow (e, (int) entries.size()) ? entries[(size_t) e].name : juce::String();
}

juce::Colour SoundMapView::colourOf (int i) const
{
    const auto& entries = processor.presets().entries();
    const int e = juce::isPositiveAndBelow (i, (int) points.size()) ? points[(size_t) i].presetIndex : -1;
    return juce::isPositiveAndBelow (e, (int) entries.size()) ? categoryColour (entries[(size_t) e].category) : col (palette::textDim);
}

//==============================================================================
void SoundMapView::endPreview (bool restore)
{
    if (! previewing) return;
    previewing = false;
    if (! restore) return;
    if (restoreIndex >= 0 && restoreIndex < (int) processor.presets().entries().size())
        processor.presets().load (restoreIndex);
    else
        processor.applyPatch (restorePatch, false);
}

void SoundMapView::tick()
{
    // Hover audition after the dwell time.
    if (hover >= 0 && ! hoverAuditioned && auditionEnabled && pressPoint < 0 && ! draggingHandle
        && juce::Time::currentTimeMillis() - hoverSince >= kHoverDelayMs)
    {
        hoverAuditioned = true;
        const int entry = points[(size_t) hover].presetIndex;
        if (entry >= 0 && entry < (int) processor.presets().entries().size())
        {
            if (! previewing)
            {
                previewing = true;
                restoreIndex = processor.presets().currentIndex();
                restorePatch = processor.currentPatch();
            }
            processor.presets().load (entry);
            processor.auditionNote (kAuditionNote, kAuditionSeconds);
            repaint();
        }
    }

    if (blendDirty && juce::Time::currentTimeMillis() - lastBlendApply >= kBlendIntervalMs)
        applyBlend (false);
}

//==============================================================================
void SoundMapView::startBlend (int pa, int pb)
{
    endPreview (false);
    auto& pm = processor.presets();
    if (! pm.readPatch (points[(size_t) pa].presetIndex, patchA) || ! pm.readPatch (points[(size_t) pb].presetIndex, patchB))
        return;
    blendA = pa;
    blendB = pb;
    blendT = 0.5f;
    applyBlend (true);
}

void SoundMapView::applyBlend (bool final)
{
    if (blendA < 0 || blendB < 0) return;
    processor.applyPatch (blendPatches (patchA, patchB, blendT), ! final);
    lastBlendApply = juce::Time::currentTimeMillis();
    blendDirty = false;
}

void SoundMapView::closeBlend()
{
    blendA = blendB = -1;
    blendDirty = draggingHandle = false;
    repaint();
}

float SoundMapView::tOnLine (juce::Point<float> p) const
{
    const auto a = position (points[(size_t) blendA]), b = position (points[(size_t) blendB]);
    const auto ab = b - a;
    const float len2 = juce::jmax (1.0f, ab.x * ab.x + ab.y * ab.y);
    return juce::jlimit (0.0f, 1.0f, ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / len2);
}

//==============================================================================
void SoundMapView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    paintDisplay (g, bounds, col (palette::accent));
    const auto a = mapArea();

    // Grid + axis captions.
    for (int k = 1; k < 4; ++k)
    {
        g.setColour (col (palette::grid));
        g.fillRect (juce::Rectangle<float> (std::round (a.getX() + a.getWidth() * (float) k / 4.0f), a.getY(), 1.0f, a.getHeight()));
        g.fillRect (juce::Rectangle<float> (a.getX(), std::round (a.getY() + a.getHeight() * (float) k / 4.0f), a.getWidth(), 1.0f));
    }
    g.setFont (font (9.5f, true).withExtraKerningFactor (0.08f));
    g.setColour (col (palette::textFaint));
    g.drawText ("DARK", juce::Rectangle<float> (a.getX(), a.getBottom() - 12.0f, 60.0f, 11.0f), juce::Justification::centredLeft, false);
    g.drawText ("BRIGHT", juce::Rectangle<float> (a.getRight() - 60.0f, a.getBottom() - 12.0f, 60.0f, 11.0f), juce::Justification::centredRight, false);
    g.drawText ("SLOW ATTACK", juce::Rectangle<float> (a.getX(), a.getY(), 100.0f, 11.0f), juce::Justification::centredLeft, false);

    // Legend: the categories present.
    g.setFont (font (10.5f));
    for (const auto& [c, r] : legend)
    {
        g.setColour (categoryColour (c));
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ r.getX() + 4.0f, r.getCentreY() }));
        g.setColour (col (palette::textDim));
        g.drawText (c, r.withTrimmedLeft (11.0f), juce::Justification::centredLeft, false);
    }

    const int current = processor.presets().currentIndex();

    // Blend line + handle.
    if (blendA >= 0 && blendB >= 0)
    {
        const auto pa = position (points[(size_t) blendA]), pb = position (points[(size_t) blendB]);
        g.setColour (col (palette::text).withAlpha (0.12f));
        g.drawLine ({ pa, pb }, 6.0f);
        juce::ColourGradient grad (colourOf (blendA), pa.x, pa.y, colourOf (blendB), pb.x, pb.y, false);
        g.setGradientFill (grad);
        g.drawLine ({ pa, pb }, 2.0f);
    }
    else if (draggingLine && pressPoint >= 0)
    {
        const auto pa = position (points[(size_t) pressPoint]);
        const int target = pointAt (dragPos, 12.0f);
        const auto pb = target >= 0 && target != pressPoint ? position (points[(size_t) target]) : dragPos;
        juce::Path line;
        line.startNewSubPath (pa);
        line.lineTo (pb);
        juce::Path dashed;
        const float dashes[] = { 5.0f, 4.0f };
        juce::PathStrokeType (1.6f).createDashedStroke (dashed, line, dashes, 2);
        g.setColour (col (palette::text).withAlpha (0.8f));
        g.fillPath (dashed);
    }

    // Points.
    for (size_t i = 0; i < points.size(); ++i)
    {
        const auto pos = position (points[i]);
        const auto c = colourOf ((int) i);
        const bool isHover = (int) i == hover, isCurrent = points[i].presetIndex == current;
        const bool isBlend = (int) i == blendA || (int) i == blendB;
        const bool isHigh = points[i].presetIndex == highlighted;
        const float r = isHover || isBlend ? 6.5f : 4.5f;
        g.setColour (c.withAlpha (isHover ? 0.35f : 0.18f));
        g.fillEllipse (juce::Rectangle<float> (r * 3.2f, r * 3.2f).withCentre (pos));
        g.setColour (c);
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (pos));
        if (isCurrent || isHigh)
        {
            g.setColour (isCurrent ? col (palette::text) : c.brighter (0.4f));
            g.drawEllipse (juce::Rectangle<float> (r * 2.0f + 7.0f, r * 2.0f + 7.0f).withCentre (pos), 1.5f);
        }
    }

    if (blendA >= 0 && blendB >= 0)
    {
        const auto pa = position (points[(size_t) blendA]), pb = position (points[(size_t) blendB]);
        const auto h = pa + (pb - pa) * blendT;
        g.setColour (col (palette::accent).withAlpha (0.3f));
        g.fillEllipse (juce::Rectangle<float> (22.0f, 22.0f).withCentre (h));
        g.setColour (col (palette::display));
        g.fillEllipse (juce::Rectangle<float> (13.0f, 13.0f).withCentre (h));
        g.setColour (col (palette::accent));
        g.drawEllipse (juce::Rectangle<float> (13.0f, 13.0f).withCentre (h), 2.5f);

        const auto text = nameOf (blendA) + "  " + juce::String (juce::roundToInt ((1.0f - blendT) * 100.0f)) + " / "
                          + juce::String (juce::roundToInt (blendT * 100.0f)) + "  " + nameOf (blendB);
        const auto f = font (Theme::fontSmall, true);
        const float w = juce::jmin (a.getWidth(), textWidth (f, text) + 20.0f);
        const auto pill = juce::Rectangle<float> (w, 22.0f).withCentre ({ a.getCentreX(), a.getY() + 12.0f });
        g.setColour (col (palette::background).withAlpha (0.9f));
        g.fillRoundedRectangle (pill, 11.0f);
        g.setColour (col (palette::accent).withAlpha (0.6f));
        g.drawRoundedRectangle (pill.reduced (0.5f), 11.0f, 1.0f);
        g.setColour (col (palette::text));
        g.setFont (f);
        g.drawFittedText (text, pill.toNearestInt().reduced (8, 0), juce::Justification::centred, 1, 0.75f);
    }

    // Hover label.
    if (hover >= 0)
    {
        const auto pos = position (points[(size_t) hover]);
        const auto name = nameOf (hover);
        const auto f = font (Theme::fontBody, true);
        const float w = textWidth (f, name) + 20.0f + (previewing && hoverAuditioned ? 16.0f : 0.0f);
        auto pill = juce::Rectangle<float> (w, 22.0f).withCentre ({ pos.x, pos.y - 20.0f });
        pill = pill.constrainedWithin (bounds.reduced (4.0f));
        g.setColour (col (palette::background).withAlpha (0.92f));
        g.fillRoundedRectangle (pill, 11.0f);
        g.setColour (colourOf (hover).withAlpha (0.8f));
        g.drawRoundedRectangle (pill.reduced (0.5f), 11.0f, 1.0f);
        auto textArea = pill.reduced (10.0f, 0.0f);
        if (previewing && hoverAuditioned)
        {
            // A small speaker-ish wedge: this preset is sounding.
            const auto icon = textArea.removeFromLeft (12.0f).withSizeKeepingCentre (10.0f, 10.0f);
            juce::Path sp;
            sp.addTriangle (icon.getX(), icon.getCentreY(), icon.getRight(), icon.getY(), icon.getRight(), icon.getBottom());
            g.setColour (col (palette::accent));
            g.fillPath (sp);
            textArea.removeFromLeft (4.0f);
        }
        g.setColour (col (palette::text));
        g.setFont (f);
        g.drawText (name, textArea, juce::Justification::centredLeft, false);
    }

    // Empty / building overlays.
    if (buildProgress >= 0.0f || points.empty())
    {
        g.setColour (col (palette::display).withAlpha (points.empty() ? 0.0f : 0.7f));
        g.fillRoundedRectangle (bounds.reduced (1.0f), Theme::radius);
        const auto box = juce::Rectangle<float> (juce::jmin (260.0f, bounds.getWidth() - 40.0f), 58.0f).withCentre (bounds.getCentre());
        g.setColour (col (palette::text));
        g.setFont (font (13.0f, true));
        const int n = (int) processor.presets().entries().size();
        g.drawText (buildProgress >= 0.0f ? "Mapping " + juce::String (n) + " presets..." : juce::String ("No sound map yet"),
                    box.withHeight (20.0f), juce::Justification::centred, false);
        if (buildProgress >= 0.0f)
        {
            const auto bar = juce::Rectangle<float> (box.getWidth(), 6.0f).withCentre ({ box.getCentreX(), box.getCentreY() + 4.0f });
            g.setColour (col (palette::knobTrack));
            g.fillRoundedRectangle (bar, 3.0f);
            g.setColour (col (palette::accent));
            g.fillRoundedRectangle (bar.withWidth (juce::jmax (6.0f, bar.getWidth() * juce::jlimit (0.0f, 1.0f, buildProgress))), 3.0f);
            g.setColour (col (palette::textDim));
            g.setFont (font (Theme::fontSmall));
            g.drawText (juce::String (juce::roundToInt (buildProgress * 100.0f)) + " %  -  each preset plays one note offline",
                        box.withTrimmedTop (36.0f), juce::Justification::centred, false);
        }
        else
        {
            g.setColour (col (palette::textDim));
            g.setFont (font (Theme::fontSmall));
            g.drawText ("Press Build map to lay the presets out by sound", box.withTrimmedTop (26.0f).withHeight (16.0f), juce::Justification::centred, false);
        }
    }
}

//==============================================================================
void SoundMapView::mouseMove (const juce::MouseEvent& e)
{
    const int h = buildProgress >= 0.0f ? -1 : pointAt (e.position);
    if (h != hover)
    {
        hover = h;
        hoverSince = juce::Time::currentTimeMillis();
        hoverAuditioned = false;
        repaint();
    }
    bool onHandle = false;
    if (blendA >= 0 && blendB >= 0)
    {
        const auto pa = position (points[(size_t) blendA]), pb = position (points[(size_t) blendB]);
        onHandle = (pa + (pb - pa) * blendT).getDistanceFrom (e.position) < 12.0f;
    }
    setMouseCursor (onHandle ? juce::MouseCursor::DraggingHandCursor
                             : (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor));
}

void SoundMapView::mouseExit (const juce::MouseEvent&)
{
    if (pressPoint >= 0 || draggingHandle) return;
    hover = -1;
    endPreview (true);
    repaint();
}

void SoundMapView::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (buildProgress >= 0.0f) return;
    dragPos = e.position;
    if (blendA >= 0 && blendB >= 0)
    {
        const auto pa = position (points[(size_t) blendA]), pb = position (points[(size_t) blendB]);
        if ((pa + (pb - pa) * blendT).getDistanceFrom (e.position) < 12.0f)
        {
            draggingHandle = true;
            return;
        }
    }
    pressPoint = pointAt (e.position);
    if (pressPoint < 0 && blendA >= 0)
        closeBlend();
}

void SoundMapView::mouseDrag (const juce::MouseEvent& e)
{
    dragPos = e.position;
    if (draggingHandle)
    {
        blendT = tOnLine (e.position);
        blendDirty = true;
        repaint();
        return;
    }
    if (pressPoint >= 0 && e.getDistanceFromDragStart() > 4)
    {
        draggingLine = true;
        hover = pointAt (e.position, 12.0f);
        hoverAuditioned = true;   // no previews while drawing a line
        repaint();
    }
}

void SoundMapView::mouseUp (const juce::MouseEvent& e)
{
    if (draggingHandle)
    {
        draggingHandle = false;
        applyBlend (true);
        repaint();
        return;
    }
    const int from = pressPoint;
    pressPoint = -1;
    if (from < 0) return;

    if (draggingLine)
    {
        draggingLine = false;
        const int to = pointAt (e.position, 12.0f);
        if (to >= 0 && to != from)
            startBlend (from, to);
        repaint();
        return;
    }

    // A click: keep this preset.
    closeBlend();
    previewing = false;
    const int entry = points[(size_t) from].presetIndex;
    if (entry >= 0 && entry < (int) processor.presets().entries().size())
    {
        if (processor.presets().currentIndex() != entry)
            processor.presets().load (entry);
        if (onLoaded) onLoaded (entry);
    }
    repaint();
}

bool SoundMapView::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey && blendA >= 0)
    {
        closeBlend();
        return true;
    }
    return false;
}

} // namespace verdant::ui
