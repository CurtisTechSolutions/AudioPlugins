#pragma once

// CHILLS — lean the mix into the moments the music is building.
//
// The response it targets: musical chills (frisson) are reliably set off by a
// crescendo, a sudden dynamic change and a new voice entering (Sloboda 1991;
// Grewe et al. 2007; Huron 2006), and the reward response peaks at the moment
// of arrival after rising anticipation (Salimpoor et al. 2011). CHILLS cannot
// write that moment, but it can hear it coming and make the peak a little
// bigger than the build: for a few seconds it asks SHINE for more air, SPACE
// for more width and RUSH for more density, then relaxes. It only amplifies
// cues the music already gives; steady material never moves it.
//
// DSP (analysis only, never touches audio). The mono sum of the dry input is
// analysed on a fixed 5 ms hop grid, so behaviour does not depend on the host
// block size (37-sample and 4096-sample blocks give the same decisions):
//
//   * Loudness: mean square per hop into two one-pole envelopes, 400 ms
//     (L_fast) and 3 s (L_slow), in dB. Both start as running means, so a cold
//     start does not look like a crescendo.
//   * Spectral novelty: six RBJ band-passes (100, 300, 1k, 3k, 8k, 14k Hz,
//     Q 1), each into a 50 ms-release peak follower; the per-hop maximum is
//     taken in dB and floored 60 dB under L_fast (an empty band is not "new"
//     just because its noise floor wobbles).
//     flux = Σ_bands max (0, env_now − baseline), where baseline is the band's
//     highest value between 300 ms and 8 s ago (kept as 50 ms segment maxima).
//     DEVIATION from PRD R2, which compares with the single value 300 ms ago:
//     that measure crosses 9 dB about 13 times in 17 s of stationary pink and
//     on most beats of a sparse drum loop, violating R6. "Louder in this band
//     than anything in the last 8 s" keeps the 300 ms look-back and the dB-sum
//     scale, but steady pink tops out near 2 dB of flux, steady drum loops near
//     3 dB, and a new 12 kHz voice reads 11-13 dB. 8 s is one 4-bar phrase at
//     120 bpm, so a looping chord progression is familiar after its first pass.
//
// Triggers (thr = base × (1.5 − sensitivity), sensitivity 0..1, default 0.5):
//   crescendo  L_fast − L_slow > 3 dB, and L_fast has been rising through the
//              last 500 ms: strictly increasing at 125 ms checkpoints and up by
//              at least thr / 6 in total. A drum hit jumps and then decays, so
//              a loop's ripple does not pass; a build does.
//   novelty    flux > 9 dB.
// Envelope e (0..1) on a trigger: linear attack to 1 in 150 ms (from wherever
// e is), hold 1 s, exponential release reaching 5 % after 3 s (τ = 1 s), then
// exactly 0 below 0.001. Refractory 4 s from each trigger (retriggers ignored).
//
// Silence never triggers: hops under −100 dBFS are not analysed. After 2 s of
// such silence (and after reset) the analyser forgets its context and listens
// for 1 s without triggering when sound returns, so pressing play is not
// mistaken for an arrival; a shorter break (a bar of silence before the drop)
// keeps the context, so the drop still counts. Every decision is a dB difference, so a −50 dBFS
// track and a −6 dBFS master behave alike.
//
// Output per block (a = amount, 100 ms linear de-zipper, e as above):
//   airDb = 3·a·e,  widthMul = 1 + 0.3·a·e,  densityMul = 1 + 0.25·a·e,
//   everything else identity. Knob at 0 → exactly Modulation::identity() and
//   activity 0 (the analysis keeps running, so turning it up needs no warm-up).

#include "modules/Module.h"

#include <ap/Dynamics.h>
#include <ap/Filters.h>

#include <array>
#include <atomic>
#include <vector>

namespace frisson
{

class ChillsModulator final : public Modulator
{
public:
    static constexpr int kNumBands = 6;

    // R5 depths at amount 1, e 1.
    static constexpr float kAirDb        = 3.0f;
    static constexpr float kWidthDepth   = 0.30f;
    static constexpr float kDensityDepth = 0.25f;

