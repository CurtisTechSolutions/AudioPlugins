#include "OscPage.h"

#include "EnvelopeEditor.h"
#include "FilterResponseView.h"
#include "ModKnob.h"
#include "WavetableView.h"
#include "Widgets.h"

#include "../BackgroundTasks.h"
#include "../PluginProcessor.h"
#include "engine/WavetableFactory.h"

namespace verdant::ui
{

namespace
{
    juce::Colour col (juce::uint32 c) { return juce::Colour (c); }

    juce::Colour oscColour (int osc) { return osc == 0 ? col (palette::accent) : col (palette::secondary); }

    bool isWav (const juce::String& path) { return path.endsWithIgnoreCase (".wav") || path.endsWithIgnoreCase (".wave"); }

    /** Binds the shared knob helper to one card. */
    struct KnobFactory
    {
        Processor& processor;
        ModulationModel& model;
        Page& page;
        juce::Component& parent;

        std::unique_ptr<ModKnob> operator() (P p, int diameter, const juce::String& label = {}, juce::Colour colour = col (palette::accent)) const
        {
            return makeKnob (processor, model, page, parent, p, diameter, label, colour);
        }
    };

    std::unique_ptr<ChoiceButton> choice (Processor& p, juce::Component& parent, P param, const juce::String& label = {},
                                          const juce::String& inlineLabel = {})
    {
        return makeChoice (p, parent, param, label, inlineLabel);
    }

    /** Places `rowHeight` tall knob rows: a caption line (16 px) then the row. Returns the row's bounds. */
    juce::Rectangle<int> captionedRow (SectionPanel& panel, juce::Rectangle<int>& area, const juce::String& caption, int rowHeight)
    {
        panel.addCaption (caption, area.removeFromTop (16));
        return area.removeFromTop (rowHeight);
    }
}

//==============================================================================
class OscCard : public SectionPanel
{
public:
    OscCard (OscPage& pg, Processor& p, ModulationModel& model, int o)
        : SectionPanel (o == 0 ? "Osc A" : "Osc B", oscColour (o)), page (pg), processor (p), osc (o),
          on (p.apvts, info (oscParam (o, OscParam::On)).id),
          view (p, o)
    {
        const auto colour = oscColour (o);
        const KnobFactory knob { p, model, pg, *this };
        auto op = [o] (OscParam x) { return oscParam (o, x); };

        on.setAccentColour (colour);
        on.onChange = [this] (bool s) { setActive (s); dimContent (! s); };
        setActive (on.getState());
        addAndMakeVisible (on);

        table = choice (p, *this, op (OscParam::Table));
        table->setTooltip ("Wavetable");
        table->menuBuilder = [] (int current)
        {
            juce::PopupMenu m;
            m.addSectionHeader ("Factory tables");
            for (int i = 0; i < kNumFactoryTables; ++i)
                m.addItem (i + 1, factoryTableNames()[i], true, i == current);
            m.addSeparator();
            m.addItem (kNumFactoryTables + 1, "User (imported / painted)", true, current == kNumFactoryTables);
            return m;
        };

        paintButton.onClick = [this] { if (page.onPaintRequested) page.onPaintRequested (osc); };
        importButton.onClick = [this] { page.importWavetable (osc); };
        addAndMakeVisible (paintButton);
        addAndMakeVisible (importButton);

        view.setAccentColour (colour);
        addAndMakeVisible (view);
        pg.addTickable (&view);

        position = knob (op (OscParam::Pos), Theme::knobPrimary, {}, colour);
        warpMode = choice (p, *this, op (OscParam::WarpMode), "Warp Mode");
        warp = knob (op (OscParam::Warp), Theme::knobPrimary, "Warp", colour);
        level = knob (op (OscParam::Level), Theme::knobPrimary, {}, colour);

        oct = knob (op (OscParam::Oct), Theme::knobSecondary, {}, colour);
        semi = knob (op (OscParam::Semi), Theme::knobSecondary, {}, colour);
        fine = knob (op (OscParam::Fine), Theme::knobSecondary, {}, colour);
        pitch = knob (op (OscParam::Pitch), Theme::knobSecondary, {}, colour);

        voices = knob (op (OscParam::UniVoices), Theme::knobSecondary, "Voices", colour);
        detune = knob (op (OscParam::UniDetune), Theme::knobSecondary, {}, colour);
        spread = knob (op (OscParam::UniSpread), Theme::knobSecondary, {}, colour);
        blend = knob (op (OscParam::UniBlend), Theme::knobSecondary, {}, colour);

        phase = knob (op (OscParam::Phase), Theme::knobTertiary, {}, colour);
        random = knob (op (OscParam::RandPhase), Theme::knobTertiary, "Random", colour);
        pan = knob (op (OscParam::Pan), Theme::knobTertiary, {}, colour);
        route = choice (p, *this, op (OscParam::Route), "Output");
        dimContent (! on.getState());
    }

