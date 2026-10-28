#include "PluginEditor.h"

#include "PresetManager.h"
#include "ui/ModSourceChip.h"
#include "ui/Widgets.h"

#include <ap_ui/Meters.h>

namespace verdant
{

using namespace ui;

namespace
{
    constexpr const char* kPageNames[Editor::numPages] = { "OSC", "MOD", "FX", "ARP/SEQ", "BREED", "BROWSE" };
    constexpr const char* kPageIds[Editor::numPages]   = { "osc", "mod", "fx", "arpseq", "breed", "browse" };

    juce::Colour col (juce::uint32 c) { return juce::Colour (c); }

    /** Plays a note through the processor when it offers auditionNote (int note, float velocity; 0 = off);
        otherwise the on-screen keyboard is a visual aid only. */
    template <typename Proc>
    void auditionNote (Proc& p, int note, float velocity)
    {
        if constexpr (requires { p.auditionNote (note, velocity); })
            p.auditionNote (note, velocity);
        else
            juce::ignoreUnused (p, note, velocity);
    }

    juce::String seedText (uint32_t seed)
    {
        return juce::String::toHexString ((juce::int64) seed).paddedLeft ('0', 8).toUpperCase();
    }
}

const char* Editor::pageName (int index) { return index >= 0 && index < numPages ? kPageNames[index] : ""; }
const char* Editor::pageId (int index)   { return index >= 0 && index < numPages ? kPageIds[index] : ""; }

//==============================================================================
/** The scaled 1120 x 720 surface: page background plus the soft shadow under the header. */
class Editor::Content : public juce::Component
{
public:
    void paint (juce::Graphics& g) override
    {
        g.fillAll (col (palette::background));
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        const float y = (float) headerHeight;
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.35f), 0.0f, y,
                                                 juce::Colours::black.withAlpha (0.0f), 0.0f, y + 10.0f, false));
        g.fillRect (juce::Rectangle<float> (0.0f, y, (float) getWidth(), 10.0f));
    }
};

//==============================================================================
/** One rounded pill: [<] category . name [v] [>] */
class Editor::PresetPill : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void()> onPrev, onNext, onMenu;

    void setPreset (const juce::String& category, const juce::String& name)
    {
        cat = category;
        nm = name;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        const float rad = r.getHeight() * 0.5f;
        g.setColour (col (palette::well));
        g.fillRoundedRectangle (r, rad);

        if (hover >= 0)
        {
            g.saveState();
            juce::Path clip;
            clip.addRoundedRectangle (r, rad);
            g.reduceClipRegion (clip);
            g.setColour (col (palette::controlHover));
            g.fillRect (zone (hover).toFloat());
            g.restoreState();
        }

        g.setColour (col (palette::panelEdge).brighter (0.15f));
        g.drawRoundedRectangle (r, rad, 1.0f);
        g.setColour (col (palette::panelEdge));
        g.fillRect (juce::Rectangle<float> ((float) zone (0).getRight(), r.getY() + 6.0f, 1.0f, r.getHeight() - 12.0f));
        g.fillRect (juce::Rectangle<float> ((float) zone (2).getX(), r.getY() + 6.0f, 1.0f, r.getHeight() - 12.0f));

        IconButton::drawIcon (g, IconButton::Icon::ChevronLeft, zone (0).toFloat().withSizeKeepingCentre (12.0f, 12.0f),
                              hover == 0 ? col (palette::text) : col (palette::textDim));
        IconButton::drawIcon (g, IconButton::Icon::ChevronRight, zone (2).toFloat().withSizeKeepingCentre (12.0f, 12.0f),
                              hover == 2 ? col (palette::text) : col (palette::textDim));

        // Centre: "CATEGORY  Name" + chevron.
        auto mid = zone (1).reduced (12, 0);
        IconButton::drawIcon (g, IconButton::Icon::ChevronDown, mid.removeFromRight (12).toFloat().withSizeKeepingCentre (10.0f, 10.0f),
                              col (palette::textDim));
        const auto catFont = font (Theme::fontSmall, true).withExtraKerningFactor (0.08f);
        const auto nameFont = font (13.5f, true);
        const auto catText = cat.toUpperCase();
        const float cw = cat.isNotEmpty() ? textWidth (catFont, catText) : 0.0f;
        const float nw = textWidth (nameFont, nm);
        const float total = cw + (cw > 0 ? 10.0f : 0.0f) + nw;
        float x = (float) mid.getCentreX() - juce::jmin (total, (float) mid.getWidth()) * 0.5f;
        if (cw > 0.0f)
        {
            g.setColour (col (palette::accent).withAlpha (0.85f));
            g.setFont (catFont);
            g.drawText (catText, juce::Rectangle<float> (x, (float) mid.getY(), cw + 2.0f, (float) mid.getHeight()), juce::Justification::centredLeft, false);
            x += cw + 10.0f;
        }
        g.setColour (col (palette::text));
        g.setFont (nameFont);
        g.drawFittedText (nm, juce::Rectangle<float> (x, (float) mid.getY(), (float) mid.getRight() - x, (float) mid.getHeight()).toNearestInt(),
                          juce::Justification::centredLeft, 1, 0.8f);
    }

    void mouseMove (const juce::MouseEvent& e) override { setHover (zoneAt (e.getPosition())); }
    void mouseExit (const juce::MouseEvent&) override   { setHover (-1); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int z = zoneAt (e.getPosition());
        if (z == 0 && onPrev) onPrev();
        if (z == 1 && onMenu) onMenu();
        if (z == 2 && onNext) onNext();
    }

