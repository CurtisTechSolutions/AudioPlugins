#include "SampleDropZone.h"

#include "../BackgroundTasks.h"
#include "../PluginProcessor.h"

namespace verdant::ui
{

namespace
{
    juce::Colour col (juce::uint32 c) { return juce::Colour (c); }

    juce::String noteNameFor (float hz)
    {
        if (hz <= 0.0f) return {};
        const float midi = 69.0f + 12.0f * std::log2 (hz / 440.0f);
        const int n = juce::roundToInt (midi);
        const int cents = juce::roundToInt ((midi - (float) n) * 100.0f);
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return juce::String (names[((n % 12) + 12) % 12]) + juce::String (n / 12 - 1)
               + (cents != 0 ? " " + juce::String (cents > 0 ? "+" : "") + juce::String (cents) + " ct" : juce::String());
    }
}

SampleDropZone::SampleDropZone (Processor& p) : processor (p)
{
    applyButton.onClick = [this] { apply(); };
    discardButton.onClick = [this] { discard(); };
    applyButton.setToggleState (true, juce::dontSendNotification);   // drawn as the primary action
    addChildComponent (applyButton);
    addChildComponent (discardButton);
    setTooltip ("Drop an audio file here (or anywhere on Verdant), or click to choose one");
}

SampleDropZone::~SampleDropZone()
{
    ++*generation;
}

bool SampleDropZone::isAudioFile (const juce::String& path)
{
    for (const auto* ext : { ".wav", ".wave", ".aif", ".aiff", ".flac", ".ogg", ".mp3", ".m4a" })
        if (path.endsWithIgnoreCase (ext)) return true;
    return false;
}

void SampleDropZone::setState (State s)
{
    state = s;
    applyButton.setVisible (s == State::Ready);
    discardButton.setVisible (s == State::Ready || s == State::Failed);
    discardButton.setButtonText (s == State::Failed ? "Dismiss" : "Discard");
    resized();
    repaint();
}

void SampleDropZone::analyse (const juce::File& f)
{
    file = f;
    progress = 0.0f;
    error.clear();
    result.reset();
    setState (State::Analysing);
    const int gen = ++*generation;
    std::weak_ptr<int> token = generation;
    juce::Component::SafePointer<SampleDropZone> safe (this);

    processor.tasks().analyzeSample (f,
        [safe, token, gen] (float p)
        {
            const auto t = token.lock();
            if (safe == nullptr || t == nullptr || *t != gen) return;
            safe->progress = p;
            safe->repaint();
        },
        [safe, token, gen] (const SampleMatchResult& r, juce::String err)
        {
            const auto t = token.lock();
            if (safe == nullptr || t == nullptr || *t != gen) return;
            if (err.isNotEmpty() || r.table == nullptr)
            {
                safe->error = err.isNotEmpty() ? err : juce::String ("No usable sound found in the file");
                safe->setState (State::Failed);
                return;
            }
            safe->result = std::make_shared<SampleMatchResult> (r);
            safe->setState (State::Ready);
        });
}

void SampleDropZone::apply()
{
    if (result == nullptr) return;
    processor.applyPatch (patchFromSampleMatch (*result, processor.currentPatch(), file.getFileNameWithoutExtension()), false);
    result.reset();
    setState (State::Idle);
}

void SampleDropZone::discard()
{
    ++*generation;
    result.reset();
    setState (State::Idle);
}

//==============================================================================
void SampleDropZone::resized()
{
    auto r = getLocalBounds().reduced (14, 12).removeFromBottom (26);
    applyButton.setBounds (r.removeFromRight (80));
    r.removeFromRight (8);
    discardButton.setBounds (r.removeFromRight (80));
}

void SampleDropZone::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const auto accent = col (palette::accent);
    const bool hover = isMouseOver (true) && state == State::Idle;

    g.setColour (col (dragOver ? palette::controlHover : palette::well).withAlpha (dragOver ? 1.0f : 0.7f));
    g.fillRoundedRectangle (bounds, Theme::radius);
    if (dragOver)
    {
        g.setColour (accent.withAlpha (0.08f));
        g.fillRoundedRectangle (bounds, Theme::radius);
    }
    juce::Path outline, dashed;
    outline.addRoundedRectangle (bounds.reduced (1.0f), Theme::radius);
    const float dashes[] = { 5.0f, 4.0f };
    juce::PathStrokeType (dragOver ? 1.6f : 1.0f).createDashedStroke (dashed, outline, dashes, 2);
    g.setColour (dragOver || hover ? accent : col (palette::panelEdge).brighter (0.3f));
    g.fillPath (dashed);

