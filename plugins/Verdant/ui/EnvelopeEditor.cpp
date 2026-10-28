#include "EnvelopeEditor.h"

#include "../PluginProcessor.h"

namespace verdant::ui
{

namespace
{
    juce::Colour col (juce::uint32 c) { return juce::Colour (c); }

    float invShape (float u, float c) noexcept
    {
        u = juce::jlimit (0.0f, 1.0f, u);
        return c >= 0.0f ? std::pow (u, 1.0f + 3.0f * c) : std::pow (u, 1.0f / (1.0f + 3.0f * -c));
    }
}

float EnvelopeEditor::shape (float t, float c) noexcept
{
    t = juce::jlimit (0.0f, 1.0f, t);
    return c >= 0.0f ? std::pow (t, 1.0f / (1.0f + 3.0f * c)) : std::pow (t, 1.0f + 3.0f * -c);
}

EnvelopeEditor::EnvelopeEditor (Processor& p, int e, juce::Colour c) : processor (p), env (e), colour (c) {}

void EnvelopeEditor::setEnvelope (int e)
{
    env = juce::jlimit (0, 2, e);
    stageGuess = 0;
    repaint();
}

void EnvelopeEditor::setColour (juce::Colour c) { colour = c; repaint(); }

juce::RangedAudioParameter* EnvelopeEditor::param (EnvParam ep) const
{
    return processor.apvts.getParameter (info (envParam (env, ep)).id);
}

float EnvelopeEditor::value (EnvParam ep) const
{
    if (auto* v = processor.apvts.getRawParameterValue (info (envParam (env, ep)).id))
        return v->load (std::memory_order_relaxed);
    return 0.0f;
}

EnvelopeEditor::Layout EnvelopeEditor::layout() const
{
    Layout l;
    l.area = getLocalBounds().toFloat().reduced (12.0f, 10.0f);
    const float sustainW = l.area.getWidth() * 0.14f;
    auto n = [this] (EnvParam ep) { return toNormalised (envParam (env, ep), value (ep)); };
    // Zoom to fit: the segments fill ~92 % of the width (between 1/4 and 1/1.2 of it per unit of knob travel).
    // The zoom is frozen while a handle is dragged so the handle tracks the mouse exactly.
    const float avail = l.area.getWidth() - sustainW;
    const float natural = n (EnvParam::A) + n (EnvParam::H) + n (EnvParam::D) + n (EnvParam::R);
    l.segMax = frozenUnit > 0.0f ? frozenUnit
                                 : juce::jlimit (avail / 4.0f, avail / 1.2f, avail * 0.92f / juce::jmax (1.0e-3f, natural));
    l.xA0 = l.area.getX();
    l.xA = l.xA0 + juce::jmax (1.0f, n (EnvParam::A) * l.segMax);
    l.xH = l.xA + n (EnvParam::H) * l.segMax;
    l.xD = l.xH + juce::jmax (1.0f, n (EnvParam::D) * l.segMax);
    l.xS = l.xD + sustainW;
    l.xR = l.xS + juce::jmax (1.0f, n (EnvParam::R) * l.segMax);
    l.yTop = l.area.getY() + 4.0f;
    l.yBottom = l.area.getBottom();
    l.ySus = l.yBottom - value (EnvParam::S) * 0.01f * (l.yBottom - l.yTop);
    return l;
}

juce::Path EnvelopeEditor::curvePath (const Layout& l) const
{
    const float ac = value (EnvParam::ACurve) * 0.01f, dc = value (EnvParam::DCurve) * 0.01f;
    const float s = value (EnvParam::S) * 0.01f;
    auto yOf = [&l] (float level) { return l.yBottom - level * (l.yBottom - l.yTop); };

    juce::Path p;
    p.startNewSubPath (l.xA0, l.yBottom);
    constexpr int steps = 32;
    for (int i = 1; i <= steps; ++i)
    {
        const float t = (float) i / steps;
        p.lineTo (l.xA0 + t * (l.xA - l.xA0), yOf (shape (t, ac)));
    }
    p.lineTo (l.xH, l.yTop);
    for (int i = 1; i <= steps; ++i)
    {
        const float t = (float) i / steps;
        p.lineTo (l.xH + t * (l.xD - l.xH), yOf (1.0f - (1.0f - s) * shape (t, -dc)));
    }
    p.lineTo (l.xS, l.ySus);
    for (int i = 1; i <= steps; ++i)
    {
        const float t = (float) i / steps;
        p.lineTo (l.xS + t * (l.xR - l.xS), yOf (s * (1.0f - shape (t, -dc))));
    }
    return p;
}

juce::Point<float> EnvelopeEditor::handlePos (Handle h, const Layout& l) const
{
    const float ac = value (EnvParam::ACurve) * 0.01f, dc = value (EnvParam::DCurve) * 0.01f;
    const float s = value (EnvParam::S) * 0.01f;
    auto yOf = [&l] (float level) { return l.yBottom - level * (l.yBottom - l.yTop); };
    switch (h)
    {
        case Attack:  return { l.xA, l.yTop };
        case Hold:    return { l.xH, l.yTop };
        case Decay:   return { l.xD, l.ySus };
        case Release: return { l.xR, l.yBottom };
        case ACurve:  return { (l.xA0 + l.xA) * 0.5f, yOf (shape (0.5f, ac)) };
        case DCurve:  return { (l.xH + l.xD) * 0.5f, yOf (1.0f - (1.0f - s) * shape (0.5f, -dc)) };
        case None: case NumHandles: default: return {};
    }
}

EnvelopeEditor::Handle EnvelopeEditor::handleAt (juce::Point<float> p) const
{
    const auto l = layout();
    Handle best = None;
    float bestD = 10.0f;
    for (int h = 0; h < NumHandles; ++h)
    {
        const float d = p.getDistanceFrom (handlePos ((Handle) h, l));
        if (d < bestD || (juce::approximatelyEqual (d, bestD) && h == Hold && p.x > l.xA + 2.0f))
        {
            bestD = d;
            best = (Handle) h;
        }
    }
    // Hold sits on the attack peak when it is 0: dragging to the right of the peak grabs it.
    if (best == Attack && value (EnvParam::H) <= 0.0f && p.x > l.xA + 3.0f)
        best = Hold;
    return best;
}

juce::String EnvelopeEditor::handleText (Handle h) const
{
    auto text = [this] (EnvParam ep)
    {
        auto* p = param (ep);
        return p != nullptr ? p->getCurrentValueAsText() : juce::String();
    };
    switch (h)
    {
        case Attack:  return "Attack " + text (EnvParam::A);
        case Hold:    return "Hold " + text (EnvParam::H);
        case Decay:   return "Decay " + text (EnvParam::D) + "  Sustain " + text (EnvParam::S);
        case Release: return "Release " + text (EnvParam::R);
        case ACurve:  return "Attack curve " + text (EnvParam::ACurve);
        case DCurve:  return "Decay curve " + text (EnvParam::DCurve);
        case None: case NumHandles: default: return {};
    }
}

juce::Point<float> EnvelopeEditor::playheadPos (const Layout& l) const
{
    const float v = shownValue;
    const float ac = value (EnvParam::ACurve) * 0.01f, dc = value (EnvParam::DCurve) * 0.01f;
    const float s = value (EnvParam::S) * 0.01f;
    const float y = l.yBottom - v * (l.yBottom - l.yTop);
    switch (stageGuess)
    {
        case 1:  return { l.xA0 + invShape (v, ac) * (l.xA - l.xA0), y };
        case 2:
            if (v >= 0.999f && l.xH > l.xA) return { l.xH, y };
            return { l.xH + invShape ((1.0f - v) / juce::jmax (1.0e-3f, 1.0f - s), -dc) * (l.xD - l.xH), y };
        case 3:  return { (l.xD + l.xS) * 0.5f, y };
        case 4:  return { l.xS + invShape (1.0f - v / juce::jmax (1.0e-3f, s), -dc) * (l.xR - l.xS), y };
        default: return {};
    }
}

void EnvelopeEditor::tick()
{
    // Parameter changes (knobs, automation) -> redraw.
    uint32_t sig = 17;
    for (int i = 0; i < (int) EnvParam::Count; ++i)
        sig = sig * 31u + (uint32_t) (juce::int64) std::lround (value ((EnvParam) i) * 100.0f);
    bool dirty = sig != signature;
    signature = sig;

    const auto& vis = processor.engine().visuals();
    const float v = vis.activeVoices.load (std::memory_order_relaxed) > 0 ? vis.envValue[env].load (std::memory_order_relaxed) : 0.0f;
    const float s = value (EnvParam::S) * 0.01f;
    const int before = stageGuess;
    if (v < 0.001f)                                   stageGuess = 0;
    else if (stageGuess == 0)                         stageGuess = 1;
    else if (stageGuess == 1 && (v < prevValue - 1.0e-4f || v >= 0.999f)) stageGuess = 2;
    else if (stageGuess == 2 && std::abs (v - s) < 0.01f) stageGuess = 3;
    else if ((stageGuess == 2 || stageGuess == 3) && v < s - 0.01f && v < prevValue) stageGuess = 4;
    else if ((stageGuess == 3 || stageGuess == 4) && v > prevValue + 0.01f) stageGuess = 1;
    prevValue = v;

    if (std::abs (v - shownValue) > 0.002f || before != stageGuess)
    {
        shownValue = v;
        dirty = true;
    }
    if (dirty)
        repaint();
}

void EnvelopeEditor::paint (juce::Graphics& g)
{
    paintDisplay (g, getLocalBounds().toFloat(), colour);
    const auto l = layout();

    // Grid: quarter levels and the segment boundaries.
    g.setColour (col (palette::grid));
    for (int i = 1; i < 4; ++i)
    {
        const float y = std::round (l.yBottom - (l.yBottom - l.yTop) * (float) i / 4.0f) + 0.5f;
        g.fillRect (juce::Rectangle<float> (l.area.getX(), y - 0.5f, l.area.getWidth(), 1.0f));
    }
    for (float x : { l.xA, l.xH, l.xD, l.xS })
        g.fillRect (juce::Rectangle<float> (std::round (x), l.area.getY(), 1.0f, l.area.getHeight()));

    const auto curve = curvePath (l);
    juce::Path fill (curve);
    fill.lineTo (l.xR, l.yBottom);
    fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.26f), 0.0f, l.yTop, colour.withAlpha (0.02f), 0.0f, l.yBottom, false));
    g.fillPath (fill);
    g.setColour (colour.withAlpha (0.18f));
    g.strokePath (curve, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (colour);
    g.strokePath (curve, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Segment letters along the bottom.
    g.setFont (font (9.5f, true));
    g.setColour (col (palette::textFaint));
    auto letter = [&] (const char* t, float x0, float x1)
    {
        if (x1 - x0 > 12.0f)
            g.drawText (t, juce::Rectangle<float> (x0, l.yBottom - 12.0f, x1 - x0, 11.0f), juce::Justification::centred, false);
    };
    letter ("A", l.xA0, l.xA); letter ("H", l.xA, l.xH); letter ("D", l.xH, l.xD); letter ("S", l.xD, l.xS); letter ("R", l.xS, l.xR);

    // Handles.
    for (int h = 0; h < NumHandles; ++h)
    {
        if (h == Hold && value (EnvParam::H) <= 0.0f && hover != Hold && dragging != Hold)
            continue;
        const auto p = handlePos ((Handle) h, l);
        const bool active = hover == h || dragging == h;
        const bool curveHandle = h == ACurve || h == DCurve;
        const float d = curveHandle ? 6.0f : 8.0f;
        if (active)
        {
            g.setColour (colour.withAlpha (0.3f));
            g.fillEllipse (juce::Rectangle<float> (d + 8.0f, d + 8.0f).withCentre (p));
        }
        g.setColour (curveHandle ? colour.withAlpha (0.6f) : col (palette::display));
        g.fillEllipse (juce::Rectangle<float> (d, d).withCentre (p));
        if (! curveHandle)
        {
            g.setColour (colour);
            g.drawEllipse (juce::Rectangle<float> (d, d).withCentre (p), 1.6f);
        }
    }

    // Playhead.
    if (stageGuess > 0)
    {
        const auto p = playheadPos (l);
        g.setColour (col (palette::text).withAlpha (0.25f));
        g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (p));
        g.setColour (col (palette::text));
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (p));
    }

    // Read-out of the handle under the mouse.
    const auto h = dragging != None ? dragging : hover;
    if (h != None)
    {
        g.setColour (col (palette::text));
        g.setFont (font (Theme::fontSmall, true));
        g.drawText (handleText (h), getLocalBounds().reduced (12, 6).removeFromTop (14), juce::Justification::topRight, false);
    }
}

