#include "PresetList.h"

#include "FilterResponseView.h"

#include "../PluginProcessor.h"
#include "../PresetManager.h"

namespace verdant::ui
{

namespace
{
    juce::Colour col (juce::uint32 c) { return juce::Colour (c); }
    constexpr int kRowHeight = 38;
}

//==============================================================================
juce::Colour categoryColour (const juce::String& category)
{
    static const std::pair<const char*, juce::uint32> known[] = {
        { "Bass", 0xfff2b134 }, { "Lead", 0xfff28cb1 }, { "Pad", 0xff5ad0ff }, { "Pluck", 0xff3ddc97 },
        { "Keys", 0xffb48cf2 }, { "FX", 0xfff2775c }, { "Texture", 0xff7fd8c8 }, { "Init", 0xff8a939e } };
    for (const auto& [name, argb] : known)
        if (category.equalsIgnoreCase (name)) return col (argb);
    static const juce::uint32 extra[] = { 0xffd6e36a, 0xff6a9cf2, 0xffe08cf2, 0xff8cf2c4, 0xfff2c38c };
    const auto h = (juce::uint32) category.toLowerCase().hashCode();
    return col (extra[h % (juce::uint32) std::size (extra)]);
}

void paintPatchGlyph (juce::Graphics& g, juce::Rectangle<float> area, const Patch& patch, juce::Colour colour)
{
    const auto& v = patch.values;
    auto val = [&v] (P p) { return v[(size_t) p]; };
    auto norm = [&v] (P p) { return toNormalised (p, v[(size_t) p]); };

    // Spectral silhouette over 28 log-spaced bins, 40 Hz .. 16 kHz.
    static constexpr float tableTilt[] = { 1.1f, 0.55f, 0.7f, 0.65f, 0.75f, 0.8f, 0.7f, 0.9f, 0.45f, 0.5f, 0.5f, 0.3f, 0.5f, 0.75f, 0.85f, 1.6f, 0.7f };
    const int bins = 28;
    std::array<float, 28> mag {};
    for (int b = 0; b < bins; ++b)
    {
        const float x = (float) b / (float) (bins - 1);
        const float hz = 40.0f * std::pow (400.0f, x);
        float m = 0.0f;
        for (int o = 0; o < 2; ++o)
        {
            if (val (oscParam (o, OscParam::On)) < 0.5f) continue;
            const int t = juce::jlimit (0, (int) std::size (tableTilt) - 1, (int) std::lround (val (oscParam (o, OscParam::Table))));
            const float level = val (oscParam (o, OscParam::Level)) * 0.01f;
            const float pos = norm (oscParam (o, OscParam::Pos)), warp = val (oscParam (o, OscParam::Warp)) * 0.01f;
            const float ripple = 0.18f * std::sin (x * (9.0f + 7.0f * pos) + (float) t * 1.3f) * (0.4f + warp);
            m += level * std::exp (-x * 2.2f * tableTilt[t]) * (1.0f + ripple);
        }
        m += val (P::SubLevel) * 0.01f * std::exp (-x * 14.0f) * 1.2f;
        m += val (P::NoiseLevel) * 0.01f * 0.35f;

        // Filters (serial approximation, both when on).
        float db = 0.0f;
        for (int f = 0; f < 2; ++f)
        {
            if (val (filterParam (f, FilterParam::On)) < 0.5f) continue;
            const auto type = (FilterType) juce::jlimit (0, (int) FilterType::Count - 1, (int) std::lround (val (filterParam (f, FilterParam::Type))));
            const float mix = val (filterParam (f, FilterParam::Mix)) * 0.01f;
            const float rdb = FilterResponseView::approxMagnitudeDb (type, val (filterParam (f, FilterParam::Cutoff)),
                                                                     val (filterParam (f, FilterParam::Res)) * 0.01f, hz);
            const float lin = mix * std::pow (10.0f, juce::jlimit (-60.0f, 24.0f, rdb) / 20.0f) + (1.0f - mix);
            db += 20.0f * std::log10 (juce::jmax (1.0e-4f, lin));
        }
        mag[(size_t) b] = m * std::pow (10.0f, db / 20.0f);
    }
    float peak = 1.0e-6f;
    for (auto m : mag) peak = juce::jmax (peak, m);

    auto spec = area.withTrimmedBottom (area.getHeight() * 0.32f);
    const float bw = spec.getWidth() / (float) bins;
    for (int b = 0; b < bins; ++b)
    {
        const float level = juce::jlimit (0.0f, 1.0f, 1.0f + 20.0f * std::log10 (juce::jmax (1.0e-5f, mag[(size_t) b] / peak)) / 60.0f);
        const float h = juce::jmax (1.5f, spec.getHeight() * level);
        const auto bar = juce::Rectangle<float> (spec.getX() + bw * (float) b + bw * 0.18f, spec.getBottom() - h, bw * 0.64f, h);
        g.setGradientFill (juce::ColourGradient (colour, 0.0f, bar.getY(), colour.withAlpha (0.25f), 0.0f, spec.getBottom(), false));
        g.fillRoundedRectangle (bar, juce::jmin (1.5f, bw * 0.3f));
    }

    // Amp envelope (Env 1) underneath, times on a compressed scale.
    auto env = area.withTrimmedTop (area.getHeight() * 0.74f).reduced (1.0f, 1.0f);
    auto t = [] (float ms) { return std::sqrt (juce::jmax (0.0f, ms) / 4000.0f); };
    const float a = t (val (envParam (0, EnvParam::A))), h = t (val (envParam (0, EnvParam::H))), d = t (val (envParam (0, EnvParam::D)));
    const float s = val (envParam (0, EnvParam::S)) * 0.01f, r = t (val (envParam (0, EnvParam::R)));
    const float total = a + h + d + 0.35f + r;
    auto px = [&env, total] (float time) { return env.getX() + env.getWidth() * time / juce::jmax (0.001f, total); };
    juce::Path p;
    p.startNewSubPath (px (0.0f), env.getBottom());
    p.lineTo (px (a), env.getY());
    p.lineTo (px (a + h), env.getY());
    p.lineTo (px (a + h + d), env.getBottom() - env.getHeight() * s);
    p.lineTo (px (a + h + d + 0.35f), env.getBottom() - env.getHeight() * s);
    p.lineTo (px (total), env.getBottom());
    g.setColour (col (palette::textDim).withAlpha (0.7f));
    g.strokePath (p, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

//==============================================================================
PresetList::PresetList (Processor& p) : processor (p)
{
    list.setModel (this);
    list.setRowHeight (kRowHeight);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    list.setOutlineThickness (0);
    list.getViewport()->setScrollBarThickness (8);
    addAndMakeVisible (list);
    refresh();
}

PresetList::~PresetList()
{
    list.setModel (nullptr);
}

void PresetList::setFilter (const Filter& f)
{
    filter = f;
    refresh();
}

bool PresetList::matches (int index) const
{
    const auto& e = processor.presets().entries()[(size_t) index];
    if (filter.source == 1 && ! e.factory) return false;
    if (filter.source == 2 && e.factory) return false;
    if (filter.category.isNotEmpty() && ! e.category.equalsIgnoreCase (filter.category)) return false;
    for (const auto& t : filter.tags)
    {
        bool found = false;
        for (const auto& et : e.tags)
            found = found || et.equalsIgnoreCase (t);
        if (! found) return false;
    }
    juce::StringArray words;
    words.addTokens (filter.search, " ", "\"");
    words.removeEmptyStrings();
    const auto hay = (e.name + " " + e.category + " " + e.author + " " + e.tags.joinIntoString (" ")).toLowerCase();
    for (const auto& w : words)
        if (! hay.contains (w.toLowerCase())) return false;
    return true;
}

void PresetList::refresh()
{
    const int selected = selectedEntry();
    shown.clear();
    const int n = (int) processor.presets().entries().size();
    for (int i = 0; i < n; ++i)
        if (matches (i))
            shown.push_back (i);
    list.updateContent();
    list.repaint();
    if (selected >= 0)
        selectEntry (selected);
}

void PresetList::selectEntry (int entryIndex)
{
    for (size_t r = 0; r < shown.size(); ++r)
        if (shown[r] == entryIndex)
        {
            list.selectRow ((int) r, false, true);
            return;
        }
    list.deselectAllRows();
}

int PresetList::selectedEntry() const
{
    return entryAtRow (list.getSelectedRow());
}

void PresetList::resized()
{
    list.setBounds (getLocalBounds());
}

void PresetList::paint (juce::Graphics& g)
{
    if (shown.empty())
    {
        g.setColour (col (palette::textDim));
        g.setFont (font (Theme::fontBody));
        g.drawText ("No presets match the filters", getLocalBounds().reduced (12).removeFromTop (40), juce::Justification::centred, false);
    }
}

int PresetList::getNumRows()
{
    return (int) shown.size();
}

void PresetList::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    const int index = entryAtRow (row);
    if (index < 0) return;
    const auto& entries = processor.presets().entries();
    if (index >= (int) entries.size()) return;
    const auto& e = entries[(size_t) index];
    const bool current = index == processor.presets().currentIndex();
    auto r = juce::Rectangle<int> (0, 0, width, height).reduced (2, 1);

    if (selected || current)
    {
        g.setColour (col (selected ? palette::controlHover : palette::well));
        g.fillRoundedRectangle (r.toFloat(), Theme::radius);
    }
    if (current)
    {
        g.setColour (col (palette::accent));
        g.fillRoundedRectangle (juce::Rectangle<float> (3.0f, (float) r.getHeight() - 14.0f).withPosition ((float) r.getX() + 3.0f, (float) r.getY() + 7.0f), 1.5f);
    }
    if (! selected && ! current && row > 0)
    {
        g.setColour (col (palette::panelEdge).withAlpha (0.6f));
        g.fillRect (r.getX() + 12, 0, r.getWidth() - 24, 1);
    }

    r = r.reduced (14, 4);
    auto line1 = r.removeFromTop (r.getHeight() / 2 + 1);
    auto line2 = r;

    // Category chip on the right of line 1.
    const auto cc = categoryColour (e.category);
    const auto chipFont = font (9.5f, true).withExtraKerningFactor (0.06f);
    const auto catText = e.category.toUpperCase();
    const int chipW = (int) std::ceil (textWidth (chipFont, catText)) + 14;
    const auto chip = line1.removeFromRight (chipW).toFloat().withSizeKeepingCentre ((float) chipW, 15.0f);
    g.setColour (cc.withAlpha (0.14f));
    g.fillRoundedRectangle (chip, 7.5f);
    g.setColour (cc);
    g.setFont (chipFont);
    g.drawText (catText, chip, juce::Justification::centred, false);
    line1.removeFromRight (8);

    // Name (+ USER badge).
    const auto nameFont = font (12.5f, true);
    g.setFont (nameFont);
    g.setColour (current ? col (palette::accent) : col (palette::text));
    const int nameW = juce::jmin (line1.getWidth() - (e.factory ? 0 : 42), (int) std::ceil (textWidth (nameFont, e.name)) + 2);
    g.drawFittedText (e.name, line1.removeFromLeft (nameW), juce::Justification::centredLeft, 1, 0.85f);
    if (! e.factory)
    {
        line1.removeFromLeft (6);
        const auto badge = line1.removeFromLeft (34).toFloat().withSizeKeepingCentre (34.0f, 14.0f);
        g.setColour (col (palette::secondary).withAlpha (0.16f));
        g.fillRoundedRectangle (badge, 3.0f);
        g.setColour (col (palette::secondary));
        g.setFont (font (8.5f, true));
        g.drawText ("USER", badge, juce::Justification::centred, false);
    }

    // Line 2: author + tags.
    juce::String meta = e.author.isNotEmpty() ? e.author : juce::String (e.factory ? "Factory" : "User");
    g.setFont (font (Theme::fontSmall));
    g.setColour (col (palette::textDim));
    const int metaW = juce::jmin (line2.getWidth() / 2, (int) std::ceil (textWidth (font (Theme::fontSmall), meta)) + 4);
    g.drawText (meta, line2.removeFromLeft (metaW), juce::Justification::centredLeft, true);
    if (! e.tags.isEmpty())
    {
        line2.removeFromLeft (8);
        juce::StringArray hashed;
        for (const auto& t : e.tags) hashed.add ("#" + t);
        g.setColour (col (palette::textFaint));
        g.drawText (hashed.joinIntoString ("  "), line2, juce::Justification::centredLeft, true);
    }
}

void PresetList::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    const int index = entryAtRow (row);
    if (index < 0 || ! e.mods.isPopupMenu()) return;

    const auto& entry = processor.presets().entries()[(size_t) index];
    juce::PopupMenu m;
    m.addSectionHeader (entry.name);
    m.addItem (1, "Load");
    juce::PopupMenu del;
    del.addItem (3, "Delete \"" + entry.name + "\" permanently");
    m.addSubMenu ("Delete", del, ! entry.factory);
    m.setLookAndFeel (&getLookAndFeel());
    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                     [safe = juce::Component::SafePointer<PresetList> (this), index] (int result)
                     {
                         if (safe == nullptr) return;
                         if (result == 1) safe->load (index);
                         if (result == 3) safe->confirmDelete (index);
                     });
}

