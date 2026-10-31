#pragma once

// Spectral paint: a time x harmonic canvas turned into a wavetable, one frame
// per column, by inverse-FFT additive synthesis.
//
//  * canvasToWavetable: frame = sum_r amp[c][r] sin (2 pi (r + 1) p + 2 pi phase[c][r])
//    over one cycle p in 0..1 (phase 0 when `phase` is empty); harmonics above
//    1023 (rows beyond 1023) are ignored; each non-silent frame is peak
//    normalised to 1, silent columns stay silent. More than kMaxFrames columns
//    are thinned to kMaxFrames evenly spaced ones.
//  * imageToCanvas: box-filter (area-weighted) resample of a luminance image;
//    the image's top row maps to the highest harmonic row (rows - 1).
//  * smoothCanvas: separable box blur over time and harmonics, radius
//    round (amount x 4) cells.
//
// Runs off the audio thread (allocates).

#include "Wavetable.h"

#include <memory>
#include <string>
#include <vector>

namespace verdant
{

struct PaintCanvas
{
    int cols = 64, rows = 128;
    std::vector<float> amp;     // cols x rows (column-major: amp[col * rows + row]), 0..1, row r = harmonic r + 1
    std::vector<float> phase;   // optional, same size, 0..1 -> 0..2 pi; empty = zero phase
};

/** One frame per column; harmonics above kFrameSize / 2 - 1 are ignored. Missing amp entries count as 0;
    returns nullptr only when cols or rows < 1. */
std::shared_ptr<Wavetable> canvasToWavetable (const PaintCanvas&, const std::string& name);

/** luminance 0..1, row-major width x height, top row = highest harmonics. amp = luminance, phases empty. */
PaintCanvas imageToCanvas (const float* luminance, int width, int height, int cols, int rows);

/** Separable blur over time and harmonics with radius round (amount x 4) cells; amount 0 = no-op.
    Values are clamped to 0..1. Phases are left alone. */
void smoothCanvas (PaintCanvas&, float amount);

} // namespace verdant
