// Sound map: feature extraction orders (noisiness, centroid, attack,
// movement, loudness), a deterministic PCA layout with bright sounds to the
// right, the small-bank fallback, and blendPatches end points / midpoints.

#include "TestHarness.h"

#include "engine/Engine.h"
#include "engine/FactoryPresets.h"
#include "engine/SoundMap.h"

#include <ap/Math.h>

#include <cmath>
#include <cstring>
#include <string>

using namespace verdant;

namespace
{
    constexpr double kSr = 44100.0;
    constexpr double kTwoPi = 6.283185307179586;

    std::vector<float> sine (double hz, double seconds, float amp, double attackMs = 0.0)
    {
        std::vector<float> x ((size_t) (seconds * kSr));
        const double attack = attackMs * 0.001 * kSr;
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double env = attack > 0.0 ? std::min (1.0, (double) i / attack) : 1.0;
            x[i] = (float) (amp * env * std::sin (kTwoPi * hz * (double) i / kSr));
        }
        return x;
    }

    std::vector<float> whiteNoise (double seconds, float amp, uint32_t seed)
    {
        ap::Rng rng;
        rng.state = seed;
        std::vector<float> x ((size_t) (seconds * kSr));
        for (auto& v : x) v = amp * rng.nextBipolar();
        return x;
    }

    /** Additive tone: `harmonics` partials with 1/k^tilt amplitudes. */
    std::vector<float> tone (double hz, int harmonics, double tilt, double seconds, float amp, double attackMs)
    {
        std::vector<float> x ((size_t) (seconds * kSr), 0.0f);
        const double attack = attackMs * 0.001 * kSr;
        for (int k = 1; k <= harmonics; ++k)
        {
            if (hz * k >= kSr * 0.45) break;
            const double a = amp / std::pow ((double) k, tilt);
            for (size_t i = 0; i < x.size(); ++i)
                x[i] += (float) (a * std::sin (kTwoPi * hz * k * (double) i / kSr));
        }
        for (size_t i = 0; i < x.size() && attack > 0.0; ++i)
            x[i] *= (float) std::min (1.0, (double) i / attack);
        return x;
    }

    SoundFeatures features (const std::vector<float>& x) { return extractFeatures (x.data(), (int) x.size(), kSr); }
}

AP_TEST (soundmap_features_tone_vs_noise)
{
    const auto s = features (sine (440.0, 1.0, 0.5f));
    const auto n = features (whiteNoise (1.0, 0.5f, 99u));

    CHECK_NEAR (s.centroidHz, 440.0, 440.0 * 0.03);
    CHECK (n.centroidHz > 8000.0f);              // white noise: about half of Nyquist
    CHECK (s.noisiness < 0.05f);
    CHECK (n.noisiness > 0.4f && n.noisiness <= 1.0f);
    CHECK_NEAR (s.loudness, 20.0 * std::log10 (0.5 / std::sqrt (2.0)), 0.1);
    CHECK (s.movement < 0.02f);                  // a steady tone does not move
    CHECK (s.attackMs < 10.0f);

    const auto silent = features (std::vector<float> (44100, 0.0f));
    CHECK_EQ (silent.loudness, -120.0f);
    CHECK_EQ (silent.centroidHz, 0.0f);
    CHECK (std::isfinite (silent.noisiness) && std::isfinite (silent.movement));
    const auto none = extractFeatures (nullptr, 0, kSr);
    CHECK_EQ (none.loudness, -120.0f);
}

AP_TEST (soundmap_features_attack_and_movement)
{
    const auto fast = features (sine (330.0, 1.5, 0.5f, 1.0));
    const auto slow = features (sine (330.0, 1.5, 0.5f, 400.0));
    CHECK (fast.attackMs < 10.0f);
    CHECK (slow.attackMs > fast.attackMs + 200.0f);
    CHECK_NEAR (slow.attackMs, 0.9 * 400.0, 30.0);   // linear ramp: 90 % of the peak RMS near 90 % of the ramp

    // A pitch glide moves the centroid a lot.
    std::vector<float> sweep ((size_t) (1.5 * kSr));
    double phase = 0.0;
    for (size_t i = 0; i < sweep.size(); ++i)
    {
        const double hz = 200.0 * std::pow (20.0, (double) i / (double) sweep.size());
        phase += kTwoPi * hz / kSr;
        sweep[i] = (float) (0.4 * std::sin (phase));
    }
    CHECK (features (sweep).movement > 0.3f);
}

