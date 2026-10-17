#include "ap_ui/Meters.h"

#include <ap/Math.h>
#include <ap/Scales.h>

namespace ap::ui
{

namespace
{
    const Theme& themeOf (juce::Component& c)
    {
        static const Theme fallback;
        if (auto* lf = dynamic_cast<LookAndFeel*> (&c.getLookAndFeel())) return lf->theme();
        return fallback;
    }
}

//==============================================================================
LevelMeter::LevelMeter (std::atomic<float>& source, float minDb_, float maxDb_)
    : src (source), minDb (minDb_), maxDb (maxDb_)
{
    startTimerHz (30);
}

LevelMeter::~LevelMeter() { stopTimer(); }

void LevelMeter::timerCallback()
{
    const float db = gainToDb (src.load (std::memory_order_relaxed));
    displayed = db > displayed ? db : displayed - 1.5f;     // ~45 dB/s fall
    if (db >= peakHold) { peakHold = db; peakAge = 0; }
    else if (++peakAge > 45) peakHold = juce::jmax (db, peakHold - 1.0f);
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    const auto& th = themeOf (*this);
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (th.knobTrack);
    g.fillRoundedRectangle (r, 3.0f);

    auto norm = [this] (float db) { return juce::jlimit (0.0f, 1.0f, (db - minDb) / (maxDb - minDb)); };

    const float level = norm (displayed);
    auto fill = r.withTop (r.getBottom() - r.getHeight() * level);
    juce::ColourGradient grad (th.meterLow, r.getX(), r.getBottom(), th.meterHigh, r.getX(), r.getY(), false);
    grad.addColour (0.75, th.accent);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (fill, 3.0f);

    const float peakY = r.getBottom() - r.getHeight() * norm (peakHold);
    g.setColour (th.text);
    g.fillRect (r.getX(), peakY - 1.0f, r.getWidth(), 2.0f);

    g.setColour (th.panelEdge);
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
}

//==============================================================================
GainReductionMeter::GainReductionMeter (std::atomic<float>& reductionDbSource, float rangeDb)
    : src (reductionDbSource), range (rangeDb)
{
    startTimerHz (30);
}

GainReductionMeter::~GainReductionMeter() { stopTimer(); }

void GainReductionMeter::timerCallback()
{
    const float gr = juce::jmin (0.0f, src.load (std::memory_order_relaxed));
    displayed = gr < displayed ? gr : juce::jmin (0.0f, displayed + 1.0f);
    repaint();
}

void GainReductionMeter::paint (juce::Graphics& g)
{
    const auto& th = themeOf (*this);
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const auto label = r.removeFromTop (14.0f);
    g.setColour (th.textDim);
    g.setFont (LookAndFeel::valueFont());
    g.drawText ("GR " + juce::String (displayed, 1) + " dB", label, juce::Justification::centredLeft);

    g.setColour (th.knobTrack);
    g.fillRoundedRectangle (r, 3.0f);

    const float amount = juce::jlimit (0.0f, 1.0f, -displayed / range);
    auto fill = r.withLeft (r.getRight() - r.getWidth() * amount);
    g.setColour (th.accent);
    g.fillRoundedRectangle (fill, 3.0f);

    g.setColour (th.panelEdge);
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
    for (int db = 6; db < (int) range; db += 6)
    {
        const float x = r.getRight() - r.getWidth() * ((float) db / range);
        g.fillRect (x, r.getY(), 1.0f, r.getHeight());
    }
}

//==============================================================================
PitchReadout::PitchReadout (std::atomic<float>& hzSource, std::atomic<float>* targetSource)
    : hz (hzSource), target (targetSource)
{
    startTimerHz (20);
}

PitchReadout::~PitchReadout() { stopTimer(); }

void PitchReadout::timerCallback()
{
    const float h = hz.load (std::memory_order_relaxed);
    const float t = target != nullptr ? target->load (std::memory_order_relaxed) : 0.0f;
    if (! juce::exactlyEqual (h, shownHz) || ! juce::exactlyEqual (t, shownTarget))
    {
        shownHz = h;
        shownTarget = t;
        repaint();
    }
}

void PitchReadout::paint (juce::Graphics& g)
{
    const auto& th = themeOf (*this);
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (th.knobTrack);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (th.panelEdge);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);

    juce::String note = "--", cents = "", hzText = "no pitch";
    if (shownHz > 0.0f)
    {
        const float midi = hzToMidi (shownHz);
        const int   n    = (int) std::lround (midi);
        const int   c    = (int) std::lround (100.0f * (midi - (float) n));
        note   = juce::String (noteName (n % 12)) + juce::String (n / 12 - 1);
        cents  = (c >= 0 ? "+" : "") + juce::String (c) + " ct";
        hzText = juce::String (shownHz, 1) + " Hz";
        if (target != nullptr && shownTarget > 0.0f)
        {
            const int tn = (int) std::lround (hzToMidi (shownTarget));
            hzText += "  → " + juce::String (noteName (tn % 12)) + juce::String (tn / 12 - 1);
        }
    }

    auto top = r.removeFromTop (r.getHeight() * 0.55f);
    g.setColour (th.text);
    g.setFont (juce::Font (juce::FontOptions (juce::jmin (30.0f, top.getHeight() * 0.9f), juce::Font::bold)));
    g.drawText (note, top.reduced (6.0f, 0.0f), juce::Justification::centredLeft);
    g.setColour (shownHz > 0.0f ? th.accent : th.textDim);
    g.setFont (LookAndFeel::labelFont());
    g.drawText (cents, top.reduced (6.0f, 0.0f), juce::Justification::centredRight);
    g.setColour (th.textDim);
    g.drawText (hzText, r.reduced (6.0f, 0.0f), juce::Justification::centredLeft);
}

} // namespace ap::ui
