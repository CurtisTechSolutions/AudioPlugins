// SampleMatch: pitch, brightness, envelope and table extraction from
// synthetic samples, parameter-range safety, progress and cancellation,
// unpitched input and determinism.

#include "TestHarness.h"

#include "engine/FFT.h"
#include "engine/SampleMatch.h"

#include <ap/Math.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace verdant;

namespace
{
    constexpr double kSr = 48000.0;
    constexpr double kTwoPi = 6.283185307179586;

    /** 2 s, 220 Hz band-limited sawtooth, exponential decay with tau = 0.5 s on the fundamental. Upper harmonics
        decay faster (tau_k = 0.5 / (1 + 0.05 (k - 1)) s, like a plucked string), so the sound also darkens:
        that is what makes "frame 0 is brighter than the last frame" and the filter envelope meaningful. */
    std::vector<float> decayingSaw (double hz = 220.0, double seconds = 2.0)
    {
        const int n = (int) (seconds * kSr);
        std::vector<float> x ((size_t) n, 0.0f);
        const int harmonics = (int) (0.5 * kSr / hz - 1.0);
        for (int k = 1; k <= harmonics; ++k)
        {
            const double tau = 0.5 / (1.0 + 0.05 * (k - 1));
            const double a = 0.5 / k;
            const double w = kTwoPi * hz * k / kSr;
            const double d = std::exp (-1.0 / (tau * kSr));
            double env = a;
            for (int i = 0; i < n; ++i)
            {
                x[(size_t) i] += (float) (env * std::sin (w * i));
                env *= d;
            }
        }
        return x;
    }

    /** 2 s, 220 Hz sine with a 200 ms linear attack, then steady. */
    std::vector<float> slowSine (double hz = 220.0, double seconds = 2.0)
    {
        const int n = (int) (seconds * kSr);
        std::vector<float> x ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const double t = i / kSr;
            x[(size_t) i] = (float) (0.6 * std::min (1.0, t / 0.2) * std::sin (kTwoPi * hz * t));
        }
        return x;
    }

    double frameCentroid (const float* frame)
    {
        const FFT fft (kFrameSize);
        std::vector<float> re ((size_t) kFrameSize / 2 + 1), im ((size_t) kFrameSize / 2 + 1);
        fft.realForward (frame, re.data(), im.data());
        double num = 0.0, den = 0.0;
        for (int k = 1; k < kFrameSize / 2; ++k)
        {
            const double m = std::hypot (re[(size_t) k], im[(size_t) k]);
            num += m * k;
            den += m;
        }
        return den > 0.0 ? num / den : 0.0;
    }

    float value (const SampleMatchResult& r, P p) { return r.values[(size_t) p]; }

    bool allInRange (const SampleMatchResult& r)
    {
        for (int i = 0; i < kNumParams; ++i)
            if (r.values[(size_t) i] != clampToRange ((P) i, r.values[(size_t) i]))
                return false;
        return true;
    }

    struct ProgressLog
    {
        std::vector<float> values;
        ProgressFn fn() { return [this] (float p) { values.push_back (p); return true; }; }
        bool monotone() const
        {
            for (size_t i = 1; i < values.size(); ++i) if (values[i] < values[i - 1]) return false;
            return true;
        }
    };
}

