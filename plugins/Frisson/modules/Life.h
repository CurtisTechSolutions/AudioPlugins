#pragma once

// LIFE — slow, correlated 1/f drift for the rest of the Frisson chain.
//
// The response it targets: pitch and loudness fluctuations in music follow a
// 1/f spectrum (Voss & Clarke 1975) — slow drift dominates, fast jitter is
// rare — and that mix of predictability and surprise is what the ear hears as
// "alive". Analogue gear drifts the same way (thermal drift, tape wow,
// component tolerance). Perfectly static sounds like a machine; white-noise
// modulation sounds like a fault. LIFE supplies the in-between.
//
// DSP: six independent ap::DriftSource generators (Voss–McCartney octave rows,
// fastest row 2 Hz × rate, six octaves below it), each advanced once per
// control step (= one host block in the normal case; a time accumulator keeps
// the rate correct when blocks vary). Each source gets a second one-pole at
// half the fastest-row rate and a slew bound (full ±1 swing in no less than
// 0.5 s / rate), so the trajectory never steps: the lockstep row updates in
// DriftSource alone would move eqFreqMul by up to ~0.35 % in one block.
// Then, with a = amount (linear 250 ms de-zipper) and d1..d6 in −1..1:
//
//     eqFreqMul    = 2^(0.03 · a · d1)      ±2.1 % EQ centre frequency
//     driveMul     = 1 + 0.10 · a · d2      ±10 % saturation drive
//     widthMul     = 1 + 0.05 · a · d3      ±5 % stereo width
//     gainL        = dB(0.2 · a · d4)       ±0.2 dB, left
//     gainR        = dB(0.2 · a · d5)       ±0.2 dB, right (independent of left)
//     transientMul = 1 + 0.10 · a · d6      ±10 % attack emphasis
//     airDb = 0, densityMul = 1
//
// These depths are deliberately at or just below the threshold of hearing a
// change in isolation (a 2 % centre shift on a broad EQ, 0.2 dB). The effect
// is felt as "breathing", never heard as wobble. Do NOT "improve" them upward:
// larger values turn hardware drift into an audible chorus/tremolo.
//
// LIFE is a clock, not a listener: analyze() never reads the audio buffers.
// Output is deterministic for a given seed; reset() restarts the seeded series.

#include "modules/Module.h"

#include <ap/PinkNoise.h>

#include <array>
#include <atomic>
#include <cstdint>

namespace frisson
{

class LifeModulator final : public Modulator
{
public:
    static constexpr int      kNumSources  = 6;
    static constexpr uint32_t kDefaultSeed = 0x4C494645u;   // "LIFE"

    // Full-scale depths (amount = 1). See the header comment before changing.
    static constexpr float kEqOctaves      = 0.03f;   // 2^±0.03 → ±2.1 %
    static constexpr float kDriveDepth     = 0.10f;
    static constexpr float kWidthDepth     = 0.05f;
    static constexpr float kGainDb         = 0.20f;
    static constexpr float kTransientDepth = 0.10f;

    static constexpr double kFastestHz     = 2.0;     // fastest drift row at rate 1
    static constexpr float  kMinRate       = 0.5f;
    static constexpr float  kMaxRate       = 2.0f;

    LifeModulator();

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;

    void setAmount (float amount) override;
    float getAmount() const override;

    /** Ignores the audio (may be null); advances the drift by numSamples. */
    Modulation analyze (const float* left, const float* right, int numSamples) noexcept override;

    /** |d1|, the EQ drift (0..1); 0 while the knob is off. */
    float activity() const override;

    const char* name() const override { return "Life"; }

    /** Drift speed multiplier, clamped to 0.5..2 (default 1). Takes effect next block. */
    void setRate (float rateMultiplier);
    float getRate() const;

    /** Selects the drift series. Applied at the next reset()/prepare(), or at the
        next analyze() if already running (the output glides, it never jumps).
        Give each plugin instance its own seed so tracks don't drift in lockstep. */
    void setSeed (uint32_t newSeed);
    uint32_t getSeed() const;

    /** Samples per drift update (normally the host block size). */
    int getControlStepSamples() const noexcept { return stepSamples; }

private:
    void configure() noexcept;
    void restartSources() noexcept;
    void trackBlockSize (int numSamples) noexcept;
    void applyPendingSettings() noexcept;
    void stepSources (float targetAmount) noexcept;

    static uint32_t sourceSeed (uint32_t base, int index) noexcept;

    double sampleRate   = 48000.0;
    int preparedBlock   = 512;    // nominal block size given to prepare()
    int nominalBlock    = 512;    // block size the control rate currently derives from
    int stepSamples     = 512;    // samples per drift update (nominal, clamped)
    int pendingSamples  = 0;      // time accumulator
    int mismatchBlocks  = 0;      // consecutive blocks > 2× away from nominalBlock

    std::atomic<float>    amountTarget  { 0.0f };
    std::atomic<float>    rateTarget    { 1.0f };
    std::atomic<uint32_t> seedTarget    { kDefaultSeed };
    std::atomic<float>    activityLevel { 0.0f };

    float appliedRate = 1.0f;
    uint32_t seed = kDefaultSeed;
    bool snapAmount = true;

    std::array<ap::DriftSource, kNumSources> sources;
    std::array<float, kNumSources> lowpass {};   // second smoothing pole
    std::array<float, kNumSources> drift {};     // slew-bounded output, −1..1
    float lowpassCoeff = 0.0f;
    float maxStepPerUpdate = 1.0f;
    float amountCurrent = 0.0f;                  // linearly de-zippered knob
    float amountStepPerUpdate = 1.0f;
};

} // namespace frisson
