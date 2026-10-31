#include "LfoEditor.h"
#include "EnvelopeEditor.h"

#include "../PluginProcessor.h"

namespace verdant::ui
{

namespace
{
    juce::Colour col (juce::uint32 c) { return juce::Colour (c); }

    float raw (Processor& p, P param)
    {
        if (auto* v = p.apvts.getRawParameterValue (info (param).id))
            return v->load (std::memory_order_relaxed);
        return 0.0f;
    }
}

float LfoEditor::shapeValue (LfoWave w, const LfoShape& s, float x) noexcept
{
    x = x - std::floor (x);
    switch (w)
    {
        case LfoWave::Sine:     return std::sin (juce::MathConstants<float>::twoPi * x);
        case LfoWave::Triangle: return x < 0.25f ? 4.0f * x : (x < 0.75f ? 2.0f - 4.0f * x : 4.0f * x - 4.0f);
        case LfoWave::SawUp:    return 2.0f * x - 1.0f;
        case LfoWave::SawDown:  return 1.0f - 2.0f * x;
        case LfoWave::Square:   return x < 0.5f ? 1.0f : -1.0f;
        case LfoWave::SampleHold:
        {
            static constexpr float steps[8] = { 0.35f, -0.7f, 0.85f, 0.1f, -0.4f, 0.6f, -0.9f, -0.15f };
            return steps[juce::jlimit (0, 7, (int) (x * 8.0f))];
        }
        case LfoWave::Custom:
        {
            const auto& pts = s.points;
            if (pts.size() < 2) return 0.0f;
            for (size_t i = 1; i < pts.size(); ++i)
            {
                if (x <= pts[i].x || i + 1 == pts.size())
                {
                    const auto& a = pts[i - 1];
                    const auto& b = pts[i];
                    const float span = b.x - a.x;
                    const float t = span > 1.0e-6f ? juce::jlimit (0.0f, 1.0f, (x - a.x) / span) : 1.0f;
                    return 2.0f * (a.y + (b.y - a.y) * EnvelopeEditor::shape (t, a.curve)) - 1.0f;
                }
            }
            return 0.0f;
        }
        case LfoWave::Count:
        default: return 0.0f;
    }
}

//==============================================================================
LfoEditor::LfoEditor (Processor& p, int l, juce::Colour c) : processor (p), lfo (l), colour (c)
{
    p.patchChanged.addChangeListener (this);
    reloadShape();
}

LfoEditor::~LfoEditor()
{
    processor.patchChanged.removeChangeListener (this);
}

void LfoEditor::setLfo (int l)
{
    lfo = juce::jlimit (0, 3, l);
    dotVisible = false;
    reloadShape();
}

void LfoEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (dragPoint < 0 && bendSegment < 0)
        reloadShape();
}

void LfoEditor::reloadShape()
{
    custom = processor.currentPatch().lfoShapes[(size_t) lfo];
    if (custom.points.size() < 2)
        custom = LfoShape();
    rebuildTable();
    repaint();
}

LfoWave LfoEditor::wave() const
{
    return (LfoWave) juce::jlimit (0, (int) LfoWave::Count - 1, (int) std::lround (raw (processor, lfoParam (lfo, LfoParam::Shape))));
}

void LfoEditor::rebuildTable()
{
    const auto w = wave();
    tableWave = (int) w;
    for (int i = 0; i < tableSize; ++i)
        table[(size_t) i] = shapeValue (w, custom, (float) i / (float) tableSize);
}

juce::Rectangle<float> LfoEditor::plotArea() const
{
    return getLocalBounds().toFloat().reduced (14.0f, 12.0f);
}

juce::Point<float> LfoEditor::pointPos (int i) const
{
    const auto a = plotArea();
    const auto& p = custom.points[(size_t) i];
    return { a.getX() + p.x * a.getWidth(), a.getBottom() - p.y * a.getHeight() };
}