private:
    juce::Rectangle<int> zone (int z) const
    {
        auto r = getLocalBounds();
        const int side = 30;
        if (z == 0) return r.removeFromLeft (side);
        if (z == 2) return r.removeFromRight (side);
        return r.reduced (side, 0);
    }
    int zoneAt (juce::Point<int> p) const
    {
        for (int z = 0; z < 3; ++z)
            if (zone (z).contains (p)) return z;
        return -1;
    }
    void setHover (int h) { if (h != hover) { hover = h; repaint(); } }

    juce::String cat, nm;
    int hover = -1;
};

//==============================================================================
class Editor::Header : public juce::Component
{
public:
    Header (Editor& e, Processor& p)
        : editor (e), processor (p), meter (p.outputPeak)
    {
        juce::StringArray names;
        for (int i = 0; i < numPages; ++i)
            names.add (pageName (i));
        tabs.setItems (names);
        tabs.setFilledSelection (true);
        tabs.setFontSize (11.5f);
        tabs.onChange = [this] (int i) { editor.showPage (i); };
        addAndMakeVisible (tabs);

        pill.setTooltip ("Browse presets");
        pill.onPrev = [this] { processor.presets().previous(); refresh(); };
        pill.onNext = [this] { processor.presets().next(); refresh(); };
        pill.onMenu = [this] { editor.showPresetMenu(); };
        addAndMakeVisible (pill);

        save.setFrameless (true);
        init.setFrameless (true);
        save.onClick = [this] { editor.toggleSavePanel(); };
        init.onClick = [this] { processor.presets().init(); refresh(); };
        addAndMakeVisible (save);
        addAndMakeVisible (init);

        addAndMakeVisible (meter);
        refresh();
    }

    void refresh()
    {
        const auto patch = processor.currentPatch();
        pill.setPreset (juce::String::fromUTF8 (patch.category.c_str()), juce::String::fromUTF8 (patch.name.c_str()));
    }

    void setTab (int index) { tabs.setSelected (index, juce::dontSendNotification); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        g.setGradientFill (juce::ColourGradient (col (0xff191c22), 0.0f, 0.0f, col (palette::panel), 0.0f, (float) r.getHeight(), false));
        g.fillRect (r);
        g.setColour (col (palette::panelEdge));
        g.fillRect (r.removeFromBottom (1));

        // Logo: a leaf mark + "VERDANT" + tagline, vertically centred as one block.
        const juce::Rectangle<float> mark (14.0f, 16.0f, 24.0f, 24.0f);
        juce::Path leaf;
        leaf.startNewSubPath (mark.getX(), mark.getBottom());
        leaf.quadraticTo (mark.getX(), mark.getY(), mark.getRight(), mark.getY());
        leaf.quadraticTo (mark.getRight(), mark.getBottom(), mark.getX(), mark.getBottom());
        leaf.closeSubPath();
        g.setColour (col (palette::accent).withAlpha (0.18f));
        g.fillEllipse (mark.expanded (5.0f));
        g.setGradientFill (juce::ColourGradient (col (palette::accent).brighter (0.25f), mark.getRight(), mark.getY(),
                                                 col (palette::accent).darker (0.25f), mark.getX(), mark.getBottom(), false));
        g.fillPath (leaf);
        g.setColour (col (palette::panel));
        g.drawLine (mark.getX() + 2.0f, mark.getBottom() - 2.0f, mark.getRight() - 6.0f, mark.getY() + 6.0f, 1.6f);

        g.setColour (col (palette::text));
        g.setFont (font (18.0f, true).withExtraKerningFactor (0.16f));
        g.drawText ("VERDANT", 48, 11, 150, 20, juce::Justification::centredLeft, false);
        g.setColour (col (palette::textDim));
        g.setFont (font (Theme::fontSmall));
        g.drawText ("Living wavetable synth", 49, 30, 160, 14, juce::Justification::centredLeft, false);
    }

