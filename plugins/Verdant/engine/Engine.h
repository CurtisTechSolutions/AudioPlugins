#pragma once

// The Verdant engine: MIDI -> Arp -> StepSeq -> voice allocator -> voices ->
// Sympathetic body -> FX rack -> master. Audio-thread safe after prepare():
// no allocation, locks or I/O inside process().
//
// Owner: W-D. FOUNDATION STUB: poly voice allocation with up to `voices` voices
// and oldest-note stealing, sustain pedal, pitch bend, the mod matrix per voice
// at control rate (32 samples), smoothed master gain, visual state.

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
    void process (float* L, float* R, int numSamples, const MidiEvent* events, int numEvents);
    const VisualState& visuals() const noexcept { return vis; }
    int latencySamples() const noexcept { return 0; }
    float tailSeconds() const noexcept;                                // max (release) + fx tail

private:
    void handleEvent (const MidiEvent&) noexcept;
    void noteOn (int note, float velocity) noexcept;
    void noteOff (int note) noexcept;
    void renderControlBlock (float* L, float* R, int numSamples) noexcept;
    void updateGlobalModulation() noexcept;

    double sr = 44100.0;
    ParamValues params {}, globalModulated {};
    ModRouting routing;
    Transport transport;
    std::array<Voice, kMaxVoices> voices;
    int lastVoice = -1;
    uint64_t noteOrder = 0;
    uint32_t noteCounter = 0;
    float bend = 0.0f, wheel = 0.0f, aftertouch = 0.0f;
    bool sustainDown = false;
    std::atomic<uint32_t> livingSeed { 0x5EED1234u };
    std::atomic<float> tail { 0.3f };

    std::array<std::shared_ptr<const Wavetable>, kNumFactoryTables> factoryTables;
    std::array<HandOff<Wavetable>, 2> userTables;
    std::array<HandOff<LfoShape>, kNumLfos> lfoShapes;
    std::array<const Wavetable*, 2> userTablePtr {};
    std::array<const LfoShape*, kNumLfos> lfoShapePtr {};

    Arpeggiator arp;
    StepSequencer seq;
    std::vector<MidiEvent> arpOut, seqOut;     // sized in the constructor (kMaxBlock events each)

    Sympathetic sympathetic;
    FxRack fx;
    ap::Smoother master;
    std::array<float, kNumModDestinations + 1> offsets {};
    VisualState vis;
};

/** Offline helper for the sound map / previews (allocates; not for the audio thread): mono (L + R) / 2. */
std::vector<float> renderPreview (const Patch&, int midiNote, float noteSeconds, float totalSeconds, double sampleRate);

} // namespace verdant
