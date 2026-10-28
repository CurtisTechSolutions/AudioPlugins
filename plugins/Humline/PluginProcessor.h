#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <ap/DelayLine.h>
#include <ap/Filters.h>
#include <ap/NoteQuantizer.h>
#include <ap/NoteTracker.h>
#include <ap/PitchTracker.h>
#include <ap/Scales.h>
#include <ap_synth/SynthVoice.h>

#include <array>
#include <atomic>
#include <vector>

namespace humline
{

namespace ids
{
    inline constexpr const char* tracking     = "tracking";
    inline constexpr const char* gate         = "gate";
    inline constexpr const char* sensitivity  = "sensitivity";
    inline constexpr const char* minNote      = "min_note";
    inline constexpr const char* release      = "release";
    inline constexpr const char* key          = "key";
    inline constexpr const char* scale        = "scale";
    inline constexpr const char* snap         = "snap";
    inline constexpr const char* octave       = "octave";
    inline constexpr const char* bend         = "bend";
    inline constexpr const char* bendRange    = "bend_range";
    inline constexpr const char* grid         = "grid";
    inline constexpr const char* timbre       = "timbre";
    inline constexpr const char* synthCutoff  = "synth_cutoff";
    inline constexpr const char* synthRes     = "synth_res";
    inline constexpr const char* synthAttack  = "synth_attack";
    inline constexpr const char* synthRelease = "synth_release";
    inline constexpr const char* glide        = "glide";
    inline constexpr const char* synthLevel   = "synth_level";
    inline constexpr const char* dryMix       = "dry_mix";
    inline constexpr const char* midiOut      = "midi_out";
    inline constexpr const char* channel      = "channel";
    inline constexpr const char* velDynamic   = "vel_dynamic";
    inline constexpr const char* velFixed     = "vel_fixed";
}

/** The grid choices, in parameter order. */
inline constexpr ap::NoteDivision kGridDivisions[] = {
    ap::NoteDivision::Quarter, ap::NoteDivision::Eighth, ap::NoteDivision::Sixteenth,
    ap::NoteDivision::EighthTriplet, ap::NoteDivision::SixteenthTriplet,
};
inline constexpr int kNumGridChoices = 1 + (int) (sizeof (kGridDivisions) / sizeof (kGridDivisions[0]));

/** Grid size in beats for a `grid` parameter index (0 = Off -> 0). */
inline double gridBeatsForChoice (int choice) noexcept
{
    if (choice <= 0 || choice >= kNumGridChoices) return 0.0;
    return ap::noteDivisionBeats (kGridDivisions[choice - 1]);
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
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // --- For the editor (atomics written on the audio thread).
    std::atomic<float> detectedHz { 0.0f };
    std::atomic<float> playedHz { 0.0f };
    std::atomic<float> inputLevel { 0.0f };
    std::atomic<float> outputLevel { 0.0f };
    std::atomic<int>   currentNote { -1 };
    std::atomic<bool>  capturing { false };
    std::atomic<bool>  clearRequested { false };
    std::atomic<float> captureBpm { 120.0f };

    /** The take: audio thread appends, editor snapshots. */
    const ap::NoteCapture& take() const noexcept { return capture; }
    void requestClear() noexcept { clearRequested.store (true); }

    /** Builds the .mid for the current take; applies the offline grid quantise when `grid` is on. */
    bool writeTakeToFile (const juce::File& file) const;
    juce::MidiFile buildTakeMidi() const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void updateParams();
    void applyTimbre (int timbre);

    static constexpr int kHop = 256;
    static constexpr int kVoices = 4;
    static constexpr int kCaptureNotes = 4096;

    std::array<ap::PitchTracker, 3> trackers;
    int activeTracker = 1;
    ap::NoteTracker noteTracker;
    ap::RhythmQuantizer rhythm;
    ap::NoteCapture capture { kCaptureNotes };

    ap::synth::Settings settings;
    ap::synth::PolySynth synth;
    float lastNoteHz = 0.0f;
    juce::MidiBuffer synthMidi;            // pre-sized; events for the embedded synth

    std::array<ap::DelayLine, 2> dryDelay;
    ap::Smoother dryGain, synthGain;

    double  sr = 44100.0;
    int     latency = -1;                  // -1 forces the first publish
    int     holdSamples = 0;
    int     lastTimbre = -1;
    int64_t clock = 0;                     // absolute sample index of the next input sample
    double  freeRunBeats = 0.0;
    int     activeChannel = 1;
    int     soundingNote = -1;             // note the synth / MIDI out currently holds
    std::atomic<bool> pendingAllNotesOff { false };

    struct Raw
    {
        std::atomic<float> *tracking, *gate, *sensitivity, *minNote, *release, *key, *scale, *snap, *octave,
                           *bend, *bendRange, *grid, *timbre, *synthCutoff, *synthRes, *synthAttack, *synthRelease,
                           *glide, *synthLevel, *dryMix, *midiOut, *channel, *velDynamic, *velFixed;
    } prm {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};

} // namespace humline
