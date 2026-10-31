// The ARP/SEQ page (CONTRACT §7, DESIGN.md):
//   left    Arp card (On, a pattern preview drawn from Mode / Octaves / Gate / Swing, Mode, Octaves,
//           Rate, Gate, Swing, Snap to Key) over the global Key & Scale card (Key, Scale, Pitch Snap
//           and a one-octave keyboard that lights the scale's notes, root marked)
//   right   Step sequencer card: StepGrid (16 steps x pitch / velocity / gate / mod, playing step
//           highlighted) above the Seq controls (On, Steps, Rate, Gate, Swing, Snap to Key), with
//           Clear and Randomise in the card header

#include "Pages.h"

#include "ModKnob.h"
#include "StepGrid.h"
#include "Widgets.h"

#include "../PluginProcessor.h"

#include <ap/Scales.h>

namespace verdant::ui
{

namespace
{
    juce::Colour col (juce::uint32 c) { return juce::Colour (c); }

    float raw (Processor& p, P param)
    {
        if (auto* v = p.apvts.getRawParameterValue (info (param).id))
            return v->load (std::memory_order_relaxed);
        return info (param).def;
    }

    void setParam (Processor& p, P param, float plain)
    {
        if (auto* q = p.apvts.getParameter (info (param).id))
        {
            q->beginChangeGesture();
            q->setValueNotifyingHost (q->convertTo0to1 (plain));
            q->endChangeGesture();
        }
    }

    //==============================================================================
    /** The arp's output for a held C-E-G chord over two bars, as a piano roll. */
    class ArpPatternView : public juce::Component, public Tickable
    {
    public:
        explicit ArpPatternView (Processor& p) : processor (p) {}

        void tick() override
        {
            const std::array<float, 5> now { raw (processor, P::ArpOn), raw (processor, P::ArpMode), raw (processor, P::ArpOct),
                                             raw (processor, P::ArpGate), raw (processor, P::ArpSwing) };
            if (now != shown) { shown = now; repaint(); }
        }

