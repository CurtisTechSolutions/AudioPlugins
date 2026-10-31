#pragma once

#include "LookAndFeel.h"
#include "ParamPanel.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <functional>
#include <vector>

namespace ap::ui
{

/**
    Base for visualisers: draws a recessed well with an optional caption and
    hands the inner area to paintDisplay(). tick() repaints by default;
    override it to pull new data first.
*/
class Display : public juce::Component, public Animated
{
public:
    explicit Display (juce::String caption = {}) : captionText (std::move (caption)) {}

    void paint (juce::Graphics&) final;
    void tick() override { repaint(); }

    void setCaption (juce::String c) { captionText = std::move (c); repaint(); }

protected:
    virtual void paintDisplay (juce::Graphics&, juce::Rectangle<float> area) = 0;

    /** Horizontal / vertical grid lines in the display style. */
    static void gridLine (juce::Graphics&, const Theme&, float x0, float y0, float x1, float y1, bool strong = false);

private:
    juce::String captionText;
};

/** Fixed-size ring of floats for scrolling histories (the newest value is at index size()-1). */
class History
{
public:
    explicit History (size_t length = 256, float initial = 0.0f) : data (length, initial) {}

    void push (float v) noexcept { data[head] = v; head = (head + 1) % data.size(); }
    size_t size() const noexcept { return data.size(); }
    float operator[] (size_t i) const noexcept { return data[(head + i) % data.size()]; }
    void fill (float v) { std::fill (data.begin(), data.end(), v); }

private:
    std::vector<float> data;
    size_t head = 0;
};

/** ADSR envelope drawn from four parameters (attack / decay / release in ms, sustain in %). */
class AdsrDisplay : public Display
{
public:
    AdsrDisplay (juce::AudioProcessorValueTreeState&, const char* attackId, const char* decayId,
                 const char* sustainId, const char* releaseId, juce::String caption = {});
    void tick() override;

protected:
    void paintDisplay (juce::Graphics&, juce::Rectangle<float>) override;

private:
    std::atomic<float> *a, *d, *s, *r;
    float shown[4] {};
};

/**
    A frequency-response style plot: x is log frequency (20 Hz–20 kHz), y is dB
    in [minDb, maxDb]. The curve comes from `response(hz) -> dB`, re-evaluated
    whenever `signature()` changes (e.g. a hash of the parameters it depends on).
*/
class ResponseDisplay : public Display
{
public:
    ResponseDisplay (std::function<float (float hz)> response, std::function<float()> signature,
                     float minDb = -24.0f, float maxDb = 24.0f, juce::String caption = {});
    void tick() override;

    /** Marks a frequency with a dot on the curve (e.g. the cutoff); return <= 0 for none. */
    void setMarker (std::function<float()> hz) { markerHz = std::move (hz); }

protected:
    void paintDisplay (juce::Graphics&, juce::Rectangle<float>) override;

private:
    std::function<float (float)> response;
    std::function<float()> signature, markerHz;
    float minDb, maxDb;
    float lastSignature = -1.0e30f;
    std::vector<float> curve;   // dB per x pixel column (128 points)
};

/**
    Piano-roll background for pitch displays: a vertical keyboard in `keys`
    and one row per semitone across `plot`, for MIDI notes `lo`..`hi`
    (fractional; `lo` at the bottom). `rowKind(note)` returns 0 for a plain
    row, 1 for a row in the active scale, 2 for the scale root, or -1 for a
    row outside an active scale (darkened). `litNote` lights its key.
*/
void drawPitchGrid (juce::Graphics&, const Theme&, juce::Rectangle<float> keys, juce::Rectangle<float> plot,
                    float lo, float hi, const std::function<int (int note)>& rowKind, int litNote);

/** "C4"-style name of a MIDI note. */
juce::String midiNoteText (int note);

/**
    A clickable keyboard strip. Pressing / dragging calls onNote; keys that
    `isHeld` reports (e.g. notes coming from the host) light up too.
*/
class Keyboard : public juce::Component, public Animated
{
public:
    Keyboard (int lowestNote = 36, int numOctaves = 4);

    std::function<void (int note, bool on)> onNote;
    std::function<bool (int note)> isHeld;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void tick() override;

private:
    int noteAt (juce::Point<float>) const;
    juce::Rectangle<float> keyBounds (int note) const;
    void press (int note);

    int lowest, count;
    int mouseNote = -1;
    std::array<bool, 128> shown {};
};

} // namespace ap::ui
