#include "Pages.h"
#include "Theme.h"

#include "../PluginProcessor.h"

namespace verdant::ui
{

namespace
{
    // FOUNDATION STUB: the page's owner replaces this with the real FX page.
    class FxPage final : public Page
    {
    public:
        explicit FxPage (Processor& p) : processor (p) {}

        void paint (juce::Graphics& g) override
        {
            g.setColour (juce::Colour (palette::text));
            g.setFont (font (20.0f, true));
            g.drawText ("FX", getLocalBounds().withTrimmedBottom (24), juce::Justification::centred);
            g.setColour (juce::Colour (palette::textDim));
            g.setFont (font (12.0f));
            g.drawText ("Eight reorderable effect slots and the Sympathetic body", getLocalBounds().withTrimmedTop (30), juce::Justification::centred);
        }

    private:
        [[maybe_unused]] Processor& processor;
    };
}

std::unique_ptr<Page> makeFxPage (Processor& p)
{
    return std::make_unique<FxPage> (p);
}

} // namespace verdant::ui
