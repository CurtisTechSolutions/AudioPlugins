// Breeding: deterministic for a seed, range-safe, group crossover, the
// never-mutate list, extras following their owning parameter, naming, and
// the generation history.

#include "TestHarness.h"

#include "engine/Breeding.h"

#include <cmath>
#include <cstring>
#include <map>
#include <string>

using namespace verdant;

namespace
{
    bool sameValues (const ParamValues& a, const ParamValues& b)
    {
        return std::memcmp (a.data(), b.data(), sizeof (float) * a.size()) == 0;
    }

    bool sameBits (float a, float b) { return std::memcmp (&a, &b, sizeof (float)) == 0; }

    /** Two parents that differ in every parameter (and in every extra). */
    void makeParents (Patch& a, Patch& b)
    {
        a = initPatch();
        b = initPatch();
        a.name = "Alpha";
        b.name = "Beta";
        a.category = "Pad";
        b.category = "Lead";
        a.tags = { "warm", "soft" };
        b.tags = { "soft", "bright" };
        a.livingSeed = 11u;
        b.livingSeed = 22u;
        for (int i = 0; i < kNumParams; ++i)
        {
            const auto& in = info ((P) i);
            a.values[(size_t) i] = clampToRange ((P) i, in.min + 0.2f * (in.max - in.min));
            b.values[(size_t) i] = clampToRange ((P) i, in.min + 0.8f * (in.max - in.min));
        }
        for (int m = 0; m < 8; ++m)
        {
            a.macroNames[(size_t) m] = "A macro " + std::to_string (m);
            b.macroNames[(size_t) m] = "B macro " + std::to_string (m);
        }
        for (int l = 0; l < 4; ++l)
        {
            a.lfoShapes[(size_t) l].points = { { 0.0f, 0.1f * (float) l, 0.0f }, { 1.0f, 1.0f, 0.0f } };
            b.lfoShapes[(size_t) l].points = { { 0.0f, 0.9f, 0.5f }, { 0.5f, 0.0f, 0.0f }, { 1.0f, 0.9f, 0.0f } };
        }
        for (int o = 0; o < 2; ++o)
        {
            a.userTables[(size_t) o].name = "A table";
            b.userTables[(size_t) o].name = "B table";
        }
    }

    int fromWhich (const Patch& a, const Patch& b, const Patch& c, int i)
    {
        if (sameBits (c.values[(size_t) i], a.values[(size_t) i])) return 0;
        if (sameBits (c.values[(size_t) i], b.values[(size_t) i])) return 1;
        return -1;
    }
}

AP_TEST (breeding_is_deterministic)
{
    Patch a, b;
    makeParents (a, b);
    const BreedSettings s { 0.4f, 8, 42u };
    const auto c1 = breed (a, b, s);
    const auto c2 = breed (a, b, s);
    CHECK_EQ ((int) c1.size(), 8);
    CHECK (c1.size() == c2.size());
    bool childrenDiffer = false;
    for (size_t k = 0; k < c1.size() && k < c2.size(); ++k)
    {
        CHECK (sameValues (c1[k].values, c2[k].values));
        CHECK (c1[k].name == c2[k].name);
        CHECK_EQ (c1[k].livingSeed, c2[k].livingSeed);
        CHECK (c1[k].userTables[0].name == c2[k].userTables[0].name);
        if (k > 0 && ! sameValues (c1[k].values, c1[0].values)) childrenDiffer = true;
    }
    CHECK (childrenDiffer);

    // A different seed gives different children.
    const auto c3 = breed (a, b, BreedSettings { 0.4f, 8, 43u });
    int different = 0;
    for (size_t k = 0; k < c1.size(); ++k)
        if (! sameValues (c1[k].values, c3[k].values)) ++different;
    CHECK (different >= 7);
}

