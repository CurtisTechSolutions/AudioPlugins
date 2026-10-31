#pragma once

// PaintCanvasPanel: the spectral-paint overlay (CONTRACT §7), a floating,
// modal-less panel opened over the OSC page by the oscillator's Paint button
// (OscPage::onPaintRequested -> open (osc)).
//
//   canvas   cols x rows (64 x 128): one column per wavetable frame, one row per harmonic
//            (harmonic 1 at the bottom); the rows view zooms to 16 / 32 / 64 / 128 harmonics
//   brushes  Paint (towards Amount), Erase, Smooth; Size 1-8 cells, Softness; left-drag uses the
//            brush, right-drag always erases
//   tools    Smooth all (smoothCanvas), Clear, Load image... (async chooser, luminance ->
//            imageToCanvas), a live waveform of the column under the mouse
//   Apply    BackgroundTasks::setPaintCanvas (osc, canvas) -> the oscillator's User table
//
// It starts from the oscillator's painted canvas when its user table is Painted,
// otherwise from a gentle starter spectrum.

#include "Widgets.h"

#include "engine/SpectralPaint.h"

#include <memory>

namespace verdant
{
class Processor;
}

namespace verdant::ui
{

class PaintCanvasPanel : public juce::Component
{
public:
    explicit PaintCanvasPanel (Processor&);
    ~PaintCanvasPanel() override;

    /** Loads oscillator `osc`'s canvas (or the starter), centres the panel in its parent and shows it. */
    void open (int osc);
    void close();
    int getOsc() const noexcept { return osc; }

    const PaintCanvas& getCanvas() const noexcept;
    void setCanvas (const PaintCanvas&);
    /** The starter canvas used when an oscillator has no painted table. */
    static PaintCanvas starterCanvas (int cols = 64, int rows = 128);

    static constexpr int panelWidth = 900, panelHeight = 540;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    class Editor;
    class Preview;

    void setStatus (const juce::String&, bool error = false);
    void loadImage();
    void apply();

    Processor& processor;
    int osc = 0;
    std::unique_ptr<Editor> editor;
    std::unique_ptr<Preview> preview;

    SegmentedControl brush { { "Paint", "Erase", "Smooth" } };
    SegmentedControl zoom { { "16", "32", "64", "128" } };
    juce::Slider size, softness, amount;
    IconButton smoothAll { IconButton::Icon::None, "Blur the whole canvas a little", "Smooth all" };
    IconButton clear { IconButton::Icon::Close, "Erase everything", "Clear" };
    IconButton loadButton { IconButton::Icon::Import, "Paint from an image (brightness = level, top = high harmonics)", "Load image..." };
    IconButton applyButton { IconButton::Icon::Brush, "Turn the canvas into this oscillator's wavetable", "Apply to Osc" };
    IconButton closeButton { IconButton::Icon::Close, "Close (Esc)" };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String status;
    bool statusError = false;
    juce::Rectangle<int> toolsArea;
};

} // namespace verdant::ui
