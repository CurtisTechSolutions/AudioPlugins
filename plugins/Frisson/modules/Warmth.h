#pragma once

// WARMTH: consonant low-mid harmonics, fullness and glue.
//
// Listener response: partials that line up as a harmonic series read as
// consonant and "expensive" (Plomp & Levelt 1965); low-level even harmonics
// reinforce that series (classic tube warmth); a little 150-250 Hz body reads
// as fullness without 300-500 Hz boxiness; and light, slow compression of
// that band keeps it "sitting right" instead of poking out.
//
// DSP, per channel, on the 80-800 Hz band of a phase-coherent LR4 3-band
// split (the low and high bands pass straight through, so with nothing
// happening in the band the output is an all-pass of the input):
//
//   mid ─► TubeShaper (bias 0.5, drive 1 + 2·amount·driveMul) ─► DcBlocker
//       ─► level correction (300 ms RMS in/out, ±3 dB)
//       ─► Peak 200 Hz·eqFreqMul, Q 0.9, +1.5 dB·amount
//       ─► glue: stereo-linked RMS compressor, 30/150 ms, threshold = 3 s band
//          RMS − 3 dB, ratio 1:1..2:1, 6 dB knee, automatic makeup that keeps
//          the band's long-term RMS unchanged
//
// Details beyond the PRD sketch:
//   * The shaper runs on a level-aligned copy of the band (pre-gain so the
//     band's 3 s RMS sits at −18 dBFS, the usual 0 VU alignment; clamped
//     ±24 dB) and the gain is divided back out, so the harmonic colour is
//     the same on a −30 dBFS track and a −6 dBFS mix.
//   * Only the shaper's residual (shaped − clean) is DC-blocked and it is mixed
//     in by sqrt(amount), so harmonics fade in continuously from the knob's
//     zero (the shaper alone would jump straight to drive 1).
//   * Glue makeup follows knob moves at once instead of re-converging over
//     seconds: the soft-knee gain computer's output scales exactly with its
//     slope (1 − 1/ratio), so the module keeps 3 s power-weighted means of the
//     gain a reference 2:1 reduction, scaled by k = 0.25/0.5/0.75/1, would
//     have produced, and interpolates the makeup for the current k. That is
//     the 3 s output/input power ratio for any ratio, not an approximation.
//   * activity() reports the glue's gain reduction minus the constant part.
//     The PRD's threshold (long-term − 3 dB) puts a steady signal at the top
//     of the 6 dB knee, i.e. 1.5 dB of permanent reduction at 2:1 that the
//     makeup cancels; the ring shows the glue reacting to the band poking out
//     above its long-term level, not that constant.
//   * The glue detector and the long-term trackers are linked on the mean of
//     the two channels' powers (a mono sum would let anti-phase content hide).
//     Long-term trackers are gated (−20 dB relative, −100 dBFS absolute) so
//     gaps between phrases do not drag the threshold down and duck the next
//     entrance.
//   * Turning the knob up from 0 resets the DSP and cross-fades (30 ms) from
//     the dry input; turning it to 0 cross-fades back to the dry input, then
//     becomes a bit-exact bypass that costs no CPU.
//
// Latency: none.

#include "modules/Module.h"

#include <ap/Crossover.h>
#include <ap/Dynamics.h>
#include <ap/Filters.h>
#include <ap/Saturation.h>

#include <array>
#include <atomic>

namespace frisson
{

class WarmthModule final : public Module
{
public:
    WarmthModule() = default;

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;

    void setAmount (float amount) override;
    float getAmount() const override { return amountTarget; }

    /** Uses driveMul (clamped 0.5..2) and eqFreqMul (clamped 0.8..1.25). */
    void setModulation (const Modulation& m) override;

    void process (float* left, float* right, int numSamples) override;

    int latencySamples() const override { return 0; }

    /** Glue gain reduction beyond the steady-state reduction a signal at its
        own long-term level gets (see header comment): 0 at 0 dB, 1 at 6 dB,
        meter ballistics 10 ms up / 150 ms down. 0 whenever bypassed. */
    float activity() const override { return activityValue.load (std::memory_order_relaxed); }

    const char* name() const override { return "Warmth"; }

private:
    /** Mean of a power value, updated at control rate: an exact running mean
        until one time constant of data has been seen, then a one-pole average.
        So it is unbiased from the first update instead of ramping up from 0. */
    struct PowerTracker
    {
        double value = 0.0;
        double count = 0.0;
        double span  = 1000.0;   // time constant in updates

        void setTime (double updateRateHz, double seconds) noexcept;
        void reset() noexcept { value = 0.0; count = 0.0; }
        bool hasData() const noexcept { return count > 0.0; }

        inline void update (double x) noexcept
        {
            if (count < span) count += 1.0;
            value += (x - value) / count;
        }

        /** Much slower update, used while the level gate is closed so a lasting
            drop in level is still followed eventually. */
        inline void leak (double x) noexcept
        {
            if (count > 0.0) value += (x - value) / (8.0 * span);
        }
    };

    void activate() noexcept;
    void resetDsp() noexcept;
    void updateControl() noexcept;
    void updateEq (bool force) noexcept;

    static constexpr int kControlInterval = 32;   // samples between control updates

    double sr = 48000.0;
    double controlRate = 1500.0;

    // Knob and modulation
    float amountTarget = 0.0f;
    float driveMulTarget = 1.0f, eqFreqMulTarget = 1.0f;
    bool  active = false;
    float wet = 0.0f, wetStep = 1.0f / 1440.0f;
    ap::Smoother amountSm, driveMulSm;                // audio rate
    ap::Smoother preGainSm, corrSm, grSm, makeupSm;   // audio rate, targets set at control rate
    ap::Smoother eqFreqMulSm;                         // control rate

    // Per-channel DSP
    std::array<ap::ThreeBandSplitter, kMaxChannels> splitter;
    std::array<ap::DcBlocker, kMaxChannels> dcBlock;
    std::array<ap::Biquad, kMaxChannels> fullness;

    // Stereo-linked analysis
    ap::EnvelopeFollower detector;
    PowerTracker shortTerm;            // 50 ms band power, drives the gate
    PowerTracker bandLevel;            // 3 s band power, shaper alignment
    PowerTracker corrIn, corrOut;      // 300 ms shaper in/out power (R6)
    static constexpr int kMakeupGrid = 4;
    PowerTracker gluePow;                                  // 3 s glue input power: threshold (R5)
    std::array<PowerTracker, kMakeupGrid> glueOutPow;      // 3 s output power at k = (j+1)/4
    float  grRefDb = 0.0f;             // reference (2:1) gain reduction applied since the last control step

    // Control-rate accumulators and state
    double accMid = 0.0, accSat = 0.0, accEq = 0.0;
    int    accCount = 0;
    int    controlCountdown = 0;
    float  eqFreqApplied = -1.0f, eqGainApplied = -1.0f;
    float  activityState = 0.0f, activityAttack = 0.0f, activityRelease = 0.0f;
    std::atomic<float> activityValue { 0.0f };
};

} // namespace frisson