        void paint (juce::Graphics& g) override
        {
            const auto bounds = getLocalBounds().toFloat();
            paintDisplay (g, bounds, col (palette::accent));
            auto a = bounds.reduced (12.0f, 10.0f);
            const bool on = raw (processor, P::ArpOn) >= 0.5f;
            const int mode = (int) std::lround (raw (processor, P::ArpMode));
            const int octaves = juce::jlimit (1, 4, (int) std::lround (raw (processor, P::ArpOct)));
            const float gate = raw (processor, P::ArpGate) * 0.01f, swing = raw (processor, P::ArpSwing) * 0.01f;

            // Held chord C4 E4 G4 -> the note list for this mode.
            const int chord[3] = { 0, 4, 7 };
            std::vector<int> up;
            for (int o = 0; o < octaves; ++o)
                for (int n : chord) up.push_back (n + 12 * o);
            std::vector<std::vector<int>> seq;
            auto add = [&seq] (int n) { seq.push_back ({ n }); };
            const int stepsShown = 16;
            switch (mode)
            {
                case 1: for (int i = (int) up.size() - 1; i >= 0; --i) add (up[(size_t) i]); break;
                case 2: for (size_t i = 0; i < up.size(); ++i) add (up[i]); for (int i = (int) up.size() - 2; i > 0; --i) add (up[(size_t) i]); break;
                case 3: for (int i = (int) up.size() - 1; i >= 0; --i) add (up[(size_t) i]); for (size_t i = 1; i + 1 < up.size(); ++i) add (up[i]); break;
                case 4: { juce::Random r (5); for (int i = 0; i < stepsShown; ++i) add (up[(size_t) r.nextInt ((int) up.size())]); break; }
                case 5: for (int o = 0; o < octaves; ++o) for (int n : { 0, 7, 4 }) add (n + 12 * o); break;   // as played: C G E
                case 6: for (int o = 0; o < octaves; ++o) seq.push_back ({ 12 * o, 4 + 12 * o, 7 + 12 * o }); break;
                default: for (int n : up) add (n); break;
            }
            if (seq.empty()) return;

            const int top = 12 * octaves;
            const float rowH = a.getHeight() / (float) (top + 1);
            const float stepW = a.getWidth() / (float) stepsShown;
            for (int k = 0; k <= stepsShown; k += 4)
            {
                g.setColour (col (palette::grid).brighter (k % 8 == 0 ? 0.15f : 0.0f));
                g.fillRect (juce::Rectangle<float> (std::round (a.getX() + stepW * (float) k), a.getY(), 1.0f, a.getHeight()));
            }
            for (int o = 0; o <= octaves; ++o)
            {
                const float y = a.getBottom() - rowH * (float) (12 * o) - rowH * 0.5f;
                g.setColour (col (palette::grid));
                g.fillRect (juce::Rectangle<float> (a.getX(), std::round (y), a.getWidth(), 1.0f));
                g.setColour (col (palette::textFaint));
                g.setFont (font (9.0f));
                if (o < octaves)
                    g.drawText ("C" + juce::String (4 + o), juce::Rectangle<float> (a.getRight() - 26.0f, y - 11.0f, 24.0f, 10.0f), juce::Justification::centredRight, false);
            }

            const auto accent = col (palette::accent).withAlpha (on ? 1.0f : 0.4f);
            for (int s = 0; s < stepsShown; ++s)
            {
                const auto& notes = seq[(size_t) s % seq.size()];
                const float x = a.getX() + stepW * (float) s + (s % 2 == 1 ? swing * stepW / 3.0f : 0.0f);
                for (int n : notes)
                {
                    const float y = a.getBottom() - rowH * (float) (n + 1);
                    const auto r = juce::Rectangle<float> (x + 1.0f, y, juce::jmax (3.0f, stepW * gate - 2.0f), juce::jmax (4.0f, rowH * 1.4f));
                    g.setColour (accent.withAlpha (accent.getFloatAlpha() * 0.25f));
                    g.fillRoundedRectangle (r.expanded (1.5f), 3.0f);
                    g.setColour (accent);
                    g.fillRoundedRectangle (r, 2.0f);
                }
            }
            if (! on)
            {
                g.setColour (col (palette::textDim));
                g.setFont (font (Theme::fontSmall, true));
                g.drawText ("ARP OFF", bounds.reduced (12.0f, 8.0f), juce::Justification::topRight, false);
            }
        }

    private:
        Processor& processor;
        std::array<float, 5> shown { -1, -1, -1, -1, -1 };
    };

    //==============================================================================
    /** One octave, the scale's notes lit and the root marked. */
    class ScaleKeys : public juce::Component, public Tickable
    {
    public:
        explicit ScaleKeys (Processor& p) : processor (p) {}

        void tick() override
        {
            const int r = (int) std::lround (raw (processor, P::KeyRoot)), s = (int) std::lround (raw (processor, P::KeyScale));
            if (r != root || s != scale) { root = r; scale = s; repaint(); }
        }

