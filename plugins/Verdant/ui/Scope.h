#pragma once

// Scope: the output oscilloscope / spectrum (CONTRACT §7), a small floating
// panel the editor shows in the bottom-right corner of every page when the
// header's Scope toggle is on. Click toggles the mode.
//
//   Scope     the last 2048 output samples (Engine::visuals().readScope), triggered on a rising
//             zero crossing, gently auto-scaled
//   Spectrum  FFT 2048 (verdant::FFT, Hann), log frequency 20 Hz .. 20 kHz, -90..0 dB, smoothed
//             per 30 Hz tick (fast rise, slow fall), with a peak-hold line
//
// Ticks (from the editor's timer) only while visible.

#include "Widgets.h"

#include "engine/FFT.h"

#include <vector>

namespace verdant
{
class Processor;
}

namespace verdant::ui
{

class Scope : public juce::Component, public juce::SettableTooltipClient, public Tickable
{
public:
    explicit Scope (Processor&);
    ~Scope() override;

    enum class Mode { Scope, Spectrum };
    void setMode (Mode);
    Mode getMode() const noexcept { return mode; }

    static constexpr int width = 300, height = 136;

    void tick() override;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void computeSpectrum();

    Processor& processor;
    Mode mode = Mode::Scope;
    static constexpr int kSize = 2048;
    std::vector<float> samples, scratch, window, re, im, spectrumDb, peakDb;
    FFT fft { kSize };
    float gain = 1.0f;
    int count = 0;
    bool silent = true;
};

} // namespace verdant::ui
