#include "ModPage.h"

#include "EnvelopeEditor.h"
#include "LfoEditor.h"
#include "ModKnob.h"
#include "PhysicsView.h"
#include "Widgets.h"

#include "../PluginProcessor.h"

namespace verdant::ui
{

namespace
{
    juce::Colour col (juce::uint32 c) { return juce::Colour (c); }

    template <typename T>
    void setVisibleAll (std::initializer_list<T*> comps, bool v)
    {
        for (auto* c : comps)
            if (c != nullptr) c->setVisible (v);
    }

    //==============================================================================
    /** A bipolar amount bar (-100..100 %) bound to a parameter: drag horizontally, double-click = 0. */
    class AmountBar : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        AmountBar (juce::RangedAudioParameter& p) : param (p)
        {
            attachment = std::make_unique<juce::ParameterAttachment> (p, [this] (float v) { value = v; repaint(); });
            attachment->sendInitialUpdate();
            setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
            setTooltip (p.getName (64) + " (drag, double-click = 0)");
        }

        void setColour (juce::Colour c) { colour = c; repaint(); }

        void paint (juce::Graphics& g) override
        {
            // "+45" on the left, a slim bipolar bar on the right.
            auto area = getLocalBounds();
            const auto textArea = area.removeFromLeft (30);
            const auto r = area.toFloat().withSizeKeepingCentre ((float) area.getWidth(), 6.0f);
            g.setColour (col (palette::knobTrack));
            g.fillRoundedRectangle (r, 3.0f);
            const float mid = r.getCentreX();
            g.setColour (col (palette::textFaint));
            g.fillRect (juce::Rectangle<float> (mid - 0.5f, r.getY() - 2.0f, 1.0f, r.getHeight() + 4.0f));
            if (! isEnabled())
                return;   // an unused slot: just the track
            const float x = mid + r.getWidth() * 0.5f * juce::jlimit (-1.0f, 1.0f, value / 100.0f);
            g.setColour (colour);
            g.fillRoundedRectangle (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (mid, x), r.getY(), juce::jmax (mid, x), r.getBottom()), 3.0f);
            g.setColour (col (palette::text));
            g.setFont (font (Theme::fontSmall));
            const int pc = juce::roundToInt (value);
            g.drawText ((pc > 0 ? "+" : "") + juce::String (pc), textArea, juce::Justification::centredRight, false);
        }