        void paint (juce::Graphics& g) override
        {
            const auto sc = ap::Scale::fromType ((ap::ScaleType) juce::jlimit (0, (int) ap::ScaleType::Custom - 1, scale), root);
            auto a = getLocalBounds().toFloat();
            auto names = a.removeFromBottom (16.0f);
            a.removeFromBottom (4.0f);
            const float ww = a.getWidth() / 7.0f;
            static constexpr int whitePc[7] = { 0, 2, 4, 5, 7, 9, 11 };
            static constexpr int blackPc[5] = { 1, 3, 6, 8, 10 };
            static constexpr float blackX[5] = { 1, 2, 4, 5, 6 };
            const auto accent = col (palette::accent);
            for (int i = 0; i < 7; ++i)
            {
                const int pc = whitePc[i];
                const auto r = juce::Rectangle<float> (a.getX() + ww * (float) i, a.getY(), ww, a.getHeight()).reduced (1.0f, 0.0f);
                const bool in = sc.contains (pc);
                g.setColour (in ? col (0xffc9cfd6) : col (0xff4a515c));
                g.fillRoundedRectangle (r, 3.0f);
                if (in)
                {
                    g.setColour (pc == sc.root ? accent : accent.withAlpha (0.75f));
                    g.fillRoundedRectangle (juce::Rectangle<float> (r.getWidth() - 8.0f, 6.0f).withCentre ({ r.getCentreX(), r.getBottom() - 9.0f }), 3.0f);
                }
                if (pc == sc.root)
                {
                    g.setColour (col (palette::background));
                    g.setFont (font (9.5f, true));
                    g.drawText ("ROOT", r.withTrimmedBottom (14.0f).withTrimmedTop (r.getHeight() * 0.55f), juce::Justification::centred, false);
                }
            }
            for (int i = 0; i < 5; ++i)
            {
                const int pc = blackPc[i];
                const float bw = ww * 0.6f;
                const auto r = juce::Rectangle<float> (a.getX() + ww * blackX[i] - bw * 0.5f, a.getY(), bw, a.getHeight() * 0.58f);
                const bool in = sc.contains (pc);
                g.setColour (col (palette::panel));
                g.fillRoundedRectangle (r.expanded (1.0f, 0.0f), 3.0f);
                g.setColour (in ? col (0xff3a414c) : col (0xff0d0f12));
                g.fillRoundedRectangle (r, 2.5f);
                if (in)
                {
                    g.setColour (pc == sc.root ? accent : accent.withAlpha (0.75f));
                    g.fillRoundedRectangle (juce::Rectangle<float> (r.getWidth() - 6.0f, 5.0f).withCentre ({ r.getCentreX(), r.getBottom() - 7.0f }), 2.5f);
                }
            }
            // The scale spelled out from the root.
            juce::StringArray notes;
            for (int k = 0; k < 12; ++k)
                if (sc.contains (sc.root + k)) notes.add (ap::noteName (sc.root + k));
            g.setColour (col (palette::textDim));
            g.setFont (font (Theme::fontSmall, true));
            g.drawText (notes.joinIntoString ("  "), names, juce::Justification::centred, false);
        }

    private:
        Processor& processor;
        int root = -1, scale = -1;
    };

