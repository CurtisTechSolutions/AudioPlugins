#include "Patch.h"

#include <cstdlib>
#include <locale>
#include <sstream>

namespace verdant
{

namespace
{
    float parseFloat (const std::string& s, bool& ok)
    {
        std::istringstream in (s);
        in.imbue (std::locale::classic());
        float v = 0.0f;
        in >> v;
        ok = ! in.fail();
        return v;
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

    std::string oneLine (std::string s)
    {
        for (auto& c : s)
            if (c == '\n' || c == '\r') c = ' ';
        return s;
    }

    /** Splits "key rest of line" at the first space. */
    void splitKey (const std::string& line, std::string& key, std::string& rest)
    {
        const auto sp = line.find (' ');
        key = line.substr (0, sp);
        rest = sp == std::string::npos ? std::string() : line.substr (sp + 1);
    }
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
    s += "name " + oneLine (p.name) + "\n";
    s += "category " + oneLine (p.category) + "\n";
    s += "author " + oneLine (p.author) + "\n";
    for (const auto& t : p.tags)
        s += "tag " + oneLine (t) + "\n";
    for (size_t m = 0; m < p.macroNames.size(); ++m)
        s += "macro " + std::to_string (m + 1) + " " + oneLine (p.macroNames[m]) + "\n";
    s += "seed " + std::to_string (p.livingSeed) + "\n";
    s += std::string ("seedlock ") + (p.seedLocked ? "1" : "0") + "\n";
    for (int i = 0; i < kNumParams; ++i)
        s += std::string ("param ") + info ((P) i).id + " " + formatFloat (p.values[(size_t) i]) + "\n";
    return s;
}

bool deserializePatch (const std::string& text, Patch& out, std::string* error)
{
    std::istringstream in (text);
    std::string line, key, rest;

    // Header: first non-empty line.
    bool headerOk = false;
    while (std::getline (in, line))
    {
        if (! line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        splitKey (line, key, rest);
        headerOk = key == "verdant-patch" && std::atoi (rest.c_str()) >= 1;
        break;
    }
    if (! headerOk)
    {
        if (error != nullptr) *error = "not a Verdant patch (missing 'verdant-patch' header)";
        return false;
    }

    Patch p;
    while (std::getline (in, line))
    {
        if (! line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        splitKey (line, key, rest);

        if (key == "name")            p.name = rest;
        else if (key == "category")   p.category = rest;
        else if (key == "author")     p.author = rest;
        else if (key == "tag")        { if (! rest.empty()) p.tags.push_back (rest); }
        else if (key == "macro")
        {
            std::string num, name;
            splitKey (rest, num, name);
            const int m = std::atoi (num.c_str());
            if (m >= 1 && m <= (int) p.macroNames.size()) p.macroNames[(size_t) m - 1] = name;
        }
        else if (key == "seed")       p.livingSeed = (uint32_t) std::strtoul (rest.c_str(), nullptr, 10);
        else if (key == "seedlock")   p.seedLocked = std::atoi (rest.c_str()) != 0;
        else if (key == "param")
        {
            std::string id, value;
            splitKey (rest, id, value);
            const int idx = indexOf (id.c_str());
            bool ok = false;
            const float v = parseFloat (value, ok);
            if (idx >= 0 && ok)
                p.values[(size_t) idx] = clampToRange ((P) idx, v);
        }
        // "blob" and anything unknown: ignored here (W-F adds blobs).
    }

    out = std::move (p);
    return true;
}

} // namespace verdant
