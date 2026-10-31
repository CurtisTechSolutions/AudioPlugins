#pragma once

// PresetList: the BROWSE page's preset list (CONTRACT §7). Two-line rows:
// name (+ USER badge) and the category chip, then author and tags. Factory
// presets first (PresetManager order). Filters: category, tags (all must
// match), source (all / factory / user) and a search text matched against
// name, category, author and tags. Double-click or Enter loads; right-click
// offers Load and (user presets only) Delete.
//
// Also home to two small helpers shared by the BROWSE and BREED pages:
// categoryColour() and paintPatchGlyph().

#include "Widgets.h"

#include "engine/Patch.h"

#include <functional>
#include <vector>

namespace verdant
{
class Processor;
}

namespace verdant::ui
{

/** A stable colour per preset category (Bass, Lead, Pad, Pluck, Keys, FX, Texture; others hashed). */
juce::Colour categoryColour (const juce::String& category);

/** A small deterministic portrait of a patch: a spectral silhouette (oscillator / sub / noise levels and
    tables shaped by the filters) over its amp envelope. */
void paintPatchGlyph (juce::Graphics&, juce::Rectangle<float> area, const Patch&, juce::Colour);

class PresetList : public juce::Component, private juce::ListBoxModel
{
public:
    explicit PresetList (Processor&);
    ~PresetList() override;

    struct Filter
    {
        juce::String category;      // empty = all
        juce::StringArray tags;     // all must match (case-insensitive)
        juce::String search;        // words, all must match somewhere
        int source = 0;             // 0 = all, 1 = factory, 2 = user
    };

    void setFilter (const Filter&);
    /** Re-reads PresetManager::entries() (after a rescan / save / delete) and re-applies the filter. */
    void refresh();

    int numShown() const noexcept { return (int) shown.size(); }
    int entryAtRow (int row) const noexcept { return juce::isPositiveAndBelow (row, (int) shown.size()) ? shown[(size_t) row] : -1; }
    /** Selects (and scrolls to) an entry if it is shown. */
    void selectEntry (int entryIndex);
    int selectedEntry() const;

    /** Called after a load from the list (double-click, Enter, menu). */
    std::function<void (int entryIndex)> onLoaded;
    std::function<void (int entryIndex)> onSelectionChanged;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int lastRowSelected) override;
    void deleteKeyPressed (int lastRowSelected) override;
    void selectedRowsChanged (int lastRowSelected) override;
    juce::String getTooltipForRow (int row) override;

    void load (int entryIndex);
    void confirmDelete (int entryIndex);
    bool matches (int entryIndex) const;

    Processor& processor;
    juce::ListBox list;
    Filter filter;
    std::vector<int> shown;
};

} // namespace verdant::ui
