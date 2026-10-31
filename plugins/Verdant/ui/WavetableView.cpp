#include "WavetableView.h"

#include "../PluginProcessor.h"
#include "engine/WavetableFactory.h"

#include <array>
#include <mutex>

namespace verdant::ui
{

namespace
{
    juce::Colour col (juce::uint32 c) { return juce::Colour (c); }
    constexpr int kMaxRows = 28;
    constexpr int kUserIndex = kNumFactoryTables;   // {o}_table choice "User"

}

//==============================================================================
std::shared_ptr<const WavetableView::Sketch> WavetableView::makeSketch (const float* frames, int numFrames, int frameSize, const juce::String& name)
{
    auto s = std::make_shared<Sketch>();
    s->name = name;
    s->numFrames = numFrames;
    if (frames == nullptr || numFrames <= 0 || frameSize <= 0)
        return s;

    s->rows = juce::jmin (numFrames, kMaxRows);
    s->data.assign ((size_t) s->rows * Sketch::points, 0.0f);
    const int box = juce::jmax (1, frameSize / Sketch::points);
    float peak = 0.0f;
    for (int r = 0; r < s->rows; ++r)
    {
        const int f = s->rows == 1 ? 0 : (int) std::lround ((double) r * (numFrames - 1) / (double) (s->rows - 1));
        const float* src = frames + (size_t) f * (size_t) frameSize;
        float* dst = s->data.data() + (size_t) r * Sketch::points;
        for (int i = 0; i < Sketch::points; ++i)
        {
            // Box-average each point's span: keeps the drawn line calm for harmonic-rich frames.
            const int start = (int) ((int64_t) i * frameSize / Sketch::points);
            float sum = 0.0f;
            for (int k = 0; k < box; ++k)
                sum += src[(start + k) % frameSize];
            dst[i] = sum / (float) box;
            peak = juce::jmax (peak, std::abs (dst[i]));
        }
    }
    if (peak > 1.0e-6f)
        for (auto& v : s->data)
            v /= peak;
    return s;
}

std::shared_ptr<const WavetableView::Sketch> WavetableView::factorySketch (int index)
{
    static std::mutex lock;
    static std::array<std::shared_ptr<const Sketch>, kNumFactoryTables> cache;
    index = juce::jlimit (0, kNumFactoryTables - 1, index);
    const std::lock_guard<std::mutex> sl (lock);
    auto& slot = cache[(size_t) index];
    if (slot == nullptr)
    {
        // Built once and only the sketch is kept (the full table with its mips is dropped).
        if (auto table = makeFactoryTable (index))
        {
            std::vector<float> frames ((size_t) table->numFrames() * kFrameSize);
            for (int f = 0; f < table->numFrames(); ++f)
                std::copy (table->frame (f), table->frame (f) + kFrameSize, frames.begin() + (std::ptrdiff_t) f * kFrameSize);
            slot = makeSketch (frames.data(), table->numFrames(), kFrameSize, juce::String::fromUTF8 (factoryTableNames()[index]));
        }
        else
            slot = std::make_shared<Sketch>();
    }
    return slot;
}

//==============================================================================
WavetableView::WavetableView (Processor& p, int o) : processor (p), osc (o)
{
    posParam = p.apvts.getParameter (info (oscParam (osc, OscParam::Pos)).id);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    p.patchChanged.addChangeListener (this);
    reloadTable();
}

WavetableView::~WavetableView()
{
    processor.patchChanged.removeChangeListener (this);
}

void WavetableView::setAccentColour (juce::Colour c)
{
    accent = c;
    stackScale = 0.0f;
    repaint();
}

void WavetableView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (tableIndex == kUserIndex)
        reloadTable();
}

