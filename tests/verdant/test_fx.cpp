// FX rack and effect units (W-G): stability at 44.1k / 192k, exact bypass,
// duplicate-slot and ordering rules, per-effect behaviour (CONTRACT §8 numbers),
// tail estimates and click-free parameter / slot changes.

#include "TestHarness.h"
#include "FxTestHelpers.h"

#include "engine/Fx.h"
#include "engine/Params.h"

#include <ap/PinkNoise.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

using namespace verdant;
using namespace fxtest;

namespace
{
    constexpr int kBlock = 512;

    void setP (ParamValues& v, P p, float value) { v[(size_t) p] = value; }

    ParamValues withSlots (std::initializer_list<FxType> types)
    {
        auto v = defaultValues();
        int s = 0;
        for (auto t : types) setP (v, fxSlotType (s++), (float) t);
        return v;
    }

    std::vector<float> pink (size_t n, uint32_t seed, float gain = 0.5f)
    {
        ap::PinkNoise noise (seed);
        std::vector<float> v (n);
        for (auto& x : v) x = noise.next() * gain;
        return v;
    }

    /** Runs the rack over stereo buffers in blocks; params / transport set every block. */
    void runRack (FxRack& rack, const ParamValues& v, std::vector<float>& L, std::vector<float>& R, const Transport& t = {})
    {
        inBlocks (L, R, kBlock, [&] (float* l, float* r, int n, size_t)
        {
            rack.setParams (v, t);
            rack.process (l, r, n);
        });
    }

    std::unique_ptr<FxRack> makeRack (double sr)
    {
        auto rack = std::make_unique<FxRack>();
        rack->prepare (sr, kBlock);
        return rack;
    }

    template <typename Unit, typename Params>
    void runUnit (Unit& u, const Params& p, std::vector<float>& L, std::vector<float>& R)
    {
        inBlocks (L, R, kBlock, [&] (float* l, float* r, int n, size_t)
        {
            u.set (p);
            u.process (l, r, n);
        });
    }

    /** RMS of consecutive windows of `win` samples. */
    std::vector<double> windowRms (const std::vector<float>& v, size_t win)
    {
        std::vector<double> out;
        for (size_t p = 0; p + win <= v.size(); p += win) out.push_back (rms (v, p, p + win));
        return out;
    }
}

//==============================================================================
AP_TEST (fx_every_type_stable_with_pink_noise)
{
    for (double sr : { 44100.0, 192000.0 })
        for (int t = 1; t < (int) FxType::Count; ++t)
        {
            auto rack = makeRack (sr);
            const auto v = withSlots ({ (FxType) t });
            const size_t n = (size_t) (2.0 * sr);
            auto L = pink (n, 11u + (uint32_t) t), R = pink (n, 97u + (uint32_t) t);
            runRack (*rack, v, L, R);
            CHECK (allFinite (L) && allFinite (R));
            CHECK (peakAbs (L) < 4.0 && peakAbs (R) < 4.0);
            CHECK (rms (L, n / 2, n) > 1.0e-3);   // still produces sound
        }

    // Every distortion curve, hot drive.
    for (int d = 0; d < (int) DistType::Count; ++d)
    {
        auto rack = makeRack (44100.0);
        auto v = withSlots ({ FxType::Distortion });
        setP (v, P::DistType, (float) d);
        setP (v, P::DistDrive, 100.0f);
        const size_t n = 44100;
        auto L = pink (n, 5u), R = pink (n, 6u);
        runRack (*rack, v, L, R);
        CHECK (allFinite (L) && allFinite (R));
        CHECK (peakAbs (L) < 4.0 && peakAbs (R) < 4.0);
    }

    // Extreme feedback settings stay bounded.
    {
        auto rack = makeRack (48000.0);
        auto v = withSlots ({ FxType::Chorus, FxType::Phaser, FxType::Flanger, FxType::Delay, FxType::Reverb });
        setP (v, P::ChorusFeedback, 100.0f);
        setP (v, P::PhaserFeedback, 100.0f);
        setP (v, P::FlangerFeedback, -100.0f);
        setP (v, P::DelayFeedback, 100.0f);
        setP (v, P::ReverbDecay, 100.0f);
        setP (v, P::DelayMix, 100.0f);
        setP (v, P::ReverbMix, 100.0f);
        const size_t n = 3 * 48000;
        auto L = pink (n, 7u, 0.9f), R = pink (n, 8u, 0.9f);
        runRack (*rack, v, L, R);
        CHECK (allFinite (L) && allFinite (R));
        CHECK (peakAbs (L) < 4.0 && peakAbs (R) < 4.0);
    }
}

