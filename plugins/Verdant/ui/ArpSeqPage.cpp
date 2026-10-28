#include "Pages.h"
#include "Theme.h"

#include "../PluginProcessor.h"

namespace verdant::ui
{

namespace
{
    // FOUNDATION STUB: the page's owner replaces this with the real ARP / SEQ page.
    class ArpSeqPage final : public Page
    {
    public:
        explicit ArpSeqPage (Processor& p) : processor (p) {}

        void paint (juce::Graphics& g) override
        {
            g.setColour (juce::Colour (palette::text));
            g.setFont (font (20.0f, true));
            g.drawText ("ARP / SEQ", getLocalBounds().withTrimmedBottom (24), juce::Justification::centred);
            g.setColour (juce::Colour (palette::textDim));
            g.setFont (font (12.0f));
            g.drawText ("Arpeggiator, step sequencer and the global key", getLocalBounds().withTrimmedTop (30), juce::Justification::centred);
        }

    private:
        [[maybe_unused]] Processor& processor;
    };
}

std::unique_ptr<Page> makeArpSeqPage (Processor& p)
{
    return std::make_unique<ArpSeqPage> (p);
}

} // namespace verdant::ui
