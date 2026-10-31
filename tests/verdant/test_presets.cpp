// Factory library: bank shape (counts per category, order, names, tags from the
// controlled vocabulary, macros), range safety and patch round trip, feature
// coverage (tables, warps, filters, FX, sympathetic, arp / seq, physics, custom
// LFOs, unison, voice modes, key snapping), and the sound itself: every preset
// is rendered on its category's preview note and checked for level
// (loudness-matched about -26 dBFS), peak headroom, category sanity (pads swell,
// plucks and drums snap and decay, basses stay low, leads sustain, sequences
// pulse) and diversity (no two presets of a category sound alike by the sound-map
// features).

#include "TestHarness.h"

#include "engine/Engine.h"
#include "engine/FactoryPresets.h"
#include "engine/SoundMap.h"
#include "engine/WavetableFactory.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace verdant;

namespace
{
    float val (const Patch& p, P param) { return p.values[(size_t) param]; }

    bool oscOn (const Patch& p, int o) { return val (p, oscParam (o, OscParam::On)) > 0.5f; }
    bool filterOn (const Patch& p, int f) { return val (p, filterParam (f, FilterParam::On)) > 0.5f; }

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

    bool isMono (const Patch& p) { return val (p, P::VoiceMode) > 0.5f; }
    bool isPercussive (const Patch& p) { return p.category == "Pluck" || p.category == "Drums/Perc"; }

    // Expected size of each category, in factoryCategories() order: the original bank plus three per
    // category from the first expansion.
    const std::map<std::string, int> kCategoryCounts {
        { "Bass", 28 }, { "Lead", 28 }, { "Pad", 28 }, { "Pluck", 21 }, { "Keys", 21 },
        { "FX", 18 }, { "Texture", 21 }, { "Sequence", 17 }, { "Drums/Perc", 17 }
    };

    // Host program numbers are frozen: the original bank fills 0..171, expansions append after it.
    constexpr int kOriginalBankSize = 172;

    double toDb (double x) { return 20.0 * std::log10 (std::max (x, 1.0e-9)); }

    double windowRms (const std::vector<float>& a, size_t start, size_t end)
    {
        end = std::min (end, a.size());
        if (end <= start) return 0.0;
        double sum = 0.0;
        for (size_t i = start; i < end; ++i) sum += (double) a[i] * a[i];
        return std::sqrt (sum / (double) (end - start));
    }

    /** What the render checks look at (a 2 s preview at 44.1 kHz, note released at 1 s). */
    struct Measures
    {
        bool finite = true;
        double rmsDb = -180.0;        // whole render
        double momentaryDb = -180.0;  // loudest 400 ms window (for short, percussive sounds)
        float peak = 0.0f;
        double onsetMs = 0.0;         // the 10 ms envelope first reaches -12 dB of its peak
        double dropDb = 0.0;          // envelope peak (first second) over the level at 0.85-0.98 s
        double pulseDb = 0.0;         // 90th / 10th percentile of the 20 ms envelope over 0.15-0.95 s
        SoundFeatures features {};
    };

    Measures measure (const std::vector<float>& a, double sr)
    {
        Measures m;
        float peak = 0.0f;
        for (const float x : a)
        {
            if (! std::isfinite (x)) m.finite = false;
            peak = std::max (peak, std::fabs (x));
        }
        m.peak = peak;
        if (! m.finite || a.empty()) return m;

        m.rmsDb = toDb (windowRms (a, 0, a.size()));
        const size_t win400 = (size_t) (0.4 * sr), hop10 = (size_t) (0.01 * sr);
        double mom = 0.0;
        for (size_t s = 0; s + win400 <= a.size(); s += hop10)
            mom = std::max (mom, windowRms (a, s, s + win400));
        m.momentaryDb = toDb (mom);

        const size_t w = (size_t) (0.01 * sr), hop = (size_t) (0.005 * sr), oneSecond = (size_t) sr;
        double envPeak = 0.0;
        for (size_t s = 0; s + w <= oneSecond; s += hop)
            envPeak = std::max (envPeak, windowRms (a, s, s + w));
        size_t onset = 0;
        for (; onset + w <= oneSecond; onset += hop)
            if (windowRms (a, onset, onset + w) >= 0.25 * envPeak) break;
        m.onsetMs = 1000.0 * (double) (onset + w) / sr;
        m.dropDb = toDb (envPeak) - toDb (windowRms (a, (size_t) (0.85 * sr), (size_t) (0.98 * sr)));

        std::vector<double> env;
        const size_t w20 = (size_t) (0.02 * sr);
        for (size_t s = (size_t) (0.15 * sr); s + w20 <= (size_t) (0.95 * sr); s += hop10)
            env.push_back (windowRms (a, s, s + w20));
        std::sort (env.begin(), env.end());
        m.pulseDb = toDb (env[env.size() * 9 / 10]) - toDb (env[env.size() / 10]);

        m.features = extractFeatures (a.data(), (int) a.size(), sr);
        return m;
    }

