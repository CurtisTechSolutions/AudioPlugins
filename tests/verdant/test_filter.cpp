// VoiceFilter + filterMagnitudeDb (CONTRACT §3 Filter semantics, §8 filter tests).

#include "TestHarness.h"

#include "engine/Common.h"
#include "engine/Filter.h"

#include <ap/Math.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

using namespace verdant;

namespace
{
    constexpr FilterType kAllTypes[] = { FilterType::LP12, FilterType::LP24, FilterType::HP12, FilterType::HP24,
                                         FilterType::BP12, FilterType::BP24, FilterType::Notch, FilterType::CombPlus,
                                         FilterType::CombMinus, FilterType::Formant, FilterType::Ladder };

    /** Runs `in` (mono, copied to both channels) through a filter in control blocks,
        calling set() before each block like the voice does. Returns the left output. */
    std::vector<float> runFilter (VoiceFilter& f, const std::vector<float>& in, FilterType t, float cutoff, float res,
                                  float drive = 0.0f, std::vector<float>* right = nullptr)
    {
        std::vector<float> L (in), R (in);
        for (size_t i = 0; i < in.size(); i += (size_t) kControlInterval)
        {
            const int n = (int) std::min<size_t> ((size_t) kControlInterval, in.size() - i);
            f.set (t, cutoff, res, drive);
            f.process (L.data() + i, R.data() + i, n);
        }
        if (right != nullptr)
            *right = R;
        return L;
    }

    std::vector<float> sine (double hz, double sr, int n, float amp)
    {
        std::vector<float> v ((size_t) n);
        for (int i = 0; i < n; ++i)
            v[(size_t) i] = amp * (float) std::sin (ap::kTwoPiD * hz * (double) i / sr);
        return v;
    }

    /** Amplitude of the `hz` component of v[from..] (single-bin DFT). */
    double toneAmplitude (const std::vector<float>& v, double hz, double sr, size_t from)
    {
        std::complex<double> acc (0.0, 0.0);
        for (size_t i = from; i < v.size(); ++i)
            acc += (double) v[i] * std::exp (std::complex<double> (0.0, -ap::kTwoPiD * hz * (double) i / sr));
        return 2.0 * std::abs (acc) / (double) (v.size() - from);
    }

    /** Steady-state gain (dB) of a filter for a sine at `hz`. */
    double measuredGainDb (FilterType t, float cutoff, float res, double sr, double hz, float amp = 0.25f)
    {
        VoiceFilter f;
        f.prepare (sr);
        const int n = (int) (0.6 * sr);
        const auto out = runFilter (f, sine (hz, sr, n, amp), t, cutoff, res);
        const double a = toneAmplitude (out, hz, sr, (size_t) (0.25 * sr));
        return 20.0 * std::log10 (std::max (a, 1.0e-12) / (double) amp);
    }

    std::vector<float> whiteNoise (int n, uint32_t seed, float amp)
    {
        ap::Rng rng;
        rng.state = seed;
        std::vector<float> v ((size_t) n);
        for (auto& x : v)
            x = amp * rng.nextBipolar();
        return v;
    }
}

AP_TEST (filter_stable_every_type_cutoff_res_rate)
{
    // Every type x cutoff x resonance x rate, white noise +-0.5 for 0.2 s.
    int bad = 0;
    for (double sr : { 44100.0, 192000.0 })
    {
        const auto noise = whiteNoise ((int) (0.2 * sr), 0xC0FFEEu, 0.5f);
        for (FilterType t : kAllTypes)
            for (float cutoff : { 20.0f, 200.0f, 2000.0f, 20000.0f })
                for (float res : { 0.0f, 0.5f, 1.0f })
                {
                    VoiceFilter f;
                    f.prepare (sr);
                    std::vector<float> right;
                    const auto out = runFilter (f, noise, t, cutoff, res, 0.0f, &right);
                    for (size_t i = 0; i < out.size(); ++i)
                        if (! std::isfinite (out[i]) || std::fabs (out[i]) >= 10.0f
                            || ! std::isfinite (right[i]) || std::fabs (right[i]) >= 10.0f)
                        {
                            ++bad;
                            break;
                        }
                }
    }
    CHECK_EQ (bad, 0);
}

