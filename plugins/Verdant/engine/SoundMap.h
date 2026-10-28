#pragma once

// The sound map: presets placed on a 2D map by sonic similarity (render a
// short note per preset offline, extract features, reduce to 2D
// deterministically), plus parameter blending between two patches.
//
// Owner: W-F. FOUNDATION STUB: rough features; points placed by
// normalised centroid (x) and attack (y); blendPatches is complete.

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

/** Continuous params: lerp in the normalised domain; Int rounds; Choice / Bool: a if t < 0.5 else b;
    user tables (and every other non-parameter field) from the chosen side. */
Patch blendPatches (const Patch& a, const Patch& b, float t);

} // namespace verdant
