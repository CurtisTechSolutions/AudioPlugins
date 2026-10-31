#pragma once

// AHDSR envelope with attack / decay curves (Env 1 = amp). Output 0..1.
//
// Owner: W-C.
//
// Each segment runs a linear phase t = samples / segmentSamples (0..1) mapped by
//   shape (t, c) = t^(1 / (1 + 3c))  for c > 0 (fast start)
//                = t^(1 + 3|c|)      for c < 0 (slow start)
// Attack rises from the level at note-on to 1 with c = attackCurve and reaches 1
// exactly attackMs after note-on (attack 0: the first sample is 1). Hold keeps 1
// for holdMs. Decay falls from 1 to sustain over decayMs and release from the level
// at note-off to 0 over releaseMs, both with c = -decayCurve (so a negative decay
// curve falls fast first, like an exponential). Sustain 0 ends the envelope (Idle)
// at the end of the decay.
//
// Times latch at noteOn(): set() during a note never restarts or jumps; the
// sustain level follows set() live through a slew limiter (full scale in 10 ms).
// A retrigger restarts the attack from the current level (no click). kill() -> Idle.
//
// The level is a pure function of the stage, the samples elapsed in it and the
// latched values, so skip (n) gives exactly the same state as n calls to next().

#include <cstdint>

namespace verdant
{

struct EnvParams
{
    float attackMs = 5, holdMs = 0, decayMs = 300, sustain = 0.8f, releaseMs = 300;
    float attackCurve = 0, decayCurve = -0.4f;   // -1..1
};

class Envelope
{
public:
    enum class Stage { Idle, Attack, Hold, Decay, Sustain, Release };

    void prepare (double sampleRate);
    void set (const EnvParams&) noexcept;         // times latch at noteOn; sustain follows live (slewed)
    void noteOn() noexcept;
    void noteOff() noexcept;
    void kill() noexcept;
    bool isActive() const noexcept;
    Stage stage() const noexcept;
    float next() noexcept;                        // advance one sample, return the new level
    float value() const noexcept;
    void skip (int numSamples) noexcept;          // advance without output (control-rate use)

    /** The segment curve of the contract: t in 0..1, c in -1..1, monotonic, shape (0) = 0, shape (1) = 1. */
    static float shape (float t, float c) noexcept;

private:
    void advance (int numSamples) noexcept;
    void enterStage (Stage) noexcept;
    void slewSustain (int numSamples) noexcept;
    float levelAt() const noexcept;

    double sr = 44100.0;
    EnvParams params, latched;
    Stage current = Stage::Idle;
    float level = 0.0f;
    float startLevel = 0.0f;                      // level at the start of an attack / release
    float sustainNow = 0.8f, sustainTarget = 0.8f, sustainSlew = 0.0f;   // slewed live sustain level
    float slewFrom = 0.8f;                        // sustain level when the current slew started
    int64_t slewCount = 0;                        // samples into the current slew
    float attackExp = 1.0f, decayExp = 1.0f;      // shape exponents for the latched curves
    double segLen = 0.0;                          // samples in the current segment (fractional)
    int64_t segEnd = 0;                           // sample count at which the segment completes
    int64_t pos = 0;                              // samples into the current segment
};

} // namespace verdant
