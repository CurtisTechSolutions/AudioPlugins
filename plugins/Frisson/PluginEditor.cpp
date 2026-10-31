#include "PluginEditor.h"

namespace frisson
{

namespace
{
    constexpr const char* kKnobTitles[kNumKnobs] = { "WEIGHT", "WARMTH", "RUSH", "FOCUS", "SHINE", "SPACE", "GROOVE", "CHILLS", "LIFE" };
    constexpr const char* kKnobTaglines[kNumKnobs] = {
        "felt in the chest", "consonant, expensive", "dense, not louder",
        "clear, forward", "mastered sheen", "inside the music",
        "tighter pulse", "the rising moment", "breathes like hardware" };
}

//==============================================================================
RingKnob::RingKnob (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId, const juce::String& title,
                    const juce::String& tagline, std::atomic<float>* activitySource, bool large)
    : slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow),
      attachment (apvts, paramId, slider),
      name (title), line (tagline),
      activity (activitySource),
      big (large)
{
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 18);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    if (auto* p = apvts.getParameter (paramId))
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
    slider.setPopupDisplayEnabled (false, false, nullptr);
    slider.setMouseDragSensitivity (big ? 320 : 240);
    addAndMakeVisible (slider);
}

void RingKnob::tick()
{
    const float a = activity != nullptr ? juce::jlimit (0.0f, 1.0f, activity->load (std::memory_order_relaxed)) : 0.0f;
    // Rise instantly, fall at ~2/s so quick events stay visible.
    const float next = a > shown ? a : juce::jmax (a, shown - 0.07f);
    if (std::abs (next - shown) > 0.004f)
    {
        shown = next;
        repaint();
    }
}

juce::Rectangle<float> RingKnob::knobArea() const
{
    auto r = getLocalBounds().toFloat().reduced (10.0f);
    r.removeFromTop (big ? 30.0f : 34.0f);
    r.removeFromBottom (22.0f);
    const float size = juce::jmin (r.getWidth(), r.getHeight());
    return juce::Rectangle<float> (size, size).withCentre (r.getCentre());
}

void RingKnob::resized()
{
    // The knob sits inside the activity ring; the slider's value box hangs below it.
    const auto k = knobArea();
    const float inset = big ? 12.0f : 8.0f;
    slider.setBounds (juce::Rectangle<float> (k.getX() + inset, k.getY() + inset,
                                              k.getWidth() - 2.0f * inset, k.getHeight() - 2.0f * inset + 22.0f).toNearestInt());
}

