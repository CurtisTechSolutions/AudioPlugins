// Patch text format: every field round trips (values and LFO shapes
// bit-exactly, int16 tables within 1/32768, uint8 canvases within half a
// step); unknown lines, blob keys and ids are ignored; missing params keep
// their defaults; garbage and malformed records are rejected.

#include "TestHarness.h"

#include "engine/Patch.h"

#include <ap/Math.h>

#include <algorithm>
#include <clocale>
#include <cmath>
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

//==============================================================================
// Extras (LFO shapes, user tables, canvases), escaping, malformed records.

namespace
{
    UserTable makeTable (const std::string& name, UserTable::Source source, int numFrames, float phase)
    {
        UserTable t;
        t.name = name;
        t.source = source;
        t.numFrames = numFrames;
        t.frames.resize ((size_t) numFrames * (size_t) kFrameSize);
        for (size_t i = 0; i < t.frames.size(); ++i)
        {
            const double x = (double) (i % (size_t) kFrameSize) / kFrameSize;
            const int f = (int) (i / (size_t) kFrameSize);
            t.frames[i] = (float) (0.9 * std::sin (6.283185307179586 * (f + 1) * x + phase) + 0.05 * std::sin (40.0 * x));
        }
        t.frames[3] = 1.0f;      // the extremes must survive too
        t.frames[4] = -1.0f;
        return t;
    }

    Patch fullPatch()
    {
        Patch p = randomPatch (1234u);
        p.name = "  \xE7\xB7\x91 Verdant \xE2\x86\x94 \xC3\xA9t\xC3\xA9 \\ back\\slash\nsecond line\r ";   // unicode, escapes, spaces
        p.category = "Pad";
        p.author = "A. Uthor \xF0\x9F\x8C\xBF";
        p.tags = { "warm", "two words", "\xC3\xBC" "ber" };
        p.macroNames[2] = "  spaced  ";
        p.macroNames[5] = "";
        p.lfoShapes[0].points = { { 0.0f, 0.1f, 0.0f }, { 0.3333333f, 0.9f, 0.75f }, { 1.0f, 0.2f, -0.123456789f } };
        p.lfoShapes[2].points = { { 0.0f, 1.0f, -1.0f }, { 1.0f, 0.0f, 1.0f } };
        // lfoShapes[1] and [3] stay default (not written)

        p.userTables[0] = makeTable ("Painted one", UserTable::Source::Painted, 3, 0.3f);
        auto& c = p.userTables[0].canvas;
        c.cols = 5;
        c.rows = 7;
        c.amp.resize (35);
        c.phase.resize (35);
        for (size_t i = 0; i < 35; ++i)
        {
            c.amp[i] = (float) ((i * 37) % 101) / 100.0f;
            c.phase[i] = (float) ((i * 13) % 50) / 49.0f;
        }
        p.userTables[1] = makeTable ("imported table.wav", UserTable::Source::Imported, 3, 1.1f);
        return p;
    }

    bool sameShape (const LfoShape& a, const LfoShape& b)
    {
        if (a.points.size() != b.points.size()) return false;
        for (size_t i = 0; i < a.points.size(); ++i)
            if (std::memcmp (&a.points[i], &b.points[i], sizeof (LfoShapePoint)) != 0) return false;
        return true;
    }

    float maxDiff (const std::vector<float>& a, const std::vector<float>& b)
    {
        if (a.size() != b.size()) return 1.0e9f;
        float m = 0.0f;
        for (size_t i = 0; i < a.size(); ++i) m = std::max (m, std::fabs (a[i] - b[i]));
        return m;
    }
}

AP_TEST (patch_full_round_trip_with_extras)
{
    const Patch p = fullPatch();
    const std::string text = serializePatch (p);

    // One line per record: no raw newline inside a field; only non-default extras are written.
    CHECK (text.find ("\nname   \xE7\xB7\x91") != std::string::npos);
    CHECK (text.find ("second line\\r \n") != std::string::npos);
    CHECK (text.find ("\nblob lfo1 ") != std::string::npos);
    CHECK (text.find ("\nblob lfo2 ") == std::string::npos);
    CHECK (text.find ("\nblob table1 ") != std::string::npos);
    CHECK (text.find ("\nblob canvas1.amp ") != std::string::npos);
    CHECK (text.find ("\nblob canvas1.phase ") != std::string::npos);
    CHECK (text.find ("\nblob canvas2.amp ") == std::string::npos);   // only painted tables carry a canvas

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
    for (size_t l = 0; l < 4; ++l)
        CHECK (sameShape (q.lfoShapes[l], p.lfoShapes[l]));

    for (size_t o = 0; o < 2; ++o)
    {
        const auto& a = p.userTables[o];
        const auto& b = q.userTables[o];
        CHECK (b.name == a.name);
        CHECK (b.source == a.source);
        CHECK_EQ (b.numFrames, 3);
        CHECK (b.frames.size() == a.frames.size());
        CHECK (maxDiff (a.frames, b.frames) <= 1.0f / 32768.0f);   // int16: lossy by design
    }
    CHECK (q.userTables[1].canvas.amp.empty());
    const auto& ca = p.userTables[0].canvas;
    const auto& cb = q.userTables[0].canvas;
    CHECK_EQ (cb.cols, ca.cols);
    CHECK_EQ (cb.rows, ca.rows);
    CHECK (maxDiff (ca.amp, cb.amp) <= 0.5f / 255.0f + 1.0e-6f);   // uint8: lossy by design
    CHECK (maxDiff (ca.phase, cb.phase) <= 0.5f / 255.0f + 1.0e-6f);

    // Quantised data re-encodes to the same text (no drift on re-save).
    CHECK (serializePatch (q) == text);
}