    /** The diversity space: sound-map features, scaled so one unit is about one octave of brightness. */
    std::array<double, 4> featureVector (const SoundFeatures& f)
    {
        return { std::log2 (std::max ((double) f.centroidHz, 20.0)),
                 0.5 * std::log2 (std::max ((double) f.attackMs, 1.0)),
                 4.0 * (double) f.noisiness,
                 2.0 * (double) f.movement };
    }
}

AP_TEST (presets_bank_shape)
{
    const auto& bank = factoryPresets();
    CHECK ((int) bank.size() >= kOriginalBankSize && (int) bank.size() <= 240);
    CHECK (&bank == &factoryPresets());   // built once
    CHECK (bank.size() >= 2 && bank[0].name == "Verdant Keys" && bank[1].name == "Glass Piano");   // host programs 0 and 1

    const auto& known = factoryCategories();
    CHECK_EQ ((int) known.size(), 9);
    for (size_t c = 0; c < known.size() && c < kCategoryCounts.size(); ++c)
        CHECK (kCategoryCounts.count (known[c]) == 1);
    const auto& vocabulary = factoryTags();
    const std::set<std::string> vocab (vocabulary.begin(), vocabulary.end());
    CHECK (vocab.size() == vocabulary.size());

    std::set<std::string> names;
    std::map<std::string, int> counts;
    int lastCategory = 0, badOrder = 0, badTags = 0, badNames = 0, macroPlayable = 0, unnamedMacroRoutes = 0;
    int aliveInRange = 0, flatPadsOrTextures = 0, deadRoutes = 0;
    for (size_t i = 0; i < bank.size(); ++i)
    {
        const auto& p = bank[i];
        if (p.name.empty() || p.name.size() > 24) { ++badNames; std::printf ("    bad name: '%s'\n", p.name.c_str()); }
        names.insert (p.name);
        ++counts[p.category];
        const auto cat = std::find (known.begin(), known.end(), p.category);
        CHECK (cat != known.end());
        // After the two openers, the original bank runs category by category in browser order, and
        // so does each expansion after it.
        const int catIndex = (int) (cat - known.begin());
        if (i == (size_t) kOriginalBankSize) lastCategory = 0;
        if (i >= 2)
        {
            if (catIndex < lastCategory) ++badOrder;
            lastCategory = catIndex;
        }

        std::set<std::string> tags (p.tags.begin(), p.tags.end());
        if (p.tags.size() < 2 || p.tags.size() > 4 || tags.size() != p.tags.size()) ++badTags;
        for (const auto& t : p.tags)
            if (vocab.count (t) == 0) { ++badTags; std::printf ("    %s: tag '%s' not in the vocabulary\n", p.name.c_str(), t.c_str()); }
        CHECK (p.author == "Curtis Tech Solutions");
        CHECK (! p.seedLocked);

        // Playable from the macros: Macro 1..4 routed somewhere, and every routed macro is named.
        bool playable = false;
        for (const auto& [src, dst] : routes (p))
        {
            const int m = (int) src - (int) ModSource::Macro1;
            if (m < 0 || m >= kNumMacros) continue;
            if (m < 4) playable = true;
            if (p.macroNames[(size_t) m] == "Macro " + std::to_string (m + 1) || p.macroNames[(size_t) m].empty())
                ++unnamedMacroRoutes;
        }
        if (playable) ++macroPlayable;

        // Every matrix slot with a source points at a modulatable destination (else it does nothing).
        for (int slot = 0; slot < kModSlots; ++slot)
            if ((ModSource) (int) val (p, modSrc (slot)) != ModSource::None && val (p, modAmt (slot)) != 0.0f
                && ! ((int) val (p, modDst (slot)) > 0))
            {
                ++deadRoutes;
                std::printf ("    %s: mod slot %d has no destination\n", p.name.c_str(), slot + 1);
            }

        const float alive = val (p, P::Alive);
        if (alive >= 10.0f && alive <= 35.0f) ++aliveInRange;
        if ((p.category == "Pad" || p.category == "Texture") && ! (val (p, P::Evolve) >= 20.0f)) ++flatPadsOrTextures;
    }
    CHECK (names.size() == bank.size());
    CHECK_EQ (badNames, 0);
    CHECK_EQ (badOrder, 0);
    CHECK_EQ (badTags, 0);
    CHECK_EQ (deadRoutes, 0);
    for (const auto& [category, expected] : kCategoryCounts)
        CHECK_EQ (counts[category], expected);
    CHECK_EQ ((int) counts.size(), (int) known.size());

    std::printf ("    %zu presets; macro-playable %d (%.0f %%), alive 10-35 %% on %d\n", bank.size(), macroPlayable,
                 100.0 * macroPlayable / (double) bank.size(), aliveInRange);
    CHECK (macroPlayable * 100 >= 60 * (int) bank.size());
    CHECK_EQ (unnamedMacroRoutes, 0);
    CHECK (aliveInRange * 100 >= 70 * (int) bank.size());   // "most": drums sit lower, drones higher
    CHECK_EQ (flatPadsOrTextures, 0);                         // pads and textures evolve

    CHECK_EQ (factoryPreviewNote ("Bass"), 36);
    CHECK_EQ (factoryPreviewNote ("Pad"), 48);
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
        CHECK (q.name == p.name && q.tags == p.tags && q.category == p.category && q.macroNames == p.macroNames);
        for (size_t l = 0; l < 4; ++l)
            CHECK (sameShape (q.lfoShapes[l], p.lfoShapes[l]));
    }
}

