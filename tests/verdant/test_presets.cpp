// Factory presets: bank size, unique names, categories, tags, feature
// coverage (CONTRACT / brief W-F), range safety, patch round trip, and every
// preset renders non-silent and unclipped on C3 at velocity 100.

#include "TestHarness.h"

#include "engine/Engine.h"
#include "engine/FactoryPresets.h"
#include "engine/WavetableFactory.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <string>

using namespace verdant;

namespace
{
    float val (const Patch& p, P param) { return p.values[(size_t) param]; }

    bool oscOn (const Patch& p, int o) { return val (p, oscParam (o, OscParam::On)) > 0.5f; }

    /** Matrix slots that actually do something: (source, destination param). */
    std::vector<std::pair<ModSource, P>> routes (const Patch& p)
    {
        std::vector<std::pair<ModSource, P>> r;
        for (int s = 0; s < kModSlots; ++s)
        {
            const auto src = (ModSource) (int) val (p, modSrc (s));
            const int dst = (int) val (p, modDst (s));
            if (src != ModSource::None && dst > 0 && val (p, modAmt (s)) != 0.0f)
                r.emplace_back (src, modDestination (dst));
        }
        return r;
    }

    bool usesSource (const Patch& p, ModSource lo, ModSource hi)
    {
        for (const auto& [src, dst] : routes (p))
            if ((int) src >= (int) lo && (int) src <= (int) hi) return true;
        return false;
    }

    bool sameShape (const LfoShape& a, const LfoShape& b)
    {
        return a.points.size() == b.points.size()
            && std::memcmp (a.points.data(), b.points.data(), a.points.size() * sizeof (LfoShapePoint)) == 0;
    }
}

AP_TEST (presets_bank_shape)
{
    const auto& bank = factoryPresets();
    CHECK ((int) bank.size() >= 30 && (int) bank.size() <= 50);
    CHECK (&bank == &factoryPresets());   // built once
    CHECK (bank[0].name == "Verdant Keys");

    std::set<std::string> names;
    std::set<std::string> categories;
    const auto& known = factoryCategories();
    CHECK_EQ ((int) known.size(), 7);
    for (const auto& p : bank)
    {
        CHECK (! p.name.empty());
        names.insert (p.name);
        categories.insert (p.category);
        CHECK (std::find (known.begin(), known.end(), p.category) != known.end());
        CHECK (p.tags.size() >= 2 && p.tags.size() <= 4);
        CHECK (p.author == "Curtis Tech Solutions");
        CHECK (! p.seedLocked);
    }
    CHECK (names.size() == bank.size());
    for (const auto& c : known)
        CHECK (categories.count (c) == 1);
}

AP_TEST (presets_values_in_range_and_round_trip)
{
    for (const auto& p : factoryPresets())
    {
        int bad = 0;
        for (int i = 0; i < kNumParams; ++i)
        {
            const float v = p.values[(size_t) i];
            if (! std::isfinite (v) || v != clampToRange ((P) i, v)) ++bad;
        }
        CHECK_EQ (bad, 0);

        Patch q;
        CHECK (deserializePatch (serializePatch (p), q));
        CHECK (std::memcmp (q.values.data(), p.values.data(), sizeof (float) * (size_t) kNumParams) == 0);
        CHECK (q.name == p.name && q.tags == p.tags);
        for (size_t l = 0; l < 4; ++l)
            CHECK (sameShape (q.lfoShapes[l], p.lfoShapes[l]));
    }
}