void PresetList::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    load (entryAtRow (row));
}

void PresetList::returnKeyPressed (int lastRowSelected)
{
    load (entryAtRow (lastRowSelected));
}

void PresetList::deleteKeyPressed (int lastRowSelected)
{
    const int index = entryAtRow (lastRowSelected);
    if (index >= 0 && ! processor.presets().entries()[(size_t) index].factory)
    {
        // A second menu as the confirmation (no modal loops in a plugin).
        juce::PopupMenu m;
        m.addItem (1, "Delete \"" + processor.presets().entries()[(size_t) index].name + "\" permanently");
        m.addItem (2, "Cancel");
        m.setLookAndFeel (&getLookAndFeel());
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                         [safe = juce::Component::SafePointer<PresetList> (this), index] (int result)
                         {
                             if (safe != nullptr && result == 1) safe->confirmDelete (index);
                         });
    }
}

void PresetList::selectedRowsChanged (int lastRowSelected)
{
    if (onSelectionChanged) onSelectionChanged (entryAtRow (lastRowSelected));
}

juce::String PresetList::getTooltipForRow (int row)
{
    const int index = entryAtRow (row);
    if (index < 0) return {};
    const auto& e = processor.presets().entries()[(size_t) index];
    return e.name + " (" + e.category + (e.author.isNotEmpty() ? ", by " + e.author : juce::String()) + ") - double-click to load";
}

void PresetList::load (int index)
{
    if (index < 0 || index >= (int) processor.presets().entries().size()) return;
    processor.presets().load (index);
    list.repaint();
    if (onLoaded) onLoaded (index);
}

void PresetList::confirmDelete (int index)
{
    juce::String error;
    if (! processor.presets().remove (index, &error))
        juce::Logger::writeToLog ("Verdant: " + error);
    refresh();
}

} // namespace verdant::ui
