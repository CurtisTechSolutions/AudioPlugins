#include "FxPage.h"

#include "FxSlotStrip.h"
#include "ModKnob.h"
#include "Widgets.h"

#include "../PluginProcessor.h"

#include <ap/Tempo.h>

#include <complex>

namespace verdant::ui
{

namespace
{
    juce::Colour col (juce::uint32 c) { return juce::Colour (c); }

    constexpr float kTau = juce::MathConstants<float>::twoPi;

    enum FxIndex { None, Distortion, Chorus, Phaser, Flanger, Delay, Reverb, Eq, Compressor, NumTypes };

    /** Curve with the hero style: soft fill to the bottom, glow, 1.8 px line. */
    void strokeHero (juce::Graphics& g, const juce::Path& line, juce::Rectangle<float> a, juce::Colour c, bool fill = true, float baseY = -1.0f)
    {
        if (fill)
        {
            juce::Path f (line);
            const float y0 = baseY >= 0.0f ? baseY : a.getBottom() + 2.0f;
            f.lineTo (line.getBounds().getRight(), y0);
            f.lineTo (line.getBounds().getX(), y0);
            f.closeSubPath();
            g.setGradientFill (juce::ColourGradient (c.withAlpha (0.22f), 0.0f, a.getY(), c.withAlpha (0.0f), 0.0f, a.getBottom(), false));
            g.fillPath (f);
        }
        g.setColour (c.withAlpha (0.18f));
        g.strokePath (line, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (c);
        g.strokePath (line, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void gridLine (juce::Graphics& g, float x0, float y0, float x1, float y1, bool major = false)
    {
        g.setColour (col (palette::grid).brighter (major ? 0.15f : 0.0f));
        if (x0 == x1) g.fillRect (juce::Rectangle<float> (std::round (x0), y0, 1.0f, y1 - y0));
        else          g.fillRect (juce::Rectangle<float> (x0, std::round (y0), x1 - x0, 1.0f));
    }

    void gridLabel (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, juce::Justification j = juce::Justification::centred)
    {
        g.setColour (col (palette::textFaint));
        g.setFont (font (9.5f));
        g.drawText (t, r, j, false);
    }

    juce::String hzText (float hz)
    {
        return hz >= 1000.0f ? juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz";
    }

    juce::String msText (float ms)
    {
        return ms >= 1000.0f ? juce::String (ms / 1000.0f, 2) + " s" : juce::String (ms, ms < 10.0f ? 1 : 0) + " ms";
    }

    //==============================================================================
    /** The selected effect drawn from its parameters (plus a type picker for an empty slot). */
    class FxDisplay : public juce::Component, public Tickable
    {
    public:
        FxDisplay (Processor& p, FxSlotStrip& s) : processor (p), strip (s) {}

        void setType (int t)
        {
            if (t == type) return;
            type = t;
            last.clear();
            repaint();
        }

        float v (P p) const
        {
            if (auto* r = processor.apvts.getRawParameterValue (info (p).id))
                return r->load (std::memory_order_relaxed);
            return info (p).def;
        }

        void tick() override
        {
            std::vector<float> now;
            for (int i = (int) P::DistType; i <= (int) P::CompMix; ++i)
                now.push_back (v ((P) i));
            const bool animated = type == Chorus || type == Phaser || type == Flanger;
            if (animated)
            {
                const P rateParam = type == Chorus ? P::ChorusRate : (type == Phaser ? P::PhaserRate : P::FlangerRate);
                phase = std::fmod (phase + kTau * v (rateParam) / 30.0f, kTau);
            }
            if (animated || now != last)
            {
                last = std::move (now);
                repaint();
            }
        }

        void paint (juce::Graphics& g) override
        {
            const auto bounds = getLocalBounds().toFloat();
            paintDisplay (g, bounds, col (palette::accent));
            auto a = bounds.reduced (16.0f, 14.0f);

            switch (type)
            {
                case Distortion: paintDistortion (g, a); break;
                case Chorus:     paintChorus (g, a); break;
                case Phaser:     paintPhaser (g, a); break;
                case Flanger:    paintFlanger (g, a); break;
                case Delay:      paintDelay (g, a); break;
                case Reverb:     paintReverb (g, a); break;
                case Eq:         paintEq (g, a); break;
                case Compressor: paintComp (g, a); break;
                default:         paintPicker (g); break;
            }
        }

        // ---- mouse: EQ band handles, the empty-slot picker
        void mouseMove (const juce::MouseEvent& e) override
        {
            const int h = type == Eq ? eqHandleAt (e.position) : (type == None ? pickerAt (e.position) : -1);
            if (h != hover) { hover = h; repaint(); }
            setMouseCursor (h >= 0 ? (type == Eq ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::PointingHandCursor)
                                   : juce::MouseCursor::NormalCursor);
        }
        void mouseExit (const juce::MouseEvent&) override { if (hover != -1) { hover = -1; repaint(); } }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (type == None)
            {
                const int t = pickerAt (e.position);
                if (t > 0) strip.setSlotType (strip.getSelected(), t);
                return;
            }
            if (type != Eq) return;
            dragBand = eqHandleAt (e.position);
            if (dragBand < 0) return;
            for (auto p : eqParams (dragBand))
                if (auto* q = param (p)) q->beginChangeGesture();
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (type != Eq || dragBand < 0) return;
            const auto a = eqArea();
            const auto [fp, gp] = eqParams (dragBand);
            const float hz = juce::jlimit (info (fp).min, info (fp).max, hzForX (a, e.position.x));
            const float db = juce::jlimit (-18.0f, 18.0f, 18.0f - 36.0f * (e.position.y - a.getY()) / a.getHeight());
            if (auto* q = param (fp)) q->setValueNotifyingHost (q->convertTo0to1 (hz));
            if (auto* q = param (gp)) q->setValueNotifyingHost (q->convertTo0to1 (std::abs (db) < 0.4f ? 0.0f : db));
            repaint();
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (type == Eq && dragBand >= 0)
                for (auto p : eqParams (dragBand))
                    if (auto* q = param (p)) q->endChangeGesture();
            dragBand = -1;
            repaint();
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            if (type != Eq) return;
            const int b = eqHandleAt (e.position);
            if (b < 0) return;
            if (auto* q = param (eqParams (b).second))
                q->setValueNotifyingHost (q->convertTo0to1 (0.0f));
        }

        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
        {
            if (type != Eq || eqHandleAt (e.position) != 1) return;
            if (auto* q = param (P::EqMidQ))
                q->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, q->getValue() + w.deltaY * 0.15f));
        }

    private:
        juce::RangedAudioParameter* param (P p) const { return processor.apvts.getParameter (info (p).id); }

        // ---------------------------------------------------------------- empty slot
        juce::Rectangle<float> pickerCell (int t) const
        {
            auto a = getLocalBounds().toFloat().reduced (24.0f, 20.0f);
            a.removeFromTop (34.0f);
            const int i = t - 1, row = i / 4, k = i % 4;
            const float w = (a.getWidth() - 3 * 10.0f) / 4.0f, h = juce::jmin (64.0f, (a.getHeight() - 10.0f) / 2.0f);
            const float y0 = a.getCentreY() - (2 * h + 10.0f) * 0.5f;
            return { a.getX() + (float) k * (w + 10.0f), y0 + (float) row * (h + 10.0f), w, h };
        }
        int pickerAt (juce::Point<float> p) const
        {
            for (int t = 1; t < NumTypes; ++t)
                if (pickerCell (t).contains (p)) return t;
            return -1;
        }
        void paintPicker (juce::Graphics& g)
        {
            auto top = getLocalBounds().toFloat().reduced (24.0f, 20.0f).removeFromTop (34.0f);
            g.setColour (col (palette::text));
            g.setFont (font (14.0f, true));
            g.drawText ("Slot " + juce::String (strip.getSelected() + 1) + " is empty", top.removeFromTop (18.0f), juce::Justification::centred, false);
            g.setColour (col (palette::textDim));
            g.setFont (font (Theme::fontSmall));
            g.drawText ("Choose an effect for it", top, juce::Justification::centred, false);

            for (int t = 1; t < NumTypes; ++t)
            {
                const auto r = pickerCell (t);
                const bool h = hover == t;
                bool used = false;
                for (int s = 0; s < FxSlotStrip::kSlots; ++s)
                    used = used || strip.typeOf (s) == t;
                g.setColour (col (h ? palette::controlHover : palette::well));
                g.fillRoundedRectangle (r, Theme::radius);
                g.setColour (h ? col (palette::accent) : col (palette::panelEdge).brighter (0.15f));
                g.drawRoundedRectangle (r.reduced (0.5f), Theme::radius, 1.0f);
                auto inner = r.reduced (10.0f, 8.0f);
                const auto name = inner.removeFromBottom (16.0f);
                FxSlotStrip::drawTypeGlyph (g, t, inner.withSizeKeepingCentre (juce::jmin (inner.getWidth(), 60.0f), juce::jmin (inner.getHeight(), 20.0f)),
                                            h ? col (palette::accent) : col (palette::text).withAlpha (used ? 0.4f : 0.8f), 1.5f);
                g.setColour (col (palette::text).withAlpha (used ? 0.5f : 1.0f));
                g.setFont (font (Theme::fontBody, true));
                g.drawText (FxSlotStrip::typeName (t) + (used ? "  (in use)" : ""), name, juce::Justification::centred, false);
            }
        }

        // ---------------------------------------------------------------- distortion
        static float shape (int dtype, float d, float x)
        {
            switch (dtype)
            {
                case 1:  return juce::jlimit (-1.0f, 1.0f, d * x);                                        // hard
                case 2:  return x >= 0.0f ? std::tanh (d * x) : 0.85f * std::tanh (0.6f * d * x);         // tube
                case 3:  return std::sin (d * x * 1.5707963f);                                            // fold
                case 4:                                                                                    // crush
                {
                    const float steps = juce::jmax (2.0f, 24.0f - 0.22f * (d - 1.0f) * 5.0f);
                    return juce::jlimit (-1.0f, 1.0f, std::round (juce::jlimit (-1.0f, 1.0f, x * (0.5f + 0.5f * std::sqrt (d))) * steps) / steps);
                }
                case 5:  return x >= 0.0f ? std::tanh (d * x) : 0.3f * std::tanh (d * x);                // diode
                default: return std::tanh (d * x);                                                         // soft
            }
        }

        void paintDistortion (juce::Graphics& g, juce::Rectangle<float> a)
        {
            const int dtype = (int) std::lround (v (P::DistType));
            const float d = 1.0f + 0.19f * v (P::DistDrive);
            const float mix = v (P::DistMix) * 0.01f;
            const float tone = v (P::DistTone);
            const auto accent = col (palette::accent);

            // Left: the transfer curve.
            auto tr = a.removeFromLeft (juce::jmin (a.getHeight(), a.getWidth() * 0.4f));
            gridLine (g, tr.getX(), tr.getCentreY(), tr.getRight(), tr.getCentreY(), true);
            gridLine (g, tr.getCentreX(), tr.getY(), tr.getCentreX(), tr.getBottom(), true);
            g.setColour (col (palette::textFaint).withAlpha (0.5f));
            g.drawLine (tr.getX(), tr.getBottom(), tr.getRight(), tr.getY(), 1.0f);
            juce::Path curve;
            for (int i = 0; i <= 120; ++i)
            {
                const float x = -1.0f + 2.0f * (float) i / 120.0f;
                const float y = mix * shape (dtype, d, x) + (1.0f - mix) * x;
                const juce::Point<float> pt (tr.getX() + (x + 1.0f) * 0.5f * tr.getWidth(), tr.getCentreY() - y * tr.getHeight() * 0.5f);
                if (i == 0) curve.startNewSubPath (pt); else curve.lineTo (pt);
            }
            strokeHero (g, curve, tr, accent, false);
            gridLabel (g, "IN", { tr.getRight() - 24.0f, tr.getCentreY() + 2.0f, 24.0f, 11.0f }, juce::Justification::centredRight);
            gridLabel (g, "OUT", { tr.getCentreX() + 4.0f, tr.getY(), 30.0f, 11.0f }, juce::Justification::centredLeft);

            // Right: two cycles of a 220 Hz sine through the shaper and the tone filter.
            a.removeFromLeft (20.0f);
            gridLine (g, a.getX(), a.getCentreY(), a.getRight(), a.getCentreY(), true);
            const int n = 436;
            const float coef = 1.0f - std::exp (-kTau * tone / 48000.0f);
            float lp = 0.0f;
            juce::Path dry, wet;
            // Prime the filter with one cycle so the drawn cycles are steady.
            for (int pass = 0; pass < 2; ++pass)
                for (int i = 0; i < n; ++i)
                {
                    const float x = std::sin (kTau * 2.0f * (float) i / (float) n);
                    lp += coef * (shape (dtype, d, x) - lp);
                    if (pass == 0) continue;
                    const float y = mix * lp + (1.0f - mix) * x;
                    const float px = a.getX() + a.getWidth() * (float) i / (float) (n - 1);
                    const float half = a.getHeight() * 0.45f;
                    if (i == 0) { dry.startNewSubPath (px, a.getCentreY() - x * half); wet.startNewSubPath (px, a.getCentreY() - y * half); }
                    else        { dry.lineTo (px, a.getCentreY() - x * half); wet.lineTo (px, a.getCentreY() - y * half); }
                }
            juce::Path dashed;
            const float dashes[] = { 3.0f, 4.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, dry, dashes, 2);
            g.setColour (col (palette::textDim).withAlpha (0.5f));
            g.fillPath (dashed);
            strokeHero (g, wet, a, accent, true, a.getCentreY());
            gridLabel (g, "TONE " + hzText (tone).toUpperCase(), { a.getRight() - 120.0f, a.getY(), 120.0f, 11.0f }, juce::Justification::centredRight);
        }

        // ---------------------------------------------------------------- chorus
        void paintChorus (juce::Graphics& g, juce::Rectangle<float> a)
        {
            const float depth = v (P::ChorusDepth) * 0.01f, fb = v (P::ChorusFeedback) * 0.01f, mix = v (P::ChorusMix) * 0.01f;
            const float rate = v (P::ChorusRate);
            auto plot = a.withTrimmedBottom (12.0f);
            for (int k = 1; k < 4; ++k)
                gridLine (g, plot.getX(), plot.getY() + plot.getHeight() * (float) k / 4.0f, plot.getRight(), plot.getY() + plot.getHeight() * (float) k / 4.0f);
            // Three voices' delay times over the last few seconds, newest at the right.
            const float seconds = juce::jlimit (1.0f, 8.0f, 3.0f / juce::jmax (0.05f, rate) * 0.5f + 1.0f);
            const auto accent = col (palette::accent);
            for (int voice = 2; voice >= 0; --voice)
            {
                juce::Path p;
                for (int i = 0; i <= 160; ++i)
                {
                    const float t = (float) i / 160.0f;
                    const float ph = phase - kTau * rate * seconds * (1.0f - t) + kTau * (float) voice / 3.0f;
                    const float y = plot.getCentreY() - std::sin (ph) * plot.getHeight() * (0.06f + 0.4f * depth);
                    const float x = plot.getX() + t * plot.getWidth();
                    if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
                }
                const auto c = accent.withAlpha ((voice == 0 ? 1.0f : (voice == 1 ? 0.6f : 0.35f)) * (0.35f + 0.65f * mix));
                if (fb > 0.02f)
                {
                    g.setColour (c.withAlpha (c.getFloatAlpha() * fb * 0.5f));
                    g.strokePath (p, juce::PathStrokeType (2.0f + 10.0f * fb, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }
                strokeHero (g, p, plot, c, false);
                const float yEnd = p.getCurrentPosition().y;
                g.setColour (c);
                g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ plot.getRight(), yEnd }));
            }
            gridLabel (g, "VOICE DELAY", { plot.getX(), plot.getY(), 120.0f, 11.0f }, juce::Justification::centredLeft);
            gridLabel (g, "-" + juce::String (seconds, 1) + " s", { a.getX(), a.getBottom() - 11.0f, 60.0f, 11.0f }, juce::Justification::centredLeft);
            gridLabel (g, "now", { a.getRight() - 40.0f, a.getBottom() - 11.0f, 40.0f, 11.0f }, juce::Justification::centredRight);
        }

