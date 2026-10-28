// SpectralPaint: canvas -> wavetable (frame count, harmonic placement,
// phase, silence, harmonic limit, normalisation), image -> canvas orientation
// and box filtering, smoothCanvas spreading and range.

#include "TestHarness.h"

#include "engine/FFT.h"
#include "engine/SpectralPaint.h"

#include <ap/Math.h>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace verdant;

namespace
{
    constexpr double kTwoPi = 6.283185307179586;

    PaintCanvas emptyCanvas (int cols, int rows)
    {
        PaintCanvas c;
        c.cols = cols;
        c.rows = rows;
        c.amp.assign ((size_t) cols * (size_t) rows, 0.0f);
        return c;
    }

    float& cell (PaintCanvas& c, int col, int row) { return c.amp[(size_t) col * (size_t) c.rows + (size_t) row]; }

    double correlationWith (const float* x, int harmonic, double phase)
    {
        double ab = 0, aa = 0, bb = 0;
        for (int i = 0; i < kFrameSize; ++i)
        {
            const double s = std::sin (kTwoPi * harmonic * i / kFrameSize + phase);
            ab += x[i] * s; aa += (double) x[i] * x[i]; bb += s * s;
        }
        return aa > 0 ? ab / std::sqrt (aa * bb) : 0.0;
    }

    float peakOf (const float* x)
    {
        float p = 0.0f;
        for (int i = 0; i < kFrameSize; ++i) p = std::max (p, std::fabs (x[i]));
        return p;
    }

    /** Energy per harmonic bin of one frame. */
    std::vector<double> binEnergy (const float* x)
    {
        const FFT fft (kFrameSize);
        std::vector<float> re ((size_t) kFrameSize / 2 + 1), im ((size_t) kFrameSize / 2 + 1);
        fft.realForward (x, re.data(), im.data());
        std::vector<double> e ((size_t) kFrameSize / 2 + 1);
        for (size_t k = 0; k < e.size(); ++k) e[k] = (double) re[k] * re[k] + (double) im[k] * im[k];
        return e;
    }
}

AP_TEST (paint_single_cell_is_one_harmonic_in_one_frame)
{
    auto c = emptyCanvas (8, 16);
    cell (c, 3, 4) = 0.7f;   // column 3, harmonic 5
    const auto t = canvasToWavetable (c, "Painted");
    CHECK (t != nullptr);
    if (t == nullptr) return;
    CHECK_EQ (t->numFrames(), 8);
    CHECK (t->name() == "Painted");
    CHECK (correlationWith (t->frame (3), 5, 0.0) > 0.999);
    CHECK_NEAR (peakOf (t->frame (3)), 1.0, 1.0e-4);   // peak normalised
    for (int f = 0; f < 8; ++f)
        if (f != 3)
            CHECK_EQ (peakOf (t->frame (f)), 0.0f);    // silent columns stay exactly silent
}

AP_TEST (paint_phase_column)
{
    auto c = emptyCanvas (2, 4);
    c.phase.assign (c.amp.size(), 0.0f);
    cell (c, 1, 0) = 1.0f;
    c.phase[(size_t) 1 * 4 + 0] = 0.25f;   // a quarter cycle: sin -> cos
    const auto t = canvasToWavetable (c, "P");
    CHECK (t != nullptr);
    if (t == nullptr) return;
    CHECK (correlationWith (t->frame (1), 1, 0.25 * kTwoPi) > 0.999);
    CHECK_NEAR (t->frame (1)[0], 1.0, 1.0e-3);   // cos starts at its peak
}

AP_TEST (paint_harmonic_limit_and_normalisation)
{
    const int rows = 40;
    auto c = emptyCanvas (6, rows);
    ap::Rng rng; rng.state = 1234u;
    for (auto& a : c.amp) a = 0.5f + 0.5f * rng.nextBipolar();
    const auto t = canvasToWavetable (c, "Dense");
    CHECK (t != nullptr);
    if (t == nullptr) return;
    for (int f = 0; f < t->numFrames(); ++f)
    {
        const auto e = binEnergy (t->frame (f));
        double inside = 0.0, outside = 0.0;
        for (size_t k = 0; k < e.size(); ++k) (k >= 1 && k <= (size_t) rows ? inside : outside) += e[k];
        CHECK (inside > 0.0);
        CHECK (10.0 * std::log10 (outside / inside + 1.0e-30) < -80.0);   // nothing above the painted rows
        CHECK (peakOf (t->frame (f)) <= 1.0f + 1.0e-6f);
        CHECK_NEAR (peakOf (t->frame (f)), 1.0, 1.0e-4);
    }

    // Rows beyond 1023 harmonics are ignored; the result is still band limited to harmonic 1023.
    auto tall = emptyCanvas (1, 1500);
    cell (tall, 0, 1200) = 1.0f;   // harmonic 1201: dropped
    cell (tall, 0, 1022) = 1.0f;   // harmonic 1023: kept
    const auto tt = canvasToWavetable (tall, "Tall");
    CHECK (tt != nullptr);
    if (tt != nullptr)
    {
        const auto e = binEnergy (tt->frame (0));
        double total = 0.0;
        for (double v : e) total += v;
        CHECK (e[1023] / total > 0.999);
    }
}