void WavetableView::reloadTable()
{
    const auto* raw = processor.apvts.getRawParameterValue (info (oscParam (osc, OscParam::Table)).id);
    tableIndex = raw != nullptr ? (int) std::lround (raw->load()) : 0;

    if (tableIndex >= kUserIndex)
    {
        const auto patch = processor.currentPatch();
        const auto& ut = patch.userTables[(size_t) juce::jlimit (0, 1, osc)];
        const int frames = juce::jmin (ut.numFrames, (int) (ut.frames.size() / (size_t) kFrameSize));
        sketch = makeSketch (frames > 0 ? ut.frames.data() : nullptr, frames, kFrameSize,
                             ut.name.empty() ? juce::String ("User") : juce::String::fromUTF8 (ut.name.c_str()));
    }
    else
        sketch = factorySketch (tableIndex);

    stackScale = 0.0f;   // re-render the cached stack
    repaint();
}

float WavetableView::currentPosition() const
{
    const auto& vis = processor.engine().visuals();
    if (vis.activeVoices.load (std::memory_order_relaxed) > 0)
        return juce::jlimit (0.0f, 1.0f, vis.oscPosition[osc].load (std::memory_order_relaxed));
    return posParam != nullptr ? posParam->getValue() : 0.0f;
}

void WavetableView::tick()
{
    const auto* raw = processor.apvts.getRawParameterValue (info (oscParam (osc, OscParam::Table)).id);
    if (raw != nullptr && (int) std::lround (raw->load()) != tableIndex)
        reloadTable();

    const float p = currentPosition();
    if (std::abs (p - shownPosition) > 0.0005f)
        repaint();
}

void WavetableView::resized()
{
    stackScale = 0.0f;
}

//==============================================================================
juce::Point<float> WavetableView::Geometry::origin (float depth) const noexcept
{
    return { area.getX() + depth * depthX, area.getBottom() - amp - depth * depthY };
}

WavetableView::Geometry WavetableView::geometry() const
{
    Geometry g;
    g.area = getLocalBounds().toFloat().reduced (14.0f, 12.0f).withTrimmedTop (8.0f);
    g.depthX = g.area.getWidth() * 0.24f;
    g.depthY = g.area.getHeight() * 0.36f;
    g.frameW = g.area.getWidth() - g.depthX;
    g.amp = (g.area.getHeight() - g.depthY) * 0.46f;
    return g;
}

juce::Path WavetableView::framePath (const float* row, float depth, const Geometry& geo) const
{
    juce::Path p;
    const auto o = geo.origin (depth);
    const float w = geo.frameW;
    for (int i = 0; i <= Sketch::points; ++i)
    {
        const float v = row[i % Sketch::points];
        const float x = o.x + w * (float) i / (float) Sketch::points;
        const float y = o.y - v * geo.amp;
        if (i == 0) p.startNewSubPath (x, y);
        else        p.lineTo (x, y);
    }
    return p;
}

void WavetableView::renderStack (float scale)
{
    const int w = juce::jmax (1, (int) std::ceil ((float) getWidth() * scale));
    const int h = juce::jmax (1, (int) std::ceil ((float) getHeight() * scale));
    stack = juce::Image (juce::Image::ARGB, w, h, true);
    stackScale = scale;

    juce::Graphics g (stack);
    g.addTransform (juce::AffineTransform::scale (scale));
    paintDisplay (g, getLocalBounds().toFloat(), accent);

    if (sketch == nullptr || sketch->rows == 0)
        return;

    const auto geo = geometry();

    // Floor guides: the front and back baselines and the depth rails.
    g.setColour (col (palette::grid));
    const auto f0 = geo.origin (0.0f), f1 = geo.origin (1.0f);
    g.drawLine (f0.x, f0.y, f1.x, f1.y, 1.0f);
    g.drawLine (f0.x + geo.frameW, f0.y, f1.x + geo.frameW, f1.y, 1.0f);

    // Back to front, each frame hides what is behind it (filled to its own floor), dimmer with distance.
    // Hidden-line fills use the display background gradient itself, so they hide what is behind without showing a slab.
    const juce::ColourGradient bg (col (palette::well), 0.0f, 0.0f, col (palette::display), 0.0f, (float) getHeight(), false);
    for (int r = sketch->rows - 1; r >= 0; --r)
    {
        const float depth = sketch->rows == 1 ? 0.0f : (float) r / (float) (sketch->rows - 1);
        auto line = framePath (sketch->row (r), depth, geo);
        const auto o = geo.origin (depth);

        juce::Path fill (line);
        fill.lineTo (o.x + geo.frameW, o.y + geo.amp);
        fill.lineTo (o.x, o.y + geo.amp);
        fill.closeSubPath();
        g.setGradientFill (bg);
        g.setOpacity (0.9f);
        g.fillPath (fill);

        const float nearness = 1.0f - depth;
        g.setColour (col (palette::textDim).interpolatedWith (accent, 0.25f).withAlpha (0.16f + 0.42f * nearness));
        g.strokePath (line, juce::PathStrokeType (0.8f + 0.4f * nearness));
    }
}

