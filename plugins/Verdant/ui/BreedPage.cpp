#include "Pages.h"
#include "Theme.h"

#include "../PluginProcessor.h"

namespace verdant::ui
{

namespace
{
    // FOUNDATION STUB: the page's owner replaces this with the real BREED page.
    class BreedPage final : public Page
    {
    public:
        explicit BreedPage (Processor& p) : processor (p) {}

        void paint (juce::Graphics& g) override
        {
            g.setColour (juce::Colour (palette::text));
            g.setFont (font (20.0f, true));
            g.drawText ("BREED", getLocalBounds().withTrimmedBottom (24), juce::Justification::centred);
            g.setColour (juce::Colour (palette::textDim));
            g.setFont (font (12.0f));
            g.drawText ("Cross two parents into eight children", getLocalBounds().withTrimmedTop (30), juce::Justification::centred);
        }

    private:
        [[maybe_unused]] Processor& processor;
    };
}

std::unique_ptr<Page> makeBreedPage (Processor& p)
{
    return std::make_unique<BreedPage> (p);
}

} // namespace verdant::ui
