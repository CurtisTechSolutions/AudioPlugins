#include "PaintCanvas.h"

#include "../BackgroundTasks.h"
#include "../PluginProcessor.h"

namespace verdant::ui
{

namespace
{
    juce::Colour col (juce::uint32 c) { return juce::Colour (c); }

    /** Level -> colour: well, deep green, accent, near white. */
    juce::Colour heat (float v)
    {
        v = std::sqrt (juce::jlimit (0.0f, 1.0f, v));
        const auto base = col (palette::display), deep = col (0xff1a4d3a), accent = col (palette::accent), hot = col (0xffdffbec);
        if (v < 0.35f) return base.interpolatedWith (deep, v / 0.35f);
        if (v < 0.8f)  return deep.interpolatedWith (accent, (v - 0.35f) / 0.45f);
        return accent.interpolatedWith (hot, (v - 0.8f) / 0.2f);
    }
}

//==============================================================================
/** The canvas itself: cells drawn from an image, brush strokes, the hovered column. */
class PaintCanvasPanel::Editor : public juce::Component
{
public:
    enum Brush { Paint, Erase, Smooth };

    PaintCanvas canvas;
    int rowsShown = 64;
    Brush brushType = Paint;
    float brushSize = 2.0f, softness = 0.5f, amount = 0.8f;
    std::function<void (int column)> onColumn;   // hovered / edited column (for the preview)

    void setCanvas (const PaintCanvas& c)
    {
        canvas = c;
        if (canvas.amp.size() < (size_t) canvas.cols * (size_t) canvas.rows)
            canvas.amp.resize ((size_t) canvas.cols * (size_t) canvas.rows, 0.0f);
        rowsShown = juce::jlimit (1, canvas.rows, rowsShown);
        rebuildImage();
    }

    void setRowsShown (int r)
    {
        rowsShown = juce::jlimit (1, juce::jmax (1, canvas.rows), r);
        rebuildImage();
    }

    void rebuildImage()
    {
        image = juce::Image (juce::Image::ARGB, juce::jmax (1, canvas.cols), juce::jmax (1, rowsShown), false);
        updateCells (0, canvas.cols - 1, 0, rowsShown - 1);
    }