        // ---------------------------------------------------------------- shared log-frequency axis
        static float xForHz (juce::Rectangle<float> a, float hz, float lo = 20.0f, float hi = 20000.0f)
        {
            return a.getX() + a.getWidth() * std::log (hz / lo) / std::log (hi / lo);
        }
        static float hzForX (juce::Rectangle<float> a, float x, float lo = 20.0f, float hi = 20000.0f)
        {
            return lo * std::pow (hi / lo, juce::jlimit (0.0f, 1.0f, (x - a.getX()) / a.getWidth()));
        }
        void frequencyGrid (juce::Graphics& g, juce::Rectangle<float> a, float minDb, float maxDb, std::initializer_list<int> dbLines)
        {
            for (int hz : { 50, 100, 200, 500, 1000, 2000, 5000, 10000 })
            {
                const float x = xForHz (a, (float) hz);
                gridLine (g, x, a.getY(), x, a.getBottom(), hz == 100 || hz == 1000 || hz == 10000);
                gridLabel (g, hz >= 1000 ? juce::String (hz / 1000) + "k" : juce::String (hz), { x - 20.0f, a.getBottom() + 1.0f, 40.0f, 11.0f });
            }
            for (int db : dbLines)
            {
                const float y = a.getY() + a.getHeight() * (maxDb - (float) db) / (maxDb - minDb);
                gridLine (g, a.getX(), y, a.getRight(), y, db == 0);
                gridLabel (g, (db > 0 ? "+" : "") + juce::String (db) + " dB", { a.getX() + 2.0f, y - 11.0f, 44.0f, 10.0f }, juce::Justification::centredLeft);
            }
        }

