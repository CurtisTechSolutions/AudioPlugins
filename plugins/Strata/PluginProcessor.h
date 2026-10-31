#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <ap/SpscQueue.h>

#include <array>
#include <atomic>
#include <juce_dsp/juce_dsp.h>

#include "SynthVoice.h"

#include <atomic>

namespace strata
{

namespace ids
{
    // Oscillator A
    inline constexpr const char* aWave = "a_wave";   inline constexpr const char* aOct = "a_oct";
    inline constexpr const char* aSemi = "a_semi";   inline constexpr const char* aFine = "a_fine";
    inline constexpr const char* aLevel = "a_level"; inline constexpr const char* aPw = "a_pw";
    inline constexpr const char* uniVoices = "uni_voices"; inline constexpr const char* uniDetune = "uni_detune";
    inline constexpr const char* uniSpread = "uni_spread";
    // Oscillator B
    inline constexpr const char* bWave = "b_wave";   inline constexpr const char* bOct = "b_oct";
    inline constexpr const char* bSemi = "b_semi";   inline constexpr const char* bFine = "b_fine";
    inline constexpr const char* bLevel = "b_level"; inline constexpr const char* bPw = "b_pw";
    // Sub & noise
    inline constexpr const char* subLevel = "sub_level"; inline constexpr const char* subWave = "sub_wave";
    inline constexpr const char* subOct = "sub_oct";     inline constexpr const char* noiseLevel = "noise_level";
    // Filter
    inline constexpr const char* fType = "f_type";   inline constexpr const char* fCutoff = "f_cutoff";
    inline constexpr const char* fRes = "f_res";     inline constexpr const char* fEnvAmt = "f_env";
    inline constexpr const char* fKey = "f_key";     inline constexpr const char* fLfo = "f_lfo";
    inline constexpr const char* fDrive = "f_drive";
    // Envelopes
    inline constexpr const char* ampA = "amp_a"; inline constexpr const char* ampD = "amp_d";
    inline constexpr const char* ampS = "amp_s"; inline constexpr const char* ampR = "amp_r";
    inline constexpr const char* fenvA = "fenv_a"; inline constexpr const char* fenvD = "fenv_d";
    inline constexpr const char* fenvS = "fenv_s"; inline constexpr const char* fenvR = "fenv_r";
    // LFOs
    inline constexpr const char* lfo1Shape = "lfo1_shape"; inline constexpr const char* lfo1Rate = "lfo1_rate";
    inline constexpr const char* lfo1Depth = "lfo1_depth"; inline constexpr const char* lfo1Fade = "lfo1_fade";
    inline constexpr const char* lfo2Shape = "lfo2_shape"; inline constexpr const char* lfo2Rate = "lfo2_rate";
    // Global
    inline constexpr const char* voices = "voices";   inline constexpr const char* glide = "glide";
    inline constexpr const char* velAmp = "vel_amp";  inline constexpr const char* velFilter = "vel_filter";
    inline constexpr const char* bendRange = "bend_range"; inline constexpr const char* master = "master";
    // Effects
    inline constexpr const char* chorusMix = "chorus_mix"; inline constexpr const char* chorusRate = "chorus_rate";
    inline constexpr const char* delayTime = "delay_time"; inline constexpr const char* delayFb = "delay_fb";
    inline constexpr const char* delayMix = "delay_mix";
    inline constexpr const char* reverbSize = "reverb_size"; inline constexpr const char* reverbMix = "reverb_mix";
}

class Processor : public juce::AudioProcessor
{
public:
    Processor();
    ~Processor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }   // release max 8 s + reverb

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float> outputLevel { 0.0f };

    /** The editor's keyboard (message thread): queued, played at the start of the next block on channel 1. */
    void keyboardNote (int note, bool on) noexcept
    {
        keyboardEvents.push ((juce::uint16) ((on ? 0x80 : 0) | juce::jlimit (0, 127, note)));
    }

    /** Whether a note is held right now (from the host or the editor's keyboard). */
    bool isNoteHeld (int note) const noexcept
    {
        return note >= 0 && note < 128 && ((held[(size_t) note >> 5].load (std::memory_order_relaxed) >> (note & 31)) & 1u) != 0;
    }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void updateSettings();

    ap::SpscQueue<juce::uint16, 256> keyboardEvents;
    std::array<std::atomic<juce::uint32>, 4> held {};

    Settings settings;
    PolySynth synth;
    float lastNoteHz = 0.0f;
    bool  delayTailActive = false;
    int   reverbTailBlocks = 0;      // keeps the reverb running after its mix drops to 0

    juce::dsp::Chorus<float> chorus;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayL { 192000 }, delayR { 192000 };
    juce::dsp::Reverb reverb;
    juce::AudioBuffer<float> fxBuffer;
    ap::Smoother delaySmoother, masterSmoother;
    double sr = 44100.0;

    struct Raw
    {
        std::atomic<float> *aWave, *aOct, *aSemi, *aFine, *aLevel, *aPw, *uniVoices, *uniDetune, *uniSpread;
        std::atomic<float> *bWave, *bOct, *bSemi, *bFine, *bLevel, *bPw;
        std::atomic<float> *subLevel, *subWave, *subOct, *noiseLevel;
        std::atomic<float> *fType, *fCutoff, *fRes, *fEnvAmt, *fKey, *fLfo, *fDrive;
        std::atomic<float> *ampA, *ampD, *ampS, *ampR, *fenvA, *fenvD, *fenvS, *fenvR;
        std::atomic<float> *lfo1Shape, *lfo1Rate, *lfo1Depth, *lfo1Fade, *lfo2Shape, *lfo2Rate;
        std::atomic<float> *voices, *glide, *velAmp, *velFilter, *bendRange, *master;
        std::atomic<float> *chorusMix, *chorusRate, *delayTime, *delayFb, *delayMix, *reverbSize, *reverbMix;
    } prm {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};

} // namespace strata