AP_TEST (soundmap_build_is_deterministic_and_bright_is_right)
{
    // Six presets: three bright (many partials) and three dull (near sines), varied in attack and level.
    std::vector<Patch> patches (6);
    const char* names[] = { "bright fast", "dull fast", "bright slow", "dull slow", "bright mid", "dull mid" };
    for (size_t i = 0; i < 6; ++i) patches[i].name = names[i];

    int calls = 0;
    auto render = [&calls] (const Patch& p) -> std::vector<float>
    {
        ++calls;
        const bool bright = p.name.rfind ("bright", 0) == 0;
        const double attack = p.name.find ("fast") != std::string::npos ? 2.0 : (p.name.find ("slow") != std::string::npos ? 300.0 : 60.0);
        return bright ? tone (220.0, 60, 1.0, 1.0, 0.25f, attack) : tone (220.0, 3, 3.0, 1.0, 0.4f, attack);
    };

    const auto m1 = buildSoundMap (patches, render);
    const auto m2 = buildSoundMap (patches, render);
    CHECK_EQ (calls, 12);
    CHECK_EQ ((int) m1.size(), 6);
    bool identical = m1.size() == m2.size();
    for (size_t i = 0; i < m1.size() && identical; ++i)
        identical = m1[i].presetIndex == m2[i].presetIndex
                 && std::memcmp (&m1[i].x, &m2[i].x, sizeof (float)) == 0
                 && std::memcmp (&m1[i].y, &m2[i].y, sizeof (float)) == 0;
    CHECK (identical);

    float minBright = 2.0f, maxDull = -1.0f;
    for (const auto& pt : m1)
    {
        CHECK (pt.x >= 0.05f - 1.0e-6f && pt.x <= 0.95f + 1.0e-6f);
        CHECK (pt.y >= 0.05f - 1.0e-6f && pt.y <= 0.95f + 1.0e-6f);
        const bool bright = patches[(size_t) pt.presetIndex].name.rfind ("bright", 0) == 0;
        if (bright) minBright = std::min (minBright, pt.x);
        else        maxDull = std::max (maxDull, pt.x);
    }
    CHECK (minBright > maxDull);
    for (int i = 0; i < 6; ++i) CHECK_EQ (m1[(size_t) i].presetIndex, i);

    // The map from cached features is the same map.
    std::vector<SoundFeatures> f;
    for (const auto& pt : m1) f.push_back (pt.features);
    const auto m3 = buildSoundMapFromFeatures (f);
    for (size_t i = 0; i < m1.size(); ++i)
    {
        CHECK_EQ (m3[i].x, m1[i].x);
        CHECK_EQ (m3[i].y, m1[i].y);
    }
}

AP_TEST (soundmap_factory_bank)
{
    // The real use: the factory bank rendered through the engine (what BackgroundTasks does).
    const auto& bank = factoryPresets();
    auto render = [] (const Patch& p) { return renderPreview (p, 48, 1.0f, 1.5f, 44100.0); };
    const auto map = buildSoundMap (bank, render);
    CHECK (map.size() == bank.size());
    float loX = 1.0f, hiX = 0.0f, loY = 1.0f, hiY = 0.0f;
    for (const auto& pt : map)
    {
        CHECK (std::isfinite (pt.x) && std::isfinite (pt.y));
        CHECK (std::isfinite (pt.features.centroidHz) && pt.features.centroidHz > 0.0f);
        CHECK (pt.features.loudness > -60.0f);
        loX = std::min (loX, pt.x); hiX = std::max (hiX, pt.x);
        loY = std::min (loY, pt.y); hiY = std::max (hiY, pt.y);
    }
    CHECK_NEAR (loX, 0.05, 1.0e-5);   // spread over the whole map
    CHECK_NEAR (hiX, 0.95, 1.0e-5);
    CHECK_NEAR (loY, 0.05, 1.0e-5);
    CHECK_NEAR (hiY, 0.95, 1.0e-5);
}

