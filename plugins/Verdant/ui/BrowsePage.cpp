#include "BrowsePage.h"

#include "PresetList.h"
#include "SampleDropZone.h"
#include "SoundMapView.h"
#include "Widgets.h"

#include "../BackgroundTasks.h"
#include "../PluginProcessor.h"
#include "../PresetManager.h"

namespace verdant::ui
{

namespace
{
    juce::Colour col (juce::uint32 c) { return juce::Colour (c); }

    //==============================================================================
    /** Category rows: "All" + one per category, a colour dot and a count; single selection. */
    class CategoryList : public juce::Component
    {
    public:
        std::function<void (const juce::String&)> onChange;

        void setItems (const juce::StringArray& names, const juce::Array<int>& counts, int total)
        {
            items = names;
            itemCounts = counts;
            totalCount = total;
            if (selected.isNotEmpty() && ! items.contains (selected))
                selected.clear();
            repaint();
        }

        const juce::String& getSelected() const noexcept { return selected; }
        static constexpr int rowH = 24;

        void paint (juce::Graphics& g) override
        {
            for (int i = 0; i <= items.size(); ++i)
            {
                const auto r = row (i).toFloat();
                if (r.getBottom() > (float) getHeight() + 0.5f) break;
                const auto name = i == 0 ? juce::String ("All") : items[i - 1];
                const bool on = i == 0 ? selected.isEmpty() : selected == name;
                const auto c = i == 0 ? col (palette::accent) : categoryColour (name);
                if (on || hover == i)
                {
                    g.setColour (on ? c.withAlpha (0.14f) : col (palette::controlHover));
                    g.fillRoundedRectangle (r, Theme::radiusSmall);
                }
                g.setColour (c);
                g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ r.getX() + 12.0f, r.getCentreY() }));
                g.setColour (on ? col (palette::text) : col (palette::textDim));
                g.setFont (font (Theme::fontBody, on));
                g.drawText (name, r.withTrimmedLeft (24.0f), juce::Justification::centredLeft, false);
                g.setColour (col (palette::textFaint));
                g.setFont (font (Theme::fontSmall));
                g.drawText (juce::String (i == 0 ? totalCount : itemCounts[i - 1]), r.withTrimmedRight (8.0f), juce::Justification::centredRight, false);
            }
        }

        void mouseMove (const juce::MouseEvent& e) override { const int h = rowAt (e.y); if (h != hover) { hover = h; repaint(); } }
        void mouseExit (const juce::MouseEvent&) override  { hover = -1; repaint(); }
        void mouseDown (const juce::MouseEvent& e) override
        {
            const int i = rowAt (e.y);
            if (i < 0) return;
            selected = i == 0 ? juce::String() : items[i - 1];
            repaint();
            if (onChange) onChange (selected);
        }

    private:
        juce::Rectangle<int> row (int i) const { return { 0, i * rowH, getWidth(), rowH }; }
        int rowAt (int y) const { const int i = y / rowH; return i >= 0 && i <= items.size() ? i : -1; }

        juce::StringArray items;
        juce::Array<int> itemCounts;
        int totalCount = 0, hover = -1;
        juce::String selected;
    };

    //==============================================================================
    /** Wrapping tag chips, most used first; any number can be on (presets must carry all of them).
        Tags that do not fit go behind a "+N" chip that opens a menu of every tag. */
    class TagCloud : public juce::Component
    {
    public:
        std::function<void()> onChange;

        /** `ordered`: every tag, most used first. */
        void setTags (const juce::StringArray& ordered)
        {
            allTags = ordered;
            juce::StringArray kept;
            for (const auto& t : selected)
                if (allTags.contains (t, true)) kept.add (t);
            selected = kept;
            layoutChips();
            repaint();
        }

        const juce::StringArray& getSelected() const noexcept { return selected; }
        void clear() { selected.clear(); layoutChips(); repaint(); if (onChange) onChange(); }

        void resized() override { layoutChips(); }

        void paint (juce::Graphics& g) override
        {
            const auto f = font (Theme::fontSmall);
            for (size_t i = 0; i < chips.size(); ++i)
            {
                const auto r = chips[i];
                const bool more = (int) i == moreChip;
                const bool on = ! more && selected.contains (shown[(int) i], true);
                g.setColour (on ? col (palette::accent).withAlpha (0.16f) : col (hover == (int) i ? palette::controlHover : palette::well));
                g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
                g.setColour (on ? col (palette::accent).withAlpha (0.8f) : col (palette::panelEdge).brighter (more ? 0.35f : 0.15f));
                g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f, 1.0f);
                g.setColour (on ? col (palette::accent) : (more ? col (palette::text) : col (palette::textDim)));
                g.setFont (more ? font (Theme::fontSmall, true) : f);
                g.drawText (more ? "+" + juce::String (allTags.size() - (int) shown.size()) + " more" : shown[(int) i], r, juce::Justification::centred, false);
            }
        }

        void mouseMove (const juce::MouseEvent& e) override { const int h = chipAt (e.position); if (h != hover) { hover = h; repaint(); } }
        void mouseExit (const juce::MouseEvent&) override  { hover = -1; repaint(); }
        void mouseDown (const juce::MouseEvent& e) override
        {
            const int i = chipAt (e.position);
            if (i < 0) return;
            if (i == moreChip) { showAllTags(); return; }
            toggle (shown[i]);
        }

    private:
        void toggle (const juce::String& t)
        {
            if (selected.contains (t, true)) selected.removeString (t, true);
            else                             selected.add (t);
            layoutChips();
            repaint();
            if (onChange) onChange();
        }

        void showAllTags()
        {
            juce::StringArray sorted (allTags);
            sorted.sortNatural();
            juce::PopupMenu m;
            for (int i = 0; i < sorted.size(); ++i)
                m.addItem (i + 1, sorted[i], true, selected.contains (sorted[i], true));
            m.setLookAndFeel (&getLookAndFeel());
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                             [safe = juce::Component::SafePointer<TagCloud> (this), sorted] (int result)
                             {
                                 if (safe != nullptr && result > 0) safe->toggle (sorted[result - 1]);
                             });
        }

        void layoutChips()
        {
            // Selected tags first, then the rest in usage order; stop when the area is full.
            juce::StringArray order (selected);
            for (const auto& t : allTags)
                if (! order.contains (t, true)) order.add (t);
            chips.clear();
            shown.clear();
            moreChip = -1;
            const auto f = font (Theme::fontSmall);
            const float h = 20.0f, gap = 5.0f, moreW = 64.0f;
            float x = 0.0f, y = 0.0f;
            for (int i = 0; i < order.size(); ++i)
            {
                const float w = textWidth (f, order[i]) + 16.0f;
                if (x > 0.0f && x + w > (float) getWidth()) { x = 0.0f; y += h + gap; }
                const bool lastRow = y + 2.0f * h + gap > (float) getHeight();
                const bool needMore = i < order.size() - 1;
                if (y + h > (float) getHeight() || (lastRow && needMore && x + w + gap + moreW > (float) getWidth()))
                {
                    if (x > 0.0f && x + moreW > (float) getWidth()) { x = 0.0f; y += h + gap; }
                    if (y + h <= (float) getHeight() || chips.empty())
                    {
                        moreChip = (int) chips.size();
                        chips.push_back ({ x, y, moreW, h });
                    }
                    else
                    {
                        // No room for the extra chip: it replaces the last tag.
                        moreChip = (int) chips.size() - 1;
                        chips.back().setWidth (moreW);
                        shown.remove (shown.size() - 1);
                    }
                    return;
                }
                chips.push_back ({ x, y, w, h });
                shown.add (order[i]);
                x += w + gap;
            }
        }
        int chipAt (juce::Point<float> p) const
        {
            for (size_t i = 0; i < chips.size(); ++i)
                if (chips[i].contains (p)) return (int) i;
            return -1;
        }

        juce::StringArray allTags, selected, shown;
        std::vector<juce::Rectangle<float>> chips;
        int hover = -1, moreChip = -1;
    };
}

