// The BREED page (CONTRACT §7, DESIGN.md):
//   left    Parents card: parent A / parent B tiles (name, category, a portrait of the patch, "Use
//           current" and a preset menu), Mutation, Seed (with re-roll) and the Breed button
//   right   Children card: the current generation as a 4 x 2 grid of child tiles (portrait, name,
//           tables / filter summary); click a tile = applyChild (play it from the host), the star saves
//           it as a user preset, "Use as A" makes it parent A; generation counter, Undo and Redo
// Everything goes through Processor::breeding() and its breedingChanged broadcaster.

#include "Pages.h"

#include "ModKnob.h"
#include "PresetList.h"
#include "Widgets.h"

#include "../PluginProcessor.h"
#include "../PresetManager.h"
#include "engine/WavetableFactory.h"

#include <set>

namespace verdant::ui
{

namespace
{
    juce::Colour col (juce::uint32 c) { return juce::Colour (c); }

    juce::Colour parentColour (int which) { return which == 0 ? col (palette::accent) : col (palette::secondary); }

    juce::String utf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

    void drawStar (juce::Graphics& g, juce::Rectangle<float> a, juce::Colour c, bool filled)
    {
        juce::Path star;
        star.addStar (a.getCentre(), 5, a.getWidth() * 0.22f, a.getWidth() * 0.5f, 0.0f);
        g.setColour (c);
        if (filled) g.fillPath (star);
        else        g.strokePath (star, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    juce::String tableName (int index)
    {
        if (index >= 0 && index < kNumFactoryTables) return factoryTableNames()[index];
        return "User table";
    }

    //==============================================================================
    /** A filled accent button (the page's primary action). */
    class AccentButton : public juce::Button
    {
    public:
        explicit AccentButton (const juce::String& buttonText) : juce::Button (buttonText) {}

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            const auto r = getLocalBounds().toFloat().reduced (2.0f);
            const auto accent = col (palette::accent);
            g.setColour (accent.withAlpha (highlighted ? 0.3f : 0.2f));
            g.fillRoundedRectangle (r.expanded (2.0f), Theme::radius + 2.0f);
            g.setGradientFill (juce::ColourGradient (accent.brighter (highlighted ? 0.25f : 0.12f), 0.0f, r.getY(),
                                                     accent.darker (down ? 0.25f : 0.1f), 0.0f, r.getBottom(), false));
            g.fillRoundedRectangle (r, Theme::radius);
            g.setColour (col (palette::background));
            g.setFont (font (14.0f, true).withExtraKerningFactor (0.06f));
            g.drawText (getButtonText(), r, juce::Justification::centred, false);
        }
    };

    //==============================================================================
    /** Small text button with an optional star icon. */
    class TileButton : public juce::Button
    {
    public:
        TileButton (const juce::String& buttonText, bool star) : juce::Button (buttonText), withStar (star) {}
        bool filledStar = false;

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            const auto r = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (col (highlighted ? palette::controlHover : palette::control).brighter (down ? 0.1f : 0.0f));
            g.fillRoundedRectangle (r, Theme::radiusSmall);
            g.setColour (col (palette::panelEdge).brighter (0.15f));
            g.drawRoundedRectangle (r, Theme::radiusSmall, 1.0f);
            const auto c = isEnabled() ? (highlighted ? col (palette::text) : col (palette::text).withAlpha (0.82f)) : col (palette::textFaint);
            const auto f = font (Theme::fontSmall, true);
            const float tw = textWidth (f, getButtonText());
            const float total = tw + (withStar ? 16.0f : 0.0f);
            float x = r.getCentreX() - total * 0.5f;
            if (withStar)
            {
                drawStar (g, { x, r.getCentreY() - 6.0f, 12.0f, 12.0f }, filledStar ? col (palette::warning) : c, filledStar);
                x += 16.0f;
            }
            g.setColour (c);
            g.setFont (f);
            g.drawText (getButtonText(), juce::Rectangle<float> (x, r.getY(), tw + 2.0f, r.getHeight()), juce::Justification::centredLeft, false);
        }

    private:
        bool withStar;
    };

