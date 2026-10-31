#pragma once

// EnvelopeEditor: an AHDSR display with draggable handles (CONTRACT §7).
//   * Attack peak: x = attack.   * Hold end: x = hold.   * Decay end: x = decay, y = sustain.
//   * Release end: x = release.  * Mid-attack / mid-decay dots: y = attack / decay curve.
// Segment widths follow each time knob's travel (normalised, skewed), so a
// handle moves exactly like its knob. The curve uses the CONTRACT shape
// function; a playhead dot rides the curve from Engine::visuals().envValue.
// setEnvelope() switches which of Env 1..3 is shown (MOD page tabs).

#include "Widgets.h"

#include "engine/Params.h"

namespace verdant
{
class Processor;
}

namespace verdant::ui
{

class EnvelopeEditor : public juce::Component, public Tickable
{
public:
    EnvelopeEditor (Processor&, int env, juce::Colour colour = juce::Colour (palette::modEnv));

    void setEnvelope (int env);
    int getEnvelope() const noexcept { return env; }
    void setColour (juce::Colour);

    /** CONTRACT §3 segment shape: t^(1/(1+3c)) for c > 0 (fast start), t^(1+3|c|) for c < 0. */
    static float shape (float t, float c) noexcept;

    void tick() override;
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    enum Handle { None = -1, Attack, Hold, Decay, Release, ACurve, DCurve, NumHandles };

    struct Layout
    {
        juce::Rectangle<float> area;
        float segMax = 0;
        float xA0 = 0, xA = 0, xH = 0, xD = 0, xS = 0, xR = 0;   // segment boundaries
        float yTop = 0, yBottom = 0, ySus = 0;
    };

    float value (EnvParam) const;
    juce::RangedAudioParameter* param (EnvParam) const;
    Layout layout() const;
    juce::Point<float> handlePos (Handle, const Layout&) const;
    Handle handleAt (juce::Point<float>) const;
    juce::Path curvePath (const Layout&) const;
    juce::Point<float> playheadPos (const Layout&) const;
    juce::String handleText (Handle) const;

    Processor& processor;
    int env;
    juce::Colour colour;
    Handle hover = None, dragging = None;
    float dragStart[2] {};
    float shownValue = -1.0f, prevValue = 0.0f;
    int stageGuess = 0;   // 0 idle, 1 attack, 2 hold / decay, 3 sustain, 4 release
    uint32_t signature = 0;
    float frozenUnit = 0.0f;   // segment scale held during a drag (0 = zoom to fit)
};

} // namespace verdant::ui