void RingKnob::paint (juce::Graphics& g)
{
    const auto& th = ap::ui::themeOf (*this);
    ap::ui::drawCard (g, getLocalBounds().toFloat().reduced (1.0f), th, 10.0f);

    // Title + tagline.
    auto top = getLocalBounds().toFloat().reduced (12.0f, 9.0f);
    g.setColour (th.text);
    g.setFont (big ? ap::ui::Type::big (16.0f).withExtraKerningFactor (0.14f) : ap::ui::Type::section().withHeight (12.0f));
    g.drawText (name, top.removeFromTop (big ? 18.0f : 15.0f), juce::Justification::centred);
    g.setColour (th.textFaint);
    g.setFont (ap::ui::Type::small());
    g.drawText (line, top.removeFromTop (13.0f), juce::Justification::centred);

    // Activity ring around the knob.
    const auto ring = knobArea().reduced (2.0f);
    const float start = juce::MathConstants<float>::pi * 1.2f, end = juce::MathConstants<float>::pi * 2.8f;
    juce::Path track;
    track.addCentredArc (ring.getCentreX(), ring.getCentreY(), ring.getWidth() * 0.5f, ring.getHeight() * 0.5f, 0.0f, start, end, true);
    g.setColour (th.panelEdge.brighter (0.1f));
    g.strokePath (track, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (shown > 0.005f)
    {
        juce::Path arc;
        arc.addCentredArc (ring.getCentreX(), ring.getCentreY(), ring.getWidth() * 0.5f, ring.getHeight() * 0.5f, 0.0f,
                           start, start + shown * (end - start), true);
        g.setColour (th.accent.withAlpha (0.12f + 0.2f * shown));
        g.strokePath (arc, juce::PathStrokeType (9.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (th.accent.withAlpha (0.45f + 0.55f * shown));
        g.strokePath (arc, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Value pill.
    const auto sb = slider.getBounds().toFloat();
    const auto pill = juce::Rectangle<float> (60.0f, 16.0f).withCentre ({ sb.getCentreX(), sb.getBottom() - 9.0f });
    const bool hot = slider.isMouseOverOrDragging();
    g.setColour (hot ? th.accent.withAlpha (0.14f) : th.well.withAlpha (0.8f));
    g.fillRoundedRectangle (pill, 8.0f);
    g.setColour (hot ? th.accent.withAlpha (0.5f) : th.wellEdge);
    g.drawRoundedRectangle (pill.reduced (0.5f), 8.0f, 1.0f);
}

//==============================================================================
MatchPanel::MatchPanel (Processor& p)
    : processor (p), attachment (p.apvts, ids::match, button), meter (p.outputPeak, -48.0f, 6.0f)
{
    button.setClickingTogglesState (true);
    button.setTooltip ("Hear the dry signal at the processed loudness, to judge the change and not the level");
    addAndMakeVisible (button);
    meter.driveExternally();
    addAndMakeVisible (meter);
}

void MatchPanel::tick()
{
    meter.tick();
    const float d = processor.loudnessDeltaDb.load (std::memory_order_relaxed);
    if (std::abs (d - delta) > 0.05f) { delta = d; repaint(); }
}

void MatchPanel::resized()
{
    auto r = getLocalBounds();
    meter.setBounds (r.removeFromBottom (10));
    r.removeFromBottom (8);
    button.setBounds (r.removeFromLeft (150).withSizeKeepingCentre (150, 40));
}

void MatchPanel::paint (juce::Graphics& g)
{
    const auto& th = ap::ui::themeOf (*this);
    auto r = getLocalBounds().toFloat();
    r.removeFromBottom (18.0f);
    r.removeFromLeft (166.0f);
    const juce::String sign = delta >= 0.0f ? "+" : "";
    g.setColour (th.text);
    g.setFont (ap::ui::Type::big (20.0f));
    g.drawText (sign + juce::String (delta, 1) + " dB", r.removeFromTop (r.getHeight() * 0.55f), juce::Justification::bottomLeft);
    g.setColour (th.textDim);
    g.setFont (ap::ui::Type::label());
    g.drawText (button.getToggleState() ? "hearing dry, level-matched" : "added by processing (short-term)", r, juce::Justification::topLeft);
}

//==============================================================================
Editor::Editor (Processor& p)
    : ap::ui::EditorBase (p, p.apvts, "Frisson", "Nine knobs, each named for what it does to the listener",
                          juce::Colour (0xffffb340), 1000, 700),
      processor (p),
      matchPanel (p)
{
    for (int k = 0; k < kNumKnobs; ++k)
    {
        knobs[(size_t) k] = std::make_unique<RingKnob> (p.apvts, knobParamId (k), kKnobTitles[k], kKnobTaglines[k], &p.activity[(size_t) k]);
        addContent (*knobs[(size_t) k]);
    }
    dose = std::make_unique<RingKnob> (p.apvts, ids::dose, "DOSE", "scales all nine together", nullptr, true);
    addContent (*dose);

    match = &addSection ("A / B");
    match->setCaption ("loudness-matched bypass");
    match->addComponent (&matchPanel, -1, 64);
    addAnimated (matchPanel);

    using ap::ui::ControlStyle;
    advanced = &addSection ("Advanced");
    advanced->add (ids::chillsSense, ControlStyle::SmallKnob, "Chills sens");
    advanced->add (ids::lifeRate, ControlStyle::SmallKnob, "Life rate");
    advanced->add (ids::spaceMonoHz, ControlStyle::SmallKnob, "Mono below");
    advanced->add (ids::spaceRoom, ControlStyle::SmallKnob, "Room");
    advanced->add (ids::rushCeiling, ControlStyle::SmallKnob, "Ceiling");
    advanced->add (ids::output, ControlStyle::SmallKnob, "Output");
    advanced->newRow();
    advanced->add (ids::groovePocket, ControlStyle::Switch, "Groove pocket");
    advanced->add (ids::rushAutoGain, ControlStyle::Switch, "Rush auto gain");

    finishSetup();
}

void Editor::layoutContent (juce::Rectangle<int> area)
{
    auto grid = area.removeFromLeft (3 * 160 + 2 * 10);
    const int cellH = (grid.getHeight() - 2 * 10) / 3;
    for (int k = 0; k < kNumKnobs; ++k)
        knobs[(size_t) k]->setBounds (grid.getX() + (k % 3) * 170, grid.getY() + (k / 3) * (cellH + 10), 160, cellH);

    area.removeFromLeft (gap);
    dose->setBounds (area.removeFromTop (282));
    area.removeFromTop (gap);
    match->setBounds (area.removeFromTop (114));
    area.removeFromTop (gap);
    advanced->setBounds (area);
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

    // A four-point spark.
    const auto c = badge.getCentre();
    const float R = badge.getWidth() * 0.32f, r = R * 0.28f;
    juce::Path star;
    for (int i = 0; i < 8; ++i)
    {
        const float a = (float) i * juce::MathConstants<float>::pi * 0.25f;
        const float rad = (i % 2 == 0) ? R : r;
        const juce::Point<float> pt (c.x + rad * std::sin (a), c.y - rad * std::cos (a));
        if (i == 0) star.startNewSubPath (pt); else star.lineTo (pt);
    }
    star.closeSubPath();
    g.setColour (th.background.withAlpha (0.85f));
    g.fillPath (star);
}

} // namespace frisson
