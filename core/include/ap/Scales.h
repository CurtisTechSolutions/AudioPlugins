#pragma once

#include <cmath>
#include <cstdint>
#include <initializer_list>

namespace ap
{

enum class ScaleType : int
{
    Chromatic = 0,
    Major,
    NaturalMinor,
    HarmonicMinor,
    MelodicMinor,
    Dorian,
    Phrygian,
    Lydian,
    Mixolydian,
    Locrian,
    MajorPentatonic,
    MinorPentatonic,
    Blues,
    WholeTone,
    Custom,
    Count
};

inline constexpr const char* scaleName (ScaleType s) noexcept
{
    switch (s)
    {
        case ScaleType::Chromatic:       return "Chromatic";
        case ScaleType::Major:           return "Major";
        case ScaleType::NaturalMinor:    return "Natural Minor";
        case ScaleType::HarmonicMinor:   return "Harmonic Minor";
        case ScaleType::MelodicMinor:    return "Melodic Minor";
        case ScaleType::Dorian:          return "Dorian";
        case ScaleType::Phrygian:        return "Phrygian";
        case ScaleType::Lydian:          return "Lydian";
        case ScaleType::Mixolydian:      return "Mixolydian";
        case ScaleType::Locrian:         return "Locrian";
        case ScaleType::MajorPentatonic: return "Major Pentatonic";
        case ScaleType::MinorPentatonic: return "Minor Pentatonic";
        case ScaleType::Blues:           return "Blues";
        case ScaleType::WholeTone:       return "Whole Tone";
        case ScaleType::Custom:          return "Custom";
        case ScaleType::Count:
        default:                         return "?";
    }
}

inline constexpr const char* noteName (int pitchClass) noexcept
{
    constexpr const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[((pitchClass % 12) + 12) % 12];
}

/** 12-bit mask of allowed pitch classes relative to the root (bit 0 = root). */
inline constexpr uint16_t scaleMask (ScaleType s) noexcept
{
    constexpr auto bits = [] (std::initializer_list<int> degrees) constexpr
    {
        uint16_t m = 0;
        for (int d : degrees) m = (uint16_t) (m | (1u << d));
        return m;
    };

    switch (s)
    {
        case ScaleType::Chromatic:       return 0x0FFF;
        case ScaleType::Major:           return bits ({ 0, 2, 4, 5, 7, 9, 11 });
        case ScaleType::NaturalMinor:    return bits ({ 0, 2, 3, 5, 7, 8, 10 });
        case ScaleType::HarmonicMinor:   return bits ({ 0, 2, 3, 5, 7, 8, 11 });
        case ScaleType::MelodicMinor:    return bits ({ 0, 2, 3, 5, 7, 9, 11 });
        case ScaleType::Dorian:          return bits ({ 0, 2, 3, 5, 7, 9, 10 });
        case ScaleType::Phrygian:        return bits ({ 0, 1, 3, 5, 7, 8, 10 });
        case ScaleType::Lydian:          return bits ({ 0, 2, 4, 6, 7, 9, 11 });
        case ScaleType::Mixolydian:      return bits ({ 0, 2, 4, 5, 7, 9, 10 });
        case ScaleType::Locrian:         return bits ({ 0, 1, 3, 5, 6, 8, 10 });
        case ScaleType::MajorPentatonic: return bits ({ 0, 2, 4, 7, 9 });
        case ScaleType::MinorPentatonic: return bits ({ 0, 3, 5, 7, 10 });
        case ScaleType::Blues:           return bits ({ 0, 3, 5, 6, 7, 10 });
        case ScaleType::WholeTone:       return bits ({ 0, 2, 4, 6, 8, 10 });
        case ScaleType::Custom:
        case ScaleType::Count:
        default:                         return 0x0FFF;
    }
}

/** A set of allowed pitch classes, rooted on a key. */
struct Scale
{
    int      root = 0;        // 0 = C ... 11 = B
    uint16_t mask = 0x0FFF;   // relative to root

    static Scale fromType (ScaleType type, int rootPitchClass, uint16_t customMask = 0x0FFF) noexcept
    {
        Scale s;
        s.root = ((rootPitchClass % 12) + 12) % 12;
        s.mask = type == ScaleType::Custom ? customMask : scaleMask (type);
        if ((s.mask & 0x0FFF) == 0) s.mask = 0x0FFF;   // never allow an empty scale
        return s;
    }

    bool contains (int midiNote) const noexcept
    {
        const int pc = (((midiNote - root) % 12) + 12) % 12;
        return (mask >> pc) & 1u;
    }

    /** Nearest allowed MIDI note to a fractional MIDI value. Ties go to the lower note. */
    int nearest (float midiFloat) const noexcept
    {
        const int center = (int) std::lround (midiFloat);
        int   best     = center;
        float bestDist = 1.0e9f;

        for (int offset = 0; offset <= 12; ++offset)
        {
            for (int sign = -1; sign <= 1; sign += 2)
            {
                if (offset == 0 && sign == 1) continue;
                const int n = center + sign * offset;
                if (! contains (n)) continue;
                const float d = std::fabs (midiFloat - (float) n);
                if (d < bestDist - 1.0e-6f || (std::fabs (d - bestDist) <= 1.0e-6f && n < best))
                {
                    bestDist = d;
                    best = n;
                }
            }
            if (bestDist <= (float) offset) break;   // nothing further out can be closer
        }
        return best;
    }
};

/**
    Rounds a fractional MIDI value to a note, pulling it toward the nearest
    scale note by `strength` first: 0 is plain chromatic rounding, 1 is
    Scale::nearest, and in between an out-of-scale note survives only when the
    value sits close to its centre.
*/
inline int quantizeNote (float midiFloat, const Scale& scale, float strength) noexcept
{
    strength = clamp (strength, 0.0f, 1.0f);
    const int target = scale.nearest (midiFloat);
    if (strength >= 1.0f) return target;
    const float pulled = midiFloat + strength * ((float) target - midiFloat);
    return (int) std::lround (pulled);
}

} // namespace ap
