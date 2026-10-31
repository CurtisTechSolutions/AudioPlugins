#include "PluginEditor.h"

#include <ap/Dynamics.h>
#include <ap/Tempo.h>

namespace ducker
{

namespace
{
    bool isGhost (juce::AudioProcessorValueTreeState& apvts)
    {
        return (int) apvts.getRawParameterValue (ids::mode)->load() == (int) Mode::Ghost;
    }

    ap::GainComputer gainComputer (juce::AudioProcessorValueTreeState& apvts)
    {
        ap::GainComputer gc;
        gc.thresholdDb = apvts.getRawParameterValue (ids::threshold)->load();
        gc.ratio = apvts.getRawParameterValue (ids::ratio)->load();
        gc.kneeDb = apvts.getRawParameterValue (ids::knee)->load();
        return gc;
    }

    void dashedLine (juce::Graphics& g, float x0, float y, float x1, juce::Colour c)
    {
        g.setColour (c);
        for (float x = x0; x < x1; x += 7.0f)
            g.fillRect (x, y - 0.5f, juce::jmin (4.0f, x1 - x), 1.0f);
    }
}

//==============================================================================
DuckHistory::DuckHistory (Processor& p) : ap::ui::Display ("Input  /  gain reduction"), processor (p) {}

void DuckHistory::tick()
{
    input.push (ap::gainToDb (processor.inputHold.exchange (0.0f, std::memory_order_relaxed)));
    reduction.push (processor.grHoldDb.exchange (0.0f, std::memory_order_relaxed));
    repaint();
}

void DuckHistory::paintDisplay (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& th = ap::ui::themeOf (*this);
    area = area.reduced (8.0f, 6.0f).withTrimmedTop (14.0f);
    const auto plot = area.withTrimmedRight (28.0f);

    // Level scale: -60..0 dB bottom to top. Reduction scale: 0 dB at the top, -24 dB at the bottom.
    auto yLevel = [&] (float db) { return plot.getBottom() - plot.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f); };
    auto yGr    = [&] (float db) { return plot.getY() + plot.getHeight() * juce::jlimit (0.0f, 1.0f, -db / 24.0f); };

    g.setFont (ap::ui::Type::small());
    for (int db = 6; db <= 18; db += 6)
    {
        gridLine (g, th, plot.getX(), yGr ((float) -db), plot.getRight(), yGr ((float) -db));
        g.setColour (th.textFaint);
        g.drawText ("-" + juce::String (db), juce::Rectangle<float> (26.0f, 12.0f).withPosition (plot.getRight() + 3.0f, yGr ((float) -db) - 6.0f),
                    juce::Justification::centredLeft);
    }

    const size_t n = input.size();
    auto xOf = [&] (size_t i) { return plot.getX() + plot.getWidth() * (float) i / (float) (n - 1); };

    juce::Path level;
    level.startNewSubPath (plot.getX(), plot.getBottom());
    for (size_t i = 0; i < n; ++i) level.lineTo (xOf (i), yLevel (input[i]));
    level.lineTo (plot.getRight(), plot.getBottom());
    level.closeSubPath();
    g.setGradientFill (juce::ColourGradient (th.textDim.withAlpha (0.35f), 0.0f, plot.getY(), th.textDim.withAlpha (0.05f), 0.0f, plot.getBottom(), false));
    g.fillPath (level);

    if (! isGhost (processor.apvts))
    {
        const float thr = processor.apvts.getRawParameterValue (ids::threshold)->load();
        dashedLine (g, plot.getX(), yLevel (thr), plot.getRight(), th.text.withAlpha (0.55f));
        g.setColour (th.textDim);
        g.drawText ("threshold", juce::Rectangle<float> (70.0f, 12.0f).withPosition (plot.getX() + 4.0f, yLevel (thr) - 13.0f),
                    juce::Justification::centredLeft);
    }

