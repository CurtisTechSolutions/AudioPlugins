#pragma once

// LFO with classic shapes, a drawable breakpoint shape, tempo sync and
// retrig / free / one-shot modes. Runs at control rate.
//
// Owner: W-C.
//
// next (n) returns the value at the CURRENT phase, then advances the phase by
// n samples' worth (n x rate / sampleRate). Call it once per control block with
// the block length; value() / phase() report the last value and the phase after
// the advance (for the UI).
//
// Output chain: shape (bipolar -1..1) -> Smooth (one-pole, time constant
// smooth x 100 ms) -> Unipolar mapping (x + 1) / 2 -> Fade-in gain (0 -> 1 over
// fadeMs, linear). The fade is applied last so a fading unipolar LFO starts from
// 0 (no modulation), not from 0.5.
//
// Shapes at phase p (0..1): Sine sin (2 pi p); Triangle 0 -> 1 (p .25) -> 0 (p .5)
// -> -1 (p .75) -> 0; Saw Up 2p - 1; Saw Down 1 - 2p; Square +1 for p < .5 else -1;
// S&H a value drawn (seeded ap::Rng, reseeded by noteOn) at every phase wrap;
// Custom the LfoShape breakpoints, segment i (point i -> i + 1) shaped by
// Envelope::shape (t, points[i].curve), y 0..1 -> 2y - 1. A null or invalid
// custom shape plays the default LfoShape (a triangle).
//
// Rate: sync ? (bpm / 60) / noteDivisionBeats (division) : rateHz, with the bpm of
// the last syncToTransport (120 when the transport has no tempo).
// Modes:
//   Retrig   noteOn sets the phase to startPhase and restarts fade and smoothing.
//   Free     every syncToTransport derives the phase from the transport: when synced
//            and playing with a tempo, frac (ppq / beats + startPhase); otherwise
//            frac (sampleTime x rate / sampleRate + startPhase). noteOn leaves the
//            phase alone (it only reseeds S&H). The fade starts at the first
//            syncToTransport (or the first noteOn / next if the engine never syncs).
//   OneShot  like Retrig, but stops at the end of the first cycle and holds the
//            final value (Saw Up +1, Saw Down / Square -1, Sine / Triangle 0,
//            S&H its last value, Custom y[last]).

#include "Common.h"

#include <ap/Math.h>
#include <ap/Tempo.h>

#include <cstdint>
#include <vector>

namespace verdant
{

// CONTRACT DEVIATION: the contract names both the shape enum and the breakpoint
// struct `LfoShape`, which cannot coexist in one namespace. The enum is LfoWave;
// LfoShape is the breakpoint shape (patch extras, Engine::setLfoShape).
enum class LfoWave : uint8_t { Sine, Triangle, SawUp, SawDown, Square, SampleHold, Custom, Count };
enum class LfoMode : uint8_t { Retrig, Free, OneShot };

struct LfoShapePoint { float x, y, curve; };   // x, y 0..1, curve -1..1 (shapes the segment that starts at this point)

struct LfoShape
{
    // 2..32 points, x ascending, x[0] = 0, x[last] = 1. Default: a triangle.
    std::vector<LfoShapePoint> points { { 0.0f, 0.0f, 0.0f }, { 0.5f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
};

/** Value (y, 0..1) of a breakpoint shape at phase x (0..1); robust to unsorted or
    out-of-range points. Shared with the UI's LfoEditor so the drawing matches. */
float evaluateLfoShape (const LfoShape&, float x) noexcept;

struct LfoParams
{
    LfoWave shape = LfoWave::Sine;
    float rateHz = 1;
    bool sync = false;
    ap::NoteDivision division = ap::NoteDivision::Quarter;
    LfoMode mode = LfoMode::Retrig;
    float startPhase = 0;   // 0..1
    float fadeMs = 0;
    float smooth = 0;       // 0..1
    bool unipolar = false;
};

class Lfo
{
public:
    void prepare (double sampleRate);
    void set (const LfoParams&) noexcept;            // any time; never resets the phase
    void setCustomShape (const LfoShape*) noexcept;  // not owned; must outlive its use (HandOff-managed)
    void noteOn (uint32_t seed) noexcept;
    void syncToTransport (const Transport&) noexcept; // call at block start (Free mode phase)
    float next (int numSamples) noexcept;            // value at the current phase, then advance by numSamples
    float value() const noexcept;
    float phase() const noexcept;

    /** Current rate in Hz (sync resolved against the last transport). */
    double rateHz() const noexcept;

private:
    float shapeAt (double p) const noexcept;
    void drawSampleHold() noexcept;

    double sr = 44100.0;
    LfoParams params;
    const LfoShape* custom = nullptr;
    Transport transport;
    double ph = 0.0;
    double elapsed = 0.0;                            // samples since the fade started
    bool fadeStarted = false;
    bool finished = false;                           // OneShot reached the end of its cycle
    bool smoothPrimed = false;
    double smoothed = 0.0;
    float out = 0.0f, held = 0.0f;
    ap::Rng rng;
};

} // namespace verdant
