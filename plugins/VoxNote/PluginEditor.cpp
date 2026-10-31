#include "PluginEditor.h"

#include <ap/Scales.h>

#include <algorithm>

namespace voxnote
{

namespace
{
    juce::String noteText (int midi) { return ap::ui::midiNoteText (midi); }
}

//==============================================================================
PitchRoll::PitchRoll (Processor& p)
    : ap::ui::Display ("Pitch  >  notes"), processor (p), frames ((size_t) kFrames)
{
    incoming.reserve (Processor::uiFrameCapacity);
    processor.uiFrames.clear();   // drop whatever piled up while no editor was open
}

void PitchRoll::tick()
{
    // Drain the hops the audio thread published since the last tick, then shift them in.
    incoming.clear();
    Processor::UiFrame f;
    while (processor.uiFrames.pop (f))
        incoming.push_back (f);
    const size_t added = juce::jmin (incoming.size(), frames.size());
    if (added > 0)
    {
        std::move (frames.begin() + (std::ptrdiff_t) added, frames.end(), frames.begin());
        std::copy (incoming.end() - (std::ptrdiff_t) added, incoming.end(), frames.end() - (std::ptrdiff_t) added);
    }

    // Follow the singer: centre the view on the notes of the last ~1.5 s.
    float lo = 1000.0f, hi = -1000.0f;
    for (size_t i = frames.size() - 280; i < frames.size(); ++i)
    {
        const auto& fr = frames[i];
        const float m = fr.note >= 0 ? (float) fr.note : (fr.active ? fr.midi : 0.0f);
        if (m > 0.0f) { lo = juce::jmin (lo, m); hi = juce::jmax (hi, m); }
    }
    if (hi >= lo)
    {
        const float target = juce::jlimit (24.0f + kRange * 0.5f, 108.0f - kRange * 0.5f, 0.5f * (lo + hi));
        centre += 0.08f * (target - centre);
    }
    repaint();
}

void PitchRoll::paintDisplay (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& th = ap::ui::themeOf (*this);
    auto& apvts = processor.apvts;
    const bool snap = apvts.getRawParameterValue (ids::snap)->load() > 0.5f;
    const auto scale = ap::Scale::fromType ((ap::ScaleType) (int) apvts.getRawParameterValue (ids::scale)->load(),
                                            (int) apvts.getRawParameterValue (ids::key)->load());

    area = area.withTrimmedTop (20.0f).withTrimmedBottom (4.0f);
    auto keys = area.removeFromLeft (42.0f).withTrimmedLeft (4.0f);
    auto plot = area.withTrimmedRight (8.0f).withTrimmedLeft (2.0f);

    const float lo = centre - kRange * 0.5f, hi = centre + kRange * 0.5f;
    const float rowH = plot.getHeight() / kRange;
    auto yOf = [&] (float m) { return plot.getBottom() - (m - lo) / (hi - lo) * plot.getHeight(); };
    const int sounding = processor.currentNote.load (std::memory_order_relaxed);

    ap::ui::drawPitchGrid (g, th, keys, plot, lo, hi, [&] (int note)
    {
        if (! snap) return 0;
        if (! scale.contains (note)) return -1;
        return (((note - scale.root) % 12) + 12) % 12 == 0 ? 2 : 1;
    }, sounding);

    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (plot.toNearestInt());

    // One vertical line per second (only once the host has told us the sample rate).
    const double hopsPerSecond = processor.getSampleRate() / 256.0;
    const float xPerHop = plot.getWidth() / (float) (kFrames - 1);
    if (hopsPerSecond > 20.0)
    {
        g.setColour (th.grid.brighter (0.15f));
        for (double s = 1.0; s * hopsPerSecond < kFrames; s += 1.0)
            g.fillRect (plot.getRight() - (float) (s * hopsPerSecond) * xPerHop, plot.getY(), 1.0f, plot.getHeight());
    }

    // Note bars (one per note-on, spanning the hops it sounded).
    const int n = (int) frames.size();
    for (int i = 0; i < n;)
    {
        const int note = frames[(size_t) i].note;
        if (note < 0) { ++i; continue; }
        int j = i + 1;
        while (j < n && frames[(size_t) j].note == note && ! frames[(size_t) j].noteStart) ++j;
        const float x0 = plot.getX() + (float) i * xPerHop, x1 = plot.getX() + (float) (j - 1) * xPerHop + xPerHop;
        const auto bar = juce::Rectangle<float> (x0, yOf ((float) note) - rowH * 0.42f, juce::jmax (2.0f, x1 - x0), rowH * 0.84f);
        g.setColour (th.accent.withAlpha (0.20f));
        g.fillRoundedRectangle (bar.expanded (1.5f), 4.0f);
        g.setGradientFill (juce::ColourGradient (th.accent.brighter (0.2f), 0.0f, bar.getY(), th.accent.darker (0.25f), 0.0f, bar.getBottom(), false));
        g.fillRoundedRectangle (bar, 3.0f);
        if (bar.getWidth() > 30.0f && rowH > 8.0f)
        {
            g.setColour (th.background.withAlpha (0.85f));
            g.setFont (ap::ui::Type::small().boldened());
            g.drawText (noteText (note), bar.reduced (5.0f, 0.0f), juce::Justification::centredLeft);
        }
        i = j;
    }

    // Detected pitch: faint where it is below the gate / not confident, bright where it is tracked.
    juce::Path tracked, untracked;
    bool trackedOpen = false, untrackedOpen = false;
    for (int i = 0; i < n; ++i)
    {
        const auto& fr = frames[(size_t) i];
        const bool voiced = fr.midi > 0.0f && fr.midi > lo - 2.0f && fr.midi < hi + 2.0f;
        const juce::Point<float> pt (plot.getX() + (float) i * xPerHop, yOf (fr.midi));
        const bool prevVoiced = i > 0 && frames[(size_t) i - 1].midi > 0.0f && std::abs (frames[(size_t) i - 1].midi - fr.midi) < 3.0f;
        if (! voiced) { trackedOpen = untrackedOpen = false; continue; }
        auto& path = fr.active ? tracked : untracked;
        bool& open = fr.active ? trackedOpen : untrackedOpen;
        if (open && prevVoiced) path.lineTo (pt);
        else                    path.startNewSubPath (pt);
        open = true;
        (fr.active ? untrackedOpen : trackedOpen) = false;
    }
    g.setColour (th.textDim.withAlpha (0.4f));
    g.strokePath (untracked, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (juce::Colours::white.withAlpha (0.75f));
    g.strokePath (tracked, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // "Now" edge and the newest pitch.
    g.setGradientFill (juce::ColourGradient (th.accent.withAlpha (0.0f), plot.getRight() - 24.0f, 0.0f,
                                             th.accent.withAlpha (0.10f), plot.getRight(), 0.0f, false));
    g.fillRect (plot.withLeft (plot.getRight() - 24.0f));
    const auto& last = frames.back();
    if (last.midi > lo && last.midi < hi)
    {
        const juce::Point<float> pt (plot.getRight(), yOf (last.midi));
        g.setColour ((last.active ? th.accent : th.textDim).withAlpha (0.35f));
        g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (pt));
        g.setColour (last.active ? juce::Colours::white : th.textDim);
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (pt));
    }
}

//==============================================================================
InputStatus::InputStatus (Processor& p, const PitchRoll& r)
    : processor (p), roll (r), meter (p.inputRms, -80.0f, 0.0f)
{
    meter.driveExternally();
    meter.setMarker ([this] { return processor.apvts.getRawParameterValue (ids::gate)->load(); });
    addAndMakeVisible (meter);
}

void InputStatus::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (14);
    meter.setBounds (r.removeFromTop (12));
}