        void mouseDown (const juce::MouseEvent&) override { start = value; attachment->beginGesture(); }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            const float scale = e.mods.isShiftDown() ? 0.25f : 1.0f;
            attachment->setValueAsPartOfGesture (juce::jlimit (-100.0f, 100.0f, start + (float) e.getDistanceFromDragStartX() * scale * 200.0f / (float) juce::jmax (40, getWidth() * 2)));
        }
        void mouseUp (const juce::MouseEvent&) override { attachment->endGesture(); }
        void mouseDoubleClick (const juce::MouseEvent&) override { attachment->setValueAsCompleteGesture (0.0f); }

    private:
        juce::RangedAudioParameter& param;
        std::unique_ptr<juce::ParameterAttachment> attachment;
        float value = 0.0f, start = 0.0f;
        juce::Colour colour { palette::accent };
    };

    //==============================================================================
    class MatrixRow : public juce::Component
    {
    public:
        MatrixRow (Processor& p, int s) : processor (p), slot (s),
            source (p.apvts, info (modSrc (s)).id), dest (p.apvts, info (modDst (s)).id),
            amount (*p.apvts.getParameter (info (modAmt (s)).id))
        {
            source.menuBuilder = [] (int current)
            {
                juce::PopupMenu m;
                m.addItem (1, "None", true, current == 0);
                auto section = [&m, current] (const char* title, ModSource from, ModSource to)
                {
                    m.addSectionHeader (title);
                    for (int i = (int) from; i <= (int) to; ++i)
                        m.addColouredItem (i + 1, modSourceDisplayName ((ModSource) i), modSourceColour ((ModSource) i), true, i == current);
                };
                section ("LFOs", ModSource::Lfo1, ModSource::Lfo4);
                section ("Envelopes", ModSource::Env1, ModSource::Env3);
                section ("Performance", ModSource::Velocity, ModSource::Random);
                section ("Macros", ModSource::Macro1, ModSource::Macro8);
                section ("Physics & Alive", ModSource::Ball, ModSource::SeqMod);
                return m;
            };
            dest.menuBuilder = [] (int current)
            {
                juce::PopupMenu m;
                m.addItem (1, "None", true, current == 0);
                juce::String group;
                juce::PopupMenu sub;
                bool subTicked = false;
                auto flush = [&]
                {
                    if (group.isNotEmpty() && sub.getNumItems() > 0)
                        m.addSubMenu (group, sub, true, nullptr, subTicked);
                    sub = {};
                    subTicked = false;
                };
                for (int d = 1; d <= numModDestinations(); ++d)
                {
                    const auto g = juce::String::fromUTF8 (info (modDestination (d)).group);
                    if (g != group) { flush(); group = g; }
                    sub.addItem (d + 1, juce::String::fromUTF8 (modDestinationName (d)), true, d == current);
                    subTicked = subTicked || d == current;
                }
                flush();
                return m;
            };
            source.onChange = [this] (int) { refresh(); };
            dest.onChange = [this] (int) { refresh(); };
            clear.setFrameless (true);
            clear.onClick = [this]
            {
                for (auto prm : { modSrc (slot), modDst (slot), modAmt (slot) })
                    if (auto* q = processor.apvts.getParameter (info (prm).id))
                    {
                        q->beginChangeGesture();
                        q->setValueNotifyingHost (q->convertTo0to1 (0.0f));
                        q->endChangeGesture();
                    }
            };
            for (auto* c : std::initializer_list<juce::Component*> { &source, &dest, &amount, &clear })
                addAndMakeVisible (c);
            refresh();
        }

        void refresh()
        {
            const auto s = (ModSource) source.getIndex();
            const bool used = source.getIndex() != 0 && dest.getIndex() != 0;
            const auto c = s == ModSource::None ? col (palette::textDim) : modSourceColour (s);
            source.setAccentColour (c);
            amount.setColour (c);
            amount.setEnabled (used);
            source.setGhost (source.getIndex() == 0);
            dest.setGhost (dest.getIndex() == 0);
            setAlpha (used ? 1.0f : 0.6f);
            clear.setVisible (source.getIndex() != 0 || dest.getIndex() != 0);
            repaint();
        }

        bool isUsed() const { return source.getIndex() != 0 && dest.getIndex() != 0; }

        void paint (juce::Graphics& g) override
        {
            g.setColour (col (palette::textFaint));
            g.setFont (font (10.5f));
            g.drawText (juce::String (slot + 1), getLocalBounds().removeFromLeft (18), juce::Justification::centred, false);
            const auto arrow = getLocalBounds().withX (source.getRight()).withWidth (dest.getX() - source.getRight()).toFloat();
            IconButton::drawIcon (g, IconButton::Icon::ChevronRight, arrow.withSizeKeepingCentre (9.0f, 9.0f), col (palette::textDim), 1.4f);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            r.removeFromLeft (20);
            clear.setBounds (r.removeFromRight (18).withSizeKeepingCentre (18, 18));
            r.removeFromRight (4);
            amount.setBounds (r.removeFromRight (78));
            r.removeFromRight (6);
            source.setBounds (r.removeFromLeft (84));
            r.removeFromLeft (14);
            dest.setBounds (r);
        }

    private:
        Processor& processor;
        int slot;
        ChoiceButton source, dest;
        AmountBar amount;
        IconButton clear { IconButton::Icon::Close, "Clear this slot" };
    };
}

//==============================================================================
struct ModPage::Impl
{
    Processor& processor;
    ModulationModel model;

    SectionPanel envCard { "Envelopes", col (palette::modEnv) };
    SegmentedControl envTabs { { "ENV 1", "ENV 2", "ENV 3" } };
    std::unique_ptr<EnvelopeEditor> envEditor;
    std::array<std::array<std::unique_ptr<ModKnob>, 7>, 3> envKnobs;
    int envShown = 0;

    SectionPanel lfoCard { "LFOs", col (palette::modLfo) };
    SegmentedControl lfoTabs { { "LFO 1", "LFO 2", "LFO 3", "LFO 4" } };
    std::unique_ptr<LfoEditor> lfoEditor;
    struct LfoControls
    {
        std::unique_ptr<ChoiceButton> shape, mode, polarity, division;
        std::unique_ptr<TogglePill> sync;
        std::unique_ptr<ModKnob> rate, phase, fade, smooth;
    };
    std::array<LfoControls, 4> lfo;
    int lfoShown = 0;