    void resized() override
    {
        auto r = getLocalBounds().withTrimmedBottom (1).reduced (Theme::pageMargin, 0);
        const int h = 30;
        auto centred = [h] (juce::Rectangle<int> a) { return a.withSizeKeepingCentre (a.getWidth(), h); };

        meter.setBounds (r.removeFromRight (6).withSizeKeepingCentre (6, 34));
        r.removeFromRight (Theme::spaceM);
        tabs.setBounds (centred (r.removeFromRight (6 * 64)));

        // Preset pill + Save / Init, centred in the space between the logo and the tabs.
        auto mid = r.withTrimmedLeft (200).withTrimmedRight (Theme::spaceM);
        const int pillW = 320, btnW = 56, total = pillW + 6 + btnW * 2;
        auto block = mid.withSizeKeepingCentre (juce::jmin (total, mid.getWidth()), mid.getHeight());
        pill.setBounds (centred (block.removeFromLeft (block.getWidth() - (6 + btnW * 2))));
        block.removeFromLeft (6);
        save.setBounds (centred (block.removeFromLeft (btnW)).reduced (0, 2));
        init.setBounds (centred (block.removeFromLeft (btnW)).reduced (0, 2));
    }

    juce::Component& saveButton() { return save; }
    juce::Component& presetPill() { return pill; }

private:
    Editor& editor;
    Processor& processor;
    PresetPill pill;
    IconButton save { IconButton::Icon::Save, "Save the current sound as a user preset", "Save" };
    IconButton init { IconButton::Icon::None, "Start from the init patch", "Init" };
    SegmentedControl tabs;
    ap::ui::LevelMeter meter;
};

//==============================================================================
/** Bottom strip: mod-source chips | 2-octave keyboard | Alive knob, living seed, lock and re-roll. */
class Editor::BottomStrip : public juce::Component, public Tickable
{
public:
    explicit BottomStrip (Processor& p) : processor (p), sources (p)
    {
        addAndMakeVisible (sources);
        addAndMakeVisible (keyboard);
        keyboard.onNote = [this] (int note, bool on) { auditionNote (processor, note, on ? 0.8f : 0.0f); };

        alive.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        alive.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        alive.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        alive.getProperties().set ("knobColour", (juce::int64) col (palette::modPhysics).getARGB());
        aliveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, info (P::Alive).id, alive);
        alive.setDoubleClickReturnValue (true, (double) info (P::Alive).def);
        alive.setTooltip ("Alive: per-note variation of pitch, position, cutoff, attack and pan");
        alive.onValueChange = [this] { repaint(); };
        addAndMakeVisible (alive);