    // R3 base thresholds (sensitivity 0.5).
    static constexpr float kCrescendoBaseDb = 3.0f;
    static constexpr float kNoveltyBaseDb   = 9.0f;

    // R4 response shape.
    static constexpr double kAttackSeconds     = 0.15;
    static constexpr double kHoldSeconds       = 1.0;
    static constexpr double kReleaseSeconds    = 3.0;    // to 5 % (three time constants)
    static constexpr double kRefractorySeconds = 4.0;

    ChillsModulator() = default;

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;

    void setAmount (float amount) override;
    float getAmount() const override;

    /** 0..1, default 0.5. Scales both thresholds by (1.5 − sensitivity). Thread-safe. */
    void setSensitivity (float sensitivity);
    float getSensitivity() const;

    /** Reads the dry block (right may be null for mono) and returns the modulation for the next block. */
    Modulation analyze (const float* left, const float* right, int numSamples) noexcept override;

    /** The trigger envelope e, 0..1; 0 whenever the knob is off. Thread-safe. */
    float activity() const override;

    const char* name() const override { return "Chills"; }

    // Diagnostics for tests and metering (audio thread): values at the latest hop.
    int   triggerCount() const noexcept  { return triggers; }
    float levelDeltaDb() const noexcept  { return lastDeltaDb; }   // L_fast − L_slow
    float noveltyFluxDb() const noexcept { return lastFluxDb; }
    float envelope() const noexcept      { return env; }           // e, independent of amount

private:
    enum class Stage { Idle, Attack, Hold, Release };

    using BandValues = std::array<float, kNumBands>;

    void configure() noexcept;
    void resetState() noexcept;
    void restartContext() noexcept;
    void endHop() noexcept;
    void pushSegment() noexcept;
    bool risingFor (float lFast, float minRiseDb) const noexcept;
    void startTrigger() noexcept;
    void advanceEnvelope() noexcept;
    void forceIdle() noexcept;

    double sr = 48000.0;

    // Hop grid.
    int    hopSize = 240;
    int    hopFill = 0;
    double hopSumSq = 0.0;
    double hopSeconds = 0.005;
    BandValues hopBandPeak {};                       // linear, max of each band envelope over the hop

    // Loudness envelopes (power domain).
    double fastPow = 0.0, slowPow = 0.0;
    double fastWeight = 0.0, slowWeight = 0.0;      // 1 − pole
    long   levelHops = 0;

    // Bands.
    std::array<ap::Biquad, kNumBands>           bands;
    std::array<ap::EnvelopeFollower, kNumBands> bandEnv;

    // L_fast history, one value per hop, for the 500 ms rise check.
    std::vector<float> levelRing;
    int levelPos = 0, levelValid = 0;
    int riseStepHops = 25, riseSteps = 4;

    // Band history as segment maxima, for the novelty baseline.
    std::vector<BandValues> segRing;
    int segPos = 0, segValid = 0;
    int segHops = 10, segFill = 0;
    int lagSegs = 6, spanSegs = 154;
    BandValues segMax {};                            // the segment being filled, dB
    BandValues baseline {};                          // dB; valid when baselineValid
    bool baselineValid = false;

    // Silence / warm-up.
    bool inSilence = true;
    int  silentHops = 0, silenceResetHops = 400;
    int  warmupLeft = 200, warmupHops = 200;

    // Trigger envelope.
    Stage  stage = Stage::Idle;
    float  env = 0.0f;
    double holdLeft = 0.0, refractoryLeft = 0.0;
    float  attackStep = 0.0f, releaseCoeff = 0.0f;
    int    triggers = 0;

    // Parameters.
    std::atomic<float> amountTarget { 0.0f };
    std::atomic<float> sensitivity  { 0.5f };
    float amountCurrent = 0.0f;
    bool  snapAmount = true;

    float lastDeltaDb = 0.0f, lastFluxDb = 0.0f;
    std::atomic<float> activityValue { 0.0f };
};

} // namespace frisson
