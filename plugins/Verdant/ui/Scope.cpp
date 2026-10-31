#include "Scope.h"

#include "../PluginProcessor.h"

namespace verdant::ui
{

namespace
{
    juce::Colour col (juce::uint32 c) { return juce::Colour (c); }
    constexpr float kMinDb = -90.0f, kMaxDb = 0.0f;
}

Scope::Scope (Processor& p) : processor (p)
{
    samples.assign (kSize, 0.0f);
    scratch.assign (kSize, 0.0f);
    window.resize (kSize);
    for (int i = 0; i < kSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (kSize - 1));
    re.assign (kSize / 2 + 1, 0.0f);
    im.assign (kSize / 2 + 1, 0.0f);
    spectrumDb.assign (kSize / 2 + 1, kMinDb);
    peakDb.assign (kSize / 2 + 1, kMinDb);
    setTooltip ("Output scope - click for the spectrum");
}

Scope::~Scope() = default;

void Scope::setMode (Mode m)
{
    mode = m;
    setTooltip (mode == Mode::Scope ? "Output scope - click for the spectrum" : "Output spectrum - click for the scope");
    repaint();
}

void Scope::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mouseWasDraggedSinceMouseDown())
        setMode (mode == Mode::Scope ? Mode::Spectrum : Mode::Scope);
}

void Scope::tick()
{
    count = processor.engine().visuals().readScope (samples.data(), kSize);
    float peak = 0.0f;
    for (int i = 0; i < count; ++i)
        peak = juce::jmax (peak, std::abs (samples[(size_t) i]));
    const bool nowSilent = peak < 1.0e-5f;
    if (nowSilent && silent)
        return;   // nothing new to draw
    silent = nowSilent;
    const float target = 0.85f / juce::jmax (0.05f, peak);
    gain += (target - gain) * (target < gain ? 0.5f : 0.08f);
    if (mode == Mode::Spectrum)
        computeSpectrum();
    repaint();
}

void Scope::computeSpectrum()
{
    auto& in = scratch;
    std::fill (in.begin(), in.end(), 0.0f);
    const int offset = kSize - count;
    for (int i = 0; i < count; ++i)
        in[(size_t) (i + offset)] = samples[(size_t) i] * window[(size_t) (i + offset)];
    fft.realForward (in.data(), re.data(), im.data());
    const float norm = 2.0f / (0.5f * (float) kSize);   // Hann coherent gain 0.5 -> a full-scale sine reads 0 dB
    for (size_t k = 0; k < re.size(); ++k)
    {
        const float mag = std::sqrt (re[k] * re[k] + im[k] * im[k]) * norm;
        const float db = juce::jlimit (kMinDb, kMaxDb + 6.0f, 20.0f * std::log10 (juce::jmax (1.0e-6f, mag)));
        auto& s = spectrumDb[k];
        s = db > s ? s + (db - s) * 0.6f : s + (db - s) * 0.18f;
        auto& pk = peakDb[k];
        pk = juce::jmax (s, pk - 0.6f);
    }
}