void InputStatus::tick()
{
    meter.tick();
    const float rmsDb = ap::gainToDb (processor.inputRms.load (std::memory_order_relaxed));
    const float gate = processor.apvts.getRawParameterValue (ids::gate)->load();
    const auto& f = roll.latest();
    const int note = processor.currentNote.load (std::memory_order_relaxed);

    juce::String s;
    bool ok = false;
    if (note >= 0)                 { s = "Sending " + noteText (note); ok = true; }
    else if (rmsDb < -75.0f)       s = "Waiting for input (check the track's input / monitoring)";
    else if (rmsDb < gate)         s = "Below the gate: sing louder or lower Gate";
    else if (! f.active)           s = "Signal above the gate, no steady pitch yet";
    else                           { s = "Tracking"; ok = true; }
    if (s != status || ok != good) { status = s; good = ok; repaint(); }
}

void InputStatus::paint (juce::Graphics& g)
{
    const auto& th = ap::ui::themeOf (*this);
    auto r = getLocalBounds().toFloat();
    auto top = r.removeFromTop (14.0f);
    ap::ui::drawCaps (g, "Input (RMS) vs gate", top, th.textDim, juce::Justification::centredLeft,
                      ap::ui::Type::small().boldened().withExtraKerningFactor (0.1f));
    g.setColour (th.textFaint);
    g.setFont (ap::ui::Type::small());
    g.drawText (juce::String (juce::roundToInt (juce::jmax (-99.0f, ap::gainToDb (processor.inputRms.load())))) + " dB", top,
                juce::Justification::centredRight);

    r.removeFromTop (16.0f);
    auto line = r.removeFromTop (16.0f);
    const auto dot = line.removeFromLeft (12.0f).withSizeKeepingCentre (7.0f, 7.0f);
    g.setColour (good ? th.accent : juce::Colour (0xffffb340));
    g.fillEllipse (dot);
    g.setColour (good ? th.text : th.textDim);
    g.setFont (ap::ui::Type::label());
    g.drawFittedText (status, line.toNearestInt().withTrimmedLeft (4), juce::Justification::centredLeft, 1, 0.85f);
}

