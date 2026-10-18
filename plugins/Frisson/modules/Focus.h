#pragma once

// FOCUS — clarity without fatigue.
//
// Listener response: the ear canal resonance makes 3-4 kHz the most sensitive
// region (ISO 226). A little energy at ~2.2 kHz reads as "forward" and
// intelligible; too much at 3-4 kHz reads as harsh, and 6-8 kHz sibilance is
// the other classic fatigue trigger. 250-450 Hz build-up is heard as "mud"
// that masks everything above it. Clarity is perceived as effortless attention.
//
// DSP, per channel, four peak biquads in series:
//   1. mud cut     330 Hz, Q 1.0, 0..-3 dB x amount      (static)
//   2. presence    2.2 kHz, Q 1.2, 0..+2 dB x amount     (static)
//   3. harsh cut   3.5 kHz, Q 1.5, dynamic                (guard)
//   4. sibilance   7 kHz,   Q 2.0, dynamic                (guard)
// The static centres follow Modulation::eqFreqMul (clamped 0.8..1.25); the
// guard frequencies are fixed because their detectors are.
//
// Guards (feed-forward from the dry input, so no latency and no feedback
// pumping): the mono sum feeds a 4th-order Butterworth band-pass (3-4 kHz or
// 6-8 kHz) and a wideband path; both get a 2 ms / 80 ms peak envelope. The
// ratio r = bandEnv / (wideEnv + eps) is level independent. Above r0 the cut
// depth is 4 dB x amount x (r - r0) / r0, capped at 4 dB (harsh) or
// 4 dB x amount (sibilance). The band-pass is 4th order rather than a single
// RBJ section so a pure 10 kHz or 2.2 kHz tone (e.g. a hi-hat partial or the
// presence region itself) does not leak past r0 and trigger a cut.
//
// Coefficients are only recomputed on a 32-sample control grid: the amount and
// eqFreqMul are smoothed there (static shape), and the guard depths are
// smoothed there before the cut biquads are updated, so nothing zippers.
// setAmount(0) is an exact bypass once the amount ramp has settled.

#include "modules/Module.h"

#include <ap/Dynamics.h>
#include <ap/Filters.h>

#include <atomic>

namespace frisson
{

class FocusModule final : public Module
{
public:
    FocusModule() = default;

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;

    void setAmount (float amount) override;
    float getAmount() const override { return amountTarget; }

    void setModulation (const Modulation& m) override;

    void process (float* left, float* right, int numSamples) override;

    int latencySamples() const override { return 0; }

    /** max (harsh cut, sibilance cut) / 4 dB, 0..1. Safe to read from the UI thread. */
    float activity() const override { return activityValue.load (std::memory_order_relaxed); }

    const char* name() const override { return "Focus"; }

private:
    /** 4th-order Butterworth band-pass (two staggered RBJ band-pass sections), unity at centre. */
    struct BandDetector
    {
        ap::Biquad s1, s2;
        float gain = 1.0f;

        void design (double sampleRate, float lowHz, float highHz) noexcept;
        void reset() noexcept { s1.reset(); s2.reset(); }
        inline float process (float x) noexcept { return gain * s2.process (s1.process (x)); }
    };

    /** One dynamic guard: detector, envelope, smoothed depth and the cut filters. */
    struct Guard
    {
        BandDetector         detector;
        ap::EnvelopeFollower env;
        ap::Smoother         depthDb;          // positive dB of cut
        ap::Biquad           cut[kMaxChannels];
        float r0 = 0.3f, cutHz = 1000.0f, cutQ = 1.0f;
        bool  capByAmount = false;
        float appliedDb = 0.0f;
        float cosW = 1.0f, alpha = 0.0f;       // fixed-frequency cut, precomputed in prepare

        void applyCut (float cutDb) noexcept;
    };

    void updateControl() noexcept;
    void applyStatic (float amount, float freqMul) noexcept;
    float guardTargetDb (const Guard& g, float amount) const noexcept;
    void resetState() noexcept;

    static constexpr int   kControlInterval = 32;
    static constexpr float kMaxCutDb        = 4.0f;

    double sr = 48000.0;

    float amountTarget  = 0.0f;
    float freqMulTarget = 1.0f;
    ap::Smoother amountSm, freqMulSm;          // run at control rate
    float appliedAmount  = -1.0f;
    float appliedFreqMul = -1.0f;

    ap::Biquad mud[kMaxChannels], presence[kMaxChannels];
    ap::EnvelopeFollower wideEnv;
    Guard harsh, sib;

    int  controlCountdown = 0;
    bool bypassed = true;

    std::atomic<float> activityValue { 0.0f };
};

} // namespace frisson
