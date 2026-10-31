#pragma once

// A complete Verdant sound: parameter values plus the non-parameter extras
// (names, tags, macro names, custom LFO shapes, user wavetables, living seed).
//
// Text format (line oriented, UTF-8, '\n' separated; unknown lines ignored):
//   verdant-patch 1
//   name <text> / category <text> / author <text> / tag <text> (one per tag)
//   macro <1..8> <text>
//   seed <uint32> / seedlock <0|1>
//   param <id> <value>          (every parameter; unknown ids ignored on load)
//   blob <key> <base64>         (tables, canvases, LFO shapes: added by W-F)
// Floats are printed like "%.9g" (bit-exact round trip) with a '.' decimal
// point whatever the C locale.
//
// Owner of serialize / deserialize: W-F. FOUNDATION: structs + a working
// serializer for everything except the blobs.

#include "Lfo.h"
#include "Params.h"
#include "SpectralPaint.h"
#include "Wavetable.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace verdant
{

struct UserTable
{
    std::string name;
    int numFrames = 0;
    std::vector<float> frames;                 // numFrames x kFrameSize, frame-major
    enum class Source : uint8_t { None, Imported, Painted, Matched } source = Source::None;
    PaintCanvas canvas;                        // only for Painted
};

struct Patch
{
    std::string name = "Init", category = "Init", author;
    std::vector<std::string> tags;
    ParamValues values = defaultValues();
    std::array<std::string, 8> macroNames { "Macro 1", "Macro 2", "Macro 3", "Macro 4", "Macro 5", "Macro 6", "Macro 7", "Macro 8" };
    std::array<LfoShape, 4> lfoShapes;
    std::array<UserTable, 2> userTables;
    uint32_t livingSeed = 0x5EED1234u;
    bool seedLocked = false;
};

Patch initPatch();
std::string serializePatch (const Patch&);
bool deserializePatch (const std::string&, Patch& out, std::string* error = nullptr);

} // namespace verdant
