#include "PluginEditor.h"

#include <ap/Filters.h>
#include <ap/Scales.h>

namespace intone
{

namespace
{
    ap::Scale currentScale (juce::AudioProcessorValueTreeState& apvts)
    {
        juce::uint16 mask = 0;
        for (int pc = 0; pc < 12; ++pc)
            if (apvts.getRawParameterValue (ids::note (pc))->load() > 0.5f) mask |= (juce::uint16) (1u << pc);
        const int key = (int) apvts.getRawParameterValue (ids::key)->load();
        // The custom mask is per pitch class from C; Scale stores masks relative to the root.
        const juce::uint16 rel = (juce::uint16) (((mask >> key) | (mask << (12 - key))) & 0x0FFF);
        return ap::Scale::fromType ((ap::ScaleType) (int) apvts.getRawParameterValue (ids::scale)->load(), key, rel);
    }

    bool isBlack (int pc) { return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }
}

//==============================================================================
PitchHistory::PitchHistory (Processor& p) : ap::ui::Display ("Detected  >  corrected"), processor (p) {}

void PitchHistory::tick()
{
    const float d = processor.detectedHz.load (std::memory_order_relaxed);
    const float t = processor.targetHz.load (std::memory_order_relaxed);
    detected.push (d > 0.0f ? ap::hzToMidi (d) : 0.0f);
    corrected.push (t > 0.0f && d > 0.0f ? ap::hzToMidi (t) : 0.0f);
    if (d > 0.0f)
        centre += 0.06f * (juce::jlimit (36.0f, 96.0f, ap::hzToMidi (d)) - centre);
    repaint();
}

void PitchHistory::paintDisplay (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& th = ap::ui::themeOf (*this);
    const auto scale = currentScale (processor.apvts);
    const bool chromatic = (scale.mask & 0x0FFF) == 0x0FFF;

    area = area.withTrimmedTop (20.0f).withTrimmedBottom (4.0f);
    auto keys = area.removeFromLeft (42.0f).withTrimmedLeft (4.0f);
    auto plot = area.withTrimmedRight (8.0f).withTrimmedLeft (2.0f);
    const float lo = centre - 9.0f, hi = centre + 9.0f;
    auto yOf = [&] (float m) { return plot.getBottom() - (m - lo) / (hi - lo) * plot.getHeight(); };

    const float targetHz = processor.targetHz.load();
    ap::ui::drawPitchGrid (g, th, keys, plot, lo, hi, [&] (int note)
    {
        if (chromatic) return 0;
        if (! scale.contains (note)) return -1;
        return (((note - scale.root) % 12) + 12) % 12 == 0 ? 2 : 1;
    }, targetHz > 0.0f ? (int) std::lround (ap::hzToMidi (targetHz)) : -1);

    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (plot.toNearestInt());

    auto trace = [&] (const ap::ui::History& h)
    {
        juce::Path p;
        bool open = false;
        for (size_t i = 0; i < h.size(); ++i)
        {
            const float m = h[i];
            if (m <= 0.0f) { open = false; continue; }
            const juce::Point<float> pt (plot.getX() + plot.getWidth() * (float) i / (float) (h.size() - 1), yOf (m));
            if (open && std::abs (h[i - 1] - m) < 4.0f) p.lineTo (pt); else p.startNewSubPath (pt);
            open = true;
        }
        return p;
    };
    const auto det = trace (detected), cor = trace (corrected);
    g.setColour (th.textDim.withAlpha (0.8f));
    g.strokePath (det, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (th.accent.withAlpha (0.25f));
    g.strokePath (cor, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (th.accent);
    g.strokePath (cor, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Legend.
    auto legend = plot.removeFromTop (16.0f).removeFromRight (190.0f);
    g.setFont (ap::ui::Type::small());
    g.setColour (th.textDim);
    g.fillRect (legend.getX(), legend.getCentreY(), 12.0f, 1.5f);
    g.drawText ("your voice", legend.withTrimmedLeft (16.0f), juce::Justification::centredLeft);
    g.setColour (th.accent);
    g.fillRect (legend.getX() + 92.0f, legend.getCentreY() - 0.5f, 12.0f, 2.0f);
    g.drawText ("corrected", legend.withTrimmedLeft (108.0f), juce::Justification::centredLeft);
}

//==============================================================================
ScaleKeys::ScaleKeys (Processor& p) : processor (p)
{
    for (int pc = 0; pc < 12; ++pc)
        if (auto* param = p.apvts.getParameter (ids::note (pc)))
        {
            attachments[(size_t) pc] = std::make_unique<juce::ParameterAttachment> (*param,
                [this, pc] (float v) { custom[(size_t) pc] = v > 0.5f; repaint(); }, nullptr);
            attachments[(size_t) pc]->sendInitialUpdate();
        }
}

juce::Rectangle<float> ScaleKeys::keyBounds (int pc) const
{
    static const int whiteIndex[12] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
    const auto r = getLocalBounds().toFloat();
    const float w = r.getWidth() / 7.0f;
    if (! isBlack (pc))
        return { r.getX() + (float) whiteIndex[pc] * w, r.getY(), w, r.getHeight() };
    return { r.getX() + (float) (whiteIndex[pc] + 1) * w - w * 0.32f, r.getY(), w * 0.64f, r.getHeight() * 0.58f };
}

int ScaleKeys::keyAt (juce::Point<float> p) const
{
    for (int pc = 0; pc < 12; ++pc)
        if (isBlack (pc) && keyBounds (pc).contains (p)) return pc;
    for (int pc = 0; pc < 12; ++pc)
        if (! isBlack (pc) && keyBounds (pc).contains (p)) return pc;
    return -1;
}

void ScaleKeys::mouseDown (const juce::MouseEvent& e)
{
    const int pc = keyAt (e.position);
    if (pc >= 0 && attachments[(size_t) pc] != nullptr)
        attachments[(size_t) pc]->setValueAsCompleteGesture (custom[(size_t) pc] ? 0.0f : 1.0f);
}

void ScaleKeys::tick()
{
    const bool isCustom = (int) processor.apvts.getRawParameterValue (ids::scale)->load() == (int) ap::ScaleType::Custom;
    const auto s = currentScale (processor.apvts);
    if (isCustom != customMode || s.mask != shownMask || s.root != shownRoot)
    {
        customMode = isCustom;
        shownMask = s.mask;
        shownRoot = s.root;
        repaint();
    }
}

void ScaleKeys::paint (juce::Graphics& g)
{
    const auto& th = ap::ui::themeOf (*this);
    ap::Scale s;
    s.root = shownRoot;
    s.mask = shownMask;
    for (int pass = 0; pass < 2; ++pass)
        for (int pc = 0; pc < 12; ++pc)
        {
            if (isBlack (pc) != (pass == 1)) continue;
            const bool on = customMode ? custom[(size_t) pc] : s.contains (pc);
            auto k = keyBounds (pc);
            if (! isBlack (pc))
            {
                k = k.reduced (1.0f, 0.0f);
                g.setColour (on ? (customMode ? th.accent : th.accent.withAlpha (0.55f).overlaidWith (juce::Colours::transparentBlack))
                                : juce::Colour (0xffd9dde2));
                if (on && ! customMode) g.setColour (juce::Colour (0xffd9dde2).interpolatedWith (th.accent, 0.45f));
                g.fillRoundedRectangle (k, 4.0f);
                g.setColour (on ? th.background : juce::Colour (0xff5f6670));
                g.setFont (ap::ui::Type::small().boldened());
                g.drawText (ap::noteName (pc), k.withTrimmedBottom (5.0f), juce::Justification::centredBottom);
            }
            else
            {
                juce::Path p;
                p.addRoundedRectangle (k, 3.0f);
                juce::DropShadow (juce::Colours::black.withAlpha (0.5f), 4, { 1, 2 }).drawForPath (g, p);
                g.setColour (on ? (customMode ? th.accent.darker (0.15f) : juce::Colour (0xff2a2f37).interpolatedWith (th.accent, 0.5f))
                                : juce::Colour (0xff20242a));
                g.fillPath (p);
            }
        }
}

//==============================================================================
Editor::Editor (Processor& p)
    : ap::ui::EditorBase (p, p.apvts, "Intone", "Pitch correction + vocal chain", juce::Colour (0xffbf5af2), 1060, 676),
      processor (p),
      readout (p.detectedHz, &p.targetHz),
      history (p),
      scaleKeys (p),
      grMeter (p.gainReductionDb),
      meter (p.outputLevel, -60.0f, 6.0f)
{
    using ap::ui::ControlStyle;
    auto& apvts = p.apvts;
    auto* lowCut = apvts.getRawParameterValue (ids::lowCut);
    auto* body = apvts.getRawParameterValue (ids::body);
    auto* presence = apvts.getRawParameterValue (ids::presence);
    auto* air = apvts.getRawParameterValue (ids::air);
    // Same filters and settings as the processor's tone stage (ChannelChain), evaluated at 48 kHz.
    eqCurve = std::make_unique<ap::ui::ResponseDisplay> (
        [lowCut, body, presence, air] (float hz)
        {
            constexpr double sr = 48000.0;
            ap::Biquad hp, b, pr, a;
            hp.configure (ap::Biquad::Type::HighPass, sr, lowCut->load(), 0.707f);
            b.configure (ap::Biquad::Type::Peak, sr, 250.0f, 1.0f, body->load());
            pr.configure (ap::Biquad::Type::Peak, sr, 3200.0f, 1.2f, presence->load());
            a.configure (ap::Biquad::Type::HighShelf, sr, 11000.0f, 0.707f, air->load());
            return hp.magnitudeDb (hz, sr) + b.magnitudeDb (hz, sr) + pr.magnitudeDb (hz, sr) + a.magnitudeDb (hz, sr);
        },
        [lowCut, body, presence, air] { return lowCut->load() + body->load() * 7.0f + presence->load() * 131.0f + air->load() * 1031.0f; },
        -18.0f, 18.0f);

    addHeaderComponent (readout, 250, 48);
    addContent (history);

    correction = &addSection ("Correction");
    correction->add (ids::key, ControlStyle::Combo);
    correction->add (ids::scale, ControlStyle::Combo);
    correction->newRow();
    correction->add (ids::speed, ControlStyle::SmallKnob, "Speed");
    correction->add (ids::amount, ControlStyle::SmallKnob, "Amount");
    correction->add (ids::transpose, ControlStyle::SmallKnob, "Transpose");
    correction->add (ids::sensitivity, ControlStyle::SmallKnob, "Sensitivity");
    correction->add (ids::mix, ControlStyle::SmallKnob, "Mix");
    correction->newRow();
    correction->add (ids::tracking, ControlStyle::Segmented);

    notes = &addSection ("Custom scale");
    notes->setCaption ("click keys when Scale = Custom");
    notes->addComponent (&scaleKeys, -1, 92);
    addAnimated (scaleKeys);

    deess = &addSection ("De-esser");
    deess->add (ids::deessAmount, ControlStyle::Knob, "Amount");
    deess->add (ids::deessFreq, ControlStyle::Knob, "Freq");

    comp = &addSection ("Compressor");
    comp->addComponent (&grMeter, -1, 30);
    addAnimated (grMeter);
    for (auto [id, label] : { std::pair { ids::compThresh, "Threshold" }, { ids::compRatio, "Ratio" }, { ids::compAttack, "Attack" },
                              { ids::compRelease, "Release" }, { ids::compMakeup, "Makeup" } })
        comp->add (id, ControlStyle::SmallKnob, label);

    tone = &addSection ("Tone");
    tone->addComponent (eqCurve.get(), 290, 96);
    addAnimated (*eqCurve);
    tone->add (ids::lowCut, ControlStyle::Knob);
    tone->add (ids::body, ControlStyle::Knob);
    tone->add (ids::presence, ControlStyle::Knob);
    tone->add (ids::air, ControlStyle::Knob);

    output = &addSection ("Output");
    output->add (ids::output, ControlStyle::Knob, "Level");
    output->setTrailingComponent (&meter, 12);
    addAnimated (meter);

    finishSetup();
}

void Editor::layoutContent (juce::Rectangle<int> area)
{
    auto row = area.removeFromTop (236);
    correction->setBounds (row.removeFromRight (372));
    row.removeFromRight (gap);
    history.setBounds (row);
    area.removeFromTop (gap);

    row = area.removeFromTop (172);
    notes->setBounds (row.removeFromLeft (330));
    row.removeFromLeft (gap);
    deess->setBounds (row.removeFromLeft (196));
    row.removeFromLeft (gap);
    comp->setBounds (row);
    area.removeFromTop (gap);

    output->setBounds (area.removeFromRight (180));
    area.removeFromRight (gap);
    tone->setBounds (area);
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

    // A wavering line that settles onto a straight pitch.
    const auto r = badge.reduced (7.0f, 9.0f);
    juce::Path p;
    for (int i = 0; i <= 24; ++i)
    {
        const float t = (float) i / 24.0f;
        const float wob = std::sin (t * juce::MathConstants<float>::twoPi * 2.0f) * (1.0f - juce::jmin (1.0f, t * 1.6f));
        const float y = r.getCentreY() + wob * r.getHeight() * 0.42f;
        if (i == 0) p.startNewSubPath (r.getX() + t * r.getWidth(), y); else p.lineTo (r.getX() + t * r.getWidth(), y);
    }
    g.setColour (th.background.withAlpha (0.85f));
    g.strokePath (p, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ r.getRight(), r.getCentreY() }));
}

} // namespace intone
