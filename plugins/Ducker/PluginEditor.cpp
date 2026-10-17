#include "PluginEditor.h"

namespace ducker
{

SidechainStatus::SidechainStatus (Processor& p) : processor (p)
{
    startTimerHz (4);
}

SidechainStatus::~SidechainStatus() { stopTimer(); }

void SidechainStatus::timerCallback()
{
    const bool now = processor.sidechainConnected.load (std::memory_order_relaxed);
    if (now != shown) { shown = now; repaint(); }
}

void SidechainStatus::paint (juce::Graphics& g)
{
    const auto* lf = dynamic_cast<ap::ui::LookAndFeel*> (&getLookAndFeel());
    const auto th = lf != nullptr ? lf->theme() : ap::ui::Theme {};
    auto r = getLocalBounds().toFloat();
    const auto dot = r.removeFromLeft (14.0f).withSizeKeepingCentre (9.0f, 9.0f);
    g.setColour (shown ? th.meterLow : th.textDim);
    g.fillEllipse (dot);
    g.setColour (th.textDim);
    g.setFont (ap::ui::LookAndFeel::labelFont());
    g.drawText (shown ? "Sidechain input connected" : "No sidechain input (host routing)", r, juce::Justification::centredLeft);
}

Editor::Editor (Processor& p)
    : ap::ui::EditorBase (p, p.apvts, "Ducker", "Sidechain compressor + ghost ducking", juce::Colour (0xff5ac8fa), 640, 1),
      processor (p),
      grMeter (p.gainReductionDb),
      status (p)
{
    setHeaderComponent (&grMeter, 180, 40);

    auto& det = addSection ("Detector", { ids::mode, ids::threshold, ids::ratio, ids::knee, ids::attack, ids::release,
                                          ids::lookahead, ids::scHpf, ids::listen });
    det.setTrailingComponent (&status, 150);
    addSection ("Output", { ids::makeup, ids::mix });
    addSection ("Ghost Ducking (tempo-synced, no sidechain needed)",
                { ids::ghostDiv, ids::ghostDepth, ids::ghostAttack, ids::ghostHold, ids::ghostRelease,
                  ids::ghostCurve, ids::ghostOffset, ids::ghostBpm });

    finishLayout();
}

} // namespace ducker