    //==============================================================================
    class ParentTile : public juce::Component
    {
    public:
        ParentTile (Processor& p, int w) : processor (p), which (w)
        {
            useCurrent.setTooltip ("Make the current sound parent " + juce::String (which == 0 ? "A" : "B"));
            presetMenu.setTooltip ("Pick a preset as parent " + juce::String (which == 0 ? "A" : "B"));
            useCurrent.onClick = [this] { processor.setParentFromCurrent (which); };
            presetMenu.onClick = [this] { showPresetMenu(); };
            addAndMakeVisible (useCurrent);
            addAndMakeVisible (presetMenu);
        }

        void paint (juce::Graphics& g) override
        {
            const auto& b = processor.breeding();
            const auto& patch = which == 0 ? b.parentA : b.parentB;
            const auto name = which == 0 ? b.parentAName : b.parentBName;
            const auto c = parentColour (which);
            const auto r = getLocalBounds().toFloat().reduced (0.5f);

            g.setColour (col (palette::well));
            g.fillRoundedRectangle (r, Theme::radius);
            g.setColour (c.withAlpha (0.35f));
            g.drawRoundedRectangle (r, Theme::radius, 1.0f);

            auto top = getLocalBounds().reduced (10, 8).removeFromTop (22);
            const auto badge = top.removeFromLeft (22).toFloat();
            g.setColour (c.withAlpha (0.2f));
            g.fillEllipse (badge);
            g.setColour (c);
            g.drawEllipse (badge.reduced (0.75f), 1.5f);
            g.setFont (font (12.0f, true));
            g.drawText (which == 0 ? "A" : "B", badge, juce::Justification::centred, false);
            top.removeFromLeft (10);

            const auto category = utf8 (patch.category);
            const auto cc = categoryColour (category);
            const auto chipFont = font (9.5f, true);
            const int chipW = (int) std::ceil (textWidth (chipFont, category.toUpperCase())) + 14;
            const auto chip = top.removeFromRight (chipW).toFloat().withSizeKeepingCentre ((float) chipW, 15.0f);
            g.setColour (cc.withAlpha (0.14f));
            g.fillRoundedRectangle (chip, 7.5f);
            g.setColour (cc);
            g.setFont (chipFont);
            g.drawText (category.toUpperCase(), chip, juce::Justification::centred, false);
            top.removeFromRight (6);
            g.setColour (col (palette::text));
            g.setFont (font (13.5f, true));
            g.drawFittedText (name, top, juce::Justification::centredLeft, 1, 0.8f);

            paintPatchGlyph (g, glyphArea().toFloat(), patch, c);
        }

        juce::Rectangle<int> glyphArea() const
        {
            return getLocalBounds().reduced (12, 8).withTrimmedTop (30).withTrimmedBottom (32);
        }

        void resized() override
        {
            auto row = getLocalBounds().reduced (10, 8).removeFromBottom (24);
            const int w = (row.getWidth() - 8) / 2;
            useCurrent.setBounds (row.removeFromLeft (w));
            row.removeFromLeft (8);
            presetMenu.setBounds (row);
        }

    private:
        void showPresetMenu()
        {
            auto& pm = processor.presets();
            const auto& entries = pm.entries();
            juce::PopupMenu menu;
            for (const auto& category : pm.categories())
            {
                juce::PopupMenu sub;
                for (size_t i = 0; i < entries.size(); ++i)
                    if (entries[i].category == category)
                        sub.addItem ((int) i + 1, entries[i].name + (entries[i].factory ? juce::String() : juce::String ("  (user)")));
                if (sub.getNumItems() > 0)
                    menu.addSubMenu (category, sub);
            }
            menu.setLookAndFeel (&getLookAndFeel());
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetMenu),
                                [safe = juce::Component::SafePointer<ParentTile> (this)] (int result)
                                {
                                    if (safe != nullptr && result > 0)
                                        safe->processor.setParentFromPreset (safe->which, result - 1);
                                });
        }

