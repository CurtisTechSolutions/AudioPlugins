#pragma once

// The editor's pages (CONTRACT §7). Each page lives in its own ui/<Name>Page.cpp
// and is created through its factory; the editor shows one at a time and calls
// pageShown() / pageHidden() when switching.
//
// Animation: the editor owns ONE 30 Hz juce::Timer and calls tick() on the
// visible page only. A page registers its animated children (ModKnobs,
// visualisers, the source strip, ...) with addTickable(); the default tick()
// ticks them in registration order. Pages never run timers of their own, so
// hidden pages cost nothing.

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <memory>
#include <vector>

namespace verdant
{
class Processor;
}

namespace verdant::ui
{

/** Anything that animates from the editor's shared 30 Hz tick (read VisualState, repaint if changed). */
struct Tickable
{
    virtual ~Tickable() = default;
    virtual void tick() = 0;
};

struct Page : public juce::Component
{
    virtual void pageShown() {}
    virtual void pageHidden() {}

    /** Called at 30 Hz by the editor while this page is visible. */
    virtual void tick()
    {
        for (auto* t : tickables)
            t->tick();
    }

    /** Registers a child for the shared tick (not owned; remove it before it is destroyed). */
    void addTickable (Tickable* t)    { if (t != nullptr) tickables.push_back (t); }
    void removeTickable (Tickable* t) { tickables.erase (std::remove (tickables.begin(), tickables.end(), t), tickables.end()); }

private:
    std::vector<Tickable*> tickables;
};

std::unique_ptr<Page> makeOscPage (Processor&);
std::unique_ptr<Page> makeModPage (Processor&);
std::unique_ptr<Page> makeFxPage (Processor&);
std::unique_ptr<Page> makeArpSeqPage (Processor&);
std::unique_ptr<Page> makeBreedPage (Processor&);
std::unique_ptr<Page> makeBrowsePage (Processor&);

} // namespace verdant::ui
