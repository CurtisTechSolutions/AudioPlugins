#include "ap_ui/ParamPanel.h"

namespace ap::ui
{

ParamControl::ParamControl (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId)
    : id (paramId)
{
    auto* param = apvts.getParameter (paramId);
    jassert (param != nullptr);   // typo in a parameter ID

    const juce::String name = param != nullptr ? param->getName (24) : paramId;
    nameLabel.setText (name, juce::dontSendNotification);
    nameLabel.setJustificationType (juce::Justification::centred);
    nameLabel.setFont (LookAndFeel::labelFont());
    nameLabel.setInterceptsMouseClicks (false, false);

    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (param))
    {
        combo = std::make_unique<juce::ComboBox>();
        combo->addItemList (choice->choices, 1);
        combo->setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (*combo);
        addAndMakeVisible (nameLabel);
        comboAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, paramId, *combo);
    }
    else if (dynamic_cast<juce::AudioParameterBool*> (param) != nullptr)
    {
        toggle = std::make_unique<juce::ToggleButton> (name);
        addAndMakeVisible (*toggle);
        buttonAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, paramId, *toggle);
    }
    else
    {
        slider = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
        slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, ParamSection::knobWidth - 6, 16);
        slider->setDoubleClickReturnValue (true, param != nullptr ? param->convertFrom0to1 (param->getDefaultValue()) : 0.0);
        slider->setPopupDisplayEnabled (false, false, nullptr);
        addAndMakeVisible (*slider);
        addAndMakeVisible (nameLabel);
        sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramId, *slider);
    }
}

void ParamControl::resized()
{
    auto r = getLocalBounds();
    if (slider != nullptr)
    {
        nameLabel.setBounds (r.removeFromTop (16));
        slider->setBounds (r);
    }
    else if (combo != nullptr)
    {
        nameLabel.setBounds (r.removeFromTop (16));
        r.removeFromTop (6);
        combo->setBounds (r.removeFromTop (24).reduced (2, 0));
    }
    else if (toggle != nullptr)
    {
        toggle->setBounds (r.withTrimmedTop (18).withHeight (24));
    }
}

ParamSection::ParamSection (juce::AudioProcessorValueTreeState& apvts, const juce::String& t,
                            const juce::StringArray& paramIds, int columns)
    : title (t), forcedColumns (columns)
{
    for (const auto& pid : paramIds)
    {
        auto c = std::make_unique<ParamControl> (apvts, pid);
        addAndMakeVisible (*c);
        controls.push_back (std::move (c));
    }
}

void ParamSection::setTrailingComponent (juce::Component* c, int width)
{
    trailing = c;
    trailingWidth = width;
    if (trailing != nullptr) addAndMakeVisible (trailing);
    resized();
}

int ParamSection::columnsFor (int width) const
{
    if (forcedColumns > 0) return forcedColumns;
    const int usable = width - 12 - (trailing != nullptr ? trailingWidth + 8 : 0);
    return juce::jmax (1, usable / knobWidth);
}

int ParamSection::preferredHeight (int width) const
{
    const int cols = columnsFor (width);
    const int rows = juce::jmax (1, ((int) controls.size() + cols - 1) / cols);
    return headerHeight + rows * knobHeight + (rows - 1) * rowGap + 10;
}

void ParamSection::paint (juce::Graphics& g)
{
    const auto th = dynamic_cast<LookAndFeel*> (&getLookAndFeel());
    const juce::Colour panel = th != nullptr ? th->theme().panel : juce::Colour (0xff1e2128);
    const juce::Colour edge  = th != nullptr ? th->theme().panelEdge : juce::Colour (0xff2c313a);
    const juce::Colour dim   = th != nullptr ? th->theme().textDim : juce::Colours::grey;
    const juce::Colour accent = th != nullptr ? th->theme().accent : juce::Colours::skyblue;

    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (panel);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (edge);
    g.drawRoundedRectangle (r, 8.0f, 1.0f);

    g.setColour (accent);
    g.fillRoundedRectangle (juce::Rectangle<float> (10.0f, 7.0f, 3.0f, (float) headerHeight - 10.0f), 1.5f);
    g.setColour (dim);
    g.setFont (LookAndFeel::sectionFont());
    g.drawText (title.toUpperCase(), 20, 2, getWidth() - 30, headerHeight, juce::Justification::centredLeft);
}

void ParamSection::resized()
{
    auto area = getLocalBounds().reduced (6);
    area.removeFromTop (headerHeight);

    if (trailing != nullptr)
    {
        auto t = area.removeFromRight (trailingWidth);
        area.removeFromRight (8);
        trailing->setBounds (t);
    }

    const int cols = columnsFor (getWidth());
    int idx = 0;
    for (auto& c : controls)
    {
        const int col = idx % cols;
        const int row = idx / cols;
        c->setBounds (area.getX() + col * knobWidth,
                      area.getY() + row * (knobHeight + rowGap),
                      knobWidth, knobHeight);
        ++idx;
    }
}

} // namespace ap::ui