AP_TEST (presets_feature_coverage)
{
    const auto& bank = factoryPresets();
    std::array<int, kNumFactoryTables> tableUse {};
    std::array<int, 8> warpUse {};
    std::array<int, 9> fxUse {};
    std::array<int, 11> filterUse {};
    int serialPairs = 0, parallelPairs = 0, symp = 0, arp = 0, seq = 0, physics = 0, customShapes = 0, bigUnison = 0,
        monoLegato = 0, pitchSnapSeq = 0, keySnappedSeq = 0, envToPosCut = 0, lfoMatrix = 0, bassesWithoutSub = 0,
        userTables = 0, fmWithoutModulator = 0;

    for (const auto& p : bank)
    {
        std::set<int> tables, warps, filters, fx;
        for (int o = 0; o < 2; ++o)
        {
            if (! oscOn (p, o)) continue;
            const int t = (int) val (p, oscParam (o, OscParam::Table));
            if (t >= kNumFactoryTables) ++userTables; else tables.insert (t);
            const int w = (int) val (p, oscParam (o, OscParam::WarpMode));
            if (w > 0) warps.insert (w);
            // FM Osc needs the other oscillator running to have a modulator.
            if (w == 5 && ! oscOn (p, 1 - o)) ++fmWithoutModulator;
        }
        for (int f = 0; f < 2; ++f)
            if (filterOn (p, f)) filters.insert ((int) val (p, filterParam (f, FilterParam::Type)));
        for (int s = 0; s < kNumFxSlots; ++s)
            if ((int) val (p, fxSlotType (s)) > 0) fx.insert ((int) val (p, fxSlotType (s)));
        for (int t : tables) ++tableUse[(size_t) t];
        for (int w : warps) ++warpUse[(size_t) w];
        for (int f : filters) ++filterUse[(size_t) f];
        for (int t : fx) ++fxUse[(size_t) t];

        if (filterOn (p, 0) && filterOn (p, 1))
            (val (p, P::FilterRouting) > 0.5f ? parallelPairs : serialPairs) += 1;
        if (val (p, P::SympAmount) > 0.0f) ++symp;
        if (val (p, P::ArpOn) > 0.5f) ++arp;
        const bool seqOn = val (p, P::SeqOn) > 0.5f;
        if (seqOn)
        {
            bool stepData = false;
            for (int s = 0; s < (int) val (p, P::SeqSteps); ++s)
                if (val (p, seqStepParam (s, SeqStepParam::Pitch)) != 0.0f) stepData = true;
            if (stepData) ++seq;
            if (val (p, P::SeqSnap) > 0.5f && (int) val (p, P::KeyScale) != 0) ++keySnappedSeq;
        }
        if (p.category == "Sequence" && val (p, P::PitchSnap) > 0.5f && (int) val (p, P::KeyScale) != 0) ++pitchSnapSeq;
        if (usesSource (p, ModSource::Ball, ModSource::Orbit)) ++physics;
        if (usesSource (p, ModSource::Lfo1, ModSource::Lfo4)) ++lfoMatrix;

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
            if (oscOn (p, o) && val (p, oscParam (o, OscParam::UniVoices)) >= 8.0f) { ++bigUnison; break; }
        if (isMono (p)) ++monoLegato;

        bool envRoute = false;
        for (const auto& [src, dst] : routes (p))
            if ((src == ModSource::Env2 || src == ModSource::Env3)
                && (dst == oscParam (0, OscParam::Pos) || dst == oscParam (1, OscParam::Pos)
                    || dst == filterParam (0, FilterParam::Cutoff) || dst == filterParam (1, FilterParam::Cutoff)))
                envRoute = true;
        if (envRoute) ++envToPosCut;

        if (p.category == "Bass" && ! (val (p, P::SubLevel) > 0.0f)) ++bassesWithoutSub;
    }

    std::printf ("    tables:");
    for (int t : tableUse) std::printf (" %d", t);
    std::printf ("\n    warps (Sync..Remap):");
    for (int w = 1; w < 8; ++w) std::printf (" %d", warpUse[(size_t) w]);
    std::printf ("\n    filters (LP12..Ladder):");
    for (int f : filterUse) std::printf (" %d", f);
    std::printf ("\n    fx (Dist..Comp):");
    for (int t = 1; t < 9; ++t) std::printf (" %d", fxUse[(size_t) t]);
    std::printf ("\n    serial %d, parallel %d, symp %d, arp %d, seq %d, physics %d, custom LFO %d, unison>=8 %d, mono/legato %d, "
                 "pitch-snap seq %d, key-snapped seq %d\n",
                 serialPairs, parallelPairs, symp, arp, seq, physics, customShapes, bigUnison, monoLegato, pitchSnapSeq, keySnappedSeq);

    for (int t = 0; t < kNumFactoryTables; ++t)
        CHECK (tableUse[(size_t) t] >= 3);
    CHECK_EQ (userTables, 0);   // the factory bank never depends on an empty User table
    for (int w = 1; w < 8; ++w)
        CHECK (warpUse[(size_t) w] >= 4);
    CHECK_EQ (fmWithoutModulator, 0);
    for (int f = 0; f < 11; ++f)
        CHECK (filterUse[(size_t) f] >= 3);
    CHECK (serialPairs >= 3);
    CHECK (parallelPairs >= 3);
    for (int t = 1; t < 9; ++t)
        CHECK (fxUse[(size_t) t] >= 8);
    CHECK (symp >= 8);
    CHECK (arp >= 8);
    CHECK (seq >= 10);
    CHECK (physics >= 10);
    CHECK (customShapes >= 10);
    CHECK (bigUnison >= 12);
    CHECK (monoLegato >= 15);
    CHECK (pitchSnapSeq >= 2);
    CHECK (keySnappedSeq >= 3);
    CHECK (envToPosCut >= 20);
    CHECK (lfoMatrix >= 40);
    CHECK_EQ (bassesWithoutSub, 0);
}

