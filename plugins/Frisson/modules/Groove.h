#pragma once

// GROOVE — transient shaper with a beat-aware "pocket".
//
// Listener response: beat perception and the urge to move are driven by
// onsets. Sharper, more consistent onsets make the pulse easier to lock onto
// (Witek et al. 2014: groove peaks with a clear pulse plus moderate
// syncopation). GROOVE cannot add syncopation, but it can clarify the pulse
// without flattening what sits between the beats.
//
// DSP (per sample, no latency):
//   * ap::TransientDetector (default params) on the mono sum 0.5 (L + R).
//   * attackDb  = 6 x amount x transientMul x attackAmount() x pocketWeight
//     sustainDb = -3 x amount x sustainAmount()
//     One wideband gain dB->lin (attackDb + sustainDb), smoothed by a 1 ms
//     one-pole so the first milliseconds of a hit are not missed, applied to
//     both channels alike (the stereo image does not move).
//   * "Snap": a 2 kHz high shelf per channel whose gain is 0.5 x attackDb, so
//     onsets gain more edge than body. Shelf gain is smoothed per sample and the
//     biquads are redesigned every 32 samples.
//   * Pocket (default on): while the host is playing with a known tempo, each
//     detected onset is weighted 1.3 when it lands within +/-25 ms of a 1/16
//     grid line and 0.8 otherwise; the weight holds until the next onset. Heard
//     as "tighter", not "quantised". Without tempo the weight is 1.
//
// Everything the detector reads is a ratio of envelopes, so the behaviour is
// level independent. A steady signal (no onsets) passes unchanged.
// setAmount (0) is a bit-exact bypass once the knob has faded out; the detector
// keeps running while bypassed so turning the knob up never sees a stale onset.

#include "modules/Module.h"

#include <ap/Filters.h>
#include <ap/TransientDetector.h>

#include <atomic>

namespace frisson
{

class GrooveModule final : public Module
{
public:
    // Gain law and pocket constants (PRD R2–R5).
    static constexpr float  kAttackRangeDb      = 6.0f;
    static constexpr float  kSustainRangeDb     = -3.0f;
    static constexpr float  kSnapShelfHz        = 2000.0f;
    static constexpr float  kSnapShelfQ         = 0.7071f;
    static constexpr float  kSnapRatio          = 0.5f;     // shelf dB per attack dB
    static constexpr int    kShelfUpdateSamples = 32;
    static constexpr float  kGainSmoothMs       = 1.0f;
    static constexpr float  kAmountSmoothMs     = 20.0f;
    static constexpr float  kMinTransientMul    = 0.5f;
    static constexpr float  kMaxTransientMul    = 2.0f;
    static constexpr double kGridBeats          = 0.25;     // 1/16 note in quarter-note beats
    static constexpr double kPocketWindowSec    = 0.025;
    static constexpr float  kOnGridWeight       = 1.3f;
    static constexpr float  kOffGridWeight      = 0.8f;

    GrooveModule() = default;

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;

    void setAmount (float amount) override;
    float getAmount() const override { return amountTarget; }

    void setModulation (const Modulation& m) override;
    void setTransport (const Transport& t) override;

    void process (float* left, float* right, int numSamples) override;

    int latencySamples() const override { return 0; }

    /** Peak attack emphasis of the last block, normalised: (attackDb x weight) / 6, clamped 0..1. */
    float activity() const override { return activityValue.load (std::memory_order_relaxed); }

    const char* name() const override { return "Groove"; }

    /** Beat-aware onset weighting (processor parameter `groove_pocket`). Default on. */
    void setPocketEnabled (bool shouldBeEnabled) noexcept { pocketEnabled = shouldBeEnabled; }
    bool isPocketEnabled() const noexcept                 { return pocketEnabled; }

    /** Pocket weight for an onset at the given transport position (exposed for tests). */
    static float gridWeight (double ppq, double bpm) noexcept;

private:
    float effectiveAmount() const noexcept;
    void enterBypass() noexcept;
    bool pocketActive() const noexcept;

    double sr = 48000.0;

    ap::TransientDetector detector;
    ap::Biquad shelfL, shelfR;
    ap::Smoother amountSmoother, mulSmoother, gainSmoother, shelfSmoother;

    float amountTarget = 0.0f;
    float transientMul = 1.0f;
    float weight = 1.0f;
    float lastShelfDb = 0.0f;
    int   shelfCountdown = 0;
    bool  bypassed = true;
    bool  fresh = true;            // no block processed since reset(): settings snap, no fade
    bool  pocketEnabled = true;

    Transport transport;
    long long samplesSinceTransport = 0;

    std::atomic<float> activityValue { 0.0f };
};

} // namespace frisson
