#pragma once

// Per-voice stereo multimode filter (Filter 1 / Filter 2 of a voice).
//
// Owner: W-C. FOUNDATION STUB: LP 12 / LP 24 / Ladder run one 2-pole
// ap::Biquad low-pass per channel; every other type passes through;
// filterMagnitudeDb() returns 0.

#include <ap/Filters.h>

#include <cstdint>

namespace verdant
{

enum class FilterType : uint8_t { LP12, LP24, HP12, HP24, BP12, BP24, Notch, CombPlus, CombMinus, Formant, Ladder, Count };

class VoiceFilter
{
public:
    void prepare (double sampleRate);
    void reset();
    void set (FilterType, float cutoffHz, float resonance01, float drive01);   // per control block; ramped inside
    void process (float* L, float* R, int numSamples);                          // in place, stereo

private:
    double sr = 44100.0;
    FilterType type = FilterType::LP24;
    float cutoff = -1.0f, resonance = -1.0f;
    ap::Biquad left, right;
};

/** Magnitude response in dB at `atHz`, for the UI's response curve. */
float filterMagnitudeDb (FilterType, float cutoffHz, float resonance01, double sampleRate, float atHz);

} // namespace verdant