    void updateCells (int c0, int c1, int r0, int r1)
    {
        c0 = juce::jmax (0, c0); c1 = juce::jmin (canvas.cols - 1, c1);
        r0 = juce::jmax (0, r0); r1 = juce::jmin (rowsShown - 1, r1);
        juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);
        for (int c = c0; c <= c1; ++c)
            for (int r = r0; r <= r1; ++r)
                data.setPixelColour (c, rowsShown - 1 - r, heat (canvas.amp[(size_t) c * (size_t) canvas.rows + (size_t) r]));
        repaint();
    }

    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (1.0f).withTrimmedLeft (30.0f).withTrimmedBottom (18.0f); }

    juce::Point<float> cellAt (juce::Point<float> p) const
    {
        const auto a = plot();
        return { (p.x - a.getX()) / a.getWidth() * (float) canvas.cols,
                 (a.getBottom() - p.y) / a.getHeight() * (float) rowsShown };
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        paintDisplay (g, bounds, col (palette::accent));
        const auto a = plot();

        g.saveState();
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
        g.drawImage (image, a, juce::RectanglePlacement::stretchToFit);
        g.restoreState();

        // Grid: frames every 8 columns, harmonic rows at powers of two.
        g.setFont (font (9.0f));
        for (int c = 8; c < canvas.cols; c += 8)
        {
            const float x = std::round (a.getX() + a.getWidth() * (float) c / (float) canvas.cols);
            g.setColour (col (palette::background).withAlpha (0.45f));
            g.fillRect (juce::Rectangle<float> (x, a.getY(), 1.0f, a.getHeight()));
            g.setColour (col (palette::textFaint));
            g.drawText (juce::String (c + 1), juce::Rectangle<float> (x - 14.0f, a.getBottom() + 3.0f, 28.0f, 11.0f), juce::Justification::centred, false);
        }
        g.drawText ("FRAME", juce::Rectangle<float> (a.getX(), a.getBottom() + 3.0f, 40.0f, 11.0f), juce::Justification::centredLeft, false);
        for (int h = 1; h <= rowsShown; h *= 2)
        {
            const float y = a.getBottom() - a.getHeight() * ((float) h - 0.5f) / (float) rowsShown;
            g.setColour (col (palette::textFaint));
            g.drawText (juce::String (h), juce::Rectangle<float> (bounds.getX() + 2.0f, y - 6.0f, 24.0f, 12.0f), juce::Justification::centredRight, false);
        }

        // Hovered column + brush outline.
        if (hoverCell.x >= 0.0f)
        {
            const int c = juce::jlimit (0, canvas.cols - 1, (int) hoverCell.x);
            const float cw = a.getWidth() / (float) canvas.cols, ch = a.getHeight() / (float) rowsShown;
            g.setColour (col (palette::text).withAlpha (0.08f));
            g.fillRect (juce::Rectangle<float> (a.getX() + cw * (float) c, a.getY(), cw, a.getHeight()));
            const auto centre = juce::Point<float> (a.getX() + hoverCell.x * cw, a.getBottom() - hoverCell.y * ch);
            const float rx = brushSize * cw, ry = brushSize * ch;
            g.setColour (col (palette::text).withAlpha (0.8f));
            g.drawEllipse (juce::Rectangle<float> (2.0f * rx, 2.0f * ry).withCentre (centre), 1.2f);
            g.setColour (col (palette::text).withAlpha (0.35f));
            g.drawEllipse (juce::Rectangle<float> (2.0f * rx * (1.0f - softness), 2.0f * ry * (1.0f - softness)).withCentre (centre), 1.0f);
        }
        g.setColour (col (0xff2c323c));
        g.drawRect (a, 1.0f);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        hoverCell = cellAt (e.position);
        if (! plot().contains (e.position)) hoverCell = { -1.0f, -1.0f };
        else if (onColumn) onColumn ((int) hoverCell.x);
        repaint();
    }
    void mouseExit (const juce::MouseEvent&) override { hoverCell = { -1.0f, -1.0f }; repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        erasing = e.mods.isPopupMenu();
        last = cellAt (e.position);
        stamp (last);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto now = cellAt (e.position);
        // Interpolate in half-cell steps so fast strokes stay continuous.
        const float dist = now.getDistanceFrom (last);
        const int steps = juce::jmax (1, (int) std::ceil (dist * 2.0f));
        for (int i = 1; i <= steps; ++i)
            stamp (last + (now - last) * ((float) i / (float) steps));
        last = now;
        hoverCell = now;
        if (onColumn) onColumn (juce::jlimit (0, canvas.cols - 1, (int) now.x));
    }

private:
    void stamp (juce::Point<float> centre)
    {
        const int c0 = (int) std::floor (centre.x - brushSize), c1 = (int) std::ceil (centre.x + brushSize);
        const int r0 = (int) std::floor (centre.y - brushSize), r1 = (int) std::ceil (centre.y + brushSize);
        const auto mode = erasing ? Erase : brushType;
        std::vector<float> before;
        if (mode == Smooth) before = canvas.amp;   // the blur reads the unmodified neighbours
        for (int c = juce::jmax (0, c0); c <= juce::jmin (canvas.cols - 1, c1); ++c)
            for (int r = juce::jmax (0, r0); r <= juce::jmin (rowsShown - 1, r1); ++r)
            {
                const float d = juce::Point<float> ((float) c + 0.5f, (float) r + 0.5f).getDistanceFrom (centre) / juce::jmax (0.5f, brushSize);
                if (d > 1.0f) continue;
                const float edge = juce::jmax (0.001f, softness);
                const float w = d <= 1.0f - edge ? 1.0f : (1.0f - d) / edge;
                auto& v = canvas.amp[(size_t) c * (size_t) canvas.rows + (size_t) r];
                switch (mode)
                {
                    case Paint:  v += (amount - v) * w * 0.6f; break;
                    case Erase:  v *= 1.0f - w * 0.8f; break;
                    case Smooth:
                    {
                        float sum = 0.0f; int n = 0;
                        for (int dc = -1; dc <= 1; ++dc)
                            for (int dr = -1; dr <= 1; ++dr)
                            {
                                const int cc = c + dc, rr = r + dr;
                                if (cc < 0 || rr < 0 || cc >= canvas.cols || rr >= canvas.rows) continue;
                                sum += before[(size_t) cc * (size_t) canvas.rows + (size_t) rr];
                                ++n;
                            }
                        v += (sum / (float) juce::jmax (1, n) - v) * w * 0.5f;
                        break;
                    }
                }
                v = juce::jlimit (0.0f, 1.0f, v);
            }
        updateCells (c0, c1, r0, r1);
    }

    juce::Image image;
    juce::Point<float> hoverCell { -1.0f, -1.0f }, last;
    bool erasing = false;
};

