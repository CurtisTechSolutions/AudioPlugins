#include "TestHarness.h"

#include <ap/Crossover.h>
#include <ap/DelayLine.h>
#include <ap/Loudness.h>
#include <ap/PinkNoise.h>
#include <ap/Saturation.h>
#include <ap/TransientDetector.h>

#include <cmath>
#include <vector>

using namespace ap;

namespace
{
    double rmsOf (const std::vector<float>& v, size_t from = 0)
    {
        double acc = 0.0;
        for (size_t i = from; i < v.size(); ++i) acc += (double) v[i] * v[i];
        return std::sqrt (acc / (double) (v.size() - from));
    }

    /** Goertzel magnitude of a frequency in a buffer (relative units). */
    double toneLevel (const std::vector<float>& v, double sr, double hz, size_t from = 0)
    {
        const double w = 2.0 * kPiD * hz / sr;
        const double c = 2.0 * std::cos (w);
        double s0 = 0, s1 = 0, s2 = 0;
        for (size_t i = from; i < v.size(); ++i)
        {
            s0 = v[i] + c * s1 - s2;
            s2 = s1; s1 = s0;
        }
        const double power = s1 * s1 + s2 * s2 - c * s1 * s2;
        return std::sqrt (std::max (0.0, power)) / (double) (v.size() - from) * 2.0;
    }
}

AP_TEST (linkwitz_riley_sums_flat)
{
    const double sr = 48000.0;
    LinkwitzRiley4 xo;
    xo.prepare (sr, 1000.0f);
    for (double hz : { 100.0, 700.0, 1000.0, 1400.0, 5000.0 })
    {
        xo.reset();
        std::vector<float> sum (8192);
        for (size_t i = 0; i < sum.size(); ++i)
        {
            float lo, hi;
            xo.process ((float) std::sin (kTwoPiD * hz * (double) i / sr), lo, hi);
            sum[i] = lo + hi;
        }
        CHECK_NEAR (rmsOf (sum, 4096), 0.7071, 0.01);   // flat magnitude
    }
}

AP_TEST (three_band_sums_flat)
{
    const double sr = 48000.0;
    ThreeBandSplitter split;
    split.prepare (sr, 120.0f, 2500.0f);
    for (double hz : { 60.0, 120.0, 400.0, 2500.0, 8000.0 })
    {
        split.reset();
        std::vector<float> sum (8192);
        for (size_t i = 0; i < sum.size(); ++i)
        {
            float lo, mid, hi;
            split.process ((float) std::sin (kTwoPiD * hz * (double) i / sr), lo, mid, hi);
            sum[i] = lo + mid + hi;
        }
        CHECK_NEAR (rmsOf (sum, 4096), 0.7071, 0.015);
    }
}

AP_TEST (tube_shaper_makes_even_harmonics)
{
    const double sr = 48000.0;
    TubeShaper tube;
    tube.drive = 3.0f;
    tube.bias = 0.5f;
    DcBlocker dc;
    dc.prepare (sr);
    std::vector<float> out (16384);
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = dc.process (tube.process (0.5f * (float) std::sin (kTwoPiD * 200.0 * (double) i / sr)));

    const double h1 = toneLevel (out, sr, 200.0, 4096);
    const double h2 = toneLevel (out, sr, 400.0, 4096);
    const double h3 = toneLevel (out, sr, 600.0, 4096);
    CHECK (h2 > 0.02 * h1);       // audible 2nd harmonic
    CHECK (h2 > h3);              // even-dominant
    double mean = 0.0;
    for (size_t i = 4096; i < out.size(); ++i) mean += out[i];
    CHECK (std::fabs (mean / (double) (out.size() - 4096)) < 1e-3);   // DC removed

    // Small signals pass at unity for any drive.
    TubeShaper t2; t2.drive = 8.0f;
    CHECK_NEAR (t2.process (0.001f) / 0.001f, 1.0f, 0.03f);
}

AP_TEST (exciter_shaper_unity_and_even)
{
    ExciterShaper ex;
    ex.drive = 4.0f;
    CHECK_NEAR (ex.process (0.001f) / 0.001f, 1.0f, 0.03f);
    CHECK (std::fabs (ex.process (0.2f) + ex.process (-0.2f)) > 1.0e-3f);   // asymmetric: even term
}

