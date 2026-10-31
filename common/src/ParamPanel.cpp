#include "ap_ui/ParamPanel.h"

namespace ap::ui
{

namespace
{
    constexpr int kLabelH = 14;
    constexpr int kValueH = 18;
    constexpr int kWellH  = 26;

    struct KnobSize { int width, height, diameter; };
    KnobSize knobSize (ControlStyle s)
    {
        if (s == ControlStyle::SmallKnob) return { 64, 88, 46 };
        if (s == ControlStyle::LargeKnob) return { 108, 142, 90 };
        return { 76, 104, 60 };
    }

    float textWidth (const juce::Font& f, const juce::String& s)
    {
        return juce::GlyphArrangement::getStringWidth (f, s);
    }
}

//==============================================================================
/** Choice parameter as a row of segments (one click selects). */
class ParamControl::Segments : public juce::Component
{
public:
    Segments (juce::RangedAudioParameter& p, juce::StringArray names)
        : labels (std::move (names)),
          attachment (p, [this] (float v) { selected = juce::roundToInt (v); repaint(); }, nullptr)
    {
        attachment.sendInitialUpdate();
    }

    static int preferredWidth (const juce::StringArray& names)
    {
        float w = 0.0f;
        for (auto& n : names) w += juce::jmax (30.0f, textWidth (Type::label(), n) + 16.0f);
        return juce::jmax (84, (int) std::ceil (w));
    }

    void paint (juce::Graphics& g) override
    {
        const auto& th = themeOf (*this);
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        drawWell (g, r, th, 6.0f);
        const int n = juce::jmax (1, labels.size());
        for (int i = 0; i < n; ++i)
        {
            auto seg = segment (i).toFloat();
            if (i == selected)
            {
                auto sel = seg.reduced (2.0f);
                g.setColour (th.accent.withAlpha (isEnabled() ? 0.22f : 0.1f));
                g.fillRoundedRectangle (sel, 4.5f);
                g.setColour (th.accent.withAlpha (0.85f));
                g.drawRoundedRectangle (sel.reduced (0.5f), 4.5f, 1.0f);
            }
            else if (i == hover)
            {
                g.setColour (juce::Colours::white.withAlpha (0.04f));
                g.fillRoundedRectangle (seg.reduced (2.0f), 4.5f);
            }
            if (i > 0 && i != selected && i - 1 != selected)
            {
                g.setColour (th.wellEdge);
                g.fillRect (seg.getX(), seg.getY() + 6.0f, 1.0f, seg.getHeight() - 12.0f);
            }
            g.setColour (i == selected ? th.text : (i == hover ? th.text.withAlpha (0.8f) : th.textDim));
            g.setFont (i == selected ? Type::label().boldened() : Type::label());
            g.drawFittedText (labels[i], seg.toNearestInt().reduced (3, 0), juce::Justification::centred, 1, 0.8f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = indexAt (e.x);
        if (i >= 0 && i != selected)
            attachment.setValueAsCompleteGesture ((float) i);
    }
    void mouseMove (const juce::MouseEvent& e) override { setHover (indexAt (e.x)); }
    void mouseExit (const juce::MouseEvent&) override   { setHover (-1); }

private:
    juce::Rectangle<int> segment (int i) const
    {
        const int n = juce::jmax (1, labels.size());
        const int x0 = getWidth() * i / n, x1 = getWidth() * (i + 1) / n;
        return { x0, 0, x1 - x0, getHeight() };
    }
    int indexAt (int x) const
    {
        const int n = labels.size();
        return n > 0 ? juce::jlimit (0, n - 1, x * n / juce::jmax (1, getWidth())) : -1;
    }
    void setHover (int i) { if (i != hover) { hover = i; repaint(); } }

    juce::StringArray labels;
    juce::ParameterAttachment attachment;
    int selected = 0, hover = -1;
};

//==============================================================================
ParamControl::ParamControl (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId,
                            ControlStyle styleIn, const juce::String& label)
    : state (apvts), id (paramId), kind (styleIn)
{
    auto* param = apvts.getParameter (paramId);
    jassert (param != nullptr);   // typo in a parameter ID

    name = label.isNotEmpty() ? label : (param != nullptr ? param->getName (32) : paramId);

    auto* choice = dynamic_cast<juce::AudioParameterChoice*> (param);
    const bool isBool = dynamic_cast<juce::AudioParameterBool*> (param) != nullptr;

    if (kind == ControlStyle::Auto)
    {
        if (isBool)
            kind = ControlStyle::Switch;
        else if (choice != nullptr)
        {
            bool shortNames = choice->choices.size() <= 4;
            for (auto& c : choice->choices) shortNames = shortNames && c.length() <= 8;
            kind = shortNames ? ControlStyle::Segmented : ControlStyle::Combo;
        }
        else
            kind = ControlStyle::Knob;
    }

    if (kind == ControlStyle::Segmented && choice != nullptr)
    {
        segments = std::make_unique<Segments> (*choice, choice->choices);
        addAndMakeVisible (*segments);
    }
    else if ((kind == ControlStyle::Combo || kind == ControlStyle::Segmented) && choice != nullptr)
    {
        kind = ControlStyle::Combo;
        combo = std::make_unique<juce::ComboBox>();
        combo->addItemList (choice->choices, 1);
        addAndMakeVisible (*combo);
        comboAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, paramId, *combo);
    }
    else if (isBool)
    {
        kind = ControlStyle::Switch;
        toggle = std::make_unique<juce::ToggleButton> (name);
        addAndMakeVisible (*toggle);
        buttonAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, paramId, *toggle);
    }
    else
    {
        if (kind != ControlStyle::SmallKnob && kind != ControlStyle::LargeKnob) kind = ControlStyle::Knob;
        slider = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
        slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, knobSize (kind).width - 4, kValueH);
        slider->setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
        slider->setDoubleClickReturnValue (true, param != nullptr ? param->convertFrom0to1 (param->getDefaultValue()) : 0.0);
        slider->setPopupDisplayEnabled (false, false, nullptr);
        slider->setMouseDragSensitivity (kind == ControlStyle::LargeKnob ? 300 : 220);
        // Explicit, so the value box never picks up a default outline before the editor's look and feel applies.
        slider->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider->setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (*slider);
        sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramId, *slider);
        slider->onDragStart = slider->onDragEnd = [this] { updateHot(); };
        slider->addMouseListener (this, true);
    }
}