//==============================================================================
/** One column's waveform: sum of its harmonics over one cycle. */
class PaintCanvasPanel::Preview : public juce::Component
{
public:
    void show (const PaintCanvas& canvas, int c)
    {
        column = juce::jlimit (0, juce::jmax (0, canvas.cols - 1), c);
        wave.assign (points, 0.0f);
        float peak = 0.0f;
        for (int i = 0; i < points; ++i)
        {
            const float t = (float) i / (float) (points - 1);
            float s = 0.0f;
            for (int r = 0; r < canvas.rows; ++r)
            {
                const float a = canvas.amp[(size_t) column * (size_t) canvas.rows + (size_t) r];
                if (a > 0.0001f)
                    s += a * std::sin (juce::MathConstants<float>::twoPi * (float) (r + 1) * t);
            }
            wave[(size_t) i] = s;
            peak = juce::jmax (peak, std::abs (s));
        }
        if (peak > 1.0e-6f)
            for (auto& s : wave) s /= peak;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        paintDisplay (g, bounds, col (palette::accent));
        const auto a = bounds.reduced (10.0f, 14.0f);
        g.setColour (col (palette::grid).brighter (0.15f));
        g.fillRect (juce::Rectangle<float> (a.getX(), std::round (a.getCentreY()), a.getWidth(), 1.0f));
        juce::Path p;
        for (size_t i = 0; i < wave.size(); ++i)
        {
            const float x = a.getX() + a.getWidth() * (float) i / (float) (wave.size() - 1);
            const float y = a.getCentreY() - wave[i] * a.getHeight() * 0.5f;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        const auto accent = col (palette::accent);
        g.setColour (accent.withAlpha (0.18f));
        g.strokePath (p, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (accent);
        g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (col (palette::textFaint));
        g.setFont (font (9.5f, true).withExtraKerningFactor (0.08f));
        g.drawText ("FRAME " + juce::String (column + 1), bounds.reduced (8.0f, 3.0f), juce::Justification::topLeft, false);
    }

private:
    static constexpr int points = 160;
    std::vector<float> wave;
    int column = 0;
};

//==============================================================================
PaintCanvasPanel::PaintCanvasPanel (Processor& p) : processor (p)
{
    editor = std::make_unique<Editor>();
    preview = std::make_unique<Preview>();
    addAndMakeVisible (*editor);
    addAndMakeVisible (*preview);
    editor->onColumn = [this] (int c) { preview->show (editor->canvas, c); };

    brush.setFontSize (Theme::fontSmall);
    brush.onChange = [this] (int i) { editor->brushType = (Editor::Brush) i; };
    addAndMakeVisible (brush);
    zoom.setFontSize (Theme::fontSmall);
    zoom.setSelected (2, juce::dontSendNotification);
    zoom.onChange = [this] (int i) { editor->setRowsShown (16 << i); };
    addAndMakeVisible (zoom);

    auto setupSlider = [this] (juce::Slider& s, double lo, double hi, double step, double value, const juce::String& tip)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        s.setRange (lo, hi, step);
        s.setValue (value, juce::dontSendNotification);
        s.setTooltip (tip);
        s.onValueChange = [this] { editor->brushSize = (float) size.getValue(); editor->softness = (float) softness.getValue();
                                   editor->amount = (float) amount.getValue(); repaint (toolsArea); };
        addAndMakeVisible (s);
    };
    setupSlider (size, 1.0, 8.0, 1.0, 2.0, "Brush size (cells)");
    setupSlider (softness, 0.0, 1.0, 0.0, 0.5, "Brush softness");
    setupSlider (amount, 0.05, 1.0, 0.0, 0.8, "Paint level");

    smoothAll.onClick = [this] { smoothCanvas (editor->canvas, 0.25f); editor->rebuildImage(); preview->show (editor->canvas, 0); };
    clear.onClick = [this] { std::fill (editor->canvas.amp.begin(), editor->canvas.amp.end(), 0.0f); editor->rebuildImage(); preview->show (editor->canvas, 0); };
    loadButton.onClick = [this] { loadImage(); };
    applyButton.onClick = [this] { apply(); };
    applyButton.setToggleState (true, juce::dontSendNotification);
    closeButton.setFrameless (true);
    closeButton.onClick = [this] { close(); };
    for (auto* b : { &smoothAll, &clear, &loadButton, &applyButton, &closeButton })
        addAndMakeVisible (*b);

    setWantsKeyboardFocus (true);
    setSize (panelWidth, panelHeight);
}

PaintCanvasPanel::~PaintCanvasPanel() = default;

const PaintCanvas& PaintCanvasPanel::getCanvas() const noexcept { return editor->canvas; }

void PaintCanvasPanel::setCanvas (const PaintCanvas& c)
{
    editor->setCanvas (c);
    preview->show (editor->canvas, editor->canvas.cols / 2);
}

PaintCanvas PaintCanvasPanel::starterCanvas (int cols, int rows)
{
    // A soft saw-like spectrum that brightens across the frames, with a formant sweeping upwards.
    PaintCanvas c;
    c.cols = cols;
    c.rows = rows;
    c.amp.assign ((size_t) cols * (size_t) rows, 0.0f);
    for (int col_ = 0; col_ < cols; ++col_)
    {
        const float t = (float) col_ / (float) juce::jmax (1, cols - 1);
        const float limit = 6.0f + t * 58.0f;
        for (int r = 0; r < rows; ++r)
        {
            const float h = (float) (r + 1);
            float a = std::pow (1.0f / h, 1.5f - 0.9f * t) * (h <= limit ? 1.0f : std::exp (-(h - limit) * 0.3f));
            a += 0.55f * std::exp (-std::pow ((h - (5.0f + t * 26.0f)) / (2.0f + 2.0f * t), 2.0f));
            c.amp[(size_t) col_ * (size_t) rows + (size_t) r] = juce::jlimit (0.0f, 1.0f, a);
        }
    }
    return c;
}

void PaintCanvasPanel::open (int o)
{
    osc = juce::jlimit (0, 1, o);
    const auto& table = processor.patchExtras().userTables[(size_t) osc];
    if (table.source == UserTable::Source::Painted && table.canvas.cols > 0 && table.canvas.rows > 0
        && table.canvas.amp.size() >= (size_t) table.canvas.cols * (size_t) table.canvas.rows)
        setCanvas (table.canvas);
    else
        setCanvas (starterCanvas());
    applyButton.setButtonText (osc == 0 ? "Apply to Osc A" : "Apply to Osc B");
    status.clear();

    if (auto* parent = getParentComponent())
        setBounds (juce::Rectangle<int> (panelWidth, panelHeight).withCentre (parent->getLocalBounds().getCentre()));
    setVisible (true);
    toFront (true);
    repaint();
}

void PaintCanvasPanel::close()
{
    setVisible (false);
}

bool PaintCanvasPanel::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { close(); return true; }
    return false;
}