    auto area = getLocalBounds().reduced (16, 12);
    switch (state)
    {
        case State::Idle:
        {
            // A waveform glyph + the call to action.
            auto icon = area.removeFromLeft (46).toFloat().withSizeKeepingCentre (40.0f, 30.0f);
            juce::Path wave;
            for (int i = 0; i <= 40; ++i)
            {
                const float t = (float) i / 40.0f;
                const float y = icon.getCentreY() - std::sin (t * 18.0f) * std::exp (-3.0f * t) * icon.getHeight() * 0.45f;
                if (i == 0) wave.startNewSubPath (icon.getX(), y); else wave.lineTo (icon.getX() + t * icon.getWidth(), y);
            }
            g.setColour (dragOver || hover ? accent : col (palette::textDim));
            g.strokePath (wave, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            area.removeFromLeft (10);
            auto text = area.withSizeKeepingCentre (area.getWidth(), 36);
            g.setColour (col (palette::text));
            g.setFont (font (13.0f, true));
            g.drawText (dragOver ? "Release to match this sample" : "Drop a sample to match", text.removeFromTop (18), juce::Justification::centredLeft, false);
            g.setColour (col (palette::textDim));
            g.setFont (font (Theme::fontSmall));
            g.drawText ("Verdant builds a patch that sounds like it - or click to choose a file", text, juce::Justification::centredLeft, true);
            break;
        }
        case State::Analysing:
        {
            g.setColour (col (palette::text));
            g.setFont (font (12.5f, true));
            g.drawFittedText ("Analysing " + file.getFileName(), area.removeFromTop (18), juce::Justification::centredLeft, 1, 0.8f);
            area.removeFromTop (8);
            const auto bar = area.removeFromTop (6).toFloat();
            g.setColour (col (palette::knobTrack));
            g.fillRoundedRectangle (bar, 3.0f);
            g.setColour (accent.withAlpha (0.25f));
            g.fillRoundedRectangle (bar.withWidth (juce::jmax (6.0f, bar.getWidth() * progress)).expanded (0.0f, 2.0f), 4.0f);
            g.setColour (accent);
            g.fillRoundedRectangle (bar.withWidth (juce::jmax (6.0f, bar.getWidth() * progress)), 3.0f);
            area.removeFromTop (6);
            g.setColour (col (palette::textDim));
            g.setFont (font (Theme::fontSmall));
            g.drawText (juce::String (juce::roundToInt (progress * 100.0f)) + " %  -  pitch, spectrum, envelope and filter", area.removeFromTop (14),
                        juce::Justification::centredLeft, false);
            break;
        }
        case State::Ready:
        {
            const auto& r = *result;
            g.setColour (col (palette::text));
            g.setFont (font (12.5f, true));
            g.drawFittedText ("Matched " + file.getFileName(), area.removeFromTop (18), juce::Justification::centredLeft, 1, 0.8f);
            const juce::String pitch = r.detectedHz > 0.0f ? juce::String (r.detectedHz, 1) + " Hz  (" + noteNameFor (r.detectedHz) + ")" : juce::String ("unpitched");
            g.setColour (accent);
            g.setFont (font (Theme::fontSmall, true));
            g.drawText (pitch + "   voiced " + juce::String (juce::roundToInt (r.confidence * 100.0f)) + " %", area.removeFromTop (16),
                        juce::Justification::centredLeft, false);
            g.setColour (col (palette::textDim));
            g.setFont (font (Theme::fontSmall));
            g.drawFittedText (juce::String::fromUTF8 (r.notes.c_str()), area.withTrimmedBottom (30).withTrimmedRight (4), juce::Justification::topLeft, 2, 0.85f);
            break;
        }
        case State::Failed:
        {
            g.setColour (col (palette::warning));
            g.setFont (font (12.5f, true));
            g.drawText ("Could not match " + file.getFileName(), area.removeFromTop (18), juce::Justification::centredLeft, false);
            g.setColour (col (palette::textDim));
            g.setFont (font (Theme::fontSmall));
            g.drawFittedText (error, area.withTrimmedBottom (30), juce::Justification::topLeft, 2, 0.85f);
            break;
        }
    }
}

void SampleDropZone::mouseUp (const juce::MouseEvent& e)
{
    if (state != State::Idle || e.mouseWasDraggedSinceMouseDown()) return;
    chooser = std::make_unique<juce::FileChooser> ("Choose a sample to match", juce::File(),
                                                   "*.wav;*.wave;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.m4a");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<SampleDropZone> (this)] (const juce::FileChooser& fc)
                          {
                              if (safe != nullptr && fc.getResult().existsAsFile())
                                  safe->analyse (fc.getResult());
                          });
}

bool SampleDropZone::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (isAudioFile (f)) return true;
    return false;
}

void SampleDropZone::fileDragEnter (const juce::StringArray&, int, int) { dragOver = true; repaint(); }
void SampleDropZone::fileDragExit (const juce::StringArray&)            { dragOver = false; repaint(); }

void SampleDropZone::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    for (const auto& f : files)
        if (isAudioFile (f))
        {
            analyse (juce::File (f));
            return;
        }
    repaint();
}

} // namespace verdant::ui
