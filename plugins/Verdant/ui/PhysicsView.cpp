#include "PhysicsView.h"

#include "../PluginProcessor.h"

namespace verdant::ui
{

namespace
{
    juce::Colour col (juce::uint32 c) { return juce::Colour (c); }
}

PhysicsView::PhysicsView (Processor& p, Kind k) : processor (p), kind (k) {}

void PhysicsView::push (float a, float b)
{
    trail[(size_t) head] = { a, b };
    head = (head + 1) % historySize;
    filled = juce::jmin (historySize, filled + 1);
}

juce::Point<float> PhysicsView::history (int age) const
{
    return trail[(size_t) ((head - 1 - age + 2 * historySize) % historySize)];
}

void PhysicsView::tick()
{
    const auto& vis = processor.engine().visuals();
    const bool nowActive = vis.activeVoices.load (std::memory_order_relaxed) > 0;
    float a = 0, b = 0;
    switch (kind)
    {
        case Kind::Ball:   a = vis.physics[0].load (std::memory_order_relaxed); break;
        case Kind::Spring: a = vis.physics[1].load (std::memory_order_relaxed); break;
        case Kind::Orbit:  a = vis.physics[2].load (std::memory_order_relaxed); b = vis.physics[3].load (std::memory_order_relaxed); break;
    }
    if (! nowActive && ! active)
        return;
    active = nowActive;
    valueA = a;
    valueB = b;
    push (a, b);
    repaint();
}

void PhysicsView::paint (juce::Graphics& g)
{
    const auto colour = col (palette::modPhysics);
    const auto bounds = getLocalBounds().toFloat();
    paintDisplay (g, bounds, colour);
    const auto a = bounds.reduced (10.0f, 10.0f);

    switch (kind)
    {
        case Kind::Ball:
        {
            const float r = juce::jmin (7.0f, a.getWidth() * 0.08f);
            const float floorY = a.getBottom() - 2.0f;
            g.setColour (col (palette::grid).brighter (0.2f));
            g.fillRect (juce::Rectangle<float> (a.getX(), floorY, a.getWidth(), 1.0f));

            // Trace of recent heights scrolling left; the ball sits at the right end.
            const float ballX = a.getRight() - r - 4.0f;
            const float travel = floorY - r - (a.getY() + r);
            if (filled > 1)
            {
                juce::Path trace;
                for (int age = 0; age < filled; ++age)
                {
                    const auto h = history (age);
                    const juce::Point<float> p (ballX - (float) age * (ballX - a.getX()) / (float) (historySize - 1), floorY - r - h.x * travel);
                    if (age == 0) trace.startNewSubPath (p);
                    else          trace.lineTo (p);
                }
                g.setColour (colour.withAlpha (0.45f));
                g.strokePath (trace, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            const juce::Point<float> c (ballX, floorY - r - juce::jlimit (0.0f, 1.0f, valueA) * travel);
            g.setColour (col (palette::background).withAlpha (0.5f));
            g.fillEllipse (juce::Rectangle<float> (r * 2.4f, 3.0f).withCentre ({ ballX, floorY + 1.0f }));
            g.setColour (colour.withAlpha (0.3f));
            g.fillEllipse (juce::Rectangle<float> (r * 3.2f, r * 3.2f).withCentre (c));
            g.setColour (colour);
            g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
            break;
        }
        case Kind::Spring:
        {
            const float anchorY = a.getY() + 2.0f;
            const float cx = a.getX() + a.getWidth() * 0.35f;
            const float rest = a.getY() + a.getHeight() * 0.55f;
            const float massY = rest + juce::jlimit (-1.0f, 1.0f, valueA) * a.getHeight() * 0.3f;

            g.setColour (col (palette::grid).brighter (0.2f));
            g.fillRect (juce::Rectangle<float> (cx - 14.0f, anchorY, 28.0f, 2.0f));
            g.setColour (col (palette::grid));
            g.fillRect (juce::Rectangle<float> (a.getX(), std::round (rest), a.getWidth(), 1.0f));

            juce::Path coil;
            const int turns = 9;
            const float top = anchorY + 4.0f, bottom = massY - 8.0f;
            coil.startNewSubPath (cx, anchorY + 2.0f);
            coil.lineTo (cx, top);
            for (int i = 0; i < turns; ++i)
            {
                const float y = top + (bottom - top) * ((float) i + 0.5f) / (float) turns;
                coil.lineTo (cx + (i % 2 == 0 ? 7.0f : -7.0f), y);
            }
            coil.lineTo (cx, bottom);
            coil.lineTo (cx, massY - 6.0f);
            g.setColour (col (palette::textDim));
            g.strokePath (coil, juce::PathStrokeType (1.3f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));

            g.setColour (colour.withAlpha (0.3f));
            g.fillRoundedRectangle (juce::Rectangle<float> (24.0f, 18.0f).withCentre ({ cx, massY + 2.0f }), 5.0f);
            g.setColour (colour);
            g.fillRoundedRectangle (juce::Rectangle<float> (18.0f, 12.0f).withCentre ({ cx, massY + 2.0f }), 3.0f);

            // Displacement trace to the right of the mass.
            if (filled > 1)
            {
                juce::Path trace;
                const float x0 = cx + 18.0f, x1 = a.getRight();
                for (int age = 0; age < filled; ++age)
                {
                    const auto h = history (age);
                    const juce::Point<float> p (x0 + (float) age * (x1 - x0) / (float) (historySize - 1), rest + h.x * a.getHeight() * 0.3f);
                    if (age == 0) trace.startNewSubPath (p);
                    else          trace.lineTo (p);
                }
                g.setColour (colour.withAlpha (0.45f));
                g.strokePath (trace, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            break;
        }
        case Kind::Orbit:
        {
            float shape = 0.3f;
            if (auto* v = processor.apvts.getRawParameterValue (info (P::OrbitShape).id))
                shape = v->load (std::memory_order_relaxed) * 0.01f;
            const auto c = a.getCentre();
            const float rx = juce::jmin (a.getWidth(), a.getHeight()) * 0.42f;
            const float ry = rx * (1.0f - 0.6f * shape);

            g.setColour (col (palette::grid).brighter (0.1f));
            g.fillRect (juce::Rectangle<float> (c.x - rx - 4.0f, std::round (c.y), rx * 2.0f + 8.0f, 1.0f));
            g.fillRect (juce::Rectangle<float> (std::round (c.x), c.y - rx - 4.0f, 1.0f, rx * 2.0f + 8.0f));

            g.setColour (colour.withAlpha (0.35f));
            g.drawEllipse (juce::Rectangle<float> (rx * 2.0f, ry * 2.0f).withCentre (c), 1.2f);
            g.setColour (col (palette::textDim));
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (c));

            auto posOf = [&] (float x, float y) { return juce::Point<float> (c.x + x * rx, c.y - y * rx); };
            for (int age = filled - 1; age >= 1; --age)
            {
                const auto h = history (age);
                const float alpha = 0.5f * (1.0f - (float) age / (float) historySize);
                g.setColour (colour.withAlpha (alpha));
                g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre (posOf (h.x, h.y)));
            }
            const auto body = active ? posOf (valueA, valueB) : posOf (0.7071f, 0.7071f * (1.0f - 0.6f * shape));
            g.setColour (colour.withAlpha (0.3f));
            g.fillEllipse (juce::Rectangle<float> (16.0f, 16.0f).withCentre (body));
            g.setColour (colour);
            g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (body));
            break;
        }
    }
}

} // namespace verdant::ui
