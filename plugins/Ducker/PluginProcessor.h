#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <ap/Dynamics.h>
#include <ap/Filters.h>
#include <ap/Tempo.h>

#include <array>
#include <atomic>

namespace ducker
{

namespace ids
{
    inline constexpr const char* mode      = "mode";
    inline constexpr const char* threshold = "threshold";
    inline constexpr const char* ratio     = "ratio";
    inline constexpr const char* knee      = "knee";
    inline constexpr const char* attack    = "attack";
    inline constexpr const char* release   = "release";
    inline constexpr const char* lookahead = "lookahead";
    inline constexpr const char* scHpf     = "sc_hpf";
    inline constexpr const char* listen    = "listen";
    inline constexpr const char* makeup    = "makeup";
    inline constexpr const char* mix       = "mix";

    inline constexpr const char* ghostDiv     = "ghost_div";
    inline constexpr const char* ghostDepth   = "ghost_depth";
    inline constexpr const char* ghostAttack  = "ghost_attack";
    inline constexpr const char* ghostHold    = "ghost_hold";
    inline constexpr const char* ghostRelease = "ghost_release";
    inline constexpr const char* ghostCurve   = "ghost_curve";
    inline constexpr const char* ghostOffset  = "ghost_offset";
    inline constexpr const char* ghostBpm     = "ghost_bpm";
}

enum class Mode : int { External = 0, Internal, Ghost };

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
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float> gainReductionDb { 0.0f };
    std::atomic<float> sidechainLevel { 0.0f };
    std::atomic<bool>  sidechainConnected { false };

    // For the editor's history display: the most gain reduction (dB, <= 0) and the main
    // input peak (linear) since the editor last took them (it swaps in 0).
    std::atomic<float> grHoldDb { 0.0f };
    std::atomic<float> inputHold { 0.0f };
    // For the ghost display: the tempo in use and where in the division the output is (0..1).
    std::atomic<float> bpmInUse { 120.0f };
    std::atomic<float> ghostPhase { 0.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void updateParams();

    static constexpr int kMaxChannels = 2;
    static constexpr float kMaxLookaheadMs = 10.0f;

    ap::EnvelopeFollower detector;
    ap::GainComputer gc;
    std::array<ap::Biquad, kMaxChannels> scFilters;
    std::array<juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None>, kMaxChannels> lookaheadDelay;
    ap::Smoother gainSmoother, mixSmoother, makeupSmoother;
    ap::GhostDuckEnvelope ghost;

    double sr = 44100.0;
    int    lookaheadSamples = 0;
    double freeRunBeats = 0.0;    // ghost mode phase when the host transport is stopped

    struct Raw
    {
        std::atomic<float> *mode, *threshold, *ratio, *knee, *attack, *release, *lookahead, *scHpf, *listen, *makeup, *mix,
                           *ghostDiv, *ghostDepth, *ghostAttack, *ghostHold, *ghostRelease, *ghostCurve, *ghostOffset, *ghostBpm;
    } prm {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};

} // namespace ducker