AP_TEST (fx_none_and_mix_zero_are_bit_exact_bypass)
{
    const size_t n = 20000;
    const auto inL = pink (n, 1u), inR = pink (n, 2u);

    auto exact = [&] (const ParamValues& v)
    {
        auto rack = makeRack (48000.0);
        auto L = inL, R = inR;
        runRack (*rack, v, L, R);
        return std::memcmp (L.data(), inL.data(), n * sizeof (float)) == 0
            && std::memcmp (R.data(), inR.data(), n * sizeof (float)) == 0;
    };

    CHECK (exact (defaultValues()));   // all slots None

    const std::pair<FxType, P> mixes[] = {
        { FxType::Distortion, P::DistMix }, { FxType::Chorus, P::ChorusMix }, { FxType::Phaser, P::PhaserMix },
        { FxType::Flanger, P::FlangerMix }, { FxType::Delay, P::DelayMix },   { FxType::Reverb, P::ReverbMix },
        { FxType::Compressor, P::CompMix },
    };
    for (const auto& [type, mixParam] : mixes)
    {
        auto v = withSlots ({ type });
        setP (v, mixParam, 0.0f);
        CHECK (exact (v));
    }

    // EQ with every band at 0 dB is a bypass too.
    CHECK (exact (withSlots ({ FxType::Eq })));

    // A mix that moves to 0 settles back to an exact bypass.
    {
        auto rack = makeRack (48000.0);
        auto v = withSlots ({ FxType::Chorus });
        auto L = inL, R = inR;
        runRack (*rack, v, L, R);
        setP (v, P::ChorusMix, 0.0f);
        L = inL; R = inR;
        runRack (*rack, v, L, R);   // ramps to 0 within the first 20 ms
        CHECK (std::memcmp (L.data() + 2048, inL.data() + 2048, (n - 2048) * sizeof (float)) == 0);
    }
}

AP_TEST (fx_duplicate_types_run_once_and_order_matters)
{
    const size_t n = 48000;
    const auto inL = pink (n, 3u), inR = pink (n, 4u);

    auto render = [&] (std::initializer_list<FxType> slots, float drive = 30.0f)
    {
        auto rack = makeRack (48000.0);
        auto v = withSlots (slots);
        setP (v, P::DistDrive, drive);
        std::vector<float> L = inL, R = inR;
        runRack (*rack, v, L, R);
        return L;
    };

    const auto oneDelay = render ({ FxType::Delay });
    const auto twoDelays = render ({ FxType::Delay, FxType::Delay });
    CHECK (std::memcmp (oneDelay.data(), twoDelays.data(), n * sizeof (float)) == 0);

    const auto a = render ({ FxType::Delay, FxType::Reverb });
    const auto b = render ({ FxType::None, FxType::Delay, FxType::Reverb, FxType::Delay, FxType::Reverb });
    CHECK (std::memcmp (a.data(), b.data(), n * sizeof (float)) == 0);

    // Order: Distortion -> Delay is not Delay -> Distortion.
    const auto distThenDelay = render ({ FxType::Distortion, FxType::Delay }, 90.0f);
    const auto delayThenDist = render ({ FxType::Delay, FxType::Distortion }, 90.0f);
    double diff = 0.0;
    for (size_t i = 0; i < n; ++i) diff = std::max (diff, (double) std::fabs (distThenDelay[i] - delayThenDist[i]));
    CHECK (diff > 0.01);

    // resolveChain reports the effective order.
    FxType chain[FxRack::kMaxChain];
    const auto v = withSlots ({ FxType::Reverb, FxType::None, FxType::Eq, FxType::Reverb, FxType::Delay });
    const int count = FxRack::resolveChain (v, chain);
    CHECK_EQ (count, 3);
    CHECK (chain[0] == FxType::Reverb && chain[1] == FxType::Eq && chain[2] == FxType::Delay);
}

