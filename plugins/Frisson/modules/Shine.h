#pragma once

// SHINE — the top-end sheen that reads as "mastered".
//
// Listener response: the ear is least sensitive above ~10 kHz (ISO 226), so
// energy there can be added generously before it hurts, and listeners link it
// with expensive playback and finished masters. An exciter (Aphex Aural
// Exciter) generates *new* harmonics from the existing high content, which
// restores "detail" to dull sources in a way EQ cannot (EQ can only boost what
// is there). CHILLS leans on this knob through Modulation::airDb.
//
// DSP, per channel, no latency:
//   1. e = LR4 high-pass at 6 kHz (two cascaded Butterworth sections: the high
//      output of ap::LinkwitzRiley4 without computing the unused low band).
//   2. Level normalisation: e is scaled by kDriveRef / env before the shaper
//      and back by env / kDriveRef after it. env is a stereo-linked peak
//      follower on e (instant attack, 50 ms release), so the shaper always sees
//      the high band peaking at kDriveRef and the harmonic-to-source ratio is
//      the same on a -30 dBFS track and a -6 dBFS mix. kDriveRef is what the
//      un-normalised design would see for a -12 dBFS 7 kHz sine.
//   3. ap::ExciterShaper, drive = 1 + 3 x amount. Only its even part,
//      (S(u) + S(-u)) / 2, is kept: the 2nd harmonics and even intermodulation
//      the shaper generated. The odd part is mostly the source band itself;
//      mixing it back in would comb-filter the top octave against the dry path,
//      because the 6k + 8k high-pass chain turns its phase by up to 360 deg
//      (about -5 dB at 12 kHz at amount 1, which would cancel the air shelf).
//   4. LR4 high-pass at 8 kHz removes the shaper's DC and the intermodulation
//      that falls below it (difference tones, envelope ripple).
//   5. Added to the signal at amount x (-12 dB + 6 dB x amount).
//   6. Air shelf, gain (2.5 + clamp (airDb, 0, 4)) x amount dB, Q 0.707. Its
//      corner, 11 kHz x eqFreqMul (clamped 0.8..1.25), is where the shelf has
//      reached ~85 % of its boost; the RBJ design (half-gain) frequency sits at
//      kShelfMidRatio x corner. With the half-gain point at 11 kHz itself, a
//      12 kHz tone would get only 1.6 of the 2.5 dB. The shelf is the RBJ
//      transfer function realised as a trapezoidal SVF (Simper), whose state
//      survives coefficient changes: the TDF-II ap::Biquad leaves a -36 dB
//      transient on low-frequency content each time the shelf gain moves.
//
// Amount, airDb and eqFreqMul are smoothed on a 32-sample control grid; the
// shelf is redesigned there only when it moved, and the exciter mix and drive
// are interpolated linearly across each grid step, so nothing zippers.
// setAmount (0) is a bit-exact bypass once the amount ramp has settled (airDb
// and eqFreqMul are ignored while bypassed).
//
// activity(): exciter output (mono sum, after the mix gain) relative to the
// input high band (mono sum of e), both power-smoothed over ~100 ms, mapped
// linearly in dB from -80 dB (0) to -20 dB (1). 0 when bypassed or when the
// exciter path is silent: high band below -140 dBFS or more than 60 dB under
// the full-band input (a bass-only track).

#include "modules/Module.h"

#include <ap/Dynamics.h>
#include <ap/Filters.h>

#include <atomic>

