#include "Patch.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <locale>
#include <optional>
#include <sstream>

// Patch text format, version 1 (the header comment in Patch.h is the summary;
// this is the full record list, in the order serializePatch writes them):
//
//   verdant-patch 1
//   name <text>                      category <text>          author <text>
//   tag <text>                       (one line per tag, in order; empty tags are dropped)
//   macro <1..8> <text>
//   seed <uint32>                    seedlock <0|1>
//   param <id> <value>               (every parameter, table order, "%.9g", classic locale)
//   blob lfo<1..4> <base64>          (only when the shape differs from the default LfoShape:
//                                     float32 little-endian x, y, curve per point -> bit exact)
//   usertable <1..2> <source 0..3> <name text>   (when the table has frames, a source or a name)
//   blob table<1..2> <base64>        (only when numFrames > 0: numFrames x kFrameSize int16 LE,
//                                     sample x 32767 rounded and clamped; the frame count is
//                                     the byte length / (2 x kFrameSize))
//   canvas <1..2> <cols> <rows>      (only for Painted tables with a complete canvas)
//   blob canvas<1..2>.amp <base64>   (cols x rows uint8, amp x 255 rounded, same layout as PaintCanvas)
//   blob canvas<1..2>.phase <base64> (only when the canvas has a phase plane)
//
// <text> is the rest of the line after one space, so leading and trailing
// spaces survive; '\' , newline and carriage return are escaped as "\\",
// "\n" and "\r". Loading: the first non-empty line must be the header; '\r\n'
// endings are accepted; unknown record types, unknown blob keys and unknown
// parameter ids are skipped; missing params keep their defaults; a known
// record with a malformed payload rejects the whole text (out is untouched).

namespace verdant
{

namespace
{
    //==============================================================================
    // Numbers

    bool parseFloat (const std::string& s, float& v)
    {
        if (s.empty()) return false;
        std::istringstream in (s);
        in.imbue (std::locale::classic());
        in >> v;
        if (in.fail()) return false;
        in >> std::ws;
        return in.eof() && std::isfinite (v);
    }

    bool parseInt (const std::string& s, long long& v)
    {
        if (s.empty()) return false;
        char* end = nullptr;
        v = std::strtoll (s.c_str(), &end, 10);
        return end != nullptr && *end == '\0';
    }

    // "%.9g" in the classic locale: enough digits for every float to read back bit-exactly.
    std::string formatFloat (float v)
    {
        std::ostringstream out;
        out.imbue (std::locale::classic());
        out.precision (9);
        out << v;
        return out.str();
    }

    //==============================================================================
    // Text fields

    std::string escapeText (const std::string& s)
    {
        std::string out;
        out.reserve (s.size());
        for (const char c : s)
        {
            if (c == '\\')      out += "\\\\";
            else if (c == '\n') out += "\\n";
            else if (c == '\r') out += "\\r";
            else                out += c;
        }
        return out;
    }

    std::string unescapeText (const std::string& s)
    {
        std::string out;
        out.reserve (s.size());
        for (size_t i = 0; i < s.size(); ++i)
        {
            if (s[i] == '\\' && i + 1 < s.size())
            {
                const char n = s[++i];
                out += n == 'n' ? '\n' : (n == 'r' ? '\r' : n);   // "\\" -> '\', unknown escapes keep the char
            }
            else
            {
                out += s[i];
            }
        }
        return out;
    }

    /** Splits "key rest of line" at the first space. */
    void splitKey (const std::string& line, std::string& key, std::string& rest)
    {
        const auto sp = line.find (' ');
        key = line.substr (0, sp);
        rest = sp == std::string::npos ? std::string() : line.substr (sp + 1);
    }

    //==============================================================================
    // Base64 (RFC 4648 alphabet, '=' padding)