//==============================================================================
MidiPortPanel::MidiPortPanel (Processor& p) : processor (p)
{
    box.beforePopup = [this] { rebuildList(); };
    box.onChange = [this] { choose (box.getSelectedId()); };
    box.setTooltip ("Also send VoxNote's notes straight to a system MIDI port");
    addAndMakeVisible (box);
    rebuildList();
    processor.midiPort.onChange = [this] { rebuildList(); tick(); };
    tick();
}

MidiPortPanel::~MidiPortPanel()
{
    processor.midiPort.onChange = nullptr;
}

void MidiPortPanel::rebuildList()
{
    ports = processor.midiPort.availablePorts();
    const auto wanted = processor.midiPort.requestedIdentifier();
    box.clear (juce::dontSendNotification);
    box.addItem ("Off (host MIDI only)", 1);
    int selected = 1;
    for (size_t i = 0; i < ports.size(); ++i)
    {
        box.addItem (ports[i].name, (int) i + 2);
        if (ports[i].identifier == wanted) selected = (int) i + 2;
    }
    if (wanted.isNotEmpty() && selected == 1)
    {
        // Saved with a port that is not here now (e.g. loopMIDI not running): keep showing it.
        box.addItem (processor.midiPort.requestedName() + " (not found)", 1000);
        selected = 1000;
    }
    box.setSelectedId (selected, juce::dontSendNotification);
}

void MidiPortPanel::choose (int itemId)
{
    if (itemId == 1)
        processor.setMidiPort ({}, {});
    else if (itemId >= 2 && itemId - 2 < (int) ports.size())
        processor.setMidiPort (ports[(size_t) itemId - 2].identifier, ports[(size_t) itemId - 2].name);
}

void MidiPortPanel::tick()
{
    const auto s = processor.midiPort.statusText();
    if (s != status) { status = s; repaint(); }
}

void MidiPortPanel::resized()
{
    auto r = getLocalBounds();
    auto left = r.removeFromLeft (juce::jmin (250, r.getWidth() / 2));
    left.removeFromTop (20);
    box.setBounds (left.removeFromTop (26));
}

void MidiPortPanel::paint (juce::Graphics& g)
{
    const auto& th = ap::ui::themeOf (*this);
    auto r = getLocalBounds().toFloat();
    auto left = r.removeFromLeft ((float) juce::jmin (250, getWidth() / 2));
    ap::ui::drawCaps (g, "Direct to a MIDI port", left.removeFromTop (14.0f), th.textDim, juce::Justification::centredLeft,
                      ap::ui::Type::small().boldened().withExtraKerningFactor (0.1f));
    left.removeFromTop (38.0f);
    const bool open = processor.midiPort.isOpen();
    const bool wanted = processor.midiPort.requestedIdentifier().isNotEmpty();
    auto line = left.removeFromTop (16.0f);
    g.setColour (open ? th.accent : (wanted ? th.meterHigh : th.textFaint));
    g.fillEllipse (line.removeFromLeft (12.0f).withSizeKeepingCentre (7.0f, 7.0f));
    g.setColour (open ? th.text : th.textDim);
    g.setFont (ap::ui::Type::label());
    g.drawFittedText (status, line.toNearestInt().withTrimmedLeft (4), juce::Justification::centredLeft, 1, 0.85f);

    r.removeFromLeft (16.0f);
    g.setColour (th.textDim);
    g.setFont (ap::ui::Type::label());
    const juce::String help = juce::String::fromUTF8 (
        "No notes on your instrument? FL Studio, Logic and GarageBand don't pass an effect's "
        "MIDI on. Pick a port here and play the instrument from it like a keyboard.\n"
        "Windows: create a port in loopMIDI, choose it here, then enable it as an input in "
        "FL's Options \xe2\x80\xba MIDI Settings.  macOS / Linux: choose \"VoxNote (virtual port)\".");
    g.drawFittedText (help, r.toNearestInt(), juce::Justification::topLeft, 6, 1.0f);
}

