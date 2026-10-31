#include "PluginEditor.h"

namespace frisson
{

namespace
{
    constexpr int kCell = 150;        // grid cell size
    constexpr int kRightColumn = 200;
    constexpr int kAdvancedToggleHeight = 26;
    constexpr const char* kKnobTitles[kNumKnobs] = { "WEIGHT", "WARMTH", "RUSH", "FOCUS", "SHINE", "SPACE", "GROOVE", "CHILLS", "LIFE" };
    constexpr const char* kKnobTaglines[kNumKnobs] = {
        "felt in the chest", "consonant, expensive", "dense, not louder",
        "clear, forward", "mastered sheen", "inside the music",
        "tighter pulse", "the rising moment", "breathes like hardware" };

    const ap::ui::Theme& themeOf (juce::Component& c)
    {
        static const ap::ui::Theme fallback;
        if (auto* lf = dynamic_cast<ap::ui::LookAndFeel*> (&c.getLookAndFeel())) return lf->theme();
        return fallback;
    }
}

//==============================================================================
RingKnob::RingKnob (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId,
                    const juce::String& title, std::atomic<float>* activitySource, bool large)
    : slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow),
      attachment (apvts, paramId, slider),
      name (title),
      activity (activitySource),
      big (large)
{
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, large ? 70 : 56, 16);
    slider.setDoubleClickReturnValue (true, large ? 40.0 : 0.0);
    slider.setPopupDisplayEnabled (false, false, nullptr);
    addAndMakeVisible (slider);
    startTimerHz (30);
}

RingKnob::~RingKnob() { stopTimer(); }

void RingKnob::timerCallback()
{
    const float a = activity != nullptr ? juce::jlimit (0.0f, 1.0f, activity->load (std::memory_order_relaxed)) : 0.0f;
    // Rise instantly, fall at ~2/s so quick events stay visible.
    const float next = a > shown ? a : juce::jmax (a, shown - 0.07f);
    if (std::fabs (next - shown) > 0.004f)
    {
        shown = next;
        repaint();
    }
}

void RingKnob::paint (juce::Graphics& g)
{
    const auto& th = themeOf (*this);
    auto r = getLocalBounds().toFloat();
    r.removeFromTop (big ? 22.0f : 18.0f);
    r.removeFromBottom (18.0f);
    const float size = juce::jmin (r.getWidth(), r.getHeight());
    const auto ring = juce::Rectangle<float> (size, size).withCentre (r.getCentre()).reduced (2.0f);

    // Activity ring: a dim track plus an arc that grows with activity, in the accent colour.
    g.setColour (th.panelEdge);
    g.drawEllipse (ring, 2.0f);
    if (shown > 0.005f)
    {
        juce::Path arc;
        const float start = -juce::MathConstants<float>::pi * 0.75f;
        const float end = start + shown * juce::MathConstants<float>::pi * 1.5f;
        arc.addCentredArc (ring.getCentreX(), ring.getCentreY(), ring.getWidth() * 0.5f, ring.getHeight() * 0.5f, 0.0f, start, end, true);
        g.setColour (th.accent.withAlpha (0.35f + 0.65f * shown));
        g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    g.setColour (th.text);
    g.setFont (big ? juce::Font (juce::FontOptions (15.0f, juce::Font::bold)) : ap::ui::LookAndFeel::sectionFont());
    g.drawText (name, getLocalBounds().removeFromTop (big ? 22 : 18), juce::Justification::centred);
}

void RingKnob::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (big ? 22 : 18);
    slider.setBounds (r.reduced (big ? 10 : 12, 2));
}

//==============================================================================
MatchPanel::MatchPanel (Processor& p)
    : processor (p), attachment (p.apvts, ids::match, button)
{
    button.setClickingTogglesState (true);
    addAndMakeVisible (button);
    startTimerHz (10);
}

MatchPanel::~MatchPanel() { stopTimer(); }

void MatchPanel::timerCallback()
{
    const float d = processor.loudnessDeltaDb.load (std::memory_order_relaxed);
    if (std::fabs (d - delta) > 0.05f) { delta = d; repaint(); }
}

void MatchPanel::resized()
{
    auto r = getLocalBounds();
    button.setBounds (r.removeFromTop (32).reduced (4, 0));
}

void MatchPanel::paint (juce::Graphics& g)
{
    const auto& th = themeOf (*this);
    auto r = getLocalBounds();
    r.removeFromTop (36);
    g.setColour (th.textDim);
    g.setFont (ap::ui::LookAndFeel::labelFont());
    const juce::String sign = delta >= 0.0f ? "+" : "";
    g.drawFittedText ("processing adds " + sign + juce::String (delta, 1) + " dB\n"
                      + (button.getToggleState() ? "hearing dry, level-matched" : "hearing processed"),
                      r, juce::Justification::centredTop, 2);
}

