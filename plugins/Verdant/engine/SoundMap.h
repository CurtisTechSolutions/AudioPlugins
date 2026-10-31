#pragma once

// The sound map: presets placed on a 2D map by sonic similarity (render a
// short note per preset offline, extract features, reduce to 2D
// deterministically), plus parameter blending between two patches.
// Message / background thread only (allocates).
//
// Features (extractFeatures):
//   centroidHz  power-weighted spectral centroid over the whole render
//               (Hann FFT 4096, hop 2048, frames pooled by their power)
//   attackMs    time for the 10 ms RMS envelope (1 ms hop, centred) to reach
//               90 % of its peak
//   noisiness   spectral flatness (geometric / arithmetic mean of the power
//               spectrum, DC and Nyquist excluded), power-weighted average over
//               frames; 0 = pure tone .. about 0.56 for white noise .. 1
//   movement    standard deviation / mean of the per-frame centroid over the
//               frames within 40 dB of the loudest
//   loudness    RMS over the whole render in dB (-120 for silence)
//
// Map (buildSoundMap): [log centroid, log attack, noisiness, movement,
// loudness] -> z-scores (constant features -> 0) -> covariance -> first two
// principal components by power iteration (start (1,1,1,1,1)/sqrt 5, 200
// iterations, deflation for the second) -> projection -> signs fixed so x
// rises with log centroid and y with log attack -> min-max to 0.05..0.95.
// Fewer than 3 presets lie on a horizontal line ordered by centroid.
// Deterministic, no RNG.
//
// Owner: W-F.

#include "Patch.h"

#include <functional>
#include <vector>

namespace verdant
{

struct SoundFeatures { float centroidHz, attackMs, noisiness, movement, loudness; };

SoundFeatures extractFeatures (const float* mono, int n, double sampleRate);

struct SoundMapPoint
{
    int presetIndex;
    float x, y;                // 0..1
    SoundFeatures features;
};

/** Deterministic PCA (power iteration, fixed init, sign fixed so x correlates positively with centroid).
    `render` returns a mono preview at 44.1 kHz (e.g. renderPreview (patch, 48, 1.0f, 1.5f, 44100.0)). */
std::vector<SoundMapPoint> buildSoundMap (const std::vector<Patch>&, const std::function<std::vector<float> (const Patch&)>& render);

/** Same, from features already extracted (e.g. a cache); presetIndex = position in the vector. Addition. */
std::vector<SoundMapPoint> buildSoundMapFromFeatures (const std::vector<SoundFeatures>&);

/** Continuous params: lerp in the normalised domain; Int rounds; Choice / Bool: a if t < 0.5 else b;
    user tables (and every other non-parameter field) from the chosen side. t <= 0 returns a and
    t >= 1 returns b unchanged; in between the name is "<A> <-> <B> 42%" (UTF-8 left-right arrow). */
Patch blendPatches (const Patch& a, const Patch& b, float t);

} // namespace verdant
