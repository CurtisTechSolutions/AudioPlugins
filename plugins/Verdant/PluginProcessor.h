#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "engine/Engine.h"
#include "engine/Params.h"
#include "engine/Patch.h"

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace verdant
{

class PresetManager;
class BackgroundTasks;

/**
    The JUCE wrapper around verdant::Engine. The APVTS layout is generated from
    the parameter table (engine/Params.h), so host, UI and engine share ids,
    ranges and skews. Everything that is not a parameter (names, tags, macro
    names, LFO shapes, user tables, living seed) lives in a Patch "extras" copy
    guarded by a lock that the audio thread never takes.
*/
class Processor : public juce::AudioProcessor
{
public:
    Processor();
    ~Processor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override;
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }

    // Programs = the factory presets (presets() lists them first).
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    // State: <VERDANT> with one <PARAM id value> per parameter and <EXTRAS> = serializePatch text of the
    // non-parameter fields (CONTRACT §6). Other root tags are ignored.
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Message-thread API for the editor, the preset manager and the background jobs.

    juce::AudioProcessorValueTreeState apvts;

    verdant::Engine& engine() noexcept     { return eng; }
    PresetManager& presets() noexcept      { return *presetManager; }
    BackgroundTasks& tasks() noexcept      { return *backgroundTasks; }

    /** The current parameter values plus the extras. Safe from any thread except the audio thread. */
    verdant::Patch currentPatch() const;

    /** Sets every parameter that differs (host-notified; the engine smooths the change) and posts the
        extras (user tables, LFO shapes, seed) to the engine. keepName = true keeps the current name,
        category, author and tags (live blends, auditions). While the seed is locked, the current living
        seed is kept; otherwise the patch's seed is adopted. Sends patchChanged. */
    void applyPatch (const verdant::Patch&, bool keepName);

    /** Drag-to-modulate: creates, updates or clears the matrix slot that routes `source` to destination
        `destIndex` (1..numModDestinations()). amount is -1..1 (= -100..100 % of mod{n}_amt); exactly 0
        clears the slot. Returns false, changing nothing, when a new slot is needed but all 24 are in use
        (a slot is free when both its source and destination are None). */
    bool setModulation (int destIndex, verdant::ModSource source, float amount);

    /** Living seed. While locked, applyPatch / preset loads keep the current seed and rerollSeed() is
        ignored; while unlocked, loading adopts the patch's seed and rerollSeed() draws a new random seed
        and posts it to the engine. */
    void rerollSeed();
    void setSeedLocked (bool locked);
    bool isSeedLocked() const;
    uint32_t livingSeed() const;

    /** Extras edited from the UI (each sends patchChanged). */
    void setMacroName (int macro, const juce::String& name);
    void setLfoShape (int lfo, const verdant::LfoShape& shape);

    /** Installs a user wavetable on oscillator `osc` (0 = A, 1 = B), stores it in the patch extras and
        selects "User" for that oscillator. `built` may pass the already-built table to skip a rebuild. */
    void setUserTable (int osc, const verdant::UserTable& table, std::shared_ptr<const verdant::Wavetable> built = nullptr);

    std::atomic<float> outputPeak { 0.0f };   // linear peak of the last block
    juce::ChangeBroadcaster patchChanged;     // async: the patch, a preset or the extras changed

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void applyPatchInternal (const verdant::Patch&, bool keepName, bool restoringState);
    void postExtrasToEngine (const verdant::Patch& extrasCopy);
    void setParameterPlain (verdant::P, float plain);
    verdant::Transport readTransport();

    static constexpr int kMaxMidiEvents = 2048;

    verdant::Engine eng;
    std::array<std::atomic<float>*, verdant::kNumParams> raw {};
    std::array<juce::RangedAudioParameter*, verdant::kNumParams> parameters {};
    verdant::ParamValues snapshot {};
    std::array<verdant::MidiEvent, kMaxMidiEvents> midiEvents {};
    std::vector<float> monoScratch;

    mutable juce::CriticalSection extrasLock;   // never taken on the audio thread
    verdant::Patch extras;                      // non-parameter fields (values unused)
    juce::CriticalSection engineLock;           // serialises posts into the engine's hand-offs
    std::array<verdant::UserTable, 2> installedTables;   // what the engine currently holds (skips rebuilds)

    std::unique_ptr<PresetManager> presetManager;
    std::unique_ptr<BackgroundTasks> backgroundTasks;
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};

} // namespace verdant