AP_TEST (fx_distortion_raises_third_harmonic)
{
    const double sr = 48000.0;
    const size_t n = 24000;
    for (int d : { (int) DistType::Soft, (int) DistType::Hard, (int) DistType::Tube, (int) DistType::Fold, (int) DistType::Diode })
    {
        Distortion dist;
        dist.prepare (sr, kBlock);
        DistortionParams p;
        p.type = (DistType) d;
        p.drive = 0.6f;
        p.toneHz = 20000.0f;
        p.mix = 1.0f;
        auto L = sine (n, 200.0, sr, 0.5f), R = L;
        const auto dry = L;
        runUnit (dist, p, L, R);

        const double h1 = toneAmp (L, n / 2, n, 200.0, sr);
        const double h3 = toneAmp (L, n / 2, n, 600.0, sr);
        const double dryH3 = toneAmp (dry, n / 2, n, 600.0, sr);
        CHECK (h1 > 0.01);
        CHECK (h3 / h1 > 0.01);              // > -40 dB re the fundamental
        CHECK (h3 > 100.0 * dryH3 + 1.0e-4);
    }

    // Small signals keep unity gain (drive normalisation), for every memoryless curve.
    for (int d : { (int) DistType::Soft, (int) DistType::Hard, (int) DistType::Fold, (int) DistType::Diode, (int) DistType::Tube })
        for (float g : { 1.0f, 8.0f, 32.0f })
            CHECK_NEAR (Distortion::shape ((DistType) d, 1.0e-4f, g) / 1.0e-4f, 1.0, 0.02);
}

AP_TEST (fx_crush_adds_aliasing)
{
    const double sr = 44100.0;
    const size_t n = 32768;

    auto inharmonicRatio = [&] (const std::vector<float>& v)
    {
        const auto mag = magnitudeSpectrum (v, 0, 32768);
        double inh = 0.0, total = 0.0;
        for (size_t k = 1; k < mag.size(); ++k)
        {
            const double hz = (double) k * sr / 32768.0;
            const double e = (double) mag[k] * mag[k];
            total += e;
            const double h = hz / 3000.0;
            if (std::fabs (h - std::round (h)) * 3000.0 > 60.0) inh += e;
        }
        return inh / std::max (total, 1.0e-30);
    };

    Distortion dist;
    dist.prepare (sr, kBlock);
    DistortionParams p;
    p.type = DistType::Crush;
    p.drive = 0.5f;
    p.toneHz = 20000.0f;
    auto L = sine (n, 3000.0, sr, 0.5f), R = L;
    const auto dry = L;
    runUnit (dist, p, L, R);

    const double crushed = inharmonicRatio (L);
    const double clean = inharmonicRatio (dry);
    CHECK (crushed > 0.01);
    CHECK (crushed > 100.0 * clean);
}

AP_TEST (fx_modulation_effects_move_and_widen)
{
    const double sr = 44100.0;
    const size_t n = (size_t) (4.5 * sr);
    const std::pair<FxType, const char*> types[] = { { FxType::Chorus, "chorus" }, { FxType::Flanger, "flanger" }, { FxType::Phaser, "phaser" } };

    for (const auto& entry : types)
    {
        auto rack = makeRack (sr);
        const auto v = withSlots ({ entry.first });
        auto L = sine (n, 1000.0, sr, 0.25f), R = L;
        runRack (*rack, v, L, R);

        // Stereo: identical input, different outputs.
        double lr = 0.0;
        for (size_t i = n / 4; i < n; ++i) lr = std::max (lr, (double) std::fabs (L[i] - R[i]));
        CHECK (lr > 0.01);

        // The response moves over time: the level of a steady sine varies across 20 ms windows.
        const auto w = windowRms (std::vector<float> (L.begin() + (long) (sr * 0.2), L.end()), (size_t) (0.02 * sr));
        const double lo = *std::min_element (w.begin(), w.end());
        const double hi = *std::max_element (w.begin(), w.end());
        CHECK (db (hi / std::max (lo, 1.0e-9)) > 1.0);
        CHECK (allFinite (L) && allFinite (R));
    }
}

