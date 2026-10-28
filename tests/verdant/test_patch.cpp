// Patch text format (foundation part): every field except the blobs round
// trips, values bit-exactly; unknown lines and ids are ignored; garbage is
// rejected.

#include "TestHarness.h"

#include "engine/Patch.h"

#include <ap/Math.h>

#include <clocale>
#include <cstring>
#include <string>

using namespace verdant;

namespace
{
    Patch randomPatch (uint32_t seed)
    {
        ap::Rng rng;
        rng.state = seed;
        Patch p;
        p.name = "Glass Bloom \xC3\x97 Night";
        p.category = "Texture";
        p.author = "Test Author";
        p.tags = { "evolving", "two words", "dark" };
        for (size_t m = 0; m < p.macroNames.size(); ++m) p.macroNames[m] = "Knob " + std::to_string (m * 7);
        p.livingSeed = 0xDEADBEEFu;
        p.seedLocked = true;
        for (int i = 0; i < kNumParams; ++i)
        {
            const auto& in = info ((P) i);
            const float v = in.min + (in.max - in.min) * rng.nextUnipolar();
            p.values[(size_t) i] = in.kind == ParamKind::Float ? v : clampToRange ((P) i, v);
        }
        return p;
    }

    bool sameValues (const ParamValues& a, const ParamValues& b)
    {
        return std::memcmp (a.data(), b.data(), sizeof (float) * a.size()) == 0;
    }
}

AP_TEST (patch_round_trip_bit_exact)
{
    for (uint32_t seed : { 1u, 2u, 77u })
    {
        const Patch p = randomPatch (seed);
        const std::string text = serializePatch (p);
        CHECK (text.rfind ("verdant-patch 1\n", 0) == 0);

        Patch q;
        std::string error;
        CHECK (deserializePatch (text, q, &error));
        CHECK (error.empty());
        CHECK (q.name == p.name);
        CHECK (q.category == p.category);
        CHECK (q.author == p.author);
        CHECK (q.tags == p.tags);
        CHECK (q.macroNames == p.macroNames);
        CHECK_EQ (q.livingSeed, p.livingSeed);
        CHECK (q.seedLocked == p.seedLocked);
        CHECK (sameValues (q.values, p.values));
        CHECK (serializePatch (q) == text);
    }

    const Patch init = initPatch();
    Patch back;
    CHECK (deserializePatch (serializePatch (init), back));
    CHECK (back.name == "Init" && back.category == "Init");
    CHECK (sameValues (back.values, defaultValues()));
}

AP_TEST (patch_ignores_unknown_and_clamps)
{
    Patch p;
    CHECK (deserializePatch ("\nverdant-patch 1\r\nname Hello\r\nparam no_such_id 5\nparam master -12\nparam voices 99\nblob x AAAA\nwhatever else\n", p));
    CHECK (p.name == "Hello");
    CHECK_EQ (p.values[(size_t) P::Master], -12.0f);
    CHECK_EQ (p.values[(size_t) P::Voices], 32.0f);
    CHECK_EQ (p.values[(size_t) P::Glide], info (P::Glide).def);
    CHECK (p.category == "Init");
}

AP_TEST (patch_rejects_garbage)
{
    Patch p;
    p.name = "untouched";
    std::string error;
    CHECK (! deserializePatch ("hello world\nparam master 0\n", p, &error));
    CHECK (! error.empty());
    CHECK (p.name == "untouched");
    CHECK (! deserializePatch ("", p));
}

AP_TEST (patch_text_is_locale_independent)
{
    // A host may switch the C locale to one with a decimal comma; the format must not change.
    const char* previous = std::setlocale (LC_NUMERIC, nullptr);
    const std::string saved = previous != nullptr ? previous : "C";
    const bool switched = std::setlocale (LC_NUMERIC, "de_DE.UTF-8") != nullptr;

    Patch p;
    p.values[(size_t) P::Glide] = 12.5f;
    const std::string text = serializePatch (p);
    CHECK (text.find ("param glide 12.5\n") != std::string::npos);
    Patch q;
    CHECK (deserializePatch (text, q));
    CHECK_EQ (q.values[(size_t) P::Glide], 12.5f);

    std::setlocale (LC_NUMERIC, saved.c_str());
    (void) switched;
}