    /** An oscillator that is off keeps its controls editable but dims them. */
    void dimContent (bool dim)
    {
        for (auto* c : getChildren())
            if (c != &on)
                c->setAlpha (dim ? 0.5f : 1.0f);
    }

    void setStatus (const juce::String& s, bool error)
    {
        status = s;
        statusIsError = error;
        statusTicks = 30 * 4;
        repaint();
    }

    void tickStatus()
    {
        if (statusTicks > 0 && --statusTicks == 0)
        {
            status.clear();
            repaint();
        }
    }

    void setDropHighlight (bool h) { if (h != dropHighlight) { dropHighlight = h; repaint(); } }

    void resized() override
    {
        clearCaptions();
        const auto header = headerArea();
        on.setBounds (header.withTrimmedLeft (header.getWidth() - 34).withSizeKeepingCentre (34, 18));

        auto area = contentArea();
        auto tableRow = area.removeFromTop (Theme::controlHeight + 2);
        importButton.setBounds (tableRow.removeFromRight (74));
        tableRow.removeFromRight (6);
        paintButton.setBounds (tableRow.removeFromRight (66));
        tableRow.removeFromRight (6);
        table->setBounds (tableRow);
        area.removeFromTop (Theme::spaceS);

        const int primaryH = ModKnob::heightFor (Theme::knobPrimary);
        const int secondaryH = ModKnob::heightFor (Theme::knobSecondary);
        const int tertiaryH = ModKnob::heightFor (Theme::knobTertiary);
        const int below = Theme::spaceS + primaryH + 3 * 16 + 2 * secondaryH + tertiaryH;
        view.setBounds (area.removeFromTop (area.getHeight() - below));
        area.removeFromTop (Theme::spaceS);

        layout::grid ({ position.get(), warpMode.get(), warp.get(), level.get() }, area.removeFromTop (primaryH), 4, primaryH);
        layout::grid ({ oct.get(), semi.get(), fine.get(), pitch.get() }, captionedRow (*this, area, "Pitch", secondaryH), 4, secondaryH);
        layout::grid ({ voices.get(), detune.get(), spread.get(), blend.get() }, captionedRow (*this, area, "Unison", secondaryH), 4, secondaryH);
        layout::grid ({ phase.get(), random.get(), pan.get(), route.get() }, captionedRow (*this, area, "Phase & output", tertiaryH), 4, tertiaryH);
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (status.isNotEmpty())
        {
            g.setColour (statusIsError ? col (palette::warning) : oscColour (osc));
            g.setFont (font (Theme::fontSmall));
            g.drawFittedText (status, headerArea().withTrimmedRight (44), juce::Justification::centredRight, 1, 0.8f);
        }
        if (dropHighlight)
        {
            const auto r = getLocalBounds().toFloat().reduced (1.5f);
            g.setColour (oscColour (osc).withAlpha (0.08f));
            g.fillRoundedRectangle (r, Theme::cardRadius);
            g.setColour (oscColour (osc));
            g.drawRoundedRectangle (r, Theme::cardRadius, 2.0f);
            g.setFont (font (13.0f, true));
            g.drawText ("Drop WAV to import into " + getTitle(), view.getBounds(), juce::Justification::centred, false);
        }
    }

private:
    OscPage& page;
    Processor& processor;
    int osc;
    TogglePill on;
    std::unique_ptr<ChoiceButton> table, warpMode, route;
    IconButton paintButton { IconButton::Icon::Brush, "Paint a wavetable (spectral canvas)", "Paint" };
    IconButton importButton { IconButton::Icon::Import, "Import a WAV wavetable (or drop one here)", "Import" };
    WavetableView view;
    std::unique_ptr<ModKnob> position, warp, level, oct, semi, fine, pitch, voices, detune, spread, blend, phase, random, pan;
    juce::String status;
    bool statusIsError = false, dropHighlight = false;
    int statusTicks = 0;
};

//==============================================================================
class FilterCard : public SectionPanel
{
public:
    FilterCard (Page& pg, Processor& p, ModulationModel& model)
        : SectionPanel ("Filters"), view (p)
    {
        routing.bindToParameter (p.apvts, info (P::FilterRouting).id);
        routing.setFontSize (Theme::fontSmall);
        addAndMakeVisible (routing);

        addAndMakeVisible (view);
        pg.addTickable (&view);

        for (int f = 0; f < 2; ++f)
        {
            const auto colour = f == 0 ? col (palette::accent) : col (palette::secondary);
            const KnobFactory knob { p, model, pg, *this };
            auto fp = [f] (FilterParam x) { return filterParam (f, x); };
            auto& r = rows[(size_t) f];
            r.on = std::make_unique<TogglePill> (p.apvts, info (fp (FilterParam::On)).id, f == 0 ? "Filter 1" : "Filter 2");
            r.on->setAccentColour (colour);
            addAndMakeVisible (*r.on);
            r.type = choice (p, *this, fp (FilterParam::Type));
            // Filter 2's knobs sit under Filter 1's and share its labels (the column headers).
            const juce::String none = f == 0 ? juce::String() : juce::String (" ");
            r.cutoff = knob (fp (FilterParam::Cutoff), Theme::knobPrimary, none, colour);
            r.res = knob (fp (FilterParam::Res), Theme::knobPrimary, none, colour);
            r.drive = knob (fp (FilterParam::Drive), Theme::knobSecondary, none, colour);
            r.key = knob (fp (FilterParam::Key), Theme::knobSecondary, none, colour);
            r.mix = knob (fp (FilterParam::Mix), Theme::knobSecondary, none, colour);
            auto* row = &r;
            r.on->onChange = [row] (bool s)
            {
                for (juce::Component* c : { (juce::Component*) row->type.get(), (juce::Component*) row->cutoff.get(), (juce::Component*) row->res.get(),
                                            (juce::Component*) row->drive.get(), (juce::Component*) row->key.get(), (juce::Component*) row->mix.get() })
                    c->setAlpha (s ? 1.0f : 0.5f);
            };
            r.on->onChange (r.on->getState());
        }
    }