    SectionPanel macroCard { "Macros", col (palette::modMacro) };
    std::array<std::unique_ptr<ModKnob>, 8> macroKnobs;
    std::array<juce::Label, 8> macroNames;

    SectionPanel physicsCard { "Physics", col (palette::modPhysics) };
    std::unique_ptr<PhysicsView> ball, spring, orbit;
    std::array<std::unique_ptr<ModKnob>, 6> physicsKnobs;

    SectionPanel aliveCard { "Alive", col (palette::modPhysics) };
    std::unique_ptr<ModKnob> alive, evolve;
    juce::Label aliveText;
    juce::Label matrixCount;

    SectionPanel matrixCard { "Mod Matrix", col (palette::accent) };
    juce::Viewport matrixViewport;
    juce::Component matrixList;
    std::array<std::unique_ptr<MatrixRow>, kModSlots> rows;
    int usedSlots = -1;

    Impl (ModPage& page, Processor& p) : processor (p), model (p)
    {
        // Envelopes: one knob set per envelope, the selected one visible.
        envEditor = std::make_unique<EnvelopeEditor> (p, 0, col (palette::modEnv));
        envCard.addAndMakeVisible (*envEditor);
        page.addTickable (envEditor.get());
        envTabs.setAccentColour (col (palette::modEnv));
        envTabs.setFontSize (Theme::fontSmall);
        envCard.addAndMakeVisible (envTabs);
        for (int e = 0; e < 3; ++e)
        {
            auto ep = [e] (EnvParam x) { return envParam (e, x); };
            const auto c = col (palette::modEnv);
            auto& k = envKnobs[(size_t) e];
            const EnvParam order[7] = { EnvParam::A, EnvParam::H, EnvParam::D, EnvParam::S, EnvParam::R, EnvParam::ACurve, EnvParam::DCurve };
            for (int i = 0; i < 7; ++i)
                k[(size_t) i] = makeKnob (p, model, page, envCard, ep (order[i]), Theme::knobSecondary,
                                          i == 5 ? "A Curve" : (i == 6 ? "D Curve" : juce::String()), c);
        }

        // LFOs.
        lfoEditor = std::make_unique<LfoEditor> (p, 0, col (palette::modLfo));
        lfoCard.addAndMakeVisible (*lfoEditor);
        page.addTickable (lfoEditor.get());
        lfoTabs.setAccentColour (col (palette::modLfo));
        lfoTabs.setFontSize (Theme::fontSmall);
        lfoCard.addAndMakeVisible (lfoTabs);
        for (int l = 0; l < 4; ++l)
        {
            auto lp = [l] (LfoParam x) { return lfoParam (l, x); };
            const auto c = col (palette::modLfo);
            auto& L = lfo[(size_t) l];
            L.shape = makeChoice (p, lfoCard, lp (LfoParam::Shape), {}, "Shape");
            L.mode = makeChoice (p, lfoCard, lp (LfoParam::Mode));
            L.polarity = makeChoice (p, lfoCard, lp (LfoParam::Polarity));
            L.sync = std::make_unique<TogglePill> (p.apvts, info (lp (LfoParam::Sync)).id, "SYNC", TogglePill::Style::Text);
            L.sync->setAccentColour (c);
            lfoCard.addAndMakeVisible (*L.sync);
            L.rate = makeKnob (p, model, page, lfoCard, lp (LfoParam::Rate), Theme::knobSecondary, {}, c);
            L.division = makeChoice (p, lfoCard, lp (LfoParam::Div), "Division");
            L.phase = makeKnob (p, model, page, lfoCard, lp (LfoParam::Phase), Theme::knobSecondary, "Phase", c);
            L.fade = makeKnob (p, model, page, lfoCard, lp (LfoParam::Fade), Theme::knobSecondary, {}, c);
            L.smooth = makeKnob (p, model, page, lfoCard, lp (LfoParam::Smooth), Theme::knobSecondary, {}, c);
            L.sync->onChange = [this, l] (bool) { if (l == lfoShown) updateLfoVisibility(); };
        }

        // Macros.
        for (int m = 0; m < 8; ++m)
        {
            macroKnobs[(size_t) m] = makeKnob (p, model, page, macroCard, macroParam (m), Theme::knobSecondary, " ", col (palette::modMacro));
            auto& name = macroNames[(size_t) m];
            name.setJustificationType (juce::Justification::centred);
            name.setFont (font (Theme::fontSmall, true));
            name.setColour (juce::Label::textColourId, col (palette::modMacro).withAlpha (0.9f));
            name.setEditable (false, true, false);
            name.setTooltip ("Double-click to rename");
            name.onTextChange = [this, m] { processor.setMacroName (m, macroNames[(size_t) m].getText()); };
            macroCard.addAndMakeVisible (name);
        }

        // Physics + Alive.
        ball = std::make_unique<PhysicsView> (p, PhysicsView::Kind::Ball);
        spring = std::make_unique<PhysicsView> (p, PhysicsView::Kind::Spring);
        orbit = std::make_unique<PhysicsView> (p, PhysicsView::Kind::Orbit);
        for (auto* v : { ball.get(), spring.get(), orbit.get() })
        {
            physicsCard.addAndMakeVisible (*v);
            page.addTickable (v);
        }
        const P physicsParams[6] = { P::BallGravity, P::BallBounce, P::SpringStiffness, P::SpringDamping, P::OrbitSpeed, P::OrbitShape };
        for (int i = 0; i < 6; ++i)
            physicsKnobs[(size_t) i] = makeKnob (p, model, page, physicsCard, physicsParams[i], Theme::knobSecondary, {}, col (palette::modPhysics));
        alive = makeKnob (p, model, page, aliveCard, P::Alive, Theme::knobPrimary, {}, col (palette::modPhysics));
        evolve = makeKnob (p, model, page, aliveCard, P::Evolve, Theme::knobPrimary, {}, col (palette::modPhysics));
        aliveText.setText ("Alive: every note gets its own small offsets in pitch, position, cutoff and pan.\n"
                           "Evolve: they drift slowly while a note is held.", juce::dontSendNotification);
        aliveText.setFont (font (Theme::fontSmall));
        aliveText.setColour (juce::Label::textColourId, col (palette::textDim));
        aliveText.setJustificationType (juce::Justification::centredLeft);
        aliveText.setMinimumHorizontalScale (1.0f);
        aliveCard.addAndMakeVisible (aliveText);

        matrixCount.setFont (font (Theme::fontSmall, true));
        matrixCount.setColour (juce::Label::textColourId, col (palette::textDim));
        matrixCount.setJustificationType (juce::Justification::centredRight);
        matrixCard.addAndMakeVisible (matrixCount);

        // Matrix.
        for (int s = 0; s < kModSlots; ++s)
        {
            rows[(size_t) s] = std::make_unique<MatrixRow> (p, s);
            matrixList.addAndMakeVisible (*rows[(size_t) s]);
        }
        matrixViewport.setViewedComponent (&matrixList, false);
        matrixViewport.setScrollBarsShown (true, false, true, false);
        matrixViewport.setScrollBarThickness (8);
        matrixCard.addAndMakeVisible (matrixViewport);

        envTabs.onChange = [this] (int i) { selectEnvelope (i); };
        lfoTabs.onChange = [this] (int i) { selectLfo (i); };
        selectEnvelope (0);
        selectLfo (0);
        refreshNames();
    }

