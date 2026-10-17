#include "ap_ui/EditorBase.h"

namespace ap::ui
{

EditorBase::EditorBase (juce::AudioProcessor& p, juce::AudioProcessorValueTreeState& apvts,
                        const juce::String& title, const juce::String& tagline, juce::Colour accent,
                        int width, int numColumns)
    : juce::AudioProcessorEditor (p),
      state (apvts),
      lookAndFeel (Theme::withAccent (accent)),
      titleText (title),
      taglineText (tagline),
      editorWidth (width),
      columns (juce::jmax (1, numColumns))
{
    setLookAndFeel (&lookAndFeel);
    setSize (editorWidth, headerHeight + margin);
}

EditorBase::~EditorBase()
{
    setLookAndFeel (nullptr);
}

int EditorBase::columnWidth() const noexcept
{
    return (editorWidth - margin * (columns + 1)) / columns;
}

ParamSection& EditorBase::addSection (const juce::String& title, const juce::StringArray& paramIds,
                                      int knobColumns, int column)
{
    Entry e;
    e.section = std::make_unique<ParamSection> (state, title, paramIds, knobColumns);
    e.section->setLookAndFeel (&lookAndFeel);
    e.column = juce::jlimit (0, columns - 1, column);
    addAndMakeVisible (*e.section);
    entries.push_back (std::move (e));
    return *entries.back().section;
}

void EditorBase::addCustomSection (juce::Component* c, int height, int column)
{
    Entry e;
    e.custom = c;
    e.customHeight = height;
    e.column = juce::jlimit (0, columns - 1, column);
    addAndMakeVisible (c);
    entries.push_back (std::move (e));
}

void EditorBase::setHeaderComponent (juce::Component* c, int width, int height)
{
    headerComponent = c;
    headerComponentWidth = width;
    headerComponentHeight = height;
    if (c != nullptr) addAndMakeVisible (c);
}

void EditorBase::finishLayout()
{
    std::vector<int> heights ((size_t) columns, headerHeight + margin);
    const int w = columnWidth();
    for (auto& e : entries)
    {
        const int h = e.section != nullptr ? e.section->preferredHeight (w) : e.customHeight;
        heights[(size_t) e.column] += h + margin;
    }
    int total = 0;
    for (int h : heights) total = juce::jmax (total, h);
    setSize (editorWidth, total);
}

void EditorBase::paint (juce::Graphics& g)
{
    const auto& th = lookAndFeel.theme();
    g.fillAll (th.background);

    auto header = getLocalBounds().removeFromTop (headerHeight);
    g.setColour (th.panel.darker (0.2f));
    g.fillRect (header);
    g.setColour (th.accent);
    g.fillRect (header.removeFromBottom (2));

    auto text = header.reduced (margin + 4, 0);
    if (headerComponent != nullptr) text.removeFromRight (headerComponentWidth + 8);
    g.setColour (th.text);
    g.setFont (LookAndFeel::titleFont());
    g.drawText (titleText, text.removeFromTop (30), juce::Justification::bottomLeft);
    g.setColour (th.textDim);
    g.setFont (LookAndFeel::labelFont());
    g.drawText (taglineText, text, juce::Justification::topLeft);
}

void EditorBase::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (headerHeight);
    if (headerComponent != nullptr)
    {
        auto hc = header.reduced (margin, 0).removeFromRight (headerComponentWidth);
        headerComponent->setBounds (hc.withSizeKeepingCentre (headerComponentWidth,
                                                              juce::jmin (headerComponentHeight, headerHeight - 8)));
    }

    const int w = columnWidth();
    std::vector<int> y ((size_t) columns, headerHeight + margin);
    for (auto& e : entries)
    {
        const int x = margin + e.column * (w + margin);
        const int h = e.section != nullptr ? e.section->preferredHeight (w) : e.customHeight;
        juce::Component* c = e.section != nullptr ? static_cast<juce::Component*> (e.section.get()) : e.custom;
        c->setBounds (x, y[(size_t) e.column], w, h);
        y[(size_t) e.column] += h + margin;
    }
}

} // namespace ap::ui