AP_TEST (presets_render_level_category_and_diversity)
{
    // Each preset on its category's preview note (C2 for Bass, else C3), velocity 100, held 1 s of a 2 s render.
    //  * Level: loudness-matched to -26 dBFS +- 2 dB: RMS of the whole render, or for the short categories
    //    (Pluck, Drums/Perc) the loudest 400 ms window. A preset whose peak already sits within 80 % of its
    //    ceiling may be quieter (down to -34 dB): the ceiling wins over loudness.
    //  * Peak ceiling: 0.5 for poly patches, 0.7 for mono / legato patches and drums.
    //  * Category sanity, diversity: see below.
    constexpr double kTargetDb = -26.0, kToleranceDb = 2.0, kPeakLimitedFloorDb = -34.0;
    constexpr double kDiversityThreshold = 0.08;
    const double sr = 44100.0;
    const auto& bank = factoryPresets();

    std::vector<Measures> ms;
    ms.reserve (bank.size());
    int nonFinite = 0, silent = 0, offLevel = 0, overPeak = 0, peakLimited = 0, badCategory = 0;
    double minLevel = 1.0e9, maxLevel = -1.0e9;
    float minPeak = 1.0e9f, maxPeak = 0.0f;
    for (const auto& p : bank)
    {
        const auto audio = renderPreview (p, factoryPreviewNote (p.category), 1.0f, 2.0f, sr);
        ms.push_back (measure (audio, sr));
        const auto& m = ms.back();
        const char* name = p.name.c_str();
        if (! m.finite) { ++nonFinite; std::printf ("    non-finite: %s\n", name); continue; }
        if (! (m.rmsDb > -50.0)) { ++silent; std::printf ("    silent: %s (%.1f dB)\n", name, m.rmsDb); }

        const bool percussive = isPercussive (p);
        const double level = percussive ? m.momentaryDb : m.rmsDb;
        const float ceiling = (isMono (p) || p.category == "Drums/Perc") ? 0.7f : 0.5f;
        const bool limited = m.peak >= 0.8f * ceiling;
        const bool inBand = std::fabs (level - kTargetDb) <= kToleranceDb;
        if (! inBand && limited && level >= kPeakLimitedFloorDb) ++peakLimited;
        else if (! inBand) { ++offLevel; std::printf ("    level off: %s %.1f dB (peak %.3f)\n", name, level, (double) m.peak); }
        if (! (m.peak <= ceiling)) { ++overPeak; std::printf ("    peak over %.1f: %s %.3f\n", (double) ceiling, name, (double) m.peak); }
        minLevel = std::min (minLevel, level);
        maxLevel = std::max (maxLevel, level);
        minPeak = std::min (minPeak, m.peak);
        maxPeak = std::max (maxPeak, m.peak);

        // Category sanity.
        const auto& f = m.features;
        auto fail = [&] (const char* rule) { ++badCategory; std::printf ("    %s (%s): %s [atk %.1f ms, onset %.0f ms, cen %.0f Hz, drop %.1f dB, pulse %.1f dB]\n",
                                                                          name, p.category.c_str(), rule, (double) f.attackMs, m.onsetMs,
                                                                          (double) f.centroidHz, m.dropDb, m.pulseDb); };
        if (p.category == "Pad" && ! (f.attackMs >= 150.0f)) fail ("pads swell in (attack >= 150 ms)");
        if (percussive && ! (f.attackMs <= 10.0f)) fail ("hits start at once (attack <= 10 ms)");
        if (percussive && ! (m.dropDb >= 20.0)) fail ("hits decay by >= 20 dB within 1 s");
        if (p.category == "Bass" && ! (f.centroidHz < 1500.0f)) fail ("basses stay low (centroid < 1.5 kHz)");
        if ((p.category == "Lead" || p.category == "Texture") && ! (m.dropDb <= 12.0)) fail ("sustains while held (drop <= 12 dB)");
        if ((p.category == "Keys" || p.category == "Lead" || p.category == "Bass") && ! (m.onsetMs <= 30.0)) fail ("speaks at once (onset <= 30 ms)");
        if (p.category == "Sequence" && ! (m.pulseDb >= 6.0)) fail ("pulses (envelope swing >= 6 dB)");
    }
    std::printf ("    level %.1f .. %.1f dB (%d peak-limited), peak %.3f .. %.3f\n", minLevel, maxLevel, peakLimited,
                 (double) minPeak, (double) maxPeak);
    CHECK_EQ (nonFinite, 0);
    CHECK_EQ (silent, 0);
    CHECK_EQ (offLevel, 0);
    CHECK_EQ (overPeak, 0);
    CHECK_EQ (badCategory, 0);
    CHECK (peakLimited <= 10);

    // Diversity: within a category no two presets may share (nearly) the same sound-map feature vector
    // (log2 centroid, 0.5 log2 attack, 4 x noisiness, 2 x movement: one unit ~ an octave of brightness).
    int tooClose = 0;
    double closest = 1.0e9;
    for (size_t i = 0; i < bank.size(); ++i)
        for (size_t j = i + 1; j < bank.size(); ++j)
        {
            if (bank[i].category != bank[j].category) continue;
            const auto a = featureVector (ms[i].features), b = featureVector (ms[j].features);
            double d = 0.0;
            for (size_t k = 0; k < a.size(); ++k) d += (a[k] - b[k]) * (a[k] - b[k]);
            d = std::sqrt (d);
            closest = std::min (closest, d);
            if (d < kDiversityThreshold)
            {
                ++tooClose;
                std::printf ("    too similar: %s / %s (%.3f)\n", bank[i].name.c_str(), bank[j].name.c_str(), d);
            }
        }
    std::printf ("    closest pair within a category: %.3f (threshold %.2f)\n", closest, kDiversityThreshold);
    CHECK_EQ (tooClose, 0);
}