    constexpr char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string base64Encode (const uint8_t* data, size_t n)
    {
        std::string out;
        out.reserve ((n + 2) / 3 * 4);
        size_t i = 0;
        for (; i + 2 < n; i += 3)
        {
            const uint32_t v = ((uint32_t) data[i] << 16) | ((uint32_t) data[i + 1] << 8) | data[i + 2];
            out += kB64[(v >> 18) & 63];
            out += kB64[(v >> 12) & 63];
            out += kB64[(v >> 6) & 63];
            out += kB64[v & 63];
        }
        if (i < n)
        {
            const bool two = i + 1 < n;
            const uint32_t v = ((uint32_t) data[i] << 16) | (two ? (uint32_t) data[i + 1] << 8 : 0u);
            out += kB64[(v >> 18) & 63];
            out += kB64[(v >> 12) & 63];
            out += two ? kB64[(v >> 6) & 63] : '=';
            out += '=';
        }
        return out;
    }

    int base64Value (char c)
    {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    }

    /** Strict decoder: length a multiple of 4, padding only at the end. Empty input decodes to nothing. */
    bool base64Decode (const std::string& s, std::vector<uint8_t>& out)
    {
        out.clear();
        if (s.size() % 4 != 0) return false;
        out.reserve (s.size() / 4 * 3);
        for (size_t i = 0; i < s.size(); i += 4)
        {
            const bool last = i + 4 == s.size();
            int v[4];
            int pad = 0;
            for (int k = 0; k < 4; ++k)
            {
                const char c = s[i + (size_t) k];
                if (c == '=' && last && k >= 2)
                {
                    v[k] = 0;
                    ++pad;
                    continue;
                }
                if (pad > 0) return false;   // data after padding
                v[k] = base64Value (c);
                if (v[k] < 0) return false;
            }
            const uint32_t bits = ((uint32_t) v[0] << 18) | ((uint32_t) v[1] << 12) | ((uint32_t) v[2] << 6) | (uint32_t) v[3];
            out.push_back ((uint8_t) (bits >> 16));
            if (pad < 2) out.push_back ((uint8_t) (bits >> 8));
            if (pad < 1) out.push_back ((uint8_t) bits);
        }
        return true;
    }

    //==============================================================================
    // Binary payloads

    std::string encodeLfoShape (const LfoShape& shape)
    {
        std::vector<uint8_t> bytes;
        bytes.reserve (shape.points.size() * 12);
        auto putFloat = [&bytes] (float f)
        {
            uint32_t u;
            std::memcpy (&u, &f, sizeof (u));
            for (int b = 0; b < 4; ++b) bytes.push_back ((uint8_t) (u >> (8 * b)));
        };
        for (const auto& pt : shape.points)
        {
            putFloat (pt.x);
            putFloat (pt.y);
            putFloat (pt.curve);
        }
        return base64Encode (bytes.data(), bytes.size());
    }

    bool decodeLfoShape (const std::vector<uint8_t>& bytes, LfoShape& shape)
    {
        if (bytes.size() % 12 != 0) return false;
        auto getFloat = [&bytes] (size_t at)
        {
            uint32_t u = 0;
            for (int b = 0; b < 4; ++b) u |= (uint32_t) bytes[at + (size_t) b] << (8 * b);
            float f;
            std::memcpy (&f, &u, sizeof (f));
            return f;
        };
        shape.points.clear();
        for (size_t at = 0; at < bytes.size(); at += 12)
        {
            const LfoShapePoint pt { getFloat (at), getFloat (at + 4), getFloat (at + 8) };
            if (! std::isfinite (pt.x) || ! std::isfinite (pt.y) || ! std::isfinite (pt.curve)) return false;
            shape.points.push_back (pt);
        }
        return true;
    }

    bool sameShape (const LfoShape& a, const LfoShape& b)
    {
        if (a.points.size() != b.points.size()) return false;
        for (size_t i = 0; i < a.points.size(); ++i)
            if (std::memcmp (&a.points[i], &b.points[i], sizeof (LfoShapePoint)) != 0)
                return false;
        return true;
    }

