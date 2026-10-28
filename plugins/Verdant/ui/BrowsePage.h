#pragma once

// The BROWSE page (CONTRACT §7, DESIGN.md):
//   left     filters: search, source (all / factory / user), categories with counts, tag chips,
//            and "Save as..." (opens the editor's save panel through onSaveAs)
//   centre   PresetList
//   right    the Sound Map (SoundMapView, Build / Rebuild with progress, audition toggle; built
//            automatically the first time the page is shown) over the Sample Match card
//            (SampleDropZone)
//
// The editor forwards audio files dropped anywhere on the window to analyseSample().

#include "Pages.h"

#include "engine/SoundMap.h"

#include <functional>
#include <memory>
#include <vector>

namespace verdant::ui
{

class BrowsePage : public Page, private juce::ChangeListener
{
public:
    explicit BrowsePage (Processor&);
    ~BrowsePage() override;

    /** Runs sample-to-patch on an audio file (shows progress, then Apply / Discard). */
    void analyseSample (const juce::File&);

    /** Installs sound-map points directly (e.g. computed synchronously by the render tool). */
    void setSoundMap (std::vector<SoundMapPoint>);

    /** Starts (or restarts, ignoring the cache) a background build of the sound map. */
    void buildSoundMap (bool ignoreCache);

    /** Wired by the editor: open the shared Save panel. */
    std::function<void()> onSaveAs;

    void pageShown() override;
    void pageHidden() override;
    void tick() override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace verdant::ui