void EnvelopeEditor::mouseMove (const juce::MouseEvent& e)
{
    const auto h = handleAt (e.position);
    if (h != hover)
    {
        hover = h;
        setMouseCursor (h == None ? juce::MouseCursor::NormalCursor : juce::MouseCursor::DraggingHandCursor);
        repaint();
    }
}

void EnvelopeEditor::mouseExit (const juce::MouseEvent&)
{
    if (hover != None) { hover = None; repaint(); }
}

void EnvelopeEditor::mouseDown (const juce::MouseEvent& e)
{
    dragging = handleAt (e.position);
    frozenUnit = dragging != None ? layout().segMax : 0.0f;
    auto begin = [this] (EnvParam ep) { if (auto* p = param (ep)) p->beginChangeGesture(); };
    switch (dragging)
    {
        case Attack:  begin (EnvParam::A); break;
        case Hold:    begin (EnvParam::H); break;
        case Decay:   begin (EnvParam::D); begin (EnvParam::S); break;
        case Release: begin (EnvParam::R); break;
        case ACurve:  begin (EnvParam::ACurve); dragStart[0] = value (EnvParam::ACurve); break;
        case DCurve:  begin (EnvParam::DCurve); dragStart[0] = value (EnvParam::DCurve); break;
        case None: case NumHandles: default: break;
    }
    repaint();
}

void EnvelopeEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging == None) return;
    const auto l = layout();
    auto setNorm = [this] (EnvParam ep, float n) { if (auto* p = param (ep)) p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, n)); };
    auto setPlain = [this] (EnvParam ep, float v) { if (auto* p = param (ep)) p->setValueNotifyingHost (p->convertTo0to1 (v)); };
    const float x = e.position.x;
    switch (dragging)
    {
        case Attack:  setNorm (EnvParam::A, (x - l.xA0) / l.segMax); break;
        case Hold:    setNorm (EnvParam::H, (x - l.xA) / l.segMax); break;
        case Decay:
            setNorm (EnvParam::D, (x - l.xH) / l.segMax);
            setPlain (EnvParam::S, juce::jlimit (0.0f, 100.0f, (l.yBottom - e.position.y) / (l.yBottom - l.yTop) * 100.0f));
            break;
        case Release: setNorm (EnvParam::R, (x - l.xS) / l.segMax); break;
        case ACurve:  setPlain (EnvParam::ACurve, juce::jlimit (-100.0f, 100.0f, dragStart[0] - (float) e.getDistanceFromDragStartY() * 0.8f)); break;
        case DCurve:  setPlain (EnvParam::DCurve, juce::jlimit (-100.0f, 100.0f, dragStart[0] - (float) e.getDistanceFromDragStartY() * 0.8f)); break;
        case None: case NumHandles: default: break;
    }
    repaint();
}

