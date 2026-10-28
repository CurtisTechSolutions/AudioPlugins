#pragma once

#include "PluginProcessor.h"
#include "ui/Pages.h"
#include "ui/Theme.h"

#include <array>
#include <memory>

namespace verdant
{

/**
    Verdant's editor. All content lives in a fixed 1120 x 720 component that is
    scaled by a transform, so the window resizes at a fixed aspect ratio
    (0.7x .. 2x) and everything stays vector-crisp. The 56 px header holds the
    logo, the page tabs, the preset bar and the output meter; below it one page
    is shown at a time.
*/
class Editor : public juce::AudioProcessorEditor, private juce::ChangeListener
{
public:
    explicit Editor (Processor&);
    ~Editor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int baseWidth = 1120, baseHeight = 720, headerHeight = 56;
    static constexpr int numPages = 6;
    static const char* pageName (int index);   // "OSC", "MOD", "FX", "ARP/SEQ", "BREED", "BROWSE"
    static const char* pageId (int index);     // "osc", "mod", "fx", "arpseq", "breed", "browse"

    void showPage (int index);
    int currentPage() const noexcept { return pageIndex; }

private:
    class Header;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void showPresetMenu();
    void saveUserPreset();

    Processor& processor;
    ui::VerdantLookAndFeel lookAndFeel;
    juce::Component content;   // baseWidth x baseHeight, scaled to the window
    std::unique_ptr<Header> header;
    std::array<std::unique_ptr<ui::Page>, numPages> pages;
    int pageIndex = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Editor)
};

} // namespace verdant
