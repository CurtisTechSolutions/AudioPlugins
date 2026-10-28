#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "engine/Breeding.h"
#include "engine/Engine.h"
#include "engine/Params.h"
#include "engine/Patch.h"

#include <array>
#include <atomic>
#include <memory>
#include <utility>
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

    Threads:
      * audio thread: processBlock only (no locks, no allocation).
      * message thread: the whole public API below, the preset manager and the
        background-task callbacks.
      * hosts may also call setStateInformation / getStateInformation /
        setCurrentProgram from a non-message thread; those paths only touch
        lock-protected state and never block the audio thread.
*/
class Processor : public juce::AudioProcessor,
                  private juce::Timer,
                  private juce::AsyncUpdater
{
public:
    Processor();
    ~Processor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override;
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    // Programs = the factory presets (presets() lists them first, so program i = preset entry i).
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    // State: <VERDANT program=".."> with one <PARAM id value> per parameter (the APVTS format) and
    // <EXTRAS> = serializePatch text of the non-parameter fields (CONTRACT §6). Other root tags are ignored.
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Message-thread API for the editor, the preset manager and the background jobs.

    juce::AudioProcessorValueTreeState apvts;

    verdant::Engine& engine() noexcept               { return eng; }
    const verdant::Engine& engine() const noexcept   { return eng; }
    PresetManager& presets() noexcept                { return *presetManager; }
    BackgroundTasks& tasks() noexcept                { return *backgroundTasks; }

    /** The current parameter values plus the extras. Safe from any thread except the audio thread. */
    verdant::Patch currentPatch() const;

    /** The current parameter values (plain units), read from the APVTS. Any thread. */
    verdant::ParamValues parameterValues() const noexcept;

    /** Sets every parameter (host-notified; the engine smooths the change) and posts the extras (user
        tables, LFO shapes, seed) to the engine. keepName = true keeps the current name, category, author
        and tags and notifies only the parameters that change (live blends, auditions). While the seed is locked, the current living
        seed is kept; otherwise the patch's seed is adopted. Sends patchChanged. */
    void applyPatch (const verdant::Patch&, bool keepName);

    /** The non-parameter fields currently loaded (name, tags, tables, canvases, LFO shapes, macro names,
        seed); `values` is not meaningful here (use currentPatch()). Message thread only: this is a
        message-thread mirror that is refreshed before patchChanged is sent. */
    const verdant::Patch& patchExtras() const noexcept { return uiExtras; }

    /** Changes the patch's name / category / tags / author without touching the sound (used by Save). */
    void setPatchInfo (const juce::String& name, const juce::String& category,
                       const juce::StringArray& tags, const juce::String& author);

    /** Drag-to-modulate: creates, updates or clears the matrix slot that routes `source` to destination
        `destIndex` (1..numModDestinations()). amount is -1..1 (= -100..100 % of mod{n}_amt); exactly 0
        clears the slot. Returns false, changing nothing, when a new slot is needed but all 24 are in use
        (a slot is free when both its source and destination are None). */
    bool setModulation (int destIndex, verdant::ModSource source, float amount);

    /** (slot, source) for every matrix slot routed to `destIndex`, in slot order (amount: mod{slot+1}_amt). */
    std::vector<std::pair<int, verdant::ModSource>> modulationsFor (int destIndex) const;

    /** Living seed. While locked, applyPatch / preset loads keep the current seed and rerollSeed() is
        ignored; while unlocked, loading adopts the patch's seed and rerollSeed() draws a new random seed,
        posts it to the engine and marks the patch modified. */
    uint32_t livingSeed() const;
    void rerollSeed();
    void setSeedLocked (bool locked);
    bool seedLocked() const;
    bool isSeedLocked() const { return seedLocked(); }

    /** Extras edited from the UI (each sends patchChanged). */
    void setMacroName (int macro, const juce::String& name);
    void setLfoShape (int lfo, const verdant::LfoShape& shape);

    /** Installs a user wavetable on oscillator `osc` (0 = A, 1 = B), stores it in the patch extras and
        selects "User" for that oscillator (an empty table clears it and leaves the selection alone).
        `built` may pass the already-built table to skip a rebuild. */
    void setUserTable (int osc, const verdant::UserTable& table, std::shared_ptr<const verdant::Wavetable> built = nullptr);

    /** Plays a note through the synth for `seconds` (preset / sound-map audition). Message thread; the
        note is merged into the next audio block, so it sounds only while the host is processing. */
    void auditionNote (int midiNote, float seconds, float velocity = 0.8f);

    //==============================================================================
    // Breeding (BREED page). Parents default to the init patch.
    struct Breeding
    {
        verdant::Patch parentA, parentB;
        juce::String parentAName { "Init" }, parentBName { "Init" };
        std::vector<verdant::Patch> children;    // the current generation (8 after breedNow())
        verdant::BreedHistory history;
        float mutation = 0.25f;                  // 0..1
        uint32_t seed = 1;                       // same parents + mutation + seed = same children
    };

    Breeding& breeding() noexcept { return breedState; }
    void breedNow();                                          // children = breed (A, B, {mutation, 8, seed}); pushed to the history
    void applyChild (int index);                              // applyPatch (children[index], false)
    void undoBreed();                                         // back to the previous generation's children
    void setParentFromCurrent (int which);                    // which: 0 = A, 1 = B
    void setParentFromPreset (int which, int presetIndex);    // presetIndex: PresetManager::entries() index

    //==============================================================================
    std::atomic<float> outputPeak { 0.0f };   // linear peak of the last block
    juce::ChangeBroadcaster patchChanged;     // async: the patch, a preset or the extras changed
    juce::ChangeBroadcaster breedingChanged;  // async: parents / children / generation changed

    /** Increments whenever the extras change (PresetManager uses it to detect a modified preset). */
    uint32_t extrasRevision() const noexcept { return extrasRev.load (std::memory_order_acquire); }

    /** Program index to report to the host (the preset manager keeps it in sync with factory loads). */
    void setCurrentProgramIndex (int index, bool notifyHost);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void applyPatchInternal (const verdant::Patch&, bool keepName, bool restoringState);
    void postExtrasToEngine (const verdant::Patch& extrasCopy);
    void setParameterPlain (verdant::P, float plain);
    void extrasChanged (bool modifiesPatch);   // bumps the revision (when modifying), refreshes uiExtras, sends patchChanged
    verdant::Transport readTransport();
    int collectMidi (const juce::MidiBuffer&, int numSamples) noexcept;

    void timerCallback() override;        // frees retired tables / shapes off the audio thread
    void handleAsyncUpdate() override;    // refreshes uiExtras after an off-message-thread change

    static constexpr int kMaxMidiEvents = 4096;
    static constexpr int kAuditionQueueSize = 16;

    verdant::Engine eng;
    std::array<std::atomic<float>*, verdant::kNumParams> raw {};
    std::array<juce::RangedAudioParameter*, verdant::kNumParams> parameters {};
    verdant::ParamValues snapshot {};
    std::array<verdant::MidiEvent, kMaxMidiEvents> midiEvents {};
    std::vector<float> monoScratch;
    int preparedBlock = 512;              // the engine's prepared maximum block (Engine() prepares 512)

    // Audition: message thread -> audio thread (single producer / single consumer).
    struct AuditionRequest { int note; float velocity; float seconds; };
    juce::AbstractFifo auditionFifo { kAuditionQueueSize };
    std::array<AuditionRequest, kAuditionQueueSize> auditionQueue {};
    int auditionPlaying = -1;             // audio thread: the sounding audition note
    juce::int64 auditionRemaining = 0;    // audio thread: samples until its note-off

    mutable juce::CriticalSection extrasLock;   // never taken on the audio thread
    verdant::Patch extras;                      // non-parameter fields (values unused)
    verdant::Patch uiExtras;                    // message-thread mirror of `extras`
    std::atomic<uint32_t> extrasRev { 0 };
    juce::CriticalSection engineLock;           // serialises posts into the engine's hand-offs
    std::array<verdant::UserTable, 2> installedTables;               // what the engine holds (skips rebuilds)
    std::array<verdant::LfoShape, verdant::kNumLfos> installedShapes;
    bool shapesInstalled = false;

    Breeding breedState;
    std::atomic<int> currentProgram { 0 };

    std::unique_ptr<PresetManager> presetManager;
    std::unique_ptr<BackgroundTasks> backgroundTasks;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};

} // namespace verdant