    void resized() override
    {
        clearCaptions();
        const auto header = headerArea();
        routing.setBounds (header.withTrimmedLeft (header.getWidth() - 140).withSizeKeepingCentre (140, 20));

        auto area = contentArea();
        const int rowH = ModKnob::heightFor (Theme::knobPrimary);
        const int row2H = rowH - Cell::labelHeight + 2;
        view.setBounds (area.removeFromTop (area.getHeight() - rowH - row2H - Theme::spaceS));
        area.removeFromTop (Theme::spaceS);

        for (int f = 0; f < 2; ++f)
        {
            auto& r = rows[(size_t) f];
            auto row = area.removeFromTop (f == 0 ? rowH : row2H);

            auto left = row.removeFromLeft (88);
            // On pill + type, centred on the knob row's middle.
            auto mid = left.withTrimmedTop (f == 0 ? Cell::labelHeight : 2).withTrimmedBottom (Cell::valueHeight);
            auto block = mid.withSizeKeepingCentre (left.getWidth(), 18 + 6 + Theme::controlHeight);
            r.on->setBounds (block.removeFromTop (18));
            block.removeFromTop (6);
            r.type->setBounds (block.removeFromTop (Theme::controlHeight));
            row.removeFromLeft (6);

            const int wide = 70;
            const int narrow = (row.getWidth() - 2 * wide) / 3;
            r.cutoff->setBounds (row.removeFromLeft (wide));
            r.res->setBounds (row.removeFromLeft (wide));
            r.drive->setBounds (row.removeFromLeft (narrow));
            r.key->setBounds (row.removeFromLeft (narrow));
            r.mix->setBounds (row);
        }
    }

private:
    struct Row
    {
        std::unique_ptr<TogglePill> on;
        std::unique_ptr<ChoiceButton> type;
        std::unique_ptr<ModKnob> cutoff, res, drive, key, mix;
    };
    SegmentedControl routing;
    FilterResponseView view;
    std::array<Row, 2> rows;
};

//==============================================================================
class SubNoiseCard : public SectionPanel
{
public:
    SubNoiseCard (Page& pg, Processor& p, ModulationModel& model)
        : SectionPanel ("Sub & Noise", col (palette::textDim))
    {
        const KnobFactory knob { p, model, pg, *this };
        subLevel = knob (P::SubLevel, Theme::knobSecondary, "Sub");
        subWave = choice (p, *this, P::SubWave, {}, "Wave");
        subOct = choice (p, *this, P::SubOct, {}, "Octave");
        subRoute = choice (p, *this, P::SubRoute, {}, "Route");
        noiseLevel = knob (P::NoiseLevel, Theme::knobSecondary, "Noise");
        noiseColour = choice (p, *this, P::NoiseColor, {}, "Colour");
        noiseRoute = choice (p, *this, P::NoiseRoute, {}, "Route");
    }

