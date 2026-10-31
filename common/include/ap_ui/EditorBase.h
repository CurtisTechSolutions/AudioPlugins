#pragma once

#include "LookAndFeel.h"
#include "ParamPanel.h"
#include "PresetBar.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <vector>

namespace ap::ui
{

/**
    Base editor: dark header with the plugin name and a tagline, then one or
    two columns of ParamSections. Subclasses call addSection() in their
    constructor, then finishLayout(); the base takes care of layout and sizing.
*/
class EditorBase : public juce::AudioProcessorEditor
{
public:
    EditorBase (juce::AudioProcessor& p, juce::AudioProcessorValueTreeState& apvts,
                const juce::String& title, const juce::String& tagline, juce::Colour accent,
                int width = 640, int numColumns = 1);
    ~EditorBase() override;

    void paint (juce::Graphics&) override;
    void resized() override;

protected:
    static constexpr int headerHeight = 54;
    static constexpr int margin = 10;
    static constexpr int presetBarHeight = 30;

    /** `column` selects the layout column (0-based) when the editor has more than one. */
    ParamSection& addSection (const juce::String& title, const juce::StringArray& paramIds,
                              int knobColumns = 0, int column = 0);

    /** Any component to be stacked in a column like a section, with a fixed height. */
    void addCustomSection (juce::Component* c, int height, int column = 0);

    /** A component placed in the header's right side (readouts, meters). */
    void setHeaderComponent (juce::Component* c, int width, int height);

    /** Adds the factory-preset strip under the header; call before adding sections. */
    void addPresetBar (ap::plug::Programs& programs);

    /** Top of the content area: below the header, and below the preset strip when there is one. */
    int contentTop() const noexcept;

    /** Recomputes the window height from the sections; call after adding sections. */
    void finishLayout();

    int columnWidth() const noexcept;

    juce::AudioProcessorValueTreeState& state;
    LookAndFeel lookAndFeel;

private:
    struct Entry
    {
        std::unique_ptr<ParamSection> section;   // owned, or
        juce::Component* custom = nullptr;       // not owned
        int customHeight = 0;
        int column = 0;
    };

    juce::String titleText, taglineText;
    std::vector<Entry> entries;
    std::unique_ptr<PresetBar> presetBar;
    juce::Component* headerComponent = nullptr;
    int headerComponentWidth = 0, headerComponentHeight = 0;
    int editorWidth;
    int columns;
};

} // namespace ap::ui