void Scope::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    // Floating panel: shadow, card, display.
    for (int i = 0; i < 5; ++i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.12f - 0.02f * (float) i));
        g.drawRoundedRectangle (r.reduced (3.0f).translated (0.0f, 2.0f).expanded ((float) i * 0.8f), Theme::cardRadius, 1.5f);
    }
    const auto card = r.reduced (3.0f);
    g.setColour (col (palette::panel));
    g.fillRoundedRectangle (card, Theme::cardRadius);
    g.setColour (col (palette::panelEdge).brighter (0.15f));
    g.drawRoundedRectangle (card.reduced (0.5f), Theme::cardRadius, 1.0f);

    auto header = card.reduced (10.0f, 6.0f).removeFromTop (16.0f);
    g.setColour (col (palette::accent));
    g.fillRoundedRectangle (juce::Rectangle<float> (3.0f, 10.0f).withCentre ({ header.getX() + 1.5f, header.getCentreY() }), 1.5f);
    drawTitleText (g, mode == Mode::Scope ? "Scope" : "Spectrum", header.withTrimmedLeft (9.0f).toNearestInt(), col (palette::text),
                   juce::Justification::centredLeft, 10.5f);
    g.setColour (col (palette::textFaint));
    g.setFont (font (9.5f));
    g.drawText (mode == Mode::Scope ? "click: spectrum" : "click: scope", header, juce::Justification::centredRight, false);

    const auto display = card.reduced (8.0f).withTrimmedTop (18.0f);
    paintDisplay (g, display, col (palette::accent));
    const auto a = display.reduced (6.0f, 6.0f);
    const auto accent = col (palette::accent);

    if (mode == Mode::Scope)
    {
        g.setColour (col (palette::grid).brighter (0.15f));
        g.fillRect (juce::Rectangle<float> (a.getX(), std::round (a.getCentreY()), a.getWidth(), 1.0f));
        if (silent || count < 64)
        {
            g.setColour (col (palette::textFaint));
            g.setFont (font (Theme::fontSmall));
            g.drawText ("No signal", display, juce::Justification::centred, false);
            return;
        }
        // Trigger on a rising zero crossing in the older half, then show 1024 samples.
        const int span = juce::jmin (1024, count / 2);
        int start = count - span;
        for (int i = count - span - 1; i > 1; --i)
            if (samples[(size_t) i - 1] < 0.0f && samples[(size_t) i] >= 0.0f) { start = i; break; }
        juce::Path p;
        const int points = juce::jmin (span, (int) a.getWidth() * 2);
        for (int k = 0; k < points; ++k)
        {
            const int i = juce::jmin (count - 1, start + k * span / points);
            const float x = a.getX() + a.getWidth() * (float) k / (float) (points - 1);
            const float y = a.getCentreY() - juce::jlimit (-1.0f, 1.0f, samples[(size_t) i] * gain) * a.getHeight() * 0.5f;
            if (k == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (accent.withAlpha (0.18f));
        g.strokePath (p, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (accent);
        g.strokePath (p, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        return;
    }

    // Spectrum.
    const double sr = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
    auto xForHz = [&a] (float hz) { return a.getX() + a.getWidth() * std::log (hz / 20.0f) / std::log (1000.0f); };
    auto yForDb = [&a] (float db) { return a.getY() + a.getHeight() * (kMaxDb - db) / (kMaxDb - kMinDb); };
    g.setFont (font (8.5f));
    for (int hz : { 100, 1000, 10000 })
    {
        const float x = std::round (xForHz ((float) hz));
        g.setColour (col (palette::grid).brighter (0.12f));
        g.fillRect (juce::Rectangle<float> (x, a.getY(), 1.0f, a.getHeight()));
        g.setColour (col (palette::textFaint));
        g.drawText (hz >= 1000 ? juce::String (hz / 1000) + "k" : juce::String (hz), juce::Rectangle<float> (x + 2.0f, a.getBottom() - 10.0f, 24.0f, 10.0f),
                    juce::Justification::centredLeft, false);
    }
    for (int db : { -60, -30 })
    {
        g.setColour (col (palette::grid));
        g.fillRect (juce::Rectangle<float> (a.getX(), std::round (yForDb ((float) db)), a.getWidth(), 1.0f));
    }

    juce::Path line, peaks;
    const int px = (int) a.getWidth();
    for (int i = 0; i <= px; ++i)
    {
        const float hz0 = 20.0f * std::pow (1000.0f, (float) i / (float) px), hz1 = 20.0f * std::pow (1000.0f, (float) (i + 1) / (float) px);
        const int k0 = juce::jlimit (1, kSize / 2, (int) std::floor (hz0 / (float) sr * (float) kSize));
        const int k1 = juce::jlimit (k0, kSize / 2, (int) std::ceil (hz1 / (float) sr * (float) kSize));
        float db = kMinDb, pk = kMinDb;
        if (k1 - k0 <= 1)
        {
            // Fewer bins than pixels (low end): interpolate so the curve stays smooth.
            const float fb = juce::jlimit (1.0f, (float) (kSize / 2 - 1), (hz0 + hz1) * 0.5f / (float) sr * (float) kSize);
            const int b = (int) fb;
            const float t = fb - (float) b;
            db = spectrumDb[(size_t) b] + (spectrumDb[(size_t) b + 1] - spectrumDb[(size_t) b]) * t;
            pk = peakDb[(size_t) b] + (peakDb[(size_t) b + 1] - peakDb[(size_t) b]) * t;
        }
        else
            for (int k = k0; k <= k1; ++k)
            {
                db = juce::jmax (db, spectrumDb[(size_t) k]);
                pk = juce::jmax (pk, peakDb[(size_t) k]);
            }
        const float x = a.getX() + (float) i;
        if (i == 0) { line.startNewSubPath (x, yForDb (db)); peaks.startNewSubPath (x, yForDb (pk)); }
        else        { line.lineTo (x, yForDb (db)); peaks.lineTo (x, yForDb (pk)); }
    }
    juce::Path fill (line);
    fill.lineTo (a.getRight(), a.getBottom());
    fill.lineTo (a.getX(), a.getBottom());
    fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.35f), 0.0f, a.getY(), accent.withAlpha (0.02f), 0.0f, a.getBottom(), false));
    g.fillPath (fill);
    g.setColour (col (palette::text).withAlpha (0.25f));
    g.strokePath (peaks, juce::PathStrokeType (1.0f));
    g.setColour (accent);
    g.strokePath (line, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (silent)
    {
        g.setColour (col (palette::textFaint));
        g.setFont (font (Theme::fontSmall));
        g.drawText ("No signal", display, juce::Justification::centred, false);
    }
}

} // namespace verdant::ui