int LfoEditor::pointAt (juce::Point<float> pos) const
{
    if (! isCustom()) return -1;
    int best = -1;
    float bestD = 9.0f;
    for (int i = 0; i < (int) custom.points.size(); ++i)
    {
        const float d = pos.getDistanceFrom (pointPos (i));
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

int LfoEditor::segmentAt (float x) const
{
    const auto a = plotArea();
    const float px = juce::jlimit (0.0f, 1.0f, (x - a.getX()) / a.getWidth());
    for (int i = 1; i < (int) custom.points.size(); ++i)
        if (px <= custom.points[(size_t) i].x)
            return i - 1;
    return (int) custom.points.size() - 2;
}

void LfoEditor::commit()
{
    if (changed)
        processor.setLfoShape (lfo, custom);
    changed = false;
}

//==============================================================================
void LfoEditor::tick()
{
    bool dirty = false;
    if ((int) wave() != tableWave)
    {
        rebuildTable();
        dirty = true;
    }

    const bool uni = raw (processor, lfoParam (lfo, LfoParam::Polarity)) > 0.5f;
    if (uni != unipolar) { unipolar = uni; dirty = true; }

    const auto& vis = processor.engine().visuals();
    const bool active = vis.activeVoices.load (std::memory_order_relaxed) > 0;
    if (active)
    {
        float v = vis.lfoValue[lfo].load (std::memory_order_relaxed);
        if (unipolar) v = v * 2.0f - 1.0f;
        // Search forward (up to half a cycle) for the phase whose shape value matches: continuous motion.
        const int start = (int) (dotPhase * tableSize);
        int best = start;
        float bestErr = 1.0e9f;
        for (int k = 0; k < tableSize / 2; ++k)
        {
            const int i = (start + k) % tableSize;
            const float err = std::abs (table[(size_t) i] - v) + (float) k * 0.0008f;
            if (err < bestErr) { bestErr = err; best = i; }
        }
        const float ph = (float) best / (float) tableSize;
        if (! dotVisible || std::abs (ph - dotPhase) > 0.001f || std::abs (v - shownValue) > 0.002f)
        {
            dotPhase = ph;
            shownValue = v;
            dirty = true;
        }
        dotVisible = true;
    }
    else if (dotVisible)
    {
        dotVisible = false;
        dirty = true;
    }

    if (dirty)
        repaint();
}

void LfoEditor::paint (juce::Graphics& g)
{
    paintDisplay (g, getLocalBounds().toFloat(), colour);
    const auto a = plotArea();

    g.setColour (col (palette::grid));
    for (int i = 1; i < 4; ++i)
        g.fillRect (juce::Rectangle<float> (std::round (a.getX() + a.getWidth() * (float) i / 4.0f), a.getY(), 1.0f, a.getHeight()));
    const float zeroY = unipolar ? a.getBottom() : a.getCentreY();
    g.setColour (col (palette::grid).brighter (0.15f));
    g.fillRect (juce::Rectangle<float> (a.getX(), std::round (a.getCentreY()), a.getWidth(), 1.0f));

    juce::Path line;
    const int n = 200;
    for (int i = 0; i <= n; ++i)
    {
        const float x = (float) i / (float) n;
        const float v = shapeValue (wave(), custom, juce::jmin (x, 0.99999f));
        const auto p = juce::Point<float> (a.getX() + x * a.getWidth(), a.getCentreY() - v * a.getHeight() * 0.5f);
        if (i == 0) line.startNewSubPath (p);
        else        line.lineTo (p);
    }

    juce::Path fill (line);
    fill.lineTo (a.getRight(), zeroY);
    fill.lineTo (a.getX(), zeroY);
    fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.22f), 0.0f, a.getY(), colour.withAlpha (0.03f), 0.0f, a.getBottom(), false));
    g.fillPath (fill);
    g.setColour (colour.withAlpha (0.18f));
    g.strokePath (line, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (colour);
    g.strokePath (line, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (isCustom())
    {
        for (int i = 0; i < (int) custom.points.size(); ++i)
        {
            const auto p = pointPos (i);
            const bool active = i == hoverPoint || i == dragPoint;
            if (active)
            {
                g.setColour (colour.withAlpha (0.3f));
                g.fillEllipse (juce::Rectangle<float> (16.0f, 16.0f).withCentre (p));
            }
            g.setColour (col (palette::display));
            g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (p));
            g.setColour (colour);
            g.drawEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (p), 1.6f);
        }
        if (isMouseOver() && dragPoint < 0 && bendSegment < 0)
        {
            g.setColour (col (palette::textFaint));
            g.setFont (font (10.0f));
            g.drawText ("click: add   drag: move / bend   double-click: remove", getLocalBounds().reduced (12, 4),
                        juce::Justification::bottomRight, false);
        }
    }

    if (dotVisible)
    {
        const juce::Point<float> p (a.getX() + dotPhase * a.getWidth(), a.getCentreY() - shownValue * a.getHeight() * 0.5f);
        g.setColour (colour.withAlpha (0.25f));
        g.fillRect (juce::Rectangle<float> (p.x - 0.5f, a.getY(), 1.0f, a.getHeight()));
        g.setColour (col (palette::text).withAlpha (0.3f));
        g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (p));
        g.setColour (col (palette::text));
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (p));
    }
}