//==============================================================================
Editor::Editor (Processor& p)
    : ap::ui::EditorBase (p, p.apvts, "Frisson", "Nine knobs, each named for what it does to the listener",
                          juce::Colour (0xffffb340), 3 * kCell + kRightColumn + 3 * margin, 1),
      processor (p),
      matchPanel (p),
      meter (p.outputPeak)
{
    addPresetBar (p.programs);
    for (int k = 0; k < kNumKnobs; ++k)
    {
        knobs[(size_t) k] = std::make_unique<RingKnob> (p.apvts, knobParamId (k), kKnobTitles[k], &p.activity[(size_t) k]);
        knobs[(size_t) k]->setTooltip (kKnobTaglines[k]);
        addAndMakeVisible (*knobs[(size_t) k]);
    }
    dose = std::make_unique<RingKnob> (p.apvts, ids::dose, "DOSE", nullptr, true);
    addAndMakeVisible (*dose);
    addAndMakeVisible (matchPanel);
    setHeaderComponent (&meter, 14, 40);

    advancedToggle.setClickingTogglesState (true);
    advancedToggle.onClick = [this] { showAdvanced = advancedToggle.getToggleState(); updateAdvancedVisibility(); };
    addAndMakeVisible (advancedToggle);

    advanced = std::make_unique<ap::ui::ParamSection> (p.apvts, "Advanced",
        juce::StringArray { ids::chillsSense, ids::groovePocket, ids::lifeRate, ids::spaceMonoHz, ids::spaceRoom,
                            ids::rushCeiling, ids::rushAutoGain, ids::output });
    advanced->setLookAndFeel (&lookAndFeel);
    addChildComponent (*advanced);

    updateAdvancedVisibility();
}

void Editor::updateAdvancedVisibility()
{
    advanced->setVisible (showAdvanced);
    const int w = getWidth() > 0 ? getWidth() : 3 * kCell + kRightColumn + 3 * margin;
    const int gridH = 3 * kCell;
    int h = contentTop() + gridH + margin + kAdvancedToggleHeight + margin;
    if (showAdvanced) h += advanced->preferredHeight (w - 2 * margin) + margin;
    setSize (w, h);
}

void Editor::paint (juce::Graphics& g)
{
    ap::ui::EditorBase::paint (g);
    const auto& th = lookAndFeel.theme();
    // Panel behind the grid and the right column.
    auto area = getLocalBounds().withTrimmedTop (contentTop()).reduced (margin, 0);
    const auto grid = area.removeFromLeft (3 * kCell);
    g.setColour (th.panel);
    g.fillRoundedRectangle (grid.withHeight (3 * kCell).toFloat(), 8.0f);
    g.setColour (th.panelEdge);
    g.drawRoundedRectangle (grid.withHeight (3 * kCell).toFloat().reduced (0.5f), 8.0f, 1.0f);
    area.removeFromLeft (margin);
    g.setColour (th.panel);
    g.fillRoundedRectangle (area.withHeight (3 * kCell).toFloat(), 8.0f);
    g.setColour (th.panelEdge);
    g.drawRoundedRectangle (area.withHeight (3 * kCell).toFloat().reduced (0.5f), 8.0f, 1.0f);
}

void Editor::resized()
{
    ap::ui::EditorBase::resized();
    auto area = getLocalBounds().withTrimmedTop (contentTop()).reduced (margin, 0);
    auto grid = area.removeFromLeft (3 * kCell).withHeight (3 * kCell);
    for (int k = 0; k < kNumKnobs; ++k)
    {
        const int col = k % 3, row = k / 3;
        knobs[(size_t) k]->setBounds (grid.getX() + col * kCell, grid.getY() + row * kCell, kCell, kCell);
    }
    area.removeFromLeft (margin);
    auto right = area.withHeight (3 * kCell);
    dose->setBounds (right.removeFromTop (2 * kCell).reduced (10));
    matchPanel.setBounds (right.reduced (10, 6));

    auto below = getLocalBounds().withTrimmedTop (contentTop() + 3 * kCell + margin).reduced (margin, 0);
    advancedToggle.setBounds (below.removeFromTop (kAdvancedToggleHeight).removeFromLeft (110));
    if (showAdvanced)
    {
        below.removeFromTop (margin);
        advanced->setBounds (below.removeFromTop (advanced->preferredHeight (below.getWidth())));
    }
}

} // namespace frisson