        template <typename Fn>
        juce::Path responsePath (juce::Rectangle<float> a, float minDb, float maxDb, Fn&& dbAt, int points = 300, float lo = 20.0f, float hi = 20000.0f)
        {
            juce::Path p;
            for (int i = 0; i < points; ++i)
            {
                const float x = a.getX() + a.getWidth() * (float) i / (float) (points - 1);
                const float hz = hzForX (a, x, lo, hi);
                const float db = juce::jlimit (minDb - 3.0f, maxDb + 3.0f, dbAt (hz));
                const float y = a.getY() + a.getHeight() * (maxDb - db) / (maxDb - minDb);
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            return p;
        }

        // ---------------------------------------------------------------- phaser
        void paintPhaser (juce::Graphics& g, juce::Rectangle<float> a)
        {
            const auto plot = a.withTrimmedBottom (12.0f);
            frequencyGrid (g, plot, -30.0f, 12.0f, { -24, -12, 0 });
            const float depth = v (P::PhaserDepth) * 0.01f, fb = v (P::PhaserFeedback) * 0.01f, mix = v (P::PhaserMix) * 0.01f;
            static constexpr int stagesFor[] = { 4, 6, 8, 12 };
            const int notches = stagesFor[juce::jlimit (0, 3, (int) std::lround (v (P::PhaserStages)))] / 2;
            const float centre = std::exp (std::log (900.0f) + depth * 1.6f * std::sin (phase));
            const float q = 1.5f + 7.0f * fb;
            auto dbAt = [&] (float hz)
            {
                float notch = 0.0f, peak = 0.0f;
                for (int k = 0; k < notches; ++k)
                {
                    const float fk = centre * std::pow (2.1f, (float) k - (float) (notches - 1) * 0.5f);
                    const float u = (hz / fk - fk / hz) * q;
                    notch = juce::jmax (notch, 1.0f / (1.0f + u * u));
                    const float fp = fk * 1.45f;
                    const float up = (hz / fp - fp / hz) * q;
                    peak += fb * 0.7f / (1.0f + up * up);
                }
                const float mag = juce::jmax (0.03f, (1.0f - mix * notch) * (1.0f + mix * peak));
                return 20.0f * std::log10 (mag);
            };
            strokeHero (g, responsePath (plot, -30.0f, 12.0f, dbAt), plot, col (palette::accent));
            gridLabel (g, juce::String (notches) + " NOTCHES", { plot.getRight() - 120.0f, plot.getY(), 120.0f, 11.0f }, juce::Justification::centredRight);
        }

        // ---------------------------------------------------------------- flanger
        void paintFlanger (juce::Graphics& g, juce::Rectangle<float> a)
        {
            const auto plot = a.withTrimmedBottom (12.0f);
            const float depth = v (P::FlangerDepth) * 0.01f, fb = juce::jlimit (-0.95f, 0.95f, v (P::FlangerFeedback) * 0.0095f);
            const float mix = v (P::FlangerMix) * 0.01f;
            const float tauMs = 0.6f + depth * 5.5f * (0.5f + 0.5f * std::sin (phase));
            const float maxHz = 8000.0f;
            for (int k = 1; k <= 7; ++k)
            {
                const float x = plot.getX() + plot.getWidth() * (float) k / 8.0f;
                gridLine (g, x, plot.getY(), x, plot.getBottom(), k % 2 == 0);
                if (k % 2 == 0) gridLabel (g, juce::String (k) + "k", { x - 20.0f, plot.getBottom() + 1.0f, 40.0f, 11.0f });
            }
            for (int db : { -24, -12, 0, 12 })
            {
                const float y = plot.getY() + plot.getHeight() * (18.0f - (float) db) / 48.0f;
                gridLine (g, plot.getX(), y, plot.getRight(), y, db == 0);
                gridLabel (g, (db > 0 ? "+" : "") + juce::String (db) + " dB", { plot.getX() + 2.0f, y - 11.0f, 44.0f, 10.0f }, juce::Justification::centredLeft);
            }
            juce::Path p;
            const int points = 600;
            for (int i = 0; i < points; ++i)
            {
                const float hz = maxHz * (float) i / (float) (points - 1);
                const std::complex<float> z = std::polar (1.0f, -kTau * hz * tauMs * 0.001f);
                const auto h = 1.0f + mix * z / (1.0f - fb * z);
                const float db = juce::jlimit (-33.0f, 21.0f, 20.0f * std::log10 (juce::jmax (1.0e-3f, std::abs (h) / (1.0f + mix * 0.5f))));
                const float x = plot.getX() + plot.getWidth() * (float) i / (float) (points - 1);
                const float y = plot.getY() + plot.getHeight() * (18.0f - db) / 48.0f;
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            strokeHero (g, p, plot, col (palette::accent));
            gridLabel (g, "DELAY " + juce::String (tauMs, 1) + " MS", { plot.getRight() - 120.0f, plot.getY(), 120.0f, 11.0f }, juce::Justification::centredRight);
        }

        // ---------------------------------------------------------------- delay
        void paintDelay (juce::Graphics& g, juce::Rectangle<float> a)
        {
            const bool sync = v (P::DelaySync) >= 0.5f, pingPong = v (P::DelayPingPong) >= 0.5f;
            const auto div = (ap::NoteDivision) juce::jlimit (0, (int) ap::NoteDivision::Count - 1, (int) std::lround (v (P::DelayDiv)));
            const float timeMs = sync ? (float) (ap::noteDivisionBeats (div) * 500.0) : v (P::DelayTime);   // 120 bpm
            const float fb = v (P::DelayFeedback) * 0.01f, mix = v (P::DelayMix) * 0.01f;
            const float lowCut = v (P::DelayLowCut), highCut = v (P::DelayHighCut);

            auto plot = a.withTrimmedBottom (12.0f);
            const int taps = 9;
            const float span = timeMs * ((float) taps - 0.4f);
            const float mid = plot.getCentreY();
            gridLine (g, plot.getX(), mid, plot.getRight(), mid, true);
            gridLabel (g, "L", { plot.getX(), plot.getY(), 12.0f, 11.0f }, juce::Justification::centredLeft);
            gridLabel (g, "R", { plot.getX(), plot.getBottom() - 11.0f, 12.0f, 11.0f }, juce::Justification::centredLeft);

            // Each repeat loses a little top and bottom to the feedback filters.
            const float tone = juce::jlimit (0.3f, 1.0f, (std::log (highCut / 1000.0f) / std::log (20.0f)) * 0.7f + 0.3f
                                                         - 0.25f * std::log (lowCut / 20.0f) / std::log (100.0f));
            const auto accent = col (palette::accent);
            for (int k = 0; k < taps; ++k)
            {
                const float t = timeMs * (float) k;
                const float x = plot.getX() + 18.0f + (plot.getWidth() - 24.0f) * t / span;
                gridLine (g, x, plot.getY() + 4.0f, x, plot.getBottom() - 4.0f, false);
                float amp = k == 0 ? 1.0f : mix * std::pow (fb, (float) (k - 1)) * std::pow (tone, (float) (k - 1));
                if (amp < 0.01f) continue;
                const float h = (plot.getHeight() * 0.5f - 6.0f) * juce::jmin (1.0f, amp);
                const auto c = k == 0 ? col (palette::text).withAlpha (0.8f) : accent.withAlpha (0.35f + 0.65f * juce::jmin (1.0f, amp / juce::jmax (0.05f, mix)));
                auto bar = [&] (bool up, float frac)
                {
                    const float hh = h * frac;
                    const auto r = up ? juce::Rectangle<float> (x - 3.0f, mid - hh, 6.0f, hh) : juce::Rectangle<float> (x - 3.0f, mid, 6.0f, hh);
                    if (k > 0)
                    {
                        g.setColour (c.withAlpha (0.18f));
                        g.fillRoundedRectangle (r.expanded (3.0f, 0.0f), 4.0f);
                    }
                    g.setColour (c);
                    g.fillRoundedRectangle (r, 3.0f);
                };
                if (k == 0 || ! pingPong) { bar (true, 1.0f); bar (false, 1.0f); }
                else                       bar (k % 2 == 1, 1.0f);
                if (k <= 4)
                    gridLabel (g, k == 0 ? juce::String ("dry") : msText (t), { x - 30.0f, a.getBottom() - 11.0f, 60.0f, 11.0f });
            }
            const juce::String head = sync ? juce::String (ap::noteDivisionName (div)) + "  =  " + msText (timeMs) + " @ 120 bpm" : msText (timeMs);
            gridLabel (g, head.toUpperCase() + (pingPong ? "   PING-PONG" : ""), { plot.getRight() - 260.0f, plot.getY(), 260.0f, 11.0f }, juce::Justification::centredRight);
        }

        // ---------------------------------------------------------------- reverb
        void paintReverb (juce::Graphics& g, juce::Rectangle<float> a)
        {
            const float size = v (P::ReverbSize) * 0.01f, decay = v (P::ReverbDecay) * 0.01f, damp = v (P::ReverbDamp) * 0.01f;
            const float pre = v (P::ReverbPredelay), width = v (P::ReverbWidth) * 0.01f, mix = v (P::ReverbMix) * 0.01f;
            auto plot = a.withTrimmedBottom (12.0f);
            const float mid = plot.getCentreY();
            gridLine (g, plot.getX(), mid, plot.getRight(), mid, true);

            const float rt = 0.4f + 7.5f * decay * decay;                // seconds (illustrative)
            const float spanMs = pre + rt * 1000.0f * 0.9f + 60.0f;
            auto xAt = [&] (float ms) { return plot.getX() + plot.getWidth() * ms / spanMs; };
            const float half = plot.getHeight() * 0.5f - 4.0f;
            const auto accent = col (palette::accent);

            // Dry impulse.
            g.setColour (col (palette::text).withAlpha (0.8f));
            g.fillRoundedRectangle (juce::Rectangle<float> (4.0f, half * 2.0f).withCentre ({ xAt (0.0f) + 2.0f, mid }), 2.0f);

            // Dense tail: lows (full decay) and highs (shorter with damping), L up / R down scaled by width.
            juce::Random rng (11);
            const int bars = 140;
            for (int layer = 0; layer < 2; ++layer)
            {
                const float rtL = layer == 0 ? rt : rt * (1.0f - 0.8f * damp);
                for (int i = 0; i < bars; ++i)
                {
                    const float ms = pre + (spanMs - pre) * (float) i / (float) bars;
                    const float tSec = (ms - pre) * 0.001f;
                    const float env = std::pow (10.0f, -3.0f * tSec / juce::jmax (0.05f, rtL)) * (0.35f + 0.65f * mix);
                    const float density = juce::jlimit (0.2f, 1.0f, 0.3f + size + tSec * 4.0f);
                    const float jitterL = 0.55f + 0.45f * rng.nextFloat(), jitterR = 0.55f + 0.45f * rng.nextFloat();
                    if (rng.nextFloat() > density) continue;
                    const float x = xAt (ms);
                    const float hu = half * env * jitterL, hd = half * env * (1.0f - 0.7f * (1.0f - width) * 0.0f) * (width * jitterR + (1.0f - width) * jitterL);
                    g.setColour (layer == 0 ? accent.withAlpha (0.35f) : accent.withAlpha (0.9f));
                    const float w = layer == 0 ? 2.2f : 1.4f;
                    g.fillRect (juce::Rectangle<float> (x, mid - hu * (layer == 0 ? 1.0f : 0.8f), w, hu * (layer == 0 ? 1.0f : 0.8f)));
                    g.fillRect (juce::Rectangle<float> (x, mid, w, hd * (layer == 0 ? 1.0f : 0.8f)));
                }
            }
            // Early reflections: a few spikes spaced by size.
            for (int k = 1; k <= 6; ++k)
            {
                const float ms = pre + (4.0f + 30.0f * size) * (float) k * (0.7f + 0.1f * (float) (k % 3));
                const float h = half * (0.7f - 0.08f * (float) k) * (0.35f + 0.65f * mix);
                g.setColour (accent.withAlpha (0.85f));
                g.fillRect (juce::Rectangle<float> (xAt (ms), mid - h, 1.5f, 2.0f * h));
            }
            if (pre > 0.5f)
            {
                const float x0 = xAt (0.0f) + 6.0f, x1 = xAt (pre);
                if (x1 - x0 > 30.0f)
                    gridLabel (g, msText (pre), { x0, mid - 14.0f, x1 - x0, 11.0f });
            }
            gridLabel (g, "RT60 ~ " + juce::String (rt, 1) + " s", { plot.getRight() - 140.0f, plot.getY(), 140.0f, 11.0f }, juce::Justification::centredRight);
            gridLabel (g, "0", { plot.getX(), a.getBottom() - 11.0f, 20.0f, 11.0f }, juce::Justification::centredLeft);
            gridLabel (g, msText (spanMs), { plot.getRight() - 60.0f, a.getBottom() - 11.0f, 60.0f, 11.0f }, juce::Justification::centredRight);
        }

        // ---------------------------------------------------------------- EQ
        juce::Rectangle<float> eqArea() const { return getLocalBounds().toFloat().reduced (16.0f, 14.0f).withTrimmedBottom (12.0f); }

        std::pair<P, P> eqParams (int band) const
        {
            if (band == 0) return { P::EqLowFreq, P::EqLowGain };
            if (band == 1) return { P::EqMidFreq, P::EqMidGain };
            return { P::EqHighFreq, P::EqHighGain };
        }

        static double biquadDb (double b0, double b1, double b2, double a0, double a1, double a2, double w)
        {
            const std::complex<double> z1 = std::polar (1.0, -w), z2 = std::polar (1.0, -2.0 * w);
            const auto h = (b0 + b1 * z1 + b2 * z2) / (a0 + a1 * z1 + a2 * z2);
            return 20.0 * std::log10 (juce::jmax (1.0e-6, std::abs (h)));
        }

        float eqDb (float hz) const
        {
            // RBJ cookbook: low shelf (S = 1), peaking, high shelf at 48 kHz.
            const double fs = 48000.0, w = juce::MathConstants<double>::twoPi * hz / fs;
            double total = 0.0;
            for (int band = 0; band < 3; ++band)
            {
                const auto [fp, gp] = eqParams (band);
                const double f0 = v (fp), gain = v (gp);
                if (std::abs (gain) < 0.01) continue;
                const double A = std::pow (10.0, gain / 40.0), w0 = juce::MathConstants<double>::twoPi * f0 / fs;
                const double cw = std::cos (w0), sw = std::sin (w0);
                if (band == 1)
                {
                    const double alpha = sw / (2.0 * (double) v (P::EqMidQ));
                    total += biquadDb (1 + alpha * A, -2 * cw, 1 - alpha * A, 1 + alpha / A, -2 * cw, 1 - alpha / A, w);
                }
                else
                {
                    const double alpha = sw / 2.0 * std::sqrt (2.0), sq = 2.0 * std::sqrt (A) * alpha;
                    if (band == 0)
                        total += biquadDb (A * ((A + 1) - (A - 1) * cw + sq), 2 * A * ((A - 1) - (A + 1) * cw), A * ((A + 1) - (A - 1) * cw - sq),
                                           (A + 1) + (A - 1) * cw + sq, -2 * ((A - 1) + (A + 1) * cw), (A + 1) + (A - 1) * cw - sq, w);
                    else
                        total += biquadDb (A * ((A + 1) + (A - 1) * cw + sq), -2 * A * ((A - 1) + (A + 1) * cw), A * ((A + 1) + (A - 1) * cw - sq),
                                           (A + 1) - (A - 1) * cw + sq, 2 * ((A - 1) - (A + 1) * cw), (A + 1) - (A - 1) * cw - sq, w);
                }
            }
            return (float) total;
        }

        juce::Point<float> eqHandle (int band) const
        {
            const auto a = eqArea();
            const auto [fp, gp] = eqParams (band);
            return { xForHz (a, v (fp)), a.getY() + a.getHeight() * (18.0f - v (gp)) / 36.0f };
        }

        int eqHandleAt (juce::Point<float> p) const
        {
            for (int b = 0; b < 3; ++b)
                if (eqHandle (b).getDistanceFrom (p) < 12.0f) return b;
            return -1;
        }

        void paintEq (juce::Graphics& g, juce::Rectangle<float>)
        {
            const auto plot = eqArea();
            frequencyGrid (g, plot, -18.0f, 18.0f, { -12, -6, 0, 6, 12 });
            const auto accent = col (palette::accent);
            const auto line = responsePath (plot, -18.0f, 18.0f, [this] (float hz) { return eqDb (hz); });
            strokeHero (g, line, plot, accent, true, plot.getCentreY());

            const char* names[3] = { "LOW", "MID", "HIGH" };
            for (int b = 0; b < 3; ++b)
            {
                const auto p = eqHandle (b);
                const bool active = hover == b || dragBand == b;
                g.setColour (accent.withAlpha (active ? 0.35f : 0.2f));
                g.fillEllipse (juce::Rectangle<float> (active ? 20.0f : 15.0f, active ? 20.0f : 15.0f).withCentre (p));
                g.setColour (col (palette::display));
                g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre (p));
                g.setColour (accent);
                g.drawEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre (p), 2.0f);
                g.setFont (font (9.5f, true));
                g.drawText (names[b], juce::Rectangle<float> (40.0f, 12.0f).withCentre (p.translated (0.0f, p.y > plot.getY() + 20.0f ? -15.0f : 15.0f)),
                            juce::Justification::centred, false);
            }
            if (dragBand >= 0)
            {
                const auto [fp, gp] = eqParams (dragBand);
                const float db = v (gp);
                g.setColour (col (palette::text));
                g.setFont (font (Theme::fontSmall, true));
                g.drawText (hzText (v (fp)) + "   " + (db > 0 ? "+" : "") + juce::String (db, 1) + " dB",
                            plot.withHeight (14.0f).translated (50.0f, 2.0f), juce::Justification::topLeft, false);
            }
            else
                gridLabel (g, "DRAG THE BANDS  -  WHEEL ON MID = Q", { plot.getRight() - 240.0f, plot.getY(), 240.0f, 11.0f }, juce::Justification::centredRight);
        }

