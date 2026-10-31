#pragma once

// Sympathetic memory: a global stereo resonator body (tuned feedback-comb /
// Karplus-Strong strings) whose tunings follow the last N notes played; older
// notes fade by the memory length. Sits after the voice sum, before the FX rack.
// Loop gain < 0.999 always, soft-clipped, retuned glitch-free (10 ms crossfade
// between two reads of the string's delay line), CPU bounded by `strings`,
// allocation-free after prepare().
//
// String assignment: the first min (strings, memory) strings take one
// remembered note each (most recent first); any strings beyond that pair up
// with the notes again, most recent first, detuned +4 / -4 / +4 ... cents
// (piano-style unison pairs), so Strings > Memory adds shimmer. Assignments are
// stable: a note keeps its physical string while it stays remembered; a new
// note takes the quietest free string among the first `strings` (a silent one if
// any, else a ringing one retunes with the crossfade); strings that lose their
// note are damped (short RT60) and ring out at their own pitch.
//
// Owner: W-G.

#include "FxUnits.h"

#include <ap/DelayLine.h>
#include <ap/Filters.h>
#include <ap/Saturation.h>
#include <ap/Scales.h>

#include <array>
#include <atomic>
#include <vector>

namespace verdant
{

class Sympathetic
{
public:
    static constexpr int kMaxStrings = 12;
    static constexpr int kMaxMemory  = 8;

    void prepare (double sampleRate, int maxBlock);
    void reset();
    void setParams (float amount01, int memory, float decay01, float bright01, float damp01, int strings, bool snap, const ap::Scale&);
    void noteOn (int midiNote);                           // updates the note memory (most recent first)
    void process (float* L, float* R, int numSamples) noexcept;   // adds the body sound (amount = wet gain); amount 0 = exact bypass

    /** RT60 of a string's fundamental for symp_decay 0..1: lerp (0.3 s, 8 s). */
    static float rt60Seconds (float decay01) noexcept;

    /** Remembered notes (raw MIDI, before snapping), most recent first. Returns the count. Audio thread. */
    int rememberedNotes (int* out, int maxOut) const noexcept;

    /** For the FX page's string-activity display (any thread): the MIDI note string s is tuned to
        (snapped; -1 = unassigned) and its recent output level (0..~1). */
    int stringNote (int s) const noexcept;
    float stringLevel (int s) const noexcept;

private:
    struct String
    {
        ap::DelayLine line;
        int key = -1;                 // assigned (note, detune) key, -1 = free
        int note = -1;
        float cents = 0.0f;
        float hz = 261.63f;
        float delay = 100.0f;         // samples, the read currently in use
        float oldDelay = 100.0f;      // the read being faded out during a retune
        float targetDelay = 100.0f;
        float fade = 1.0f;            // 1 = no crossfade running
        float loopGain = 0.0f, targetLoopGain = 0.0f;
        float excGain = 0.0f;
        float lpState = 0.0f;
        fxdetail::LinearRamp gain;    // note-age output gain (1 - age / memory), ramped
        float panL = 1.0f, panR = 0.0f;
        float level = 0.0f;
        bool asleep = true;
        bool released = false;        // no longer wanted: damped, not excited
    };

    void updateAssignments() noexcept;
    void updateTuning (String&) noexcept;
    void processChunk (float* L, float* R, int numSamples) noexcept;

    double sr = 44100.0;
    int maxBlockSize = 0;
    bool fresh = true;

    // Parameters
    fxdetail::LinearRamp amount;
    int memory = 4, numStrings = 6;
    float decay = 0.5f, bright = 0.5f, damp = 0.3f;
    bool snap = false;
    ap::Scale scale;

    // Note memory, most recent first.
    std::array<int, kMaxMemory> notes {};
    int noteCount = 0;

    static constexpr int kPowerPoints = 48;
    std::array<float, kPowerPoints> powerCos {};   // cos (w) on a uniform grid over 0..pi

    std::array<String, kMaxStrings> strings;
    float lpCoeff = 0.5f;
    float configuredBright = -1.0f;

    ap::Biquad hp, shelf;
    ap::DcBlocker dcL, dcR;
    std::vector<float> excitation, amountBuf, wetL, wetR;

    std::array<std::atomic<int>, kMaxStrings> uiNote {};
    std::array<std::atomic<float>, kMaxStrings> uiLevel {};
};

} // namespace verdant
