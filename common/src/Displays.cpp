#include "ap_ui/Displays.h"

#include <algorithm>
#include <iterator>

namespace ap::ui
{

void Display::paint (juce::Graphics& g)
{
    const auto& th = themeOf (*this);
    auto r = getLocalBounds().toFloat();
    drawWell (g, r, th, 6.0f);
    auto inner = r.reduced (1.0f);
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (inner.toNearestInt());
        paintDisplay (g, inner);
    }
    if (captionText.isNotEmpty())
        drawCaps (g, captionText, r.reduced (8.0f, 5.0f).removeFromTop (12.0f), th.textFaint,
                  juce::Justification::topLeft, Type::small().boldened().withExtraKerningFactor (0.12f));
}

void Display::gridLine (juce::Graphics& g, const Theme& th, float x0, float y0, float x1, float y1, bool strong)
{
    g.setColour (strong ? th.grid.brighter (0.25f) : th.grid);
    g.drawLine (x0, y0, x1, y1, 1.0f);
}

//==============================================================================
AdsrDisplay::AdsrDisplay (juce::AudioProcessorValueTreeState& apvts, const char* attackId, const char* decayId,
                          const char* sustainId, const char* releaseId, juce::String caption)
    : Display (std::move (caption)),
      a (apvts.getRawParameterValue (attackId)), d (apvts.getRawParameterValue (decayId)),
      s (apvts.getRawParameterValue (sustainId)), r (apvts.getRawParameterValue (releaseId))
{
    jassert (a != nullptr && d != nullptr && s != nullptr && r != nullptr);
}

void AdsrDisplay::tick()
{
    const float now[4] = { a->load(), d->load(), s->load(), r->load() };
    if (! std::equal (std::begin (now), std::end (now), std::begin (shown), [] (float x, float y) { return juce::exactlyEqual (x, y); }))
    {
        std::copy (std::begin (now), std::end (now), std::begin (shown));
        repaint();
    }
}

void AdsrDisplay::paintDisplay (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& th = themeOf (*this);
    area = area.reduced (10.0f, 8.0f).withTrimmedTop (10.0f);

    // Segment widths grow with the cube root of their times so 1 ms and 5 s both stay readable.
    auto w = [] (float ms) { return std::cbrt (juce::jmax (0.5f, ms)); };
    const float wa = w (shown[0]), wd = w (shown[1]), ws = w (300.0f), wr = w (shown[3]);
    const float total = wa + wd + ws + wr;
    const float sus = juce::jlimit (0.0f, 1.0f, shown[2] * 0.01f);
    auto X = [&] (float units) { return area.getX() + area.getWidth() * units / total; };
    auto Y = [&] (float level) { return area.getBottom() - area.getHeight() * level; };

    for (int i = 1; i < 4; ++i)
        gridLine (g, th, area.getX(), area.getY() + area.getHeight() * (float) i / 4.0f,
                  area.getRight(), area.getY() + area.getHeight() * (float) i / 4.0f);

    juce::Path p;
    p.startNewSubPath (X (0.0f), Y (0.0f));
    // Attack: slightly convex; decay and release: exponential-looking.
    const int steps = 24;
    for (int i = 1; i <= steps; ++i)
    {
        const float t = (float) i / steps;
        p.lineTo (X (wa * t), Y (1.0f - std::pow (1.0f - t, 1.6f)));
    }
    for (int i = 1; i <= steps; ++i)
    {
        const float t = (float) i / steps;
        p.lineTo (X (wa + wd * t), Y (sus + (1.0f - sus) * std::pow (1.0f - t, 2.6f)));
    }
    p.lineTo (X (wa + wd + ws), Y (sus));
    for (int i = 1; i <= steps; ++i)
    {
        const float t = (float) i / steps;
        p.lineTo (X (wa + wd + ws + wr * t), Y (sus * std::pow (1.0f - t, 2.6f)));
    }

    juce::Path fill (p);
    fill.lineTo (X (total), Y (0.0f));
    fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (th.accent.withAlpha (0.28f), 0.0f, area.getY(),
                                             th.accent.withAlpha (0.02f), 0.0f, area.getBottom(), false));
    g.fillPath (fill);
    g.setColour (th.accent.withAlpha (0.25f));
    g.strokePath (p, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (th.accent);
    g.strokePath (p, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Handles at the corners.
    for (auto pt : { juce::Point<float> (X (wa), Y (1.0f)), juce::Point<float> (X (wa + wd), Y (sus)),
                     juce::Point<float> (X (wa + wd + ws), Y (sus)) })
    {
        g.setColour (th.well);
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (pt));
        g.setColour (th.text);
        g.drawEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (pt), 1.4f);
    }
}

//==============================================================================
ResponseDisplay::ResponseDisplay (std::function<float (float)> resp, std::function<float()> sig,
                                  float lo, float hi, juce::String caption)
    : Display (std::move (caption)), response (std::move (resp)), signature (std::move (sig)), minDb (lo), maxDb (hi)
{
}

