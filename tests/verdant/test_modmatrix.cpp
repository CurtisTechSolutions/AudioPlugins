// Mod matrix (CONTRACT §4): routingFrom reads the 24 slots, apply() adds
// amount x source in the normalised (host knob) domain, clamps at both ends,
// sums slots that share a destination, reports the offsets, ignores None
// slots and non-finite sources, may run in place, and is cheap enough to run
// per voice per control block.

#include "TestHarness.h"

#include "engine/ModMatrix.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace verdant;

namespace
{
    const P kCutoff = filterParam (0, FilterParam::Cutoff);

    int dest (P p) { return modDestinationIndexOf (p); }

    ModRouting oneSlot (ModSource src, P target, float amount)
    {
        ModRouting r;
        r.slots[0] = ModRoute { src, dest (target), amount };
        return r;
    }

    bool sameValues (const ParamValues& a, const ParamValues& b)
    {
        for (size_t i = 0; i < a.size(); ++i)
            if (! (a[i] == b[i])) return false;
        return true;
    }
}

AP_TEST (modmatrix_routing_from_reads_slots)
{
    auto v = defaultValues();
    v[(size_t) modSrc (3)] = (float) ModSource::Lfo2;
    v[(size_t) modDst (3)] = (float) dest (kCutoff);
    v[(size_t) modAmt (3)] = -50.0f;
    v[(size_t) modSrc (23)] = (float) ModSource::SeqMod;
    v[(size_t) modDst (23)] = (float) dest (P::Master);
    v[(size_t) modAmt (23)] = 100.0f;
    v[(size_t) modSrc (5)] = 99.0f;   // out of range -> None
    v[(size_t) modDst (5)] = (float) dest (kCutoff);

    const auto r = routingFrom (v);
    CHECK (r.slots[3].src == ModSource::Lfo2);
    CHECK_EQ (r.slots[3].dest, dest (kCutoff));
    CHECK_NEAR (r.slots[3].amount, -0.5, 1.0e-7);
    CHECK (r.slots[23].src == ModSource::SeqMod);
    CHECK_EQ (r.slots[23].dest, dest (P::Master));
    CHECK_NEAR (r.slots[23].amount, 1.0, 1.0e-7);
    CHECK (r.slots[5].src == ModSource::None);
    CHECK (r.slots[0].src == ModSource::None);
    CHECK_EQ (r.slots[0].dest, 0);

    // routingParamsDiffer sees matrix edits only.
    auto w = v;
    CHECK (! routingParamsDiffer (v, w));
    w[(size_t) P::Master] = 3.0f;
    CHECK (! routingParamsDiffer (v, w));
    w[(size_t) modAmt (11)] = 1.0f;
    CHECK (routingParamsDiffer (v, w));
}

AP_TEST (modmatrix_apply_adds_in_normalised_domain)
{
    auto base = defaultValues();
    base[(size_t) kCutoff] = 1000.0f;
    ModSourceValues s;
    s.v[(size_t) ModSource::Macro1] = 1.0f;

    ParamValues out {};
    std::vector<float> offsets ((size_t) numModDestinations() + 1, 123.0f);
    ModMatrix::apply (base, oneSlot (ModSource::Macro1, kCutoff, 0.5f), s, out, offsets.data());

    const float expected = fromNormalised (kCutoff, 0.5f + toNormalised (kCutoff, 1000.0f));
    CHECK_NEAR (out[(size_t) kCutoff], expected, expected * 1.0e-5);
    CHECK (out[(size_t) kCutoff] > 1000.0f);

    // Offsets: the destination gets 0.5, everything else (incl. index 0) is cleared.
    CHECK_NEAR (offsets[(size_t) dest (kCutoff)], 0.5, 1.0e-7);
    bool othersZero = true;
    for (int d = 0; d <= numModDestinations(); ++d)
        if (d != dest (kCutoff) && offsets[(size_t) d] != 0.0f) othersZero = false;
    CHECK (othersZero);

    // Untouched params are bit-identical to the base.
    auto expectedOut = base;
    expectedOut[(size_t) kCutoff] = out[(size_t) kCutoff];
    CHECK (sameValues (out, expectedOut));
}

AP_TEST (modmatrix_apply_clamps_both_ends)
{
    const auto base = defaultValues();   // cutoff 12 kHz
    ModSourceValues s;
    s.v[(size_t) ModSource::Velocity] = 1.0f;
    ParamValues out {};

    ModMatrix::apply (base, oneSlot (ModSource::Velocity, kCutoff, 1.0f), s, out, nullptr);
    CHECK_NEAR (out[(size_t) kCutoff], info (kCutoff).max, 1.0e-3);

    ModMatrix::apply (base, oneSlot (ModSource::Velocity, kCutoff, -1.0f), s, out, nullptr);
    CHECK_NEAR (out[(size_t) kCutoff], info (kCutoff).min, 1.0e-3);

    // A bipolar source going negative on a linear param.
    const P pan = oscParam (0, OscParam::Pan);
    s.v[(size_t) ModSource::Lfo1] = -1.0f;
    ModMatrix::apply (base, oneSlot (ModSource::Lfo1, pan, 1.0f), s, out, nullptr);
    CHECK_NEAR (out[(size_t) pan], -100.0, 1.0e-3);
    s.v[(size_t) ModSource::Lfo1] = 0.25f;
    ModMatrix::apply (base, oneSlot (ModSource::Lfo1, pan, 1.0f), s, out, nullptr);
    CHECK_NEAR (out[(size_t) pan], 50.0, 1.0e-3);   // n 0.5 + 0.25 -> 0.75 -> +50 %
}

