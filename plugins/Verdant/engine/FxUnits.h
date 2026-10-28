#pragma once

// Verdant FX units: the eight effects the FX rack (Fx.h) chains. Each one is a
// plain stereo in-place processor with prepare / reset / set / process, so the
// rack and the tests drive them the same way.
//
// Rules every unit follows:
//   * prepare() allocates; reset(), set() and process() never do.
//   * set() is cheap (per block): it only updates targets. Every parameter is
//     ramped per sample (or, for filter coefficients, re-derived every 32
//     samples while its ramp moves), so automation and modulation never zip.
//     The first set() after reset() snaps the ramps (no fade-in at start).
//   * A unit whose mix is settled at 0 is an exact bypass (output == input,
//     bit for bit); isBypassed() reports it so the rack can skip the unit.
//   * Output is always finite: feedback paths are bounded (loop gain < 1 plus
//     a soft safety limiter that is linear below 1.0).
//
// Owner: W-G.

#include "Common.h"

#include <ap/DelayLine.h>
#include <ap/Filters.h>
#include <ap/Math.h>
#include <ap/Saturation.h>
#include <ap/Tempo.h>

#include <array>
#include <cmath>
#include <cstdint>

namespace verdant
{

namespace fxdetail
{
    /** Linear per-sample ramp that lands on its target exactly (so "settled at 0" is a real 0). */
    class LinearRamp
    {
    public:
        void prepare (double sampleRate, float ms) noexcept
        {
            rampLength = std::max (1, (int) std::lround (sampleRate * 0.001 * (double) ms));
        }

        void snap (float v) noexcept   { current = target = v; steps = 0; }

        void setTarget (float t) noexcept
        {
            if (sameValue (t, target))
                return;
            target = t;
            steps = rampLength;
            increment = (target - current) / (float) steps;
        }

        inline float next() noexcept
        {
            if (steps > 0)
            {
                if (--steps == 0) current = target;
                else              current += increment;
            }
            return current;
        }

        /** Advances n samples at once (for values only read once per sub-block). */
        void skip (int n) noexcept
        {
            if (steps <= 0) return;
            if (n >= steps) { current = target; steps = 0; return; }
            current += increment * (float) n;
            steps -= n;
        }

        float value() const noexcept        { return current; }
        float targetValue() const noexcept  { return target; }
        bool isRamping() const noexcept     { return steps > 0; }
        bool isSettledAt (float v) const noexcept { return steps <= 0 && sameValue (current, v); }

    private:
        /** Exact equality (+0 == -0) without operator== on floats (-Wfloat-equal in the plugin build). */
        static bool sameValue (float a, float b) noexcept { return ! (a < b) && ! (b < a); }

        float current = 0.0f, target = 0.0f, increment = 0.0f;
        int steps = 0, rampLength = 1;
    };

    /** One-pole slew (used for delay times: a time change becomes a short pitch glide, never a jump). */
    class Slew
    {
    public:
        void prepare (double sampleRate, float ms) noexcept { coeff = (double) ap::onePoleCoeff (ms, sampleRate); }
        void snap (double v) noexcept   { current = target = v; }
        void setTarget (double t) noexcept { target = t; }
        inline double next() noexcept
        {
            current = target + coeff * (current - target);
            if (std::fabs (current - target) < 1.0e-4) current = target;
            return current;
        }
        double value() const noexcept { return current; }
        double targetValue() const noexcept { return target; }

    private:
        double coeff = 0.0, current = 0.0, target = 0.0;
    };

    /** Linear below |x| = 1, then saturates smoothly towards 2: a feedback-loop safety net that never touches normal levels. */
    inline float loopSafety (float x) noexcept
    {
        const float a = std::fabs (x);
        if (a <= 1.0f) return x;
        const float y = 1.0f + ap::fastTanh (a - 1.0f);
        return x < 0.0f ? -y : y;
    }

    /** Replaces NaN / inf with 0 and flushes tiny values (denormal guard) in feedback state. */
    inline float sanitise (float x) noexcept
    {
        if (! (std::fabs (x) < 1.0e6f)) return 0.0f;   // also catches NaN
        return std::fabs (x) < 1.0e-20f ? 0.0f : x;
    }
} // namespace fxdetail

//==============================================================================
enum class DistType : uint8_t { Soft, Hard, Tube, Fold, Crush, Diode, Count };

struct DistortionParams
{
    DistType type = DistType::Soft;
    float drive  = 0.3f;      // 0..1 → input gain 1..32 (log)
    float toneHz = 8000.0f;   // post low-pass
    float mix    = 1.0f;      // 0..1, equal-level dry / wet crossfade
};

/** Waveshaping distortion with six curves, a post tone low-pass and a dry / wet mix.
    Small signals keep unity gain for every curve at every drive. */
class Distortion
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void set (const DistortionParams&);
    void process (float* L, float* R, int numSamples) noexcept;
    bool isBypassed() const noexcept { return mix.isSettledAt (0.0f); }