AP_TEST (breeding_children_are_range_safe)
{
    Patch a, b;
    makeParents (a, b);
    // Push the parents to the extremes so mutation keeps hitting the clamps.
    for (int i = 0; i < kNumParams; ++i)
    {
        a.values[(size_t) i] = info ((P) i).min;
        b.values[(size_t) i] = info ((P) i).max;
    }
    for (uint32_t seed : { 1u, 2u, 3u })
    {
        const auto children = breed (a, b, BreedSettings { 1.0f, 16, seed });
        CHECK_EQ ((int) children.size(), 16);
        for (const auto& c : children)
            for (int i = 0; i < kNumParams; ++i)
            {
                const float v = c.values[(size_t) i];
                CHECK (std::isfinite (v));
                CHECK (sameBits (v, clampToRange ((P) i, v)));
            }
    }

    CHECK_EQ ((int) breed (a, b, BreedSettings { 0.25f, 0, 1u }).size(), 1);
    CHECK_EQ ((int) breed (a, b, BreedSettings { 0.25f, 99, 1u }).size(), 16);
    CHECK_EQ ((int) breed (a, b, BreedSettings { std::nanf (""), 2, 1u }).size(), 2);
}

AP_TEST (breeding_mutation_zero_only_inherits)
{
    Patch a, b;
    makeParents (a, b);
    const auto children = breed (a, b, BreedSettings { 0.0f, 16, 7u });
    int bad = 0;
    for (const auto& c : children)
        for (int i = 0; i < kNumParams; ++i)
            if (fromWhich (a, b, c, i) < 0) ++bad;
    CHECK_EQ (bad, 0);
}

AP_TEST (breeding_group_inheritance)
{
    Patch a, b;
    makeParents (a, b);
    for (int i = 0; i < kNumParams; ++i)
        CHECK (! sameBits (a.values[(size_t) i], b.values[(size_t) i]));   // the premise

    double majoritySum = 0.0;
    int groupsCounted = 0;
    int slotSplits = 0, fxSplits = 0, groupsFromA = 0, groupsFromB = 0;
    for (uint32_t seed = 1; seed <= 4; ++seed)
    {
        for (const auto& c : breed (a, b, BreedSettings { 0.0f, 16, seed }))
        {
            std::map<std::string, std::pair<int, int>> counts;   // group -> (from a, from b)
            for (int i = 0; i < kNumParams; ++i)
            {
                const int w = fromWhich (a, b, c, i);
                auto& cnt = counts[info ((P) i).group];
                (w == 0 ? cnt.first : cnt.second)++;
            }
            for (const auto& [group, cnt] : counts)
            {
                const int total = cnt.first + cnt.second;
                majoritySum += (double) std::max (cnt.first, cnt.second) / total;
                ++groupsCounted;
                (cnt.first >= cnt.second ? groupsFromA : groupsFromB)++;
            }

            // A matrix slot's source, destination and amount travel together.
            for (int s = 0; s < kModSlots; ++s)
            {
                const int w = fromWhich (a, b, c, (int) modSrc (s));
                if (fromWhich (a, b, c, (int) modDst (s)) != w || fromWhich (a, b, c, (int) modAmt (s)) != w)
                    ++slotSplits;
            }
            // The FX slot types come from one parent.
            for (int s = 1; s < kNumFxSlots; ++s)
                if (fromWhich (a, b, c, (int) fxSlotType (s)) != fromWhich (a, b, c, (int) fxSlotType (0)))
                    ++fxSplits;
        }
    }
    const double meanMajority = majoritySum / groupsCounted;
    CHECK (meanMajority >= 0.70);
    CHECK (meanMajority < 0.95);       // the 25 % in-group swap does happen
    CHECK_EQ (slotSplits, 0);
    CHECK_EQ (fxSplits, 0);
    CHECK (groupsFromA > groupsCounted / 4);   // both parents contribute groups
    CHECK (groupsFromB > groupsCounted / 4);
}