AP_TEST (paint_empty_and_degenerate_canvases)
{
    const auto t = canvasToWavetable (emptyCanvas (4, 8), "Empty");
    CHECK (t != nullptr);
    if (t != nullptr)
    {
        CHECK_EQ (t->numFrames(), 4);
        for (int f = 0; f < 4; ++f) CHECK_EQ (peakOf (t->frame (f)), 0.0f);
    }

    PaintCanvas none; none.cols = 0;
    CHECK (canvasToWavetable (none, "x") == nullptr);

    // Out-of-range and NaN amplitudes are sanitised; a short amp vector counts as zeros.
    PaintCanvas odd; odd.cols = 3; odd.rows = 4;
    odd.amp = { 5.0f, std::nanf (""), -1.0f };
    const auto to = canvasToWavetable (odd, "Odd");
    CHECK (to != nullptr && to->numFrames() == 3);
    if (to != nullptr)
    {
        CHECK (correlationWith (to->frame (0), 1, 0.0) > 0.999);
        CHECK_EQ (peakOf (to->frame (1)), 0.0f);
    }

    // More columns than kMaxFrames are thinned.
    auto wide = emptyCanvas (kMaxFrames + 100, 2);
    cell (wide, 0, 0) = 1.0f;
    cell (wide, kMaxFrames + 99, 1) = 1.0f;
    const auto tw = canvasToWavetable (wide, "Wide");
    CHECK (tw != nullptr && tw->numFrames() == kMaxFrames);
    if (tw != nullptr)
    {
        CHECK (correlationWith (tw->frame (0), 1, 0.0) > 0.999);
        CHECK (correlationWith (tw->frame (kMaxFrames - 1), 2, 0.0) > 0.999);
    }
}

AP_TEST (paint_image_top_row_is_highest_harmonic)
{
    const int w = 16, h = 32;
    std::vector<float> img ((size_t) w * h, 0.0f);
    for (int x = 0; x < w; ++x) img[(size_t) x] = 1.0f;                     // top row lit
    for (int x = 0; x < w / 2; ++x) img[(size_t) (h - 1) * w + (size_t) x] = 0.5f;   // bottom row, left half

    const auto c = imageToCanvas (img.data(), w, h, 8, 16);
    CHECK_EQ (c.cols, 8);
    CHECK_EQ (c.rows, 16);
    CHECK (c.phase.empty());
    CHECK_EQ ((int) c.amp.size(), 8 * 16);
    for (int col = 0; col < 8; ++col)
    {
        // Each canvas row covers two image rows: the lit top row averages with the dark one below it.
        CHECK_NEAR (c.amp[(size_t) col * 16 + 15], 0.5, 1.0e-6);
        CHECK_NEAR (c.amp[(size_t) col * 16 + 7], 0.0, 1.0e-6);
        CHECK_NEAR (c.amp[(size_t) col * 16 + 0], col < 4 ? 0.25 : 0.0, 1.0e-6);
    }

    // Upsampling (canvas bigger than the image) keeps values and orientation.
    const float tiny[4] = { 1.0f, 0.0f,    // top
                            0.0f, 0.2f };  // bottom
    const auto up = imageToCanvas (tiny, 2, 2, 4, 4);
    CHECK_NEAR (up.amp[0 * 4 + 3], 1.0, 1.0e-6);   // col 0, highest row = image top-left
    CHECK_NEAR (up.amp[0 * 4 + 0], 0.0, 1.0e-6);
    CHECK_NEAR (up.amp[3 * 4 + 0], 0.2, 1.0e-6);   // col 3, harmonic 1 = image bottom-right
    CHECK_NEAR (up.amp[3 * 4 + 3], 0.0, 1.0e-6);

    // Box filter: a uniform image stays uniform.
    std::vector<float> grey ((size_t) 37 * 23, 0.3f);
    const auto g = imageToCanvas (grey.data(), 37, 23, 10, 7);
    bool uniform = true;
    for (float a : g.amp) uniform = uniform && std::fabs (a - 0.3f) < 1.0e-5f;
    CHECK (uniform);
}

AP_TEST (paint_smooth_spreads_and_keeps_range)
{
    auto c = emptyCanvas (16, 16);
    cell (c, 8, 8) = 1.0f;
    auto unchanged = c;
    smoothCanvas (unchanged, 0.0f);
    CHECK (unchanged.amp == c.amp);   // amount 0 = no-op

    smoothCanvas (c, 0.5f);   // radius 2
    CHECK (cell (c, 8, 8) < 1.0f);
    CHECK (cell (c, 9, 8) > 0.0f);
    CHECK (cell (c, 8, 10) > 0.0f);
    CHECK (cell (c, 10, 10) > 0.0f);
    CHECK_EQ (cell (c, 11, 8), 0.0f);   // beyond the radius
    double sum = 0.0;
    bool inRange = true;
    for (float a : c.amp) { sum += a; inRange = inRange && a >= 0.0f && a <= 1.0f; }
    CHECK (inRange);
    CHECK_NEAR (sum, 1.0, 1.0e-5);   // energy spread, not created (interior cell)

    // A full, saturated canvas stays within 0..1; out-of-range input is clamped.
    auto full = emptyCanvas (5, 5);
    std::fill (full.amp.begin(), full.amp.end(), 1.0f);
    full.amp[3] = 7.0f;
    full.amp[4] = -2.0f;
    smoothCanvas (full, 2.0f);   // amount clamps to 1
    for (float a : full.amp) CHECK (a >= 0.0f && a <= 1.0f);
}