    std::string encodeFrames (const UserTable& t)
    {
        const size_t count = (size_t) t.numFrames * (size_t) kFrameSize;
        std::vector<uint8_t> bytes (count * 2);
        for (size_t i = 0; i < count; ++i)
        {
            const float x = std::isfinite (t.frames[i]) ? t.frames[i] : 0.0f;
            const auto q = (int16_t) std::clamp (std::lround ((double) x * 32767.0), -32767L, 32767L);
            const auto u = (uint16_t) q;
            bytes[2 * i]     = (uint8_t) (u & 0xFF);
            bytes[2 * i + 1] = (uint8_t) (u >> 8);
        }
        return base64Encode (bytes.data(), bytes.size());
    }

    std::string encodePlane (const std::vector<float>& plane)
    {
        std::vector<uint8_t> bytes (plane.size());
        for (size_t i = 0; i < plane.size(); ++i)
        {
            const float x = std::isfinite (plane[i]) ? plane[i] : 0.0f;
            bytes[i] = (uint8_t) std::clamp (std::lround ((double) x * 255.0), 0L, 255L);
        }
        return base64Encode (bytes.data(), bytes.size());
    }

    std::vector<float> decodePlane (const std::vector<uint8_t>& bytes)
    {
        std::vector<float> plane (bytes.size());
        for (size_t i = 0; i < bytes.size(); ++i)
            plane[i] = (float) bytes[i] / 255.0f;
        return plane;
    }

    bool hasFrames (const UserTable& t)
    {
        return t.numFrames > 0 && t.frames.size() >= (size_t) t.numFrames * (size_t) kFrameSize;
    }

    bool hasCanvas (const UserTable& t)
    {
        const auto& c = t.canvas;
        const size_t cells = (size_t) std::max (0, c.cols) * (size_t) std::max (0, c.rows);
        return t.source == UserTable::Source::Painted && cells > 0 && c.amp.size() == cells
            && (c.phase.empty() || c.phase.size() == cells);
    }

    /** "lfo3" -> 2 for prefix "lfo" and count 4; -1 when it does not match. */
    int indexedKey (const std::string& key, const char* prefix, int count)
    {
        const size_t len = std::strlen (prefix);
        if (key.size() != len + 1 || key.compare (0, len, prefix) != 0) return -1;
        const int i = key[len] - '1';
        return i >= 0 && i < count ? i : -1;
    }

