#include "Pages.h"
#include "Theme.h"

#include "../PluginProcessor.h"

namespace verdant::ui
{

namespace
{
    // FOUNDATION STUB: the page's owner replaces this with the real MOD page.
    class ModPage final : public Page
    {
    public:
        explicit ModPage (Processor& p) : processor (p) {}

        void paint (juce::Graphics& g) override
        {
            g.setColour (juce::Colour (palette::text));
            g.setFont (font (20.0f, true));
            g.drawText ("MOD", getLocalBounds().withTrimmedBottom (24), juce::Justification::centred);
            g.setColour (juce::Colour (palette::textDim));
            g.setFont (font (12.0f));
            g.drawText ("Envelopes, LFOs, macros, physics, Alive and the 24-slot matrix", getLocalBounds().withTrimmedTop (30), juce::Justification::centred);
        }

    private:
        [[maybe_unused]] Processor& processor;
    };
}

std::unique_ptr<Page> makeModPage (Processor& p)
{
    return std::make_unique<ModPage> (p);
}

} // namespace verdant::ui