AP_TEST (modmatrix_slots_on_one_destination_sum)
{
    const auto base = defaultValues();
    const P level = oscParam (0, OscParam::Level);   // 80 %, linear 0..100
    ModSourceValues s;
    s.v[(size_t) ModSource::Macro1] = 1.0f;
    s.v[(size_t) ModSource::Macro2] = 0.5f;

    ModRouting r;
    r.slots[2] = ModRoute { ModSource::Macro1, dest (level), -0.25f };
    r.slots[9] = ModRoute { ModSource::Macro2, dest (level), -0.5f };
    ParamValues out {};
    std::vector<float> offsets ((size_t) numModDestinations() + 1, 0.0f);
    ModMatrix::apply (base, r, s, out, offsets.data());
    CHECK_NEAR (offsets[(size_t) dest (level)], -0.5, 1.0e-6);
    CHECK_NEAR (out[(size_t) level], 30.0, 1.0e-3);   // 0.8 - 0.25 - 0.25 = 0.3

    // Opposite slots cancel exactly: the base value survives bit-exactly.
    r.slots[9] = ModRoute { ModSource::Macro1, dest (level), 0.25f };
    ModMatrix::apply (base, r, s, out, offsets.data());
    CHECK_EQ (out[(size_t) level], base[(size_t) level]);
}

AP_TEST (modmatrix_none_slots_and_bad_sources_are_ignored)
{
    const auto base = defaultValues();
    ModSourceValues s;
    for (auto& x : s.v) x = 1.0f;
    s.v[(size_t) ModSource::Lfo3] = std::nanf ("");

    ModRouting r;
    r.slots[0] = ModRoute { ModSource::None, dest (kCutoff), 1.0f };      // no source
    r.slots[1] = ModRoute { ModSource::Macro1, 0, 1.0f };                 // no destination
    r.slots[2] = ModRoute { ModSource::Macro1, 999, 1.0f };               // out of range
    r.slots[3] = ModRoute { ModSource::Lfo3, dest (kCutoff), 1.0f };      // NaN source
    ParamValues out {};
    std::vector<float> offsets ((size_t) numModDestinations() + 1, 7.0f);
    ModMatrix::apply (base, r, s, out, offsets.data());
    CHECK (sameValues (out, base));
    bool allZero = true;
    for (float o : offsets) allZero = allZero && o == 0.0f;
    CHECK (allZero);
}

AP_TEST (modmatrix_apply_in_place)
{
    auto base = defaultValues();
    base[(size_t) kCutoff] = 1000.0f;
    ModSourceValues s;
    s.v[(size_t) ModSource::Env2] = 0.8f;
    const auto r = oneSlot (ModSource::Env2, kCutoff, 0.5f);

    ParamValues separate {};
    ModMatrix::apply (base, r, s, separate, nullptr);
    auto inPlace = base;
    ModMatrix::apply (inPlace, r, s, inPlace, nullptr);
    CHECK (sameValues (separate, inPlace));
}

AP_TEST (modmatrix_apply_is_cheap)
{
    // All 24 slots active on distinct destinations: 10k applies (the voice rate of
    // ~3 voice-seconds at 48 kHz) must be quick and touch no heap (apply is noexcept
    // and works on stack arrays only).
    const auto base = defaultValues();
    ModRouting r;
    for (int slot = 0; slot < kModSlots; ++slot)
        r.slots[(size_t) slot] = ModRoute { (ModSource) (1 + slot % ((int) ModSource::Count - 1)), 1 + slot * 2, 0.1f };
    ModSourceValues s;
    for (size_t i = 0; i < s.v.size(); ++i) s.v[i] = 0.5f;

    ParamValues out {};
    std::vector<float> offsets ((size_t) numModDestinations() + 1, 0.0f);
    static_assert (noexcept (ModMatrix::apply (base, r, s, out, offsets.data())), "apply must be noexcept");

    const auto t0 = std::chrono::steady_clock::now();
    double guard = 0.0;
    for (int i = 0; i < 10000; ++i)
    {
        s.v[1] = (float) (i % 100) * 0.01f;
        ModMatrix::apply (base, r, s, out, offsets.data());
        guard += out[(size_t) modDestination (1)];
    }
    const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("  modmatrix: 10k applies x 24 slots in %.2f ms\n", ms);
    CHECK (std::isfinite (guard));
    CHECK (ms < 1000.0);
#ifdef NDEBUG
    CHECK (ms < 100.0);
#endif
}