AP_TEST (fx_delay_sync_time_and_feedback)
{
    const double sr = 44100.0;
    auto impulseResponse = [&] (const DelayParams& p, const Transport& t, size_t n)
    {
        StereoDelay d;
        d.prepare (sr, kBlock);
        std::vector<float> L (n, 0.0f), R (n, 0.0f);
        L[0] = R[0] = 1.0f;
        inBlocks (L, R, kBlock, [&] (float* l, float* r, int len, size_t)
        {
            d.set (p, t);
            d.process (l, r, len);
        });
        L[0] -= 1.0f;   // remove the dry impulse: wet only
        R[0] -= 1.0f;
        return std::make_pair (L, R);
    };

    DelayParams p;
    p.sync = true;
    p.division = ap::NoteDivision::Eighth;
    p.feedback = 0.0f;
    p.pingPong = false;
    p.lowCutHz = 20.0f;
    p.highCutHz = 20000.0f;
    p.mix = 1.0f;

    Transport t;
    t.bpm = 120.0;
    t.hasTempo = true;
    for (bool tempo : { true, false })
    {
        t.hasTempo = tempo;
        t.bpm = tempo ? 120.0 : 77.0;   // without tempo the delay assumes 120 bpm
        const auto [L, R] = impulseResponse (p, t, (size_t) (0.4 * sr));
        size_t peak = 0;
        for (size_t i = 0; i < L.size(); ++i) if (std::fabs (L[i]) > std::fabs (L[peak])) peak = i;
        CHECK_NEAR ((double) peak / sr * 1000.0, 250.0, 1.0);
        CHECK_NEAR (StereoDelay::resolveTimeMs (p, t), 250.0, 1.0e-3);
    }

    // Feedback 50 %: the second echo is 6 dB below the first.
    p.sync = false;
    p.timeMs = 100.0f;
    p.feedback = 0.5f;
    t.hasTempo = true;
    t.bpm = 120.0;
    {
        const auto [L, R] = impulseResponse (p, t, (size_t) (0.35 * sr));
        const size_t e1 = (size_t) (0.1 * sr), e2 = (size_t) (0.2 * sr), half = (size_t) (0.004 * sr);
        const double first = rms (L, e1 - half, e1 + half), second = rms (L, e2 - half, e2 + half);
        CHECK_NEAR (db (second / first), -6.02, 0.5);
    }

    // Ping-pong: the first echo is on the left, the second on the right.
    p.pingPong = true;
    {
        const auto [L, R] = impulseResponse (p, t, (size_t) (0.35 * sr));
        const size_t e1 = (size_t) (0.1 * sr), e2 = (size_t) (0.2 * sr), half = (size_t) (0.004 * sr);
        CHECK (rms (L, e1 - half, e1 + half) > 10.0 * rms (R, e1 - half, e1 + half));
        CHECK (rms (R, e2 - half, e2 + half) > 10.0 * rms (L, e2 - half, e2 + half));
    }
}

AP_TEST (fx_delay_cuts_shape_the_echo)
{
    const double sr = 44100.0;
    auto echoLevel = [&] (double hz, float lowCut, float highCut)
    {
        StereoDelay d;
        d.prepare (sr, kBlock);
        DelayParams p;
        p.timeMs = 50.0f;
        p.feedback = 0.0f;
        p.pingPong = false;
        p.lowCutHz = lowCut;
        p.highCutHz = highCut;
        p.mix = 1.0f;
        const size_t n = (size_t) (0.5 * sr);
        auto L = sine (n, hz, sr, 0.5f), R = L;
        const auto dry = L;
        inBlocks (L, R, kBlock, [&] (float* l, float* r, int len, size_t)
        {
            d.set (p, Transport {});
            d.process (l, r, len);
        });
        for (size_t i = 0; i < n; ++i) L[i] -= dry[i];
        return rms (L, n / 2, n);
    };

    const double lowOpen = echoLevel (60.0, 20.0f, 20000.0f);
    const double lowCut = echoLevel (60.0, 600.0f, 20000.0f);
    const double highOpen = echoLevel (10000.0, 20.0f, 20000.0f);
    const double highCut = echoLevel (10000.0, 20.0f, 1500.0f);
    CHECK (lowOpen > 0.2);
    CHECK (db (lowCut / lowOpen) < -12.0);
    CHECK (highOpen > 0.2);
    CHECK (db (highCut / highOpen) < -12.0);
}