    juce::Path gr, grLine;
    gr.startNewSubPath (plot.getX(), plot.getY());
    for (size_t i = 0; i < n; ++i)
    {
        const juce::Point<float> pt (xOf (i), yGr (reduction[i]));
        gr.lineTo (pt);
        if (i == 0) grLine.startNewSubPath (pt); else grLine.lineTo (pt);
    }
    gr.lineTo (plot.getRight(), plot.getY());
    gr.closeSubPath();
    g.setGradientFill (juce::ColourGradient (th.accent.withAlpha (0.45f), 0.0f, plot.getY(), th.accent.withAlpha (0.08f), 0.0f, plot.getBottom(), false));
    g.fillPath (gr);
    g.setColour (th.accent);
    g.strokePath (grLine, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

//==============================================================================
TransferCurve::TransferCurve (Processor& p) : ap::ui::Display ("Transfer"), processor (p) {}

void TransferCurve::tick()
{
    const float sc = processor.sidechainLevel.load (std::memory_order_relaxed);
    const float db = ap::gainToDb (sc);
    levelDb = db > levelDb ? db : juce::jmax (db, levelDb - 1.5f);
    setAlpha (isGhost (processor.apvts) ? 0.35f : 1.0f);
    repaint();
}

void TransferCurve::paintDisplay (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& th = ap::ui::themeOf (*this);
    const auto plot = area.reduced (10.0f, 8.0f).withTrimmedTop (12.0f);
    const auto gc = gainComputer (processor.apvts);
    auto x = [&] (float db) { return plot.getX() + plot.getWidth() * (db + 60.0f) / 60.0f; };
    auto y = [&] (float db) { return plot.getBottom() - plot.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f); };

    for (int db = -48; db < 0; db += 12)
    {
        gridLine (g, th, x ((float) db), plot.getY(), x ((float) db), plot.getBottom());
        gridLine (g, th, plot.getX(), y ((float) db), plot.getRight(), y ((float) db));
    }
    g.setColour (th.grid.brighter (0.3f));
    g.drawLine (plot.getX(), plot.getBottom(), plot.getRight(), plot.getY(), 1.0f);   // unity

    juce::Path curve;
    for (int i = 0; i <= 120; ++i)
    {
        const float in = -60.0f + 60.0f * (float) i / 120.0f;
        const float out = in + gc.gainReductionDb (in);
        if (i == 0) curve.startNewSubPath (x (in), y (out)); else curve.lineTo (x (in), y (out));
    }
    g.setColour (th.accent.withAlpha (0.25f));
    g.strokePath (curve, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (th.accent);
    g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (levelDb > -60.0f)
    {
        const float in = juce::jmin (0.0f, levelDb);
        const juce::Point<float> pt (x (in), y (in + gc.gainReductionDb (in)));
        g.setColour (th.text.withAlpha (0.3f));
        g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (pt));
        g.setColour (th.text);
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (pt));
    }
}

//==============================================================================
GhostShape::GhostShape (Processor& p) : ap::ui::Display ("One division"), processor (p) {}

void GhostShape::paintDisplay (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& th = ap::ui::themeOf (*this);
    auto& apvts = processor.apvts;
    const auto plot = area.reduced (10.0f, 8.0f).withTrimmedTop (12.0f);

    ap::GhostDuckEnvelope env;
    ap::GhostDuckEnvelope::Shape shape;
    shape.attackMs = apvts.getRawParameterValue (ids::ghostAttack)->load();
    shape.holdMs = apvts.getRawParameterValue (ids::ghostHold)->load();
    shape.releaseMs = apvts.getRawParameterValue (ids::ghostRelease)->load();
    shape.depthDb = apvts.getRawParameterValue (ids::ghostDepth)->load();
    shape.curve = apvts.getRawParameterValue (ids::ghostCurve)->load() * 0.01f;
    env.setShape (shape);

    const auto division = (ap::NoteDivision) (int) apvts.getRawParameterValue (ids::ghostDiv)->load();
    const float bpm = juce::jmax (1.0f, processor.bpmInUse.load (std::memory_order_relaxed));
    const double msPerDivision = ap::noteDivisionBeats (division) * 60000.0 / bpm;
    const float floorDb = -juce::jmax (12.0f, shape.depthDb);
    auto y = [&] (float db) { return plot.getY() + plot.getHeight() * juce::jlimit (0.0f, 1.0f, db / floorDb); };

    for (int q = 1; q < 4; ++q)
        gridLine (g, th, plot.getX() + plot.getWidth() * (float) q / 4.0f, plot.getY(), plot.getX() + plot.getWidth() * (float) q / 4.0f, plot.getBottom());

    juce::Path p, fill;
    fill.startNewSubPath (plot.getX(), plot.getY());
    for (int i = 0; i <= 200; ++i)
    {
        const float t = (float) i / 200.0f;
        const float db = ap::gainToDb (env.gainAt ((double) t * msPerDivision));
        const juce::Point<float> pt (plot.getX() + t * plot.getWidth(), y (db));
        if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
        fill.lineTo (pt);
    }
    fill.lineTo (plot.getRight(), plot.getY());
    fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (th.accent.withAlpha (0.4f), 0.0f, plot.getY(), th.accent.withAlpha (0.05f), 0.0f, plot.getBottom(), false));
    g.fillPath (fill);
    g.setColour (th.accent);
    g.strokePath (p, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Playhead.
    if (isGhost (apvts))
    {
        const float ph = processor.ghostPhase.load (std::memory_order_relaxed);
        const float px = plot.getX() + ph * plot.getWidth();
        g.setColour (th.text.withAlpha (0.6f));
        g.fillRect (px, plot.getY(), 1.0f, plot.getHeight());
        const float db = ap::gainToDb (env.gainAt ((double) ph * msPerDivision));
        g.setColour (th.text);
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ px, y (db) }));
    }

    g.setColour (th.textFaint);
    g.setFont (ap::ui::Type::small());
    g.drawText (juce::String (ap::noteDivisionName (division)) + " at " + juce::String (juce::roundToInt (bpm)) + " bpm",
                area.reduced (8.0f, 5.0f).removeFromTop (12.0f), juce::Justification::topRight);
}

