#pragma once

// Key-aware helpers: scale snapping, the arpeggiator and the step sequencer.
//
// Timing: when transport.playing && hasTempo, step boundaries are locked to ppq
// (step k starts at ppq = k x beats (division), swing delays odd steps by
// swing x step / 3, so swing 100 % turns straight eighths into a triplet feel);
// otherwise the step clock free-runs from the first NoteOn (pressed while no
// notes were held) at transport.bpm (120 if no tempo).
//
// Both processors: input NoteOns are consumed into a held-note set (velocity 0
// counts as a NoteOff); a NoteOff removes the note, releases what it is sounding
// immediately and, if it was the last held note, stops the clock and releases
// everything. A NoteOff for a note that is not held (pressed while the processor
// was off) passes through, as do all non-note events (AllNotesOff also clears
// the held set). Every step boundary first releases the previous step's notes;
// each NoteOn's NoteOff follows at gate x step length (at least one sample).
// Output is in ascending sampleOffset order and never exceeds maxOut.
//
// Owner: W-E. Audio thread: process() never allocates (fixed-size state).

#include "Common.h"

#include <ap/Math.h>
#include <ap/Scales.h>
#include <ap/Tempo.h>

#include <array>
#include <cstdint>

namespace verdant
{

int snapToScale (int midiNote, const ap::Scale&);                     // nearest scale note (ties -> lower)
int scaleDegreeOffset (int midiNote, int degrees, const ap::Scale&);   // move `degrees` scale steps from the (snapped) note

struct ArpParams
{
    enum Mode { Up, Down, UpDown, DownUp, Random, AsPlayed, Chord, NumModes };

    bool on = false;
    int mode = 0;            // Up, Down, Up-Down, Down-Up, Random, As Played, Chord
    int octaves = 1;
    ap::NoteDivision division = ap::NoteDivision::Eighth;
    float gate = 0.8f;       // 0..1 of the step length
    float swing = 0;         // 0..1: odd steps delayed by swing x step / 3
    bool snap = false;
};

struct SeqParams
{
    bool on = false;
    int steps = 8;
    ap::NoteDivision division = ap::NoteDivision::Sixteenth;
    float gate = 0.8f;
    float swing = 0;
    bool snap = true;        // on: pitch[] are scale degrees in the global key; off: semitones
    std::array<int, 16> pitch {};
    std::array<float, 16> velocity {};
    std::array<bool, 16> gateOn {};
    std::array<float, 16> mod {};
};

namespace detail
{
    /** Shared step-clock / held-note / pending-NoteOff machinery of the arp and the sequencer. */
    struct StepCore
    {
        static constexpr int kMaxHeld     = 128;
        static constexpr int kMaxSounding = 128;

        struct Held     { int note = 0, channel = 1; float velocity = 0.0f; };
        struct Sounding { int note = 0, channel = 1, source = 0; int64_t offTime = 0; };

        double sr = 44100.0;
        std::array<Held, kMaxHeld> held {};
        int numHeld = 0;
        std::array<Sounding, kMaxSounding> sounding {};
        int numSounding = 0;

        bool running = false;
        bool wasSynced = false;
        int64_t blockStart = 0;       // absolute sample index of the current block's first sample
        int64_t counter = 0;          // steps fired since the clock started
        int64_t lastGridStep = -1;    // synced: the last absolute grid step fired
        double freeBase = 0.0;        // free-run: absolute (unswung) time of the next step
        double lastFireBase = 0.0;    // absolute (unswung) time of the last step fired

        // Output of the current process() call.
        MidiEvent* out = nullptr;
        int maxOut = 0, numOut = 0;

        void clear() noexcept;
        void emit (const MidiEvent&) noexcept;
        void noteOff (int note, int channel, int offset) noexcept;
        void releaseAll (int offset) noexcept;
        void releaseSource (int source, int offset) noexcept;
        /** NoteOn for `note`, NoteOff scheduled at `offTime` (absolute); skipped if the note already sounds. */
        void startNote (int note, int channel, float velocity, int source, int offset, int64_t offTime) noexcept;
        int findHeld (int note) const noexcept;
    };
}

class Arpeggiator
{
public:
    void prepare (double sampleRate);
    void set (const ArpParams&, const ap::Scale&);
    /** Feeds the incoming note events; emits the arpeggiated note on/offs with sample offsets into `out`
        (capacity maxOut, e.g. kMaxBlock events). When off, passes the input through unchanged. Returns the count. */
    int process (const MidiEvent* in, int numIn, const Transport&, int numSamples, MidiEvent* out, int maxOut) noexcept;
    void reset();

    bool isRunning() const noexcept { return core.running; }

private:
    void fireStep (int64_t gridStep, int offset, double stepSamples) noexcept;

    ArpParams params;
    ap::Scale scale;
    detail::StepCore core;
    ap::Rng rng;
    int lastRandomIndex = -1;
};

class StepSequencer
{
public:
    void prepare (double sampleRate);
    void set (const SeqParams&, const ap::Scale&);
    int process (const MidiEvent* in, int numIn, const Transport&, int numSamples, MidiEvent* out, int maxOut) noexcept;
    float modValue() const noexcept;     // current step's mod value 0..1, smoothed by a 2 ms one-pole
    int currentStep() const noexcept;    // -1 when not running
    void reset();

private:
    void fireStep (int64_t gridStep, int offset, double stepSamples) noexcept;
    void advanceMod (int numSamples) noexcept;

    SeqParams params;
    ap::Scale scale;
    detail::StepCore core;
    int step = -1;
    float modTarget = 0.0f, modSmoothed = 0.0f;
};

} // namespace verdant