AP_TEST (transient_detector_fires_on_onsets)
{
    const double sr = 48000.0;
    TransientDetector det;
    det.prepare (sr);
    int onsets = 0;
    float maxAttack = 0.0f;
    // Four bursts 250 ms apart, each a 20 ms decaying click on top of silence.
    for (int i = 0; i < 48000; ++i)
    {
        const int t = i % 12000;
        const float x = t < 960 ? (float) (std::sin (kTwoPiD * 1000.0 * i / sr) * std::exp (-t / 200.0)) : 0.0f;
        if (det.process (x)) ++onsets;
        maxAttack = std::max (maxAttack, det.attackAmount());
    }
    CHECK_EQ (onsets, 4);
    CHECK (maxAttack > 0.5f);

    // A steady tone produces no onsets and no attack after settling.
    det.reset();
    onsets = 0;
    float lateAttack = 0.0f;
    for (int i = 0; i < 48000; ++i)
    {
        if (det.process ((float) std::sin (kTwoPiD * 440.0 * i / sr)) && i > 4800) ++onsets;
        if (i > 24000) lateAttack = std::max (lateAttack, det.attackAmount());
    }
    CHECK_EQ (onsets, 0);
    CHECK (lateAttack < 0.05f);
}

AP_TEST (loudness_meter_reads_sine_level)
{
    const double sr = 48000.0;
    LoudnessMeter meter;
    meter.prepare (sr, 2);
    std::vector<float> l (48000 * 4), r (48000 * 4);
    const float amp = dbToGain (-20.0f);    // -20 dBFS peak sine on both channels
    for (size_t i = 0; i < l.size(); ++i)
        l[i] = r[i] = amp * (float) std::sin (kTwoPiD * 1000.0 * (double) i / sr);
    meter.process (l.data(), r.data(), (int) l.size());
    CHECK (meter.hasShortTerm());
    // Stereo 1 kHz sine at -20 dBFS: each channel mean square = amp^2/2, sum -> -23 dB + 3 dB ... = -20.69 LUFS - ... ;
    // BS.1770 states a -20 dBFS stereo sine reads about -20.0 LUFS (±0.1).
    CHECK_NEAR (meter.shortTermLufs(), -20.0f, 0.3f);
    CHECK_NEAR (meter.momentaryLufs(), -20.0f, 0.3f);
}

AP_TEST (pink_noise_tilts_down)
{
    const double sr = 48000.0;
    PinkNoise pink (42);
    std::vector<float> v (1 << 16);
    for (auto& s : v) s = pink.next();
    CHECK (rmsOf (v) > 0.05 && rmsOf (v) < 1.0);
    // Energy near 500 Hz should exceed energy near 8 kHz by roughly 12 dB (1/f over 4 octaves).
    double lowE = 0.0, highE = 0.0;
    for (double hz = 450; hz <= 550; hz += 10) lowE += std::pow (toneLevel (v, sr, hz), 2.0);
    for (double hz = 7200; hz <= 8800; hz += 160) highE += std::pow (toneLevel (v, sr, hz), 2.0);
    const double tiltDb = 10.0 * std::log10 (lowE / highE);
    CHECK (tiltDb > 8.0 && tiltDb < 16.0);
}

AP_TEST (drift_source_is_slow_and_bounded)
{
    DriftSource drift (7);
    drift.prepare (100.0, 2.0);        // 100 control steps per second
    std::vector<float> v (6000);       // 60 s
    for (auto& s : v) s = drift.next();
    float lo = 1e9f, hi = -1e9f;
    double mean = 0.0;
    for (float s : v) { lo = std::min (lo, s); hi = std::max (hi, s); mean += s; }
    mean /= (double) v.size();
    CHECK (lo >= -1.0f && hi <= 1.0f);
    CHECK (hi - lo > 0.3f);                 // it actually moves
    CHECK (std::fabs (mean) < 0.5);         // and isn't stuck at a rail
    // Autocorrelation at 1 s lag is high: slow drift, not jitter.
    double num = 0.0, den = 0.0;
    for (size_t i = 100; i < v.size(); ++i) { num += (v[i] - mean) * (v[i - 100] - mean); den += (v[i] - mean) * (v[i] - mean); }
    CHECK (num / den > 0.35);   // fastest row is 2 Hz, so 1 s correlation is moderate, not jitter
}

AP_TEST (delay_line_delays)
{
    DelayLine d;
    d.prepare (100);
    for (int i = 0; i < 200; ++i)
    {
        const float out = d.process ((float) i, 10);
        if (i >= 10) CHECK_NEAR (out, (float) (i - 10), 1e-6);
    }
    CHECK_NEAR (d.read (0), 199.0f, 1e-6);
    CHECK_NEAR (d.readFractional (2.5f), 196.5f, 1e-6);
}
