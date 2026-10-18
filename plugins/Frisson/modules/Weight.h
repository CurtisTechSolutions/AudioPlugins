#pragma once

// WEIGHT — makes the low end feel physical and translate to small speakers
// without adding mud.
//
// Listener response: felt bass (20–90 Hz) reads as "big" and "powerful", but
// small speakers cannot reproduce it. The auditory system rebuilds a missing
// fundamental from its harmonics (the effect psychoacoustic bass enhancers use),
// so harmonics of the sub band keep the weight audible on phones and laptops.
// Wide stereo bass smears that weight and cancels in mono, so bass is centred.
//
// DSP, per sample, zero latency:
//   1. M/S encode. The side channel is split by an LR4 crossover at
//      monoBelowHz (60–300 Hz, default 120) and its low band is removed:
//      side' = high + (1 - depth) * low, depth = min (1, 4 * amount), so the
//      bass is fully centred from amount 0.25 up and the knob never jumps.
//      The LR4 halves sum to an all-pass, so partial depths have a smooth,
//      notch-free magnitude response.
//   2. Clean sub lift: low shelf, Q 0.707, gain 4 dB * amount, on L and R after
//      decoding. Its corner (turnover / pole frequency) is held at 70 Hz for any
//      gain, i.e. the RBJ midpoint is 70 * 10^(gain/80) Hz (78.6 Hz at +4 dB):
//      +3.0 dB at 60 Hz, +0.3 dB at 150 Hz, flat in the mids.
//   3. Harmonic generator on the input mid M:
//        b   = LR4 band-pass 40–90 Hz of M
//        env = peak envelope (5 ms / 80 ms) of b
//        n   = clamp (b / max (env, 1e-4), -1, 1)       (unit-level carrier)
//        g   = env * (0.7 * T2(n) + 0.3 * T3(n))         (Chebyshev: pure 2nd + 3rd;
//              the PRD's n|n| is odd-symmetric and yields no 2nd harmonic)
//        h   = LR4 band-pass 80–250 Hz of DC-blocked g
//      h * amount * -12 dB is added equally to L and R. Because the carrier is
//      normalised and then rescaled by its own envelope, the harmonics track
//      the sub level as a ratio: quiet passages get quiet harmonics, never a
//      constant hum, and the result is the same at -30 dBFS and -6 dBFS.
//
// setAmount (0) is an exact bypass (buffer untouched). Leaving and entering
// bypass is crossfaded internally so the side all-pass never clicks.

#include "modules/Module.h"

#include <ap/Crossover.h>
#include <ap/Dynamics.h>
#include <ap/Filters.h>
#include <ap/Saturation.h>

#include <atomic>

namespace frisson
{

class WeightModule final : public Module
{
public:
    static constexpr float kShelfHz        = 70.0f;
    static constexpr float kShelfQ         = 0.70710678f;
    static constexpr float kShelfMaxDb     = 4.0f;
    static constexpr float kHarmonicMixDb  = -12.0f;   // harmonics re the band they came from, amount 1
    static constexpr float kMonoMinHz      = 60.0f;
    static constexpr float kMonoMaxHz      = 300.0f;
    static constexpr float kMonoDefaultHz  = 120.0f;

    WeightModule() = default;

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;

    void setAmount (float amount) override;
    float getAmount() const override { return targetAmount; }

    /** Side content below this frequency is folded to mono. Clamped to 60–300 Hz, smoothed. */
    void setMonoBelowHz (float hz);
    float getMonoBelowHz() const noexcept { return targetMonoHz; }

    void process (float* left, float* right, int numSamples) noexcept override;

    int latencySamples() const override { return 0; }

    /** Level of the generated harmonics relative to the input, 0..1 on a dB scale
        (-54 dB -> 0, -18 dB -> 1), 80 ms smoothed. A ratio, so level-independent;
        0 in bypass. Safe to read from any thread. */
    float activity() const override { return activityLevel.load (std::memory_order_relaxed); }

    const char* name() const override { return "Weight"; }

private:
    /** Two cascaded Butterworth sections: one side of an LR4 crossover. */
    struct Lr4
    {
        ap::Biquad s1, s2;

        void configure (ap::Biquad::Type type, double sr, float hz) noexcept
        {
            s1.configure (type, sr, hz, 0.70710678f);
            s2.configure (type, sr, hz, 0.70710678f);
        }

        void reset() noexcept { s1.reset(); s2.reset(); }

        inline float process (float x) noexcept { return s2.process (s1.process (x)); }
    };

    void resetDsp() noexcept;
    void updateFilters (bool force) noexcept;

    double sampleRate = 48000.0;

    float targetAmount = 0.0f;
    float targetMonoHz = kMonoDefaultHz;

    ap::Smoother amountSm, monoHzSm;
    float engageGain = 0.0f;   // 0 = dry, 1 = fully processed; linear ramp
    float engageStep = 0.0f;
    bool  fresh      = true;   // first block after prepare/reset: jump to targets, no fade
    bool  engaged    = false;  // false = exact bypass, DSP state idle

    float configuredShelfDb = -1.0f;
    float configuredMonoHz  = -1.0f;

    // Mono-below (side channel only).
    ap::LinkwitzRiley4 sideSplit;

    // Sub lift.
    ap::Biquad shelfL, shelfR;

    // Harmonic generator.
    Lr4 bandHp, bandLp;       // 40–90 Hz source band
    Lr4 harmHp, harmLp;       // 80–250 Hz output band
    ap::EnvelopeFollower bandEnv;
    ap::DcBlocker dcBlock;

    float activitySmoothed = 0.0f;
    std::atomic<float> activityLevel { 0.0f };
};

} // namespace frisson
