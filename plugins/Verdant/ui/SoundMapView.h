#pragma once

// SoundMapView: the presets on a 2D map by sonic similarity (CONTRACT §7).
// x rises with brightness, y with attack time (BackgroundTasks::buildSoundMap);
// points are coloured by category (legend along the bottom).
//
//   hover        name label; after 300 ms (audition on) the preset is previewed: loaded and played
//                as a 400 ms C3 (MIDI 48) through Processor::auditionNote. Leaving the map without
//                clicking restores what was loaded before the preview.
//   click        loads the preset (keeps it)
//   drag A -> B  draws a line; releasing on B opens a blend handle on the line (t = 50 %). Dragging
//                the handle applies blendPatches (A, B, t) live (at most 20 Hz, keepName); releasing
//                applies it once more with its blended name. Click empty space / Esc closes it.
//
// Build progress and the empty state are drawn over the map by the view itself.

#include "Widgets.h"

#include "engine/Patch.h"
#include "engine/SoundMap.h"

#include <functional>
#include <vector>

namespace verdant
{
class Processor;
}

namespace verdant::ui
{

class SoundMapView : public juce::Component, public Tickable
{
public:
    explicit SoundMapView (Processor&);
    ~SoundMapView() override;

    void setPoints (std::vector<SoundMapPoint>);
    bool hasPoints() const noexcept { return ! points.empty(); }
    const std::vector<SoundMapPoint>& getPoints() const noexcept { return points; }

    /** progress < 0 = not building. */
    void setBuildProgress (float progress);
    void setAuditionEnabled (bool);
    /** A point to ring (e.g. the list's selection); -1 = none. */
    void setHighlightedEntry (int entryIndex);

    /** Called after a click loads a preset. */
    std::function<void (int entryIndex)> onLoaded;

    /** Ends a hover preview, restoring the sound that was loaded before it. */
    void endPreview (bool restore);
    void closeBlend();

    void tick() override;
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void resized() override;

private:
    juce::Rectangle<float> mapArea() const;
    void updateLayout();
    juce::StringArray legendCategories() const;
    std::vector<std::pair<juce::String, juce::Rectangle<float>>> legendLayout() const;
    juce::Point<float> position (const SoundMapPoint&) const;
    int pointAt (juce::Point<float>, float radius = 9.0f) const;   // index into points
    juce::String nameOf (int pointIndex) const;
    juce::Colour colourOf (int pointIndex) const;
    void startBlend (int pa, int pb);
    void applyBlend (bool final);
    float tOnLine (juce::Point<float>) const;

    Processor& processor;
    std::vector<SoundMapPoint> points;
    std::vector<std::pair<juce::String, juce::Rectangle<float>>> legend;
    juce::Rectangle<float> map;
    float buildProgress = -1.0f;
    bool auditionEnabled = true;
    int highlighted = -1;

    int hover = -1;
    juce::int64 hoverSince = 0;
    bool hoverAuditioned = false;

    // preview session
    bool previewing = false;
    int restoreIndex = -1;           // entry to reload on restore, or -1 = restorePatch
    Patch restorePatch;

    // drag / blend
    int pressPoint = -1;
    bool draggingLine = false, draggingHandle = false;
    juce::Point<float> dragPos;
    int blendA = -1, blendB = -1;
    float blendT = 0.5f;
    bool blendDirty = false;
    juce::int64 lastBlendApply = 0;
    Patch patchA, patchB;
};

} // namespace verdant::ui
