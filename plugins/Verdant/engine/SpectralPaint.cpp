#include "SpectralPaint.h"

#include "FFT.h"

#include <algorithm>
#include <cmath>

namespace verdant
{

namespace
{
    constexpr int    kMaxHarmonic  = kFrameSize / 2 - 1;   // 1023: the Nyquist bin of a 2048 frame is never used
    constexpr float  kSilentPeak   = 1.0e-6f;              // frames quieter than this (-120 dB) are written as exact silence
    constexpr int    kMaxBlurCells = 4;                    // smoothCanvas radius at amount 1
    constexpr double kTwoPi        = 6.283185307179586;

    float sanitise01 (float v) noexcept { return std::isfinite (v) ? std::clamp (v, 0.0f, 1.0f) : 0.0f; }

    /** Area-weighted box filter taps mapping `srcSize` cells onto `dstSize` cells (works for up- and downsampling). */
    struct Taps { std::vector<int> begin, count; std::vector<int> index; std::vector<float> weight; };

    Taps boxTaps (int srcSize, int dstSize)
    {
        Taps t;
        t.begin.resize ((size_t) dstSize);
        t.count.resize ((size_t) dstSize);
        const double ratio = (double) srcSize / (double) dstSize;
        for (int d = 0; d < dstSize; ++d)
        {
            const double lo = d * ratio, hi = (d + 1) * ratio;
            const int first = std::clamp ((int) std::floor (lo), 0, srcSize - 1);
            const int last  = std::clamp ((int) std::ceil (hi) - 1, first, srcSize - 1);
            t.begin[(size_t) d] = (int) t.index.size();
            double total = 0.0;
            for (int s = first; s <= last; ++s)
                total += std::max (0.0, std::min (hi, (double) s + 1.0) - std::max (lo, (double) s));
            for (int s = first; s <= last; ++s)
            {
                const double w = std::max (0.0, std::min (hi, (double) s + 1.0) - std::max (lo, (double) s));
                t.index.push_back (s);
                t.weight.push_back (total > 0.0 ? (float) (w / total) : 1.0f / (float) (last - first + 1));
            }
            t.count[(size_t) d] = last - first + 1;
        }
        return t;
    }

