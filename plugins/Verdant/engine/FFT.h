#pragma once

// Radix-2 complex FFT (iterative, split real / imaginary arrays) with real
// helpers. Sizes are powers of two from 2 to 65536; any other size is rounded
// up to the next power of two (see size()). Tables are built in the
// constructor; the transforms never allocate and are safe to call
// concurrently on one object (they keep no state).
//
// Conventions: forward X[k] = sum x[n] e^(-2 pi i k n / N), unscaled;
// inverse x[n] = (1/N) sum X[k] e^(+2 pi i k n / N), so inverse(forward(x)) == x.

#include <vector>

namespace verdant
{

class FFT
{
public:
    explicit FFT (int size);

    int size() const noexcept { return n; }

    /** Complex transforms. `im` may be null (all-zero imaginary input). The output may alias the input. */
    void forward (const float* re, const float* im, float* outRe, float* outIm) const noexcept;
    void inverse (const float* re, const float* im, float* outRe, float* outIm) const noexcept;

    /** Real input of size() samples -> size()/2 + 1 bins (DC .. Nyquist), unscaled like forward(). `in` must not alias re / im. */
    void realForward (const float* in, float* re, float* im) const noexcept;

    /** Inverse of realForward: size()/2 + 1 bins -> size() real samples (scaled by 1/N). `out` must not alias re / im. */
    void realInverse (const float* re, const float* im, float* out) const noexcept;

private:
    template <int Stride>
    void butterflies (float* re, float* im, int m, bool inverseTransform) const noexcept;
    void complexTransform (const float* re, const float* im, float* outRe, float* outIm, bool inverseTransform) const noexcept;

    int n = 2, log2n = 1;
    std::vector<float> cosTable, sinTable;   // cos / sin (2 pi k / n), k < n / 2
    std::vector<int> bitrev;                 // bit reversal for size n (size n/2 uses bitrev[2k])
};

} // namespace verdant
