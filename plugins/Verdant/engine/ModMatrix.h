#pragma once

// The 24-slot modulation matrix (CONTRACT §4). For every modulatable param:
// n = toNormalised (base) + sum (amount x source) over the slots targeting it,
// clamped to 0..1, then fromNormalised. Amount -100..100 % maps to -1..1 in
// the normalised domain.
//
// apply() is O(slots): it accumulates the active slots into their destinations
// and converts only the touched destinations; it never allocates and is
// noexcept, so it runs per voice per control block on the audio thread.
//
// Owner: W-D.

#include "Params.h"

#include <array>

namespace verdant
{

struct ModRoute
{
    ModSource src = ModSource::None;
    int dest = 0;          // destination index (0 = none), see modDestination()
    float amount = 0;      // -1..1
};

struct ModRouting { std::array<ModRoute, kModSlots> slots; };

/** Reads the mod{n}_src / _dst / _amt params (amount % -> -1..1, out-of-range sources / destinations -> None). */
ModRouting routingFrom (const ParamValues&);

/** True when the mod{n}_src / _dst / _amt params differ between a and b (the routing must be re-derived). */
bool routingParamsDiffer (const ParamValues& a, const ParamValues& b) noexcept;

struct ModSourceValues { std::array<float, (int) ModSource::Count> v {}; };

class ModMatrix
{
public:
    /** out = base with the modulated entries replaced; offsets (size numModDestinations() + 1, may be null)
        receives the summed normalised offset per destination. `out` may alias `base`. */
    static void apply (const ParamValues& base, const ModRouting&, const ModSourceValues&, ParamValues& out, float* offsets) noexcept;
};

} // namespace verdant