void ParamControl::updateHot()
{
    const bool now = slider != nullptr && slider->isMouseOverOrDragging (true);
    if (now == hot)
        return;
    hot = now;
    // Colour the value label itself: changing the slider's colours would make it rebuild the label.
    const auto& th = themeOf (*this);
    for (auto* child : slider->getChildren())
        if (auto* label = dynamic_cast<juce::Label*> (child))
            label->setColour (juce::Label::textColourId, hot ? th.text : th.textDim);
    repaint();
}

ParamControl::~ParamControl() = default;

int ParamControl::preferredWidth() const noexcept
{
    auto* choice = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (id));
    if (kind == ControlStyle::Switch)
        return juce::jmax (84, (int) std::ceil (textWidth (Type::label(), name)) + 46);
    if (kind == ControlStyle::Segmented)
        return choice != nullptr ? juce::jmax (Segments::preferredWidth (choice->choices), (int) textWidth (Type::section(), name.toUpperCase()) + 4)
                                 : 100;
    if (kind == ControlStyle::Combo)
    {
        float w = textWidth (Type::section(), name.toUpperCase());
        if (choice != nullptr)
            for (auto& s : choice->choices) w = juce::jmax (w, textWidth (Type::label(), s) + 34.0f);
        return juce::jlimit (96, 172, (int) std::ceil (w));
    }
    return juce::jmax (knobSize (kind).width, (int) std::ceil (textWidth (Type::small(), name.toUpperCase()) * 1.12f) + 6);
}

int ParamControl::preferredHeight() const noexcept
{
    if (kind == ControlStyle::Switch)
        return 28;
    if (kind == ControlStyle::Segmented || kind == ControlStyle::Combo)
        return kLabelH + 6 + kWellH;
    return knobSize (kind).height;
}

void ParamControl::activeWhen (const juce::String& otherParamId, std::function<bool (float)> predicate)
{
    condition = state.getRawParameterValue (otherParamId);
    jassert (condition != nullptr);
    conditionTest = std::move (predicate);
    tick();
}

void ParamControl::tick()
{
    if (condition == nullptr || conditionTest == nullptr)
        return;
    const bool shouldDim = ! conditionTest (condition->load (std::memory_order_relaxed));
    if (shouldDim != dimmed)
    {
        dimmed = shouldDim;
        setAlpha (dimmed ? 0.38f : 1.0f);
    }
}

