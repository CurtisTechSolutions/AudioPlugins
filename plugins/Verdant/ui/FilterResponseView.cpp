#include "FilterResponseView.h"

#include "../PluginProcessor.h"

#include <complex>

namespace verdant::ui
{

namespace
{
    juce::Colour col (juce::uint32 c) { return juce::Colour (c); }
    constexpr float minHz = 20.0f, maxHz = 20000.0f;
    constexpr double displayRate = 48000.0;

    using cd = std::complex<double>;

    cd lp2 (cd s, double q) { return 1.0 / (s * s + s / q + 1.0); }
    cd hp2 (cd s, double q) { return s * s / (s * s + s / q + 1.0); }
    cd bp2 (cd s, double q) { return (s / q) / (s * s + s / q + 1.0); }

    float raw (Processor& p, P param)
    {
        if (auto* v = p.apvts.getRawParameterValue (info (param).id))
            return v->load (std::memory_order_relaxed);
        return 0.0f;
    }

    juce::Colour filterColour (int f) { return f == 0 ? col (palette::accent) : col (palette::secondary); }
}

//==============================================================================
float FilterResponseView::approxMagnitudeDb (FilterType type, float cutoffHz, float res01, float atHz)
{
    const double w = (double) atHz / (double) juce::jmax (1.0f, cutoffHz);
    const cd s (0.0, w);
    const double r = juce::jlimit (0.0, 1.0, (double) res01);
    const double q = 0.707 + std::pow (r, 1.5) * 9.0;
    cd h (1.0, 0.0);

    switch (type)
    {
        case FilterType::LP12:   h = lp2 (s, q); break;
        case FilterType::LP24:   h = lp2 (s, q) * lp2 (s, 0.54); break;
        case FilterType::HP12:   h = hp2 (s, q); break;
        case FilterType::HP24:   h = hp2 (s, q) * hp2 (s, 0.54); break;
        case FilterType::BP12:   h = bp2 (s, 0.5 + q * 0.6); break;
        case FilterType::BP24:   h = bp2 (s, 0.5 + q * 0.6) * bp2 (s, 0.5 + q * 0.6); break;
        case FilterType::Notch:  h = (s * s + 1.0) / (s * s + s / (0.4 + q * 0.4) + 1.0); break;
        case FilterType::Ladder: h = lp2 (s, 0.55 + std::pow (r, 1.3) * 14.0) * lp2 (s, 0.55); break;
        case FilterType::CombPlus:
        case FilterType::CombMinus:
        {
            const double g = 0.97 * r;
            const cd z1 = std::exp (cd (0.0, -juce::MathConstants<double>::twoPi * w));
            h = (1.0 - g) / (1.0 + (type == FilterType::CombPlus ? -g : g) * z1);
            h *= 1.0 + g * 0.5;   // keep the peaks visible, roughly 0 dB average
            break;
        }
        case FilterType::Formant:
        {
            // A-E-I-O-U swept by the cutoff position over the log range.
            static constexpr double vowels[5][3] = { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 350, 1700, 2700 },
                                                     { 450, 800, 2830 }, { 325, 700, 2530 } };
            const double pos = juce::jlimit (0.0, 1.0, std::log ((double) cutoffHz / minHz) / std::log ((double) maxHz / minHz)) * 4.0;
            const int v0 = juce::jlimit (0, 3, (int) pos);
            const double t = pos - v0;
            const double fq = 4.0 + r * 16.0;
            cd sum (0.0, 0.0);
            const double gains[3] = { 1.0, 0.6, 0.35 };
            for (int k = 0; k < 3; ++k)
            {
                const double fk = vowels[v0][k] + (vowels[v0 + 1][k] - vowels[v0][k]) * t;
                sum += gains[k] * bp2 (cd (0.0, atHz / fk), fq);
            }
            h = sum;
            break;
        }
        case FilterType::Count:
        default: break;
    }
    return (float) (20.0 * std::log10 (juce::jmax (1.0e-6, std::abs (h))));
}

float FilterResponseView::responseDb (FilterType type, float cutoffHz, float res01, float atHz, bool& usedFallback)
{
    if (! usedFallback)
        return filterMagnitudeDb (type, cutoffHz, res01, displayRate, atHz);
    return approxMagnitudeDb (type, cutoffHz, res01, atHz);
}

