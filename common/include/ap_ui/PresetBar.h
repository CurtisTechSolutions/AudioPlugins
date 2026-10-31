#pragma once

#include <ap_plug/Programs.h>

#include <juce_gui_basics/juce_gui_basics.h>

namespace ap::ui
{

/**
    The factory-preset strip under the editor header: previous / next arrows
    around a menu of the bank grouped by category. It follows program changes
    made by the host, and marks the name with '*' once a parameter is edited.
*/
class PresetBar : public juce::Component, private juce::Timer
{
public:
    explicit PresetBar (ap::plug::Programs&);
    ~PresetBar() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void step (int delta);
    void refresh();

    /** A triangle button drawn in the theme's accent colour. */
    class Arrow : public juce::Button
    {
    public:
        Arrow (const juce::String& name, bool pointsLeft) : juce::Button (name), left (pointsLeft) {}
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    private:
        bool left;
    };

    ap::plug::Programs& programs;
    Arrow prev { "Previous preset", true };
    Arrow next { "Next preset", false };
    juce::ComboBox menu;
    int shownProgram = -1;
    bool shownModified = false;
};

} // namespace ap::ui
