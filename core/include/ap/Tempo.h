#pragma once

#include "Math.h"

#include <cmath>

namespace ap
{

/** Rhythmic divisions expressed in quarter-note beats. */
enum class NoteDivision : int
{
    Whole = 0, Half, Quarter, Eighth, Sixteenth, ThirtySecond,
    QuarterTriplet, EighthTriplet, SixteenthTriplet,
    HalfDotted, QuarterDotted, EighthDotted,
    Count
};

inline constexpr const char* noteDivisionName (NoteDivision d) noexcept
{
    switch (d)
    {
        case NoteDivision::Whole:            return "1/1";
        case NoteDivision::Half:             return "1/2";
        case NoteDivision::Quarter:          return "1/4";
        case NoteDivision::Eighth:           return "1/8";
        case NoteDivision::Sixteenth:        return "1/16";
        case NoteDivision::ThirtySecond:     return "1/32";
        case NoteDivision::QuarterTriplet:   return "1/4 T";
        case NoteDivision::EighthTriplet:    return "1/8 T";
        case NoteDivision::SixteenthTriplet: return "1/16 T";
        case NoteDivision::HalfDotted:       return "1/2 D";
        case NoteDivision::QuarterDotted:    return "1/4 D";
        case NoteDivision::EighthDotted:     return "1/8 D";
        case NoteDivision::Count:
        default:                             return "?";
    }
}

inline constexpr double noteDivisionBeats (NoteDivision d) noexcept
{
    switch (d)
    {
        case NoteDivision::Whole:            return 4.0;
        case NoteDivision::Half:             return 2.0;
        case NoteDivision::Quarter:          return 1.0;
        case NoteDivision::Eighth:           return 0.5;
        case NoteDivision::Sixteenth:        return 0.25;
        case NoteDivision::ThirtySecond:     return 0.125;
        case NoteDivision::QuarterTriplet:   return 2.0 / 3.0;
        case NoteDivision::EighthTriplet:    return 1.0 / 3.0;
        case NoteDivision::SixteenthTriplet: return 1.0 / 6.0;
        case NoteDivision::HalfDotted:       return 3.0;
        case NoteDivision::QuarterDotted:    return 1.5;
        case NoteDivision::EighthDotted:     return 0.75;
        case NoteDivision::Count:
        default:                             return 1.0;
    }
}

/**
    Tempo-synced "ghost" ducking envelope: a dip that starts on every division
    boundary, holds, then recovers. Output is a linear gain multiplier.
*/
class GhostDuckEnvelope
{
public:
    struct Shape
    {
        float attackMs  = 5.0f;     // time to reach full depth
        float holdMs    = 40.0f;    // time held at full depth
        float releaseMs = 180.0f;   // time to recover to unity
        float depthDb   = 12.0f;    // how far down we go
        float curve     = 0.5f;     // 0 = linear release, 1 = strongly exponential
    };

    void setShape (const Shape& s) noexcept { shape = s; }

    /** Gain at `msSinceTrigger` milliseconds after a division boundary. */
    float gainAt (double msSinceTrigger) const noexcept
    {
        if (msSinceTrigger < 0.0) return 1.0f;
        const double a = std::max (0.1, (double) shape.attackMs);
        const double h = std::max (0.0, (double) shape.holdMs);
        const double r = std::max (0.1, (double) shape.releaseMs);
        const float  floorGain = dbToGain (-std::max (0.0f, shape.depthDb));

        double depthAmount;    // 0 = unity, 1 = full depth
        if (msSinceTrigger < a)
        {
            depthAmount = msSinceTrigger / a;
        }
        else if (msSinceTrigger < a + h)
        {
            depthAmount = 1.0;
        }
        else if (msSinceTrigger < a + h + r)
        {
            const double x = (msSinceTrigger - a - h) / r;      // 0..1
            const double linear = 1.0 - x;
            const double expo   = std::pow (1.0 - x, 2.5);
            depthAmount = lerp (linear, expo, (double) clamp (shape.curve, 0.0f, 1.0f));
        }
        else
        {
            depthAmount = 0.0;
        }
        return lerp (1.0f, floorGain, (float) depthAmount);
    }

    /** Gain for an absolute beat position with a given division (beats) and phase offset (0..1). */
    float gainAtBeat (double ppqPosition, double bpm, NoteDivision division, double phaseOffset = 0.0) const noexcept
    {
        const double beats = noteDivisionBeats (division);
        if (beats <= 0.0 || bpm <= 0.0) return 1.0f;
        double pos = ppqPosition / beats - phaseOffset;
        pos -= std::floor (pos);                                  // 0..1 within the division
        const double msPerDivision = beats * 60000.0 / bpm;
        return gainAt (pos * msPerDivision);
    }

private:
    Shape shape;
};

} // namespace ap
