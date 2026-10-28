#pragma once

// AHDSR envelope with attack / decay curves (Env 1 = amp). Output 0..1.
//
// Owner: W-C. FOUNDATION STUB: linear AHDSR, curves ignored; times latch at noteOn().

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
    void set (const EnvParams&);                  // times latch at noteOn
    void noteOn();
    void noteOff();
    void kill();
    bool isActive() const noexcept;
    Stage stage() const noexcept;
    float next() noexcept;                        // one sample
    float value() const noexcept;
    void skip (int numSamples) noexcept;          // advance without output (control-rate use)

private:
    double sr = 44100.0;
    EnvParams params, latched;
    Stage current = Stage::Idle;
    float level = 0.0f, releaseStep = 0.0f;
    double stagePos = 0.0;                        // samples into the current stage
};

} // namespace verdant