//==============================================================================
FilterResponseView::FilterResponseView (Processor& p) : processor (p)
{
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    for (int f = 0; f < 2; ++f)
        compute (f);
}

FilterResponseView::State FilterResponseView::readState (int f) const
{
    auto& p = processor;
    State s;
    s.on = raw (p, filterParam (f, FilterParam::On)) > 0.5f;
    s.type = (int) std::lround (raw (p, filterParam (f, FilterParam::Type)));
    s.cutoff = raw (p, filterParam (f, FilterParam::Cutoff));
    s.res = raw (p, filterParam (f, FilterParam::Res)) * 0.01f;
    s.mix = raw (p, filterParam (f, FilterParam::Mix)) * 0.01f;

    // While a voice plays, follow the modulated cutoff / resonance.
    const auto& vis = p.engine().visuals();
    if (vis.activeVoices.load (std::memory_order_relaxed) > 0)
    {
        const auto cutP = filterParam (f, FilterParam::Cutoff), resP = filterParam (f, FilterParam::Res);
        const float dc = vis.modOffset[modDestinationIndexOf (cutP)].load (std::memory_order_relaxed);
        const float dr = vis.modOffset[modDestinationIndexOf (resP)].load (std::memory_order_relaxed);
        if (! juce::exactlyEqual (dc, 0.0f)) s.cutoff = fromNormalised (cutP, juce::jlimit (0.0f, 1.0f, toNormalised (cutP, s.cutoff) + dc));
        if (! juce::exactlyEqual (dr, 0.0f)) s.res = juce::jlimit (0.0f, 1.0f, s.res + dr);
    }
    // Quantise so tiny drifts do not trigger recomputation.
    s.cutoff = std::round (s.cutoff * 10.0f) / 10.0f;
    s.res = std::round (s.res * 1000.0f) / 1000.0f;
    s.mix = std::round (s.mix * 1000.0f) / 1000.0f;
    return s;
}

void FilterResponseView::compute (int f)
{
    auto& c = curves[(size_t) f];
    c.state = readState (f);
    const auto type = (FilterType) juce::jlimit (0, (int) FilterType::Count - 1, c.state.type);

    bool fallback = false;
    float maxAbs = 0.0f;
    for (int i = 0; i < numPoints; ++i)
    {
        const float hz = minHz * std::pow (maxHz / minHz, (float) i / (float) (numPoints - 1));
        c.db[(size_t) i] = responseDb (type, c.state.cutoff, c.state.res, hz, fallback);
        maxAbs = juce::jmax (maxAbs, std::abs (c.db[(size_t) i]));
    }
    if (maxAbs < 1.0e-3f)
    {
        fallback = true;   // engine stub: draw the analytic shape instead of a flat line
        for (int i = 0; i < numPoints; ++i)
        {
            const float hz = minHz * std::pow (maxHz / minHz, (float) i / (float) (numPoints - 1));
            c.db[(size_t) i] = responseDb (type, c.state.cutoff, c.state.res, hz, fallback);
        }
    }
    // Mix blends the dry signal back in (magnitudes, ignoring phase).
    if (c.state.mix < 0.999f)
        for (auto& d : c.db)
            d = 20.0f * std::log10 (juce::jmax (1.0e-6f, c.state.mix * juce::Decibels::decibelsToGain (d, -120.0f) + (1.0f - c.state.mix)));
    c.valid = true;
}

void FilterResponseView::tick()
{
    bool changed = false;
    for (int f = 0; f < 2; ++f)
        if (! (readState (f) == curves[(size_t) f].state))
        {
            compute (f);
            changed = true;
        }
    if (changed)
        repaint();
}

void FilterResponseView::resized() {}

//==============================================================================
juce::Rectangle<float> FilterResponseView::plotArea() const
{
    return getLocalBounds().toFloat().reduced (10.0f, 8.0f).withTrimmedBottom (12.0f);
}

float FilterResponseView::xForHz (float hz) const
{
    const auto a = plotArea();
    return a.getX() + a.getWidth() * std::log (hz / minHz) / std::log (maxHz / minHz);
}

float FilterResponseView::hzForX (float x) const
{
    const auto a = plotArea();
    return minHz * std::pow (maxHz / minHz, juce::jlimit (0.0f, 1.0f, (x - a.getX()) / a.getWidth()));
}

