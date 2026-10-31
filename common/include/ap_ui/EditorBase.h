#pragma once

#include "Displays.h"
#include "LookAndFeel.h"
#include "Meters.h"
#include "ParamPanel.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <vector>

namespace ap::ui
{

/**
    Base editor for the plugins: a header (logo badge, name, tagline, header
    components on the right) above a content area the subclass lays out in
    layoutContent().

    Everything is laid out at a fixed base size and drawn scaled, so the
    window can be resized (fixed aspect ratio, 70–200 %); the chosen size is
    stored in the plugin state. One 30 Hz timer ticks every Animated
    component registered with addAnimated() (sections are registered
    automatically); headless tools call tick() directly instead.
*/
class EditorBase : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    static constexpr int headerHeight = 62;
    static constexpr int margin = 14;
    static constexpr int gap = 12;

    EditorBase (juce::AudioProcessor& p, juce::AudioProcessorValueTreeState& apvts,
                const juce::String& title, const juce::String& tagline, juce::Colour accent,
                int baseWidth, int baseHeight);
    ~EditorBase() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Steps every Animated component once (the editor's timer does this at 30 Hz). */
    void tick();

    int getBaseWidth() const noexcept  { return baseW; }
    int getBaseHeight() const noexcept { return baseH; }

protected:
    /** Lay the content out in base coordinates; `area` is everything below the header, inset by the margin. */
    virtual void layoutContent (juce::Rectangle<int> area) = 0;

    /** Makes `c` a visible child of the scaled content (not owned). Animated components are also registered. */
    void addContent (juce::Component& c);

    /** A new owned section card in the content. */
    ParamSection& addSection (const juce::String& title, const juce::StringArray& paramIds = {});

    /** Ticks `a` from the editor timer (and stops its own timer, if it has one). */
    void addAnimated (Animated& a);

    /** Components placed in the header from the right edge leftwards, in call order. */
    void addHeaderComponent (juce::Component& c, int width, int height);

    /** Draws the logo badge; the default is the title's first letter on the accent. */
    virtual void paintLogo (juce::Graphics&, juce::Rectangle<float> badge);

    /** Call at the end of the subclass constructor: lays out and applies the saved size. */
    void finishSetup();

    juce::AudioProcessorValueTreeState& state;
    LookAndFeel lookAndFeel;

private:
    class Canvas;
    void timerCallback() override { tick(); }

    std::unique_ptr<Canvas> canvas;
    std::vector<std::unique_ptr<ParamSection>> sections;
    std::vector<Animated*> animated;
    struct HeaderItem { juce::Component* c; int w, h; };
    std::vector<HeaderItem> headerItems;
    juce::String titleText, taglineText;
    int baseW, baseH;
    bool ready = false;

    friend class Canvas;
};

} // namespace ap::ui