void PaintCanvasPanel::setStatus (const juce::String& s, bool error)
{
    status = s;
    statusError = error;
    repaint();
}

void PaintCanvasPanel::loadImage()
{
    chooser = std::make_unique<juce::FileChooser> ("Paint from an image", juce::File(), "*.png;*.jpg;*.jpeg;*.gif");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe = juce::Component::SafePointer<PaintCanvasPanel> (this)] (const juce::FileChooser& fc)
        {
            if (safe == nullptr) return;
            const auto file = fc.getResult();
            if (! file.existsAsFile()) return;
            auto img = juce::ImageFileFormat::loadFrom (file);
            if (! img.isValid())
            {
                safe->setStatus ("Could not read " + file.getFileName(), true);
                return;
            }
            if (img.getWidth() > 512 || img.getHeight() > 512)
                img = img.rescaled (juce::jmin (512, img.getWidth()), juce::jmin (512, img.getHeight()), juce::Graphics::mediumResamplingQuality);
            const int w = img.getWidth(), h = img.getHeight();
            std::vector<float> lum ((size_t) w * (size_t) h);
            const juce::Image::BitmapData data (img, juce::Image::BitmapData::readOnly);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    const auto c = data.getPixelColour (x, y);
                    lum[(size_t) y * (size_t) w + (size_t) x] = c.getPerceivedBrightness() * c.getFloatAlpha();
                }
            const auto& cur = safe->editor->canvas;
            safe->setCanvas (imageToCanvas (lum.data(), w, h, cur.cols, cur.rows));
            safe->setStatus ("Painted from " + file.getFileName());
        });
}

void PaintCanvasPanel::apply()
{
    setStatus ("Building the wavetable...");
    const int target = osc;
    processor.tasks().setPaintCanvas (target, editor->canvas, [safe = juce::Component::SafePointer<PaintCanvasPanel> (this), target]
    {
        if (safe != nullptr)
            safe->setStatus (juce::String ("Applied: Osc ") + (target == 0 ? "A" : "B") + " now plays the painted table");
    });
}

