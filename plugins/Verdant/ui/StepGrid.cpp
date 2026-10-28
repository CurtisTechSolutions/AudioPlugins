#include "StepGrid.h"

#include "../PluginProcessor.h"

namespace verdant::ui
{

namespace
{
    juce::Colour col (juce::uint32 c) { return juce::Colour (c); }

    constexpr float kLabelW = 62.0f, kNumberH = 20.0f, kLaneGap = 10.0f, kGateH = 26.0f;
    constexpr const char* kLaneNames[] = { "PITCH", "VELOCITY", "GATE", "MOD" };
}

StepGrid::StepGrid (Processor& p) : processor (p)
{
    read();
    setTooltip ("Click or drag to draw steps, double-click resets a step; click a step number to set the length");
}

StepGrid::~StepGrid()
{
    endGestures();
}

juce::RangedAudioParameter* StepGrid::param (int step, int lane) const
{
    static constexpr SeqStepParam map[] = { SeqStepParam::Pitch, SeqStepParam::Vel, SeqStepParam::Gate, SeqStepParam::Mod };
    return processor.apvts.getParameter (info (seqStepParam (step, map[lane])).id);
}

void StepGrid::read()
{
    for (int l = 0; l < NumLanes; ++l)
        for (int s = 0; s < kSteps; ++s)
            if (auto* prm = param (s, l))
                values[(size_t) l][(size_t) s] = prm->convertFrom0to1 (prm->getValue());
    auto raw = [this] (P p) { auto* r = processor.apvts.getRawParameterValue (info (p).id); return r != nullptr ? r->load (std::memory_order_relaxed) : 0.0f; };
    steps = juce::jlimit (1, kSteps, (int) std::lround (raw (P::SeqSteps)));
    snap = raw (P::SeqSnap) >= 0.5f;
    running = raw (P::SeqOn) >= 0.5f;
    const int st = processor.engine().visuals().seqStep.load (std::memory_order_relaxed);
    playing = running && processor.engine().visuals().activeVoices.load (std::memory_order_relaxed) > 0 ? st : -1;
}

void StepGrid::tick()
{
    const auto oldValues = values;
    const int oldSteps = steps, oldPlaying = playing;
    const bool oldSnap = snap, oldRunning = running;
    read();
    if (oldValues != values || oldSteps != steps || oldPlaying != playing || oldSnap != snap || oldRunning != running)
        repaint();
}

//==============================================================================
void StepGrid::resized()
{
    grid = getLocalBounds().toFloat();
}

juce::Rectangle<float> StepGrid::numberRow() const
{
    return grid.withTrimmedLeft (kLabelW).withHeight (kNumberH);
}

juce::Rectangle<float> StepGrid::laneArea (int lane) const
{
    auto r = grid.withTrimmedLeft (kLabelW).withTrimmedTop (kNumberH + 4.0f);
    const float flexible = r.getHeight() - kGateH - 3.0f * kLaneGap;
    const float pitchH = flexible * 0.5f, barH = flexible * 0.25f;
    const float heights[] = { pitchH, barH, kGateH, barH };
    for (int l = 0; l < lane; ++l)
        r.removeFromTop (heights[l] + kLaneGap);
    return r.withHeight (heights[lane]);
}

juce::Rectangle<float> StepGrid::column (int step, juce::Rectangle<float> lane) const
{
    const float w = lane.getWidth() / (float) kSteps;
    return { lane.getX() + w * (float) step, lane.getY(), w, lane.getHeight() };
}

int StepGrid::stepAt (float x) const
{
    const auto r = numberRow();
    return juce::jlimit (0, kSteps - 1, (int) std::floor ((x - r.getX()) / (r.getWidth() / (float) kSteps)));
}

int StepGrid::laneAt (juce::Point<float> p) const
{
    for (int l = 0; l < NumLanes; ++l)
        if (laneArea (l).expanded (0.0f, kLaneGap * 0.5f).contains (p)) return l;
    return -1;
}

float StepGrid::valueFor (int lane, float y) const
{
    const auto a = laneArea (lane).reduced (0.0f, 4.0f);
    const float t = juce::jlimit (0.0f, 1.0f, (a.getBottom() - y) / a.getHeight());
    if (lane == Pitch) return (float) juce::roundToInt (-24.0f + 48.0f * t);
    if (lane == Gate)  return gatePaint;
    return std::round (100.0f * t);
}

void StepGrid::setValue (int step, int lane, float value)
{
    auto* prm = param (step, lane);
    if (prm == nullptr) return;
    if (touched.insert (prm).second)
        prm->beginChangeGesture();
    prm->setValueNotifyingHost (prm->convertTo0to1 (value));
    values[(size_t) lane][(size_t) step] = value;
    repaint();
}

void StepGrid::endGestures()
{
    for (auto* prm : touched)
        prm->endChangeGesture();
    touched.clear();
}

//==============================================================================
void StepGrid::paint (juce::Graphics& g)
{
    const auto accent = col (palette::accent);
    const float colW = numberRow().getWidth() / (float) kSteps;

    // Step numbers (click sets the length) with the playhead.
    for (int s = 0; s < kSteps; ++s)
    {
        const auto r = column (s, numberRow()).reduced (2.0f, 1.0f);
        const bool active = s < steps;
        if (s == playing)
        {
            g.setColour (accent);
            g.fillRoundedRectangle (r, 4.0f);
        }
        else if (hoverLane == -2 && hoverStep == s)
        {
            g.setColour (col (palette::controlHover));
            g.fillRoundedRectangle (r, 4.0f);
        }
        if (s == steps - 1 && s != playing)
        {
            g.setColour (accent.withAlpha (0.7f));
            g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
        }
        g.setColour (s == playing ? col (palette::background) : (active ? col (palette::text) : col (palette::textFaint)));
        g.setFont (font (10.5f, s == playing || s % 4 == 0));
        g.drawText (juce::String (s + 1), r, juce::Justification::centred, false);
    }

    for (int l = 0; l < NumLanes; ++l)
    {
        const auto lane = laneArea (l);
        // Lane label + well.
        g.setColour (col (palette::textFaint));
        g.setFont (font (10.0f, true).withExtraKerningFactor (0.06f));
        auto label = juce::Rectangle<float> (grid.getX(), lane.getY(), kLabelW - 4.0f, juce::jmin (lane.getHeight(), 16.0f));
        g.drawFittedText (kLaneNames[l], label.toNearestInt(), juce::Justification::centredLeft, 1, 0.9f);
        if (l == Pitch)
        {
            g.setFont (font (9.5f));
            g.drawText (snap ? "degrees" : "semitones", label.translated (0.0f, 13.0f), juce::Justification::centredLeft, false);
        }

        g.setGradientFill (juce::ColourGradient (col (palette::well), 0.0f, lane.getY(), col (palette::display), 0.0f, lane.getBottom(), false));
        g.fillRoundedRectangle (lane, Theme::radius);
        g.setColour (col (0xff2c323c));
        g.drawRoundedRectangle (lane.reduced (0.5f), Theme::radius, 1.0f);

        // Beat separators every 4 steps.
        for (int s = 4; s < kSteps; s += 4)
        {
            const float x = std::round (lane.getX() + colW * (float) s);
            g.setColour (col (palette::grid).brighter (0.12f));
            g.fillRect (juce::Rectangle<float> (x, lane.getY() + 3.0f, 1.0f, lane.getHeight() - 6.0f));
        }

        const auto inner = lane.reduced (0.0f, 4.0f);
        if (l == Pitch)
        {
            for (int st : { -24, -12, 12, 24 })
            {
                const float y = std::round (inner.getBottom() - inner.getHeight() * (float) (st + 24) / 48.0f);
                g.setColour (col (palette::grid));
                g.fillRect (juce::Rectangle<float> (lane.getX() + 4.0f, y, lane.getWidth() - 8.0f, 1.0f));
            }
            g.setColour (col (palette::grid).brighter (0.2f));
            g.fillRect (juce::Rectangle<float> (lane.getX() + 4.0f, std::round (inner.getCentreY()), lane.getWidth() - 8.0f, 1.0f));
            g.setColour (col (palette::textFaint));
            g.setFont (font (9.0f));
            for (int st : { 24, 12, 0, -12, -24 })
            {
                const float y = inner.getBottom() - inner.getHeight() * (float) (st + 24) / 48.0f;
                g.drawText ((st > 0 ? "+" : "") + juce::String (st), juce::Rectangle<float> (grid.getX() + kLabelW - 34.0f, y - 6.0f, 28.0f, 12.0f),
                            juce::Justification::centredRight, false);
            }
        }

        for (int s = 0; s < kSteps; ++s)
        {
            const auto c = column (s, inner).reduced (colW * 0.16f, 0.0f);
            const bool active = s < steps;
            const bool gated = values[Gate][(size_t) s] >= 0.5f;
            const float alpha = active ? (gated || l == Gate || l == Mod ? 1.0f : 0.45f) : 0.22f;
            const auto colour = (l == Mod ? col (palette::modPhysics) : accent).withAlpha (alpha);
            const float val = values[(size_t) l][(size_t) s];

            if (s == playing)
            {
                g.setColour (accent.withAlpha (0.10f));
                g.fillRect (column (s, lane).reduced (1.0f, 2.0f));
            }
            if (s == hoverStep && l == hoverLane)
            {
                g.setColour (col (palette::text).withAlpha (0.05f));
                g.fillRect (column (s, lane).reduced (1.0f, 2.0f));
            }

            switch (l)
            {
                case Pitch:
                {
                    const float y0 = inner.getCentreY();
                    const float y1 = inner.getBottom() - inner.getHeight() * (val + 24.0f) / 48.0f;
                    auto bar = juce::Rectangle<float>::leftTopRightBottom (c.getX(), juce::jmin (y0, y1), c.getRight(), juce::jmax (y0, y1));
                    if (bar.getHeight() < 3.0f) bar = bar.withSizeKeepingCentre (bar.getWidth(), 3.0f);
                    g.setColour (colour.withAlpha (alpha * 0.25f));
                    g.fillRoundedRectangle (bar, 3.0f);
                    g.setColour (colour);
                    g.fillRoundedRectangle (juce::Rectangle<float> (c.getX(), y1 - 2.0f, c.getWidth(), 4.0f), 2.0f);
                    const int iv = juce::roundToInt (val);
                    g.setColour (active ? col (palette::text).withAlpha (gated ? 0.9f : 0.45f) : col (palette::textFaint));
                    g.setFont (font (9.5f, true));
                    const float ty = val >= 0.0f ? y1 - 15.0f : y1 + 3.0f;
                    g.drawText (iv == 0 ? juce::String ("0") : (iv > 0 ? "+" : "") + juce::String (iv),
                                juce::Rectangle<float> (c.getX() - 6.0f, ty, c.getWidth() + 12.0f, 12.0f), juce::Justification::centred, false);
                    break;
                }
                case Velocity:
                case Mod:
                {
                    const float h = inner.getHeight() * juce::jlimit (0.0f, 1.0f, val / 100.0f);
                    const auto bar = juce::Rectangle<float> (c.getX(), inner.getBottom() - h, c.getWidth(), h);
                    g.setGradientFill (juce::ColourGradient (colour, 0.0f, bar.getY(), colour.withAlpha (alpha * 0.35f), 0.0f, inner.getBottom(), false));
                    g.fillRoundedRectangle (bar, 3.0f);
                    g.setColour (colour);
                    g.fillRoundedRectangle (bar.withHeight (juce::jmin (3.0f, bar.getHeight())), 1.5f);
                    break;
                }
                case Gate:
                default:
                {
                    const auto pill = c.withSizeKeepingCentre (c.getWidth(), 12.0f);
                    if (gated)
                    {
                        g.setColour (colour);
                        g.fillRoundedRectangle (pill, 6.0f);
                    }
                    else
                    {
                        g.setColour (col (palette::knobTrack).withAlpha (active ? 1.0f : 0.5f));
                        g.drawRoundedRectangle (pill.reduced (0.5f), 6.0f, 1.0f);
                    }
                    break;
                }
            }
        }

        // Steps beyond the length.
        if (steps < kSteps)
        {
            const auto off = juce::Rectangle<float>::leftTopRightBottom (column (steps, lane).getX(), lane.getY(), lane.getRight(), lane.getBottom());
            g.setColour (col (palette::background).withAlpha (0.35f));
            g.fillRoundedRectangle (off.reduced (1.0f), Theme::radius);
        }
    }
}

//==============================================================================
void StepGrid::mouseMove (const juce::MouseEvent& e)
{
    const int s = stepAt (e.position.x);
    const int l = numberRow().contains (e.position) ? -2 : laneAt (e.position);
    if (s != hoverStep || l != hoverLane)
    {
        hoverStep = s;
        hoverLane = l;
        repaint();
    }
}

void StepGrid::mouseExit (const juce::MouseEvent&)
{
    hoverStep = hoverLane = -1;
    repaint();
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    if (e.position.x < grid.getX() + kLabelW) return;
    const int s = stepAt (e.position.x);

    if (numberRow().contains (e.position))
    {
        if (auto* prm = processor.apvts.getParameter (info (P::SeqSteps).id))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 ((float) (s + 1)));
            prm->endChangeGesture();
        }
        read();
        repaint();
        return;
    }

    dragLane = laneAt (e.position);
    if (dragLane < 0) return;
    if (dragLane == Gate)
        gatePaint = values[Gate][(size_t) s] >= 0.5f ? 0.0f : 1.0f;
    lastDragStep = s;
    setValue (s, dragLane, valueFor (dragLane, e.position.y));
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (dragLane < 0) return;
    const int s = stepAt (e.position.x);
    const float v = valueFor (dragLane, e.position.y);
    // Fill every step between the last and this one so fast drags leave no gaps.
    const int from = lastDragStep < 0 ? s : lastDragStep;
    for (int k = juce::jmin (from, s); k <= juce::jmax (from, s); ++k)
        if (k == s || dragLane == Gate || k != from)
            setValue (k, dragLane, v);
    lastDragStep = s;
    hoverStep = s;
}

void StepGrid::mouseUp (const juce::MouseEvent&)
{
    dragLane = -1;
    lastDragStep = -1;
    endGestures();
}

void StepGrid::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int l = laneAt (e.position);
    if (l < 0 || l == Gate || e.position.x < grid.getX() + kLabelW) return;
    const int s = stepAt (e.position.x);
    static constexpr SeqStepParam map[] = { SeqStepParam::Pitch, SeqStepParam::Vel, SeqStepParam::Gate, SeqStepParam::Mod };
    setValue (s, l, info (seqStepParam (s, map[l])).def);
    endGestures();
}

} // namespace verdant::ui