float FilterResponseView::yForDb (float db) const
{
    // +18 dB at the top, -30 dB at the bottom: 0 dB sits in the upper half where pass bands live.
    const auto a = plotArea();
    constexpr float top = 18.0f, bottom = -30.0f;
    return a.getY() + a.getHeight() * (top - juce::jlimit (bottom - 4.0f, top + 4.0f, db)) / (top - bottom);
}

juce::Point<float> FilterResponseView::handlePos (int f) const
{
    const auto& c = curves[(size_t) f];
    const float x = xForHz (juce::jlimit (minHz, maxHz, c.state.cutoff));
    const int i = juce::jlimit (0, numPoints - 1, (int) std::lround (std::log (c.state.cutoff / minHz) / std::log (maxHz / minHz) * (numPoints - 1)));
    const auto a = plotArea();
    return { x, juce::jlimit (a.getY() + 4.0f, a.getBottom() - 4.0f, yForDb (c.db[(size_t) i])) };
}

int FilterResponseView::handleAt (juce::Point<float> p) const
{
    int best = -1;
    float bestD = 14.0f;
    for (int f = 0; f < 2; ++f)
    {
        if (! curves[(size_t) f].state.on) continue;
        const float d = p.getDistanceFrom (handlePos (f));
        if (d < bestD) { bestD = d; best = f; }
    }
    if (best < 0)
    {
        // Anywhere else: the nearest enabled filter by cutoff x.
        float bestX = 1.0e9f;
        for (int f = 0; f < 2; ++f)
            if (curves[(size_t) f].state.on && std::abs (p.x - handlePos (f).x) < bestX)
            {
                bestX = std::abs (p.x - handlePos (f).x);
                best = f;
            }
    }
    return best;
}

void FilterResponseView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    paintDisplay (g, bounds, col (palette::accent));
    const auto a = plotArea();

    // Grid.
    g.setFont (font (9.5f));
    for (int hzInt : { 50, 100, 200, 500, 1000, 2000, 5000, 10000 })
    {
        const float hz = (float) hzInt;
        const float x = std::round (xForHz (hz)) + 0.5f;
        const bool major = hzInt == 100 || hzInt == 1000 || hzInt == 10000;
        g.setColour (col (palette::grid).brighter (major ? 0.12f : 0.0f));
        g.fillRect (juce::Rectangle<float> (x - 0.5f, a.getY(), 1.0f, a.getHeight()));
        if (major || hzInt == 500 || hzInt == 5000 || hzInt == 50)
        {
            g.setColour (col (palette::textFaint));
            const auto label = hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz);
            g.drawText (label, juce::Rectangle<float> (x - 20.0f, a.getBottom() + 1.0f, 40.0f, 11.0f), juce::Justification::centred, false);
        }
    }
    for (int dbInt : { -24, -12, 0, 12 })
    {
        const float db = (float) dbInt;
        const float y = std::round (yForDb (db)) + 0.5f;
        g.setColour (col (palette::grid).brighter (dbInt == 0 ? 0.15f : 0.0f));
        g.fillRect (juce::Rectangle<float> (a.getX(), y - 0.5f, a.getWidth(), 1.0f));
        g.setColour (col (palette::textFaint));
        g.drawText ((dbInt > 0 ? "+" : "") + juce::String (dbInt) + " dB", juce::Rectangle<float> (a.getX() + 2.0f, y - 11.0f, 40.0f, 10.0f),
                    juce::Justification::centredLeft, false);
    }

    // Curves: Filter 2 first so Filter 1 sits on top.
    g.saveState();
    g.reduceClipRegion (a.toNearestInt().expanded (0, 2));
    for (int f = 1; f >= 0; --f)
    {
        const auto& c = curves[(size_t) f];
        const auto colour = filterColour (f);
        juce::Path line;
        for (int i = 0; i < numPoints; ++i)
        {
            const float x = a.getX() + a.getWidth() * (float) i / (float) (numPoints - 1);
            const float y = yForDb (c.db[(size_t) i]);
            if (i == 0) line.startNewSubPath (x, y);
            else        line.lineTo (x, y);
        }

        if (! c.state.on)
        {
            juce::Path dashed;
            const float dashes[] = { 3.0f, 4.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, line, dashes, 2);
            g.setColour (colour.withAlpha (0.25f));
            g.fillPath (dashed);
            continue;
        }

        juce::Path fill (line);
        fill.lineTo (a.getRight(), a.getBottom() + 4.0f);
        fill.lineTo (a.getX(), a.getBottom() + 4.0f);
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (colour.withAlpha (f == 0 ? 0.24f : 0.16f), 0.0f, a.getY(),
                                                 colour.withAlpha (0.0f), 0.0f, a.getBottom(), false));
        g.fillPath (fill);

        g.setColour (colour.withAlpha (0.18f));
        g.strokePath (line, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (colour);
        g.strokePath (line, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.restoreState();

    // Handles + legend.
    for (int f = 1; f >= 0; --f)
    {
        const auto& c = curves[(size_t) f];
        if (! c.state.on) continue;
        const auto colour = filterColour (f);
        const auto p = handlePos (f);
        const bool active = hover == f || dragging == f;
        g.setColour (colour.withAlpha (active ? 0.35f : 0.2f));
        g.fillEllipse (juce::Rectangle<float> (active ? 18.0f : 14.0f, active ? 18.0f : 14.0f).withCentre (p));
        g.setColour (col (palette::display));
        g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (p));
        g.setColour (colour);
        g.drawEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (p), 2.0f);
        g.setFont (font (9.5f, true));
        g.drawText (f == 0 ? "1" : "2", juce::Rectangle<float> (16.0f, 12.0f).withCentre (p.translated (0.0f, -13.0f)), juce::Justification::centred, false);
    }

    if (dragging >= 0)
    {
        const auto& c = curves[(size_t) dragging];
        const auto text = (c.state.cutoff >= 1000.0f ? juce::String (c.state.cutoff / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (c.state.cutoff)) + " Hz")
                          + "   Res " + juce::String (juce::roundToInt (c.state.res * 100.0f)) + " %";
        g.setColour (col (palette::text));
        g.setFont (font (Theme::fontSmall, true));
        g.drawText (text, getLocalBounds().reduced (12, 8).removeFromTop (14), juce::Justification::topLeft, false);
    }
}