//==============================================================================
Editor::Editor (Processor& p)
    : ap::ui::EditorBase (p, p.apvts, "VoxNote", "Hum or sing in, MIDI out", juce::Colour (0xff34c759), 1040, 640),
      processor (p),
      readout (p.detectedHz),
      midiLed (p.midiEventCount, "MIDI out"),
      roll (p),
      input (p, roll),
      portPanel (p)
{
    addHeaderComponent (readout, 230, 48);
    addHeaderComponent (midiLed, 84, 20);

    addContent (roll);

    using ap::ui::ControlStyle;
    detection = &addSection ("Detection");
    detection->addComponent (&input, -1, 48);
    addAnimated (input);
    detection->add (ids::tracking, ControlStyle::Segmented);
    detection->newRow();
    for (auto* id : { ids::gate, ids::sensitivity, ids::onset, ids::release })
        detection->add (id, ControlStyle::SmallKnob);

    notes = &addSection ("Notes", { ids::retrigger, ids::transpose });

    scale = &addSection ("Snap to scale");
    scale->setHeaderSwitch (ids::snap);
    scale->add (ids::key, ControlStyle::Combo);
    scale->add (ids::scale, ControlStyle::Combo);

    bend = &addSection ("Pitch bend");
    bend->setHeaderSwitch (ids::bend);
    bend->add (ids::bendRange);

    velocity = &addSection ("Velocity");
    velocity->add (ids::velDynamic, ControlStyle::Switch, "Dynamic");
    velocity->add (ids::velFixed, ControlStyle::SmallKnob, "Fixed").activeWhen (ids::velDynamic, [] (float v) { return v < 0.5f; });
    velocity->add (ids::velMin, ControlStyle::SmallKnob, "Min").activeWhen (ids::velDynamic, [] (float v) { return v > 0.5f; });
    velocity->add (ids::velMax, ControlStyle::SmallKnob, "Max").activeWhen (ids::velDynamic, [] (float v) { return v > 0.5f; });

    output = &addSection ("Output");
    output->add (ids::channel, ControlStyle::Knob, "Channel");
    output->add (ids::audioThru, ControlStyle::Switch);
    output->add (ids::monitor, ControlStyle::Switch, "Monitor tone");
    output->add (ids::monitorLevel, ControlStyle::Knob, "Tone level").activeWhen (ids::monitor, [] (float v) { return v > 0.5f; });

    midiOut = &addSection ("MIDI out");
    midiOut->setCaption ("host MIDI is always sent too");
    midiOut->addComponent (&portPanel, -1, 92);
    addAnimated (portPanel);

    finishSetup();
}

void Editor::layoutContent (juce::Rectangle<int> area)
{
    auto row1 = area.removeFromTop (236);
    detection->setBounds (row1.removeFromRight (340));
    row1.removeFromRight (gap);
    roll.setBounds (row1);
    area.removeFromTop (gap);

    auto row2 = area.removeFromTop (144);
    notes->setBounds (row2.removeFromLeft (196));
    row2.removeFromLeft (gap);
    scale->setBounds (row2.removeFromLeft (250));
    row2.removeFromLeft (gap);
    bend->setBounds (row2.removeFromLeft (172));
    row2.removeFromLeft (gap);
    velocity->setBounds (row2);
    area.removeFromTop (gap);

    output->setBounds (area.removeFromLeft (420));
    area.removeFromLeft (gap);
    midiOut->setBounds (area);
}

void Editor::paintLogo (juce::Graphics& g, juce::Rectangle<float> badge)
{
    ap::ui::EditorBase::paintLogo (g, badge);   // badge + letter, then cover the letter with the glyph
    const auto& th = lookAndFeel.theme();
    juce::Path shape;
    shape.addRoundedRectangle (badge, 9.0f);
    g.setGradientFill (juce::ColourGradient (th.accent.brighter (0.25f), badge.getX(), badge.getY(),
                                             th.accent.darker (0.35f), badge.getRight(), badge.getBottom(), false));
    g.fillPath (shape);

    // A sung wave settling into a note.
    const auto r = badge.reduced (7.0f);
    juce::Path wave;
    for (int i = 0; i <= 20; ++i)
    {
        const float t = (float) i / 20.0f;
        const float x = r.getX() + t * r.getWidth() * 0.55f;
        const float y = r.getCentreY() + 2.0f + std::sin (t * juce::MathConstants<float>::twoPi * 1.5f) * r.getHeight() * 0.22f * (1.0f - t * 0.6f);
        if (i == 0) wave.startNewSubPath (x, y); else wave.lineTo (x, y);
    }
    g.setColour (th.background.withAlpha (0.85f));
    g.strokePath (wave, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const auto head = juce::Rectangle<float> (8.0f, 6.0f).withCentre ({ r.getRight() - 6.0f, r.getBottom() - 4.0f });
    g.fillEllipse (head);
    g.fillRect (head.getRight() - 1.8f, r.getY() + 1.0f, 1.8f, head.getCentreY() - r.getY() - 1.0f);
}

} // namespace voxnote