AP_TEST (fx_reverb_decays_within_rt60)
{
    const double sr = 44100.0;
    auto impulseResponse = [&] (const ReverbParams& p, double seconds)
    {
        Reverb rv;
        rv.prepare (sr, kBlock);
        const size_t n = (size_t) (seconds * sr);
        std::vector<float> L (n, 0.0f), R (n, 0.0f);
        L[0] = R[0] = 1.0f;
        runUnit (rv, p, L, R);
        L[0] -= 1.0f;
        R[0] -= 1.0f;
        std::vector<float> mono (n);
        for (size_t i = 0; i < n; ++i) mono[i] = 0.5f * (L[i] + R[i]);
        return mono;
    };

    for (float decay : { 0.5f, 0.0f, 1.0f })
    {
        ReverbParams p;
        p.decay = decay;
        p.mix = 1.0f;
        const double rt60 = Reverb::rt60Seconds (decay);
        const auto ir = impulseResponse (p, rt60 * 1.5 + 0.3);
        CHECK (allFinite (ir));

        const size_t win = (size_t) (0.05 * sr);
        const auto w = windowRms (ir, win);
        const size_t peakIdx = (size_t) (std::max_element (w.begin(), w.end()) - w.begin());
        const double peak = w[peakIdx];
        CHECK (peak > 1.0e-3);

        // Below -60 dB (re the loudest window) by RT60 x 1.5.
        const size_t at = (size_t) (rt60 * 1.5 / 0.05);
        CHECK (at < w.size());
        if (at < w.size())
            CHECK (db (w[at] / peak) < -60.0);

        // Never grows: after the build-up, no span of RT60 / 20 (an expected 3 dB of decay) is
        // more than 1 dB louder than the span before it.
        const auto spans = windowRms (ir, (size_t) (std::max (0.1, rt60 / 20.0) * sr));
        bool grows = false;
        for (size_t k = 3; k < spans.size(); ++k)
            if (spans[k] > spans[k - 1] * 1.12 && spans[k] > peak * 1.0e-4) grows = true;
        CHECK (! grows);
    }

    // No metallic flutter at the smallest size: the late tail has no strong periodicity
    // (normalised autocorrelation below 0.5 for every lag from 1 to 50 ms).
    {
        ReverbParams p;
        p.size = 0.0f;
        p.mix = 1.0f;
        const auto ir = impulseResponse (p, 0.8);
        const size_t from = (size_t) (0.3 * sr), len = (size_t) (0.25 * sr);
        double e0 = 0.0;
        for (size_t i = from; i < from + len; ++i) e0 += (double) ir[i] * ir[i];
        double worst = 0.0;
        for (size_t lag = (size_t) (0.001 * sr); lag <= (size_t) (0.05 * sr); ++lag)
        {
            double acc = 0.0;
            for (size_t i = from; i < from + len; ++i) acc += (double) ir[i] * ir[i + lag];
            worst = std::max (worst, std::fabs (acc) / e0);
        }
        CHECK (worst < 0.5);
    }

    // Width 0 is mono; width 1 is decorrelated stereo.
    {
        Reverb rv;
        rv.prepare (sr, kBlock);
        ReverbParams p;
        p.mix = 1.0f;
        p.width = 0.0f;
        const size_t n = (size_t) (0.5 * sr);
        std::vector<float> L (n, 0.0f), R (n, 0.0f);
        L[0] = 1.0f;
        runUnit (rv, p, L, R);
        double d = 0.0;
        for (size_t i = 1; i < n; ++i) d = std::max (d, (double) std::fabs (L[i] - R[i]));
        CHECK (d < 1.0e-5);
    }
}

