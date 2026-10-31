#include "ModKnob.h"
#include "ModSourceChip.h"

#include "../PluginProcessor.h"

namespace verdant::ui
{

namespace
{
    juce::Colour col (juce::uint32 c) { return juce::Colour (c); }

    juce::RangedAudioParameter* parameterFor (Processor& p, P param)
    {
        return p.apvts.getParameter (info (param).id);
    }

    float rawValue (Processor& p, P param)
    {
        if (auto* v = p.apvts.getRawParameterValue (info (param).id))
            return v->load (std::memory_order_relaxed);
        return 0.0f;
    }

    juce::String percent (float amount)
    {
        const int pc = juce::roundToInt (amount * 100.0f);
        return (pc > 0 ? "+" : "") + juce::String (pc) + " %";
    }
}

//==============================================================================
ModulationModel::ModulationModel (Processor& p) : proc (p)
{
    refresh();
}

bool ModulationModel::refresh()
{
    bool changed = false;
    for (int s = 0; s < kModSlots; ++s)
    {
        Route r;
        r.slot = s;
        r.source = (ModSource) juce::jlimit (0, (int) ModSource::Count - 1, (int) std::lround (rawValue (proc, modSrc (s))));
        r.dest = juce::jlimit (0, kNumModDestinations, (int) std::lround (rawValue (proc, modDst (s))));
        r.amount = juce::jlimit (-1.0f, 1.0f, rawValue (proc, modAmt (s)) * 0.01f);
        auto& old = slots[(size_t) s];
        if (old.source != r.source || old.dest != r.dest || ! juce::exactlyEqual (old.amount, r.amount))
        {
            old = r;
            changed = true;
        }
    }
    for (int l = 0; l < 4; ++l)
    {
        const bool uni = rawValue (proc, lfoParam (l, LfoParam::Polarity)) > 0.5f;
        if (uni != lfoUnipolar[(size_t) l])
        {
            lfoUnipolar[(size_t) l] = uni;
            changed = true;
        }
    }
    if (changed)
        ++ver;
    return changed;
}

std::vector<ModulationModel::Route> ModulationModel::routesFor (int destIndex) const
{
    std::vector<Route> out;
    if (destIndex <= 0)
        return out;
    for (const auto& r : slots)
        if (r.dest == destIndex && r.source != ModSource::None)
            out.push_back (r);
    return out;
}

ModulationModel::Route ModulationModel::find (int destIndex, ModSource source) const
{
    for (const auto& r : slots)
        if (r.dest == destIndex && r.source == source)
            return r;
    return {};
}

bool ModulationModel::isBipolar (ModSource s) const
{
    if (s >= ModSource::Lfo1 && s <= ModSource::Lfo4)
        return ! lfoUnipolar[(size_t) ((int) s - (int) ModSource::Lfo1)];
    return s == ModSource::PitchBend || s == ModSource::Note || s == ModSource::Spring
        || s == ModSource::Orbit || s == ModSource::Alive;
}

void ModulationModel::beginAmountGesture (int s)
{
    if (auto* p = parameterFor (proc, modAmt (s)))
        p->beginChangeGesture();
}

void ModulationModel::setAmountDuringGesture (int s, float amount)
{
    if (auto* p = parameterFor (proc, modAmt (s)))
        p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (-100.0f, 100.0f, amount * 100.0f)));
    refresh();
}

void ModulationModel::endAmountGesture (int s)
{
    if (auto* p = parameterFor (proc, modAmt (s)))
        p->endChangeGesture();
}

//==============================================================================
/** The rotary slider inside a ModKnob: routes right-click, alt-drag and depth editing to the knob and
    asks the knob for its tooltip. Shift while pressing gives fine control. */
class ModKnob::KnobSlider : public juce::Slider
{
public:
    explicit KnobSlider (ModKnob& o) : owner (o)
    {
        setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        setRotaryParameters (ModKnob::startAngle, ModKnob::endAngle, true);
        setScrollWheelEnabled (true);
        setMouseDragSensitivity (220);
    }

    juce::String getTooltip() override
    {
        return juce::String::fromUTF8 (owner.paramInfo.name) + ": " + owner.getValueText();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            owner.showMenu();
            return;
        }
        if (owner.wantsAmountDrag (e))
        {
            amountDragging = true;
            owner.amountDragStart (e);
            return;
        }
        setMouseDragSensitivity (e.mods.isShiftDown() ? 1200 : 220);
        owner.dragging = true;
        owner.repaint();
        juce::Slider::mouseDown (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (amountDragging) { owner.amountDrag (e); return; }
        if (! e.mods.isPopupMenu())
            juce::Slider::mouseDrag (e);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (amountDragging)
        {
            amountDragging = false;
            owner.amountDragEnd();
            return;
        }
        if (e.mods.isPopupMenu())
            return;
        juce::Slider::mouseUp (e);
        owner.dragging = false;
        owner.repaint();
    }