    //==============================================================================
    class ArpSeqPage final : public Page
    {
    public:
        explicit ArpSeqPage (Processor& p)
            : processor (p), model (p),
              arpOn (p.apvts, info (P::ArpOn).id), seqOn (p.apvts, info (P::SeqOn).id),
              pattern (p), keys (p), grid (p)
        {
            for (auto* c : std::initializer_list<juce::Component*> { &arpCard, &keyCard, &seqCard })
                addAndMakeVisible (c);

            // Arp.
            arpCard.addAndMakeVisible (arpOn);
            arpCard.addAndMakeVisible (pattern);
            addTickable (&pattern);
            arpMode = makeChoice (p, arpCard, P::ArpMode, "Mode");
            arpOct = makeChoice (p, arpCard, P::ArpOct, "Octaves");
            arpDiv = makeChoice (p, arpCard, P::ArpDiv, "Rate");
            arpGate = makeKnob (p, model, *this, arpCard, P::ArpGate, Theme::knobPrimary);
            arpSwing = makeKnob (p, model, *this, arpCard, P::ArpSwing, Theme::knobPrimary);
            arpSnap = std::make_unique<TogglePill> (p.apvts, info (P::ArpSnap).id);
            arpSnap->setLabel ("Snap to Key");
            arpCard.addAndMakeVisible (*arpSnap);
            arpOn.onChange = [this] (bool s) { arpCard.setActive (s); };
            arpCard.setActive (arpOn.getState());

            // Key & scale (global).
            keyCard.addAndMakeVisible (keys);
            addTickable (&keys);
            keyRoot = makeChoice (p, keyCard, P::KeyRoot, "Key");
            keyScale = makeChoice (p, keyCard, P::KeyScale, "Scale");
            pitchSnap = std::make_unique<TogglePill> (p.apvts, info (P::PitchSnap).id);
            pitchSnap->setLabel ("Pitch Snap");
            pitchSnap->setTooltip ("Pitch Snap to Key: oscillator pitch follows the key / scale");
            keyCard.addAndMakeVisible (*pitchSnap);
            keyHint.setText ("Used by arp / seq snapping, Pitch Snap and the Sympathetic body.", juce::dontSendNotification);
            keyHint.setFont (font (Theme::fontSmall));
            keyHint.setColour (juce::Label::textColourId, col (palette::textFaint));
            keyHint.setJustificationType (juce::Justification::centredLeft);
            keyCard.addAndMakeVisible (keyHint);

            // Sequencer.
            seqCard.addAndMakeVisible (seqOn);
            seqCard.addAndMakeVisible (grid);
            addTickable (&grid);
            seqSteps = makeChoice (p, seqCard, P::SeqSteps, "Steps");
            seqDiv = makeChoice (p, seqCard, P::SeqDiv, "Rate");
            seqGate = makeKnob (p, model, *this, seqCard, P::SeqGate, Theme::knobPrimary);
            seqSwing = makeKnob (p, model, *this, seqCard, P::SeqSwing, Theme::knobPrimary);
            seqSnap = std::make_unique<TogglePill> (p.apvts, info (P::SeqSnap).id);
            seqSnap->setLabel ("Snap to Key");
            seqCard.addAndMakeVisible (*seqSnap);
            seqOn.onChange = [this] (bool s) { seqCard.setActive (s); };
            seqCard.setActive (seqOn.getState());

            clearButton.setFrameless (true);
            randomButton.setFrameless (true);
            clearButton.onClick = [this] { clearPattern(); };
            randomButton.onClick = [this] { randomisePattern(); };
            seqCard.addAndMakeVisible (clearButton);
            seqCard.addAndMakeVisible (randomButton);
        }

        void pageShown() override { model.refresh(); }
        void tick() override
        {
            model.refresh();
            Page::tick();
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (Theme::pageMargin);
            auto left = area.removeFromLeft (340);
            area.removeFromLeft (Theme::gutter);
            const int primaryH = ModKnob::heightFor (Theme::knobPrimary);
            const int choiceH = Cell::labelHeight + Theme::controlHeight + 6;

            // Key card (bottom left).
            const int keyH = Theme::sectionHeader + choiceH + 8 + 70 + 4 + 20 + 8;
            keyCard.setBounds (left.removeFromBottom (keyH));
            left.removeFromBottom (Theme::gutter);
            {
                auto c = keyCard.contentArea();
                auto row = c.removeFromTop (choiceH);
                keyRoot->setBounds (row.removeFromLeft (70));
                row.removeFromLeft (8);
                pitchSnap->setBounds (row.removeFromRight (78));
                row.removeFromRight (8);
                keyScale->setBounds (row);
                c.removeFromTop (8);
                keyHint.setBounds (c.removeFromBottom (18));
                c.removeFromBottom (4);
                keys.setBounds (c);
            }

            // Arp card (top left).
            arpCard.setBounds (left);
            {
                const auto header = arpCard.headerArea();
                arpOn.setBounds (header.withTrimmedLeft (header.getWidth() - 34).withSizeKeepingCentre (34, 18));
                auto c = arpCard.contentArea();
                auto knobs = c.removeFromBottom (primaryH);
                c.removeFromBottom (4);
                auto choices = c.removeFromBottom (choiceH);
                c.removeFromBottom (Theme::spaceS);
                pattern.setBounds (c);
                layout::spread ({ arpMode.get(), arpOct.get(), arpDiv.get() }, choices);
                for (auto* ch : { arpMode.get(), arpOct.get(), arpDiv.get() })
                    ch->setBounds (ch->getBounds().reduced (3, 0));
                layout::spread ({ arpGate.get(), arpSwing.get(), arpSnap.get() }, knobs);
            }

            // Sequencer card (right).
            seqCard.setBounds (area);
            {
                const auto header = seqCard.headerArea();
                auto h = header;
                seqOn.setBounds (h.removeFromRight (34).withSizeKeepingCentre (34, 18));
                h.removeFromRight (10);
                randomButton.setBounds (h.removeFromRight (96).withSizeKeepingCentre (96, 22));
                h.removeFromRight (4);
                clearButton.setBounds (h.removeFromRight (70).withSizeKeepingCentre (70, 22));

                auto c = seqCard.contentArea();
                auto controls = c.removeFromBottom (primaryH);
                c.removeFromBottom (Theme::spaceS);
                grid.setBounds (c);
                controls.removeFromLeft (62);   // align with the grid's lanes
                const int w = controls.getWidth() / 5;
                for (auto* comp : std::initializer_list<juce::Component*> { seqSteps.get(), seqDiv.get(), seqGate.get(), seqSwing.get(), seqSnap.get() })
                    comp->setBounds (controls.removeFromLeft (w));
                for (auto* ch : { seqSteps.get(), seqDiv.get() })
                    ch->setBounds (ch->getBounds().reduced (12, 0));
            }
        }

