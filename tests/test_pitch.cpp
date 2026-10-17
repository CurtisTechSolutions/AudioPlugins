#include "TestHarness.h"

#include <ap/Math.h>
#include <ap/PitchDetector.h>
#include <ap/PitchTracker.h>

#include <cmath>
#include <vector>

using namespace ap;

namespace
{
    std::vector<float> makeTone (double sr, double hz, int n, int harmonics = 1, float noise = 0.0f)
    {
        std::vector<float> v ((size_t) n);
        Rng rng;
        for (int i = 0; i < n; ++i)
        {
            double s = 0.0;
            for (int h = 1; h <= harmonics; ++h)
                s += std::sin (kTwoPiD * hz * h * i / sr) / h;
            v[(size_t) i] = (float) (s / (harmonics > 1 ? 1.5 : 1.0)) + noise * rng.nextBipolar();
        }
        return v;
    }
}

AP_TEST (yin_detects_pure_tones)
{
    const double sr = 48000.0;
    YinPitchDetector yin;
    yin.prepare (sr, 1024, 50.0f, 1500.0f);

    for (double hz : { 82.4, 110.0, 220.0, 261.6, 440.0, 659.3, 1046.5 })
    {
        const auto tone = makeTone (sr, hz, yin.requiredSamples());
        const auto r = yin.analyze (tone.data());
        CHECK (r.voiced);
        CHECK (r.confidence > 0.9f);
        CHECK_NEAR (r.frequency, hz, hz * 0.005);   // within half a percent (~9 cents)
    }
}

AP_TEST (yin_detects_harmonic_tones_with_noise)
{
    const double sr = 44100.0;
    YinPitchDetector yin;
    yin.prepare (sr, 1024, 50.0f, 1500.0f);

    for (double hz : { 98.0, 196.0, 329.6, 523.3 })
    {
        const auto tone = makeTone (sr, hz, yin.requiredSamples(), 6, 0.02f);
        const auto r = yin.analyze (tone.data());
        CHECK (r.voiced);
        CHECK_NEAR (r.frequency, hz, hz * 0.01);
    }
}

AP_TEST (yin_rejects_noise)
{
    const double sr = 44100.0;
    YinPitchDetector yin;
    yin.prepare (sr);
    std::vector<float> noise ((size_t) yin.requiredSamples());
    Rng rng;
    for (auto& s : noise) s = rng.nextBipolar();
    const auto r = yin.analyze (noise.data());
    CHECK (! r.voiced);
    CHECK (r.confidence < 0.6f);
}

AP_TEST (pitch_tracker_streams_frames)
{
    const double sr = 48000.0;
    PitchTracker tracker;
    tracker.prepare (sr, 256, 1024, 50.0f, 1500.0f);

    const auto tone = makeTone (sr, 330.0, 48000, 3);
    int frames = 0;
    float lastHz = 0.0f;
    float lastRms = -200.0f;
    int64_t lastTime = -1;

    // Feed in odd block sizes to exercise the ring buffer.
    int pos = 0;
    const int blocks[] = { 37, 512, 100, 1000, 3 };
    int bi = 0;
    while (pos < (int) tone.size())
    {
        const int n = std::min (blocks[bi++ % 5], (int) tone.size() - pos);
        tracker.process (tone.data() + pos, n, [&] (const PitchTracker::Frame& f)
        {
            ++frames;
            CHECK (f.time > lastTime);
            lastTime = f.time;
            if (f.voiced) { lastHz = f.frequency; lastRms = f.rmsDb; }
        });
        pos += n;
    }

    CHECK (frames > 150);
    CHECK_NEAR (lastHz, 330.0f, 2.0f);
    CHECK (lastRms > -12.0f && lastRms < 0.0f);
    CHECK (tracker.latencySamples() > 0);
    CHECK (tracker.latencySamples() < 4096);
}

AP_TEST (pitch_timeline_lookup)
{
    PitchTimeline tl;
    CHECK (tl.at (0) == nullptr);
    for (int i = 0; i < 10; ++i)
    {
        PitchTracker::Frame f;
        f.time = i * 256;
        f.frequency = 100.0f + (float) i;
        tl.push (f);
    }
    CHECK_NEAR (tl.at (0)->frequency, 100.0f, 1e-6);
    CHECK_NEAR (tl.at (256 * 4 + 10)->frequency, 104.0f, 1e-6);
    CHECK_NEAR (tl.at (256 * 4 + 200)->frequency, 105.0f, 1e-6);
    CHECK_NEAR (tl.at (100000)->frequency, 109.0f, 1e-6);
    CHECK_NEAR (tl.latest()->frequency, 109.0f, 1e-6);
}