AP_TEST (filter_stable_under_cutoff_res_drive_sweeps)
{
    // Exponential cutoff sweeps 20 Hz <-> 20 kHz with resonance and drive moving
    // every control block, loud noise: never NaN, never runaway.
    int bad = 0;
    for (double sr : { 44100.0, 192000.0 })
        for (FilterType t : kAllTypes)
        {
            VoiceFilter f;
            f.prepare (sr);
            const int n = (int) (0.5 * sr);
            auto L = whiteNoise (n, 1234u, 1.0f);
            auto R = whiteNoise (n, 5678u, 1.0f);
            for (int i = 0, block = 0; i < n; i += kControlInterval, ++block)
            {
                const double x = (double) i / n;
                const float cutoff = 20.0f * std::pow (1000.0f, (float) (0.5 - 0.5 * std::cos (ap::kTwoPiD * 3.0 * x)));
                const float res = (float) (0.5 + 0.5 * std::sin (ap::kTwoPiD * 7.0 * x));
                const float drive = (block % 50) < 25 ? 0.0f : 1.0f;
                const int m = std::min (kControlInterval, n - i);
                f.set (t, cutoff, res, drive);
                f.process (L.data() + i, R.data() + i, m);
            }
            for (int i = 0; i < n; ++i)
                if (! std::isfinite (L[(size_t) i]) || std::fabs (L[(size_t) i]) >= 10.0f
                    || ! std::isfinite (R[(size_t) i]) || std::fabs (R[(size_t) i]) >= 10.0f)
                {
                    ++bad;
                    break;
                }
        }
    CHECK_EQ (bad, 0);

    // NaN / inf parameters are rejected, not propagated.
    VoiceFilter f;
    f.prepare (48000.0);
    auto L = whiteNoise (256, 9u, 0.5f), R = L;
    f.set (FilterType::LP24, std::nanf (""), std::nanf (""), INFINITY);
    f.process (L.data(), R.data(), 256);
    bool finite = true;
    for (float v : L) finite = finite && std::isfinite (v);
    CHECK (finite);
}

AP_TEST (filter_slopes_lp24_lp12_hp)
{
    const double sr = 48000.0;
    // LP 24 at 1 kHz: 4 kHz vs 250 Hz ~ -48 dB over two octaves.
    const double lp24 = measuredGainDb (FilterType::LP24, 1000.0f, 0.0f, sr, 4000.0) - measuredGainDb (FilterType::LP24, 1000.0f, 0.0f, sr, 250.0);
    CHECK_NEAR (lp24, -48.0, 4.0);
    const double lp12 = measuredGainDb (FilterType::LP12, 1000.0f, 0.0f, sr, 4000.0) - measuredGainDb (FilterType::LP12, 1000.0f, 0.0f, sr, 250.0);
    CHECK_NEAR (lp12, -24.0, 3.0);

    // HP mirrors LP: 250 Hz vs 4 kHz.
    const double hp24 = measuredGainDb (FilterType::HP24, 1000.0f, 0.0f, sr, 250.0) - measuredGainDb (FilterType::HP24, 1000.0f, 0.0f, sr, 4000.0);
    CHECK_NEAR (hp24, -48.0, 4.0);
    const double hp12 = measuredGainDb (FilterType::HP12, 1000.0f, 0.0f, sr, 250.0) - measuredGainDb (FilterType::HP12, 1000.0f, 0.0f, sr, 4000.0);
    CHECK_NEAR (hp12, -24.0, 3.0);

    // Passbands are flat.
    CHECK_NEAR (measuredGainDb (FilterType::LP24, 1000.0f, 0.0f, sr, 100.0), 0.0, 0.5);
    CHECK_NEAR (measuredGainDb (FilterType::HP24, 1000.0f, 0.0f, sr, 10000.0), 0.0, 0.5);
}