    void mouseEnter (const juce::MouseEvent& e) override { juce::Slider::mouseEnter (e); owner.repaint(); }
    void mouseExit (const juce::MouseEvent& e) override  { juce::Slider::mouseExit (e); owner.repaint(); }

private:
    ModKnob& owner;
    bool amountDragging = false;
};

//==============================================================================
ModKnob::ModKnob (Processor& p, ModulationModel& m, P prm, int d, const juce::String& l)
    : processor (p), model (m), param (prm), paramInfo (info (prm))
{
    init (d, l);
}

ModKnob::ModKnob (Processor& p, ModulationModel& m, const juce::String& paramId, int d, const juce::String& l)
    : processor (p), model (m), param ((P) juce::jmax (0, indexOf (paramId.toRawUTF8()))),
      paramInfo (info ((P) juce::jmax (0, indexOf (paramId.toRawUTF8()))))
{
    jassert (indexOf (paramId.toRawUTF8()) >= 0);
    init (d, l);
}

void ModKnob::init (int d, const juce::String& l)
{
    diameter = d;
    label = l.isNotEmpty() ? l : juce::String::fromUTF8 (paramInfo.shortName);
    dest = paramInfo.modulatable ? modDestinationIndexOf (param) : 0;
    parameter = parameterFor (processor, param);

    slider = std::make_unique<KnobSlider> (*this);
    addAndMakeVisible (*slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, paramInfo.id, *slider);
    slider->setDoubleClickReturnValue (true, (double) paramInfo.def);
    slider->onValueChange = [this] { repaint(); };
    slider->onDragEnd = [this] { dragging = false; repaint(); };

    refreshRoutes();
}

ModKnob::~ModKnob()
{
    popup.hide();
}

juce::Slider& ModKnob::getSlider() noexcept { return *slider; }

void ModKnob::setLabel (const juce::String& l)  { label = l; repaint(); }
void ModKnob::setDiameter (int d)                { diameter = d; resized(); repaint(); }
void ModKnob::setValueVisible (bool v)           { valueVisible = v; repaint(); }

void ModKnob::setKnobColour (juce::Colour c)
{
    knobColour = c;
    slider->getProperties().set ("knobColour", (juce::int64) c.getARGB());
    repaint();
}

juce::String ModKnob::getValueText() const
{
    if (parameter == nullptr || slider == nullptr)
        return {};
    if (paramInfo.kind == ParamKind::Int)
    {
        const int v = (int) std::lround (slider->getValue());
        juce::String t = (paramInfo.min < 0.0f && v > 0 ? "+" : "") + juce::String (v);
        const juce::String unit = juce::String::fromUTF8 (paramInfo.unit);
        if (unit.isNotEmpty()) t << " " << unit;
        return t;
    }
    return parameter->getText (parameter->convertTo0to1 ((float) slider->getValue()), 32);
}

juce::Rectangle<float> ModKnob::knobArea() const
{
    // The label / knob / value block sits at the bottom, so knobs of different sizes in one row share the value baseline.
    const float top = (float) (getHeight() - heightFor (diameter) + Cell::labelHeight + ringMargin);
    return { (float) getWidth() * 0.5f - (float) diameter * 0.5f, top, (float) diameter, (float) diameter };
}

juce::Rectangle<int> ModKnob::valueArea() const
{
    return getLocalBounds().removeFromBottom (Cell::valueHeight + 1);
}

float ModKnob::angleFor (float n) const noexcept
{
    return startAngle + juce::jlimit (0.0f, 1.0f, n) * (endAngle - startAngle);
}

void ModKnob::resized()
{
    slider->setBounds (knobArea().toNearestInt());
    if (valueEditor != nullptr)
        valueEditor->setBounds (valueArea().withSizeKeepingCentre (juce::jmin (getWidth(), 72), Cell::valueHeight + 2));
}