void ResponseDisplay::tick()
{
    const float s = signature != nullptr ? signature() : 0.0f;
    if (! juce::exactlyEqual (s, lastSignature) || curve.empty())
    {
        lastSignature = s;
        curve.resize (160);
        for (size_t i = 0; i < curve.size(); ++i)
        {
            const float hz = 20.0f * std::pow (1000.0f, (float) i / (float) (curve.size() - 1));
            curve[i] = response (hz);
        }
        repaint();
    }
}

void ResponseDisplay::paintDisplay (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& th = themeOf (*this);
    if (curve.empty()) tick();
    area = area.reduced (2.0f, 6.0f).withTrimmedTop (8.0f);

    auto xOf = [&] (float hz) { return area.getX() + area.getWidth() * std::log (hz / 20.0f) / std::log (1000.0f); };
    auto yOf = [&] (float db) { return area.getY() + area.getHeight() * (maxDb - juce::jlimit (minDb, maxDb, db)) / (maxDb - minDb); };

    for (int hz : { 50, 100, 200, 500, 1000, 2000, 5000, 10000 })
        gridLine (g, th, xOf ((float) hz), area.getY(), xOf ((float) hz), area.getBottom(), hz == 100 || hz == 1000 || hz == 10000);
    const float step = (maxDb - minDb) > 30.0f ? 12.0f : 6.0f;
    for (float db = std::ceil (minDb / step) * step; db <= maxDb; db += step)
        gridLine (g, th, area.getX(), yOf (db), area.getRight(), yOf (db), std::abs (db) < 0.01f);

    g.setColour (th.textFaint);
    g.setFont (Type::small());
    for (auto [hz, text] : { std::pair { 100.0f, "100" }, std::pair { 1000.0f, "1k" }, std::pair { 10000.0f, "10k" } })
        g.drawText (text, juce::Rectangle<float> (30.0f, 12.0f).withPosition (xOf (hz) + 3.0f, area.getBottom() - 13.0f),
                    juce::Justification::centredLeft);

    juce::Path p;
    for (size_t i = 0; i < curve.size(); ++i)
    {
        const float x = area.getX() + area.getWidth() * (float) i / (float) (curve.size() - 1);
        const float y = yOf (curve[i]);
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    juce::Path fill (p);
    fill.lineTo (area.getRight(), area.getBottom() + 8.0f);
    fill.lineTo (area.getX(), area.getBottom() + 8.0f);
    fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (th.accent.withAlpha (0.22f), 0.0f, area.getY(),
                                             th.accent.withAlpha (0.0f), 0.0f, area.getBottom(), false));
    g.fillPath (fill);
    g.setColour (th.accent.withAlpha (0.25f));
    g.strokePath (p, juce::PathStrokeType (4.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (th.accent);
    g.strokePath (p, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (markerHz != nullptr)
    {
        const float hz = markerHz();
        if (hz > 20.0f && hz < 20000.0f)
        {
            const juce::Point<float> pt (xOf (hz), yOf (response (hz)));
            g.setColour (th.accent.withAlpha (0.3f));
            g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (pt));
            g.setColour (th.text);
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (pt));
        }
    }
}

//==============================================================================
namespace
{
    bool isBlack (int note);
}

juce::String midiNoteText (int note)
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[((note % 12) + 12) % 12]) + juce::String (note / 12 - 1);
}

void drawPitchGrid (juce::Graphics& g, const Theme& th, juce::Rectangle<float> keys, juce::Rectangle<float> plot,
                    float lo, float hi, const std::function<int (int)>& rowKind, int litNote)
{
    const float rowH = plot.getHeight() / (hi - lo);
    auto yOf = [&] (float m) { return plot.getBottom() - (m - lo) / (hi - lo) * plot.getHeight(); };

    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (keys.getUnion (plot).toNearestInt());

    g.setColour (juce::Colour (0xffd5d9df));
    g.fillRoundedRectangle (keys, 3.0f);

    for (int n = (int) std::floor (lo) - 1; n <= (int) std::ceil (hi) + 1; ++n)
    {
        const float yc = yOf ((float) n);
        const auto band = juce::Rectangle<float> (plot.getX(), yc - rowH * 0.5f, plot.getWidth(), rowH);
        const int pc = ((n % 12) + 12) % 12;
        const int kind = rowKind != nullptr ? rowKind (n) : 0;
        if (kind > 0)
        {
            g.setColour (th.accent.withAlpha (kind == 2 ? 0.16f : 0.08f));
            g.fillRect (band);
        }
        else if (kind < 0 || isBlack (n))
        {
            g.setColour (juce::Colours::black.withAlpha (0.28f));
            g.fillRect (band);
        }
        g.setColour (pc == 11 ? th.grid.brighter (0.5f) : th.grid);
        g.fillRect (plot.getX(), band.getY() - 0.5f, plot.getWidth(), 1.0f);

        const auto keyRow = juce::Rectangle<float> (keys.getX(), band.getY(), keys.getWidth(), rowH);
        if (isBlack (n))
        {
            g.setColour (n == litNote ? th.accent.darker (0.2f) : juce::Colour (0xff1d2127));
            g.fillRoundedRectangle (keyRow.withWidth (keys.getWidth() * 0.62f).reduced (0.0f, 0.5f), 1.5f);
        }
        else if (n == litNote)
        {
            g.setColour (th.accent);
            g.fillRect (keyRow.reduced (0.0f, 0.5f));
        }
        g.setColour (juce::Colour (0xffa9afb8));
        g.fillRect (keyRow.getX(), keyRow.getY() - 0.4f, keys.getWidth(), 0.8f);
        if (pc == 0 && rowH > 6.0f)
        {
            g.setColour (n == litNote ? th.background : juce::Colour (0xff4a515c));
            g.setFont (Type::small().boldened().withHeight (juce::jmin (9.5f, rowH + 1.0f)));
            g.drawText (midiNoteText (n), keyRow.withTrimmedRight (3.0f), juce::Justification::centredRight);
        }
    }
}

