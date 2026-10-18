#pragma once

// SPACE — "I am inside the sound".
//
// Listener response: listener envelopment and apparent source width are among
// the strongest predictors of preference in concert-hall research (Beranek).
// Width above ~300 Hz reads as spaciousness; width below it adds nothing
// audible, costs headroom and cancels in mono. Short, sparse, decorrelated
// early reflections (10-25 ms) suggest a room without audible reverb.
//
// DSP (no latency, the direct path is never delayed):
//   1. Width. M = (L+R)/2, S = (L-R)/2. S_high = LR4 high-pass of S at
//      monoBelowHz (60..300, default 120); S_low = S - S_high, so the split is
//      exactly complementary. S' = S_low + width x shelf(S_high), with
//      width = 1 + 0.8 x amount x widthMul (widthMul clamped 0.5..2 and the
//      total clamped to 2.2) and shelf = +2 dB x amount high shelf at 8 kHz,
//      Q 0.707 (a brighter side image). M is never touched, so the mono fold
//      down of the width stage is exact.
//   2. Room. Each output channel gets two early reflections from its own
//      delay line, fed with the mono input biased toward the opposite side
//      (L taps hear 0.25 L + 0.75 R, R taps the mirror; for dual mono both
//      are just M): L at 11.3 / 17.9 ms, R at 13.7 / 23.1 ms, the second tap
//      polarity-inverted. The tap sum is darkened by a -6 dB high shelf at
//      5 kHz and scaled by -18 dB x amount x room (linear). Different delays
//      per side make the reflections decorrelated by construction.
//
// amount, widthMul and room are smoothed per sample (20-30 ms); the side
// shelf gain and the split frequency are recomputed on a 32-sample control
// grid. Once the amount ramp has settled at 0 the module is an exact bypass
// (widthMul and room are ignored); filter and delay states are cleared on the
// way in so re-engaging never replays stale audio.

#include "modules/Module.h"

#include <ap/Crossover.h>
#include <ap/DelayLine.h>
#include <ap/Filters.h>

#include <atomic>

namespace frisson
{

class SpaceModule final : public Module
{
public:
    SpaceModule() = default;

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;

    void setAmount (float amount) override;
    float getAmount() const override { return amountTarget; }

    /** Reads widthMul only (clamped 0.5..2). */
    void setModulation (const Modulation& m) override;

    void process (float* left, float* right, int numSamples) override;

    int latencySamples() const override { return 0; }

    /** (width - 1) / 1.2, clamped 0..1. Safe to read from the UI thread. */
    float activity() const override { return activityValue.load (std::memory_order_relaxed); }

    const char* name() const override { return "Space"; }

    /** Side content below this frequency stays at its original width. 60..300 Hz, default 120. */
    void setMonoBelowHz (float hz);
    float getMonoBelowHz() const noexcept { return monoHzTarget; }

    /** Early-reflection level, 0..1, default 0.3. */
    void setRoom (float room);
    float getRoom() const noexcept { return roomTarget; }

private:
    void updateControl() noexcept;
    void applySideShelf (float amount) noexcept;
    void clearState() noexcept;
    void snapSecondary() noexcept;
    void publishActivity (float amount, float widthMul) noexcept;

    static constexpr int kControlInterval = 32;

    double sr = 48000.0;

    float amountTarget   = 0.0f;
    float widthMulTarget = 1.0f;
    float roomTarget     = 0.3f;
    float monoHzTarget   = 120.0f;

    ap::Smoother amountSm, widthMulSm, roomSm;   // per sample
    ap::Smoother monoHzSm;                       // control rate

    float appliedShelfAmount = -1.0f;
    float appliedMonoHz      = -1.0f;

    ap::LinkwitzRiley4 splitter;     // only its high-pass output is used
    ap::Biquad sideShelf;            // +2 dB x amount at 8 kHz on S_high
    ap::Biquad tapShelf[kMaxChannels];

    ap::DelayLine roomLine[kMaxChannels];
    int tapDelay[kMaxChannels][2] = { { 1, 2 }, { 1, 2 } };

    int  controlCountdown = 0;
    bool bypassed = true;

    std::atomic<float> activityValue { 0.0f };
};

} // namespace frisson
