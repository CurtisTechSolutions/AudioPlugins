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
    (0.7x .. 2x) and everything stays vector-crisp.

        0 ..  56   header: logo, preset pill (prev | category . name | next), Save / Init, page tabs, meter
       56 .. 684   the current page (1120 x 628)
      684 .. 720   bottom strip: mod-source chips, 2-octave keyboard, Alive + living seed lock / re-roll

    The editor owns the one 30 Hz timer: it ticks the visible page (Page::tick)
    and the bottom strip. It is the DragAndDropContainer for ModSourceChips and
    hosts the tooltip window.
*/
class Editor : public juce::AudioProcessorEditor,
               public juce::DragAndDropContainer,
               private juce::ChangeListener,
               private juce::Timer
{
public:
    explicit Editor (Processor&);
    ~Editor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int baseWidth = 1120, baseHeight = 720, headerHeight = 56, stripHeight = 36;
    static constexpr int pageHeight = baseHeight - headerHeight - stripHeight;   // 628
    static constexpr int numPages = 6;
    static const char* pageName (int index);   // "OSC", "MOD", "FX", "ARP/SEQ", "BREED", "BROWSE"
    static const char* pageId (int index);     // "osc", "mod", "fx", "arpseq", "breed", "browse"

    void showPage (int index);
    int currentPage() const noexcept { return pageIndex; }
    ui::Page* page (int index) const noexcept { return index >= 0 && index < numPages ? pages[(size_t) index].get() : nullptr; }

private:
    class Content;
    class Header;
    class PresetPill;
    class BottomStrip;
    class SavePanel;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void showPresetMenu();
    void toggleSavePanel();

    Processor& processor;
    ui::VerdantLookAndFeel lookAndFeel;
    std::unique_ptr<Content> content;   // baseWidth x baseHeight, scaled to the window
    std::unique_ptr<Header> header;
    std::unique_ptr<BottomStrip> strip;
    std::unique_ptr<SavePanel> savePanel;
    std::array<std::unique_ptr<ui::Page>, numPages> pages;
    int pageIndex = -1;
    juce::TooltipWindow tooltipWindow { this, 700 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Editor)
};

} // namespace verdant