AP_TEST (match_saw_and_sine)
{
    ProgressLog sawLog, sineLog;
    const auto saw = decayingSaw();
    const auto sine = slowSine();
    const auto rs = matchSample (saw.data(), (int) saw.size(), kSr, sawLog.fn());
    const auto rn = matchSample (sine.data(), (int) sine.size(), kSr, sineLog.fn());
    std::printf ("  saw:  %s\n  sine: %s\n", rs.notes.c_str(), rn.notes.c_str());

    // Pitch within 1 %.
    CHECK_NEAR (rs.detectedHz, 220.0, 2.2);
    CHECK_NEAR (rn.detectedHz, 220.0, 2.2);
    CHECK (rs.confidence > 0.8f);
    CHECK (rn.confidence > 0.8f);

    // Brightness: the saw gets the higher cutoff.
    const float sawCut = value (rs, filterParam (0, FilterParam::Cutoff));
    const float sineCut = value (rn, filterParam (0, FilterParam::Cutoff));
    CHECK (sawCut > sineCut);
    CHECK_NEAR (sineCut, 440.0, 60.0);   // 2 x the centroid of a 220 Hz sine

    // Envelope: the sine's slow attack is found (~180 ms to 90 %), the saw's is short; the saw decays, the sine sustains.
    const float sawA = value (rs, envParam (0, EnvParam::A));
    const float sineA = value (rn, envParam (0, EnvParam::A));
    CHECK (sineA > sawA);
    CHECK_NEAR (sineA, 180.0, 25.0);
    CHECK (sawA < 20.0f);
    CHECK (value (rn, envParam (0, EnvParam::S)) > 90.0f);
    CHECK (value (rs, envParam (0, EnvParam::S)) < 15.0f);
    CHECK (value (rs, envParam (0, EnvParam::D)) > 200.0f && value (rs, envParam (0, EnvParam::D)) < 800.0f);
    CHECK_NEAR (value (rs, envParam (0, EnvParam::R)), 300.0, 1.0e-3);

    // Table: >= 8 frames, frame 0 brighter than the last.
    CHECK (rs.table != nullptr && rn.table != nullptr);
    if (rs.table != nullptr)
    {
        CHECK (rs.table->numFrames() >= 8);
        CHECK (rs.table->numFrames() <= 64);
        CHECK (frameCentroid (rs.table->frame (0)) > 1.5 * frameCentroid (rs.table->frame (rs.table->numFrames() - 1)));
        float peak = 0.0f;
        for (int f = 0; f < rs.table->numFrames(); ++f)
            for (int i = 0; i < kFrameSize; ++i) peak = std::max (peak, std::fabs (rs.table->frame (f)[i]));
        CHECK_NEAR (peak, 1.0, 1.0e-4);
        CHECK (std::fabs (rs.table->frame (0)[0]) < 0.2f);   // phases rotated: frame starts near the fundamental's zero crossing
    }
    if (rn.table != nullptr)
    {
        // A sine's frames are sines.
        const float* f0 = rn.table->frame (rn.table->numFrames() / 2);
        double ab = 0, aa = 0, bb = 0;
        for (int i = 0; i < kFrameSize; ++i)
        {
            const double s = std::sin (kTwoPi * i / kFrameSize);
            ab += f0[i] * s; aa += (double) f0[i] * f0[i]; bb += s * s;
        }
        CHECK (ab / std::sqrt (aa * bb) > 0.999);
    }

    // Patch layout: Osc A User table, Osc B off, Filter 1 LP 24, Env 2 scanning the table via slot 1.
    for (const auto* r : { &rs, &rn })
    {
        CHECK_EQ (value (*r, oscParam (0, OscParam::On)), 1.0f);
        CHECK_EQ (value (*r, oscParam (0, OscParam::Table)), 16.0f);
        CHECK_EQ (value (*r, oscParam (1, OscParam::On)), 0.0f);
        CHECK_EQ (value (*r, filterParam (0, FilterParam::On)), 1.0f);
        CHECK_EQ (value (*r, filterParam (0, FilterParam::Type)), 1.0f);
        CHECK_EQ (value (*r, modSrc (0)), (float) ModSource::Env2);
        CHECK_EQ (value (*r, modDst (0)), (float) modDestinationIndexOf (oscParam (0, OscParam::Pos)));
        // Start of the note = frame 0: position base + amount x Env 2 (= 1 at note-on) = 0.
        const float startPos = value (*r, oscParam (0, OscParam::Pos)) / 100.0f + value (*r, modAmt (0)) / 100.0f;
        CHECK_NEAR (startPos, 0.0, 1.0e-6);
        CHECK_EQ (value (*r, envParam (1, EnvParam::A)), 0.0f);
        CHECK_EQ (value (*r, envParam (1, EnvParam::S)), 0.0f);
        CHECK_NEAR (value (*r, envParam (1, EnvParam::D)), 2000.0, 50.0);   // the analysed region = the whole 2 s
        CHECK (allInRange (*r));
        CHECK (! r->notes.empty());
        CHECK (value (*r, P::NoiseLevel) < 10.0f);   // clean tones are not noisy
    }

    // The saw darkens: filter envelope on Env 3 via slot 2, cutoff from the late centroid.
    CHECK_EQ (value (rs, modSrc (1)), (float) ModSource::Env3);
    CHECK_EQ (value (rs, modDst (1)), (float) modDestinationIndexOf (filterParam (0, FilterParam::Cutoff)));
    CHECK (value (rs, modAmt (1)) > 10.0f);
    CHECK (value (rs, envParam (2, EnvParam::D)) > 50.0f);
    // The steady sine does not.
    CHECK_EQ (value (rn, modSrc (1)), (float) ModSource::None);
    CHECK_EQ (value (rn, modAmt (1)), 0.0f);

    // Progress: monotone, ends at exactly 1.
    for (const auto* log : { &sawLog, &sineLog })
    {
        CHECK (log->values.size() >= 3);
        CHECK (log->monotone());
        CHECK (! log->values.empty() && log->values.back() == 1.0f);
    }
}

AP_TEST (match_cancel_returns_early)
{
    const auto saw = decayingSaw();
    int calls = 0;
    const auto r = matchSample (saw.data(), (int) saw.size(), kSr, [&calls] (float) { ++calls; return false; });
    CHECK_EQ (calls, 1);
    CHECK_EQ (r.confidence, 0.0f);
    CHECK (allInRange (r));

    // Cancelling midway (after the pitch stage) also stops.
    std::vector<float> seen;
    const auto r2 = matchSample (saw.data(), (int) saw.size(), kSr, [&seen] (float p) { seen.push_back (p); return p < 0.5f; });
    CHECK_EQ (r2.confidence, 0.0f);
    CHECK (! seen.empty() && seen.back() < 1.0f);
}