AP_TEST (filter_bandpass_peaks_at_cutoff_and_notch_kills_it)
{
    const double sr = 48000.0;
    for (FilterType t : { FilterType::BP12, FilterType::BP24 })
    {
        const double atCutoff = measuredGainDb (t, 1000.0f, 0.5f, sr, 1000.0);
        CHECK_NEAR (atCutoff, 0.0, 1.0);   // unity-gain peak
        CHECK (measuredGainDb (t, 1000.0f, 0.5f, sr, 500.0) < atCutoff - 6.0);
        CHECK (measuredGainDb (t, 1000.0f, 0.5f, sr, 2000.0) < atCutoff - 6.0);
    }

    const double notch = measuredGainDb (FilterType::Notch, 1000.0f, 0.3f, sr, 1000.0);
    const double pass  = measuredGainDb (FilterType::Notch, 1000.0f, 0.3f, sr, 250.0);
    CHECK (pass - notch > 30.0);
    CHECK_NEAR (pass, 0.0, 1.0);
}

AP_TEST (filter_comb_peaks_at_cutoff_multiples)
{
    const double sr = 48000.0;
    for (float cutoff : { 300.0f, 1000.0f })
    {
        const double atF   = measuredGainDb (FilterType::CombPlus, cutoff, 0.8f, sr, cutoff);
        const double at2F  = measuredGainDb (FilterType::CombPlus, cutoff, 0.8f, sr, 2.0 * cutoff);
        const double at15F = measuredGainDb (FilterType::CombPlus, cutoff, 0.8f, sr, 1.5 * cutoff);
        CHECK (atF - at15F > 6.0);
        CHECK (at2F - at15F > 6.0);
    }

    // Comb -: peaks at odd multiples of cutoff / 2, a dip at the cutoff.
    const double minusHalf = measuredGainDb (FilterType::CombMinus, 1000.0f, 0.8f, sr, 500.0);
    const double minusF    = measuredGainDb (FilterType::CombMinus, 1000.0f, 0.8f, sr, 1000.0);
    CHECK (minusHalf - minusF > 6.0);
}

AP_TEST (filter_formant_vowel_a_and_ladder)
{
    const double sr = 48000.0;
    // Cutoff at the bottom of the range = vowel "A": strong ~700 Hz, weak 3 kHz.
    const double f700 = measuredGainDb (FilterType::Formant, 20.0f, 0.15f, sr, 700.0);
    const double f3k  = measuredGainDb (FilterType::Formant, 20.0f, 0.15f, sr, 3000.0);
    CHECK (f700 - f3k > 12.0);
    CHECK (f700 > -6.0);

    // Moving along the vowel path changes the response (the "I" end has F1 near 270 Hz).
    const double i300 = measuredGainDb (FilterType::Formant, 632.0f, 0.15f, sr, 270.0);   // log-midpoint = "I"
    const double a300 = measuredGainDb (FilterType::Formant, 20.0f, 0.15f, sr, 270.0);
    CHECK (i300 - a300 > 6.0);

    // Ladder at 5 kHz: 100 Hz passes within 3 dB, 10 kHz down > 12 dB.
    CHECK_NEAR (measuredGainDb (FilterType::Ladder, 5000.0f, 0.0f, sr, 100.0, 0.5f), 0.0, 3.0);
    CHECK_NEAR (measuredGainDb (FilterType::Ladder, 5000.0f, 0.15f, sr, 100.0, 0.5f), 0.0, 3.0);
    CHECK (measuredGainDb (FilterType::Ladder, 5000.0f, 0.15f, sr, 10000.0, 0.5f) < -12.0);

    // Ladder resonance peaks near the cutoff.
    const double resPeak = measuredGainDb (FilterType::Ladder, 1000.0f, 0.8f, sr, 900.0, 0.05f);
    const double resLow  = measuredGainDb (FilterType::Ladder, 1000.0f, 0.8f, sr, 100.0, 0.05f);
    CHECK (resPeak > resLow);
}