    void selectEnvelope (int e)
    {
        envShown = juce::jlimit (0, 2, e);
        envTabs.setSelected (envShown, juce::dontSendNotification);
        envEditor->setEnvelope (envShown);
        for (int i = 0; i < 3; ++i)
            for (auto& k : envKnobs[(size_t) i])
                k->setVisible (i == envShown);
    }

    void selectLfo (int l)
    {
        lfoShown = juce::jlimit (0, 3, l);
        lfoTabs.setSelected (lfoShown, juce::dontSendNotification);
        lfoEditor->setLfo (lfoShown);
        updateLfoVisibility();
    }

    void updateLfoVisibility()
    {
        for (int i = 0; i < 4; ++i)
        {
            auto& L = lfo[(size_t) i];
            const bool shown = i == lfoShown;
            setVisibleAll<juce::Component> ({ L.shape.get(), L.mode.get(), L.polarity.get(), L.sync.get(), L.phase.get(), L.fade.get(), L.smooth.get() }, shown);
            L.rate->setVisible (shown && ! L.sync->getState());
            L.division->setVisible (shown && L.sync->getState());
        }
    }

    void refreshNames()
    {
        const auto patch = processor.currentPatch();
        for (int m = 0; m < 8; ++m)
            macroNames[(size_t) m].setText (juce::String::fromUTF8 (patch.macroNames[(size_t) m].c_str()), juce::dontSendNotification);
    }