//==============================================================================
void LfoEditor::mouseMove (const juce::MouseEvent& e)
{
    const int h = pointAt (e.position);
    if (h != hoverPoint)
        hoverPoint = h;
    setMouseCursor (! isCustom() ? juce::MouseCursor::NormalCursor
                                 : (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::CrosshairCursor));
    repaint();
}

void LfoEditor::mouseExit (const juce::MouseEvent&)
{
    hoverPoint = -1;
    repaint();
}

void LfoEditor::mouseDown (const juce::MouseEvent& e)
{
    if (! isCustom()) return;
    downPos = e.position;
    dragPoint = pointAt (e.position);
    if (dragPoint < 0)
    {
        bendSegment = segmentAt (e.position.x);
        bendStart = custom.points[(size_t) bendSegment].curve;
        pendingAdd = true;
    }
}

void LfoEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (! isCustom()) return;
    const auto a = plotArea();
    if (dragPoint >= 0)
    {
        auto& pts = custom.points;
        auto& p = pts[(size_t) dragPoint];
        const bool endPoint = dragPoint == 0 || dragPoint == (int) pts.size() - 1;
        if (! endPoint)
        {
            const float lo = pts[(size_t) dragPoint - 1].x + 0.002f, hi = pts[(size_t) dragPoint + 1].x - 0.002f;
            p.x = juce::jlimit (lo, juce::jmax (lo, hi), (e.position.x - a.getX()) / a.getWidth());
        }
        p.y = juce::jlimit (0.0f, 1.0f, (a.getBottom() - e.position.y) / a.getHeight());
        // Keep a periodic shape seamless: the two end points share their level.
        if (dragPoint == 0) pts.back().y = p.y;
        if (dragPoint == (int) pts.size() - 1) pts.front().y = p.y;
        changed = true;
    }
    else if (bendSegment >= 0 && (pendingAdd ? e.position.getDistanceFrom (downPos) > 3.0f : true))
    {
        pendingAdd = false;
        auto& pa = custom.points[(size_t) bendSegment];
        const auto& pb = custom.points[(size_t) bendSegment + 1];
        const float dir = pb.y >= pa.y ? 1.0f : -1.0f;
        pa.curve = juce::jlimit (-1.0f, 1.0f, bendStart - dir * (float) e.getDistanceFromDragStartY() / 80.0f);
        changed = true;
    }
    rebuildTable();
    repaint();
}

void LfoEditor::mouseUp (const juce::MouseEvent& e)
{
    if (! isCustom()) return;
    if (pendingAdd)
    {
        const auto a = plotArea();
        const float x = juce::jlimit (0.001f, 0.999f, (e.position.x - a.getX()) / a.getWidth());
        const float y = juce::jlimit (0.0f, 1.0f, (a.getBottom() - e.position.y) / a.getHeight());
        auto& pts = custom.points;
        auto it = std::upper_bound (pts.begin(), pts.end(), x, [] (float v, const LfoShapePoint& p) { return v < p.x; });
        pts.insert (it, LfoShapePoint { x, y, 0.0f });
        changed = true;
    }
    pendingAdd = false;
    dragPoint = bendSegment = -1;
    rebuildTable();
    commit();
    repaint();
}

void LfoEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! isCustom()) return;
    const int i = pointAt (e.position);
    if (i > 0 && i < (int) custom.points.size() - 1)
    {
        custom.points.erase (custom.points.begin() + i);
        changed = true;
        rebuildTable();
        commit();
        repaint();
    }
}

} // namespace verdant::ui