//==============================================================================
struct BrowsePage::Impl
{
    Processor& processor;

    SectionPanel filterCard { "Browse" };
    juce::TextEditor search;
    SegmentedControl source { { "All", "Factory", "User" } };
    CategoryList categories;
    TagCloud tags;
    IconButton clearTags { IconButton::Icon::None, "Clear the tag filter", "Clear" };
    IconButton saveAs { IconButton::Icon::Save, "Save the current sound as a user preset", "Save as..." };

    SectionPanel listCard { "Presets", col (palette::accent) };
    PresetList list;
    juce::Label count;

    SectionPanel mapCard { "Sound Map", col (palette::secondary) };
    SoundMapView map;
    TogglePill audition { "AUDITION", TogglePill::Style::Text };
    IconButton build { IconButton::Icon::Reroll, "Lay the presets out by sound (renders each preset once)", "Build map" };
    bool building = false, autoBuilt = false;
    size_t knownEntries = 0;

    SectionPanel sampleCard { "Sample Match", col (palette::warning) };
    SampleDropZone dropZone;

    explicit Impl (Processor& p) : processor (p), list (p), map (p), dropZone (p)
    {
        search.setTextToShowWhenEmpty ("Search name, category, author, tag", col (palette::textFaint));
        search.setFont (font (Theme::fontBody));
        search.setIndents (10, 6);
        search.onTextChange = [this] { applyFilter(); };
        search.onEscapeKey = [this] { search.clear(); applyFilter(); };
        filterCard.addAndMakeVisible (search);
        source.setFontSize (Theme::fontSmall);
        source.onChange = [this] (int) { applyFilter(); };
        filterCard.addAndMakeVisible (source);
        categories.onChange = [this] (const juce::String&) { applyFilter(); };
        filterCard.addAndMakeVisible (categories);
        tags.onChange = [this] { applyFilter(); };
        filterCard.addAndMakeVisible (tags);
        clearTags.setFrameless (true);
        clearTags.onClick = [this] { tags.clear(); };
        filterCard.addAndMakeVisible (clearTags);
        filterCard.addAndMakeVisible (saveAs);

        listCard.addAndMakeVisible (list);
        count.setFont (font (Theme::fontSmall, true));
        count.setColour (juce::Label::textColourId, col (palette::textDim));
        count.setJustificationType (juce::Justification::centredRight);
        listCard.addAndMakeVisible (count);
        list.onSelectionChanged = [this] (int entry) { map.setHighlightedEntry (entry); };
        list.onLoaded = [this] (int) { map.repaint(); };

        mapCard.addAndMakeVisible (map);
        audition.setState (true, juce::dontSendNotification);
        audition.setTooltip ("Hovering a point previews the preset with a short C3");
        audition.onChange = [this] (bool on) { map.setAuditionEnabled (on); };
        mapCard.addAndMakeVisible (audition);
        build.onClick = [this] { startBuild (map.hasPoints()); };
        mapCard.addAndMakeVisible (build);
        map.onLoaded = [this] (int entry) { list.selectEntry (entry); list.repaint(); };

        sampleCard.addAndMakeVisible (dropZone);
        refreshFilters();
    }