void ParamControl::paint (juce::Graphics& g)
{
    if (kind == ControlStyle::Switch)
        return;
    const auto& th = themeOf (*this);
    if (slider != nullptr)
    {
        // A soft pill behind the value read-out.
        const auto sb = slider->getBounds();
        const float w = juce::jmin ((float) sb.getWidth() - 6.0f, 64.0f);
        const auto pill = juce::Rectangle<float> (w, (float) kValueH - 2.0f)
                              .withCentre ({ (float) sb.getCentreX(), (float) sb.getBottom() - kValueH * 0.5f });
        g.setColour (hot ? th.accent.withAlpha (0.14f) : th.well.withAlpha (0.8f));
        g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
        g.setColour (hot ? th.accent.withAlpha (0.5f) : th.wellEdge);
        g.drawRoundedRectangle (pill.reduced (0.5f), pill.getHeight() * 0.5f, 1.0f);
    }
    const auto font = kind == ControlStyle::LargeKnob ? Type::section().withHeight (11.5f) : Type::small().boldened().withExtraKerningFactor (0.1f);
    drawCaps (g, name, getLocalBounds().removeFromTop (kLabelH).toFloat(), hot ? th.text : th.textDim,
              slider != nullptr ? juce::Justification::centred : juce::Justification::centredLeft, font);
}

void ParamControl::resized()
{
    auto r = getLocalBounds();
    if (slider != nullptr)
    {
        r.removeFromTop (kLabelH + (kind == ControlStyle::LargeKnob ? 4 : 2));
        const auto ks = knobSize (kind);
        // The slider draws the knob square in its top part and the value box below.
        slider->setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth(), ks.width), r.getHeight()));
        slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, juce::jmin (r.getWidth(), ks.width) - 4, kValueH);
    }
    else if (combo != nullptr || segments != nullptr)
    {
        r.removeFromTop (kLabelH + 6);
        auto well = r.removeFromTop (kWellH);
        if (combo != nullptr) combo->setBounds (well);
        else                  segments->setBounds (well);
    }
    else if (toggle != nullptr)
    {
        toggle->setBounds (r.withSizeKeepingCentre (r.getWidth(), 24));
    }
}

//==============================================================================
ParamSection::ParamSection (juce::AudioProcessorValueTreeState& apvts, const juce::String& t,
                            const juce::StringArray& paramIds, int columns)
    : state (apvts), title (t), forcedColumns (columns)
{
    for (const auto& pid : paramIds)
        add (pid);
}

ParamSection::~ParamSection() = default;

ParamControl& ParamSection::add (const juce::String& paramId, ControlStyle style, const juce::String& label)
{
    auto c = std::make_unique<ParamControl> (state, paramId, style, label);
    addAndMakeVisible (*c);
    Item item;
    item.comp = c.get();
    item.control = c.get();
    item.width = c->preferredWidth();
    item.height = c->preferredHeight();
    items.push_back (item);
    controls.push_back (std::move (c));
    return *controls.back();
}

void ParamSection::addComponent (juce::Component* c, int width, int height)
{
    jassert (c != nullptr);
    addAndMakeVisible (c);
    Item item;
    item.comp = c;
    item.width = width;
    item.height = height;
    items.push_back (item);
}

void ParamSection::newRow()
{
    Item item;
    item.rowBreak = true;
    items.push_back (item);
}

void ParamSection::setHeaderSwitch (const juce::String& boolParamId)
{
    headerSwitch = std::make_unique<juce::ToggleButton>();
    headerSwitch->setTooltip (state.getParameter (boolParamId) != nullptr ? state.getParameter (boolParamId)->getName (64) : boolParamId);
    addAndMakeVisible (*headerSwitch);
    headerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, boolParamId, *headerSwitch);
    tick();
}

void ParamSection::setActiveWhen (const juce::String& paramId, std::function<bool (float)> predicate)
{
    activeParam = state.getRawParameterValue (paramId);
    jassert (activeParam != nullptr);
    activeTest = std::move (predicate);
    tick();
}

void ParamSection::setTrailingComponent (juce::Component* c, int width)
{
    trailing = c;
    trailingWidth = width;
    if (trailing != nullptr) addAndMakeVisible (trailing);
    resized();
}

ParamControl* ParamSection::control (const juce::String& paramId) const
{
    for (auto& c : controls)
        if (c->paramId() == paramId) return c.get();
    return nullptr;
}