        Processor& processor;
        int which;
        IconButton useCurrent { IconButton::Icon::None, {}, "Use current" };
        IconButton presetMenu { IconButton::Icon::ChevronDown, {}, "Preset" };
    };

    //==============================================================================
    class ChildTile : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        std::function<void (int)> onApply, onSave, onUseAsParent;

        ChildTile (Processor& p, int i) : processor (p), index (i)
        {
            save.onClick = [this] { if (onSave) onSave (index); };
            asParent.onClick = [this] { if (onUseAsParent) onUseAsParent (index); };
            save.setTooltip ("Save this child as a user preset");
            asParent.setTooltip ("Breed from this: make it parent A");
            addChildComponent (save);
            addChildComponent (asParent);
            setTooltip ("Click to play this child (applies it to the synth)");
        }

        void setState (bool hasChild, bool isApplied, bool isSaved)
        {
            exists = hasChild;
            applied = isApplied;
            saved = isSaved;
            save.filledStar = saved;
            save.setVisible (exists);
            asParent.setVisible (exists);
            save.repaint();
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat().reduced (0.5f);
            const auto accent = col (palette::accent);
            const bool hover = isMouseOver (true) && exists;

            if (! exists)
            {
                juce::Path outline, dashed;
                outline.addRoundedRectangle (r, Theme::radius);
                const float dashes[] = { 4.0f, 3.0f };
                juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
                g.setColour (col (palette::panelEdge).brighter (0.2f));
                g.fillPath (dashed);
                g.setColour (col (palette::textFaint));
                g.setFont (font (22.0f, true));
                g.drawText (juce::String (index + 1), r.withTrimmedBottom (r.getHeight() * 0.3f), juce::Justification::centred, false);
                return;
            }

            const auto& kids = processor.breeding().children;
            if (index >= (int) kids.size()) return;
            const auto& child = kids[(size_t) index];

            if (applied)
                for (int i = 0; i < 3; ++i)
                {
                    g.setColour (accent.withAlpha (0.10f - 0.03f * (float) i));
                    g.drawRoundedRectangle (r.expanded (1.5f + 1.5f * (float) i), Theme::radius + 2.0f, 1.5f);
                }
            g.setGradientFill (juce::ColourGradient (col (hover || applied ? 0xff242a33 : palette::controlHover), 0.0f, r.getY(),
                                                     col (palette::well), 0.0f, r.getBottom(), false));
            g.fillRoundedRectangle (r, Theme::radius);
            g.setColour (applied ? accent : col (palette::panelEdge).brighter (hover ? 0.35f : 0.18f));
            g.drawRoundedRectangle (r, Theme::radius, applied ? 1.5f : 1.0f);

            auto area = getLocalBounds().reduced (12, 10);
            auto top = area.removeFromTop (16);
            g.setFont (font (10.5f, true).withExtraKerningFactor (0.08f));
            g.setColour (applied ? accent : col (palette::textFaint));
            g.drawText ("#" + juce::String (index + 1), top, juce::Justification::centredLeft, false);
            if (applied)
                g.drawText ("PLAYING", top, juce::Justification::centredRight, false);
            area.removeFromTop (4);

            auto bottom = area.removeFromBottom (24);
            juce::ignoreUnused (bottom);
            area.removeFromBottom (8);
            auto info2 = area.removeFromBottom (30);
            auto nameArea = area.removeFromBottom (34);
            area.removeFromBottom (4);

            g.setColour (col (palette::display).withAlpha (0.6f));
            g.fillRoundedRectangle (area.toFloat(), Theme::radiusSmall);
            paintPatchGlyph (g, area.toFloat().reduced (6.0f, 6.0f), child, applied || hover ? accent : accent.withAlpha (0.8f));

            // Name: "<A> x <B> #k" -> two lines.
            g.setColour (col (palette::text));
            g.setFont (font (12.0f, true));
            g.drawFittedText (utf8 (child.name), nameArea, juce::Justification::centredLeft, 2, 0.85f);

            const auto& v = child.values;
            juce::StringArray lines;
            juce::String tables;
            for (int o = 0; o < 2; ++o)
                if (v[(size_t) oscParam (o, OscParam::On)] >= 0.5f)
                    tables << (tables.isEmpty() ? "" : " + ") << tableName ((int) std::lround (v[(size_t) oscParam (o, OscParam::Table)]));
            lines.add (tables.isEmpty() ? juce::String ("No oscillators") : tables);
            const float cutoff = v[(size_t) filterParam (0, FilterParam::Cutoff)];
            const auto ftype = juce::String (info (filterParam (0, FilterParam::Type)).choices[(int) std::lround (v[(size_t) filterParam (0, FilterParam::Type)])]);
            lines.add (v[(size_t) filterParam (0, FilterParam::On)] >= 0.5f
                           ? ftype + "  " + (cutoff >= 1000.0f ? juce::String (cutoff / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (cutoff)) + " Hz")
                           : juce::String ("Filter off"));
            g.setFont (font (10.5f));
            g.setColour (col (palette::textDim));
            g.drawFittedText (lines[0], info2.removeFromTop (15), juce::Justification::centredLeft, 1, 0.8f);
            g.setColour (col (palette::textFaint));
            g.drawFittedText (lines[1], info2, juce::Justification::centredLeft, 1, 0.8f);
        }

        void resized() override
        {
            auto row = getLocalBounds().reduced (12, 10).removeFromBottom (24);
            const int w = (row.getWidth() - 6) / 2;
            save.setBounds (row.removeFromLeft (w));
            row.removeFromLeft (6);
            asParent.setBounds (row);
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (exists && ! e.mouseWasDraggedSinceMouseDown() && getLocalBounds().contains (e.getPosition()) && onApply)
                onApply (index);
        }
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { repaint(); }

    private:
        Processor& processor;
        int index;
        bool exists = false, applied = false, saved = false;
        TileButton save { "Save", true };
        TileButton asParent { "Use as A", false };
    };

    //==============================================================================
    class BreedPage final : public Page, private juce::ChangeListener
    {
    public:
        explicit BreedPage (Processor& p) : processor (p), parentA (p, 0), parentB (p, 1)
        {
            addAndMakeVisible (parentsCard);
            addAndMakeVisible (childrenCard);
            parentsCard.addAndMakeVisible (parentA);
            parentsCard.addAndMakeVisible (parentB);

            mutation.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            mutation.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
            mutation.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
            mutation.setRange (0.0, 1.0, 0.0);
            mutation.setDoubleClickReturnValue (true, 0.25);
            mutation.setValue (processor.breeding().mutation, juce::dontSendNotification);
            mutation.setTooltip ("Mutation: how far children stray from their parents");
            mutation.onValueChange = [this] { processor.breeding().mutation = (float) mutation.getValue(); parentsCard.repaint(); };
            parentsCard.addAndMakeVisible (mutation);

            seedEditor.setInputRestrictions (10, "0123456789");
            seedEditor.setJustification (juce::Justification::centred);
            seedEditor.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain)));
            seedEditor.setTooltip ("Breeding seed: same parents + mutation + seed = same children");
            seedEditor.onTextChange = [this]
            {
                processor.breeding().seed = (uint32_t) juce::jlimit<juce::int64> (0, 0xffffffffLL, seedEditor.getText().getLargeIntValue());
            };
            parentsCard.addAndMakeVisible (seedEditor);
            reroll.setFrameless (false);
            reroll.onClick = [this]
            {
                processor.breeding().seed = (uint32_t) juce::Random::getSystemRandom().nextInt (juce::Range<int> (1, 999999));
                refreshSeed();
            };
            parentsCard.addAndMakeVisible (reroll);

            breed.onClick = [this] { processor.breedNow(); };
            parentsCard.addAndMakeVisible (breed);

            undo.setFrameless (true);
            redo.setFrameless (true);
            undo.onClick = [this] { processor.undoBreed(); };
            redo.onClick = [this]
            {
                auto& b = processor.breeding();
                if (! b.history.canRedo()) return;
                b.history.redo();
                if (const auto* kids = b.history.current()) b.children = *kids;
                processor.breedingChanged.sendChangeMessage();
            };
            childrenCard.addAndMakeVisible (undo);
            childrenCard.addAndMakeVisible (redo);

            for (int i = 0; i < 8; ++i)
            {
                auto t = std::make_unique<ChildTile> (p, i);
                t->onApply = [this] (int k) { applied = k; processor.applyChild (k); refreshTiles(); };
                t->onSave = [this] (int k) { saveChild (k); };
                t->onUseAsParent = [this] (int k)
                {
                    auto& b = processor.breeding();
                    if (k >= (int) b.children.size()) return;
                    b.parentA = b.children[(size_t) k];
                    b.parentAName = utf8 (b.parentA.name);
                    processor.breedingChanged.sendChangeMessage();
                };
                childrenCard.addAndMakeVisible (*t);
                tiles[(size_t) i] = std::move (t);
            }

            processor.breedingChanged.addChangeListener (this);
            processor.patchChanged.addChangeListener (this);
            refreshSeed();
            refreshTiles();
        }

        ~BreedPage() override
        {
            processor.breedingChanged.removeChangeListener (this);
            processor.patchChanged.removeChangeListener (this);
        }

        void pageShown() override
        {
            mutation.setValue (processor.breeding().mutation, juce::dontSendNotification);
            refreshSeed();
            refreshTiles();
        }

        void paint (juce::Graphics&) override {}

        void paintOverChildren (juce::Graphics& g) override
        {
            // The cross between the parents, and the mutation read-out.
            const auto a = parentsCard.getBounds();
            const auto between = juce::Rectangle<int> (a.getX(), parentA.getBottom() + a.getY(), a.getWidth(), parentB.getY() - parentA.getBottom());
            const auto c = between.toFloat().getCentre();
            g.setColour (col (palette::panel));
            g.fillEllipse (juce::Rectangle<float> (22.0f, 22.0f).withCentre (c));
            g.setColour (col (palette::panelEdge).brighter (0.3f));
            g.drawEllipse (juce::Rectangle<float> (22.0f, 22.0f).withCentre (c), 1.0f);
            IconButton::drawIcon (g, IconButton::Icon::Close, juce::Rectangle<float> (12.0f, 12.0f).withCentre (c), col (palette::text), 1.6f);

            const auto m = mutation.getBounds().translated (a.getX(), a.getY());
            g.setColour (col (palette::textDim));
            g.setFont (font (Theme::fontSmall));
            g.drawText ("Mutation", m.withHeight (Cell::labelHeight).translated (0, -Cell::labelHeight - 3), juce::Justification::centred, false);
            g.setColour (col (palette::text));
            g.drawText (juce::String (juce::roundToInt (mutation.getValue() * 100.0)) + " %", m.withY (m.getBottom() + 3).withHeight (Cell::valueHeight),
                        juce::Justification::centred, false);
            const auto s = seedEditor.getBounds().translated (a.getX(), a.getY());
            g.setColour (col (palette::textDim));
            g.drawText ("Seed", s.withHeight (Cell::labelHeight).translated (0, -Cell::labelHeight - 3), juce::Justification::centred, false);

            const auto cc = childrenCard.getBounds();
            const auto header = childrenCard.headerArea().translated (cc.getX(), cc.getY());
            const int gen = processor.breeding().history.generation();
            g.setColour (col (palette::textDim));
            g.setFont (font (Theme::fontSmall, true));
            g.drawText (gen == 0 ? juce::String ("No generation yet") : "Generation " + juce::String (gen),
                        header.withTrimmedRight (156), juce::Justification::centredRight, false);
            if (status.isNotEmpty())
            {
                g.setColour (col (palette::accent));
                g.drawText (status, header.withTrimmedRight (300), juce::Justification::centredLeft, false);
            }
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (Theme::pageMargin);
            parentsCard.setBounds (area.removeFromLeft (330));
            area.removeFromLeft (Theme::gutter);
            childrenCard.setBounds (area);

            // Parents: A, cross, B, then mutation + seed, then Breed.
            {
                auto c = parentsCard.contentArea();
                auto breedRow = c.removeFromBottom (42);
                c.removeFromBottom (12);
                const int knobRow = Cell::labelHeight + 3 + Theme::knobPrimary + 3 + Cell::valueHeight;
                auto controls = c.removeFromBottom (knobRow);
                c.removeFromBottom (14);
                const int tileH = (c.getHeight() - 26) / 2;
                parentA.setBounds (c.removeFromTop (tileH));
                c.removeFromTop (26);
                parentB.setBounds (c.removeFromTop (tileH));

                breed.setBounds (breedRow);
                auto knobArea = controls.removeFromLeft (controls.getWidth() / 2);
                mutation.setBounds (knobArea.withSizeKeepingCentre (Theme::knobPrimary + 6, Theme::knobPrimary + 6));
                auto seedArea = controls.withSizeKeepingCentre (controls.getWidth(), 28);
                reroll.setBounds (seedArea.removeFromRight (28));
                seedArea.removeFromRight (6);
                seedEditor.setBounds (seedArea.reduced (4, 0));
            }

            // Children.
            {
                const auto header = childrenCard.headerArea();
                auto h = header;
                redo.setBounds (h.removeFromRight (70).withSizeKeepingCentre (70, 22));
                h.removeFromRight (4);
                undo.setBounds (h.removeFromRight (70).withSizeKeepingCentre (70, 22));

                auto c = childrenCard.contentArea();
                const int gap = 12;
                const int w = (c.getWidth() - 3 * gap) / 4, hgt = (c.getHeight() - gap) / 2;
                for (int i = 0; i < 8; ++i)
                    tiles[(size_t) i]->setBounds (c.getX() + (i % 4) * (w + gap), c.getY() + (i / 4) * (hgt + gap), w, hgt);
            }
        }

    private:
        void changeListenerCallback (juce::ChangeBroadcaster* source) override
        {
            if (source == &processor.breedingChanged)
            {
                applied = -1;
                saved.clear();
                status.clear();
            }
            else if (applied >= 0)
            {
                // Something else was loaded: the child is no longer playing.
                const auto& kids = processor.breeding().children;
                if (applied >= (int) kids.size() || processor.patchExtras().name != kids[(size_t) applied].name)
                    applied = -1;
            }
            refreshTiles();
            parentA.repaint();
            parentB.repaint();
        }

        void refreshSeed()
        {
            seedEditor.setText (juce::String ((juce::int64) processor.breeding().seed), false);
        }

        void refreshTiles()
        {
            const auto& b = processor.breeding();
            for (int i = 0; i < 8; ++i)
                tiles[(size_t) i]->setState (i < (int) b.children.size(), i == applied, saved.count (i) > 0);
            undo.setEnabled (b.history.canUndo());
            redo.setEnabled (b.history.canRedo());
            repaint();
        }

        void saveChild (int k)
        {
            const auto& kids = processor.breeding().children;
            if (k >= (int) kids.size()) return;
            const auto& child = kids[(size_t) k];
            juce::StringArray tags;
            for (const auto& t : child.tags) tags.add (utf8 (t));
            tags.addIfNotAlreadyThere ("bred");
            juce::String error;
            if (processor.presets().savePatch (child, utf8 (child.name), utf8 (child.category), tags, {}, &error))
            {
                saved.insert (k);
                status = "Saved \"" + utf8 (child.name) + "\"";
            }
            else
                status = error.isNotEmpty() ? error : juce::String ("Could not save");
            refreshTiles();
        }

        Processor& processor;
        SectionPanel parentsCard { "Parents" };
        SectionPanel childrenCard { "Children", col (palette::secondary) };
        ParentTile parentA, parentB;
        juce::Slider mutation;
        juce::TextEditor seedEditor;
        IconButton reroll { IconButton::Icon::Dice, "New random seed" };
        AccentButton breed { "BREED" };
        IconButton undo { IconButton::Icon::ChevronLeft, "Back to the previous generation", "Undo" };
        IconButton redo { IconButton::Icon::ChevronRight, "Forward to the next generation", "Redo" };
        std::array<std::unique_ptr<ChildTile>, 8> tiles;
        int applied = -1;
        std::set<int> saved;
        juce::String status;
    };
}

std::unique_ptr<Page> makeBreedPage (Processor& p)
{
    return std::make_unique<BreedPage> (p);
}

} // namespace verdant::ui
