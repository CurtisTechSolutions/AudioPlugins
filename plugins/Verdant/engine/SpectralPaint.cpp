#include "SpectralPaint.h"

#include "FFT.h"

#include <algorithm>
#include <cmath>

namespace verdant
{

std::shared_ptr<Wavetable> canvasToWavetable (const PaintCanvas& c, const std::string& name)
{
    const int cols = std::clamp (c.cols, 1, kMaxFrames);
    const int rows = std::max (0, c.rows);
    if ((int) c.amp.size() < cols * rows)
        return nullptr;

    const FFT fft (kFrameSize);
    std::vector<float> re ((size_t) kFrameSize / 2 + 1), im ((size_t) kFrameSize / 2 + 1);
    std::vector<float> data ((size_t) cols * (size_t) kFrameSize);
    const bool hasPhase = (int) c.phase.size() >= cols * rows;
    const int maxRow = std::min (rows, kFrameSize / 2 - 1);

    for (int col = 0; col < cols; ++col)
    {
        std::fill (re.begin(), re.end(), 0.0f);
        std::fill (im.begin(), im.end(), 0.0f);
        for (int r = 0; r < maxRow; ++r)
        {
            const size_t idx = (size_t) col * (size_t) rows + (size_t) r;
            const double a = std::clamp (c.amp[idx], 0.0f, 1.0f) / (double) (r + 1);   // painted level, 1/h weighting
            const double ph = hasPhase ? 6.283185307179586 * c.phase[idx] : 0.0;
            // a sin (2 pi h t + ph) -> X[h] = (N / 2) a (sin ph - i cos ph)
            re[(size_t) r + 1] = (float) (a * std::sin (ph) * kFrameSize * 0.5);
            im[(size_t) r + 1] = (float) (-a * std::cos (ph) * kFrameSize * 0.5);
        }
        fft.realInverse (re.data(), im.data(), data.data() + (size_t) col * (size_t) kFrameSize);
    }

    float peak = 0.0f;
    for (float s : data) peak = std::max (peak, std::fabs (s));
    if (peak > 1.0e-9f)
        for (float& s : data) s *= 0.95f / peak;

    return Wavetable::fromFrames (data.data(), cols, name);
}

PaintCanvas imageToCanvas (const float* luminance, int width, int height, int cols, int rows)
{
    PaintCanvas c;
    c.cols = std::max (1, cols);
    c.rows = std::max (1, rows);
    c.amp.assign ((size_t) c.cols * (size_t) c.rows, 0.0f);
    if (luminance == nullptr || width < 1 || height < 1)
        return c;

    for (int col = 0; col < c.cols; ++col)
        for (int r = 0; r < c.rows; ++r)
        {
            const int x = std::min (width - 1, col * width / c.cols);
            const int y = std::min (height - 1, (c.rows - 1 - r) * height / c.rows);   // top = high harmonics
            c.amp[(size_t) col * (size_t) c.rows + (size_t) r] = std::clamp (luminance[(size_t) y * (size_t) width + (size_t) x], 0.0f, 1.0f);
        }
    return c;
}

void smoothCanvas (PaintCanvas& c, float amount)
{
    amount = std::clamp (amount, 0.0f, 1.0f);
    if (amount <= 0.0f || (int) c.amp.size() < c.cols * c.rows || c.cols < 2)
        return;

    std::vector<float> src = c.amp;
    const float side = 0.25f * amount;
    for (int col = 0; col < c.cols; ++col)
        for (int r = 0; r < c.rows; ++r)
        {
            const int prev = std::max (0, col - 1), next = std::min (c.cols - 1, col + 1);
            const auto at = [&] (int cc) { return src[(size_t) cc * (size_t) c.rows + (size_t) r]; };
            c.amp[(size_t) col * (size_t) c.rows + (size_t) r] = (1.0f - 2.0f * side) * at (col) + side * (at (prev) + at (next));
        }
}

} // namespace verdant
