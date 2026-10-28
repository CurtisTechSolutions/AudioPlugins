#include "PluginEditor.h"

#include "PresetManager.h"

#include <ap_ui/Meters.h>

namespace verdant
{

using namespace ui;

namespace
{
    constexpr const char* kPageNames[Editor::numPages] = { "OSC", "MOD", "FX", "ARP/SEQ", "BREED", "BROWSE" };
    constexpr const char* kPageIds[Editor::numPages]   = { "osc", "mod", "fx", "arpseq", "breed", "browse" };
}

const char* Editor::pageName (int index) { return index >= 0 && index < numPages ? kPageNames[index] : ""; }
const char* Editor::pageId (int index)   { return index >= 0 && index < numPages ? kPageIds[index] : ""; }

//==============================================================================
class Editor::Header : public juce::Component
{
public:
    Header (Editor& e, Processor& p)
        : editor (e), processor (p), meter (p.outputPeak)
    {
        for (int i = 0; i < numPages; ++i)
        {
            auto& t = tabs[(size_t) i];
            t.setButtonText (pageName (i));
            t.setClickingTogglesState (false);
            t.onClick = [this, i] { editor.showPage (i); };
            addAndMakeVisible (t);
        }

        prev.setTooltip ("Previous preset");
        next.setTooltip ("Next preset");
        presetName.setTooltip ("Browse presets");
        prev.onClick = [this] { processor.presets().previous(); };
        next.onClick = [this] { processor.presets().next(); };
        presetName.onClick = [this] { editor.showPresetMenu(); };
        save.onClick = [this] { editor.saveUserPreset(); };
        init.onClick = [this] { processor.presets().init(); };
        for (auto* b : { &prev, &presetName, &next, &save, &init })
            addAndMakeVisible (*b);

        addAndMakeVisible (meter);
        refresh();
    }

    void refresh()
    {
        presetName.setButtonText (juce::String::fromUTF8 (processor.currentPatch().name.c_str()));
    }

    void setTab (int index)
    {
        for (int i = 0; i < numPages; ++i)
            tabs[(size_t) i].setToggleState (i == index, juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        g.setColour (juce::Colour (palette::panel));
        g.fillRect (r);
        g.setColour (juce::Colour (palette::accent));
        g.fillRect (r.removeFromBottom (2));

        // Logo: a small leaf mark + name + tagline.
        const juce::Rectangle<float> mark (16.0f, 15.0f, 22.0f, 22.0f);
        juce::Path leaf;
        leaf.startNewSubPath (mark.getX(), mark.getBottom());
        leaf.quadraticTo (mark.getX(), mark.getY(), mark.getRight(), mark.getY());
        leaf.quadraticTo (mark.getRight(), mark.getBottom(), mark.getX(), mark.getBottom());
        leaf.closeSubPath();
        g.setColour (juce::Colour (palette::accent));
        g.fillPath (leaf);
        g.setColour (juce::Colour (palette::panel));
        g.drawLine (mark.getX() + 2.0f, mark.getBottom() - 2.0f, mark.getRight() - 5.0f, mark.getY() + 5.0f, 1.6f);

        g.setColour (juce::Colour (palette::text));
        g.setFont (font (20.0f, true));
        g.drawText ("Verdant", 46, 7, 140, 24, juce::Justification::centredLeft);
        g.setColour (juce::Colour (palette::textDim));
        g.setFont (font (11.0f));
        g.drawText ("Living wavetable synth", 46, 29, 160, 16, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16, 0).withTrimmedBottom (2);
        const int buttonH = 28;
        auto row = [&] (juce::Rectangle<int> area) { return area.withSizeKeepingCentre (area.getWidth(), buttonH); };

        r.removeFromLeft (190);   // logo

        meter.setBounds (r.removeFromRight (12).withSizeKeepingCentre (12, 40));
        r.removeFromRight (16);

        init.setBounds (row (r.removeFromRight (46)));
        r.removeFromRight (4);
        save.setBounds (row (r.removeFromRight (52)));
        r.removeFromRight (10);
        next.setBounds (row (r.removeFromRight (28)));
        r.removeFromRight (4);
        presetName.setBounds (row (r.removeFromRight (210)));
        r.removeFromRight (4);
        prev.setBounds (row (r.removeFromRight (28)));

        r.removeFromRight (16);
        const int tabW = juce::jmin (78, (r.getWidth() - 5 * 4) / numPages);
        for (int i = 0; i < numPages; ++i)
        {
            tabs[(size_t) i].setBounds (row (r.removeFromLeft (tabW)));
            r.removeFromLeft (4);
        }
    }

    juce::TextButton presetName;

private:
    Editor& editor;
    Processor& processor;
    juce::TextButton prev { "<" }, next { ">" }, save { "Save" }, init { "Init" };
    std::array<juce::TextButton, numPages> tabs;
    ap::ui::LevelMeter meter;
};

//==============================================================================
Editor::Editor (Processor& p)
    : juce::AudioProcessorEditor (p), processor (p)
{
    setLookAndFeel (&lookAndFeel);

    content.setBounds (0, 0, baseWidth, baseHeight);
    addAndMakeVisible (content);

    header = std::make_unique<Header> (*this, p);
    header->setBounds (0, 0, baseWidth, headerHeight);
    content.addAndMakeVisible (*header);

    pages = { makeOscPage (p), makeModPage (p), makeFxPage (p), makeArpSeqPage (p), makeBreedPage (p), makeBrowsePage (p) };
    for (auto& page : pages)
    {
        page->setBounds (0, headerHeight, baseWidth, baseHeight - headerHeight);
        content.addChildComponent (*page);
    }
    showPage (0);

    processor.patchChanged.addChangeListener (this);

    setResizable (true, true);
    setResizeLimits ((int) (baseWidth * 0.7), (int) (baseHeight * 0.7), baseWidth * 2, baseHeight * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) baseWidth / (double) baseHeight);
    setSize (baseWidth, baseHeight);
}

Editor::~Editor()
{
    processor.patchChanged.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void Editor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (palette::background));
}

void Editor::resized()
{
    content.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) baseWidth));
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
    header->setTab (index);
}

void Editor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    header->refresh();
}

void Editor::showPresetMenu()
{
    auto& pm = processor.presets();
    const auto& entries = pm.entries();

    juce::PopupMenu menu;
    for (const auto& category : pm.categories())
    {
        juce::PopupMenu sub;
        for (size_t i = 0; i < entries.size(); ++i)
            if (entries[i].category == category)
                sub.addItem ((int) i + 1, entries[i].name, true, (int) i == pm.currentIndex());
        menu.addSubMenu (category, sub);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&header->presetName),
                        [safe = juce::Component::SafePointer<Editor> (this)] (int result)
                        {
                            if (safe != nullptr && result > 0)
                                safe->processor.presets().load (result - 1);
                        });
}

void Editor::saveUserPreset()
{
    // Minimal shell: saves as the next free "User Preset N" in the User category
    // (the BROWSE page provides the full save dialog).
    auto& pm = processor.presets();
    const auto folder = PresetManager::userPresetFolder().getChildFile ("User");
    int n = 1;
    while (folder.getChildFile ("User Preset " + juce::String (n) + PresetManager::fileExtension).exists())
        ++n;
    pm.saveCurrent ("User Preset " + juce::String (n), "User", {}, {});
}

} // namespace verdant
