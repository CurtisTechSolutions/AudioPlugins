#include "Breeding.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace verdant
{

namespace
{
    constexpr double kInGroupSwap = 0.25;   // per-param (per matrix slot) chance to take the other parent
    constexpr double kSigmaScale  = 0.15;   // Float / Int: sigma = mutation x 0.15 (normalised domain)
    constexpr double kFlipScale   = 0.2;    // Choice / Bool: p (other parent) = mutation x 0.2

    /** splitmix64: a small, high-quality seeded generator (no shared state, fully deterministic). */
    struct SplitMix
    {
        uint64_t state;

        uint64_t next() noexcept
        {
            uint64_t z = (state += 0x9E3779B97F4A7C15ull);
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            return z ^ (z >> 31);
        }

        /** Uniform in [0, 1). */
        double uniform() noexcept { return (double) (next() >> 11) * (1.0 / 9007199254740992.0); }

        /** Standard normal by Box-Muller (one value per call keeps the stream simple). */
        double gaussian() noexcept
        {
            const double u1 = 1.0 - uniform();   // (0, 1]
            const double u2 = uniform();
            return std::sqrt (-2.0 * std::log (u1)) * std::cos (6.283185307179586 * u2);
        }
    };

    uint32_t mix32 (uint32_t h, uint32_t v) noexcept
    {
        h ^= v + 0x9E3779B9u + (h << 6) + (h >> 2);
        h ^= h >> 16; h *= 0x7FEB352Du;
        h ^= h >> 15; h *= 0x846CA68Bu;
        h ^= h >> 16;
        return h;
    }

    /** Parameter -> group index (groups numbered by first appearance in table order), plus the
        per-param rules, computed once. */
    struct Layout
    {
        std::vector<int> groupOf;            // per param
        int numGroups = 0;
        int macroGroup = -1;
        std::vector<bool> neverMutate;       // per param
        std::vector<bool> isFxSlotType;       // per param
        std::vector<int> modSlotOf;          // per param: matrix slot (0..23) or -1

        Layout()
        {
            std::vector<const char*> names;
            groupOf.resize ((size_t) kNumParams);
            neverMutate.assign ((size_t) kNumParams, false);
            isFxSlotType.assign ((size_t) kNumParams, false);
            modSlotOf.assign ((size_t) kNumParams, -1);

            for (int i = 0; i < kNumParams; ++i)
            {
                const char* g = info ((P) i).group;
                int found = -1;
                for (size_t n = 0; n < names.size(); ++n)
                    if (std::strcmp (names[n], g) == 0) { found = (int) n; break; }
                if (found < 0)
                {
                    found = (int) names.size();
                    names.push_back (g);
                }
                groupOf[(size_t) i] = found;
                if (std::strcmp (g, "Macros") == 0) macroGroup = found;
            }
            numGroups = (int) names.size();

            for (P p : { P::Voices, P::VoiceMode, P::Master, oscParam (0, OscParam::Table), oscParam (1, OscParam::Table) })
                neverMutate[(size_t) p] = true;
            for (int s = 0; s < kNumFxSlots; ++s)
            {
                neverMutate[(size_t) fxSlotType (s)] = true;
                isFxSlotType[(size_t) fxSlotType (s)] = true;
            }
            for (int s = 0; s < kModSlots; ++s)
            {
                neverMutate[(size_t) modSrc (s)] = true;
                neverMutate[(size_t) modDst (s)] = true;
                for (P p : { modSrc (s), modDst (s), modAmt (s) })
                    modSlotOf[(size_t) p] = s;
            }
        }
    };

    const Layout& layout()
    {
        static const Layout l;
        return l;
    }

    Patch makeChild (const Patch& a, const Patch& b, float mutation, uint32_t seed, int k)
    {
        const auto& lay = layout();
        const Patch* parents[2] = { &a, &b };
        SplitMix rng { ((uint64_t) seed << 32) ^ (uint64_t) mix32 (0xB4EED000u, (uint32_t) k) ^ ((uint64_t) (uint32_t) k << 7) };
        rng.next();   // decorrelate neighbouring seeds

        // ---- Crossover
        std::vector<int> groupParent ((size_t) lay.numGroups);
        for (auto& gp : groupParent)
            gp = rng.uniform() < 0.5 ? 0 : 1;

        Patch child = a;
        std::vector<int> from ((size_t) kNumParams, 0);
        int slotParent = 0;
        for (int i = 0; i < kNumParams; ++i)
        {
            const int base = groupParent[(size_t) lay.groupOf[(size_t) i]];
            int parent = base;
            const int slot = lay.modSlotOf[(size_t) i];
            if (slot >= 0)
            {
                if ((P) i == modSrc (slot))   // one draw per slot: src, dst and amount travel together
                    slotParent = rng.uniform() < kInGroupSwap ? 1 - base : base;
                parent = slotParent;
            }
            else if (! lay.isFxSlotType[(size_t) i])
            {
                parent = rng.uniform() < kInGroupSwap ? 1 - base : base;
            }
            from[(size_t) i] = parent;
            child.values[(size_t) i] = parents[parent]->values[(size_t) i];
        }

        // ---- Mutation
        if (mutation > 0.0f)
        {
            const double sigma = (double) mutation * kSigmaScale;
            const double flip = (double) mutation * kFlipScale;
            for (int i = 0; i < kNumParams; ++i)
            {
                if (lay.neverMutate[(size_t) i])
                    continue;
                const P p = (P) i;
                const auto kind = info (p).kind;
                float& v = child.values[(size_t) i];
                if (kind == ParamKind::Float || kind == ParamKind::Int)
                {
                    const double n = (double) toNormalised (p, v) + sigma * rng.gaussian();
                    v = fromNormalised (p, (float) std::clamp (n, 0.0, 1.0));   // Int rounds inside
                }
                else if (rng.uniform() < flip)
                {
                    const int other = 1 - from[(size_t) i];
                    v = parents[other]->values[(size_t) i];
                    from[(size_t) i] = other;
                }
            }
        }

        for (int i = 0; i < kNumParams; ++i)
            child.values[(size_t) i] = clampToRange ((P) i, child.values[(size_t) i]);

        // ---- Extras follow the parameter that owns them
        for (int o = 0; o < kNumOscs; ++o)
            child.userTables[(size_t) o] = parents[from[(size_t) oscParam (o, OscParam::Table)]]->userTables[(size_t) o];
        for (int l = 0; l < kNumLfos; ++l)
            child.lfoShapes[(size_t) l] = parents[from[(size_t) lfoParam (l, LfoParam::Shape)]]->lfoShapes[(size_t) l];
        if (lay.macroGroup >= 0)
            child.macroNames = parents[groupParent[(size_t) lay.macroGroup]]->macroNames;

        // ---- Identity
        child.name = a.name + " \xC3\x97 " + b.name + " #" + std::to_string (k + 1);
        child.category = a.category;
        child.author.clear();   // a new sound: whoever saves it signs it
        child.tags = a.tags;
        for (const auto& t : b.tags)
            if (std::find (child.tags.begin(), child.tags.end(), t) == child.tags.end())
                child.tags.push_back (t);
        child.livingSeed = mix32 (mix32 (mix32 (a.livingSeed, b.livingSeed), seed), (uint32_t) k + 1u);
        child.seedLocked = false;
        return child;
    }
}

std::vector<Patch> breed (const Patch& a, const Patch& b, const BreedSettings& settings)
{
    const int count = std::clamp (settings.count, 1, 16);
    const float mutation = std::isfinite (settings.mutation) ? std::clamp (settings.mutation, 0.0f, 1.0f) : 0.0f;
    std::vector<Patch> children;
    children.reserve ((size_t) count);
    for (int k = 0; k < count; ++k)
        children.push_back (makeChild (a, b, mutation, settings.seed, k));
    return children;
}

//==============================================================================
void BreedHistory::push (Patch a, Patch b, std::vector<Patch> children)
{
    generations.erase (generations.begin() + (std::ptrdiff_t) cursor, generations.end());   // drop the redo tail
    generations.push_back (Generation { std::move (a), std::move (b), std::move (children) });
    cursor = generations.size();
}

bool BreedHistory::canUndo() const
{
    return cursor > 0;
}

void BreedHistory::undo()
{
    if (cursor > 0)
        --cursor;
}

bool BreedHistory::canRedo() const
{
    return cursor < generations.size();
}

void BreedHistory::redo()
{
    if (cursor < generations.size())
        ++cursor;
}

const std::vector<Patch>* BreedHistory::current() const
{
    return cursor == 0 ? nullptr : &generations[cursor - 1].children;
}

const Patch* BreedHistory::parent (int which) const
{
    if (cursor == 0) return nullptr;
    const auto& g = generations[cursor - 1];
    return which == 0 ? &g.a : &g.b;
}

int BreedHistory::generation() const
{
    return (int) cursor;
}

void BreedHistory::clear()
{
    generations.clear();
    cursor = 0;
}

} // namespace verdant
