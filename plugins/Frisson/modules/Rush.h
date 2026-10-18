#pragma once

// RUSH: density and arousal without cheating on loudness.
//
// Listener response: dense, sustained, consistent sound reads as "big" and
// "energetic" and raises arousal even at matched loudness. Parallel ("New
// York") compression supplies that density while the untouched dry path keeps
// the transients (the groove) intact; a slow auto gain then takes back the
// level the parallel sum added, so turning RUSH up changes how dense the sound
// is rather than how loud it is. CHILLS pushes the density up momentarily via
// Modulation::densityMul.
//
// DSP, per sample:
//   in ─► 3-band LR4 split (120 Hz / 2.5 kHz, per channel)
//       ─► per band: band + mix · makeup · g · band
//            g      = 4:1, 6 dB knee on the band's mono sum; RMS detector with a
//                     10 ms window, 10 ms attack / 120 ms release (in dB);
//                     threshold = band long-term RMS (3 s) − 6 dB
//            makeup = long-term RMS (band) / long-term RMS (g · band), 0..+12 dB
//            mix    = amount × densityMul, densityMul 0.5..2, mix 0..1.5
//       ─► Σ bands ─► auto gain: trim = 3 s RMS (in) / 3 s RMS (out), −12..0 dB
//       ─► lookahead peak limiter: 1.5 ms, release 60 ms, ceiling −3..0 dBFS
//
// The limiter delays the processed signal by round (1.5 ms × sr) and reads
// the undelayed processed signal as its lookahead. The gain it applies is a
// boxcar average (one lookahead long) of the release-held sliding minimum of
// the gain each sample needs, so the ceiling is never exceeded and the gain
// never steps. That delay is the module's whole, constant latency. (During the
// 20 ms fade when the knob leaves or returns to 0 the limiter gain fades with
// it, so the ceiling is guaranteed from the end of that fade on.)
//
// Auto gain: the processed power is E[dry²] + 2·mix·E[dry·comp] + mix²·E[comp²];
// each term is a gated 3 s average, so the trim follows a knob (or CHILLS)
// move at once while never reacting to short transients.
//
// Bypass: at amount 0 the input itself runs through the same delay, so the
// output is the input delayed by latencySamples(), bit for bit. The analysis
// keeps running while bypassed, so engaging starts from settled levels; the
// knob crossing zero crossfades over 20 ms so nothing clicks. Long-term
// trackers skip control steps 60 dB under their running level (silence,
// gaps), so a pause does not drag the thresholds down.

#include "modules/Module.h"

#include <ap/Crossover.h>
#include <ap/DelayLine.h>
#include <ap/Dynamics.h>
#include <ap/Filters.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace frisson
{

class RushModule : public Module
{
public:
    RushModule() = default;

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;

    void setAmount (float amount) override;
    float getAmount() const override { return amount; }

    /** Reads densityMul (clamped 0.5..2); everything else is ignored. */
    void setModulation (const Modulation& m) override;

    void process (float* left, float* right, int numSamples) override;

    /** round (1.5 ms × sampleRate) (72 at 48 kHz); constant after prepare(). */
    int latencySamples() const override { return latency; }

    /** max (limiter GR, mean band GR) / 12 dB, clamped 0..1; 0 while bypassed. */
    float activity() const override { return activityValue.load (std::memory_order_relaxed); }

    const char* name() const override { return "Rush"; }

    /** Limiter ceiling in dBFS, clamped to −3..0. Default −0.3. Smoothed (20 ms). */
    void setCeilingDb (float db) noexcept;
    float getCeilingDb() const noexcept { return ceilingDb; }

    /** Slow (3 s) loudness-matching trim after the parallel sum. Default on. Smoothed. */
    void setAutoGain (bool on) noexcept { autoGainOn = on; }
    bool getAutoGain() const noexcept   { return autoGainOn; }

private:
    static constexpr int kBands = 3;
    static constexpr int kControlInterval = 16;   // samples between gain-computer updates

    struct Band
    {
        float  meanSquare = 0.0f;                  // 10 ms RMS window of the mono band
        float  levelDb = -200.0f;                  // detector level after attack/release, dB
        double bandPow = 0.0;                      // 3 s one-pole of mono²
        double compPow = 0.0;                      // 3 s one-pole of (g · mono)²
        double norm = 0.0;                         // start-up bias correction for the 3 s trackers
        float  sumBand = 0.0f, sumComp = 0.0f;     // accumulated over the current control step
        float  g = 1.0f, gStep = 0.0f;             // compressor gain, ramped per sample
        float  makeup = 1.0f, makeupStep = 0.0f;   // makeup gain, ramped per sample
        float  grDb = 0.0f;                        // latest gain reduction in dB (>= 0)
    };

    float mixTarget() const noexcept;
    void controlUpdate (float mix) noexcept;
    float slidingMin (float value) noexcept;

    // Settings
    double sr = 48000.0;
    float amount = 0.0f;
    float densityMul = 1.0f;
    float ceilingDb = -0.3f;
    bool autoGainOn = true;

    // Derived at prepare()
    int latency = 72;
    int window = 73;              // latency + 1: lookahead window and boxcar length
    float engageStep = 1.0f / 960.0f;
    float rmsCoeff = 0.0f;        // detector RMS window, per sample
    float attackCoeff = 0.0f;     // detector ballistics, per control step
    float releaseCoeff = 0.0f;
    double ltCoeff = 0.0;         // 3 s one-pole, per control step
    float autoGainCoeff = 0.0f;   // autoGain on/off fade, per control step
    float limiterRelease = 0.0f;  // limiter release, per sample

    // Processing state
    std::array<ap::ThreeBandSplitter, kMaxChannels> splitters;
    std::array<Band, kBands> bands;
    std::array<ap::DelayLine, kMaxChannels> delays;
    ap::DelayLine engageDelay;    // engage, aligned with the delayed audio for the limiter gain
    ap::Smoother mixSmoother, ceilingSmoother;
    ap::GainComputer gainComputer;

    double inPow = 0.0, dryPow = 0.0, crossPow = 0.0, wetPow = 0.0, ioNorm = 0.0;
    float sumIn = 0.0f, sumDry = 0.0f, sumCross = 0.0f, sumWet = 0.0f;
    float trim = 1.0f, trimStep = 0.0f;   // auto-gain trim, ramped per sample
    float autoGainBlend = 1.0f;           // 0 = auto gain off, 1 = on
    int controlCounter = 0;

    float engage = 0.0f;          // 0 = input (delayed), 1 = fully processed
    bool needsSnap = true;

    // Lookahead limiter: monotonic deque for the sliding-window minimum of the
    // required gain, a release one-pole, then a boxcar of `window` samples.
    std::vector<float> dqValue;
    std::vector<int64_t> dqIndex;
    int dqHead = 0, dqSize = 0, dqCapacity = 1;
    std::vector<float> boxRing;
    int boxPos = 0;
    double boxSum = 0.0;
    float released = 1.0f;
    int64_t sampleIndex = 0;

    std::atomic<float> activityValue { 0.0f };
};

} // namespace frisson