//==============================================================================
void ModKnob::paint (juce::Graphics& g)
{
    const bool enabled = isEnabled();
    const bool hover = slider->isMouseOverOrDragging();

    // Label.
    g.setColour (enabled ? col (palette::textDim) : col (palette::textFaint));
    g.setFont (font (Theme::fontSmall));
    g.drawFittedText (label, getLocalBounds().removeFromBottom (heightFor (diameter)).removeFromTop (Cell::labelHeight),
                      juce::Justification::centred, 1, 0.85f);

    // Value: plain text, a dark pill behind it on hover / drag.
    if (valueVisible && (valueEditor == nullptr || ! valueEditor->isVisible()))
    {
        const auto text = getValueText();
        const auto f = font (Theme::fontSmall);
        const auto area = valueArea();
        if (hover || dragging)
        {
            const float w = juce::jmin ((float) getWidth(), textWidth (f, text) + 12.0f);
            const auto pill = juce::Rectangle<float> (w, (float) Cell::valueHeight).withCentre (area.toFloat().getCentre());
            g.setColour (col (palette::background).withAlpha (0.9f));
            g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
            g.setColour (dragging ? knobColour.withAlpha (0.6f) : col (palette::panelEdge).brighter (0.2f));
            g.drawRoundedRectangle (pill.reduced (0.5f), pill.getHeight() * 0.5f, 1.0f);
        }
        g.setColour (! enabled ? col (palette::textFaint) : (dragging ? col (palette::text) : col (palette::text).withAlpha (0.86f)));
        g.setFont (f);
        g.drawFittedText (text, area, juce::Justification::centred, 1, 0.8f);
    }
}