    /** The memoryless curves (Crush is handled in process()); gain = input gain ≥ 1, output normalised to unity small-signal gain. */
    static float shape (DistType, float x, float gain) noexcept;

private:
    float shapeSample (DistType, float x, float gain, float drive01, float hold) const noexcept;
    void configureTone (float hz) noexcept;

    double sr = 44100.0;
    bool fresh = true;
    DistType type = DistType::Soft, previousType = DistType::Soft;
    fxdetail::LinearRamp drive, toneLog2, mix, typeFade;
    ap::Biquad toneL, toneR;
    float configuredTone = -1.0f;
    ap::DcBlocker dcL, dcR;
    float holdL = 0.0f, holdR = 0.0f, holdPhase = 1.0f;
    int toneCounter = 0;
};

//==============================================================================
struct ChorusParams
{
    float rateHz   = 0.8f;
    float depth    = 0.4f;   // 0..1 → 0..5 ms modulation
    float feedback = 0.1f;   // 0..1
    float mix      = 0.5f;   // 0..1, equal-level crossfade
};

/** Three-voice chorus (7 / 12 / 19 ms, LFO phases 0 / 120 / 240°, right channel +90°). */
class Chorus
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void set (const ChorusParams&);
    void process (float* L, float* R, int numSamples) noexcept;
    bool isBypassed() const noexcept { return mix.isSettledAt (0.0f); }

private:
    double sr = 44100.0;
    bool fresh = true;
    fxdetail::LinearRamp rate, depth, feedback, mix;
    ap::DelayLine lineL, lineR;
    double phase = 0.0;
    float lastL = 0.0f, lastR = 0.0f;
};

//==============================================================================
struct PhaserParams
{
    float rateHz   = 0.3f;
    float depth    = 0.6f;   // 0..1 sweep width (full = 200 Hz .. 8 kHz)
    float feedback = 0.3f;   // 0..1
    int   stages   = 6;      // 4, 6, 8 or 12
    float mix      = 0.5f;
};

/** First-order all-pass phaser, exponential sweep, quadrature LFO on the right channel. */
class Phaser
{
public:
    static constexpr int kMaxStages = 12;

    void prepare (double sampleRate, int maxBlock);
    void reset();
    void set (const PhaserParams&);
    void process (float* L, float* R, int numSamples) noexcept;
    bool isBypassed() const noexcept { return mix.isSettledAt (0.0f); }

private:
    float runChain (std::array<float, kMaxStages>& state, float x, float coeff, float& tapOld, float& tapNew) const noexcept;

    double sr = 44100.0;
    bool fresh = true;
    fxdetail::LinearRamp rate, depth, feedback, mix, stageFade;
    int stages = 6, previousStages = 6;
    std::array<float, kMaxStages> stateL {}, stateR {};
    float lastL = 0.0f, lastR = 0.0f;
    double phase = 0.0;
};

//==============================================================================
struct FlangerParams
{
    float rateHz   = 0.25f;
    float depth    = 0.5f;   // 0..1: sweep 0.5 ms .. 0.5 + 7.5 × depth ms
    float feedback = 0.4f;   // −1..1
    float mix      = 0.5f;
};

/** Short modulated delay with bipolar feedback; the right channel's LFO is inverted. */
class Flanger
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void set (const FlangerParams&);
    void process (float* L, float* R, int numSamples) noexcept;
    bool isBypassed() const noexcept { return mix.isSettledAt (0.0f); }

private:
    double sr = 44100.0;
    bool fresh = true;
    fxdetail::LinearRamp rate, depth, feedback, mix;
    ap::DelayLine lineL, lineR;
    double phase = 0.0;
};

//==============================================================================
struct DelayParams
{
    float timeMs = 375.0f;
    bool sync = false;
    ap::NoteDivision division = ap::NoteDivision::Eighth;
    float feedback  = 0.35f;   // 0..1 (loop gain, capped at 0.98)
    bool pingPong   = true;
    float lowCutHz  = 100.0f;  // high-pass in the loop
    float highCutHz = 8000.0f; // low-pass in the loop
    float mix = 0.3f;          // wet level over the full dry signal
};

/** Stereo / ping-pong delay with filtered feedback; time changes glide (50 ms slew). */
class StereoDelay
{
public:
    static constexpr float kMaxSeconds = 2.1f;

    void prepare (double sampleRate, int maxBlock);
    void reset();
    void set (const DelayParams&, const Transport&);
    void process (float* L, float* R, int numSamples) noexcept;
    bool isBypassed() const noexcept { return mix.isSettledAt (0.0f); }
    float tailSeconds() const noexcept;

    /** Delay time in ms: delay_time, or (sync) division beats × 60 / bpm × 1000 (120 bpm without tempo); clamped 1..2000. */
    static float resolveTimeMs (const DelayParams&, const Transport&) noexcept;

private:
    void configureFilters (float lowHz, float highHz) noexcept;

