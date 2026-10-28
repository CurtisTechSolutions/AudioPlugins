#pragma once

// Patch breeding: children from two parents by group crossover plus small,
// musically scaled mutations; deterministic for a given seed.
//
// For child k (0-based; its RNG is seeded from hash (seed, k)):
//  * Crossover: every parameter group (ParamInfo::group, table order) takes a
//    base parent from a seeded coin; inside the group each parameter then has
//    a 25 % chance to come from the other parent. Exceptions that keep things
//    coherent: a matrix slot's (source, dest, amount) travels together (the
//    25 % swap is per slot), and the eight FX slot types come together from
//    the FX group's parent (the rack order stays intact).
//  * Mutation (skipped entirely when mutation <= 0, so mutation 0 copies
//    parent values bit-exactly): Float and Int params get N(0, mutation x 0.15)
//    in the normalised domain, clamped (Int rounded); Choice / Bool params take
//    the other parent's value with p = mutation x 0.2 (never a third value).
//    Never mutated: voices, voice_mode, master, {o}_table, fx{n}_type,
//    mod{n}_src / mod{n}_dst (the slot amount does mutate).
//  * Extras follow the parameter that owns them: a user table (with its canvas)
//    comes from the parent that supplied {o}_table, a custom LFO shape from the
//    parent that supplied {l}_shape, the macro names from the Macros group's
//    parent.
//  * Child seed = hash (parent seeds, breed seed, k); name "<A> x <B> #k" (UTF-8
//    multiplication sign, k 1-based); category = A's; tags = A's then B's new
//    ones; author empty; seed unlocked. Every value is range-safe.
//  * count is clamped to 1..16.
//
// Owner: W-F.

#include "Patch.h"

#include <cstdint>
#include <vector>

namespace verdant
{

struct BreedSettings
{
    float mutation = 0.25f;   // 0..1
    int count = 8;            // clamped to 1..16
    uint32_t seed = 1;
};

std::vector<Patch> breed (const Patch& a, const Patch& b, const BreedSettings&);

/** Generation history for the BREED page (message thread). Undo steps back
    without forgetting; push() after an undo drops the undone generations. */
class BreedHistory
{
public:
    void push (Patch a, Patch b, std::vector<Patch> children);
    bool canUndo() const;
    void undo();
    bool canRedo() const;                        // addition: a generation was undone and not overwritten
    void redo();                                 // addition
    const std::vector<Patch>* current() const;   // children of the current generation, or nullptr
    const Patch* parent (int which) const;       // addition: parent A (0) / B (1) of the current generation, or nullptr
    int generation() const;                      // number of generations up to the current one (0 = none)
    void clear();                                // addition

private:
    struct Generation { Patch a, b; std::vector<Patch> children; };
    std::vector<Generation> generations;
    size_t cursor = 0;                           // generations[0 .. cursor) are live; the rest is the redo tail
};

} // namespace verdant
