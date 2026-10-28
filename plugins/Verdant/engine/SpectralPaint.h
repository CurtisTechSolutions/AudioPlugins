#pragma once

// Spectral paint: a time x harmonic canvas turned into a wavetable, one frame
// per column (additive / inverse FFT).
//
// Owner: W-B. FOUNDATION STUB: naive additive conversion, nearest-neighbour
// image sampling and a simple time blur.

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

/** One frame per column; harmonics above kFrameSize / 2 - 1 are ignored. */
std::shared_ptr<Wavetable> canvasToWavetable (const PaintCanvas&, const std::string& name);

/** luminance 0..1, row-major width x height, top row = highest harmonics. */
PaintCanvas imageToCanvas (const float* luminance, int width, int height, int cols, int rows);

/** Smooths along time (and a little across harmonics); amount 0..1. */
void smoothCanvas (PaintCanvas&, float amount);

} // namespace verdant