    double sr = 44100.0;
    bool fresh = true;
    DelayParams params;
    fxdetail::Slew delaySamples;
    fxdetail::LinearRamp feedback, pingPong, mix, lowLog2, highLog2;
    ap::DelayLine lineL, lineR;
    ap::Biquad hpL, hpR, lpL, lpR;
    float configuredLow = -1.0f, configuredHigh = -1.0f;
    int filterCounter = 0;
};

//==============================================================================
struct ReverbParams
{
    float size       = 0.6f;   // 0..1 → delay lengths × 0.5..1.5
    float decay      = 0.5f;   // 0..1 → RT60 0.3..12 s (log)
    float damp       = 0.4f;   // 0..1 → in-loop low-pass 18 kHz .. 1.2 kHz
    float predelayMs = 10.0f;  // 0..200
    float width      = 1.0f;   // 0..1, M/S
    float mix        = 0.25f;  // wet level over the full dry signal
};

/** 8-line feedback delay network (Hadamard mixing) with input diffusion, gentle line modulation, damping and pre-delay. */
class Reverb
{
public:
    static constexpr int kLines = 8;
    static constexpr int kDiffusers = 4;

    void prepare (double sampleRate, int maxBlock);
    void reset();
    void set (const ReverbParams&);
    void process (float* L, float* R, int numSamples) noexcept;
    bool isBypassed() const noexcept { return mix.isSettledAt (0.0f); }
    float tailSeconds() const noexcept;

    static float rt60Seconds (float decay01) noexcept;

private:
    struct Allpass
    {
        ap::DelayLine line;
        int length = 1;
        inline float process (float x, float g) noexcept
        {
            const float delayed = line.read (length - 1);
            const float w = x + g * delayed;
            line.push (w);
            return delayed - g * w;
        }
    };

    void updateLoopGains() noexcept;

    double sr = 44100.0;
    bool fresh = true;
    ReverbParams params;
    std::array<ap::DelayLine, kLines> lines;
    std::array<fxdetail::Slew, kLines> lengths;          // samples (size), slewed
    std::array<float, kLines> loopGain {}, lp {}, modRate {}, modPhase {};
    std::array<Allpass, kDiffusers> diffL, diffR;
    ap::DelayLine predelayL, predelayR;
    fxdetail::Slew predelay;
    fxdetail::LinearRamp decayRamp, dampRamp, width, mix;
    float dampCoeff = 0.0f, modDepth = 0.0f;
    int gainCounter = 0;
};

//==============================================================================
struct EqParams
{
    float lowGainDb  = 0.0f, lowHz  = 120.0f;
    float midGainDb  = 0.0f, midHz  = 1000.0f, midQ = 0.8f;
    float highGainDb = 0.0f, highHz = 8000.0f;
};

/** Low shelf, mid peak, high shelf. The shelf frequencies are the turnover corners (the shelf's
    half-gain point moves with gain like an analog shelf). Bands at 0 dB are skipped (exact). */
class ThreeBandEq
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void set (const EqParams&);
    void process (float* L, float* R, int numSamples) noexcept;
    bool isBypassed() const noexcept;

    /** The RBJ midpoint used for a shelf whose turnover corner is `cornerHz` at `gainDb` (high shelf: mirrored). */
    static float shelfMidpointHz (float cornerHz, float gainDb, bool highShelf) noexcept;

private:
    struct Band
    {
        ap::Biquad l, r;
        fxdetail::LinearRamp gainDb, freqLog2, q;
        float cfgGain = 1.0e9f, cfgFreq = -1.0f, cfgQ = -1.0f;
        bool settledAtZero() const noexcept { return gainDb.isSettledAt (0.0f); }
    };

    void configure (Band&, ap::Biquad::Type, bool force) noexcept;

    double sr = 44100.0;
    bool fresh = true;
    std::array<Band, 3> bands;
};

//==============================================================================
struct CompressorParams
{
    float thresholdDb = -18.0f;
    float ratio       = 4.0f;
    float attackMs    = 10.0f;
    float releaseMs   = 100.0f;
    float makeupDb    = 0.0f;
    float mix         = 1.0f;   // parallel (dry / compressed)
};

/** Feed-forward compressor: stereo-linked peak / RMS hybrid detector (5 ms RMS calibrated to
    read a sine's peak level, plus a fast peak follower for transients), 6 dB soft knee, attack /
    release one-poles in the dB domain, makeup and parallel mix. */
class Compressor
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void set (const CompressorParams&);
    void process (float* L, float* R, int numSamples) noexcept;
    bool isBypassed() const noexcept { return mix.isSettledAt (0.0f); }
    float gainReductionDb() const noexcept { return grDb; }

private:
    double sr = 44100.0;
    bool fresh = true;
    fxdetail::LinearRamp threshold, ratio, makeup, mix;
    float attackCoeff = 0.0f, releaseCoeff = 0.0f, rmsCoeff = 0.0f, peakCoeff = 0.0f;
    float meanSquare = 0.0f, peakEnv = 0.0f, grDb = 0.0f;
};

} // namespace verdant
