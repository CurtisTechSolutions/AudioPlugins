#include "ap_ui/Meters.h"

#include <ap/Math.h>
#include <ap/Scales.h>

namespace ap::ui
{

namespace
{
    juce::String noteText (int midi)
    {
        return juce::String (noteName (((midi % 12) + 12) % 12)) + juce::String (midi / 12 - 1);
    }

    /** Cuts thin gaps across a bar so it reads as LED segments. */
    void drawSegmentGaps (juce::Graphics& g, juce::Rectangle<float> r, bool vertical, juce::Colour gap)
    {
        g.setColour (gap);
        if (vertical)
            for (float y = r.getBottom() - 3.0f; y > r.getY(); y -= 3.0f)
                g.fillRect (r.getX(), y, r.getWidth(), 1.0f);
        else
            for (float x = r.getX() + 3.0f; x < r.getRight(); x += 3.0f)
                g.fillRect (x, r.getY(), 1.0f, r.getHeight());
    }
}

//==============================================================================
LevelMeter::LevelMeter (std::atomic<float>& source, float minDb_, float maxDb_)
    : src (source), minDb (minDb_), maxDb (maxDb_)
{
    startTimerHz (30);
}

LevelMeter::~LevelMeter() { stopTimer(); }

void LevelMeter::tick()
{
    const float db = gainToDb (src.load (std::memory_order_relaxed));
    displayed = db > displayed ? db : juce::jmax (db, displayed - 1.5f);     // ~45 dB/s fall
    if (db >= peakHold) { peakHold = db; peakAge = 0; }
    else if (++peakAge > 45) peakHold = juce::jmax (db, peakHold - 1.0f);
    if (marker != nullptr) markerDb = marker();
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    const auto& th = themeOf (*this);
    const auto r = getLocalBounds().toFloat();
    const bool vertical = r.getHeight() >= r.getWidth();
    drawWell (g, r, th, 3.0f);

    auto norm = [this] (float db) { return juce::jlimit (0.0f, 1.0f, (db - minDb) / (maxDb - minDb)); };
    const auto inner = r.reduced (2.0f);
    const float level = norm (displayed);

    juce::ColourGradient grad = vertical
        ? juce::ColourGradient (th.accent.withMultipliedBrightness (0.8f), 0.0f, inner.getBottom(), th.meterHigh, 0.0f, inner.getY(), false)
        : juce::ColourGradient (th.accent.withMultipliedBrightness (0.8f), inner.getX(), 0.0f, th.meterHigh, inner.getRight(), 0.0f, false);
    grad.addColour (norm (-6.0f), th.accent);
    grad.addColour (norm (-1.0f), juce::Colour (0xffffd60a));
    g.setGradientFill (grad);
    const auto fill = vertical ? inner.withTop (inner.getBottom() - inner.getHeight() * level)
                               : inner.withWidth (inner.getWidth() * level);
    g.fillRoundedRectangle (fill, 1.5f);
    drawSegmentGaps (g, inner, vertical, th.well);

    if (peakHold > minDb)
    {
        const float p = norm (peakHold);
        g.setColour (th.text.withAlpha (0.85f));
        if (vertical) g.fillRect (inner.getX(), inner.getBottom() - inner.getHeight() * p - 1.0f, inner.getWidth(), 1.5f);
        else          g.fillRect (inner.getX() + inner.getWidth() * p - 1.0f, inner.getY(), 1.5f, inner.getHeight());
    }

    if (marker != nullptr && markerDb > minDb && markerDb < maxDb)
    {
        const float m = norm (markerDb);
        g.setColour (juce::Colours::white);
        if (vertical)
        {
            const float y = inner.getBottom() - inner.getHeight() * m;
            g.fillRect (r.getX(), y - 0.75f, r.getWidth(), 1.5f);
        }
        else
        {
            const float x = inner.getX() + inner.getWidth() * m;
            g.fillRect (x - 0.75f, r.getY(), 1.5f, r.getHeight());
            juce::Path tri;
            tri.addTriangle (x - 3.5f, r.getY(), x + 3.5f, r.getY(), x, r.getY() + 4.0f);
            g.fillPath (tri);
        }
    }
}

//==============================================================================
GainReductionMeter::GainReductionMeter (std::atomic<float>& reductionDbSource, float rangeDb)
    : src (reductionDbSource), range (rangeDb)
{
    startTimerHz (30);
}

GainReductionMeter::~GainReductionMeter() { stopTimer(); }

void GainReductionMeter::tick()
{
    const float gr = juce::jmin (0.0f, src.load (std::memory_order_relaxed));
    displayed = gr < displayed ? gr : juce::jmin (0.0f, displayed + 1.0f);
    repaint();
}

void GainReductionMeter::paint (juce::Graphics& g)
{
    const auto& th = themeOf (*this);
    auto r = getLocalBounds().toFloat();
    auto label = r.removeFromTop (juce::jmin (16.0f, r.getHeight() * 0.45f));
    drawCaps (g, "Gain reduction", label, th.textDim, juce::Justification::centredLeft, Type::small().boldened());
    g.setColour (displayed < -0.05f ? th.text : th.textDim);
    g.setFont (Type::value());
    g.drawText (juce::String (displayed, 1) + " dB", label, juce::Justification::centredRight);

    r.removeFromTop (3.0f);
    drawWell (g, r, th, 3.0f);
    const auto inner = r.reduced (2.0f);
    const float amount = juce::jlimit (0.0f, 1.0f, -displayed / range);
    const auto fill = inner.withLeft (inner.getRight() - inner.getWidth() * amount);
    g.setGradientFill (juce::ColourGradient (th.accent, fill.getRight(), 0.0f, th.accent.withRotatedHue (-0.08f), inner.getX(), 0.0f, false));
    g.fillRoundedRectangle (fill, 1.5f);
    drawSegmentGaps (g, inner, false, th.well);

    g.setColour (th.textFaint);
    for (int db = 6; db < (int) range; db += 6)
    {
        const float x = inner.getRight() - inner.getWidth() * ((float) db / range);
        g.fillRect (x, inner.getBottom() - 3.0f, 1.0f, 3.0f);
    }
}

//==============================================================================
PitchReadout::PitchReadout (std::atomic<float>& hzSource, std::atomic<float>* targetSource)
    : hz (hzSource), target (targetSource)
{
    startTimerHz (30);
}

PitchReadout::~PitchReadout() { stopTimer(); }

void PitchReadout::tick()
{
    const float h = hz.load (std::memory_order_relaxed);
    const float t = target != nullptr ? target->load (std::memory_order_relaxed) : 0.0f;
    if (h > 0.0f)
    {
        shownHz = h;
        shownTarget = t;
        holdFrames = 10;
        const float midi = hzToMidi (h);
        const float cents = 100.0f * (midi - std::round (midi));
        needle += 0.5f * (cents - needle);
    }
    else if (holdFrames > 0 && --holdFrames == 0)
    {
        shownHz = 0.0f;
        shownTarget = 0.0f;
    }
    repaint();
}

void PitchReadout::paint (juce::Graphics& g)
{
    const auto& th = themeOf (*this);
    auto r = getLocalBounds().toFloat();
    drawWell (g, r, th, 8.0f);
    r = r.reduced (10.0f, 6.0f);

    const bool live = hz.load (std::memory_order_relaxed) > 0.0f;
    juce::String note = "--", sub = "no pitch";
    if (shownHz > 0.0f)
    {
        const int n = (int) std::lround (hzToMidi (shownHz));
        note = noteText (n);
        sub = juce::String (shownHz, 1) + " Hz";
        if (target != nullptr && shownTarget > 0.0f)
            sub = juce::String (juce::roundToInt (shownHz)) + " Hz  " + juce::String::fromUTF8 ("\xe2\x86\x92") + "  "
                  + noteText ((int) std::lround (hzToMidi (shownTarget)));
    }

    auto needleArea = r.removeFromBottom (juce::jmin (12.0f, r.getHeight() * 0.25f));
    auto noteArea = r.removeFromLeft (r.getWidth() * 0.46f);
    g.setColour (shownHz > 0.0f ? (live ? th.text : th.textDim) : th.textFaint);
    g.setFont (Type::big (juce::jmin (30.0f, noteArea.getHeight() * 0.95f)));
    g.drawText (note, noteArea, juce::Justification::centredLeft);

    const int cents = (int) std::lround (needle);
    g.setColour (shownHz > 0.0f ? (std::abs (cents) <= 10 ? th.accent : th.text) : th.textFaint);
    g.setFont (Type::big (15.0f));
    g.drawText (shownHz > 0.0f ? ((cents >= 0 ? "+" : "") + juce::String (cents) + " ct") : juce::String(),
                r.removeFromTop (r.getHeight() * 0.55f), juce::Justification::bottomRight);
    g.setColour (th.textDim);
    g.setFont (Type::value());
    g.drawText (sub, r, juce::Justification::topRight);

    // Tuning needle: -50..+50 cents.
    needleArea = needleArea.withTrimmedTop (4.0f);
    const float cy = needleArea.getCentreY();
    g.setColour (th.knobTrack);
    g.fillRoundedRectangle (needleArea.withHeight (3.0f).withCentre ({ needleArea.getCentreX(), cy }), 1.5f);
    g.setColour (th.textFaint);
    g.fillRect (needleArea.getCentreX() - 0.5f, needleArea.getY(), 1.0f, needleArea.getHeight());
    if (shownHz > 0.0f)
    {
        const float x = needleArea.getCentreX() + juce::jlimit (-1.0f, 1.0f, needle / 50.0f) * needleArea.getWidth() * 0.5f;
        const auto c = std::abs (needle) <= 10.0f ? th.accent : (std::abs (needle) <= 25.0f ? juce::Colour (0xffffd60a) : th.meterHigh);
        g.setColour (c.withAlpha (0.3f));
        g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ x, cy }));
        g.setColour (c);
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ x, cy }));
    }
}

