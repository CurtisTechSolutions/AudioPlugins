#include "ap_ui/PresetBar.h"

#include "ap_ui/LookAndFeel.h"

namespace ap::ui
{

void PresetBar::Arrow::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto* lf = dynamic_cast<LookAndFeel*> (&getLookAndFeel());
    const auto th = lf != nullptr ? lf->theme() : Theme {};
    auto r = getLocalBounds().toFloat();
    if (highlighted || down)
    {
        g.setColour (th.knobTrack.withAlpha (down ? 1.0f : 0.6f));
        g.fillRoundedRectangle (r, 4.0f);
    }
    r = r.reduced (r.getWidth() * 0.3f, r.getHeight() * 0.28f);
    juce::Path tri;
    if (left) tri.addTriangle (r.getRight(), r.getY(), r.getRight(), r.getBottom(), r.getX(), r.getCentreY());
    else      tri.addTriangle (r.getX(), r.getY(), r.getX(), r.getBottom(), r.getRight(), r.getCentreY());
    g.setColour (isEnabled() ? th.accent : th.textDim);
    g.fillPath (tri);
}

PresetBar::PresetBar (ap::plug::Programs& p) : programs (p)
{
    juce::String lastCategory;
    for (int i = 0; i < programs.size(); ++i)
    {
        const auto& preset = programs.preset (i);
        if (preset.category.isNotEmpty() && preset.category != lastCategory)
        {
            menu.addSectionHeading (preset.category);
            lastCategory = preset.category;
        }
        menu.addItem (preset.name, i + 1);
    }
    menu.setTooltip ("Factory presets");
    menu.onChange = [this]
    {
        // Choosing the current preset again while it is edited reloads it.
        const int index = menu.getSelectedId() - 1;
        if (index >= 0 && (index != programs.getCurrentProgram() || programs.isModified()))
            programs.selectFromEditor (index);
        refresh();
    };
    prev.onClick = [this] { step (-1); };
    next.onClick = [this] { step (+1); };
    prev.setTooltip ("Previous preset");
    next.setTooltip ("Next preset");

    addAndMakeVisible (prev);
    addAndMakeVisible (menu);
    addAndMakeVisible (next);
    refresh();
    startTimerHz (5);
}

PresetBar::~PresetBar() { stopTimer(); }

void PresetBar::step (int delta)
{
    const int n = programs.size();
    if (n <= 0) return;
    programs.selectFromEditor ((programs.getCurrentProgram() + delta + n) % n);
    refresh();
}

void PresetBar::refresh()
{
    const int program = programs.getCurrentProgram();
    const bool modified = programs.isModified();
    if (program == shownProgram && modified == shownModified) return;
    shownProgram = program;
    shownModified = modified;
    menu.setSelectedId (program + 1, juce::dontSendNotification);
    // The text shows the edited mark; the menu keeps the plain names.
    if (modified) menu.setText (programs.getProgramName (program) + " *", juce::dontSendNotification);
}

void PresetBar::timerCallback() { refresh(); }

void PresetBar::paint (juce::Graphics& g)
{
    const auto* lf = dynamic_cast<LookAndFeel*> (&getLookAndFeel());
    const auto th = lf != nullptr ? lf->theme() : Theme {};
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (th.panel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (th.panelEdge);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);

    g.setColour (th.textDim);
    g.setFont (LookAndFeel::sectionFont());
    g.drawText ("PRESET", getLocalBounds().reduced (12, 0).removeFromLeft (60), juce::Justification::centredLeft);
    g.setFont (LookAndFeel::labelFont());
    g.drawText (juce::String (programs.getCurrentProgram() + 1) + " / " + juce::String (programs.size()),
                getLocalBounds().reduced (12, 0).removeFromRight (70), juce::Justification::centredRight);
}

void PresetBar::resized()
{
    auto r = getLocalBounds().reduced (6, 4);
    r.removeFromLeft (64);
    r.removeFromRight (76);
    const int arrow = r.getHeight();
    prev.setBounds (r.removeFromLeft (arrow));
    next.setBounds (r.removeFromRight (arrow));
    menu.setBounds (r.reduced (4, 0));
}

} // namespace ap::ui
