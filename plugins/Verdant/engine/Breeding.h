#pragma once

// Patch breeding: children from two parents by group crossover plus small,
// musically scaled mutations; deterministic for a given seed.
//
// Crossover per group (whole group from one parent, chosen by a seeded coin) with 25 % per-param
// uniform crossover inside the group; mutation: continuous params get N(0, mutation x 0.15) in the
// normalised domain, clamped; Int rounds; Choice / Bool flip to the other parent's value with
// p = mutation x 0.2; never mutates: voices, voice_mode, master, {o}_table (inherits with its user
// table), mod{n}_dst / src keep pairs together. Children are named "<A> x <B> #k" (UTF-8 multiplication sign).
//
// Owner: W-F. FOUNDATION STUB: children are copies of parent A (correctly named); BreedHistory works.

#include "Patch.h"

#include <cstdint>
#include <vector>

namespace verdant
{

struct BreedSettings
{
    float mutation = 0.25f;   // 0..1
    int count = 8;
    uint32_t seed = 1;
};

std::vector<Patch> breed (const Patch& a, const Patch& b, const BreedSettings&);

/** Generation history for the BREED page (message thread). */
class BreedHistory
{
public:
    void push (Patch a, Patch b, std::vector<Patch> children);
    bool canUndo() const;
    void undo();
    const std::vector<Patch>* current() const;   // children of the latest generation, or nullptr
    int generation() const;                      // number of generations held (0 = none)

private:
    struct Generation { Patch a, b; std::vector<Patch> children; };
    std::vector<Generation> generations;
};

} // namespace verdant