    /** Per-oscillator pieces collected while parsing, assembled once every line is read (order independent). */
    struct PendingTable
    {
        bool hasHeader = false, hasCanvasHeader = false;
        int source = 0;
        std::string name;
        std::optional<std::vector<uint8_t>> frames, amp, phase;
        int cols = 0, rows = 0;
    };
}

Patch initPatch()
{
    return Patch();
}

std::string serializePatch (const Patch& p)
{
    std::string s;
    s.reserve (16384);
    s += "verdant-patch 1\n";
    s += "name " + escapeText (p.name) + "\n";
    s += "category " + escapeText (p.category) + "\n";
    s += "author " + escapeText (p.author) + "\n";
    for (const auto& t : p.tags)
        if (! t.empty())
            s += "tag " + escapeText (t) + "\n";
    for (size_t m = 0; m < p.macroNames.size(); ++m)
        s += "macro " + std::to_string (m + 1) + " " + escapeText (p.macroNames[m]) + "\n";
    s += "seed " + std::to_string (p.livingSeed) + "\n";
    s += std::string ("seedlock ") + (p.seedLocked ? "1" : "0") + "\n";
    for (int i = 0; i < kNumParams; ++i)
    {
        const float v = p.values[(size_t) i];   // a non-finite value would not load back: write its clamped stand-in
        s += std::string ("param ") + info ((P) i).id + " " + formatFloat (std::isfinite (v) ? v : clampToRange ((P) i, v)) + "\n";
    }

    const LfoShape defaultShape;
    for (size_t l = 0; l < p.lfoShapes.size(); ++l)
        if (! sameShape (p.lfoShapes[l], defaultShape))
            s += "blob lfo" + std::to_string (l + 1) + " " + encodeLfoShape (p.lfoShapes[l]) + "\n";

    for (size_t o = 0; o < p.userTables.size(); ++o)
    {
        const auto& t = p.userTables[o];
        const std::string n = std::to_string (o + 1);
        const bool frames = hasFrames (t);
        if (frames || t.source != UserTable::Source::None || ! t.name.empty())
            s += "usertable " + n + " " + std::to_string ((int) t.source) + " " + escapeText (t.name) + "\n";
        if (frames)
            s += "blob table" + n + " " + encodeFrames (t) + "\n";
        if (hasCanvas (t))
        {
            s += "canvas " + n + " " + std::to_string (t.canvas.cols) + " " + std::to_string (t.canvas.rows) + "\n";
            s += "blob canvas" + n + ".amp " + encodePlane (t.canvas.amp) + "\n";
            if (! t.canvas.phase.empty())
                s += "blob canvas" + n + ".phase " + encodePlane (t.canvas.phase) + "\n";
        }
    }
    return s;
}

bool deserializePatch (const std::string& text, Patch& out, std::string* error)
{
    std::istringstream in (text);
    std::string line, key, rest;
    int lineNumber = 0;

    auto fail = [&] (const std::string& why)
    {
        if (error != nullptr)
            *error = lineNumber > 0 ? "line " + std::to_string (lineNumber) + ": " + why : why;
        return false;
    };

    // Header: first non-empty line.
    bool headerOk = false;
    while (std::getline (in, line))
    {
        ++lineNumber;
        if (! line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        splitKey (line, key, rest);
        long long version = 0;
        headerOk = key == "verdant-patch" && parseInt (rest, version) && version >= 1;
        break;
    }
    if (! headerOk)
        return fail ("not a Verdant patch (missing 'verdant-patch' header)");

    Patch p;
    std::array<PendingTable, 2> pending;

    while (std::getline (in, line))
    {
        ++lineNumber;
        if (! line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        splitKey (line, key, rest);

        if (key == "name")            p.name = unescapeText (rest);
        else if (key == "category")   p.category = unescapeText (rest);
        else if (key == "author")     p.author = unescapeText (rest);
        else if (key == "tag")        { if (! rest.empty()) p.tags.push_back (unescapeText (rest)); }
        else if (key == "macro")
        {
            std::string num, name;
            splitKey (rest, num, name);
            long long m = 0;
            if (! parseInt (num, m) || m < 1 || m > (long long) p.macroNames.size())
                return fail ("bad macro index '" + num + "'");
            p.macroNames[(size_t) m - 1] = unescapeText (name);
        }
        else if (key == "seed")
        {
            long long v = 0;
            if (! parseInt (rest, v) || v < 0 || v > 0xFFFFFFFFLL)
                return fail ("bad seed '" + rest + "'");
            p.livingSeed = (uint32_t) v;
        }
        else if (key == "seedlock")
        {
            long long v = 0;
            if (! parseInt (rest, v))
                return fail ("bad seedlock '" + rest + "'");
            p.seedLocked = v != 0;
        }
        else if (key == "param")
        {
            std::string id, value;
            splitKey (rest, id, value);
            float v = 0.0f;
            if (id.empty() || ! parseFloat (value, v))
                return fail ("bad param line '" + rest + "'");
            const int idx = indexOf (id.c_str());
            if (idx >= 0)
                p.values[(size_t) idx] = clampToRange ((P) idx, v);
        }
        else if (key == "usertable")
        {
            std::string num, tail, src, name;
            splitKey (rest, num, tail);
            splitKey (tail, src, name);
            long long o = 0, s = 0;
            if (! parseInt (num, o) || o < 1 || o > 2 || ! parseInt (src, s) || s < 0 || s > (long long) UserTable::Source::Matched)
                return fail ("bad usertable line '" + rest + "'");
            auto& t = pending[(size_t) o - 1];
            t.hasHeader = true;
            t.source = (int) s;
            t.name = unescapeText (name);
        }
        else if (key == "canvas")
        {
            std::istringstream fields (rest);
            fields.imbue (std::locale::classic());
            long long o = 0, cols = 0, rows = 0;
            fields >> o >> cols >> rows;
            if (fields.fail() || o < 1 || o > 2 || cols < 1 || rows < 1 || cols > 4096 || rows > 4096)
                return fail ("bad canvas line '" + rest + "'");
            auto& t = pending[(size_t) o - 1];
            t.hasCanvasHeader = true;
            t.cols = (int) cols;
            t.rows = (int) rows;
        }
        else if (key == "blob")
        {
            std::string blobKey, payload;
            splitKey (rest, blobKey, payload);
            const int lfo = indexedKey (blobKey, "lfo", 4);
            const int table = indexedKey (blobKey, "table", 2);
            int canvasAmp = -1, canvasPhase = -1;
            if (blobKey.size() > 7 && blobKey.compare (0, 6, "canvas") == 0)
            {
                const std::string head = blobKey.substr (0, 7), tail = blobKey.substr (7);
                const int o = indexedKey (head, "canvas", 2);
                if (tail == ".amp")   canvasAmp = o;
                if (tail == ".phase") canvasPhase = o;
            }
            if (lfo < 0 && table < 0 && canvasAmp < 0 && canvasPhase < 0)
                continue;   // a blob this version does not know

            std::vector<uint8_t> bytes;
            if (! base64Decode (payload, bytes))
                return fail ("bad base64 in blob '" + blobKey + "'");

            if (lfo >= 0)
            {
                if (! decodeLfoShape (bytes, p.lfoShapes[(size_t) lfo]))
                    return fail ("bad LFO shape blob '" + blobKey + "'");
            }
            else if (table >= 0)
            {
                const size_t frameBytes = 2 * (size_t) kFrameSize;
                if (bytes.empty() || bytes.size() % frameBytes != 0 || bytes.size() / frameBytes > (size_t) kMaxFrames)
                    return fail ("bad table blob '" + blobKey + "' (" + std::to_string (bytes.size()) + " bytes)");
                pending[(size_t) table].frames = std::move (bytes);
            }
            else if (canvasAmp >= 0)
            {
                pending[(size_t) canvasAmp].amp = std::move (bytes);
            }
            else
            {
                pending[(size_t) canvasPhase].phase = std::move (bytes);
            }
        }
        // Anything else: a record type this version does not know; skipped.
    }

    // Assemble the user tables.
    lineNumber = 0;
    for (size_t o = 0; o < pending.size(); ++o)
    {
        auto& src = pending[o];
        auto& t = p.userTables[o];
        if (src.hasHeader)
        {
            t.source = (UserTable::Source) src.source;
            t.name = src.name;
        }
        if (src.frames)
        {
            const auto& bytes = *src.frames;
            t.numFrames = (int) (bytes.size() / (2 * (size_t) kFrameSize));
            t.frames.resize (bytes.size() / 2);
            for (size_t i = 0; i < t.frames.size(); ++i)
            {
                const auto u = (uint16_t) (bytes[2 * i] | (bytes[2 * i + 1] << 8));
                t.frames[i] = (float) (int16_t) u / 32767.0f;
            }
        }
        if (src.hasCanvasHeader || src.amp || src.phase)
        {
            const size_t cells = (size_t) src.cols * (size_t) src.rows;
            if (! src.hasCanvasHeader || ! src.amp || src.amp->size() != cells || (src.phase && src.phase->size() != cells))
                return fail ("canvas " + std::to_string (o + 1) + " is incomplete or its size does not match");
            t.canvas.cols = src.cols;
            t.canvas.rows = src.rows;
            t.canvas.amp = decodePlane (*src.amp);
            t.canvas.phase = src.phase ? decodePlane (*src.phase) : std::vector<float>();
            if (t.source == UserTable::Source::None)
                t.source = UserTable::Source::Painted;   // canvases are only written for painted tables
        }
    }

    out = std::move (p);
    if (error != nullptr) error->clear();
    return true;
}

} // namespace verdant