    void tick()
    {
        // Live dots on the tabs: which envelopes / LFOs are moving.
        const auto& vis = processor.engine().visuals();
        for (int e = 0; e < 3; ++e)
        {
            const float v = vis.envValue[e].load (std::memory_order_relaxed);
            envTabs.setSegmentDot (e, v > 0.01f ? col (palette::modEnv).withAlpha (0.35f + 0.65f * v) : juce::Colours::transparentBlack);
        }
        const bool playing = vis.activeVoices.load (std::memory_order_relaxed) > 0;
        for (int l = 0; l < 4; ++l)
        {
            const float v = std::abs (vis.lfoValue[l].load (std::memory_order_relaxed));
            lfoTabs.setSegmentDot (l, playing ? col (palette::modLfo).withAlpha (0.35f + 0.65f * v) : juce::Colours::transparentBlack);
        }

        int used = 0;
        for (auto& r : rows)
            used += r->isUsed() ? 1 : 0;
        if (used != usedSlots)
        {
            usedSlots = used;
            matrixCount.setText (juce::String (used) + " of " + juce::String (kModSlots) + " slots in use", juce::dontSendNotification);
        }
    }
};

//==============================================================================
ModPage::ModPage (Processor& p)
{
    impl = std::make_unique<Impl> (*this, p);
    for (auto* c : std::initializer_list<juce::Component*> { &impl->envCard, &impl->lfoCard, &impl->macroCard,
                                                             &impl->physicsCard, &impl->aliveCard, &impl->matrixCard })
        addAndMakeVisible (c);
    p.patchChanged.addChangeListener (this);
}

ModPage::~ModPage()
{
    impl->processor.patchChanged.removeChangeListener (this);
}

void ModPage::changeListenerCallback (juce::ChangeBroadcaster*)
{
    impl->refreshNames();
}

void ModPage::pageShown()
{
    impl->model.refresh();
    impl->refreshNames();
}

void ModPage::tick()
{
    impl->model.refresh();
    impl->tick();
    Page::tick();
}

void ModPage::showEnvelope (int e) { impl->selectEnvelope (e); }
void ModPage::showLfo (int l)      { impl->selectLfo (l); }

