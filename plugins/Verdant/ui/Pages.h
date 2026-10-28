#pragma once

// The editor's pages (CONTRACT §7). Each page lives in its own ui/<Name>Page.cpp
// and is created through its factory; the editor shows one at a time and calls
// pageShown() / pageHidden() when switching (start / stop visualiser timers there).

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace verdant
{
class Processor;
}

namespace verdant::ui
{

struct Page : public juce::Component
{
    virtual void pageShown() {}
    virtual void pageHidden() {}
};

std::unique_ptr<Page> makeOscPage (Processor&);
std::unique_ptr<Page> makeModPage (Processor&);
std::unique_ptr<Page> makeFxPage (Processor&);
std::unique_ptr<Page> makeArpSeqPage (Processor&);
std::unique_ptr<Page> makeBreedPage (Processor&);
std::unique_ptr<Page> makeBrowsePage (Processor&);

} // namespace verdant::ui