namespace frisson
{

class ShineModule final : public Module
{
public:
    // PRD R2 / R3 constants.
    static constexpr float kExciteSplitHz   = 6000.0f;   // LR4 high-pass feeding the shaper
    static constexpr float kExciteCleanHz   = 8000.0f;   // LR4 high-pass after the shaper
    static constexpr float kDriveRange      = 3.0f;      // drive = 1 + 3 x amount
    static constexpr float kEvenWeight      = 0.4f;      // ExciterShaper x^2 weight
    static constexpr float kDriveRef        = 0.16f;     // normalised peak level the shaper sees
    static constexpr float kMixBaseDb       = -12.0f;    // exciter mix (-12 dB + 6 dB x amount) x amount
    static constexpr float kMixRangeDb      = 6.0f;
    static constexpr float kShelfCornerHz   = 11000.0f;
    static constexpr float kShelfMidRatio   = 0.72f;     // RBJ design frequency / corner
    static constexpr float kShelfQ          = 0.7071f;
    static constexpr float kShelfBaseDb     = 2.5f;
    static constexpr float kMaxAirDb        = 4.0f;
    static constexpr float kFreqMulMin      = 0.8f;
    static constexpr float kFreqMulMax      = 1.25f;
    static constexpr int   kControlInterval = 32;

    ShineModule() = default;

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;

    void setAmount (float amount) override;
    float getAmount() const override { return amountTarget; }

    /** Reads airDb and eqFreqMul; everything else is ignored. */
    void setModulation (const Modulation& m) override;

    void process (float* left, float* right, int numSamples) override;

    int latencySamples() const override { return 0; }

    /** Exciter output vs input high band, 0..1. Safe to read from the UI thread. */
    float activity() const override { return activityValue.load (std::memory_order_relaxed); }

    const char* name() const override { return "Shine"; }

private:
    /** LR4 high-pass: two cascaded Butterworth sections. */
    struct HighPass4
    {
        ap::Biquad s1, s2;

        void configure (double sampleRate, float hz) noexcept;
        void reset() noexcept { s1.reset(); s2.reset(); }
        inline float process (float x) noexcept { return s2.process (s1.process (x)); }
    };

    /** RBJ high shelf realised as a trapezoidal state-variable filter (modulation-safe). */
    struct ShelfCoeffs
    {
        float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
        float m0 = 1.0f, m1 = 0.0f, m2 = 0.0f;
    };

    struct Shelf
    {
        float ic1 = 0.0f, ic2 = 0.0f;

        void reset() noexcept { ic1 = ic2 = 0.0f; }

        inline float process (float x, const ShelfCoeffs& c) noexcept
        {
            const float v3 = x - ic2;
            const float v1 = c.a1 * ic1 + c.a2 * v3;
            const float v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            return c.m0 * x + c.m1 * v1 + c.m2 * v2;
        }
    };

    struct Channel
    {
        HighPass4 split, clean;
        Shelf     shelf;
    };

    void updateControl() noexcept;
    void applyShelf (float gainDb, float freqMul) noexcept;
    void resetState() noexcept;
    void wakeUp (float firstL, float firstR) noexcept;

    static float mixForAmount (float amount) noexcept;

    double sr = 48000.0;

    float amountTarget  = 0.0f;
    float freqMulTarget = 1.0f;
    float airTarget     = 0.0f;
    ap::Smoother amountSm, freqMulSm, airSm;     // run at control rate

    ShelfCoeffs shelfCoeffs;
    float appliedShelfDb = 0.0f;
    float appliedFreqMul = 1.0f;

    // Exciter mix and drive, linearly interpolated across each control step.
    float mix = 0.0f, mixStep = 0.0f;
    float drive = 1.0f, driveStep = 0.0f;

    Channel channels[kMaxChannels];
    ap::EnvelopeFollower bandEnv;                // stereo-linked peak of e (normalisation)

    // Activity: per-step energy accumulators and their control-rate smoothing.
    float inputAcc = 0.0f, bandAcc = 0.0f, exciterAcc = 0.0f;
    float inputPow = 0.0f, bandPow = 0.0f, exciterPow = 0.0f;
    float activityCoeff = 0.0f;
    int   accCount = 0;

    int  controlCountdown = 0;
    bool bypassed = true;

    std::atomic<float> activityValue { 0.0f };
};

} // namespace frisson