AP_TEST (filter_cutoff_jump_is_ramped)
{
    // 100 Hz sine, cutoff jumps 200 -> 8000 Hz between two control blocks: no
    // output step larger than 0.2 (the sine's own step is ~0.013).
    const double sr = 48000.0;
    const int n = (int) (0.4 * sr);
    const auto in = sine (100.0, sr, n, 0.5f);
    for (FilterType t : { FilterType::LP12, FilterType::LP24, FilterType::HP12, FilterType::HP24, FilterType::BP12,
                          FilterType::Notch, FilterType::Ladder, FilterType::Formant, FilterType::CombPlus })
    {
        VoiceFilter f;
        f.prepare (sr);
        std::vector<float> L (in), R (in);
        float maxStep = 0.0f;
        for (int i = 0; i < n; i += kControlInterval)
        {
            f.set (t, i < n / 2 ? 200.0f : 8000.0f, 0.15f, 0.0f);
            f.process (L.data() + i, R.data() + i, std::min (kControlInterval, n - i));
        }
        for (int i = n / 4; i < n; ++i)
            maxStep = std::max (maxStep, std::fabs (L[(size_t) i] - L[(size_t) i - 1]));
        CHECK (maxStep < 0.2f);
    }
}

AP_TEST (filter_magnitude_matches_measurement)
{
    const double sr = 48000.0;
    struct Case { FilterType t; float cutoff, res; };
    const Case cases[] = {
        { FilterType::LP12, 1000.0f, 0.3f }, { FilterType::LP24, 1000.0f, 0.3f }, { FilterType::HP12, 1000.0f, 0.3f },
        { FilterType::HP24, 2000.0f, 0.5f }, { FilterType::BP12, 1500.0f, 0.4f }, { FilterType::Notch, 1000.0f, 0.2f },
        { FilterType::Formant, 300.0f, 0.3f }, { FilterType::CombPlus, 400.0f, 0.5f }, { FilterType::Ladder, 2000.0f, 0.4f },
    };
    for (const auto& c : cases)
        for (double hz : { 300.0, 1100.0, 3500.0 })
        {
            const double predicted = filterMagnitudeDb (c.t, c.cutoff, c.res, sr, (float) hz);
            const double measured  = measuredGainDb (c.t, c.cutoff, c.res, sr, hz, 0.05f);
            if (measured < -60.0)
                continue;   // deep in a stopband the DFT floor dominates
            CHECK_NEAR (measured, predicted, 1.5);
        }

    // Finite for any input.
    bool finite = true;
    for (FilterType t : kAllTypes)
        for (float hz : { 0.0f, -5.0f, 1.0f, 1000.0f, 22050.0f, 1.0e9f, std::nanf ("") })
            for (float cutoff : { -1.0f, 20.0f, 1.0e6f, std::nanf ("") })
                for (double rate : { 0.0, 44100.0, 192000.0 })
                    finite = finite && std::isfinite (filterMagnitudeDb (t, cutoff, 1.0f, rate, hz));
    CHECK (finite);
    finite = std::isfinite (filterMagnitudeDb ((FilterType) 200, 1000.0f, 0.5f, 48000.0, 1000.0f));
    CHECK (finite);
}

AP_TEST (filter_drive_and_stereo_independence)
{
    const double sr = 48000.0;
    // Drive saturates: a loud sine through an open LP gets compressed, a quiet one barely.
    const double loudClean  = measuredGainDb (FilterType::LP12, 18000.0f, 0.0f, sr, 200.0, 0.9f);
    VoiceFilter f;
    f.prepare (sr);
    const auto driven = runFilter (f, sine (200.0, sr, (int) (0.5 * sr), 0.9f), FilterType::LP12, 18000.0f, 0.0f, 1.0f);
    const double loudDriven = 20.0 * std::log10 (toneAmplitude (driven, 200.0, sr, (size_t) (0.2 * sr)) / 0.9);
    CHECK (loudDriven < loudClean - 3.0);

    // Left and right keep separate state: silence on R stays silence.
    VoiceFilter g;
    g.prepare (sr);
    auto L = whiteNoise (4096, 77u, 0.5f);
    std::vector<float> R (4096, 0.0f);
    for (int i = 0; i < 4096; i += kControlInterval)
    {
        g.set (FilterType::LP24, 1000.0f, 0.9f, 0.0f);
        g.process (L.data() + i, R.data() + i, kControlInterval);
    }
    float peakR = 0.0f;
    for (float v : R) peakR = std::max (peakR, std::fabs (v));
    CHECK (peakR == 0.0f);
}
