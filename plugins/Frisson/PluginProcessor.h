#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <ap_plug/Programs.h>

#include <ap/DelayLine.h>
#include <ap/Filters.h>
#include <ap/Loudness.h>

#include "modules/Module.h"
#include "modules/Weight.h"
#include "modules/Warmth.h"
#include "modules/Focus.h"
#include "modules/Groove.h"
#include "modules/Shine.h"
#include "modules/Space.h"
#include "modules/Rush.h"
#include "modules/Chills.h"
#include "modules/Life.h"

#include <array>
#include <atomic>
#include <vector>

namespace frisson
{

namespace ids
{
    inline constexpr const char* dose    = "dose";
    inline constexpr const char* weight  = "weight";
    inline constexpr const char* warmth  = "warmth";
    inline constexpr const char* focus   = "focus";
    inline constexpr const char* groove  = "groove";
    inline constexpr const char* shine   = "shine";
    inline constexpr const char* space   = "space";
    inline constexpr const char* rush    = "rush";
    inline constexpr const char* chills  = "chills";
    inline constexpr const char* life    = "life";
    inline constexpr const char* match   = "match";
    inline constexpr const char* output  = "output";

    inline constexpr const char* chillsSense   = "chills_sense";
    inline constexpr const char* groovePocket  = "groove_pocket";
    inline constexpr const char* lifeRate      = "life_rate";
    inline constexpr const char* spaceMonoHz   = "space_mono_hz";
    inline constexpr const char* spaceRoom     = "space_room";
    inline constexpr const char* rushCeiling   = "rush_ceiling";
    inline constexpr const char* rushAutoGain  = "rush_autogain";
}

/** Knob order used by the editor grid and the activity array. */
enum Knob : int { kWeight = 0, kWarmth, kRush, kFocus, kShine, kSpace, kGroove, kChills, kLife, kNumKnobs };

inline constexpr const char* knobParamId (int knob) noexcept
{
    switch (knob)
    {
        case kWeight: return ids::weight;  case kWarmth: return ids::warmth; case kRush:   return ids::rush;
        case kFocus:  return ids::focus;   case kShine:  return ids::shine;  case kSpace:  return ids::space;
        case kGroove: return ids::groove;  case kChills: return ids::chills; case kLife:   return ids::life;
        default:      return ids::dose;
    }
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
    double getTailLengthSeconds() const override { return 0.05; }

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

    // --- for the editor
    std::array<std::atomic<float>, kNumKnobs> activity {};
    std::atomic<float> dryLufs { -120.0f };
    std::atomic<float> wetLufs { -120.0f };
    std::atomic<float> loudnessDeltaDb { 0.0f };   // wet - dry, short term
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<float> matchGainDb { 0.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void updateModuleSettings();

    WeightModule weight;
    WarmthModule warmth;
    FocusModule  focus;
    GrooveModule groove;
    ShineModule  shine;
    SpaceModule  space;
    RushModule   rush;
    ChillsModulator chills;
    LifeModulator   life;

    std::array<Module*, 7> chain { &weight, &warmth, &focus, &groove, &shine, &space, &rush };

    std::array<ap::DelayLine, 2> dryDelay;
    std::vector<float> dryL, dryR, scratchR;
    ap::LoudnessMeter dryMeter, wetMeter;
    ap::Smoother doseSmoother, outputSmoother, gainLSmoother, gainRSmoother, matchSmoother;

    double sr = 48000.0;
    int    latency = 0;
    int    maxBlock = 512;

    struct Raw
    {
        std::atomic<float> *dose, *weight, *warmth, *focus, *groove, *shine, *space, *rush, *chills, *life, *match, *output,
                           *chillsSense, *groovePocket, *lifeRate, *spaceMonoHz, *spaceRoom, *rushCeiling, *rushAutoGain;
    } prm {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};

} // namespace frisson