    private:
        void clearPattern()
        {
            for (int s = 0; s < kNumSeqSteps; ++s)
                for (auto w : { SeqStepParam::Pitch, SeqStepParam::Vel, SeqStepParam::Gate, SeqStepParam::Mod })
                    setParam (processor, seqStepParam (s, w), info (seqStepParam (s, w)).def);
        }

        void randomisePattern()
        {
            // Musical rather than uniform: mostly small intervals, accents on the beat, a few rests.
            juce::Random r;
            static constexpr int intervals[] = { 0, 0, 0, 2, 3, 4, 5, 7, 7, 12, -5, -12 };
            for (int s = 0; s < kNumSeqSteps; ++s)
            {
                setParam (processor, seqStepParam (s, SeqStepParam::Pitch), (float) intervals[r.nextInt ((int) std::size (intervals))]);
                setParam (processor, seqStepParam (s, SeqStepParam::Vel), s % 4 == 0 ? 90.0f + 10.0f * r.nextFloat() : 45.0f + 40.0f * r.nextFloat());
                setParam (processor, seqStepParam (s, SeqStepParam::Gate), s % 4 == 0 || r.nextFloat() > 0.2f ? 1.0f : 0.0f);
                setParam (processor, seqStepParam (s, SeqStepParam::Mod), std::round (100.0f * r.nextFloat()));
            }
        }

        Processor& processor;
        ModulationModel model;

        SectionPanel arpCard { "Arpeggiator" };
        SectionPanel keyCard { "Key & Scale", col (palette::warning) };
        SectionPanel seqCard { "Step Sequencer", col (palette::modPhysics) };

        TogglePill arpOn, seqOn;
        ArpPatternView pattern;
        ScaleKeys keys;
        StepGrid grid;
        std::unique_ptr<ChoiceButton> arpMode, arpOct, arpDiv, keyRoot, keyScale, seqSteps, seqDiv;
        std::unique_ptr<ModKnob> arpGate, arpSwing, seqGate, seqSwing;
        std::unique_ptr<TogglePill> arpSnap, pitchSnap, seqSnap;
        juce::Label keyHint;
        IconButton clearButton { IconButton::Icon::Close, "Reset every step", "Clear" };
        IconButton randomButton { IconButton::Icon::Dice, "Randomise the pattern", "Randomise" };
    };
}

std::unique_ptr<Page> makeArpSeqPage (Processor& p)
{
    return std::make_unique<ArpSeqPage> (p);
}

} // namespace verdant::ui