AP_TEST (presets_feature_coverage)
{
    const auto& bank = factoryPresets();
    std::set<int> tables, fxTypes;
    int lfoMatrix = 0, envToPosCut = 0, macros = 0, physics = 0, alive = 0, arp = 0, seq = 0, symp = 0,
        customShapes = 0, wideUnison = 0, bassesWithoutSub = 0, basses = 0;

    for (const auto& p : bank)
    {
        for (int o = 0; o < 2; ++o)
        {
            if (! oscOn (p, o)) continue;
            tables.insert ((int) val (p, oscParam (o, OscParam::Table)));
        }
        for (int s = 0; s < kNumFxSlots; ++s)
            fxTypes.insert ((int) val (p, fxSlotType (s)));

        if (usesSource (p, ModSource::Lfo1, ModSource::Lfo4)) ++lfoMatrix;
        if (usesSource (p, ModSource::Macro1, ModSource::Macro8)) ++macros;
        if (usesSource (p, ModSource::Ball, ModSource::Orbit)) ++physics;

        bool envRoute = false;
        for (const auto& [src, dst] : routes (p))
            if ((src == ModSource::Env2 || src == ModSource::Env3)
                && (dst == oscParam (0, OscParam::Pos) || dst == oscParam (1, OscParam::Pos)
                    || dst == filterParam (0, FilterParam::Cutoff) || dst == filterParam (1, FilterParam::Cutoff)))
                envRoute = true;
        if (envRoute) ++envToPosCut;

        if (val (p, P::Alive) > 30.0f || val (p, P::Evolve) > 30.0f) ++alive;
        if (val (p, P::ArpOn) > 0.5f) ++arp;
        if (val (p, P::SeqOn) > 0.5f)
        {
            bool stepData = false;
            for (int s = 0; s < (int) val (p, P::SeqSteps); ++s)
                if (val (p, seqStepParam (s, SeqStepParam::Pitch)) != 0.0f) stepData = true;
            if (stepData) ++seq;
        }
        if (val (p, P::SympAmount) > 0.0f) ++symp;

        bool custom = false;
        for (int l = 0; l < kNumLfos; ++l)
        {
            const auto src = (ModSource) ((int) ModSource::Lfo1 + l);
            if ((int) val (p, lfoParam (l, LfoParam::Shape)) == 6 && usesSource (p, src, src)
                && ! sameShape (p.lfoShapes[(size_t) l], LfoShape()) && p.lfoShapes[(size_t) l].points.size() >= 2)
                custom = true;
        }
        if (custom) ++customShapes;

        for (int o = 0; o < 2; ++o)
            if (oscOn (p, o) && val (p, oscParam (o, OscParam::UniVoices)) >= 6.0f) { ++wideUnison; break; }

        if (p.category == "Bass")
        {
            ++basses;
            if (! (val (p, P::SubLevel) > 0.0f)) ++bassesWithoutSub;
        }
    }

    CHECK_EQ ((int) tables.size(), kNumFactoryTables);   // every factory table, nothing else (no empty User)
    for (int t = 0; t < kNumFactoryTables; ++t)
        CHECK (tables.count (t) == 1);
    for (int f = 1; f <= 8; ++f)
        CHECK (fxTypes.count (f) == 1);
    CHECK (lfoMatrix >= 4);
    CHECK (envToPosCut >= 3);
    CHECK (macros >= 3);
    CHECK (physics >= 2);
    CHECK (alive >= 3);
    CHECK (arp >= 2);
    CHECK (seq >= 2);
    CHECK (symp >= 2);
    CHECK (customShapes >= 3);
    CHECK (wideUnison >= 2);
    CHECK (basses >= 4);
    CHECK_EQ (bassesWithoutSub, 0);
}

AP_TEST (presets_render_non_silent_and_unclipped)
{
    const auto& bank = factoryPresets();
    int silent = 0, clipped = 0, nonFinite = 0;
    double minRms = 1.0e9;
    float maxPeak = 0.0f;
    for (size_t i = 0; i < bank.size(); ++i)
    {
        const auto audio = renderPreview (bank[i], 48, 1.0f, 2.0f, 44100.0);
        double sum = 0.0;
        float peak = 0.0f;
        bool finite = true;
        for (const float x : audio)
        {
            if (! std::isfinite (x)) finite = false;
            sum += (double) x * x;
            peak = std::max (peak, std::fabs (x));
        }
        const double rms = audio.empty() ? 0.0 : std::sqrt (sum / (double) audio.size());
        minRms = std::min (minRms, rms);
        maxPeak = std::max (maxPeak, peak);
        if (! finite) ++nonFinite;
        if (! (rms > 0.005)) { ++silent; std::printf ("    silent: %s (rms %.5f)\n", bank[i].name.c_str(), rms); }
        if (! (peak < 1.0f)) { ++clipped; std::printf ("    clipped: %s (peak %.3f)\n", bank[i].name.c_str(), (double) peak); }
    }
    std::printf ("    %zu presets: quietest RMS %.4f, loudest peak %.3f\n", bank.size(), minRms, (double) maxPeak);
    CHECK_EQ (nonFinite, 0);
    CHECK_EQ (silent, 0);
    CHECK_EQ (clipped, 0);
}