AP_TEST (soundmap_small_banks)
{
    CHECK (buildSoundMap ({}, nullptr).empty());

    std::vector<SoundFeatures> one { { 500.0f, 5.0f, 0.1f, 0.0f, -12.0f } };
    const auto m1 = buildSoundMapFromFeatures (one);
    CHECK_EQ ((int) m1.size(), 1);
    CHECK_EQ (m1[0].x, 0.5f);
    CHECK_EQ (m1[0].y, 0.5f);

    std::vector<SoundFeatures> two { { 3000.0f, 5.0f, 0.1f, 0.0f, -12.0f }, { 300.0f, 5.0f, 0.1f, 0.0f, -12.0f } };
    const auto m2 = buildSoundMapFromFeatures (two);
    CHECK_EQ (m2[0].x, 0.95f);   // the brighter one to the right
    CHECK_EQ (m2[1].x, 0.05f);

    // Identical sounds: no variance anywhere, everything sits in the middle (and stays finite).
    std::vector<SoundFeatures> same (4, SoundFeatures { 800.0f, 20.0f, 0.2f, 0.1f, -10.0f });
    for (const auto& pt : buildSoundMapFromFeatures (same))
    {
        CHECK_EQ (pt.x, 0.5f);
        CHECK_EQ (pt.y, 0.5f);
    }
}

AP_TEST (soundmap_blend_patches)
{
    Patch a = initPatch(), b = initPatch();
    a.name = "Left";
    b.name = "Right";
    a.userTables[0].name = "A table";
    b.userTables[0].name = "B table";
    a.macroNames[0] = "A macro";
    b.macroNames[0] = "B macro";
    for (int i = 0; i < kNumParams; ++i)
    {
        const auto& in = info ((P) i);
        a.values[(size_t) i] = clampToRange ((P) i, in.min + 0.1f * (in.max - in.min));
        b.values[(size_t) i] = clampToRange ((P) i, in.min + 0.9f * (in.max - in.min));
    }

    auto same = [] (const Patch& x, const Patch& y)
    {
        return std::memcmp (x.values.data(), y.values.data(), sizeof (float) * (size_t) kNumParams) == 0
            && x.name == y.name && x.userTables[0].name == y.userTables[0].name;
    };
    CHECK (same (blendPatches (a, b, 0.0f), a));
    CHECK (same (blendPatches (a, b, 1.0f), b));
    CHECK (same (blendPatches (a, b, -3.0f), a));
    CHECK (same (blendPatches (a, b, 7.0f), b));
    CHECK (same (blendPatches (a, b, std::nanf ("")), a));

    // Choice / Bool and extras: A below 0.5, B from 0.5 (CONTRACT §5, PRD R34).
    const Patch below = blendPatches (a, b, 0.49f);
    const Patch half = blendPatches (a, b, 0.5f);
    CHECK (below.userTables[0].name == "A table" && below.macroNames[0] == "A macro");
    CHECK (half.userTables[0].name == "B table" && half.macroNames[0] == "B macro");
    int choiceErrors = 0, floatErrors = 0, intErrors = 0;
    for (int i = 0; i < kNumParams; ++i)
    {
        const P p = (P) i;
        const auto kind = info (p).kind;
        const float va = a.values[(size_t) i], vb = b.values[(size_t) i];
        if (kind == ParamKind::Choice || kind == ParamKind::Bool)
        {
            if (below.values[(size_t) i] != va || half.values[(size_t) i] != vb) ++choiceErrors;
        }
        else if (kind == ParamKind::Float)
        {
            const double mid = 0.5 * ((double) toNormalised (p, va) + toNormalised (p, vb));
            if (std::fabs (toNormalised (p, half.values[(size_t) i]) - mid) > 1.0e-5) ++floatErrors;
        }
        else
        {
            const float na = toNormalised (p, va), nb = toNormalised (p, vb);
            const float expected = fromNormalised (p, na + (nb - na) * 0.5f);   // the lerp, rounded to the step
            const float got = half.values[(size_t) i];
            if (got != expected || got != std::round (got)) ++intErrors;
        }
    }
    CHECK_EQ (choiceErrors, 0);
    CHECK_EQ (floatErrors, 0);
    CHECK_EQ (intErrors, 0);

    CHECK (blendPatches (a, b, 0.42f).name == "Left \xE2\x86\x94 Right 42%");
}
