#pragma once

// Sympathetic memory: a global stereo resonator body (tuned feedback-comb /
// modal strings) whose tunings follow the last N notes played; older notes
// fade by the memory length. Sits after the voice sum, before the FX rack.
// Loop gain < 1 always, soft-clipped, retuned glitch-free (crossfade), CPU
// bounded by `strings`, allocation-free after prepare().
//
// Owner: W-G. FOUNDATION STUB: exact bypass.

#include <ap/Scales.h>

namespace verdant
{

class Sympathetic
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void setParams (float amount01, int memory, float decay01, float bright01, float damp01, int strings, bool snap, const ap::Scale&);
    void noteOn (int midiNote);                           // updates the note memory (most recent first)
    void process (float* L, float* R, int numSamples);    // adds the body sound (amount = wet gain); amount 0 = exact bypass

private:
    double sr = 44100.0;
    float amount = 0.0f;
};

} // namespace verdant
