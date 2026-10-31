#pragma once

#include "LookAndFeel.h"
#include "ParamPanel.h"

#include <atomic>
#include <functional>

namespace ap::ui
{

/**
    Level meter reading a linear peak value from an atomic the audio thread
    writes to: segmented bar with a decaying peak hold, vertical or horizontal
    (whichever way the component is longer). Runs its own 30 Hz timer until an
    editor drives it (Animated::driveExternally).

    An optional marker (e.g. a gate threshold) is drawn as a line across the bar.
*/
class LevelMeter : public juce::Component, public Animated, private juce::Timer
{
public:
    explicit LevelMeter (std::atomic<float>& source, float minDb = -60.0f, float maxDb = 6.0f);
    ~LevelMeter() override;

    /** Draw a threshold line at the dB value returned by `getDb` (polled each tick). */
    void setMarker (std::function<float()> getDb) { marker = std::move (getDb); }

    void paint (juce::Graphics&) override;
    void tick() override;
    void driveExternally() override { stopTimer(); }

    float displayedDb() const noexcept { return displayed; }

private:
    void timerCallback() override { tick(); }

    std::atomic<float>& src;
    float minDb, maxDb;
    float displayed = -120.0f;
    float peakHold = -120.0f;
    int   peakAge = 0;
    std::function<float()> marker;
    float markerDb = -1000.0f;
};

/** Horizontal gain-reduction meter (0 dB on the right, reduction grows leftwards) with a read-out. */
class GainReductionMeter : public juce::Component, public Animated, private juce::Timer
{
public:
    explicit GainReductionMeter (std::atomic<float>& reductionDbSource, float rangeDb = 24.0f);
    ~GainReductionMeter() override;

    void paint (juce::Graphics&) override;
    void tick() override;
    void driveExternally() override { stopTimer(); }

private:
    void timerCallback() override { tick(); }

    std::atomic<float>& src;
    float range;
    float displayed = 0.0f;
};

/** Note name + cents offset (with a tuning needle) for a detected frequency. */
class PitchReadout : public juce::Component, public Animated, private juce::Timer
{
public:
    /** `hzSource` carries the detected frequency (0 = none); `targetSource` the corrected one (optional). */
    PitchReadout (std::atomic<float>& hzSource, std::atomic<float>* targetSource = nullptr);
    ~PitchReadout() override;

    void paint (juce::Graphics&) override;
    void tick() override;
    void driveExternally() override { stopTimer(); }

private:
    void timerCallback() override { tick(); }

    std::atomic<float>& hz;
    std::atomic<float>* target;
    float shownHz = 0.0f, shownTarget = 0.0f;
    float needle = 0.0f;   // smoothed cents
    int   holdFrames = 0;  // keeps the last note briefly after the pitch drops out
};

/** A small LED that flashes whenever a counter changes (e.g. MIDI events sent). */
class ActivityLed : public juce::Component, public Animated
{
public:
    ActivityLed (std::atomic<juce::uint32>& counter, juce::String label);

    void paint (juce::Graphics&) override;
    void tick() override;

private:
    std::atomic<juce::uint32>& source;
    juce::String text;
    juce::uint32 last = 0;
    float glow = 0.0f;
};

} // namespace ap::ui
