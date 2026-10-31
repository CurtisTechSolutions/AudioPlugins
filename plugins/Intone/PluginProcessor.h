#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <ap_plug/Programs.h>
#include <juce_dsp/juce_dsp.h>

#include <ap/Dynamics.h>
#include <ap/Filters.h>
#include <ap/PitchShifter.h>
#include <ap/PitchTracker.h>
#include <ap/Scales.h>

#include <array>
#include <atomic>

namespace intone
{

namespace ids
{
    inline constexpr const char* key        = "key";
    inline constexpr const char* scale      = "scale";
    inline constexpr const char* speed      = "speed";
    inline constexpr const char* amount     = "amount";
    inline constexpr const char* transpose  = "transpose";
    inline constexpr const char* tracking   = "tracking";
    inline constexpr const char* sensitivity = "sensitivity";
    inline constexpr const char* mix        = "mix";

    inline constexpr const char* deessAmount = "deess_amount";
    inline constexpr const char* deessFreq   = "deess_freq";
    inline constexpr const char* compThresh  = "comp_thresh";
    inline constexpr const char* compRatio   = "comp_ratio";
    inline constexpr const char* compAttack  = "comp_attack";
    inline constexpr const char* compRelease = "comp_release";
    inline constexpr const char* compMakeup  = "comp_makeup";

    inline constexpr const char* lowCut   = "low_cut";
    inline constexpr const char* body     = "body";
    inline constexpr const char* presence = "presence";
    inline constexpr const char* air      = "air";
    inline constexpr const char* output   = "output";

    /** Custom-scale note toggles, one per pitch class ("note_0" = C … "note_11" = B). */
    inline juce::String note (int pc) { return "note_" + juce::String (pc); }
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
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.1; }

    // Factory presets (Presets.cpp) are the program list.
    int getNumPrograms() override { return programs.getNumPrograms(); }
    int getCurrentProgram() override { return programs.getCurrentProgram(); }
    void setCurrentProgram (int index) override { programs.setCurrentProgram (index); }
    const juce::String getProgramName (int index) override { return programs.getProgramName (index); }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    ap::plug::Programs programs;

    std::atomic<float> detectedHz { 0.0f };
    std::atomic<float> targetHz { 0.0f };
    std::atomic<float> gainReductionDb { 0.0f };
    std::atomic<float> outputLevel { 0.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void updateParams();
    void updateChannelFilters (int ch);

    static constexpr int kMaxChannels = 2;
    static constexpr int kControlInterval = 32;

    std::array<ap::PitchTracker, 3> trackers;
    int activeTracker = 1;
    ap::PitchTimeline timeline;
    std::array<ap::PsolaShifter, kMaxChannels> shifters;
    std::array<juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None>, kMaxChannels> dryDelay;

    ap::Scale scale;
    float   smoothedTargetMidi = 0.0f;
    bool    haveTarget = false;
    float   retuneCoeff = 0.0f;
    int     controlCounter = 0;

    struct ChannelChain
    {
        ap::Biquad lowCut, deessSplit, body, presence, air;
        ap::EnvelopeFollower deessEnv, compEnv;
    };
    std::array<ChannelChain, kMaxChannels> chains;
    ap::GainComputer compGc, deessGc;
    ap::Smoother outputSmoother, mixSmoother;

    double sr = 44100.0;
    int    latency = 0;

    struct Raw
    {
        std::atomic<float> *key, *scaleType, *speed, *amount, *transpose, *tracking, *sensitivity, *mix,
                           *deessAmount, *deessFreq, *compThresh, *compRatio, *compAttack, *compRelease, *compMakeup,
                           *lowCut, *body, *presence, *air, *output;
        std::array<std::atomic<float>*, 12> notes {};
    } prm {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};

} // namespace intone