        lock.setClickingTogglesState (true);
        lock.setFrameless (true);
        lock.onClick = [this]
        {
            processor.setSeedLocked (lock.getToggleState());
            refreshSeed();
        };
        reroll.setFrameless (true);
        reroll.onClick = [this] { processor.rerollSeed(); refreshSeed(); };
        addAndMakeVisible (lock);
        addAndMakeVisible (reroll);
        refreshSeed();
    }

    void refreshSeed()
    {
        seed = seedText (processor.livingSeed());
        const bool locked = processor.isSeedLocked();
        lock.setToggleState (locked, juce::dontSendNotification);
        lock.setIcon (locked ? IconButton::Icon::Lock : IconButton::Icon::Unlock);
        lock.setTooltip (locked ? "Seed locked: presets keep this living seed" : "Lock the living seed across preset changes");
        reroll.setEnabled (! locked);
        reroll.setTooltip ("Re-roll the living seed (new per-note variation)");
        repaint();
    }

    void tick() override { sources.tick(); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        g.setColour (col (palette::panel));
        g.fillRect (r);
        g.setColour (col (palette::panelEdge));
        g.fillRect (r.removeFromTop (1));

        // Alive group: label, value, seed.
        g.setFont (font (Theme::fontSmall, true).withExtraKerningFactor (0.08f));
        g.setColour (col (palette::modPhysics));
        g.drawText ("ALIVE", aliveLabel, juce::Justification::centredRight, false);
        g.setColour (col (palette::text));
        g.setFont (font (Theme::fontSmall));
        g.drawText (juce::String (juce::roundToInt (alive.getValue())) + " %", aliveValue, juce::Justification::centredLeft, false);

        g.setColour (col (palette::well));
        g.fillRoundedRectangle (seedBox.toFloat(), Theme::radius);
        g.setColour (col (palette::panelEdge));
        g.drawRoundedRectangle (seedBox.toFloat().reduced (0.5f), Theme::radius, 1.0f);
        g.setColour (col (palette::textDim));
        g.setFont (font (9.5f, true).withExtraKerningFactor (0.08f));
        g.drawText ("SEED", seedBox.withTrimmedLeft (8), juce::Justification::centredLeft, false);
        g.setColour (col (palette::text));
        g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain)));
        g.drawText (seed, seedBox.withTrimmedLeft (38).withTrimmedRight (6), juce::Justification::centredLeft, false);
    }

    void resized() override
    {
        auto r = getLocalBounds().withTrimmedTop (1).reduced (Theme::pageMargin, 0);
        const int h = 24;

        // Right: Alive + seed.
        auto right = r.removeFromRight (290);
        reroll.setBounds (right.removeFromRight (24).withSizeKeepingCentre (24, h));
        right.removeFromRight (2);
        lock.setBounds (right.removeFromRight (24).withSizeKeepingCentre (24, h));
        right.removeFromRight (4);
        seedBox = right.removeFromRight (106).withSizeKeepingCentre (106, h);
        right.removeFromRight (Theme::spaceM);
        aliveValue = right.removeFromRight (34);
        alive.setBounds (right.removeFromRight (28).withSizeKeepingCentre (28, 28));
        right.removeFromRight (4);
        aliveLabel = right;

        // Left: source chips; centre: keyboard (2 octaves, 14 white keys).
        r.removeFromRight (Theme::spaceL);
        const int keysW = juce::jmin (14 * 15, r.getWidth() / 3);
        keyboard.setBounds (r.removeFromRight (keysW).withSizeKeepingCentre (keysW, 28));
        r.removeFromRight (Theme::spaceL);
        sources.setBounds (r.withSizeKeepingCentre (r.getWidth(), 28));
    }

private:
    /** Two octaves (C3..B4), drawn flat. Click / drag auditions through the processor when it supports it. */
    class Keyboard : public juce::Component
    {
    public:
        std::function<void (int, bool)> onNote;
        static constexpr int lowNote = 48, numWhite = 14;

        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            g.setColour (col (palette::well));
            g.fillRoundedRectangle (r, Theme::radiusSmall);
            for (int pass = 0; pass < 2; ++pass)
                for (int n = lowNote; n < lowNote + 24; ++n)
                {
                    const bool black = isBlack (n);
                    if ((pass == 0) == black) continue;
                    auto k = keyRect (n);
                    const bool down = n == held;
                    if (black)
                    {
                        g.setColour (down ? col (palette::accent) : col (0xff0d0f12));
                        g.fillRoundedRectangle (k, 2.0f);
                    }
                    else
                    {
                        g.setColour (down ? col (palette::accent) : col (0xffc9cfd6));
                        g.fillRoundedRectangle (k.reduced (0.5f, 0.0f), 2.0f);
                        if (n % 12 == 0)
                        {
                            g.setColour (col (0xff5d6570));
                            g.setFont (font (8.5f, true));
                            g.drawText ("C" + juce::String (n / 12 - 1), k.withTrimmedTop (k.getHeight() - 11.0f), juce::Justification::centred, false);
                        }
                    }
                }
        }

        void mouseDown (const juce::MouseEvent& e) override { press (noteAt (e.position)); }
        void mouseDrag (const juce::MouseEvent& e) override { const int n = noteAt (e.position); if (n != held) press (n); }
        void mouseUp (const juce::MouseEvent&) override     { press (-1); }

    private:
        static bool isBlack (int n) { const int k = n % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; }
        float whiteW() const { return (float) getWidth() / (float) numWhite; }
        juce::Rectangle<float> keyRect (int n) const
        {
            static constexpr int whiteIndex[12] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
            const int oct = (n - lowNote) / 12;
            const float x = ((float) (oct * 7 + whiteIndex[n % 12])) * whiteW();
            if (! isBlack (n))
                return { x, 0.0f, whiteW(), (float) getHeight() };
            const float bw = whiteW() * 0.62f;
            return { x + whiteW() - bw * 0.5f, 0.0f, bw, (float) getHeight() * 0.6f };
        }
        int noteAt (juce::Point<float> p) const
        {
            for (int n = lowNote; n < lowNote + 24; ++n)
                if (isBlack (n) && keyRect (n).contains (p)) return n;
            for (int n = lowNote; n < lowNote + 24; ++n)
                if (! isBlack (n) && keyRect (n).contains (p)) return n;
            return -1;
        }
        void press (int n)
        {
            if (held >= 0 && onNote) onNote (held, false);
            held = n;
            if (held >= 0 && onNote) onNote (held, true);
            repaint();
        }
        int held = -1;
    };

    Processor& processor;
    ModSourceStrip sources;
    Keyboard keyboard;
    juce::Slider alive;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> aliveAttachment;
    IconButton lock { IconButton::Icon::Unlock, "Lock the living seed" };
    IconButton reroll { IconButton::Icon::Reroll, "Re-roll the living seed" };
    juce::Rectangle<int> aliveLabel, aliveValue, seedBox;
    juce::String seed;
};

