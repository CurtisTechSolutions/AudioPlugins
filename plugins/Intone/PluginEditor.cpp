#include "PluginEditor.h"

namespace intone
{

NoteToggleRow::NoteToggleRow (juce::AudioProcessorValueTreeState& apvts)
{
    for (int pc = 0; pc < 12; ++pc)
    {
        auto& b = buttons[(size_t) pc];
        b = std::make_unique<juce::TextButton> (ap::noteName (pc));
        b->setClickingTogglesState (true);
        addAndMakeVisible (*b);
        attachments[(size_t) pc] = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, ids::note (pc), *b);
    }
}

void NoteToggleRow::paint (juce::Graphics& g)
{
    const auto* lf = dynamic_cast<ap::ui::LookAndFeel*> (&getLookAndFeel());
    const auto th = lf != nullptr ? lf->theme() : ap::ui::Theme {};
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (th.panel);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (th.panelEdge);
    g.drawRoundedRectangle (r, 8.0f, 1.0f);
    g.setColour (th.accent);
    g.fillRoundedRectangle (juce::Rectangle<float> (10.0f, 7.0f, 3.0f, 12.0f), 1.5f);
    g.setColour (th.textDim);
    g.setFont (ap::ui::LookAndFeel::sectionFont());
    g.drawText ("CUSTOM SCALE NOTES  (used when Scale = Custom)", 20, 2, getWidth() - 30, 22, juce::Justification::centredLeft);
}

void NoteToggleRow::resized()
{
    auto area = getLocalBounds().reduced (10);
    area.removeFromTop (22);
    const int w = area.getWidth() / 12;
    for (int pc = 0; pc < 12; ++pc)
        buttons[(size_t) pc]->setBounds (area.getX() + pc * w + 2, area.getY(), w - 4, juce::jmin (30, area.getHeight()));
}

Editor::Editor (Processor& p)
    : ap::ui::EditorBase (p, p.apvts, "Intone", "Pitch correction + vocal chain", juce::Colour (0xffbf5af2), 700, 1),
      processor (p),
      readout (p.detectedHz, &p.targetHz),
      notes (p.apvts),
      grMeter (p.gainReductionDb),
      meter (p.outputLevel)
{
    setHeaderComponent (&readout, 230, 46);

    addSection ("Pitch Correction", { ids::key, ids::scale, ids::speed, ids::amount, ids::transpose,
                                      ids::tracking, ids::sensitivity, ids::mix });
    addCustomSection (&notes, 72);
    auto& dyn = addSection ("De-esser & Compressor", { ids::deessAmount, ids::deessFreq, ids::compThresh, ids::compRatio,
                                                       ids::compAttack, ids::compRelease, ids::compMakeup });
    dyn.setTrailingComponent (&grMeter, 110);
    auto& tone = addSection ("Tone & Output", { ids::lowCut, ids::body, ids::presence, ids::air, ids::output });
    tone.setTrailingComponent (&meter, 14);

    finishLayout();
}

} // namespace intone