//==============================================================================
void Editor::SidechainCaption::tick()
{
    const int now = processor->sidechainConnected.load (std::memory_order_relaxed) ? 1 : 0;
    if (now != shown)
    {
        shown = now;
        section->setCaption (now != 0 ? "sidechain input connected" : "no sidechain routed (host)");
    }
}

Editor::Editor (Processor& p)
    : ap::ui::EditorBase (p, p.apvts, "Ducker", "Sidechain compressor + ghost ducking", juce::Colour (0xff5ac8fa), 1000, 660),
      processor (p),
      modeControl (p.apvts, ids::mode, ap::ui::ControlStyle::Segmented),
      grMeter (p.gainReductionDb),
      history (p),
      transfer (p),
      ghostShape (p)
{
    using ap::ui::ControlStyle;
    addHeaderComponent (grMeter, 200, 32);
    addHeaderComponent (modeControl, juce::jmax (330, modeControl.preferredWidth()), modeControl.preferredHeight());

    addContent (history);
    addContent (transfer);

    detector = &addSection ("Detector");
    for (auto [id, label] : { std::pair { ids::threshold, "Threshold" }, { ids::ratio, "Ratio" }, { ids::knee, "Knee" },
                              { ids::attack, "Attack" }, { ids::release, "Release" }, { ids::lookahead, "Lookahead" },
                              { ids::scHpf, "SC low cut" } })
        detector->add (id, ControlStyle::SmallKnob, label);
    detector->add (ids::listen, ControlStyle::Switch, "Listen");
    detector->setActiveWhen (ids::mode, [] (float m) { return juce::roundToInt (m) != (int) Mode::Ghost; });
    scCaption.processor = &p;
    scCaption.section = detector;
    addAnimated (scCaption);

    output = &addSection ("Output");
    output->add (ids::makeup, ControlStyle::SmallKnob, "Makeup");
    output->add (ids::mix, ControlStyle::SmallKnob, "Mix");

    ghost = &addSection ("Ghost ducking");
    ghost->setCaption ("tempo-synced, no sidechain needed");
    ghost->addComponent (&ghostShape, 300, 118);
    addAnimated (ghostShape);
    ghost->add (ids::ghostDiv, ControlStyle::Combo, "Division");
    for (auto [id, label] : { std::pair { ids::ghostDepth, "Depth" }, { ids::ghostAttack, "Attack" }, { ids::ghostHold, "Hold" },
                              { ids::ghostRelease, "Release" }, { ids::ghostCurve, "Curve" }, { ids::ghostOffset, "Offset" },
                              { ids::ghostBpm, "Free BPM" } })
        ghost->add (id, ControlStyle::SmallKnob, label);
    ghost->setActiveWhen (ids::mode, [] (float m) { return juce::roundToInt (m) == (int) Mode::Ghost; });

    finishSetup();
}

void Editor::layoutContent (juce::Rectangle<int> area)
{
    auto row = area.removeFromTop (236);
    transfer.setBounds (row.removeFromRight (236));
    row.removeFromRight (gap);
    history.setBounds (row);
    area.removeFromTop (gap);

    row = area.removeFromTop (128);
    output->setBounds (row.removeFromRight (176));
    row.removeFromRight (gap);
    detector->setBounds (row);
    area.removeFromTop (gap);

    ghost->setBounds (area);
}

void Editor::paintLogo (juce::Graphics& g, juce::Rectangle<float> badge)
{
    ap::ui::EditorBase::paintLogo (g, badge);
    const auto& th = lookAndFeel.theme();
    juce::Path shape;
    shape.addRoundedRectangle (badge, 9.0f);
    g.setGradientFill (juce::ColourGradient (th.accent.brighter (0.25f), badge.getX(), badge.getY(),
                                             th.accent.darker (0.35f), badge.getRight(), badge.getBottom(), false));
    g.fillPath (shape);

    // Two ducks of a pumping gain curve.
    const auto r = badge.reduced (7.0f, 10.0f);
    juce::Path p;
    p.startNewSubPath (r.getX(), r.getY());
    for (int k = 0; k < 2; ++k)
    {
        const float x0 = r.getX() + (float) k * r.getWidth() * 0.5f, w = r.getWidth() * 0.5f;
        p.lineTo (x0 + w * 0.08f, r.getBottom());
        p.quadraticTo (x0 + w * 0.45f, r.getBottom(), x0 + w, r.getY());
    }
    g.setColour (th.background.withAlpha (0.85f));
    g.strokePath (p, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

} // namespace ducker