juce::Rectangle<int> ParamSection::bodyArea (juce::Rectangle<int> b) const
{
    b.removeFromTop (headerHeight);
    b = b.withTrimmedLeft (padding).withTrimmedRight (padding).withTrimmedBottom (padding - 2);
    if (trailing != nullptr)
        b.removeFromRight (trailingWidth + 10);
    return b;
}

std::vector<ParamSection::Placement> ParamSection::computeLayout (juce::Rectangle<int> body) const
{
    std::vector<Placement> out;
    std::vector<const Item*> row;
    int y = body.getY();

    auto flush = [&]
    {
        if (row.empty()) return;
        int total = 0, rowH = 0;
        for (auto* it : row) { total += it->width; rowH = juce::jmax (rowH, it->height); }
        const int n = (int) row.size();
        const int free = juce::jmax (0, body.getWidth() - total);
        const int gap = juce::jlimit (4, 26, free / (n + 1));
        const int groupW = total + gap * (n - 1);
        int x = body.getX() + juce::jmax (0, (body.getWidth() - groupW) / 2);
        for (auto* it : row)
        {
            out.push_back ({ it->comp, { x, y + (rowH - it->height) / 2, it->width, it->height } });
            x += it->width + gap;
        }
        y += rowH + rowGap;
        row.clear();
    };

    for (auto& it : items)
    {
        if (it.rowBreak) { flush(); continue; }
        if (it.width <= 0)
        {
            flush();
            out.push_back ({ it.comp, { body.getX(), y, body.getWidth(), it.height } });
            y += it.height + rowGap;
            continue;
        }
        int used = 0;
        for (auto* r : row) used += r->width + 4;
        const bool full = forcedColumns > 0 ? (int) row.size() >= forcedColumns
                                            : (! row.empty() && used + it.width > body.getWidth());
        if (full) flush();
        row.push_back (&it);
    }
    flush();
    return out;
}

int ParamSection::preferredHeight (int width) const
{
    const auto body = bodyArea ({ 0, 0, width, 10000 });
    const auto placed = computeLayout (body);
    int bottom = body.getY();
    for (auto& p : placed) bottom = juce::jmax (bottom, p.bounds.getBottom());
    return bottom + padding - 2;
}

void ParamSection::paint (juce::Graphics& g)
{
    const auto& th = themeOf (*this);
    drawCard (g, getLocalBounds().toFloat().reduced (1.0f), th);

    auto head = getLocalBounds().removeFromTop (headerHeight).reduced (padding, 0).toFloat();
    const auto dot = head.removeFromLeft (6.0f).withSizeKeepingCentre (6.0f, 6.0f).translated (0.0f, 1.0f);
    g.setColour (bodyDimmed ? th.textFaint : th.accent);
    g.fillEllipse (dot);
    if (! bodyDimmed)
    {
        g.setColour (th.accent.withAlpha (0.25f));
        g.fillEllipse (dot.expanded (2.5f));
    }
    head.removeFromLeft (8.0f);
    if (headerSwitch != nullptr) head.removeFromRight (44.0f);
    if (caption.isNotEmpty())
    {
        g.setColour (th.textFaint);
        g.setFont (Type::small());
        g.drawText (caption, head.translated (0.0f, 1.0f), juce::Justification::centredRight, true);
    }
    drawCaps (g, title, head.translated (0.0f, 1.0f), bodyDimmed ? th.textDim : th.text.withAlpha (0.92f));
}

void ParamSection::resized()
{
    if (headerSwitch != nullptr)
        headerSwitch->setBounds (getWidth() - padding - 38, 4, 40, headerHeight - 6);

    const auto body = bodyArea (getLocalBounds());
    if (trailing != nullptr)
        trailing->setBounds (body.getRight() + 10, body.getY(), trailingWidth, body.getHeight());
    for (auto& p : computeLayout (body))
        p.comp->setBounds (p.bounds);
}

void ParamSection::tick()
{
    for (auto& c : controls)
        c->tick();

    if (headerSwitch != nullptr || activeParam != nullptr)
    {
        bool off = headerSwitch != nullptr && ! headerSwitch->getToggleState();
        if (activeParam != nullptr && activeTest != nullptr)
            off = off || ! activeTest (activeParam->load (std::memory_order_relaxed));
        if (off != bodyDimmed)
        {
            bodyDimmed = off;
            for (auto& it : items)
                if (it.comp != nullptr)
                    it.comp->setAlpha (off ? 0.38f : 1.0f);
            repaint();
        }
    }
}

} // namespace ap::ui
