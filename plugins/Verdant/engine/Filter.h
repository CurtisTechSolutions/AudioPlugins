#pragma once

// Per-voice stereo multimode filter (Filter 1 / Filter 2 of a voice).
//
// Owner: W-C.
//
//   LP / HP / BP 12, Notch   one zero-delay-feedback TPT state-variable filter
//                            (Zavalishin / Cytomic), Q = lerp (0.5, 25, res^2)
//   LP / HP / BP 24          two SVFs in series, Q per stage = sqrt (Q)
//   Comb + / Comb -          feedback comb y = x +/- g * LP (y[n - D]), D = sr / cutoff
//                            (fractional, linear interpolation), g = 0.98 * res, one-pole
//                            damping LP (pole 0.3) in the loop, output normalised by
//                            sqrt (1 - g^2) so broadband level stays put as res rises
//   Formant                  four parallel unity-peak bandpasses on an A-E-I-O-U vowel
//                            path (cutoff, log-mapped over 20 Hz..20 kHz, picks the point
//                            on the path); Q = lerp (3, 20, res)
//   Ladder                   4-pole transistor-ladder style LP, nonlinear feedback node,
//                            k = 4 * res, run at 2x internally, passband loss
//                            compensated by (1 + 0.5 k)
//
// Drive (before the filter): x = tanh (x d) / sqrt (d), d = 1 + 6 drive; skipped at 0.
//
// Every control change is ramped linearly over kControlInterval samples, so
// set() once per control block gives zipper-free sweeps. A type change snaps
// the coefficients (and clears the state of a filter family that was not in use).
// No allocation after prepare(); output stays finite and bounded (the SVF and comb
// states are soft-limited to about +-4, the ladder saturates).

#include <ap/Filters.h>   // kept from the skeleton: other engine files may rely on it transitively

#include <cstdint>
#include <vector>

namespace verdant
{

enum class FilterType : uint8_t { LP12, LP24, HP12, HP24, BP12, BP24, Notch, CombPlus, CombMinus, Formant, Ladder, Count };

class VoiceFilter
{
public:
    void prepare (double sampleRate);                                                     // allocates the comb delay lines
    void reset() noexcept;
    void set (FilterType, float cutoffHz, float resonance01, float drive01) noexcept;    // per control block; ramped inside
    void process (float* L, float* R, int numSamples) noexcept;                           // in place, stereo

    FilterType currentType() const noexcept { return type; }

private:
    // Coefficient slots: [0] = drive factor d, then per type:
    //   SVF      [1] g, [2] k
    //   Comb     [1] delay (samples), [2] signed feedback, [3] output normalisation
    //   Formant  [1..4] g per formant, [5..8] linear amplitude per formant, [9] k
    //   Ladder   [1] one-pole g (at 2x), [2] k
    static constexpr int kNumCoeffs = 10;

    struct Svf { float ic1 = 0.0f, ic2 = 0.0f; };

    struct Channel
    {
        Svf svf[4];                                  // stages 0..1 for 12/24 dB, 0..3 for Formant
        float ladder[4] {}, ladderPrevIn = 0.0f;
        float combLp = 0.0f;
        std::vector<float> comb;                     // delay line, power-of-two size
    };

    void deriveCoefficients() noexcept;
    void clearState() noexcept;
    void sanitiseState() noexcept;

    template <int Mode, int Stages> void runSvf (float* L, float* R, int n) noexcept;
    void runComb (float* L, float* R, int n) noexcept;
    void runFormant (float* L, float* R, int n) noexcept;
    void runLadder (float* L, float* R, int n) noexcept;

    inline void stepRamp() noexcept;
    inline float driveSample (float x) const noexcept;

    double sr = 44100.0;
    FilterType type = FilterType::LP24;
    bool primed = false;                             // false until the first set() after prepare / reset

    float cur[kNumCoeffs] {}, tgt[kNumCoeffs] {}, stp[kNumCoeffs] {};
    int rampLeft = 0;
    bool driveOn = false;

    // Derived per-sample coefficients (recomputed only while ramping).
    float a1[4] {}, a2[4] {}, a3[4] {};

    Channel ch[2];
    int combMask = 0, combWrite = 0;
    bool combDirty = false;                          // the delay lines hold signal (cleared lazily)
};

/** Magnitude response in dB at `atHz`, for the UI's response curve. Matches the
    digital filter (TPT/bilinear responses for the SVF types, the exact comb and
    formant sums, the linearised ladder). Always finite, clamped to -120..+60 dB.
    Drive is not part of the curve. */
float filterMagnitudeDb (FilterType, float cutoffHz, float resonance01, double sampleRate, float atHz);

} // namespace verdant