    void resized() override
    {
        clearCaptions();
        auto area = contentArea();
        const int half = (area.getWidth() - 13) / 2;
        auto left = area.removeFromLeft (half);
        addDivider (area.removeFromLeft (13).reduced (0, 4));
        auto right = area;

        auto placeStack = [] (juce::Rectangle<int> r, std::initializer_list<juce::Component*> comps)
        {
            const int h = 20, gap = 4;
            const int total = (int) comps.size() * h + ((int) comps.size() - 1) * gap;
            auto block = r.withSizeKeepingCentre (r.getWidth(), total);
            for (auto* c : comps)
            {
                c->setBounds (block.removeFromTop (h));
                block.removeFromTop (gap);
            }
        };

        subLevel->setBounds (left.removeFromLeft (58));
        left.removeFromLeft (6);
        placeStack (left, { subWave.get(), subOct.get(), subRoute.get() });

        noiseLevel->setBounds (right.removeFromLeft (58));
        right.removeFromLeft (6);
        placeStack (right, { noiseColour.get(), noiseRoute.get() });
    }

private:
    std::unique_ptr<ModKnob> subLevel, noiseLevel;
    std::unique_ptr<ChoiceButton> subWave, subOct, subRoute, noiseColour, noiseRoute;
};

//==============================================================================
class AmpEnvCard : public SectionPanel
{
public:
    AmpEnvCard (Page& pg, Processor& p, ModulationModel& model)
        : SectionPanel ("Amp Envelope", col (palette::modEnv)), editor (p, 0)
    {
        addAndMakeVisible (editor);
        pg.addTickable (&editor);
        const KnobFactory knob { p, model, pg, *this };
        const auto c = col (palette::modEnv);
        auto ep = [] (EnvParam x) { return envParam (0, x); };
        knobs = { knob (ep (EnvParam::A), Theme::knobTertiary, {}, c), knob (ep (EnvParam::H), Theme::knobTertiary, {}, c),
                  knob (ep (EnvParam::D), Theme::knobTertiary, {}, c), knob (ep (EnvParam::S), Theme::knobTertiary, {}, c),
                  knob (ep (EnvParam::R), Theme::knobTertiary, {}, c), knob (ep (EnvParam::ACurve), Theme::knobTertiary, "A Curve", c),
                  knob (ep (EnvParam::DCurve), Theme::knobTertiary, "D Curve", c) };
    }

    void resized() override
    {
        auto area = contentArea();
        const int rowH = ModKnob::heightFor (Theme::knobTertiary);
        editor.setBounds (area.removeFromTop (area.getHeight() - rowH - Theme::spaceXS));
        area.removeFromTop (Theme::spaceXS);
        layout::grid ({ knobs[0].get(), knobs[1].get(), knobs[2].get(), knobs[3].get(), knobs[4].get(), knobs[5].get(), knobs[6].get() },
                      area, 7, rowH);
    }

private:
    EnvelopeEditor editor;
    std::array<std::unique_ptr<ModKnob>, 7> knobs;
};

//==============================================================================
struct OscPage::Impl
{
    Impl (OscPage& page, Processor& p)
        : processor (p), model (p),
          oscA (page, p, model, 0), oscB (page, p, model, 1),
          filters (page, p, model), subNoise (page, p, model), ampEnv (page, p, model)
    {
    }

    Processor& processor;
    ModulationModel model;
    OscCard oscA, oscB;
    FilterCard filters;
    SubNoiseCard subNoise;
    AmpEnvCard ampEnv;
    std::unique_ptr<juce::FileChooser> chooser;
    int dropTarget = -1;

