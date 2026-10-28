#pragma once

// Sample-to-patch: analyses a dropped sample and builds a user wavetable plus
// a starting patch that gets close to it. Runs off the audio thread (in a
// background job): allocates freely, deterministic for identical input, a
// 10 s sample takes well under 2 s.
//
// Steps (progress is reported 0..1, monotone, ending at exactly 1):
//  1. Pitch: ap::YinPitchDetector (window 2048, 40..2000 Hz) on hops of 512
//     over the part of the sample above -40 dB of its RMS peak (at most 128
//     evenly spaced hops); detectedHz = median of the voiced estimates,
//     confidence = voiced fraction. Under 10 % voiced counts as unpitched
//     (noise, drums): detectedHz = 0, the table is read from the spectral
//     envelope on a nominal 110 Hz grid and the noise level goes to its cap.
//  2. Table: up to 64 analysis times evenly spaced over the -40 dB region; at
//     each a 4-period Hann window (zero-phase), FFT, harmonic magnitudes and
//     phases at k x f0 (log-parabolic peak interpolation) up to Nyquist and
//     harmonic 1023, resynthesised at 2048 samples with the phases rotated so
//     harmonic 1 is a zero-phase sine (frames stay morph-aligned). Frames are
//     equalised to the same RMS (Env 1 carries the loudness), then the table
//     is peak normalised to 1. Frame 0 = start of the sound.
//  3. Patch (from defaultValues()): Osc A on with the User table, Osc B off,
//     Filter 1 LP 24 with cutoff = 2 x the spectral centroid of the sustained
//     part, noise = noisiness x 100 (capped at 40 %), Env 1 from the RMS
//     envelope, Env 2 + matrix slot 1 scanning the table over the analysed
//     region, and - when the centroid falls by more than 20 % over the sound -
//     a filter envelope on Env 3 + matrix slot 2. See SampleMatch.cpp.
//
// Cancelling (the callback returns false) stops at the next checkpoint and
// returns the partial result with confidence = 0 (`table` may then be null).

#include "Params.h"
#include "Wavetable.h"

#include <functional>
#include <memory>
#include <string>

namespace verdant
{

struct SampleMatchResult
{
    std::shared_ptr<Wavetable> table;
    ParamValues values;          // a full patch starting from defaultValues(); every value inside its range
    float detectedHz = 0;        // 0 = unpitched
    float confidence = 0;        // voiced fraction 0..1; 0 when cancelled
    std::string notes;           // one-line human summary
};

using ProgressFn = std::function<bool (float progress01)>;   // return false to cancel

SampleMatchResult matchSample (const float* mono, int numSamples, double sampleRate, const ProgressFn& progress);

} // namespace verdant
