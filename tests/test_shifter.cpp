#include "TestHarness.h"

#include <ap/PitchDetector.h>
#include <ap/PitchShifter.h>

#include <cmath>
#include <vector>

using namespace ap;

namespace
{
    std::vector<float> harmonicTone (double sr, double hz, int n)
    {
        std::vector<float> v ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            double s = 0.0;
            for (int h = 1; h <= 8; ++h)
                s += std::sin (kTwoPiD * hz * h * i / sr + 0.3 * h) / h;
            v[(size_t) i] = (float) (s * 0.5);
        }
        return v;
    }

    float measureHz (double sr, const std::vector<float>& v, int from)
    {
        YinPitchDetector yin;
        yin.prepare (sr, 2048, 50.0f, 1500.0f);
        const auto r = yin.analyze (v.data() + from);
        return r.voiced ? r.frequency : 0.0f;
    }

    float rms (const std::vector<float>& v, int from, int n)
    {
        double acc = 0.0;
        for (int i = from; i < from + n; ++i) acc += (double) v[(size_t) i] * v[(size_t) i];
        return (float) std::sqrt (acc / n);
    }
}

AP_TEST (psola_identity_is_delayed_passthrough)
{
    const double sr = 48000.0;
    PsolaShifter shifter;
    shifter.prepare (sr, 50.0f, 512);
    const int latency = shifter.latencySamples();
    CHECK (latency > 0);

    const int n = 24000;
    const auto in = harmonicTone (sr, 220.0, n);
    std::vector<float> out ((size_t) n, 0.0f);

    shifter.setPeriod ((float) (sr / 220.0));
    shifter.setRatio (1.0f);
    for (int pos = 0; pos < n; pos += 512)
        shifter.process (in.data() + pos, out.data() + pos, std::min (512, n - pos));

    // After the latency, output should match the input delayed by `latency`.
    double err = 0.0, ref = 0.0;
    for (int i = latency + 4000; i < n; ++i)
    {
        const double d = out[(size_t) i] - in[(size_t) (i - latency)];
        err += d * d;
        ref += (double) in[(size_t) (i - latency)] * in[(size_t) (i - latency)];
    }
    CHECK (err / ref < 1e-3);
}

AP_TEST (psola_shifts_pitch_and_keeps_level)
{
    const double sr = 48000.0;
    const double inHz = 196.0;
    const int n = 48000;
    const auto in = harmonicTone (sr, inHz, n);

    for (float ratio : { 0.75f, 0.891f, 1.122f, 1.5f, 2.0f })
    {
        PsolaShifter shifter;
        shifter.prepare (sr, 50.0f, 256);
        shifter.setPeriod ((float) (sr / inHz));
        shifter.setRatio (ratio);

        std::vector<float> out ((size_t) n, 0.0f);
        for (int pos = 0; pos < n; pos += 256)
            shifter.process (in.data() + pos, out.data() + pos, std::min (256, n - pos));

        const float hz = measureHz (sr, out, 20000);
        CHECK_NEAR (hz, inHz * ratio, inHz * ratio * 0.01);

        // Formant preservation means the new harmonics sample the original
        // spectral envelope, so a large upward shift of a 1/h harmonic series
        // legitimately loses level (an octave up is ~-6 dB). Near unity it
        // must stay close to the input.
        const float inLevel  = rms (in, 20000, 8000);
        const float outLevel = rms (out, 20000, 8000);
        if (ratio > 0.85f && ratio < 1.2f)
            CHECK (outLevel > inLevel * 0.85f && outLevel < inLevel * 1.2f);
        else
            CHECK (outLevel > inLevel * 0.35f && outLevel < inLevel * 2.0f);
    }
}

AP_TEST (psola_handles_unvoiced_and_period_changes)
{
    const double sr = 44100.0;
    PsolaShifter shifter;
    shifter.prepare (sr, 50.0f, 128);

    const int n = 30000;
    const auto in = harmonicTone (sr, 150.0, n);
    std::vector<float> out ((size_t) n, 0.0f);

    for (int pos = 0; pos < n; pos += 128)
    {
        // Alternate between unvoiced, voiced-with-shift and different periods.
        const int phase = (pos / 2048) % 3;
        if (phase == 0)      { shifter.setPeriod (0.0f); }
        else if (phase == 1) { shifter.setPeriod ((float) (sr / 150.0)); shifter.setRatio (1.3f); }
        else                 { shifter.setPeriod ((float) (sr / 150.0) * 1.1f); shifter.setRatio (0.8f); }
        shifter.process (in.data() + pos, out.data() + pos, std::min (128, n - pos));
    }

    bool finite = true;
    float peak = 0.0f;
    for (float s : out) { finite = finite && std::isfinite (s); peak = std::max (peak, std::fabs (s)); }
    CHECK (finite);
    CHECK (peak < 2.5f);
    CHECK (peak > 0.05f);
}