//==============================================================================
ActivityLed::ActivityLed (std::atomic<juce::uint32>& counter, juce::String label)
    : source (counter), text (std::move (label))
{
    last = source.load (std::memory_order_relaxed);
}

void ActivityLed::tick()
{
    const auto now = source.load (std::memory_order_relaxed);
    const float before = glow;
    if (now != last) { last = now; glow = 1.0f; }
    else             glow = juce::jmax (0.0f, glow - 0.12f);
    if (! juce::exactlyEqual (glow, before)) repaint();
}

void ActivityLed::paint (juce::Graphics& g)
{
    const auto& th = themeOf (*this);
    auto r = getLocalBounds().toFloat();
    const auto dot = r.removeFromLeft (14.0f).withSizeKeepingCentre (8.0f, 8.0f);
    if (glow > 0.0f)
    {
        g.setColour (th.accent.withAlpha (0.35f * glow));
        g.fillEllipse (dot.expanded (4.0f * glow));
    }
    g.setColour (th.knobTrack.interpolatedWith (th.accent, glow));
    g.fillEllipse (dot);
    drawCaps (g, text, r.withTrimmedLeft (4.0f), glow > 0.3f ? th.text : th.textDim, juce::Justification::centredLeft,
              Type::small().boldened().withExtraKerningFactor (0.1f));
}

} // namespace ap::ui
