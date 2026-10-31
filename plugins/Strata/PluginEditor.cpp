#include "PluginEditor.h"

namespace strata
{

namespace
{
    /** One sample of a naive (display-only) waveform at phase 0..1. */
    float waveAt (int wave, float phase, float pulseWidth)
    {
        switch (wave)
        {
            case 0:  return std::sin (phase * juce::MathConstants<float>::twoPi);
            case 1:  return 1.0f - 4.0f * std::abs (phase - 0.5f);
            case 2:  return 2.0f * phase - 1.0f;
            default: return phase < pulseWidth ? 1.0f : -1.0f;
        }
    }

    /** The voice filter's analog-prototype magnitude (JUCE's TPT SVF; band-pass is unnormalised, peak = Q). */
    float svfDb (int type, float hz, float cutoff, float q)
    {
        const float w = hz / juce::jmax (1.0f, cutoff);
        const float den = std::sqrt ((1.0f - w * w) * (1.0f - w * w) + (w / q) * (w / q));
        const float num = type == 1 ? w : (type == 2 ? w * w : 1.0f);
        return juce::Decibels::gainToDecibels (num / juce::jmax (1.0e-6f, den), -60.0f);
    }
}

//==============================================================================
WaveDisplay::WaveDisplay (juce::AudioProcessorValueTreeState& apvts, const char* waveId, const char* widthId, const char* levelId)
    : wave (apvts.getRawParameterValue (waveId)), width (apvts.getRawParameterValue (widthId)),
      level (apvts.getRawParameterValue (levelId))
{
}

void WaveDisplay::tick()
{
    const float now[3] = { wave->load(), width->load(), level->load() };
    if (! juce::exactlyEqual (now[0], shown[0]) || ! juce::exactlyEqual (now[1], shown[1]) || ! juce::exactlyEqual (now[2], shown[2]))
    {
        std::copy (std::begin (now), std::end (now), std::begin (shown));
        repaint();
    }
}

void WaveDisplay::paintDisplay (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& th = ap::ui::themeOf (*this);
    area = area.reduced (8.0f, 7.0f);
    gridLine (g, th, area.getX(), area.getCentreY(), area.getRight(), area.getCentreY());

    const int w = juce::roundToInt (shown[0]);
    const float pw = juce::jlimit (0.05f, 0.95f, shown[1] * 0.01f);
    const float amp = 0.25f + 0.75f * juce::jlimit (0.0f, 1.0f, shown[2] * 0.01f);
    juce::Path p;
    const int steps = 160;
    for (int i = 0; i <= steps; ++i)
    {
        const float t = (float) i / steps * 2.0f;
        const float y = area.getCentreY() - waveAt (w, t - std::floor (t), pw) * amp * area.getHeight() * 0.46f;
        const float x = area.getX() + area.getWidth() * (float) i / steps;
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    g.setColour (th.accent.withAlpha (0.22f));
    g.strokePath (p, juce::PathStrokeType (4.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
    g.setColour (th.accent);
    g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
}

//==============================================================================
Editor::Editor (Processor& p)
    : ap::ui::EditorBase (p, p.apvts, "Strata", "Polyphonic subtractive synth", juce::Colour (0xfff5a623), 1200, 764),
      processor (p),
      meter (p.outputLevel, -60.0f, 6.0f),
      waveA (p.apvts, ids::aWave, ids::aPw, ids::aLevel),
      waveB (p.apvts, ids::bWave, ids::bPw, ids::bLevel),
      ampEnv (p.apvts, ids::ampA, ids::ampD, ids::ampS, ids::ampR),
      filterEnv (p.apvts, ids::fenvA, ids::fenvD, ids::fenvS, ids::fenvR),
      keyboard (36, 4)
{
    using ap::ui::ControlStyle;
    auto* type = p.apvts.getRawParameterValue (ids::fType);
    auto* cutoff = p.apvts.getRawParameterValue (ids::fCutoff);
    auto* res = p.apvts.getRawParameterValue (ids::fRes);
    auto q = [res] { const float r = res->load() * 0.01f; return juce::jmap (r * r, 0.5f, 8.0f); };
    filterCurve = std::make_unique<ap::ui::ResponseDisplay> (
        [type, cutoff, q] (float hz) { return svfDb (juce::roundToInt (type->load()), hz, cutoff->load(), q()); },
        [type, cutoff, res] { return type->load() * 1.0e6f + cutoff->load() + res->load() * 1.0e-3f; },
        -36.0f, 24.0f);
    filterCurve->setMarker ([cutoff] { return cutoff->load(); });

    addHeaderComponent (meter, 190, 10);

    oscA = &addSection ("Oscillator A");
    oscA->add (ids::aWave, ControlStyle::Segmented, "Wave");
    oscA->addComponent (&waveA, 150, 46);
    oscA->newRow();
    for (auto [id, label] : { std::pair { ids::aOct, "Octave" }, { ids::aSemi, "Semi" }, { ids::aFine, "Fine" },
                              { ids::aLevel, "Level" }, { ids::aPw, "Width" }, { ids::uniVoices, "Unison" },
                              { ids::uniDetune, "Detune" }, { ids::uniSpread, "Spread" } })
        oscA->add (id, ControlStyle::SmallKnob, label);
    oscA->control (ids::aPw)->activeWhen (ids::aWave, [] (float w) { return juce::roundToInt (w) == 3; });
    oscA->control (ids::uniDetune)->activeWhen (ids::uniVoices, [] (float v) { return v > 1.5f; });
    oscA->control (ids::uniSpread)->activeWhen (ids::uniVoices, [] (float v) { return v > 1.5f; });
    addAnimated (waveA);

    oscB = &addSection ("Oscillator B");
    oscB->add (ids::bWave, ControlStyle::Segmented, "Wave");
    oscB->addComponent (&waveB, 112, 46);
    oscB->newRow();
    for (auto [id, label] : { std::pair { ids::bOct, "Octave" }, { ids::bSemi, "Semi" }, { ids::bFine, "Fine" },
                              { ids::bLevel, "Level" }, { ids::bPw, "Width" } })
        oscB->add (id, ControlStyle::SmallKnob, label);
    oscB->control (ids::bPw)->activeWhen (ids::bWave, [] (float w) { return juce::roundToInt (w) == 3; });
    addAnimated (waveB);

    sub = &addSection ("Sub & noise");
    sub->add (ids::subWave, ControlStyle::Segmented, "Sub wave");
    sub->add (ids::subOct, ControlStyle::Segmented, "Octave");
    sub->newRow();
    sub->add (ids::subLevel, ControlStyle::SmallKnob, "Sub");
    sub->add (ids::noiseLevel, ControlStyle::SmallKnob, "Noise");

    filter = &addSection ("Filter");
    filter->add (ids::fType, ControlStyle::Segmented, "Type");
    filter->addComponent (filterCurve.get(), 270, 60);
    filter->newRow();
    for (auto [id, label] : { std::pair { ids::fCutoff, "Cutoff" }, { ids::fRes, "Res" }, { ids::fEnvAmt, "Env" },
                              { ids::fKey, "Key trk" }, { ids::fLfo, "LFO 2" }, { ids::fDrive, "Drive" } })
        filter->add (id, ControlStyle::SmallKnob, label);
    addAnimated (*filterCurve);

    amp = &addSection ("Amp envelope");
    amp->addComponent (&ampEnv, -1, 60);
    for (auto [id, label] : { std::pair { ids::ampA, "Attack" }, { ids::ampD, "Decay" }, { ids::ampS, "Sustain" }, { ids::ampR, "Release" } })
        amp->add (id, ControlStyle::SmallKnob, label);
    addAnimated (ampEnv);

    fenv = &addSection ("Filter envelope");
    fenv->addComponent (&filterEnv, -1, 60);
    for (auto [id, label] : { std::pair { ids::fenvA, "Attack" }, { ids::fenvD, "Decay" }, { ids::fenvS, "Sustain" }, { ids::fenvR, "Release" } })
        fenv->add (id, ControlStyle::SmallKnob, label);
    addAnimated (filterEnv);

    lfo1 = &addSection ("LFO 1 > pitch");
    lfo1->add (ids::lfo1Shape, ControlStyle::Combo, "Shape");
    lfo1->add (ids::lfo1Rate, ControlStyle::SmallKnob, "Rate");
    lfo1->add (ids::lfo1Depth, ControlStyle::SmallKnob, "Vibrato");
    lfo1->add (ids::lfo1Fade, ControlStyle::SmallKnob, "Fade in");

    lfo2 = &addSection ("LFO 2 > filter");
    lfo2->add (ids::lfo2Shape, ControlStyle::Combo, "Shape");
    lfo2->add (ids::lfo2Rate, ControlStyle::SmallKnob, "Rate");

    global = &addSection ("Voice & output");
    for (auto [id, label] : { std::pair { ids::voices, "Voices" }, { ids::glide, "Glide" }, { ids::velAmp, "Vel > amp" },
                              { ids::velFilter, "Vel > filt" }, { ids::bendRange, "Bend" }, { ids::master, "Master" } })
        global->add (id, ControlStyle::SmallKnob, label);

    chorus = &addSection ("Chorus");
    chorus->add (ids::chorusMix, ControlStyle::SmallKnob, "Mix");
    chorus->add (ids::chorusRate, ControlStyle::SmallKnob, "Rate");

    delay = &addSection ("Delay");
    delay->add (ids::delayMix, ControlStyle::SmallKnob, "Mix");
    delay->add (ids::delayTime, ControlStyle::SmallKnob, "Time");
    delay->add (ids::delayFb, ControlStyle::SmallKnob, "Feedback");

    reverb = &addSection ("Reverb");
    reverb->add (ids::reverbMix, ControlStyle::SmallKnob, "Mix");
    reverb->add (ids::reverbSize, ControlStyle::SmallKnob, "Size");

    play = &addSection ("Play");
    play->setCaption ("click to audition");
    keyboard.onNote = [this] (int note, bool on) { processor.keyboardNote (note, on); };
    keyboard.isHeld = [this] (int note) { return processor.isNoteHeld (note); };
    play->addComponent (&keyboard, -1, 76);
    addAnimated (keyboard);

    finishSetup();
}

void Editor::layoutContent (juce::Rectangle<int> area)
{
    auto row = area.removeFromTop (186);
    oscA->setBounds (row.removeFromLeft (564));
    row.removeFromLeft (gap);
    oscB->setBounds (row.removeFromLeft (360));
    row.removeFromLeft (gap);
    sub->setBounds (row);
    area.removeFromTop (gap);

    row = area.removeFromTop (196);
    filter->setBounds (row.removeFromLeft (556));
    row.removeFromLeft (gap);
    amp->setBounds (row.removeFromLeft ((row.getWidth() - gap) / 2));
    row.removeFromLeft (gap);
    fenv->setBounds (row);
    area.removeFromTop (gap);

    row = area.removeFromTop (128);
    lfo1->setBounds (row.removeFromLeft (376));
    row.removeFromLeft (gap);
    lfo2->setBounds (row.removeFromLeft (232));
    row.removeFromLeft (gap);
    global->setBounds (row);
    area.removeFromTop (gap);

    chorus->setBounds (area.removeFromLeft (176));
    area.removeFromLeft (gap);
    delay->setBounds (area.removeFromLeft (250));
    area.removeFromLeft (gap);
    reverb->setBounds (area.removeFromLeft (176));
    area.removeFromLeft (gap);
    play->setBounds (area);
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

    // Three stacked strata.
    const auto r = badge.reduced (8.0f, 9.0f);
    g.setColour (th.background.withAlpha (0.85f));
    for (int i = 0; i < 3; ++i)
    {
        juce::Path layer;
        const float y = r.getY() + (float) i * r.getHeight() * 0.5f;
        layer.startNewSubPath (r.getX(), y + 2.0f);
        layer.cubicTo (r.getX() + r.getWidth() * 0.35f, y - 2.5f, r.getX() + r.getWidth() * 0.65f, y + 5.0f, r.getRight(), y);
        g.strokePath (layer, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

} // namespace strata