AP_TEST (patch_missing_params_and_extras_keep_defaults)
{
    Patch p;
    CHECK (deserializePatch ("verdant-patch 1\nparam master -3\n", p));
    CHECK_EQ (p.values[(size_t) P::Master], -3.0f);
    const ParamValues defaults = defaultValues();
    int differing = 0;
    for (int i = 0; i < kNumParams; ++i)
        if (p.values[(size_t) i] != defaults[(size_t) i]) ++differing;
    CHECK_EQ (differing, 1);
    CHECK (p.name == "Init");
    CHECK (p.tags.empty());
    CHECK_EQ (p.livingSeed, 0x5EED1234u);
    CHECK (sameShape (p.lfoShapes[0], LfoShape()));
    CHECK_EQ (p.userTables[0].numFrames, 0);
    CHECK (p.userTables[0].source == UserTable::Source::None);
}

AP_TEST (patch_skips_unknown_records_with_extras)
{
    const Patch p = fullPatch();
    std::string text = serializePatch (p);
    text.insert (text.find ("\nparam "), "\nfuture-record 1 2 3\nblob future.key QUJD\nblob lfo9 AAAA");
    text += "zzz trailing unknown\n";
    Patch q;
    std::string error;
    CHECK (deserializePatch (text, q, &error));
    CHECK (sameValues (q.values, p.values));
    CHECK (sameShape (q.lfoShapes[0], p.lfoShapes[0]));
    CHECK_EQ (q.userTables[0].numFrames, 3);
}

AP_TEST (patch_rejects_malformed_records)
{
    const std::string good = serializePatch (fullPatch());
    auto rejects = [] (const std::string& text)
    {
        Patch q;
        q.name = "untouched";
        std::string error;
        const bool ok = deserializePatch (text, q, &error);
        return ! ok && ! error.empty() && q.name == "untouched";
    };

    CHECK (rejects ("verdant-patch 1\nparam master loud\n"));
    CHECK (rejects ("verdant-patch 1\nparam master\n"));
    CHECK (rejects ("verdant-patch 1\nmacro 9 Too Far\n"));
    CHECK (rejects ("verdant-patch 1\nseed -5\n"));
    CHECK (rejects ("verdant-patch 1\nblob lfo1 A*BC\n"));                // bad base64 character
    CHECK (rejects ("verdant-patch 1\nblob lfo1 AAA\n"));                 // bad base64 length
    CHECK (rejects ("verdant-patch 1\nblob lfo1 AAAAAAAA\n"));            // 6 bytes: not whole points
    CHECK (rejects ("verdant-patch 1\nblob table1 AAAA\n"));              // not a whole frame
    CHECK (rejects ("verdant-patch 1\nusertable 3 1 x\n"));
    CHECK (rejects ("verdant-patch 1\ncanvas 1 2 2\nblob canvas1.amp AAAA\n"));   // 3 bytes for 4 cells
    CHECK (rejects ("verdant-patch 1\nblob canvas1.amp AAAA\n"));         // no canvas header
    CHECK (rejects ("verdant-patch\n" + good.substr (good.find ('\n') + 1)));
    CHECK (rejects ("verdant-patch 0\nname x\n"));

    // A non-finite value in memory is saved as its clamped stand-in, so the file always loads back.
    Patch nan;
    nan.values[(size_t) P::Glide] = std::nanf ("");
    nan.values[(size_t) P::Master] = INFINITY;
    Patch back;
    CHECK (deserializePatch (serializePatch (nan), back));
    CHECK_EQ (back.values[(size_t) P::Glide], info (P::Glide).def);
    CHECK_EQ (back.values[(size_t) P::Master], info (P::Master).max);

    // A clipped table blob (e.g. a truncated file) is caught rather than loading a short table.
    const auto at = good.find ("\nblob table1 ");
    const auto end = good.find ('\n', at + 1);
    std::string clipped = good;
    clipped.erase (end - 8, 8);
    CHECK (rejects (clipped));
}
