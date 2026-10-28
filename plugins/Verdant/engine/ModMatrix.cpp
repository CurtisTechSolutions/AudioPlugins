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
        const float srcValue = v[(size_t) modSrc (s)];
        const float dstValue = v[(size_t) modDst (s)];
        const float amtValue = v[(size_t) modAmt (s)];
        const int src = std::isfinite (srcValue) ? (int) std::lround (srcValue) : 0;
        const int dst = std::isfinite (dstValue) ? (int) std::lround (dstValue) : 0;
        slot.src = src > 0 && src < (int) ModSource::Count ? (ModSource) src : ModSource::None;
        slot.dest = dst > 0 && dst <= numModDestinations() ? dst : 0;
        slot.amount = std::isfinite (amtValue) ? std::fmax (-1.0f, std::fmin (1.0f, amtValue * 0.01f)) : 0.0f;
    }
    return r;
}

bool routingParamsDiffer (const ParamValues& a, const ParamValues& b) noexcept
{
    // The matrix params are one contiguous block: src, dst, amt per slot.
    const auto first = (size_t) modSrc (0);
    const auto last  = (size_t) modAmt (kModSlots - 1);
    for (size_t i = first; i <= last; ++i)
        if (a[i] != b[i])   // NaN compares unequal, which only forces a (harmless) re-derive
            return true;
    return false;
}

void ModMatrix::apply (const ParamValues& base, const ModRouting& routing, const ModSourceValues& sources,
                       ParamValues& out, float* offsets) noexcept
{
    // Summed normalised offset per destination, plus the list of touched
    // destinations so the conversion below is O(slots), not O(destinations).
    std::array<float, kNumModDestinations + 1> sum {};
    std::array<int, kModSlots> touched {};
    int numTouched = 0;

    for (const auto& slot : routing.slots)
    {
        if (slot.src == ModSource::None || slot.src >= ModSource::Count || slot.dest <= 0 || slot.dest > kNumModDestinations)
            continue;
        const float contribution = slot.amount * sources.v[(size_t) slot.src];
        if (! std::isfinite (contribution))
            continue;

        auto& acc = sum[(size_t) slot.dest];
        bool seen = false;
        for (int t = 0; t < numTouched && ! seen; ++t)
            seen = touched[(size_t) t] == slot.dest;
        if (! seen)
            touched[(size_t) numTouched++] = slot.dest;
        acc += contribution;
    }

    if (&out != &base)
        out = base;

    if (offsets != nullptr)
        for (int d = 0; d <= kNumModDestinations; ++d)
            offsets[d] = sum[(size_t) d];

    for (int t = 0; t < numTouched; ++t)
    {
        const int d = touched[(size_t) t];
        const float off = sum[(size_t) d];
        const P p = modDestination (d);
        if (off == 0.0f || p == P::Count)
            continue;   // a zero net offset leaves the base value bit-exact
        // Each destination is read and written once, so `out` may alias `base`.
        const float n = toNormalised (p, base[(size_t) p]) + off;
        out[(size_t) p] = fromNormalised (p, n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n));
    }
}

} // namespace verdant