void ModPage::resized()
{
    auto& I = *impl;
    auto area = getLocalBounds().reduced (Theme::pageMargin);
    const int gutter = Theme::gutter;
    auto left = area.removeFromLeft (372);
    area.removeFromLeft (gutter);
    auto middle = area.removeFromLeft (336);
    area.removeFromLeft (gutter);
    auto right = area;

    const int k40 = ModKnob::heightFor (Theme::knobSecondary);
    const int k56 = ModKnob::heightFor (Theme::knobPrimary);

    // Left: envelopes over LFOs.
    {
        const int envH = (left.getHeight() - gutter) * 46 / 100;
        I.envCard.setBounds (left.removeFromTop (envH));
        left.removeFromTop (gutter);
        I.lfoCard.setBounds (left);

        auto header = I.envCard.headerArea();
        I.envTabs.setBounds (header.withTrimmedLeft (header.getWidth() - 186).withSizeKeepingCentre (186, 20));
        auto c = I.envCard.contentArea();
        auto knobRow = c.removeFromBottom (k40);
        c.removeFromBottom (Theme::spaceXS);
        I.envEditor->setBounds (c);
        for (auto& set : I.envKnobs)
            layout::grid ({ set[0].get(), set[1].get(), set[2].get(), set[3].get(), set[4].get(), set[5].get(), set[6].get() }, knobRow, 7, k40);

        header = I.lfoCard.headerArea();
        I.lfoTabs.setBounds (header.withTrimmedLeft (header.getWidth() - 232).withSizeKeepingCentre (232, 20));
        c = I.lfoCard.contentArea();
        knobRow = c.removeFromBottom (k40);
        c.removeFromBottom (Theme::spaceXS);
        auto choices = c.removeFromBottom (22);
        c.removeFromBottom (Theme::spaceS);
        I.lfoEditor->setBounds (c);
        for (auto& L : I.lfo)
        {
            auto row = choices;
            L.shape->setBounds (row.removeFromLeft (124));
            row.removeFromLeft (6);
            L.sync->setBounds (row.removeFromRight (52));
            row.removeFromRight (6);
            const int w = (row.getWidth() - 6) / 2;
            L.mode->setBounds (row.removeFromLeft (w));
            row.removeFromLeft (6);
            L.polarity->setBounds (row);
            layout::grid ({ L.rate.get(), L.phase.get(), L.fade.get(), L.smooth.get() }, knobRow, 4, k40);
            L.division->setBounds (L.rate->getBounds());
        }
    }

    // Middle: macros, physics, alive.
    {
        const int macroH = Theme::sectionHeader + 2 * k40 + Theme::spaceXS + 10;
        const int aliveH = Theme::sectionHeader + k56 + 8;
        I.macroCard.setBounds (middle.removeFromTop (macroH));
        middle.removeFromTop (gutter);
        I.aliveCard.setBounds (middle.removeFromBottom (aliveH));
        middle.removeFromBottom (gutter);
        I.physicsCard.setBounds (middle);

        auto c = I.macroCard.contentArea();
        for (int m = 0; m < 8; ++m)
        {
            const int row = m / 4, k = m % 4;
            const int x0 = c.getX() + c.getWidth() * k / 4, x1 = c.getX() + c.getWidth() * (k + 1) / 4;
            const juce::Rectangle<int> cell (x0, c.getY() + row * (k40 + Theme::spaceXS), x1 - x0, k40);
            I.macroKnobs[(size_t) m]->setBounds (cell);
            I.macroNames[(size_t) m].setBounds (cell.withHeight (Cell::labelHeight + 2).translated (0, -2));
        }

        I.physicsCard.clearCaptions();
        c = I.physicsCard.contentArea();
        auto knobs = c.removeFromBottom (k40);
        c.removeFromBottom (Theme::spaceXS);
        const int colW = (c.getWidth() - 2 * Theme::spaceS) / 3;
        PhysicsView* views[3] = { I.ball.get(), I.spring.get(), I.orbit.get() };
        const char* names[3] = { "Ball", "Spring", "Orbit" };
        for (int i = 0; i < 3; ++i)
        {
            const int x = c.getX() + i * (colW + Theme::spaceS);
            auto col_ = juce::Rectangle<int> (x, c.getY(), colW, c.getHeight());
            I.physicsCard.addCaption (names[i], col_.removeFromTop (16));
            col_.removeFromTop (2);
            views[i]->setBounds (col_);
            auto kr = juce::Rectangle<int> (x, knobs.getY(), colW, k40);
            I.physicsKnobs[(size_t) (i * 2)]->setBounds (kr.removeFromLeft (colW / 2));
            I.physicsKnobs[(size_t) (i * 2 + 1)]->setBounds (kr);
        }

        c = I.aliveCard.contentArea();
        I.alive->setBounds (c.removeFromLeft (76));
        I.evolve->setBounds (c.removeFromLeft (76));
        c.removeFromLeft (Theme::spaceS);
        I.aliveText.setBounds (c.withTrimmedTop (Cell::labelHeight));
    }

    // Right: matrix.
    {
        I.matrixCard.setBounds (right);
        I.matrixCount.setBounds (I.matrixCard.headerArea());
        auto c = I.matrixCard.contentArea();
        c.removeFromTop (14);   // column captions are painted by the card
        I.matrixCard.clearCaptions();
        I.matrixCard.addCaption ("Source", juce::Rectangle<int> (c.getX() + 20, c.getY() - 14, 84, 12));
        I.matrixCard.addCaption ("Destination", juce::Rectangle<int> (c.getX() + 118, c.getY() - 14, c.getWidth() - 118 - 112, 12));
        I.matrixCard.addCaption ("Amount", juce::Rectangle<int> (c.getRight() - 100, c.getY() - 14, 78, 12));
        I.matrixViewport.setBounds (c);
        const int rowH = juce::jmax (22, c.getHeight() / kModSlots);
        const bool scroll = rowH * kModSlots > c.getHeight();
        I.matrixList.setBounds (0, 0, c.getWidth() - (scroll ? 10 : 0), rowH * kModSlots);
        for (int s = 0; s < kModSlots; ++s)
            I.rows[(size_t) s]->setBounds (0, s * rowH, I.matrixList.getWidth(), rowH);
    }
}

std::unique_ptr<Page> makeModPage (Processor& p)
{
    return std::make_unique<ModPage> (p);
}

} // namespace verdant::ui