    void refreshFilters()
    {
        const auto& entries = processor.presets().entries();
        knownEntries = entries.size();
        const auto cats = processor.presets().categories();
        juce::Array<int> counts;
        for (const auto& c : cats)
        {
            int n = 0;
            for (const auto& e : entries) n += e.category == c ? 1 : 0;
            counts.add (n);
        }
        categories.setItems (cats, counts, (int) entries.size());
        // Tags, most used first.
        auto all = processor.presets().allTags();
        std::vector<std::pair<int, juce::String>> ranked;
        for (const auto& t : all)
        {
            int n = 0;
            for (const auto& e : entries) n += e.tags.contains (t, true) ? 1 : 0;
            ranked.emplace_back (-n, t);
        }
        std::stable_sort (ranked.begin(), ranked.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
        juce::StringArray ordered;
        for (const auto& r : ranked) ordered.add (r.second);
        tags.setTags (ordered);
        applyFilter();
    }

    void applyFilter()
    {
        PresetList::Filter f;
        f.category = categories.getSelected();
        f.tags = tags.getSelected();
        f.search = search.getText();
        f.source = source.getSelected();
        list.setFilter (f);
        clearTags.setVisible (! f.tags.isEmpty());
        const int total = (int) processor.presets().entries().size();
        count.setText (list.numShown() == total ? juce::String (total) + " presets"
                                                : juce::String (list.numShown()) + " of " + juce::String (total),
                       juce::dontSendNotification);
    }

    void startBuild (bool ignoreCache)
    {
        if (building) return;
        building = true;
        map.setBuildProgress (0.0f);
        build.setEnabled (false);
        juce::Component::SafePointer<SoundMapView> safeMap (&map);
        auto* self = this;
        juce::Component::SafePointer<IconButton> safeButton (&build);
        processor.tasks().buildSoundMap (
            [safeMap] (float p) { if (safeMap != nullptr) safeMap->setBuildProgress (p); },
            [safeMap, safeButton, self] (std::vector<SoundMapPoint> pts)
            {
                if (safeMap == nullptr || safeButton == nullptr) return;
                self->building = false;
                safeMap->setBuildProgress (-1.0f);
                safeMap->setPoints (std::move (pts));
                safeButton->setEnabled (true);
                self->updateBuildButton();
            },
            ignoreCache);
    }

    void updateBuildButton()
    {
        build.setButtonText (map.hasPoints() ? "Rebuild" : "Build map");
        build.repaint();
    }
};

//==============================================================================
BrowsePage::BrowsePage (Processor& p)
{
    impl = std::make_unique<Impl> (p);
    for (auto* c : std::initializer_list<juce::Component*> { &impl->filterCard, &impl->listCard, &impl->mapCard, &impl->sampleCard })
        addAndMakeVisible (c);
    impl->saveAs.onClick = [this] { if (onSaveAs) onSaveAs(); };
    addTickable (&impl->map);
    p.patchChanged.addChangeListener (this);
}

BrowsePage::~BrowsePage()
{
    impl->processor.patchChanged.removeChangeListener (this);
}

void BrowsePage::analyseSample (const juce::File& f)
{
    impl->dropZone.analyse (f);
}

void BrowsePage::setSoundMap (std::vector<SoundMapPoint> pts)
{
    impl->map.setPoints (std::move (pts));
    impl->autoBuilt = true;
    impl->updateBuildButton();
}

void BrowsePage::buildSoundMap (bool ignoreCache)
{
    impl->startBuild (ignoreCache);
}

void BrowsePage::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // Presets saved / deleted elsewhere (Save panel, BREED) change the list.
    if (impl->processor.presets().entries().size() != impl->knownEntries)
        impl->refreshFilters();
    impl->list.repaint();
    impl->map.repaint();
}

