#include "FFT.h"

#include <cmath>

namespace verdant
{

FFT::FFT (int size)
{
    n = 2;
    log2n = 1;
    while (n < size && n < 65536)
    {
        n <<= 1;
        ++log2n;
    }

    cosTable.resize ((size_t) n / 2);
    sinTable.resize ((size_t) n / 2);
    for (int k = 0; k < n / 2; ++k)
    {
        const double w = 2.0 * 3.14159265358979323846 * (double) k / (double) n;
        cosTable[(size_t) k] = (float) std::cos (w);
        sinTable[(size_t) k] = (float) std::sin (w);
    }

    bitrev.resize ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        int r = 0;
        for (int b = 0; b < log2n; ++b)
            if (i & (1 << b)) r |= 1 << (log2n - 1 - b);
        bitrev[(size_t) i] = r;
    }
}

// In-place iterative butterflies on bit-reversed data of length m (m <= n).
// Element k lives at re[k * Stride], im[k * Stride] (Stride 1 = split arrays,
// Stride 2 = interleaved re/im pairs with im = re + 1).
template <int Stride>
void FFT::butterflies (float* re, float* im, int m, bool inverseTransform) const noexcept
{
    const float sign = inverseTransform ? 1.0f : -1.0f;

    for (int len = 2; len <= m; len <<= 1)
    {
        const int half = len >> 1;
        const int step = n / len;   // W_len^j = W_n^(j * n / len)

        for (int j = 0; j < half; ++j)
        {
            const float wr = cosTable[(size_t) (j * step)];
            const float wi = sign * sinTable[(size_t) (j * step)];

            for (int i = j; i < m; i += len)
            {
                const int a = i * Stride, b = (i + half) * Stride;
                const float tr = re[b] * wr - im[b] * wi;
                const float ti = re[b] * wi + im[b] * wr;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
            }
        }
    }
}

void FFT::complexTransform (const float* re, const float* im, float* outRe, float* outIm, bool inverseTransform) const noexcept
{
    // Bit-reversal permutation into the output (in place when aliased).
    if (outRe == re)
    {
        for (int i = 0; i < n; ++i)
        {
            const int r = bitrev[(size_t) i];
            if (i < r) { const float t = outRe[i]; outRe[i] = outRe[r]; outRe[r] = t; }
        }
    }
    else
    {
        for (int i = 0; i < n; ++i) outRe[i] = re[bitrev[(size_t) i]];
    }

    if (im == nullptr)
    {
        for (int i = 0; i < n; ++i) outIm[i] = 0.0f;
    }
    else if (outIm == im)
    {
        for (int i = 0; i < n; ++i)
        {
            const int r = bitrev[(size_t) i];
            if (i < r) { const float t = outIm[i]; outIm[i] = outIm[r]; outIm[r] = t; }
        }
    }
    else
    {
        for (int i = 0; i < n; ++i) outIm[i] = im[bitrev[(size_t) i]];
    }

    butterflies<1> (outRe, outIm, n, inverseTransform);

    if (inverseTransform)
    {
        const float scale = 1.0f / (float) n;
        for (int i = 0; i < n; ++i) { outRe[i] *= scale; outIm[i] *= scale; }
    }
}

void FFT::forward (const float* re, const float* im, float* outRe, float* outIm) const noexcept
{
    complexTransform (re, im, outRe, outIm, false);
}

void FFT::inverse (const float* re, const float* im, float* outRe, float* outIm) const noexcept
{
    complexTransform (re, im, outRe, outIm, true);
}

// Real transforms via one complex transform of half the size:
// z[k] = x[2k] + i x[2k+1], Z = FFT_M(z), M = N/2, then
// X[k] = E[k] + W^k O[k] with E[k] = (Z[k] + conj Z[M-k]) / 2, O[k] = (Z[k] - conj Z[M-k]) / 2i.
void FFT::realForward (const float* in, float* re, float* im) const noexcept
{
    const int m = n / 2;

    for (int k = 0; k < m; ++k)
    {
        const int r = bitrev[(size_t) (2 * k)];   // bit reversal for size m
        re[r] = in[2 * k];
        im[r] = in[2 * k + 1];
    }

    butterflies<1> (re, im, m, false);

    // Pairs (k, m - k) are rewritten together, in place.
    for (int k = 1; k <= m / 2; ++k)
    {
        const int k2 = m - k;
        const float a = re[k], b = im[k], c = re[k2], d = im[k2];

        const float er = 0.5f * (a + c), ei = 0.5f * (b - d);   // E[k]
        const float orr = 0.5f * (b + d), oi = -0.5f * (a - c); // O[k]
        const float wr = cosTable[(size_t) k], wi = -sinTable[(size_t) k];   // W^k, W = e^(-2 pi i / N)
        const float tr = wr * orr - wi * oi;
        const float ti = wr * oi + wi * orr;

        re[k] = er + tr;
        im[k] = ei + ti;
        if (k2 != k)
        {
            re[k2] = er - tr;     // X[m-k] = conj (E[k] - W^k O[k])
            im[k2] = -(ei - ti);
        }
    }

    const float z0r = re[0], z0i = im[0];
    re[0] = z0r + z0i;  im[0] = 0.0f;
    re[m] = z0r - z0i;  im[m] = 0.0f;
}

void FFT::realInverse (const float* re, const float* im, float* out) const noexcept
{
    const int m = n / 2;

    // Z[k] = E[k] + i O[k], E[k] = (X[k] + conj X[m-k]) / 2, O[k] = (X[k] - conj X[m-k]) W^-k / 2,
    // written straight into its bit-reversed slot of `out`, viewed as m interleaved complex values.
    for (int k = 0; k < m; ++k)
    {
        const float a = re[k], b = im[k], c = re[m - k], d = im[m - k];
        const float er = 0.5f * (a + c), ei = 0.5f * (b - d);
        const float dr = 0.5f * (a - c), di = 0.5f * (b + d);
        const float wr = cosTable[(size_t) k], wi = sinTable[(size_t) k];   // W^-k
        const float orr = dr * wr - di * wi;
        const float oi = dr * wi + di * wr;

        const int r = bitrev[(size_t) (2 * k)];
        out[2 * r]     = er - oi;   // Re (E + iO)
        out[2 * r + 1] = ei + orr;  // Im (E + iO)
    }

    butterflies<2> (out, out + 1, m, true);

    const float scale = 1.0f / (float) m;
    for (int i = 0; i < n; ++i) out[i] *= scale;
}

} // namespace verdant
