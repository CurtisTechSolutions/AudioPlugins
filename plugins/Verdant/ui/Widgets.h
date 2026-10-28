#pragma once

// Small shared Verdant controls (reused by every page):
//
//   SectionPanel      titled panel frame; children are laid out in contentArea(), header widgets in headerArea()
//   ChoiceButton      a Choice / Int / Bool parameter (or a plain item list) as a button that opens a menu;
//                     same cell layout as ModKnob when it has a label
//   TogglePill        a Bool parameter as a pill switch, a power button or a text pill
//   SegmentedControl  a row of mutually exclusive segments (page tabs, ENV 1/2/3, Serial / Parallel ...)
//   IconButton        a small vector-icon button (chevrons, lock, re-roll, brush, import, ...)
//   ValuePopup        a floating value read-out shown above a component while it is dragged
//   layout::*         grid helpers for knob cells
//
// Every bound control uses juce::ParameterAttachment, so host automation, undo
// gestures and preset loads stay in sync both ways.

#include "Pages.h"
#include "Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <vector>
#include <memory>

namespace verdant::ui
{

/** Knob-cell geometry shared by ModKnob and the labelled ChoiceButton / TogglePill, so mixed rows line up. */
struct Cell
{
    static constexpr int width = 60;         // default cell width
    static constexpr int height = 72;        // label + 40 px knob + rings + value
    static constexpr int labelHeight = 13;   // label row at the top
    static constexpr int valueHeight = 13;   // value row at the bottom

    /** The area between the label and value rows (where the knob / button sits). */
    static juce::Rectangle<int> middle (juce::Rectangle<int> cell)
    {
        return cell.withTrimmedTop (labelHeight).withTrimmedBottom (valueHeight);
    }
};

//==============================================================================
/** Draws the panel frame used by SectionPanel (for pages that paint panels themselves). */
void paintSection (juce::Graphics&, juce::Rectangle<int> bounds, const juce::String& title,
                   juce::Colour marker = juce::Colour (palette::accent), bool active = true);

/** Background of a hero display (wavetable, filter curve, envelope, LFO, physics): well gradient,
    a faint inner glow of `accent` and a 1 px lighter edge. */
void paintDisplay (juce::Graphics&, juce::Rectangle<float> bounds, juce::Colour accent = juce::Colour (palette::accent));

/** A titled panel: 30 px title bar (marker + upper-case title), 12 px padding. Add children and place
    them in contentArea(); header widgets go in headerArea() (the title bar right of the title). */
class SectionPanel : public juce::Component
{
public:
    explicit SectionPanel (const juce::String& title = {}, juce::Colour marker = juce::Colour (palette::accent));

    void setTitle (const juce::String&);
    const juce::String& getTitle() const noexcept { return title; }
    void setMarkerColour (juce::Colour);
    /** Dims the title (e.g. an oscillator that is switched off). */
    void setActive (bool);

    /** Sub-group captions inside the card: small dim caps text followed by a hairline across `line`. */
    void clearCaptions();
    void addCaption (const juce::String& text, juce::Rectangle<int> line);
    /** Vertical hairline dividers (e.g. between Sub and Noise). */
    void addDivider (juce::Rectangle<int> line);

    juce::Rectangle<int> headerArea() const;    // title bar minus the title, 12 px side padding
    juce::Rectangle<int> contentArea() const;   // below the title bar, 12 px padding (8 px top)

    static constexpr int padding = Theme::spaceM;

    void paint (juce::Graphics&) override;

private:
    juce::String title;
    juce::Colour marker;
    bool active = true;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> captions;
    std::vector<juce::Rectangle<int>> dividers;
};

//==============================================================================
/** A choice as a button: shows the current item and opens a menu on click (mouse wheel steps).
    Bound to a Choice / Int / Bool parameter, or unbound with a plain item list. With a label it lays
    out like a knob cell (label above, button in the middle); without one the button fills the bounds. */
class ChoiceButton : public juce::Component, public juce::SettableTooltipClient
{
public:
    ChoiceButton (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& label = {});
    ChoiceButton (const juce::StringArray& items, const juce::String& label = {});
    ~ChoiceButton() override;

    void setLabel (const juce::String&);
    void setAccentColour (juce::Colour);          // text colour of the current item (default: text)
    /** A dim caption drawn inside the button on the left ("Wave"), the value right-aligned after it. */
    void setInlineLabel (const juce::String&);
    /** Ghost style: no box until hovered (empty matrix rows, dense lists). */
    void setGhost (bool);
    void setItems (const juce::StringArray&);     // unbound only

    int getIndex() const noexcept { return index; }
    void setIndex (int, juce::NotificationType = juce::sendNotificationSync);
    juce::String getText() const;

