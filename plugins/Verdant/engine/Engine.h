#pragma once

// The Verdant engine: MIDI -> Arp -> StepSeq -> voice allocator (poly / mono /
// legato, glide, 2 ms steal fades) -> voices (control blocks of <= 32 samples,
// split at event offsets) -> Sympathetic body -> FX rack -> master (20 ms
// smoothed gain). A second, global mod-matrix pass (macros, wheel, bend,
// channel aftertouch, Free-mode LFOs, Seq Mod + the last-triggered voice's
// sources) drives master, global pitch, FX and Sympathetic params.
//
// Audio-thread safe after prepare(): no allocation, locks or I/O inside
// process(). Deterministic: the same params, living seed and event sequence
// render bit-identical output.
//
// Owner: W-D.

#include "Common.h"
#include "Fx.h"
#include "HandOff.h"
#include "KeyAware.h"
#include "Lfo.h"
#include "ModMatrix.h"
#include "Params.h"
#include "Sympathetic.h"
#include "Voice.h"
#include "Wavetable.h"
#include "WavetableFactory.h"

#include <ap/Filters.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace verdant
{

struct Patch;

/** Written by the audio thread, read by the UI (relaxed atomics; values of the last-triggered voice). */
struct VisualState
{
    std::atomic<float> oscPosition[2] {};
    std::atomic<int>   activeVoices { 0 };
    std::atomic<float> modOffset[kNumModDestinations + 1] {};   // summed normalised offset per destination
    std::atomic<float> physics[4] {};                           // ball h, spring x, orbit x, orbit y
    std::atomic<float> envValue[3] {};
    std::atomic<float> lfoValue[4] {};
    std::atomic<int>   seqStep { -1 };
    std::atomic<float> outputPeak { 0.0f };

    /** Scope: a lock-free ring of the last kScopeSize output samples (mono). */
    static constexpr int kScopeSize = 2048;
    void writeScope (const float* L, const float* R, int n) noexcept;
    /** Copies the most recent min (n, kScopeSize) samples, oldest first; returns the count. */
    int readScope (float* dest, int n) const noexcept;

private:
    std::atomic<float> scope[kScopeSize] {};
    std::atomic<uint32_t> scopeWrite { 0 };
};

class Engine
{
public:
    Engine();
    ~Engine();

    void prepare (double sampleRate, int maxBlock);
    void reset();
    void setParams (const ParamValues&);                               // once per block (plain units)
    void setTransport (const Transport&);
    void setUserTable (int osc, std::shared_ptr<const Wavetable>);     // message thread (HandOff inside); used when {o}_table == User
    void setLfoShape (int lfo, std::shared_ptr<const LfoShape>);        // message thread
    void setLivingSeed (uint32_t);
    /** Any numSamples (blocks longer than the prepared maxBlock are split); events sorted by sampleOffset. */
    void process (float* L, float* R, int numSamples, const MidiEvent* events, int numEvents);
    /** Message thread: frees user tables / LFO shapes the audio thread has swapped out (call from a timer). */
    void releaseRetired();
    const VisualState& visuals() const noexcept { return vis; }
    int latencySamples() const noexcept { return 0; }
    float tailSeconds() const noexcept;                                // max (release) + fx tail

private:
    void processChunk (float* L, float* R, int numSamples, const MidiEvent* events, int numEvents) noexcept;
    int applyPatternSustain (const MidiEvent* events, int numEvents) noexcept;   // -> patternIn
    void handleEvent (const MidiEvent&) noexcept;
    void noteOn (int note, float velocity) noexcept;
    void noteOff (int note) noexcept;
    void releaseAll() noexcept;
    void renderControlBlock (float* L, float* R, int numSamples, int blockOffset) noexcept;
    void updateBlockContext() noexcept;
    void updateGlobalSources() noexcept;
    void updateGlobalModulation() noexcept;
    Transport transportAt (int sampleOffset) const noexcept;
    NoteStart makeNoteStart (int note, float velocity) noexcept;
    int voiceLimit() const noexcept;
    int voiceMode() const noexcept;

    // Mono / Legato note stack (last-note priority; releasing the top note returns to the held one).
    struct HeldNote { int note; float velocity; };
    void stackPush (int note, float velocity) noexcept;
    bool stackRemove (int note) noexcept;   // true if it was the top

    double sr = 44100.0;
    int maxBlockSize = 512;
    ParamValues params {}, globalModulated {};
    ModRouting routing;
    ap::Scale scale;
    Transport transport;
    std::array<Voice, kMaxVoices> voices;
    int lastVoice = -1;
    uint64_t noteOrder = 0;
    uint32_t noteCounter = 0;
    float bend = 0.0f, wheel = 0.0f, aftertouch = 0.0f;
    bool sustainDown = false;
    float lastNotePitch = 60.0f;
    bool haveLastNote = false;
    std::array<HeldNote, 128> heldNotes {};
    int numHeld = 0;
    std::atomic<uint32_t> livingSeed { 0x5EED1234u };
    std::atomic<float> tail { 1.3f };

    std::array<std::shared_ptr<const Wavetable>, kNumFactoryTables> factoryTables;
    std::array<HandOff<Wavetable>, 2> userTables;
    std::array<HandOff<LfoShape>, kNumLfos> lfoShapes;
    std::array<const Wavetable*, 2> userTablePtr {};
    std::array<const LfoShape*, kNumLfos> lfoShapePtr {};

    // Global (shared) LFOs: the Free-mode value of each LFO for every voice and for global destinations.
    std::array<Lfo, kNumLfos> freeLfos;
    float seqMod = 0.0f;                         // Seq Mod source, smoothed by 2 ms
    VoiceBlockContext ctx;                       // shared by all voices for the current control block

    Arpeggiator arp;
    StepSequencer seq;
    std::vector<MidiEvent> arpOut, seqOut, chunkEvents;   // sized in the constructor (kMaxBlock events each)

    // Sustain pedal while the arp or sequencer runs: the pedal holds the pattern's input notes
    // (their note-offs wait for the pedal) rather than sustaining every generated note.
    std::vector<MidiEvent> patternIn;            // kMaxBlock + 129 events
    std::array<bool, 128> deferredOff {};
    int numDeferred = 0;
    bool patternSustain = false;

    Sympathetic sympathetic;
    FxRack fx;
    ap::Smoother master;
    bool masterPrimed = false;                   // false until the first block after prepare / reset (snaps instead of ramping)
    std::array<float, kNumModDestinations + 1> offsets {}, globalOffsets {};
    VisualState vis;
};

/** Offline helper for the sound map / previews (allocates; not for the audio thread): mono (L + R) / 2. */
std::vector<float> renderPreview (const Patch&, int midiNote, float noteSeconds, float totalSeconds, double sampleRate);

} // namespace verdant