AP_TEST (fx_eq_bands_hit_their_targets)
{
    const double sr = 44100.0;
    auto gainAt = [&] (const EqParams& p, double hz)
    {
        ThreeBandEq eq;
        eq.prepare (sr, kBlock);
        const size_t n = (size_t) (0.5 * sr);
        auto L = sine (n, hz, sr, 0.1f), R = L;
        const auto dry = L;
        runUnit (eq, p, L, R);
        return db (toneAmp (L, n / 2, n, hz, sr) / toneAmp (dry, n / 2, n, hz, sr));
    };

    EqParams low;
    low.lowGainDb = 12.0f;
    const double g60 = gainAt (low, 60.0);
    CHECK (g60 > 11.0 && g60 < 13.0);
    CHECK (std::fabs (gainAt (low, 5000.0)) < 0.5);

    EqParams mid;
    mid.midGainDb = 12.0f;
    CHECK_NEAR (gainAt (mid, 1000.0), 12.0, 0.5);
    CHECK (std::fabs (gainAt (mid, 60.0)) < 1.0);

    EqParams high;
    high.highGainDb = 12.0f;
    const double g10k = gainAt (high, 10000.0);
    CHECK (g10k > 10.5 && g10k < 13.0);
    CHECK (std::fabs (gainAt (high, 100.0)) < 0.5);

    EqParams cut;
    cut.lowGainDb = -12.0f;
    const double c60 = gainAt (cut, 60.0);
    CHECK (c60 < -11.0 && c60 > -13.0);
}

AP_TEST (fx_compressor_static_curve_makeup_and_mix)
{
    const double sr = 44100.0;
    const size_t n = (size_t) (0.6 * sr);
    auto outPeakDb = [&] (const CompressorParams& p)
    {
        Compressor c;
        c.prepare (sr, kBlock);
        auto L = sine (n, 1000.0, sr, 0.5f), R = L;   // -6 dBFS
        runUnit (c, p, L, R);
        return db (peakAbs (L, n - (size_t) (0.1 * sr), n));
    };

    CompressorParams p;   // threshold -18, ratio 4, attack 10, release 100
    // Standard feed-forward static curve: -18 + (-6 + 18) / 4 = -15 dBFS.
    const double out = outPeakDb (p);
    CHECK_NEAR (out, -15.0, 1.0);

    p.makeupDb = 6.0f;
    CHECK_NEAR (outPeakDb (p) - out, 6.0, 0.25);

    p.makeupDb = 0.0f;
    p.ratio = 1.0f;
    CHECK_NEAR (outPeakDb (p), -6.02, 0.25);

    // Parallel mix 0 = dry, bit for bit.
    p.ratio = 4.0f;
    p.mix = 0.0f;
    Compressor c;
    c.prepare (sr, kBlock);
    auto L = sine (n, 1000.0, sr, 0.5f), R = L;
    const auto dry = L;
    runUnit (c, p, L, R);
    CHECK (std::memcmp (L.data(), dry.data(), n * sizeof (float)) == 0);
}

AP_TEST (fx_tail_seconds_track_feedback_and_decay)
{
    auto tailFor = [] (const ParamValues& v)
    {
        auto rack = makeRack (48000.0);
        rack->setParams (v, Transport {});
        return rack->tailSeconds();
    };

    CHECK_EQ (tailFor (defaultValues()), 0.0f);

    auto v = withSlots ({ FxType::Delay });
    setP (v, P::DelayFeedback, 20.0f);
    const float lowFb = tailFor (v);
    setP (v, P::DelayFeedback, 80.0f);
    const float highFb = tailFor (v);
    CHECK (lowFb > 0.375f);
    CHECK (highFb > lowFb * 2.0f);
    setP (v, P::DelayFeedback, 100.0f);
    CHECK (tailFor (v) <= 20.0f);

    auto r = withSlots ({ FxType::Reverb });
    setP (r, P::ReverbDecay, 10.0f);
    const float shortTail = tailFor (r);
    setP (r, P::ReverbDecay, 90.0f);
    const float longTail = tailFor (r);
    CHECK (longTail > shortTail);
    CHECK_NEAR (longTail, Reverb::rt60Seconds (0.9f) + 0.010f, 0.01);

    CHECK_NEAR (tailFor (withSlots ({ FxType::Chorus })), 0.1, 1.0e-6);
}