//==============================================================================
void FilterResponseView::mouseMove (const juce::MouseEvent& e)
{
    int h = -1;
    for (int f = 0; f < 2; ++f)
        if (curves[(size_t) f].state.on && e.position.getDistanceFrom (handlePos (f)) < 14.0f)
            h = f;
    if (h != hover) { hover = h; repaint(); }
}

void FilterResponseView::mouseExit (const juce::MouseEvent&)
{
    if (hover != -1) { hover = -1; repaint(); }
}

void FilterResponseView::mouseDown (const juce::MouseEvent& e)
{
    dragging = handleAt (e.position);
    if (dragging < 0) return;
    dragCutoff = curves[(size_t) dragging].state.cutoff;
    dragRes = raw (processor, filterParam (dragging, FilterParam::Res));
    for (auto fp : { FilterParam::Cutoff, FilterParam::Res })
        if (auto* p = processor.apvts.getParameter (info (filterParam (dragging, fp)).id))
            p->beginChangeGesture();
    mouseDrag (e);
}

void FilterResponseView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0) return;
    const bool fine = e.mods.isShiftDown();
    auto* cut = processor.apvts.getParameter (info (filterParam (dragging, FilterParam::Cutoff)).id);
    auto* res = processor.apvts.getParameter (info (filterParam (dragging, FilterParam::Res)).id);

    float hz = hzForX (e.position.x);
    if (fine)
        hz = dragCutoff * std::pow (2.0f, (float) e.getDistanceFromDragStartX() / 400.0f);
    if (cut != nullptr) cut->setValueNotifyingHost (cut->convertTo0to1 (juce::jlimit (minHz, maxHz, hz)));
    if (res != nullptr)
        res->setValueNotifyingHost (res->convertTo0to1 (juce::jlimit (0.0f, 100.0f, dragRes - (float) e.getDistanceFromDragStartY() * (fine ? 0.1f : 0.5f))));
    tick();
    repaint();
}

void FilterResponseView::mouseUp (const juce::MouseEvent&)
{
    if (dragging < 0) return;
    for (auto fp : { FilterParam::Cutoff, FilterParam::Res })
        if (auto* p = processor.apvts.getParameter (info (filterParam (dragging, fp)).id))
            p->endChangeGesture();
    dragging = -1;
    repaint();
}

} // namespace verdant::ui
