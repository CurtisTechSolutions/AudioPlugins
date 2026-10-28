#include "Pages.h"
#include "Theme.h"

#include "../PluginProcessor.h"

namespace verdant::ui
{

namespace
{
    // FOUNDATION STUB: the page's owner replaces this with the real BROWSE page.
    class BrowsePage final : public Page
    {
    public:
        explicit BrowsePage (Processor& p) : processor (p) {}

        void paint (juce::Graphics& g) override
        {
            g.setColour (juce::Colour (palette::text));
            g.setFont (font (20.0f, true));
            g.drawText ("BROWSE", getLocalBounds().withTrimmedBottom (24), juce::Justification::centred);
            g.setColour (juce::Colour (palette::textDim));
            g.setFont (font (12.0f));
            g.drawText ("Preset browser, sound map and sample matching", getLocalBounds().withTrimmedTop (30), juce::Justification::centred);
        }

    private:
        [[maybe_unused]] Processor& processor;
    };
}

std::unique_ptr<Page> makeBrowsePage (Processor& p)
{
    return std::make_unique<BrowsePage> (p);
}

} // namespace verdant::ui