        // ---------------------------------------------------------------- compressor
        void paintComp (juce::Graphics& g, juce::Rectangle<float> a)
        {
            const float thr = v (P::CompThreshold), ratio = v (P::CompRatio), makeup = v (P::CompMakeup);
            const float atk = v (P::CompAttack), rel = v (P::CompRelease), mix = v (P::CompMix) * 0.01f;
            const auto accent = col (palette::accent);

            // Left: static curve, input / output -60..+6 dB.
            auto tr = a.removeFromLeft (juce::jmin (a.getHeight(), a.getWidth() * 0.4f)).withTrimmedBottom (12.0f).withTrimmedLeft (8.0f);
            auto xOf = [&] (float db) { return tr.getX() + tr.getWidth() * (db + 60.0f) / 66.0f; };
            auto yOf = [&] (float db) { return tr.getBottom() - tr.getHeight() * (db + 60.0f) / 66.0f; };
            for (int db : { -48, -36, -24, -12, 0 })
            {
                gridLine (g, xOf ((float) db), tr.getY(), xOf ((float) db), tr.getBottom(), db == 0);
                gridLine (g, tr.getX(), yOf ((float) db), tr.getRight(), yOf ((float) db), db == 0);
                gridLabel (g, juce::String (db), { xOf ((float) db) - 15.0f, tr.getBottom() + 1.0f, 30.0f, 11.0f });
            }
            g.setColour (col (palette::textFaint).withAlpha (0.5f));
            g.drawLine (xOf (-60.0f), yOf (-60.0f), xOf (6.0f), yOf (6.0f), 1.0f);
            juce::Path curve;
            const float knee = 6.0f;
            for (int i = 0; i <= 132; ++i)
            {
                const float in = -60.0f + 66.0f * (float) i / 132.0f;
                float out;
                if (in < thr - knee * 0.5f)       out = in;
                else if (in > thr + knee * 0.5f)  out = thr + (in - thr) / ratio;
                else { const float d = in - thr + knee * 0.5f; out = in + (1.0f / ratio - 1.0f) * d * d / (2.0f * knee); }
                out = mix * (out + makeup) + (1.0f - mix) * in;
                const juce::Point<float> pt (xOf (in), yOf (juce::jlimit (-62.0f, 8.0f, out)));
                if (i == 0) curve.startNewSubPath (pt); else curve.lineTo (pt);
            }
            g.saveState();
            g.reduceClipRegion (tr.toNearestInt().expanded (2));
            strokeHero (g, curve, tr, accent, false);
            g.restoreState();
            g.setColour (col (palette::warning).withAlpha (0.8f));
            const float tx = xOf (thr);
            g.fillRect (juce::Rectangle<float> (tx - 0.5f, tr.getY(), 1.0f, tr.getHeight()));
            gridLabel (g, "THRESHOLD", { tx + 4.0f, tr.getY(), 80.0f, 11.0f }, juce::Justification::centredLeft);

            // Right: gain reduction of a burst (attack / release), time in ms.
            a.removeFromLeft (24.0f);
            auto env = a.withTrimmedBottom (12.0f);
            const float burst = juce::jmax (4.0f * atk, 120.0f);
            const float span = burst + 4.0f * rel + 20.0f;
            auto xT = [&] (float ms) { return env.getX() + env.getWidth() * ms / span; };
            const float grMax = juce::jmax (0.0f, (0.0f - thr) * (1.0f - 1.0f / ratio));   // for a 0 dB burst
            const float top = env.getY() + 14.0f, depthPx = env.getHeight() - 20.0f;
            gridLine (g, env.getX(), top, env.getRight(), top, true);
            // the burst
            g.setColour (col (palette::textDim).withAlpha (0.25f));
            g.fillRect (juce::Rectangle<float> (xT (10.0f), top - 8.0f, xT (10.0f + burst) - xT (10.0f), 4.0f));
            gridLabel (g, "INPUT BURST", { xT (10.0f), env.getY() - 2.0f, 100.0f, 11.0f }, juce::Justification::centredLeft);
            juce::Path gr;
            float level = 0.0f;
            const int n = 240;
            for (int i = 0; i <= n; ++i)
            {
                const float ms = span * (float) i / (float) n;
                const bool on = ms >= 10.0f && ms < 10.0f + burst;
                const float target = on ? grMax : 0.0f;
                const float tau = on ? atk : rel;
                level += (target - level) * (1.0f - std::exp (-(span / (float) n) / juce::jmax (0.1f, tau)));
                const float y = top + depthPx * juce::jlimit (0.0f, 1.0f, level / 40.0f);
                if (i == 0) gr.startNewSubPath (xT (ms), y); else gr.lineTo (xT (ms), y);
            }
            juce::Path fill (gr);
            fill.lineTo (env.getRight(), top);
            fill.lineTo (env.getX(), top);
            fill.closeSubPath();
            g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.0f), 0.0f, top, accent.withAlpha (0.22f), 0.0f, env.getBottom(), false));
            g.fillPath (fill);
            g.setColour (accent.withAlpha (0.18f));
            g.strokePath (gr, juce::PathStrokeType (5.0f));
            g.setColour (accent);
            g.strokePath (gr, juce::PathStrokeType (1.8f));
            gridLabel (g, "GAIN REDUCTION  " + juce::String (grMax, 1) + " dB", { env.getRight() - 200.0f, env.getBottom() - 12.0f, 200.0f, 11.0f },
                       juce::Justification::centredRight);
            gridLabel (g, "0", { env.getX(), a.getBottom() - 11.0f, 20.0f, 11.0f }, juce::Justification::centredLeft);
            gridLabel (g, msText (span), { env.getRight() - 60.0f, a.getBottom() - 11.0f, 60.0f, 11.0f }, juce::Justification::centredRight);
        }

        Processor& processor;
        FxSlotStrip& strip;
        int type = -1;
        std::vector<float> last;
        float phase = 0.0f;
        int hover = -1, dragBand = -1;
    };

    //==============================================================================
    /** Stylised Sympathetic strings: one line per string, excited by the output level x Amount. */
    class StringsView : public juce::Component, public Tickable
    {
    public:
        explicit StringsView (Processor& p) : processor (p) {}

        float v (P p) const
        {
            if (auto* r = processor.apvts.getRawParameterValue (info (p).id))
                return r->load (std::memory_order_relaxed);
            return info (p).def;
        }

        void tick() override
        {
            const float amount = v (P::SympAmount) * 0.01f;
            const float peak = juce::jlimit (0.0f, 1.0f, processor.outputPeak.load (std::memory_order_relaxed) * 3.0f);
            // Rings out with Decay, damped by Damping (per 30 Hz tick).
            const float release = 0.80f + 0.19f * (v (P::SympDecay) * 0.01f) * (1.0f - 0.5f * v (P::SympDamp) * 0.01f);
            const float target = amount * std::sqrt (peak);
            energy = target > energy ? energy + (target - energy) * 0.5f : energy * release;
            if (energy < 0.002f) energy = 0.0f;
            time += 1.0f / 30.0f;

            const int strings = (int) std::lround (v (P::SympStrings)), memory = (int) std::lround (v (P::SympMemory));
            const float bright = v (P::SympBright), snap = v (P::SympSnap);
            const bool changed = strings != shownStrings || memory != shownMemory || std::abs (bright - shownBright) > 0.5f
                                 || std::abs (amount - shownAmount) > 0.005f || snap != shownSnap;
            if (energy > 0.0f || changed || wasMoving)
            {
                wasMoving = energy > 0.0f;
                shownStrings = strings; shownMemory = memory; shownBright = bright; shownAmount = amount; shownSnap = snap;
                repaint();
            }
        }

        void paint (juce::Graphics& g) override
        {
            const auto bounds = getLocalBounds().toFloat();
            paintDisplay (g, bounds, col (palette::accent));
            auto a = bounds.reduced (14.0f, 12.0f);
            auto footer = a.removeFromBottom (18.0f);
            a.removeFromBottom (6.0f);

            const int strings = juce::jlimit (1, 12, (int) std::lround (v (P::SympStrings)));
            const int memory = juce::jlimit (1, 8, (int) std::lround (v (P::SympMemory)));
            const float amount = v (P::SympAmount) * 0.01f, bright = v (P::SympBright) * 0.01f;
            const auto accent = col (palette::accent);
            const auto cold = col (palette::accentDim);

            // Bridges.
            g.setColour (col (palette::panelEdge).brighter (0.25f));
            g.fillRoundedRectangle (juce::Rectangle<float> (3.0f, a.getHeight()).withCentre ({ a.getX() + 1.5f, a.getCentreY() }), 1.5f);
            g.fillRoundedRectangle (juce::Rectangle<float> (3.0f, a.getHeight()).withCentre ({ a.getRight() - 1.5f, a.getCentreY() }), 1.5f);

            const float spacing = a.getHeight() / (float) strings;
            for (int s = 0; s < strings; ++s)
            {
                const float y = a.getY() + spacing * ((float) s + 0.5f);
                // Strings beyond the memory pair up with a remembered note (detuned): drawn as its twin.
                const bool twin = s >= memory;
                const float mode = 1.0f + (float) (s % 3);
                const float freq = 3.0f + 1.7f * (float) s;
                const float amp = energy * spacing * 0.42f * (twin ? 0.6f : 1.0f);
                juce::Path p;
                const int n = 64;
                for (int i = 0; i <= n; ++i)
                {
                    const float t = (float) i / (float) n;
                    const float dy = amp * std::sin (juce::MathConstants<float>::pi * mode * t) * std::sin (kTau * freq * time + (float) s);
                    const float x = a.getX() + 3.0f + (a.getWidth() - 6.0f) * t;
                    if (i == 0) p.startNewSubPath (x, y + dy); else p.lineTo (x, y + dy);
                }
                const float glow = juce::jlimit (0.0f, 1.0f, energy * 1.6f);
                const auto c = cold.interpolatedWith (accent, 0.25f + 0.75f * juce::jmax (glow, amount * 0.35f)).brighter (0.4f * bright * glow);
                if (glow > 0.02f)
                {
                    g.setColour (c.withAlpha (0.2f * glow));
                    g.strokePath (p, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved));
                }
                g.setColour (twin ? c.withAlpha (0.55f) : c);
                g.strokePath (p, juce::PathStrokeType (twin ? 1.0f : 1.4f + 0.6f * bright, juce::PathStrokeType::curved));
            }

            // Memory slots.
            g.setFont (font (9.5f, true).withExtraKerningFactor (0.1f));
            g.setColour (col (palette::textFaint));
            g.drawText ("MEMORY", footer.removeFromLeft (52.0f), juce::Justification::centredLeft, false);
            for (int m = 0; m < 8; ++m)
            {
                const auto r = juce::Rectangle<float> (16.0f, 8.0f).withCentre ({ footer.getX() + 10.0f + (float) m * 20.0f, footer.getCentreY() });
                g.setColour (m < memory ? accent.withAlpha (0.25f + 0.75f * juce::jmax (amount, 0.3f)) : col (palette::knobTrack));
                g.fillRoundedRectangle (r, 3.0f);
            }
            g.setColour (amount > 0.0f ? col (palette::textDim) : col (palette::warning).withAlpha (0.85f));
            g.setFont (font (Theme::fontSmall));
            g.drawText (amount > 0.0f ? juce::String (strings) + (strings == 1 ? " string" : " strings")
                                      : juce::String ("Off - raise Amount"),
                        footer, juce::Justification::centredRight, false);
        }

    private:
        Processor& processor;
        float energy = 0.0f, time = 0.0f;
        int shownStrings = -1, shownMemory = -1;
        float shownBright = -1.0f, shownAmount = -1.0f, shownSnap = -1.0f;
        bool wasMoving = false;
    };
}