void EnvelopeEditor::mouseUp (const juce::MouseEvent&)
{
    auto end = [this] (EnvParam ep) { if (auto* p = param (ep)) p->endChangeGesture(); };
    switch (dragging)
    {
        case Attack:  end (EnvParam::A); break;
        case Hold:    end (EnvParam::H); break;
        case Decay:   end (EnvParam::D); end (EnvParam::S); break;
        case Release: end (EnvParam::R); break;
        case ACurve:  end (EnvParam::ACurve); break;
        case DCurve:  end (EnvParam::DCurve); break;
        case None: case NumHandles: default: break;
    }
    dragging = None;
    frozenUnit = 0.0f;
    repaint();
}

void EnvelopeEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    auto reset = [this] (EnvParam ep)
    {
        if (auto* p = param (ep))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getDefaultValue());
            p->endChangeGesture();
        }
    };
    switch (handleAt (e.position))
    {
        case Attack:  reset (EnvParam::A); break;
        case Hold:    reset (EnvParam::H); break;
        case Decay:   reset (EnvParam::D); reset (EnvParam::S); break;
        case Release: reset (EnvParam::R); break;
        case ACurve:  reset (EnvParam::ACurve); break;
        case DCurve:  reset (EnvParam::DCurve); break;
        case None: case NumHandles: default: break;
    }
    repaint();
}

} // namespace verdant::ui
