#include "PluginEditor.h"

namespace humline
{

namespace
{
    const ap::ui::Theme& themeOf (const juce::Component& c)
    {
        static const ap::ui::Theme fallback {};
        const auto* lf = dynamic_cast<const ap::ui::LookAndFeel*> (&c.getLookAndFeel());
        return lf != nullptr ? lf->theme() : fallback;
    }
}

//==============================================================================
void CapturePanel::DragHandle::paint (juce::Graphics& g)
{
    const auto& th = themeOf (*this);
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (th.knobTrack);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (isMouseOver() ? th.accent : th.panelEdge);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
    g.setColour (th.text);
    g.setFont (ap::ui::LookAndFeel::labelFont());
    g.drawText ("Drag .mid to DAW", getLocalBounds(), juce::Justification::centred);
}

void CapturePanel::DragHandle::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 8) return;
    dragging = true;
    owner.startExternalDrag();
}

void CapturePanel::DragHandle::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
}

//==============================================================================
CapturePanel::CapturePanel (Processor& p)
    : processor (p), handle (*this)
{
    captureButton.setClickingTogglesState (true);
    captureButton.onClick = [this]
    {
        processor.capturing.store (captureButton.getToggleState());
    };
    clearButton.onClick = [this] { processor.requestClear(); };
    saveButton.onClick = [this] { saveAs(); };

    addAndMakeVisible (captureButton);
    addAndMakeVisible (clearButton);
    addAndMakeVisible (saveButton);
    addAndMakeVisible (handle);
    startTimerHz (10);
}

CapturePanel::~CapturePanel() { stopTimer(); }

void CapturePanel::timerCallback()
{
    const auto gen = processor.take().currentGeneration();
    const int count = processor.take().size();
    const bool cap = processor.capturing.load();
    if (gen != shownGeneration || count != shownCount || cap != shownCapturing)
    {
        shownGeneration = gen;
        shownCount = count;
        shownCapturing = cap;
        processor.take().snapshot (notes);
        shownBeats = 0.0;
        if (! notes.empty())
        {
            double first = notes.front().startBeat, last = notes.front().endBeat;
            for (const auto& n : notes) { first = juce::jmin (first, n.startBeat); last = juce::jmax (last, n.endBeat); }
            shownBeats = last - first;
        }
        captureButton.setToggleState (cap, juce::dontSendNotification);
        repaint();
    }
}

void CapturePanel::paint (juce::Graphics& g)
{
    const auto& th = themeOf (*this);
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (th.panel);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (th.panelEdge);
    g.drawRoundedRectangle (r, 8.0f, 1.0f);
    g.setColour (shownCapturing ? th.meterHigh : th.accent);
    g.fillRoundedRectangle (juce::Rectangle<float> (10.0f, 7.0f, 3.0f, 12.0f), 1.5f);
    g.setColour (th.textDim);
    g.setFont (ap::ui::LookAndFeel::sectionFont());
    g.drawText ("CAPTURE TAKE", 20, 2, getWidth() - 30, 22, juce::Justification::centredLeft);

    juce::String info = shownCount == 0 ? juce::String ("no notes yet")
                                        : juce::String (shownCount) + (shownCount == 1 ? " note" : " notes")
                                          + "  ·  " + juce::String (shownBeats, 1) + " beats";
    if (shownCapturing) info = "recording  ·  " + info;
    g.setColour (th.text);
    g.setFont (ap::ui::LookAndFeel::labelFont());
    g.drawText (info, getLocalBounds().reduced (12, 0).removeFromBottom (24), juce::Justification::centredLeft);
}

void CapturePanel::resized()
{
    auto area = getLocalBounds().reduced (10);
    area.removeFromTop (22);
    auto row = area.removeFromTop (30);
    const int w = row.getWidth();
    captureButton.setBounds (row.removeFromLeft (w * 28 / 100).reduced (2, 0));
    clearButton.setBounds (row.removeFromLeft (w * 18 / 100).reduced (2, 0));
    saveButton.setBounds (row.removeFromLeft (w * 24 / 100).reduced (2, 0));
    handle.setBounds (row.reduced (2, 0));
}

juce::File CapturePanel::writeTempFile()
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Humline");
    dir.createDirectory();
    const auto name = "Humline take " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S") + ".mid";
    auto file = dir.getChildFile (name);
    if (! processor.writeTakeToFile (file)) return {};
    return file;
}

void CapturePanel::startExternalDrag()
{
    if (processor.take().size() == 0) return;
    const auto file = writeTempFile();
    if (file == juce::File()) return;
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, true, this);
}

void CapturePanel::saveAs()
{
    const auto defaultFile = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                 .getChildFile ("Humline take.mid");
    chooser = std::make_unique<juce::FileChooser> ("Save take as MIDI", defaultFile, "*.mid");
    const int mode = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                      | juce::FileBrowserComponent::warnAboutOverwriting;
    chooser->launchAsync (mode, [this] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file == juce::File()) return;
        if (! file.hasFileExtension ("mid")) file = file.withFileExtension ("mid");
        processor.writeTakeToFile (file);
    });
}

//==============================================================================
Editor::Editor (Processor& p)
    : ap::ui::EditorBase (p, p.apvts, "Humline", "Hum it, hear it in tune, drag it out as MIDI", juce::Colour (0xffff5e8a), 820, 2),
      processor (p),
      readout (p.detectedHz, &p.playedHz),
      inMeter (p.inputLevel),
      outMeter (p.outputLevel),
      capturePanel (p)
{
    setHeaderComponent (&readout, 230, 46);

    auto& det = addSection ("Detection", { ids::tracking, ids::gate, ids::sensitivity, ids::minNote, ids::release }, 0, 0);
    det.setTrailingComponent (&inMeter, 14);
    addSection ("Pitch",  { ids::key, ids::scale, ids::snap, ids::octave, ids::bend, ids::bendRange }, 0, 0);
    addSection ("Rhythm", { ids::grid }, 0, 0);

    auto& syn = addSection ("Synth", { ids::timbre, ids::synthCutoff, ids::synthRes, ids::synthAttack,
                                       ids::synthRelease, ids::glide, ids::synthLevel }, 0, 1);
    syn.setTrailingComponent (&outMeter, 14);
    addSection ("Output", { ids::dryMix, ids::midiOut, ids::channel, ids::velDynamic, ids::velFixed }, 0, 1);
    addCustomSection (&capturePanel, 92, 1);

    finishLayout();
}

} // namespace humline