void WavetableView::paint (juce::Graphics& g)
{
    const float scale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (stack.isNull() || ! juce::approximatelyEqual (scale, stackScale))
        renderStack (scale);
    g.drawImage (stack, getLocalBounds().toFloat());

    shownPosition = currentPosition();
    if (sketch == nullptr || sketch->rows == 0)
    {
        g.setColour (col (palette::textDim));
        g.setFont (font (Theme::fontBody));
        g.drawText ("No user table - Import a WAV or Paint one", getLocalBounds(), juce::Justification::centred, false);
        return;
    }

    // The current frame, interpolated between the two nearest sketch rows.
    const auto geo = geometry();
    const float s = shownPosition * (float) (sketch->rows - 1);
    const int r0 = juce::jlimit (0, sketch->rows - 1, (int) std::floor (s));
    const int r1 = juce::jmin (sketch->rows - 1, r0 + 1);
    const float t = s - (float) r0;
    highlight.resize ((size_t) Sketch::points);
    for (int i = 0; i < Sketch::points; ++i)
        highlight[(size_t) i] = sketch->row (r0)[i] + (sketch->row (r1)[i] - sketch->row (r0)[i]) * t;

    const float depth = sketch->rows == 1 ? 0.0f : s / (float) (sketch->rows - 1);
    const auto line = framePath (highlight.data(), depth, geo);
    const auto o = geo.origin (depth);

    juce::Path area (line);
    area.lineTo (o.x + geo.frameW, o.y);
    area.lineTo (o.x, o.y);
    area.closeSubPath();
    g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.28f), 0.0f, o.y - geo.amp, accent.withAlpha (0.04f), 0.0f, o.y, false));
    g.fillPath (area);

    g.setColour (accent.withAlpha (0.35f));
    g.drawLine (o.x, o.y, o.x + geo.frameW, o.y, 1.0f);

    g.setColour (accent.withAlpha (0.16f));
    g.strokePath (line, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (accent.withAlpha (0.3f));
    g.strokePath (line, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (accent.brighter (0.3f));
    g.strokePath (line, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Read-out: frame number, top right.
    const int frame = (int) std::lround (shownPosition * (float) juce::jmax (0, sketch->numFrames - 1)) + 1;
    g.setFont (font (10.5f, true).withExtraKerningFactor (0.06f));
    g.setColour (col (palette::textDim));
    g.drawText ("FRAME " + juce::String (frame) + " / " + juce::String (sketch->numFrames),
                getLocalBounds().reduced (10, 6).removeFromTop (14), juce::Justification::topRight, false);
}

//==============================================================================
void WavetableView::mouseDown (const juce::MouseEvent&)
{
    if (posParam == nullptr) return;
    dragStartValue = posParam->getValue();
    posParam->beginChangeGesture();
}

void WavetableView::mouseDrag (const juce::MouseEvent& e)
{
    if (posParam == nullptr) return;
    const float range = e.mods.isShiftDown() ? 800.0f : 200.0f;
    posParam->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, dragStartValue - (float) e.getDistanceFromDragStartY() / range));
    repaint();
}

void WavetableView::mouseUp (const juce::MouseEvent&)
{
    if (posParam != nullptr)
        posParam->endChangeGesture();
}

void WavetableView::mouseDoubleClick (const juce::MouseEvent&)
{
    if (posParam == nullptr) return;
    posParam->beginChangeGesture();
    posParam->setValueNotifyingHost (posParam->getDefaultValue());
    posParam->endChangeGesture();
}

} // namespace verdant::ui
