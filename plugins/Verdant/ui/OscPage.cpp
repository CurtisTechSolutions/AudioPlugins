#include "Pages.h"
#include "Theme.h"

#include "../PluginProcessor.h"

#include <ap_ui/ParamPanel.h>

namespace verdant::ui
{

namespace
{
    // FOUNDATION STUB: generic parameter sections so the shell is visibly real.
    // The OSC page's owner replaces this with the wavetable views, filters and Env 1 strip.
    class OscPage final : public Page
    {
    public:
        explicit OscPage (Processor& p)
            : processor (p),
              oscA (p.apvts, "Osc A", { "a_on", "a_table", "a_pos", "a_warp_mode", "a_warp", "a_level", "a_uni_voices", "a_uni_detune" }, 4),
              filter1 (p.apvts, "Filter 1", { "f1_on", "f1_type", "f1_cutoff", "f1_res", "f1_drive", "f1_mix" }, 3),
              env1 (p.apvts, "Env 1", { "env1_a", "env1_d", "env1_s", "env1_r" }, 2)
        {
            for (auto* s : { &oscA, &filter1, &env1 })
                addAndMakeVisible (*s);
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (juce::Colour (palette::textDim));
            g.setFont (font (12.0f, true));
            g.drawText ("OSC", getLocalBounds().reduced (16, 8).removeFromTop (18), juce::Justification::centredLeft);
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (12);
            area.removeFromTop (24);
            const int gap = 12;
            const int wA = 4 * ap::ui::ParamSection::knobWidth + 24;
            const int wF = 3 * ap::ui::ParamSection::knobWidth + 24;
            const int wE = 2 * ap::ui::ParamSection::knobWidth + 24;

            oscA.setBounds (area.removeFromLeft (wA).withHeight (oscA.preferredHeight (wA)));
            area.removeFromLeft (gap);
            filter1.setBounds (area.removeFromLeft (wF).withHeight (filter1.preferredHeight (wF)));
            area.removeFromLeft (gap);
            env1.setBounds (area.removeFromLeft (wE).withHeight (env1.preferredHeight (wE)));
        }

    private:
        [[maybe_unused]] Processor& processor;
        ap::ui::ParamSection oscA, filter1, env1;
    };
}

std::unique_ptr<Page> makeOscPage (Processor& p)
{
    return std::make_unique<OscPage> (p);
}

} // namespace verdant::ui