AP_TEST (breeding_never_mutate_list)
{
    Patch a, b;
    makeParents (a, b);
    int violations = 0, mutatedFloats = 0;
    for (uint32_t seed = 1; seed <= 4; ++seed)
    {
        for (const auto& c : breed (a, b, BreedSettings { 1.0f, 16, seed }))
        {
            std::vector<P> fixed { P::Voices, P::VoiceMode, P::Master, oscParam (0, OscParam::Table), oscParam (1, OscParam::Table) };
            for (int s = 0; s < kNumFxSlots; ++s) fixed.push_back (fxSlotType (s));
            for (P p : fixed)
                if (fromWhich (a, b, c, (int) p) < 0) ++violations;

            // Matrix pairs: (src, dst) equal one parent's pair.
            for (int s = 0; s < kModSlots; ++s)
            {
                const int w = fromWhich (a, b, c, (int) modSrc (s));
                if (w < 0 || fromWhich (a, b, c, (int) modDst (s)) != w) ++violations;
            }
            // Choice / Bool params only ever hold a parent's value.
            for (int i = 0; i < kNumParams; ++i)
            {
                const auto kind = info ((P) i).kind;
                if ((kind == ParamKind::Choice || kind == ParamKind::Bool) && fromWhich (a, b, c, i) < 0) ++violations;
                if (kind == ParamKind::Float && fromWhich (a, b, c, i) < 0) ++mutatedFloats;
            }
        }
    }
    CHECK_EQ (violations, 0);
    CHECK (mutatedFloats > 1000);   // mutation 1 really does move the continuous params
}

AP_TEST (breeding_extras_follow_their_parameter)
{
    Patch a, b;
    makeParents (a, b);
    for (const auto& c : breed (a, b, BreedSettings { 0.8f, 16, 5u }))
    {
        for (int o = 0; o < 2; ++o)
        {
            const int w = fromWhich (a, b, c, (int) oscParam (o, OscParam::Table));
            CHECK (c.userTables[(size_t) o].name == (w == 0 ? "A table" : "B table"));
        }
        for (int l = 0; l < 4; ++l)
        {
            const int w = fromWhich (a, b, c, (int) lfoParam (l, LfoParam::Shape));
            const auto& expected = (w == 0 ? a : b).lfoShapes[(size_t) l].points;
            const auto& got = c.lfoShapes[(size_t) l].points;
            CHECK (got.size() == expected.size() && std::memcmp (got.data(), expected.data(), got.size() * sizeof (LfoShapePoint)) == 0);
        }
        CHECK (c.macroNames == a.macroNames || c.macroNames == b.macroNames);
    }
}

AP_TEST (breeding_names_category_tags_seed)
{
    Patch a, b;
    makeParents (a, b);
    const auto children = breed (a, b, BreedSettings { 0.25f, 8, 9u });
    CHECK (children[0].name == "Alpha \xC3\x97 Beta #1");
    CHECK (children[7].name == "Alpha \xC3\x97 Beta #8");
    for (const auto& c : children)
    {
        CHECK (c.category == "Pad");
        CHECK ((c.tags == std::vector<std::string> { "warm", "soft", "bright" }));
        CHECK (! c.seedLocked);
        CHECK (c.author.empty());
    }
    CHECK (children[0].livingSeed != children[1].livingSeed);
    CHECK (children[0].livingSeed != a.livingSeed && children[0].livingSeed != b.livingSeed);
}

AP_TEST (breeding_history_undo_redo)
{
    Patch a, b;
    makeParents (a, b);
    BreedHistory h;
    CHECK (h.current() == nullptr);
    CHECK (! h.canUndo());
    CHECK_EQ (h.generation(), 0);

    for (uint32_t g = 1; g <= 3; ++g)
        h.push (a, b, breed (a, b, BreedSettings { 0.25f, 2 + (int) g, g }));
    CHECK_EQ (h.generation(), 3);
    CHECK (h.current() != nullptr && h.current()->size() == 5);
    CHECK (h.parent (0) != nullptr && h.parent (0)->name == "Alpha");

    h.undo();
    CHECK_EQ (h.generation(), 2);
    CHECK (h.current() != nullptr && h.current()->size() == 4);
    CHECK (h.canRedo());
    h.redo();
    CHECK_EQ (h.generation(), 3);
    h.undo();

    // A new generation after an undo drops the redo tail.
    h.push (b, a, breed (b, a, BreedSettings { 0.25f, 7, 99u }));
    CHECK_EQ (h.generation(), 3);
    CHECK (! h.canRedo());
    CHECK (h.current()->size() == 7);
    CHECK (h.parent (0)->name == "Beta");

    h.undo(); h.undo(); h.undo();
    CHECK_EQ (h.generation(), 0);
    CHECK (h.current() == nullptr);
    CHECK (! h.canUndo());
    h.undo();   // harmless
    CHECK_EQ (h.generation(), 0);
    h.clear();
    CHECK (! h.canRedo());
}
