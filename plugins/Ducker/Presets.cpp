#include "Presets.h"

#include "PluginProcessor.h"

#include <ap/Tempo.h>

namespace ducker
{

namespace
{
    constexpr float kExternal = 0.0f, kInternal = 1.0f, kGhost = 2.0f;
    float div (ap::NoteDivision d) { return (float) (int) d; }
    using ND = ap::NoteDivision;
}

std::vector<ap::plug::FactoryPreset> factoryPresets()
{
    using namespace ids;
    return {
        { "Default", "", {} },

        // ---- Sidechain: keyed from the host's sidechain input.
        { "Kick Pump", "Sidechain",
          { { mode, kExternal }, { threshold, -30.0f }, { ratio, 10.0f }, { knee, 3.0f }, { attack, 0.5f },
            { release, 180.0f }, { lookahead, 2.0f } } },
        { "Tight Kick & Bass", "Sidechain",
          { { mode, kExternal }, { threshold, -28.0f }, { ratio, 8.0f }, { knee, 4.0f }, { attack, 0.3f },
            { release, 60.0f }, { lookahead, 1.5f }, { scHpf, 30.0f } } },
        { "Gentle Bass Duck", "Sidechain",
          { { mode, kExternal }, { threshold, -24.0f }, { ratio, 3.0f }, { knee, 9.0f }, { attack, 5.0f },
            { release, 90.0f }, { scHpf, 30.0f } } },
        { "Parallel Pump", "Sidechain",
          { { mode, kExternal }, { threshold, -32.0f }, { ratio, 20.0f }, { knee, 2.0f }, { attack, 0.5f },
            { release, 220.0f }, { mix, 60.0f } } },
        { "Brickwall Duck", "Sidechain",
          { { mode, kExternal }, { threshold, -20.0f }, { ratio, 30.0f }, { knee, 0.0f }, { attack, 0.1f },
            { release, 150.0f }, { lookahead, 5.0f } } },
        { "Snare Clearance", "Sidechain",
          { { mode, kExternal }, { threshold, -26.0f }, { ratio, 4.0f }, { knee, 6.0f }, { attack, 1.0f },
            { release, 80.0f }, { scHpf, 200.0f } } },
        { "Hat Shimmer Duck", "Sidechain",
          { { mode, kExternal }, { threshold, -32.0f }, { ratio, 3.0f }, { knee, 6.0f }, { attack, 0.5f },
            { release, 40.0f }, { scHpf, 1000.0f } } },
        { "Voiceover Duck", "Sidechain",
          { { mode, kExternal }, { threshold, -36.0f }, { ratio, 4.0f }, { knee, 12.0f }, { attack, 15.0f },
            { release, 400.0f }, { scHpf, 150.0f } } },
        { "Podcast Music Bed", "Sidechain",
          { { mode, kExternal }, { threshold, -40.0f }, { ratio, 6.0f }, { knee, 12.0f }, { attack, 25.0f },
            { release, 700.0f }, { scHpf, 120.0f } } },
        { "Pad Breathe", "Sidechain",
          { { mode, kExternal }, { threshold, -34.0f }, { ratio, 5.0f }, { knee, 10.0f }, { attack, 8.0f },
            { release, 450.0f }, { scHpf, 40.0f } } },

        // ---- Internal: a compressor keyed from its own input.
        { "Bus Glue", "Internal",
          { { mode, kInternal }, { threshold, -18.0f }, { ratio, 2.0f }, { knee, 9.0f }, { attack, 20.0f },
            { release, 200.0f }, { makeup, 2.0f } } },
        { "Drum Smash", "Internal",
          { { mode, kInternal }, { threshold, -30.0f }, { ratio, 12.0f }, { knee, 3.0f }, { attack, 1.0f },
            { release, 60.0f }, { makeup, 8.0f }, { mix, 50.0f } } },
        { "Vocal Leveler", "Internal",
          { { mode, kInternal }, { threshold, -24.0f }, { ratio, 3.0f }, { knee, 9.0f }, { attack, 8.0f },
            { release, 150.0f }, { scHpf, 100.0f }, { makeup, 4.0f } } },
        { "Bass Clamp", "Internal",
          { { mode, kInternal }, { threshold, -20.0f }, { ratio, 6.0f }, { knee, 6.0f }, { attack, 5.0f },
            { release, 120.0f }, { lookahead, 3.0f }, { makeup, 3.0f } } },
        { "Transient Tamer", "Internal",
          { { mode, kInternal }, { threshold, -16.0f }, { ratio, 8.0f }, { knee, 3.0f }, { attack, 0.1f },
            { release, 30.0f }, { lookahead, 5.0f }, { makeup, 1.0f } } },
        { "Room Squash", "Internal",
          { { mode, kInternal }, { threshold, -36.0f }, { ratio, 20.0f }, { knee, 2.0f }, { attack, 3.0f },
            { release, 250.0f }, { makeup, 12.0f }, { mix, 35.0f } } },

        // ---- Ghost: tempo-synced ducking, no sidechain needed.
        { "Ghost Quarter Pump", "Ghost",
          { { mode, kGhost }, { ghostDiv, div (ND::Quarter) }, { ghostDepth, 12.0f }, { ghostAttack, 5.0f },
            { ghostHold, 40.0f }, { ghostRelease, 180.0f }, { ghostCurve, 50.0f } } },
        { "Ghost Deep Pump", "Ghost",
          { { mode, kGhost }, { ghostDiv, div (ND::Quarter) }, { ghostDepth, 30.0f }, { ghostAttack, 2.0f },
            { ghostHold, 20.0f }, { ghostRelease, 320.0f }, { ghostCurve, 70.0f } } },
        { "Ghost Sub Clear", "Ghost",
          { { mode, kGhost }, { ghostDiv, div (ND::Quarter) }, { ghostDepth, 48.0f }, { ghostAttack, 1.0f },
            { ghostHold, 60.0f }, { ghostRelease, 120.0f }, { ghostCurve, 30.0f } } },
        { "Ghost Eighth Bounce", "Ghost",
          { { mode, kGhost }, { ghostDiv, div (ND::Eighth) }, { ghostDepth, 9.0f }, { ghostAttack, 3.0f },
            { ghostHold, 10.0f }, { ghostRelease, 110.0f }, { ghostCurve, 40.0f } } },
        { "Ghost 16th Stutter", "Ghost",
          { { mode, kGhost }, { ghostDiv, div (ND::Sixteenth) }, { ghostDepth, 18.0f }, { ghostAttack, 1.0f },
            { ghostHold, 15.0f }, { ghostRelease, 60.0f }, { ghostCurve, 30.0f } } },
        { "Ghost Offbeat", "Ghost",
          { { mode, kGhost }, { ghostDiv, div (ND::Quarter) }, { ghostDepth, 10.0f }, { ghostAttack, 10.0f },
            { ghostHold, 30.0f }, { ghostRelease, 160.0f }, { ghostOffset, 50.0f } } },
        { "Ghost Triplet Gallop", "Ghost",
          { { mode, kGhost }, { ghostDiv, div (ND::EighthTriplet) }, { ghostDepth, 12.0f }, { ghostAttack, 2.0f },
            { ghostHold, 10.0f }, { ghostRelease, 120.0f }, { ghostCurve, 45.0f } } },
        { "Ghost Dotted Groove", "Ghost",
          { { mode, kGhost }, { ghostDiv, div (ND::EighthDotted) }, { ghostDepth, 10.0f }, { ghostAttack, 4.0f },
            { ghostHold, 20.0f }, { ghostRelease, 200.0f }, { ghostCurve, 55.0f } } },
        { "Ghost Half-Time Swell", "Ghost",
          { { mode, kGhost }, { ghostDiv, div (ND::Half) }, { ghostDepth, 15.0f }, { ghostAttack, 8.0f },
            { ghostHold, 60.0f }, { ghostRelease, 900.0f }, { ghostCurve, 80.0f } } },
        { "Ghost Slow Breather", "Ghost",
          { { mode, kGhost }, { ghostDiv, div (ND::Whole) }, { ghostDepth, 6.0f }, { ghostAttack, 20.0f },
            { ghostHold, 100.0f }, { ghostRelease, 1500.0f }, { ghostCurve, 70.0f } } },
        { "Ghost Gentle Glue", "Ghost",
          { { mode, kGhost }, { ghostDiv, div (ND::Quarter) }, { ghostDepth, 4.0f }, { ghostAttack, 8.0f },
            { ghostHold, 30.0f }, { ghostRelease, 250.0f }, { ghostCurve, 50.0f } } },
    };
}

} // namespace ducker