AP_TEST (match_noise_is_unpitched)
{
    const int n = (int) kSr;
    std::vector<float> noise ((size_t) n);
    ap::Rng rng; rng.state = 4242u;
    for (int i = 0; i < n; ++i) noise[(size_t) i] = 0.5f * rng.nextBipolar() * (float) std::exp (-3.0 * i / n);
    const auto r = matchSample (noise.data(), n, kSr, nullptr);
    CHECK_EQ (r.detectedHz, 0.0f);
    CHECK (r.confidence < 0.1f);
    CHECK (r.table != nullptr);
    if (r.table != nullptr) CHECK (r.table->numFrames() >= 1);
    CHECK_NEAR (value (r, P::NoiseLevel), 40.0, 1.0e-4);
    CHECK (value (r, filterParam (0, FilterParam::Cutoff)) > 2000.0f);   // white noise is bright
    CHECK (allInRange (r));
}

AP_TEST (match_deterministic_and_robust)
{
    const auto saw = decayingSaw (330.0, 1.0);
    const auto a = matchSample (saw.data(), (int) saw.size(), kSr, nullptr);
    const auto b = matchSample (saw.data(), (int) saw.size(), kSr, nullptr);
    CHECK (a.values == b.values);
    CHECK (a.detectedHz == b.detectedHz);
    CHECK (a.table != nullptr && b.table != nullptr);
    if (a.table != nullptr && b.table != nullptr)
    {
        CHECK_EQ (a.table->numFrames(), b.table->numFrames());
        bool same = true;
        for (int f = 0; f < a.table->numFrames(); ++f)
            for (int i = 0; i < kFrameSize; ++i) same = same && a.table->frame (f)[i] == b.table->frame (f)[i];
        CHECK (same);
    }
    CHECK_NEAR (a.detectedHz, 330.0, 3.3);

    // A sound that decays to silence inside the file: sustain 0, decay = time to -40 dB (~ 0.5 s x ln 100 = 2.3 s).
    std::vector<float> pluck ((size_t) (4.0 * kSr));
    for (size_t i = 0; i < pluck.size(); ++i)
        pluck[i] = (float) (0.8 * std::exp (-(double) i / (0.5 * kSr)) * std::sin (kTwoPi * 440.0 * (double) i / kSr));
    const auto p = matchSample (pluck.data(), (int) pluck.size(), kSr, nullptr);
    CHECK_NEAR (p.detectedHz, 440.0, 4.4);
    CHECK_EQ (value (p, envParam (0, EnvParam::S)), 0.0f);
    CHECK_NEAR (value (p, envParam (0, EnvParam::D)), 2300.0, 150.0);
    CHECK (allInRange (p));

    // Degenerate input: silence, tiny, NaN, null.
    std::vector<float> silence (4800, 0.0f);
    const auto s = matchSample (silence.data(), (int) silence.size(), kSr, nullptr);
    CHECK (allInRange (s));
    CHECK (s.table != nullptr);

    std::vector<float> tiny = { 0.5f, -0.5f, std::nanf (""), 0.25f };
    const auto t = matchSample (tiny.data(), (int) tiny.size(), kSr, nullptr);
    CHECK (allInRange (t));
    CHECK (t.table != nullptr);
    if (t.table != nullptr)
        for (int i = 0; i < kFrameSize; ++i) CHECK (std::isfinite (t.table->frame (0)[i]));

    int calls = 0;
    const auto z = matchSample (nullptr, 0, kSr, [&calls] (float p01) { ++calls; return p01 >= 0.0f; });
    CHECK (allInRange (z));
    CHECK (z.table != nullptr);
    CHECK (calls >= 1);
}

AP_TEST (match_ten_seconds_is_fast_enough)
{
    // CONTRACT: a 10 s sample is analysed in < 2 s. Only asserted in Release (Debug is many times slower).
    // Worst case: sustained for the whole 10 s, so every stage works over the full length.
    std::vector<float> saw ((size_t) (10.0 * kSr));
    for (size_t i = 0; i < saw.size(); ++i)
    {
        const double ph = 110.0 * (double) i / kSr;
        saw[i] = (float) (0.5 * (2.0 * (ph - std::floor (ph)) - 1.0));
    }
    const auto start = std::chrono::steady_clock::now();
    const auto r = matchSample (saw.data(), (int) saw.size(), kSr, nullptr);
    const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
    std::printf ("  matchSample 10 s @ 48 kHz: %.3f s, %s\n", seconds, r.notes.c_str());
    CHECK_NEAR (r.detectedHz, 110.0, 1.1);
#ifdef NDEBUG
    CHECK (seconds < 2.0);
#endif
}
