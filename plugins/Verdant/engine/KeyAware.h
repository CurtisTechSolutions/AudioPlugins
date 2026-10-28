#pragma once

// Key-aware helpers: scale snapping, the arpeggiator and the step sequencer.
//
// Timing: when transport.playing && hasTempo, step boundaries are locked to ppq
// (step k starts at ppq = k x beats (division), swing delays odd steps by
// swing x 0.5 x step); otherwise free-run from the first note at transport.bpm
// (120 if no tempo).
//
// Owner: W-E. FOUNDATION STUB: snapToScale / scaleDegreeOffset are real; the
// arpeggiator and sequencer pass their input through unchanged (on or off).

#include "Common.h"

#include <ap/Scales.h>
#include <ap/Tempo.h>

#include <array>

namespace verdant
{

int snapToScale (int midiNote, const ap::Scale&);                     // nearest scale note (ties -> lower)
int scaleDegreeOffset (int midiNote, int degrees, const ap::Scale&);   // move `degrees` scale steps from the (snapped) note

struct ArpParams
{
    bool on = false;
    int mode = 0;            // Up, Down, Up-Down, Down-Up, Random, As Played, Chord
    int octaves = 1;
    ap::NoteDivision division = ap::NoteDivision::Eighth;
    float gate = 0.8f;
    float swing = 0;
    bool snap = false;
};

struct SeqParams
{
    bool on = false;
    int steps = 8;
    ap::NoteDivision division = ap::NoteDivision::Sixteenth;
    float gate = 0.8f;
    float swing = 0;
    bool snap = true;
    std::array<int, 16> pitch {};
    std::array<float, 16> velocity {};
    std::array<bool, 16> gateOn {};
    std::array<float, 16> mod {};
};

class Arpeggiator
{
public:
    void prepare (double sampleRate);
    void set (const ArpParams&, const ap::Scale&);
    /** Feeds the incoming note events; emits the arpeggiated note on/offs with sample offsets into `out`
        (capacity maxOut, e.g. kMaxBlock events). When off, passes the input through unchanged. Returns the count. */
    int process (const MidiEvent* in, int numIn, const Transport&, int numSamples, MidiEvent* out, int maxOut);
    void reset();

private:
    double sr = 44100.0;
    ArpParams params;
    ap::Scale scale;
};

class StepSequencer
{
public:
    void prepare (double sampleRate);
    void set (const SeqParams&, const ap::Scale&);
    int process (const MidiEvent* in, int numIn, const Transport&, int numSamples, MidiEvent* out, int maxOut);
    float modValue() const noexcept;     // current step's mod value 0..1
    int currentStep() const noexcept;    // -1 when not running
    void reset();

private:
    double sr = 44100.0;
    SeqParams params;
    ap::Scale scale;
};

} // namespace verdant