//==============================================================================
struct FxPage::Impl
{
    struct Controls
    {
        std::vector<juce::Component*> primary, secondary;
        juce::String caption;
    };

    Processor& processor;
    ModulationModel model;

    SectionPanel rackCard { "FX Rack" };
    FxSlotStrip strip;
    juce::Label rackHint;

    SectionPanel fxCard { "Effect" };
    FxDisplay display;
    std::array<std::unique_ptr<ChoiceButton>, kNumFxSlots> slotChoice;
    IconButton removeButton { IconButton::Icon::Close, "Empty this slot" };
    std::array<Controls, NumTypes> controls;
    std::vector<std::unique_ptr<juce::Component>> owned;
    ChoiceButton* delayDivision = nullptr;
    ModKnob* delayTime = nullptr;
    int shownType = -1;

    SectionPanel sympCard { "Sympathetic", col (palette::accent) };
    StringsView strings;
    std::vector<juce::Component*> sympPrimary, sympSecondary;

    Impl (FxPage& page, Processor& p) : processor (p), model (p), strip (p), display (p, strip), strings (p)
    {
        rackCard.addAndMakeVisible (strip);
        page.addTickable (&strip);
        rackHint.setText ("Signal flows left to right  -  drag a tile to reorder  -  chevron to change", juce::dontSendNotification);
        rackHint.setFont (font (Theme::fontSmall));
        rackHint.setColour (juce::Label::textColourId, col (palette::textFaint));
        rackHint.setJustificationType (juce::Justification::centredRight);
        rackCard.addAndMakeVisible (rackHint);

        fxCard.addAndMakeVisible (display);
        page.addTickable (&display);
        for (int s = 0; s < kNumFxSlots; ++s)
        {
            slotChoice[(size_t) s] = makeChoice (p, fxCard, fxSlotType (s));
            slotChoice[(size_t) s]->setTooltip ("Effect in slot " + juce::String (s + 1));
        }
        removeButton.setFrameless (true);
        removeButton.onClick = [this] { strip.setSlotType (strip.getSelected(), 0); };
        fxCard.addAndMakeVisible (removeButton);

        auto knob = [&] (P prm, int d, const juce::String& label = {}) -> juce::Component*
        {
            auto k = makeKnob (p, model, page, fxCard, prm, d, label);
            auto* raw = k.get();
            owned.push_back (std::move (k));
            return raw;
        };
        auto choice = [&] (P prm, const juce::String& label) -> ChoiceButton*
        {
            auto c = makeChoice (p, fxCard, prm, label);
            auto* raw = c.get();
            owned.push_back (std::move (c));
            return raw;
        };
        auto pill = [&] (P prm, const juce::String& label) -> juce::Component*
        {
            auto t = std::make_unique<TogglePill> (p.apvts, info (prm).id);
            t->setLabel (label);
            fxCard.addAndMakeVisible (*t);
            auto* raw = t.get();
            owned.push_back (std::move (t));
            return raw;
        };
        const int L = Theme::knobPrimary, S = Theme::knobSecondary;

        controls[Distortion].primary = { choice (P::DistType, "Type"), knob (P::DistDrive, L), knob (P::DistTone, L), knob (P::DistMix, L) };
        controls[Chorus].primary = { knob (P::ChorusRate, L), knob (P::ChorusDepth, L), knob (P::ChorusFeedback, L), knob (P::ChorusMix, L) };
        controls[Phaser].primary = { knob (P::PhaserRate, L), knob (P::PhaserDepth, L), knob (P::PhaserFeedback, L), choice (P::PhaserStages, "Stages"),
                                     knob (P::PhaserMix, L) };
        controls[Flanger].primary = { knob (P::FlangerRate, L), knob (P::FlangerDepth, L), knob (P::FlangerFeedback, L), knob (P::FlangerMix, L) };
        delayTime = static_cast<ModKnob*> (knob (P::DelayTime, L));
        delayDivision = choice (P::DelayDiv, "Division");
        controls[Delay].primary = { delayTime, knob (P::DelayFeedback, L), knob (P::DelayMix, L), pill (P::DelaySync, "Sync"), pill (P::DelayPingPong, "Ping-Pong") };
        controls[Delay].secondary = { knob (P::DelayLowCut, S), knob (P::DelayHighCut, S) };
        controls[Delay].caption = "Feedback filter";
        controls[Reverb].primary = { knob (P::ReverbSize, L), knob (P::ReverbDecay, L), knob (P::ReverbMix, L) };
        controls[Reverb].secondary = { knob (P::ReverbDamp, S), knob (P::ReverbPredelay, S), knob (P::ReverbWidth, S) };
        controls[Reverb].caption = "Character";
        controls[Eq].primary = { knob (P::EqLowGain, L), knob (P::EqMidGain, L), knob (P::EqHighGain, L) };
        controls[Eq].secondary = { knob (P::EqLowFreq, S), knob (P::EqMidFreq, S), knob (P::EqMidQ, S), knob (P::EqHighFreq, S) };
        controls[Eq].caption = "Frequencies";
        controls[Compressor].primary = { knob (P::CompThreshold, L), knob (P::CompRatio, L), knob (P::CompMakeup, L), knob (P::CompMix, L) };
        controls[Compressor].secondary = { knob (P::CompAttack, S), knob (P::CompRelease, S) };
        controls[Compressor].caption = "Timing";

        // Sympathetic.
        sympCard.addAndMakeVisible (strings);
        page.addTickable (&strings);
        auto sk = [&] (P prm, int d, const juce::String& label = {}) -> juce::Component*
        {
            auto k = makeKnob (p, model, page, sympCard, prm, d, label);
            auto* raw = k.get();
            owned.push_back (std::move (k));
            return raw;
        };
        sympPrimary = { sk (P::SympAmount, L, "Amount"), sk (P::SympDecay, L, "Decay"), sk (P::SympBright, L, "Brightness") };
        auto memory = makeChoice (p, sympCard, P::SympMemory, "Memory");
        auto stringCount = makeChoice (p, sympCard, P::SympStrings, "Strings");
        auto snap = std::make_unique<TogglePill> (p.apvts, info (P::SympSnap).id);
        snap->setLabel ("Snap to Key");
        sympCard.addAndMakeVisible (*snap);
        sympSecondary = { sk (P::SympDamp, S, "Damping"), memory.get(), stringCount.get(), snap.get() };
        owned.push_back (std::move (memory));
        owned.push_back (std::move (stringCount));
        owned.push_back (std::move (snap));

        strip.onSelect = [this] (int) { refresh(); };
        refresh();
    }

