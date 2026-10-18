#pragma once

// Frisson module contract.
//
// Every knob of Frisson is one Module (an ingredient that processes audio in
// place) or one Modulator (reads the music and produces a Modulation the chain
// applies to the ingredients). Modules are plain C++ on top of the ap:: core —
// no JUCE — so they compile into both the plugin and the frisson_tests binary.
//
// Rules every implementation follows:
//   * setAmount(0) is a true bypass: the output equals the input exactly, and
//     latencySamples() does not change with amount (latency is a prepare-time
//     constant per module).
//   * No allocation, locks or I/O inside process()/analyze(); allocate in
//     prepare(). Parameter changes are smoothed internally so they never click.
//   * Stereo in place; a mono host passes the same buffer as left and right.
//   * Anything that reads level must be level-independent (relative
//     thresholds), because the plugin sits on tracks at -30 dBFS and masters
//     at -6 dBFS alike.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace frisson
{

inline constexpr int kMaxChannels = 2;

/** What the modulators ask of the ingredients. Identity values mean "no change". */
struct Modulation
{
    float airDb        = 0.0f;   // extra high-shelf gain, dB; Shine applies it (clamped 0..+4)
    float widthMul     = 1.0f;   // multiplier on Space's width amount
    float densityMul   = 1.0f;   // multiplier on Rush's parallel-compression amount
    float driveMul     = 1.0f;   // multiplier on Warmth's saturation drive
    float eqFreqMul    = 1.0f;   // multiplier on EQ centre frequencies (Focus, Shine, Warmth)
    float transientMul = 1.0f;   // multiplier on Groove's attack emphasis
    float gainL        = 1.0f;   // per-channel linear trims applied at the end of the chain
    float gainR        = 1.0f;

    static Modulation identity() noexcept { return {}; }

    /** Combine two modulations: dB values add, multipliers multiply. */
    Modulation combined (const Modulation& o) const noexcept
    {
        Modulation m;
        m.airDb        = airDb + o.airDb;
        m.widthMul     = widthMul * o.widthMul;
        m.densityMul   = densityMul * o.densityMul;
        m.driveMul     = driveMul * o.driveMul;
        m.eqFreqMul    = eqFreqMul * o.eqFreqMul;
        m.transientMul = transientMul * o.transientMul;
        m.gainL        = gainL * o.gainL;
        m.gainR        = gainR * o.gainR;
        return m;
    }
};

/** Host transport for tempo-aware behaviour; hasTempo=false means "unknown, free-run". */
struct Transport
{
    double bpm         = 120.0;
    double ppqPosition = 0.0;    // quarter notes at the first sample of the block
    bool   playing     = false;
    bool   hasTempo    = false;
};

class Module
{
public:
    virtual ~Module() = default;

    virtual void prepare (double sampleRate, int maxBlockSize) = 0;
    virtual void reset() = 0;

    /** The knob, 0..1. 0 must be an exact bypass. */
    virtual void setAmount (float amount) = 0;
    virtual float getAmount() const = 0;

    /** Applied by the chain once per block before process(). Default: ignore. */
    virtual void setModulation (const Modulation&) {}

    /** Set once per block before process() for tempo-aware modules. Default: ignore. */
    virtual void setTransport (const Transport&) {}

    /** In-place stereo processing. */
    virtual void process (float* left, float* right, int numSamples) = 0;

    /** Constant after prepare(); must not depend on amount. */
    virtual int latencySamples() const { return 0; }

    /** 0..1 for the UI activity ring: gain reduction, trigger level, drift, … */
    virtual float activity() const { return 0.0f; }

    virtual const char* name() const = 0;
};

class Modulator
{
public:
    virtual ~Modulator() = default;

    virtual void prepare (double sampleRate, int maxBlockSize) = 0;
    virtual void reset() = 0;

    virtual void setAmount (float amount) = 0;
    virtual float getAmount() const = 0;

    virtual void setTransport (const Transport&) {}

    /** Read-only analysis of a block of the *dry* input; returns the modulation to
        apply to the ingredients for the next block. Called at block rate, so any
        smoothing/envelopes run inside with the block length. */
    virtual Modulation analyze (const float* left, const float* right, int numSamples) = 0;

    virtual float activity() const = 0;

    virtual const char* name() const = 0;
};

/** Small helpers shared by modules. */
namespace util
{
    inline float clamp01 (float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

    /** Exact-bypass guard: true when the knob is effectively zero. */
    inline bool isOff (float amount) noexcept { return amount <= 1.0e-4f; }
}

} // namespace frisson