void ModKnob::paintOverChildren (juce::Graphics& g)
{
    const auto k = knobArea();
    const auto c = k.getCentre();
    const float n = (float) slider->valueToProportionOfLength (slider->getValue());

    if (dragHover)
    {
        const float r = diameter * 0.5f + 3.5f;
        g.setColour (col (palette::accent).withAlpha (0.18f));
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
        g.setColour (col (palette::accent));
        juce::Path ring;
        ring.addEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
        juce::Path dashed;
        const float dashes[] = { 3.0f, 3.0f };
        juce::PathStrokeType (1.5f).createDashedStroke (dashed, ring, dashes, 2);
        g.fillPath (dashed);
    }

    if (routes.empty())
        return;

    // Rings: up to two radii outside the arc; extra routes share the outer one.
    for (size_t i = 0; i < routes.size(); ++i)
    {
        const auto& r = routes[i];
        const float radius = diameter * 0.5f + 2.5f + (float) juce::jmin<size_t> (i, 1) * 3.0f;
        const auto colour = modSourceColour (r.source);
        const bool editing = r.source == depthSource || r.slot == amountSlot;

        const float lo = model.isBipolar (r.source) ? n - std::abs (r.amount) : juce::jmin (n, n + r.amount);
        const float hi = model.isBipolar (r.source) ? n + std::abs (r.amount) : juce::jmax (n, n + r.amount);
        const float a0 = angleFor (lo), a1 = angleFor (hi);

        juce::Path track;
        track.addCentredArc (c.x, c.y, radius, radius, 0.0f, startAngle, endAngle, true);
        g.setColour (colour.withAlpha (0.12f));
        g.strokePath (track, juce::PathStrokeType (2.0f));

        if (a1 - a0 > 0.005f)
        {
            juce::Path arc;
            arc.addCentredArc (c.x, c.y, radius, radius, 0.0f, a0, a1, true);
            if (editing)
            {
                g.setColour (colour.withAlpha (0.3f));
                g.strokePath (arc, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            g.setColour (colour);
            g.strokePath (arc, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
        }
    }

    // Live dot at the modulated value, on the value arc.
    const float trackW = diameter >= 50 ? 3.5f : (diameter >= 38 ? 3.0f : 2.5f);
    const float arcR = diameter * 0.5f - trackW * 0.5f - 0.5f;
    const float a = angleFor (n + liveOffset) - juce::MathConstants<float>::halfPi;
    const juce::Point<float> p (c.x + arcR * std::cos (a), c.y + arcR * std::sin (a));
    const auto dotColour = modSourceColour (routes.front().source);
    g.setColour (dotColour.withAlpha (0.35f));
    g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (p));
    g.setColour (col (palette::text));
    g.fillEllipse (juce::Rectangle<float> (4.5f, 4.5f).withCentre (p));
}

//==============================================================================
void ModKnob::refreshRoutes()
{
    seenVersion = model.version();
    routes = model.routesFor (dest);
}

void ModKnob::tick()
{
    bool needsRepaint = false;
    if (seenVersion != model.version())
    {
        refreshRoutes();
        needsRepaint = true;
    }

    if (dest > 0 && ! routes.empty())
    {
        const float off = processor.engine().visuals().modOffset[dest].load (std::memory_order_relaxed);
        if (std::abs (off - liveOffset) > 0.002f)
        {
            liveOffset = off;
            needsRepaint = true;
        }
    }

    if (depthSource != ModSource::None && amountSlot < 0 && ++depthTicks > 30 * 6)
    {
        depthSource = ModSource::None;   // depth edit expires after ~6 s without a drag
        popup.hide();
        needsRepaint = true;
    }

    if (popupTicks > 0 && --popupTicks == 0 && amountSlot < 0)
        popup.hide();

    if (needsRepaint)
        repaint();
}

//==============================================================================
bool ModKnob::isInterestedInDragSource (const SourceDetails& d)
{
    return dest > 0 && ModSourceChip::sourceFromDescription (d.description) != ModSource::None;
}

void ModKnob::itemDragEnter (const SourceDetails&) { dragHover = true; repaint(); }
void ModKnob::itemDragExit (const SourceDetails&)  { dragHover = false; repaint(); }

void ModKnob::itemDropped (const SourceDetails& d)
{
    dragHover = false;
    const auto source = ModSourceChip::sourceFromDescription (d.description);
    if (source == ModSource::None || dest <= 0)
        return;

    if (model.find (dest, source).slot < 0)
    {
        if (! processor.setModulation (dest, source, 0.5f))
        {
            popup.show (*this, "All 24 matrix slots are in use", col (palette::warning));
            popupTicks = 60;
            repaint();
            return;
        }
        model.refresh();
    }
    beginDepthEdit (source);
}

void ModKnob::beginDepthEdit (ModSource source)
{
    refreshRoutes();
    depthSource = source;
    depthTicks = 0;
    const auto r = model.find (dest, source);
    if (r.slot >= 0)
    {
        popup.show (*this, modSourceDisplayName (source) + "  " + percent (r.amount) + "  - drag to set depth", modSourceColour (source));
        popupTicks = 0;
    }
    repaint();
}

bool ModKnob::wantsAmountDrag (const juce::MouseEvent& e)
{
    if (dest <= 0)
        return false;
    if (depthSource != ModSource::None && model.find (dest, depthSource).slot >= 0)
        return true;
    return e.mods.isAltDown() && ! routes.empty();
}

int ModKnob::routeIndexNear (juce::Point<float> p) const
{
    if (routes.empty())
        return -1;
    // Ring 0 sits just outside the arc, ring 1 (and all later routes) 3 px further out.
    const float d = p.getDistanceFrom (knobArea().getCentre()) - diameter * 0.5f;
    return d > 4.0f && routes.size() > 1 ? (int) routes.size() - 1 : 0;
}

void ModKnob::showRoutePopup (const ModulationModel::Route& r)
{
    popup.show (*this, modSourceDisplayName (r.source) + "  " + percent (r.amount), modSourceColour (r.source));
}

void ModKnob::amountDragStart (const juce::MouseEvent& e)
{
    ModulationModel::Route r;
    if (depthSource != ModSource::None)
        r = model.find (dest, depthSource);
    else
    {
        const int i = routeIndexNear (e.getEventRelativeTo (this).position);
        if (i >= 0) r = routes[(size_t) i];
    }
    if (r.slot < 0)
        return;

    amountSlot = r.slot;
    depthSource = r.source;
    amountStart = amountNow = r.amount;
    model.beginAmountGesture (amountSlot);
    showRoutePopup (r);
    repaint();
}

void ModKnob::amountDrag (const juce::MouseEvent& e)
{
    if (amountSlot < 0)
        return;
    const float scale = e.mods.isShiftDown() ? 600.0f : 150.0f;
    amountNow = juce::jlimit (-1.0f, 1.0f, amountStart - (float) e.getDistanceFromDragStartY() / scale);
    if (std::abs (amountNow) < 0.01f) amountNow = 0.0f;   // a small detent at zero
    model.setAmountDuringGesture (amountSlot, amountNow);
    refreshRoutes();
    auto r = model.slot (amountSlot);
    showRoutePopup (r);
    repaint();
}

void ModKnob::amountDragEnd()
{
    if (amountSlot >= 0)
    {
        model.endAmountGesture (amountSlot);
        const auto r = model.slot (amountSlot);
        if (juce::exactlyEqual (amountNow, 0.0f))
            processor.setModulation (dest, r.source, 0.0f);   // dragged to zero: free the slot
        model.refresh();
    }
    amountSlot = -1;
    depthSource = ModSource::None;
    popup.hide();
    refreshRoutes();
    repaint();
}

//==============================================================================
void ModKnob::showMenu()
{
    enum { idReset = 1, idEnter, idRemoveAll, idAddBase = 100, idRemoveBase = 200, idInvertBase = 300 };

    juce::PopupMenu menu;
    menu.addSectionHeader (juce::String::fromUTF8 (paramInfo.name));
    menu.addItem (idReset, "Reset to default");
    menu.addItem (idEnter, "Enter value...");

    if (dest > 0)
    {
        menu.addSeparator();
        juce::PopupMenu add;
        for (int s = 1; s < (int) ModSource::Count; ++s)
        {
            const auto src = (ModSource) s;
            add.addColouredItem (idAddBase + s, modSourceDisplayName (src), modSourceColour (src), true,
                                 model.find (dest, src).slot >= 0);
        }
        menu.addSubMenu ("Add modulation", add);

        for (const auto& r : routes)
        {
            juce::PopupMenu sub;
            sub.addItem (idInvertBase + r.slot, "Invert");
            sub.addItem (idRemoveBase + r.slot, "Remove");
            menu.addSubMenu (modSourceDisplayName (r.source) + juce::String::fromUTF8 (" \xe2\x86\x92 ") + percent (r.amount), sub);
        }
        if (! routes.empty())
            menu.addItem (idRemoveAll, "Remove all modulations");
    }

    menu.setLookAndFeel (&getLookAndFeel());
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (slider.get()),
        [safe = juce::Component::SafePointer<ModKnob> (this)] (int result)
        {
            if (safe == nullptr || result == 0)
                return;
            auto& k = *safe;
            if (result == idReset)
            {
                if (k.parameter != nullptr)
                {
                    k.parameter->beginChangeGesture();
                    k.parameter->setValueNotifyingHost (k.parameter->getDefaultValue());
                    k.parameter->endChangeGesture();
                }
            }
            else if (result == idEnter)
                k.showValueEditor();
            else if (result == idRemoveAll)
            {
                for (const auto& r : k.routes)
                    k.processor.setModulation (k.dest, r.source, 0.0f);
            }
            else if (result >= idInvertBase)
            {
                const auto r = k.model.slot (result - idInvertBase);
                k.processor.setModulation (k.dest, r.source, -r.amount);
            }
            else if (result >= idRemoveBase)
                k.processor.setModulation (k.dest, k.model.slot (result - idRemoveBase).source, 0.0f);
            else if (result >= idAddBase)
            {
                const auto src = (ModSource) (result - idAddBase);
                if (k.model.find (k.dest, src).slot < 0 && ! k.processor.setModulation (k.dest, src, 0.5f))
                {
                    k.popup.show (k, "All 24 matrix slots are in use", col (palette::warning));
                    k.popupTicks = 60;
                    return;
                }
                k.model.refresh();
                k.beginDepthEdit (src);
            }
            k.model.refresh();
            k.refreshRoutes();
            k.repaint();
        });
}

void ModKnob::showValueEditor()
{
    if (valueEditor == nullptr)
    {
        valueEditor = std::make_unique<juce::TextEditor>();
        valueEditor->setJustification (juce::Justification::centred);
        valueEditor->setFont (font (Theme::fontSmall));
        valueEditor->setIndents (2, 1);
        valueEditor->setBorder ({});
        valueEditor->onReturnKey = [this]
        {
            slider->setValue (slider->getValueFromText (valueEditor->getText()), juce::sendNotificationSync);
            valueEditor->setVisible (false);
            repaint();
        };
        valueEditor->onEscapeKey = [this] { valueEditor->setVisible (false); repaint(); };
        valueEditor->onFocusLost = [this] { valueEditor->setVisible (false); repaint(); };
        addChildComponent (*valueEditor);
    }
    resized();
    valueEditor->setText (getValueText(), false);
    valueEditor->setVisible (true);
    valueEditor->grabKeyboardFocus();
    valueEditor->selectAll();
    repaint();
}

//==============================================================================
std::unique_ptr<ModKnob> makeKnob (Processor& p, ModulationModel& m, Page& page, juce::Component& parent, P param,
                                   int diameter, const juce::String& label, juce::Colour colour)
{
    auto k = std::make_unique<ModKnob> (p, m, param, diameter, label);
    k->setKnobColour (colour);
    parent.addAndMakeVisible (*k);
    page.addTickable (k.get());
    return k;
}

std::unique_ptr<ChoiceButton> makeChoice (Processor& p, juce::Component& parent, P param, const juce::String& label, const juce::String& inlineLabel)
{
    auto c = std::make_unique<ChoiceButton> (p.apvts, juce::String (info (param).id), label);
    c->setInlineLabel (inlineLabel);
    parent.addAndMakeVisible (*c);
    return c;
}

} // namespace verdant::ui