    void refresh()
    {
        const int slot = strip.getSelected();
        const int type = strip.typeOf (slot);
        for (int s = 0; s < kNumFxSlots; ++s)
            slotChoice[(size_t) s]->setVisible (s == slot);
        removeButton.setVisible (type != None);
        if (type != shownType)
        {
            for (int t = 0; t < NumTypes; ++t)
            {
                for (auto* c : controls[(size_t) t].primary)   c->setVisible (t == type);
                for (auto* c : controls[(size_t) t].secondary) c->setVisible (t == type);
            }
            delayDivision->setVisible (false);
            shownType = type;
            layoutFx();
        }
        updateDelaySync();
        display.setType (type);
        fxCard.setTitle (type == None ? "Slot " + juce::String (slot + 1)
                                      : FxSlotStrip::typeName (type) + "  -  slot " + juce::String (slot + 1));
        fxCard.setActive (type != None);
        if (type != None && strip.isDuplicate (slot))
            fxCard.setTitle (FxSlotStrip::typeName (type) + "  -  slot " + juce::String (slot + 1) + " (bypassed)");
        layoutHeader();
    }

    void updateDelaySync()
    {
        const bool delayShown = shownType == Delay;
        bool sync = false;
        if (auto* r = processor.apvts.getRawParameterValue (info (P::DelaySync).id))
            sync = r->load (std::memory_order_relaxed) >= 0.5f;
        delayTime->setVisible (delayShown && ! sync);
        delayDivision->setVisible (delayShown && sync);
    }

