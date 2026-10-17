#include "TestHarness.h"

#include <ap/Filters.h>
#include <ap/Oscillator.h>
#include <ap/PitchDetector.h>

#include <cmath>
#include <vector>

using namespace ap;

namespace
{
    std::vector<float> render (PolyBlepOscillator& osc, int n)
    {
        std::vector<float> v ((size_t) n);
        for (auto& s : v) s = osc.next();
        return v;
    }

    double rmsOf (const std::vector<float>& v)
    {
        double acc = 0.0;
        for (float s : v) acc += (double) s * s;
        return std::sqrt (acc / (double) v.size());
    }

    double meanOf (const std::vector<float>& v)
    {
        double acc = 0.0;
        for (float s : v) acc += s;
        return acc / (double) v.size();
    }
}

AP_TEST (oscillator_waveforms_are_bounded_and_pitched)
{
    const double sr = 48000.0;
    YinPitchDetector yin;
    yin.prepare (sr, 1024, 50.0f, 1500.0f);

    for (auto wave : { Waveform::Sine, Waveform::Triangle, Waveform::Saw, Waveform::Square })
    {
        PolyBlepOscillator osc;
        osc.prepare (sr);
        osc.setWaveform (wave);
        osc.setFrequency (220.0f);
        osc.reset (wave == Waveform::Triangle ? 0.25f : 0.0f);

        auto v = render (osc, 8192);
        float peak = 0.0f;
        for (float s : v) peak = std::max (peak, std::fabs (s));
        CHECK (peak <= 1.2f);
        CHECK (peak > 0.6f);
        CHECK (std::fabs (meanOf (v)) < 0.05);

        const auto r = yin.analyze (v.data() + 2048);
        CHECK (r.voiced);
        CHECK_NEAR (r.frequency, 220.0f, 1.5f);
    }
}

AP_TEST (oscillator_saw_is_band_limited)
{
    // A naive saw at high frequency aliases heavily; PolyBLEP should keep energy
    // above Nyquist/2 well below the fundamental. Compare the energy of a
    // high-pass filtered signal against the full signal.
    const double sr = 44100.0;
    PolyBlepOscillator osc;
    osc.prepare (sr);
    osc.setWaveform (Waveform::Saw);
    osc.setFrequency (4000.0f);
    auto v = render (osc, 16384);

    Biquad hp;
    hp.configure (Biquad::Type::HighPass, sr, 16000.0f, 0.707f);
    std::vector<float> filtered (v.size());
    for (size_t i = 0; i < v.size(); ++i) filtered[i] = hp.process (v[i]);

    const double full = rmsOf (v);
    const double high = rmsOf (std::vector<float> (filtered.begin() + 2048, filtered.end()));
    CHECK (high / full < 0.35);   // naive saw at 4 kHz gives ~0.5+ here
}

AP_TEST (biquad_lowpass_attenuates_highs)
{
    const double sr = 48000.0;
    Biquad lp;
    lp.configure (Biquad::Type::LowPass, sr, 1000.0f, 0.707f);

    auto run = [&] (double hz)
    {
        lp.reset();
        std::vector<float> v (8192);
        for (size_t i = 0; i < v.size(); ++i)
            v[i] = lp.process ((float) std::sin (kTwoPiD * hz * (double) i / sr));
        return rmsOf (std::vector<float> (v.begin() + 2048, v.end()));
    };

    const double low  = run (100.0);
    const double mid  = run (1000.0);
    const double high = run (10000.0);
    CHECK_NEAR (low, 0.7071, 0.02);
    CHECK_NEAR (mid / low, 0.7071, 0.05);   // -3 dB at cutoff
    CHECK (high / low < 0.02);              // ~-40 dB a decade above
}

AP_TEST (smoother_converges)
{
    Smoother s;
    s.prepare (48000.0, 10.0f);
    s.snap (0.0f);
    s.setTarget (1.0f);
    float v = 0.0f;
    for (int i = 0; i < 480; ++i) v = s.next();   // 10 ms = one time constant
    CHECK_NEAR (v, 1.0f - std::exp (-1.0f), 0.02f);
    for (int i = 0; i < 48000; ++i) v = s.next();
    CHECK_NEAR (v, 1.0f, 1e-4);
    CHECK (! s.isSmoothing());
}

AP_TEST (lfo_shapes_in_range)
{
    for (int si = 0; si < (int) Lfo::Shape::Count; ++si)
    {
        Lfo lfo;
        lfo.prepare (48000.0);
        lfo.setShape ((Lfo::Shape) si);
        lfo.setRate (3.0f);
        lfo.reset();
        float lo = 1e9f, hi = -1e9f;
        for (int i = 0; i < 48000; ++i)
        {
            const float v = lfo.next();
            lo = std::min (lo, v);
            hi = std::max (hi, v);
        }
        CHECK (lo >= -1.0001f && hi <= 1.0001f);
        CHECK (hi - lo > 0.5f);
    }
}