    OscCard& card (int osc) { return osc == 0 ? oscA : oscB; }
};

OscPage::OscPage (Processor& p)
{
    impl = std::make_unique<Impl> (*this, p);
    for (auto* c : std::initializer_list<juce::Component*> { &impl->oscA, &impl->oscB, &impl->filters, &impl->subNoise, &impl->ampEnv })
        addAndMakeVisible (*c);
}

OscPage::~OscPage() = default;

void OscPage::pageShown()
{
    impl->model.refresh();
}

void OscPage::tick()
{
    // The matrix model refreshes first so every knob sees this tick's routes.
    impl->model.refresh();
    impl->oscA.tickStatus();
    impl->oscB.tickStatus();
    Page::tick();
}

void OscPage::resized()
{
    auto area = getLocalBounds().reduced (Theme::pageMargin);
    const int oscW = 330;
    impl->oscA.setBounds (area.removeFromLeft (oscW));
    area.removeFromLeft (Theme::gutter);
    impl->oscB.setBounds (area.removeFromLeft (oscW));
    area.removeFromLeft (Theme::gutter);

    const int subH = Theme::sectionHeader + ModKnob::heightFor (Theme::knobSecondary) + 8;
    const int envH = Theme::sectionHeader + 48 + Theme::spaceXS + ModKnob::heightFor (Theme::knobTertiary) + 8;
    impl->ampEnv.setBounds (area.removeFromBottom (envH));
    area.removeFromBottom (Theme::gutter);
    impl->subNoise.setBounds (area.removeFromBottom (subH));
    area.removeFromBottom (Theme::gutter);
    impl->filters.setBounds (area);
}

//==============================================================================
void OscPage::importWavetable (int osc)
{
    impl->chooser = std::make_unique<juce::FileChooser> ("Import a wavetable into Osc " + juce::String (osc == 0 ? "A" : "B"),
                                                         juce::File(), "*.wav;*.wave");
    impl->chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe = juce::Component::SafePointer<OscPage> (this), osc] (const juce::FileChooser& fc)
        {
            if (safe == nullptr) return;
            const auto file = fc.getResult();
            if (file.existsAsFile())
                safe->filesDropped ({ file.getFullPathName() }, osc == 0 ? safe->impl->oscA.getBounds().getCentreX()
                                                                          : safe->impl->oscB.getBounds().getCentreX(),
                                    safe->getHeight() / 2);
        });
}

bool OscPage::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (isWav (f)) return true;
    return false;
}

void OscPage::fileDragEnter (const juce::StringArray& files, int x, int y) { fileDragMove (files, x, y); }

void OscPage::fileDragMove (const juce::StringArray&, int x, int y)
{
    const int target = impl->oscA.getBounds().contains (x, y) ? 0 : (impl->oscB.getBounds().contains (x, y) ? 1 : -1);
    impl->dropTarget = target;
    impl->oscA.setDropHighlight (target == 0);
    impl->oscB.setDropHighlight (target == 1);
}

void OscPage::fileDragExit (const juce::StringArray&)
{
    impl->dropTarget = -1;
    impl->oscA.setDropHighlight (false);
    impl->oscB.setDropHighlight (false);
}

void OscPage::filesDropped (const juce::StringArray& files, int x, int y)
{
    fileDragExit (files);
    const int osc = x < impl->oscB.getX() ? 0 : (impl->oscB.getBounds().contains (x, y) || x < impl->filters.getX() ? 1 : -1);
    if (osc < 0) return;
    for (const auto& path : files)
    {
        if (! isWav (path)) continue;
        const juce::File file (path);
        impl->card (osc).setStatus ("Importing " + file.getFileName() + "...", false);
        impl->processor.tasks().importWavetable (file, osc,
            [safe = juce::Component::SafePointer<OscPage> (this), osc, name = file.getFileNameWithoutExtension()] (juce::String error)
            {
                if (safe == nullptr) return;
                if (error.isEmpty()) safe->impl->card (osc).setStatus ("Imported " + name, false);
                else                 safe->impl->card (osc).setStatus (error, true);
            });
        break;
    }
}

std::unique_ptr<Page> makeOscPage (Processor& p)
{
    return std::make_unique<OscPage> (p);
}

} // namespace verdant::ui