//==============================================================================
void PaintCanvasPanel::resized()
{
    auto r = getLocalBounds().reduced (16).withTrimmedTop (Theme::sectionHeader);
    closeButton.setBounds (getLocalBounds().removeFromTop (Theme::sectionHeader + 8).removeFromRight (40).withSizeKeepingCentre (24, 24).translated (-6, 2));

    toolsArea = r.removeFromRight (220);
    r.removeFromRight (16);
    editor->setBounds (r);

    auto t = toolsArea;
    t.removeFromTop (16);   // caption "Brush"
    brush.setBounds (t.removeFromTop (26));
    t.removeFromTop (12);
    auto sliderRow = [&t] (juce::Slider& s)
    {
        t.removeFromTop (14);   // label painted by paint()
        s.setBounds (t.removeFromTop (20));
        t.removeFromTop (6);
    };
    sliderRow (size);
    sliderRow (softness);
    sliderRow (amount);
    t.removeFromTop (6);
    t.removeFromTop (16);   // caption "Harmonics shown"
    zoom.setBounds (t.removeFromTop (24));
    t.removeFromTop (12);
    auto row = t.removeFromTop (26);
    smoothAll.setBounds (row.removeFromLeft ((row.getWidth() - 8) / 2));
    row.removeFromLeft (8);
    clear.setBounds (row);
    t.removeFromTop (8);
    loadButton.setBounds (t.removeFromTop (26));
    t.removeFromTop (8);

    applyButton.setBounds (t.removeFromBottom (32));
    t.removeFromBottom (12);
    t.removeFromTop (12);
    preview->setBounds (t);
}

void PaintCanvasPanel::paint (juce::Graphics& g)
{
    // Drop shadow, then the card.
    const auto r = getLocalBounds().toFloat();
    for (int i = 0; i < 6; ++i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.10f - 0.015f * (float) i));
        g.drawRoundedRectangle (r.reduced (1.0f).translated (0.0f, 2.0f).expanded ((float) i), Theme::cardRadius + (float) i, 2.0f);
    }
    paintSection (g, getLocalBounds(), juce::String ("Spectral paint  -  Osc ") + (osc == 0 ? "A" : "B"), osc == 0 ? col (palette::accent) : col (palette::secondary));
    g.setColour (col (palette::accent).withAlpha (0.35f));
    g.drawRoundedRectangle (r.reduced (0.5f), Theme::cardRadius, 1.0f);

    // Header hint / status.
    auto header = getLocalBounds().removeFromTop (Theme::sectionHeader + 8).withTrimmedTop (8).reduced (16, 0).withTrimmedRight (40);
    g.setFont (font (Theme::fontSmall));
    g.setColour (status.isNotEmpty() ? (statusError ? col (palette::warning) : col (palette::accent)) : col (palette::textFaint));
    g.drawText (status.isNotEmpty() ? status : juce::String ("Columns are frames, rows are harmonics. Left-drag paints, right-drag erases."),
                header, juce::Justification::centredRight, false);

    // Tool captions and values.
    auto caption = [&g] (const juce::String& text, juce::Rectangle<int> area, const juce::String& value = {})
    {
        g.setFont (font (10.0f, true).withExtraKerningFactor (0.1f));
        g.setColour (col (palette::textFaint));
        g.drawText (text.toUpperCase(), area, juce::Justification::centredLeft, false);
        if (value.isNotEmpty())
        {
            g.setColour (col (palette::text));
            g.setFont (font (Theme::fontSmall));
            g.drawText (value, area, juce::Justification::centredRight, false);
        }
    };
    caption ("Brush", brush.getBounds().translated (0, -16).withHeight (14));
    caption ("Size", size.getBounds().translated (0, -14).withHeight (14), juce::String ((int) size.getValue()) + (size.getValue() > 1.5 ? " cells" : " cell"));
    caption ("Softness", softness.getBounds().translated (0, -14).withHeight (14), juce::String (juce::roundToInt (softness.getValue() * 100.0)) + " %");
    caption ("Level", amount.getBounds().translated (0, -14).withHeight (14), juce::String (juce::roundToInt (amount.getValue() * 100.0)) + " %");
    caption ("Harmonics shown", zoom.getBounds().translated (0, -16).withHeight (14));
    caption ("Column", preview->getBounds().translated (0, -14).withHeight (14));
}

} // namespace verdant::ui