AP_TEST (fx_parameter_and_slot_jumps_do_not_click)
{
    const double sr = 44100.0;
    const size_t n = (size_t) (0.6 * sr);
    const float amp = ap::dbToGain (-12.0f);

    const std::pair<FxType, P> mixes[] = {
        { FxType::Distortion, P::DistMix }, { FxType::Chorus, P::ChorusMix }, { FxType::Phaser, P::PhaserMix },
        { FxType::Flanger, P::FlangerMix }, { FxType::Delay, P::DelayMix },   { FxType::Reverb, P::ReverbMix },
        { FxType::Compressor, P::CompMix },  { FxType::Eq, P::EqLowGain },
    };

    for (const auto& [type, param] : mixes)
    {
        auto rack = makeRack (sr);
        auto v = withSlots ({ type });
        setP (v, P::DistDrive, 100.0f);
        setP (v, param, 0.0f);
        auto L = sine (n, 100.0, sr, amp), R = L;
        inBlocks (L, R, kBlock, [&] (float* l, float* r, int len, size_t pos)
        {
            if (pos >= (size_t) (0.2 * sr))
                setP (v, param, type == FxType::Eq ? 18.0f : 100.0f);   // one-block jump
            rack->setParams (v, Transport {});
            rack->process (l, r, len);
        });
        CHECK (maxStep (L) < 0.1 && maxStep (R) < 0.1);
    }

    // Slot changes: None -> Distortion, then a reorder, then removal.
    {
        auto rack = makeRack (sr);
        auto v = withSlots ({ FxType::None });
        setP (v, P::DistDrive, 100.0f);
        setP (v, P::DistType, (float) DistType::Hard);
        auto L = sine (2 * n, 100.0, sr, amp), R = L;
        inBlocks (L, R, kBlock, [&] (float* l, float* r, int len, size_t pos)
        {
            FxType first = FxType::None, second = FxType::None;
            if (pos >= (size_t) (0.6 * sr))      { first = FxType::None; second = FxType::None; }        // removal
            else if (pos >= (size_t) (0.4 * sr)) { first = FxType::Eq; second = FxType::Distortion; }    // reorder
            else if (pos >= (size_t) (0.2 * sr)) { first = FxType::Distortion; second = FxType::Eq; }    // insertion
            setP (v, fxSlotType (0), (float) first);
            setP (v, fxSlotType (1), (float) second);
            setP (v, P::EqHighGain, 12.0f);
            rack->setParams (v, Transport {});
            rack->process (l, r, len);
        });
        CHECK (maxStep (L) < 0.1 && maxStep (R) < 0.1);
        FxType chain[FxRack::kMaxChain];
        CHECK_EQ (rack->currentChain (chain), 0);
        // Back to an exact bypass at the end.
        const auto ref = sine (2 * n, 100.0, sr, amp);
        CHECK (std::memcmp (L.data() + (size_t) (0.8 * sr), ref.data() + (size_t) (0.8 * sr), (2 * n - (size_t) (0.8 * sr)) * sizeof (float)) == 0);
    }

    // Distortion type and phaser stage switches crossfade.
    {
        auto rack = makeRack (sr);
        auto v = withSlots ({ FxType::Distortion, FxType::Phaser });
        setP (v, P::DistDrive, 100.0f);
        setP (v, P::PhaserMix, 100.0f);
        auto L = sine (n, 100.0, sr, amp), R = L;
        // Crush is left out: its 4-bit steps at full drive are the effect itself, not a click.
        constexpr DistType kCurves[] = { DistType::Soft, DistType::Hard, DistType::Tube, DistType::Fold, DistType::Diode };
        int k = 0;
        inBlocks (L, R, kBlock, [&] (float* l, float* r, int len, size_t)
        {
            setP (v, P::DistType, (float) kCurves[k % 5]);
            setP (v, P::PhaserStages, (float) (k % 4));
            ++k;
            rack->setParams (v, Transport {});
            rack->process (l, r, len);
        });
        CHECK (maxStep (L) < 0.1 && maxStep (R) < 0.1);
    }
}