//==============================================================================
namespace
{
    bool isBlack (int note)
    {
        const int pc = ((note % 12) + 12) % 12;
        return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
    }
    int whiteIndex (int note)   // white keys below `note`, counted from C0
    {
        static const int offs[12] = { 0, 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6 };
        const int oct = note / 12;
        return oct * 7 + offs[note % 12];
    }
}

Keyboard::Keyboard (int lowestNote, int numOctaves)
    : lowest (lowestNote - lowestNote % 12), count (numOctaves * 12 + 1)
{
}

juce::Rectangle<float> Keyboard::keyBounds (int note) const
{
    const auto r = getLocalBounds().toFloat();
    const int whites = whiteIndex (lowest + count - 1) - whiteIndex (lowest) + 1;
    const float w = r.getWidth() / (float) whites;
    const float x = r.getX() + (float) (whiteIndex (note) - whiteIndex (lowest)) * w;
    if (! isBlack (note))
        return { x, r.getY(), w, r.getHeight() };
    return { x - w * 0.32f, r.getY(), w * 0.64f, r.getHeight() * 0.6f };
}

int Keyboard::noteAt (juce::Point<float> p) const
{
    for (int n = lowest; n < lowest + count; ++n)
        if (isBlack (n) && keyBounds (n).contains (p)) return n;
    for (int n = lowest; n < lowest + count; ++n)
        if (! isBlack (n) && keyBounds (n).contains (p)) return n;
    return -1;
}

void Keyboard::press (int note)
{
    if (note == mouseNote) return;
    if (mouseNote >= 0 && onNote) onNote (mouseNote, false);
    mouseNote = note;
    if (mouseNote >= 0 && onNote) onNote (mouseNote, true);
    repaint();
}

void Keyboard::mouseDown (const juce::MouseEvent& e) { press (noteAt (e.position)); }
void Keyboard::mouseDrag (const juce::MouseEvent& e) { press (noteAt (e.position)); }
void Keyboard::mouseUp (const juce::MouseEvent&)     { press (-1); }

void Keyboard::tick()
{
    bool changed = false;
    for (int n = lowest; n < lowest + count && n < 128; ++n)
    {
        const bool on = n == mouseNote || (isHeld != nullptr && isHeld (n));
        if (on != shown[(size_t) n]) { shown[(size_t) n] = on; changed = true; }
    }
    if (changed) repaint();
}

void Keyboard::paint (juce::Graphics& g)
{
    const auto& th = themeOf (*this);
    for (int pass = 0; pass < 2; ++pass)
        for (int n = lowest; n < lowest + count && n < 128; ++n)
        {
            if (isBlack (n) != (pass == 1)) continue;
            auto k = keyBounds (n);
            const bool on = shown[(size_t) n] || n == mouseNote;
            if (pass == 0)
            {
                k = k.reduced (0.6f, 0.0f);
                g.setGradientFill (juce::ColourGradient (on ? th.accent.brighter (0.2f) : juce::Colour (0xffe4e7eb), 0.0f, k.getY(),
                                                         on ? th.accent : juce::Colour (0xffb9bec6), 0.0f, k.getBottom(), false));
                g.fillRoundedRectangle (k.withTrimmedTop (-3.0f), 3.0f);
                if (n % 12 == 0)
                {
                    g.setColour (on ? th.background : juce::Colour (0xff6b727c));
                    g.setFont (Type::small());
                    g.drawText ("C" + juce::String (n / 12 - 1), k.withTrimmedBottom (3.0f), juce::Justification::centredBottom);
                }
            }
            else
            {
                juce::Path p;
                p.addRoundedRectangle (k.getX(), k.getY() - 3.0f, k.getWidth(), k.getHeight() + 3.0f, 2.5f);
                juce::DropShadow (juce::Colours::black.withAlpha (0.5f), 4, { 1, 2 }).drawForPath (g, p);
                g.setGradientFill (juce::ColourGradient (on ? th.accent : juce::Colour (0xff3a404a), 0.0f, k.getY(),
                                                         on ? th.accent.darker (0.3f) : juce::Colour (0xff16191d), 0.0f, k.getBottom(), false));
                g.fillPath (p);
            }
        }
}

} // namespace ap::ui