    /** Box blur of radius `r` along one axis of a strided line, renormalised at the edges so borders do not darken. */
    void blurLine (float* v, int n, int stride, int r, std::vector<float>& scratch)
    {
        scratch.resize ((size_t) n);
        for (int i = 0; i < n; ++i) scratch[(size_t) i] = v[(size_t) i * (size_t) stride];
        for (int i = 0; i < n; ++i)
        {
            const int a = std::max (0, i - r), b = std::min (n - 1, i + r);
            float sum = 0.0f;
            for (int j = a; j <= b; ++j) sum += scratch[(size_t) j];
            v[(size_t) i * (size_t) stride] = sum / (float) (b - a + 1);
        }
    }
}

std::shared_ptr<Wavetable> canvasToWavetable (const PaintCanvas& c, const std::string& name)
{
    if (c.cols < 1 || c.rows < 1)
        return nullptr;

    const int cols = c.cols, rows = c.rows;
    const int numFrames = std::min (cols, kMaxFrames);
    const int maxRow = std::min (rows, kMaxHarmonic);   // row r = harmonic r + 1, so rows 0..1022
    const size_t cells = (size_t) cols * (size_t) rows;
    const bool hasPhase = c.phase.size() >= cells;

    const FFT fft (kFrameSize);
    std::vector<float> re ((size_t) kFrameSize / 2 + 1), im ((size_t) kFrameSize / 2 + 1);
    std::vector<float> data ((size_t) numFrames * (size_t) kFrameSize, 0.0f);

    for (int f = 0; f < numFrames; ++f)
    {
        const int col = numFrames == cols ? f : (int) std::lround ((double) f * (double) (cols - 1) / (double) (numFrames - 1));
        std::fill (re.begin(), re.end(), 0.0f);
        std::fill (im.begin(), im.end(), 0.0f);

        bool any = false;
        for (int r = 0; r < maxRow; ++r)
        {
            const size_t idx = (size_t) col * (size_t) rows + (size_t) r;
            const float a = idx < c.amp.size() ? sanitise01 (c.amp[idx]) : 0.0f;
            if (a <= 0.0f)
                continue;
            any = true;
            const double ph = hasPhase && std::isfinite (c.phase[idx]) ? kTwoPi * (double) c.phase[idx] : 0.0;
            // a sin (2 pi h n / N + ph) has bin X[h] = (N a / 2) (sin ph - i cos ph) under realInverse's 1/N scaling.
            re[(size_t) r + 1] = (float) (0.5 * kFrameSize * a * std::sin (ph));
            im[(size_t) r + 1] = (float) (-0.5 * kFrameSize * a * std::cos (ph));
        }
        if (! any)
            continue;

        float* out = data.data() + (size_t) f * (size_t) kFrameSize;
        fft.realInverse (re.data(), im.data(), out);

        float peak = 0.0f;
        for (int i = 0; i < kFrameSize; ++i) peak = std::max (peak, std::fabs (out[i]));
        if (peak < kSilentPeak)
            std::fill (out, out + kFrameSize, 0.0f);
        else
            for (int i = 0; i < kFrameSize; ++i) out[i] /= peak;
    }

    return Wavetable::fromFrames (data.data(), numFrames, name);
}

PaintCanvas imageToCanvas (const float* luminance, int width, int height, int cols, int rows)
{
    PaintCanvas c;
    c.cols = std::max (1, cols);
    c.rows = std::max (1, rows);
    c.amp.assign ((size_t) c.cols * (size_t) c.rows, 0.0f);
    if (luminance == nullptr || width < 1 || height < 1)
        return c;

    const Taps tx = boxTaps (width, c.cols);
    const Taps ty = boxTaps (height, c.rows);   // dst index counts from the image top

    for (int col = 0; col < c.cols; ++col)
        for (int yTop = 0; yTop < c.rows; ++yTop)
        {
            double acc = 0.0;
            for (int j = 0; j < ty.count[(size_t) yTop]; ++j)
            {
                const size_t tj = (size_t) (ty.begin[(size_t) yTop] + j);
                const size_t y = (size_t) ty.index[tj];
                for (int i = 0; i < tx.count[(size_t) col]; ++i)
                {
                    const size_t ti = (size_t) (tx.begin[(size_t) col] + i);
                    const size_t x = (size_t) tx.index[ti];
                    acc += (double) ty.weight[tj] * (double) tx.weight[ti] * (double) sanitise01 (luminance[y * (size_t) width + x]);
                }
            }
            const int row = c.rows - 1 - yTop;   // image top = highest harmonic
            c.amp[(size_t) col * (size_t) c.rows + (size_t) row] = std::clamp ((float) acc, 0.0f, 1.0f);
        }
    return c;
}

void smoothCanvas (PaintCanvas& c, float amount)
{
    amount = std::isfinite (amount) ? std::clamp (amount, 0.0f, 1.0f) : 0.0f;
    const int radius = (int) std::lround (amount * (float) kMaxBlurCells);
    if (radius <= 0 || c.cols < 1 || c.rows < 1)
        return;   // amount 0 leaves the canvas untouched

    const size_t cells = (size_t) c.cols * (size_t) c.rows;
    if (c.amp.size() < cells)
        c.amp.resize (cells, 0.0f);
    for (auto& v : c.amp) v = sanitise01 (v);

    std::vector<float> scratch;
    for (int col = 0; col < c.cols; ++col)   // across harmonics (contiguous within a column)
        blurLine (c.amp.data() + (size_t) col * (size_t) c.rows, c.rows, 1, radius, scratch);
    for (int r = 0; r < c.rows; ++r)         // across time
        blurLine (c.amp.data() + (size_t) r, c.cols, c.rows, radius, scratch);

    for (auto& v : c.amp) v = std::clamp (v, 0.0f, 1.0f);
}

} // namespace verdant
