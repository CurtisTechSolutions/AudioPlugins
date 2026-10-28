#include "ModMatrix.h"

#include <cmath>

namespace verdant
{

ModRouting routingFrom (const ParamValues& v)
{
    ModRouting r;
    for (int s = 0; s < kModSlots; ++s)
    {
        auto& slot = r.slots[(size_t) s];
        const int src = (int) std::lround (v[(size_t) modSrc (s)]);
        const int dst = (int) std::lround (v[(size_t) modDst (s)]);
        slot.src = src > 0 && src < (int) ModSource::Count ? (ModSource) src : ModSource::None;
        slot.dest = dst > 0 && dst <= numModDestinations() ? dst : 0;
        slot.amount = v[(size_t) modAmt (s)] * 0.01f;
    }
    return r;
}

void ModMatrix::apply (const ParamValues& base, const ModRouting& routing, const ModSourceValues& sources,
                       ParamValues& out, float* offsets) noexcept
{
    std::array<float, kNumModDestinations + 1> sum {};
    bool any = false;

    for (const auto& slot : routing.slots)
    {
        if (slot.src == ModSource::None || slot.dest <= 0 || slot.dest > kNumModDestinations)
            continue;
        const float s = sources.v[(size_t) slot.src];
        sum[(size_t) slot.dest] += slot.amount * (std::isfinite (s) ? s : 0.0f);
        any = true;
    }

    if (&out != &base)
        out = base;

    if (offsets != nullptr)
        for (int d = 0; d <= kNumModDestinations; ++d)
            offsets[d] = sum[(size_t) d];

    if (! any)
        return;

    for (int d = 1; d <= kNumModDestinations; ++d)
    {
        const float off = sum[(size_t) d];
        if (off == 0.0f)
            continue;
        const P p = modDestination (d);
        const float n = toNormalised (p, base[(size_t) p]) + off;
        out[(size_t) p] = fromNormalised (p, n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n));
    }
}

} // namespace verdant