    /** Optional: build a custom (e.g. grouped) menu. Item ids must be index + 1. */
    std::function<juce::PopupMenu (int currentIndex)> menuBuilder;
    /** Optional: the text shown for an index (default: the item). */
    std::function<juce::String (int index)> textForIndex;
    std::function<void (int index)> onChange;

    juce::Rectangle<int> buttonBounds() const;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    void showMenu();
    void choose (int newIndex);

    juce::StringArray items;
    juce::String label, inlineLabel;
    juce::Colour accent { palette::text };
    bool ghost = false;
    int index = 0;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChoiceButton)
};

//==============================================================================
/** A Bool parameter (or a plain state) as a pill switch, a round power button, or a text pill. */
class TogglePill : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum class Style { Switch, Power, Text };

    TogglePill (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& text = {}, Style = Style::Switch);
    explicit TogglePill (const juce::String& text = {}, Style = Style::Switch);
    ~TogglePill() override;

    bool getState() const noexcept { return state; }
    void setState (bool, juce::NotificationType = juce::sendNotificationSync);
    void setText (const juce::String&);
    /** Shows `label` above the control, knob-cell style. */
    void setLabel (const juce::String&);
    void setAccentColour (juce::Colour);

    std::function<void (bool)> onChange;

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    juce::String text, label;
    Style style;
    juce::Colour accent { palette::accent };
    bool state = false;
    std::unique_ptr<juce::ParameterAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TogglePill)
};

//==============================================================================
/** Mutually exclusive segments in one rounded frame. Optionally bound to a Choice parameter. */
class SegmentedControl : public juce::Component
{
public:
    explicit SegmentedControl (const juce::StringArray& items = {});
    ~SegmentedControl() override;

    void setItems (const juce::StringArray&);
    void bindToParameter (juce::AudioProcessorValueTreeState&, const juce::String& paramId);
    void setSelected (int, juce::NotificationType = juce::sendNotificationSync);
    int getSelected() const noexcept { return selected; }
    void setAccentColour (juce::Colour);
    void setFontSize (float);
    /** Selected segment filled with the accent (dark text) instead of outlined; used by the page tabs. */
    void setFilledSelection (bool);
    /** Optional per-segment colour dot drawn left of the text (e.g. live source activity); alpha 0 = none. */
    void setSegmentDot (int segment, juce::Colour);

    std::function<void (int)> onChange;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    int segmentAt (juce::Point<int>) const;
    juce::Rectangle<float> segmentBounds (int) const;

    juce::StringArray items;
    juce::Array<juce::Colour> dots;
    int selected = 0, hover = -1;
    float fontSize = Theme::fontBody;
    bool filled = false;
    juce::Colour accent { palette::accent };
    std::unique_ptr<juce::ParameterAttachment> attachment;
    juce::RangedAudioParameter* param = nullptr;
};

//==============================================================================
/** A small button that draws a vector icon (plus optional text) in the Verdant style. */
class IconButton : public juce::Button
{
public:
    enum class Icon { None, ChevronLeft, ChevronRight, ChevronDown, Lock, Unlock, Reroll, Brush, Import, Power, Close, Plus, Save, Dice };

    explicit IconButton (Icon, const juce::String& tooltip = {}, const juce::String& buttonText = {});

    void setIcon (Icon);
    /** Frameless: no background until hovered (used inside rows and panels). */
    void setFrameless (bool);
    void setIconColour (juce::Colour);

    static void drawIcon (juce::Graphics&, Icon, juce::Rectangle<float> area, juce::Colour, float strokeWidth = 1.5f);

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    Icon icon;
    bool frameless = false;
    juce::Colour iconColour { palette::text };
};

//==============================================================================
/** A floating read-out ("12.4 kHz", "LFO 1 -> 45 %") placed above an anchor, inside the anchor's Page so it
    scales with the editor. Owned by the anchor; show() / hide() add and remove it. */
class ValuePopup : public juce::Component
{
public:
    ValuePopup();
    ~ValuePopup() override;

    void show (juce::Component& anchor, const juce::String& text, juce::Colour accent = juce::Colour (palette::accent));
    void hide();

    void paint (juce::Graphics&) override;

private:
    juce::String text;
    juce::Colour accent;
};

//==============================================================================
namespace layout
{
    /** Places components left to right in `cellWidth` wide cells (nullptr leaves a gap). */
    void row (std::initializer_list<juce::Component*>, juce::Rectangle<int> area, int cellWidth, int gap = 0);

    /** Spreads components evenly over the full width of `area`. */
    void spread (std::initializer_list<juce::Component*>, juce::Rectangle<int> area);

    /** Grid of `columns` equal cells per row, rows of `rowHeight` separated by `rowGap`; nullptr leaves a gap. */
    void grid (std::initializer_list<juce::Component*>, juce::Rectangle<int> area, int columns, int rowHeight, int rowGap = 0);
}

} // namespace verdant::ui
