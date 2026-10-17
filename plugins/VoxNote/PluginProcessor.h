#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <ap/NoteTracker.h>
#include <ap/Oscillator.h>
#include <ap/PitchTracker.h>

#include <array>
#include <atomic>

namespace voxnote
{

namespace ids
{
    inline constexpr const char* tracking   = "tracking";
    inline constexpr const char* gate       = "gate";
    inline constexpr const char* sensitivity = "sensitivity";
    inline constexpr const char* onset      = "onset";
    inline constexpr const char* release    = "release";
    inline constexpr const char* retrigger  = "retrigger";
    inline constexpr const char* transpose  = "transpose";
    inline constexpr const char* snap       = "snap";
    inline constexpr const char* key        = "key";
    inline constexpr const char* scale      = "scale";
    inline constexpr const char* bend       = "bend";
    inline constexpr const char* bendRange  = "bend_range";
    inline constexpr const char* velDynamic = "vel_dynamic";
    inline constexpr const char* velFixed   = "vel_fixed";
    inline constexpr const char* velMin     = "vel_min";
    inline constexpr const char* velMax     = "vel_max";
    inline constexpr const char* channel    = "channel";
    inline constexpr const char* audioThru  = "audio_thru";
    inline constexpr const char* monitor    = "monitor";
    inline constexpr const char* monitorLevel = "monitor_level";
}

class Processor : public juce::AudioProcessor
{
public:
    Processor();
    ~Processor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // For the editor.
    std::atomic<float> detectedHz { 0.0f };
    std::atomic<float> inputLevel { 0.0f };
    std::atomic<int>   currentNote { -1 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void updateTrackerParams();

    // Three trackers tuned for different ranges; only one is fed at a time so
    // switching never allocates on the audio thread.
    std::array<ap::PitchTracker, 3> trackers;
    int activeTracker = 1;
    ap::NoteTracker noteTracker;

    // Monitor synth: a simple sine voice so the detector can be auditioned
    // without routing MIDI anywhere.
    ap::PolyBlepOscillator monitorOsc;
    float monitorEnv = 0.0f, monitorTarget = 0.0f, monitorBend = 0.0f;
    double sr = 44100.0;
    std::atomic<bool> pendingAllNotesOff { false };

    struct Raw
    {
        std::atomic<float> *tracking, *gate, *sensitivity, *onset, *release, *retrigger, *transpose, *snap, *key, *scale,
                           *bend, *bendRange, *velDynamic, *velFixed, *velMin, *velMax, *channel, *audioThru, *monitor, *monitorLevel;
    } prm {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};

} // namespace voxnote