    void layoutHeader()
    {
        const auto header = fxCard.headerArea();
        auto r = header;
        removeButton.setBounds (r.removeFromRight (22).withSizeKeepingCentre (22, 22));
        r.removeFromRight (6);
        const auto choiceBounds = r.removeFromRight (150).withSizeKeepingCentre (150, 22);
        for (auto& c : slotChoice)
            c->setBounds (choiceBounds);
    }

    void layoutFx()
    {
        auto c = fxCard.contentArea();
        const int primaryH = ModKnob::heightFor (Theme::knobPrimary), secondaryH = ModKnob::heightFor (Theme::knobSecondary);
        auto controlsArea = c.removeFromBottom (primaryH + 16 + secondaryH);
        c.removeFromBottom (Theme::spaceS);
        display.setBounds (c);

        fxCard.clearCaptions();
        if (! juce::isPositiveAndBelow (shownType, (int) NumTypes) || shownType == None)
            return;
        const auto& ctl = controls[(size_t) shownType];
        auto placeRow = [] (const std::vector<juce::Component*>& comps, juce::Rectangle<int> row, int cellW)
        {
            const int n = (int) comps.size();
            const int w = juce::jmin (cellW, row.getWidth() / juce::jmax (1, n));
            auto r = row.withSizeKeepingCentre (w * n, row.getHeight());
            for (auto* comp : comps)
                comp->setBounds (r.removeFromLeft (w));
        };
        if (ctl.secondary.empty())
        {
            placeRow (ctl.primary, controlsArea.withSizeKeepingCentre (controlsArea.getWidth(), primaryH), 118);
        }
        else
        {
            placeRow (ctl.primary, controlsArea.removeFromTop (primaryH), 118);
            fxCard.addCaption (ctl.caption, controlsArea.removeFromTop (16));
            placeRow (ctl.secondary, controlsArea.removeFromTop (secondaryH), 96);
        }
        delayDivision->setBounds (delayTime->getBounds().withSizeKeepingCentre (juce::jmin (delayTime->getWidth(), 100), delayTime->getHeight()));
    }
};

