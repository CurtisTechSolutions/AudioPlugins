// FFT: complex and real round trips for every supported size, a known
// spectrum, real/complex agreement and size rounding.

#include "TestHarness.h"

#include "engine/FFT.h"

#include <ap/Math.h>

#include <algorithm>
#include <cmath>
#include <vector>

using verdant::FFT;

namespace
{
    std::vector<float> noise (int n, uint32_t seed)
    {
        ap::Rng rng;
        rng.state = seed;
        std::vector<float> v ((size_t) n);
        for (auto& x : v) x = rng.nextBipolar();
        return v;
    }

    double maxAbsDiff (const std::vector<float>& a, const std::vector<float>& b)
    {
        double m = 0.0;
        for (size_t i = 0; i < a.size(); ++i) m = std::max (m, (double) std::fabs (a[i] - b[i]));
        return m;
    }
}

AP_TEST (fft_size_rounding)
{
    CHECK_EQ (FFT (1).size(), 2);
    CHECK_EQ (FFT (2).size(), 2);
    CHECK_EQ (FFT (1000).size(), 1024);
    CHECK_EQ (FFT (2048).size(), 2048);
    CHECK_EQ (FFT (65536).size(), 65536);
    CHECK_EQ (FFT (100000).size(), 65536);
}

AP_TEST (fft_complex_round_trip_all_sizes)
{
    for (int n = 2; n <= 65536; n *= 2)
    {
        const FFT fft (n);
        const auto re = noise (n, 0x1234u + (uint32_t) n), im = noise (n, 0x9876u + (uint32_t) n);
        std::vector<float> fr ((size_t) n), fi ((size_t) n), br ((size_t) n), bi ((size_t) n);
        fft.forward (re.data(), im.data(), fr.data(), fi.data());
        fft.inverse (fr.data(), fi.data(), br.data(), bi.data());
        CHECK (maxAbsDiff (re, br) < 1.0e-4);
        CHECK (maxAbsDiff (im, bi) < 1.0e-4);

        // In place (output aliases input).
        std::vector<float> r2 = re, i2 = im;
        fft.forward (r2.data(), i2.data(), r2.data(), i2.data());
        CHECK (maxAbsDiff (r2, fr) < 1.0e-6 * n);
        fft.inverse (r2.data(), i2.data(), r2.data(), i2.data());
        CHECK (maxAbsDiff (re, r2) < 1.0e-4);
    }
}

AP_TEST (fft_real_round_trip_all_sizes)
{
    for (int n = 2; n <= 65536; n *= 2)
    {
        const FFT fft (n);
        const auto x = noise (n, 0x5151u + (uint32_t) n);
        std::vector<float> re ((size_t) n / 2 + 1), im ((size_t) n / 2 + 1), back ((size_t) n);
        fft.realForward (x.data(), re.data(), im.data());
        fft.realInverse (re.data(), im.data(), back.data());
        CHECK (maxAbsDiff (x, back) < 1.0e-4);

        // realForward == the first n/2 + 1 bins of the complex transform.
        std::vector<float> cr ((size_t) n), ci ((size_t) n);
        fft.forward (x.data(), nullptr, cr.data(), ci.data());
        double worst = 0.0;
        for (int k = 0; k <= n / 2; ++k)
        {
            worst = std::max (worst, (double) std::fabs (re[(size_t) k] - cr[(size_t) k]));
            worst = std::max (worst, (double) std::fabs (im[(size_t) k] - ci[(size_t) k]));
        }
        CHECK (worst < 2.0e-5 * n + 1.0e-4);
    }
}

AP_TEST (fft_known_spectrum)
{
    const int n = 1024;
    const FFT fft (n);
    std::vector<float> x ((size_t) n);
    for (int i = 0; i < n; ++i)
        x[(size_t) i] = (float) (0.75 + std::cos (ap::kTwoPiD * 5.0 * i / n) + 0.5 * std::sin (ap::kTwoPiD * 12.0 * i / n));

    std::vector<float> re ((size_t) n / 2 + 1), im ((size_t) n / 2 + 1);
    fft.realForward (x.data(), re.data(), im.data());

    CHECK_NEAR (re[0], 0.75 * n, 1.0e-2);           // DC
    CHECK_NEAR (re[5], 0.5 * n, 1.0e-2);            // cos -> real, N/2
    CHECK_NEAR (im[5], 0.0, 1.0e-2);
    CHECK_NEAR (re[12], 0.0, 1.0e-2);
    CHECK_NEAR (im[12], -0.25 * n, 1.0e-2);         // 0.5 sin -> -i N/4
    double other = 0.0;
    for (int k = 1; k <= n / 2; ++k)
        if (k != 5 && k != 12) other = std::max (other, (double) std::hypot (re[(size_t) k], im[(size_t) k]));
    CHECK (other < 1.0e-2);

    // A pure tone in the complex transform lands in bin k and its mirror.
    std::vector<float> cr ((size_t) n), ci ((size_t) n), zero ((size_t) n, 0.0f);
    for (int i = 0; i < n; ++i) x[(size_t) i] = (float) std::cos (ap::kTwoPiD * 100.0 * i / n);
    fft.forward (x.data(), zero.data(), cr.data(), ci.data());
    CHECK_NEAR (cr[100], 0.5 * n, 1.0e-2);
    CHECK_NEAR (cr[(size_t) n - 100], 0.5 * n, 1.0e-2);
    CHECK_NEAR (cr[101], 0.0, 1.0e-2);
}