void BrowsePage::pageShown()
{
    impl->processor.presets().rescan();
    impl->refreshFilters();
    impl->list.selectEntry (impl->processor.presets().currentIndex());
    // First visit: load the cached map, or build it once.
    const bool stale = impl->map.hasPoints() && impl->map.getPoints().size() != impl->processor.presets().entries().size();
    if ((! impl->map.hasPoints() && ! impl->autoBuilt) || stale)
    {
        impl->autoBuilt = true;
        impl->startBuild (false);
    }
    impl->updateBuildButton();
}

void BrowsePage::pageHidden()
{
    impl->map.endPreview (true);
}

void BrowsePage::tick()
{
    Page::tick();
}

void BrowsePage::resized()
{
    auto& I = *impl;
    auto area = getLocalBounds().reduced (Theme::pageMargin);
    I.filterCard.setBounds (area.removeFromLeft (232));
    area.removeFromLeft (Theme::gutter);
    I.listCard.setBounds (area.removeFromLeft (392));
    area.removeFromLeft (Theme::gutter);
    I.sampleCard.setBounds (area.removeFromBottom (138));
    area.removeFromBottom (Theme::gutter);
    I.mapCard.setBounds (area);

    // Filters.
    {
        I.filterCard.clearCaptions();
        auto c = I.filterCard.contentArea();
        I.search.setBounds (c.removeFromTop (28));
        c.removeFromTop (8);
        I.source.setBounds (c.removeFromTop (24));
        c.removeFromTop (10);
        I.saveAs.setBounds (c.removeFromBottom (28));
        c.removeFromBottom (10);
        I.filterCard.addCaption ("Category", c.removeFromTop (16));
        c.removeFromTop (2);
        const int rows = (int) I.processor.presets().categories().size() + 1;
        I.categories.setBounds (c.removeFromTop (juce::jmin (c.getHeight() - 80, rows * CategoryList::rowH)));
        c.removeFromTop (8);
        auto tagCaption = c.removeFromTop (16);
        I.clearTags.setBounds (tagCaption.removeFromRight (44).withSizeKeepingCentre (44, 18));
        I.filterCard.addCaption ("Tags", tagCaption.withTrimmedRight (6));
        c.removeFromTop (4);
        I.tags.setBounds (c);
    }

    // List.
    I.count.setBounds (I.listCard.headerArea());
    I.list.setBounds (I.listCard.contentArea().withTrimmedLeft (-6).withTrimmedRight (-6));

    // Map.
    {
        auto h = I.mapCard.headerArea();
        I.build.setBounds (h.removeFromRight (96).withSizeKeepingCentre (96, 22));
        h.removeFromRight (6);
        I.audition.setBounds (h.removeFromRight (78).withSizeKeepingCentre (78, 20));
        I.map.setBounds (I.mapCard.contentArea());
    }

    I.dropZone.setBounds (I.sampleCard.contentArea());
}

std::unique_ptr<Page> makeBrowsePage (Processor& p)
{
    return std::make_unique<BrowsePage> (p);
}

} // namespace verdant::ui