//==============================================================================
FxPage::FxPage (Processor& p)
{
    impl = std::make_unique<Impl> (*this, p);
    for (auto* c : std::initializer_list<juce::Component*> { &impl->rackCard, &impl->fxCard, &impl->sympCard })
        addAndMakeVisible (c);
}

FxPage::~FxPage() = default;

void FxPage::selectSlot (int slot)
{
    impl->strip.setSelected (slot);
    impl->refresh();
}

void FxPage::pageShown()
{
    impl->model.refresh();
    impl->strip.tick();
    impl->refresh();
}

void FxPage::tick()
{
    impl->model.refresh();
    Page::tick();
    // The slot's type can change from the host or a preset: follow it.
    if (impl->strip.typeOf (impl->strip.getSelected()) != impl->shownType)
        impl->refresh();
    else
        impl->updateDelaySync();
}

void FxPage::resized()
{
    auto& I = *impl;
    auto area = getLocalBounds().reduced (Theme::pageMargin);

    I.rackCard.setBounds (area.removeFromTop (Theme::sectionHeader + 84 + 10));
    area.removeFromTop (Theme::gutter);
    {
        auto c = I.rackCard.contentArea();
        I.strip.setBounds (c.removeFromTop (84));
        I.rackHint.setBounds (I.rackCard.headerArea());
    }

    I.sympCard.setBounds (area.removeFromRight (330));
    area.removeFromRight (Theme::gutter);
    I.fxCard.setBounds (area);
    I.shownType = -1;
    I.refresh();

    // Sympathetic: strings display, then Amount / Decay / Brightness, then Damping / Memory / Strings / Snap.
    {
        I.sympCard.clearCaptions();
        auto c = I.sympCard.contentArea();
        const int primaryH = ModKnob::heightFor (Theme::knobPrimary), secondaryH = ModKnob::heightFor (Theme::knobSecondary);
        auto bottom = c.removeFromBottom (primaryH + 16 + secondaryH);
        c.removeFromBottom (Theme::spaceS);
        I.strings.setBounds (c);
        layout::spread ({ I.sympPrimary[0], I.sympPrimary[1], I.sympPrimary[2] }, bottom.removeFromTop (primaryH));
        I.sympCard.addCaption ("Body", bottom.removeFromTop (16));
        layout::spread ({ I.sympSecondary[0], I.sympSecondary[1], I.sympSecondary[2], I.sympSecondary[3] }, bottom.removeFromTop (secondaryH));
    }
}

std::unique_ptr<Page> makeFxPage (Processor& p)
{
    return std::make_unique<FxPage> (p);
}

} // namespace verdant::ui
