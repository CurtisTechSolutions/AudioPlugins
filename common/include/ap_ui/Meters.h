#pragma once

#include "LookAndFeel.h"

#include <atomic>

namespace ap::ui
{

/**
    Vertical level meter reading a linear peak value from an atomic the audio
    thread writes to. Shows a decaying peak with a dB scale.
*/
class LevelMeter : public juce::Component, private juce::Timer
{
public:
    explicit LevelMeter (std::atomic<float>& source, float minDb = -60.0f, float maxDb = 6.0f);
    ~LevelMeter() override;

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    std::atomic<float>& src;
    float minDb, maxDb;
    float displayed = -120.0f;
    float peakHold = -120.0f;
    int   peakAge = 0;
};

/** Horizontal gain-reduction meter (0 dB on the right, reduction grows leftwards). */
class GainReductionMeter : public juce::Component, private juce::Timer
{
public:
    explicit GainReductionMeter (std::atomic<float>& reductionDbSource, float rangeDb = 24.0f);
    ~GainReductionMeter() override;

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    std::atomic<float>& src;
    float range;
    float displayed = 0.0f;
};

/** Shows the note name + cents offset for a detected frequency. */
class PitchReadout : public juce::Component, private juce::Timer
{
public:
    /** `hzSource` carries the detected frequency (0 = none); `targetSource` the corrected one (optional). */
    PitchReadout (std::atomic<float>& hzSource, std::atomic<float>* targetSource = nullptr);
    ~PitchReadout() override;

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    std::atomic<float>& hz;
    std::atomic<float>* target;
    float shownHz = 0.0f, shownTarget = 0.0f;
};

} // namespace ap::ui