//==============================================================================
/** Inline save form under the Save button: name, category (pick or type a new one), tags, author. */
class Editor::SavePanel : public juce::Component
{
public:
    explicit SavePanel (Processor& p) : processor (p)
    {
        for (auto* ed : { &name, &tags, &author })
        {
            ed->setFont (font (Theme::fontBody));
            ed->setIndents (8, 5);
            addAndMakeVisible (*ed);
        }
        name.setTextToShowWhenEmpty ("Preset name", col (palette::textFaint));
        tags.setTextToShowWhenEmpty ("Tags, comma separated", col (palette::textFaint));
        author.setTextToShowWhenEmpty ("Author", col (palette::textFaint));
        category.setEditableText (true);
        category.setTextWhenNothingSelected ("Category");
        addAndMakeVisible (category);

        cancel.onClick = [this] { setVisible (false); };
        ok.onClick = [this] { commit(); };
        name.onReturnKey = [this] { commit(); };
        name.onEscapeKey = [this] { setVisible (false); };
        addAndMakeVisible (cancel);
        addAndMakeVisible (ok);
    }

    void open()
    {
        const auto patch = processor.currentPatch();
        name.setText (juce::String::fromUTF8 (patch.name.c_str()), false);
        category.clear (juce::dontSendNotification);
        int id = 1;
        for (const auto& c : processor.presets().categories())
            category.addItem (c, id++);
        category.setText (patch.category.empty() || patch.category == "Init" ? juce::String ("User")
                                                                            : juce::String::fromUTF8 (patch.category.c_str()),
                          juce::dontSendNotification);
        juce::StringArray t;
        for (const auto& s : patch.tags) t.add (juce::String::fromUTF8 (s.c_str()));
        tags.setText (t.joinIntoString (", "), false);
        author.setText (juce::String::fromUTF8 (patch.author.c_str()), false);
        error.clear();
        setVisible (true);
        toFront (true);
        name.grabKeyboardFocus();
        name.selectAll();
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.fillRoundedRectangle (r.translated (0.0f, 3.0f), Theme::cardRadius);
        paintSection (g, getLocalBounds(), "Save preset");
        if (error.isNotEmpty())
        {
            g.setColour (col (palette::warning));
            g.setFont (font (Theme::fontSmall));
            g.drawText (error, errorArea, juce::Justification::centredLeft, true);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().withTrimmedTop (Theme::sectionHeader).reduced (SectionPanel::padding, 0).withTrimmedBottom (SectionPanel::padding);
        auto rowOf = [&r] { auto row = r.removeFromTop (26); r.removeFromTop (6); return row; };
        name.setBounds (rowOf());
        category.setBounds (rowOf());
        tags.setBounds (rowOf());
        author.setBounds (rowOf());
        auto buttons = r.removeFromBottom (26);
        ok.setBounds (buttons.removeFromRight (72));
        buttons.removeFromRight (6);
        cancel.setBounds (buttons.removeFromRight (72));
        errorArea = buttons;
    }

private:
    void commit()
    {
        juce::StringArray t;
        t.addTokens (tags.getText(), ",", "\"");
        t.trim();
        t.removeEmptyStrings();
        juce::String err;
        if (processor.presets().saveCurrent (name.getText(), category.getText(), t, author.getText(), &err))
            setVisible (false);
        else
        {
            error = err.isNotEmpty() ? err : juce::String ("Could not save the preset");
            repaint();
        }
    }

    Processor& processor;
    juce::TextEditor name, tags, author;
    juce::ComboBox category;
    juce::TextButton cancel { "Cancel" }, ok { "Save" };
    juce::String error;
    juce::Rectangle<int> errorArea;
};

//==============================================================================
Editor::Editor (Processor& p)
    : juce::AudioProcessorEditor (p), processor (p)
{
    setLookAndFeel (&lookAndFeel);

    content = std::make_unique<Content>();
    content->setBounds (0, 0, baseWidth, baseHeight);
    addAndMakeVisible (*content);

    header = std::make_unique<Header> (*this, p);
    header->setBounds (0, 0, baseWidth, headerHeight);
    content->addAndMakeVisible (*header);

    strip = std::make_unique<BottomStrip> (p);
    strip->setBounds (0, baseHeight - stripHeight, baseWidth, stripHeight);
    content->addAndMakeVisible (*strip);

    pages = { makeOscPage (p), makeModPage (p), makeFxPage (p), makeArpSeqPage (p), makeBreedPage (p), makeBrowsePage (p) };
    for (auto& page : pages)
    {
        page->setBounds (0, headerHeight, baseWidth, pageHeight);
        content->addChildComponent (*page);
    }

    savePanel = std::make_unique<SavePanel> (p);
    content->addChildComponent (*savePanel);

    showPage (0);

    processor.patchChanged.addChangeListener (this);

    setResizable (true, true);
    setResizeLimits ((int) (baseWidth * 0.7), (int) (baseHeight * 0.7), baseWidth * 2, baseHeight * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) baseWidth / (double) baseHeight);
    setSize (baseWidth, baseHeight);

