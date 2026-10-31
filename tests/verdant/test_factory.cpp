// WavetableFactory: names, frame counts, normalisation, non-silence, Basic Shapes keyframes, caching.

#include "TestHarness.h"

#include "engine/WavetableFactory.h"

#include <ap/Math.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

using namespace verdant;

namespace
{
    double correlation (const float* x, const float* y, int n)
    {
        double xy = 0.0, xx = 0.0, yy = 0.0;
        for (int i = 0; i < n; ++i)
        {
            xy += (double) x[i] * y[i];
            xx += (double) x[i] * x[i];
            yy += (double) y[i] * y[i];
        }
        return xy / std::sqrt (xx * yy + 1.0e-30);
    }

    double correlationWith (const float* x, double (*fn) (double))
    {
        double xy = 0.0, xx = 0.0, yy = 0.0;
        for (int i = 0; i < kFrameSize; ++i)
        {
            const double y = fn ((double) i / kFrameSize);
            xy += (double) x[i] * y;
            xx += (double) x[i] * x[i];
            yy += y * y;
        }
        return xy / std::sqrt (xx * yy + 1.0e-30);
    }
}

AP_TEST (factory_names_and_shapes)
{
    const char* const expected[kNumFactoryTables] = {
        "Basic Shapes", "Analog Saws", "Pulse Width", "Harmonic Stack", "Odd Harmonics", "Formant Vowels", "Bell", "Organ",
        "Digital Glass", "FM Sweep", "Sync Sweep", "Noise Bloom", "Growl", "Pluck Decay", "Choir", "Sub Bass"
    };
    const auto names = factoryTableNames();
    for (int i = 0; i < kNumFactoryTables; ++i)
        CHECK (std::string (names[i]) == expected[i]);

    for (int i = 0; i < kNumFactoryTables; ++i)
    {
        const auto t = factoryTable (i);
        CHECK (t != nullptr);
        CHECK (t->name() == expected[i]);
        CHECK (t->numFrames() >= 16);
        CHECK_EQ (t->numFrames(), kFactoryTableFrames);

        float maxPeak = 0.0f;
        double minFrame0Rms = 0.0;
        bool finite = true;
        for (int f = 0; f < t->numFrames(); ++f)
        {
            double acc = 0.0;
            for (int s = 0; s < kFrameSize; ++s)
            {
                const float v = t->frame (f)[s];
                finite = finite && std::isfinite (v);
                maxPeak = std::max (maxPeak, std::fabs (v));
                acc += (double) v * v;
            }
            if (f == 0) minFrame0Rms = std::sqrt (acc / kFrameSize);
            CHECK (std::sqrt (acc / kFrameSize) > 0.05);   // every frame sounds
        }
        CHECK (finite);
        CHECK (maxPeak <= 1.0f + 1.0e-4f);
        CHECK (minFrame0Rms > 0.05);

        // Neighbouring frames are related (the tables morph rather than jump), except deliberately sparse
        // ones; at least the mean neighbour correlation stays high.
        double meanCorr = 0.0;
        for (int f = 0; f + 1 < t->numFrames(); ++f)
            meanCorr += correlation (t->frame (f), t->frame (f + 1), kFrameSize);
        meanCorr /= (double) (t->numFrames() - 1);
        std::printf ("    %-15s mean neighbour correlation %.3f\n", expected[i], meanCorr);
        CHECK (meanCorr > 0.5);
    }
}

AP_TEST (factory_basic_shapes_keyframes)
{
    const auto t = factoryTable (0);
    CHECK (correlationWith (t->frame (0), [] (double p) { return std::sin (ap::kTwoPiD * p); }) > 0.999);
    CHECK (correlationWith (t->frame (6), [] (double p) { return p < 0.25 ? 4.0 * p : (p < 0.75 ? 2.0 - 4.0 * p : 4.0 * p - 4.0); }) > 0.999);
    CHECK (correlationWith (t->frame (kBasicShapesSawFrame), [] (double p) { return 1.0 - 2.0 * p; }) > 0.99);
    CHECK (correlationWith (t->frame (19), [] (double p) { return p < 0.5 ? 1.0 : -1.0; }) > 0.99);
    CHECK (correlationWith (t->frame (25), [] (double p) { return p < 0.25 ? 0.75 : -0.25; }) > 0.99);
    CHECK (correlationWith (t->frame (31), [] (double p) { return p < 0.125 ? 0.875 : -0.125; }) > 0.98);

    // Frame 0 is a pure sine with phase 0: starts at 0 rising, peak at a quarter cycle.
    CHECK (std::fabs (t->frame (0)[0]) < 1.0e-3f);
    CHECK_NEAR (t->frame (0)[kFrameSize / 4], 1.0, 1.0e-3);
}

AP_TEST (factory_cache_and_fresh_builds)
{
    const auto a = factoryTable (5);
    const auto b = factoryTable (5);
    CHECK (a.get() == b.get());                    // cached: the same pointer twice
    CHECK (factoryTable (-1).get() == factoryTable (0).get());
    CHECK (factoryTable (16).get() == factoryTable (0).get());

    const auto fresh = makeFactoryTable (5);
    CHECK (fresh.get() != a.get());
    bool same = fresh->numFrames() == a->numFrames();
    for (int f = 0; same && f < a->numFrames(); ++f)
        same = std::equal (a->frame (f), a->frame (f) + kFrameSize, fresh->frame (f));
    CHECK (same);                                  // deterministic
    CHECK (makeFactoryTable (99)->name() == "Basic Shapes");
}