    startTimerHz (30);
}

Editor::~Editor()
{
    stopTimer();
    processor.patchChanged.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void Editor::paint (juce::Graphics& g)
{
    g.fillAll (col (palette::background));
}

void Editor::resized()
{
    content->setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) baseWidth));
}

void Editor::showPage (int index)
{
    if (index < 0 || index >= numPages || index == pageIndex)
        return;

    if (pageIndex >= 0)
    {
        pages[(size_t) pageIndex]->setVisible (false);
        pages[(size_t) pageIndex]->pageHidden();
    }
    pageIndex = index;
    pages[(size_t) index]->setVisible (true);
    pages[(size_t) index]->pageShown();
    pages[(size_t) index]->tick();
    header->setTab (index);
}

void Editor::timerCallback()
{
    if (pageIndex >= 0)
        pages[(size_t) pageIndex]->tick();
    strip->tick();
}

void Editor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    header->refresh();
    strip->refreshSeed();
}

void Editor::showPresetMenu()
{
    auto& pm = processor.presets();
    const auto& entries = pm.entries();

    juce::PopupMenu menu;
    menu.addItem (100000, "Init patch");
    for (int section = 0; section < 2; ++section)
    {
        const bool factory = section == 0;
        bool any = false;
        for (const auto& e : entries)
            any = any || e.factory == factory;
        if (! any)
            continue;
        menu.addSectionHeader (factory ? "Factory" : "User");
        for (const auto& category : pm.categories())
        {
            juce::PopupMenu sub;
            for (size_t i = 0; i < entries.size(); ++i)
                if (entries[i].factory == factory && entries[i].category == category)
                    sub.addItem ((int) i + 1, entries[i].name, true, (int) i == pm.currentIndex());
            if (sub.getNumItems() > 0)
                menu.addSubMenu (category, sub, true, nullptr, false);
        }
    }

    menu.setLookAndFeel (&lookAndFeel);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&header->presetPill()).withMinimumWidth (header->presetPill().getWidth()),
                        [safe = juce::Component::SafePointer<Editor> (this)] (int result)
                        {
                            if (safe == nullptr || result <= 0)
                                return;
                            if (result == 100000)
                                safe->processor.presets().init();
                            else
                                safe->processor.presets().load (result - 1);
                            safe->header->refresh();
                        });
}

void Editor::toggleSavePanel()
{
    if (savePanel->isVisible())
    {
        savePanel->setVisible (false);
        return;
    }
    const auto anchor = content->getLocalArea (&header->saveButton(), header->saveButton().getLocalBounds());
    const int w = 280, h = 214;
    savePanel->setBounds (juce::jlimit (8, baseWidth - w - 8, anchor.getCentreX() - w / 2), headerHeight + 6, w, h);
    savePanel->open();
}

} // namespace verdant
