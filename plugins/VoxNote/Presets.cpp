#include "Presets.h"

#include "PluginProcessor.h"

#include <ap/Scales.h>

namespace voxnote
{

namespace
{
    using ST = ap::ScaleType;
    float scaleIndex (ST s) { return (float) (int) s; }
    constexpr float kLow = 0.0f, kHigh = 2.0f;   // Tracking choices (Mid is the default)
    constexpr float kOn = 1.0f, kOff = 0.0f;
    enum : int { C, Cs, D, Ds, E, F, Fs, G, Gs, A, As, B };
}

std::vector<ap::plug::FactoryPreset> factoryPresets()
{
    using namespace ids;
    return {
        { "Default", "", {} },

        // ---- Voice: humming, singing, whistling.
        { "Hum to Melody", "Voice",
          { { onset, 20.0f }, { release, 50.0f }, { sensitivity, 55.0f } } },
        { "Sing to Lead", "Voice",
          { { onset, 12.0f }, { sensitivity, 60.0f }, { retrigger, 55.0f } } },
        { "Whistle Lead", "Voice",
          { { tracking, kHigh }, { gate, -50.0f }, { onset, 8.0f }, { release, 30.0f }, { sensitivity, 60.0f } } },
        { "Octave Up Lead", "Voice",
          { { transpose, 12.0f }, { onset, 12.0f }, { sensitivity, 60.0f } } },
        { "Smooth Legato Glide", "Voice",
          { { onset, 25.0f }, { release, 120.0f }, { retrigger, 100.0f }, { bendRange, 12.0f } } },
        { "Fast Staccato", "Voice",
          { { gate, -40.0f }, { onset, 5.0f }, { release, 10.0f }, { retrigger, 30.0f }, { bend, kOff } } },

        // ---- Bass: low voices and beatbox bass lines.
        { "Bass Voice to Sub", "Bass",
          { { tracking, kLow }, { transpose, -12.0f }, { bend, kOff }, { onset, 15.0f } } },
        { "Beatbox Bass", "Bass",
          { { tracking, kLow }, { transpose, -12.0f }, { snap, kOn }, { key, (float) A },
            { scale, scaleIndex (ST::MinorPentatonic) }, { bend, kOff }, { onset, 6.0f }, { release, 25.0f } } },
        { "Deep Drone Bass", "Bass",
          { { tracking, kLow }, { transpose, -24.0f }, { onset, 30.0f }, { release, 200.0f }, { bend, kOff },
            { velDynamic, kOff }, { velFixed, 110.0f } } },

        // ---- In key: snap every note to a scale (pitch bend off so the synth stays in tune).
        { "Snap to C Major", "In Key",
          { { snap, kOn }, { key, (float) C }, { scale, scaleIndex (ST::Major) }, { bend, kOff } } },
        { "Snap to A Minor", "In Key",
          { { snap, kOn }, { key, (float) A }, { scale, scaleIndex (ST::NaturalMinor) }, { bend, kOff } } },
        { "Pentatonic Safe (A)", "In Key",
          { { snap, kOn }, { key, (float) A }, { scale, scaleIndex (ST::MinorPentatonic) }, { bend, kOff },
            { retrigger, 50.0f } } },
        { "Blues Hummer (E)", "In Key",
          { { snap, kOn }, { key, (float) E }, { scale, scaleIndex (ST::Blues) }, { bend, kOff } } },
        { "Dorian Groove (D)", "In Key",
          { { snap, kOn }, { key, (float) D }, { scale, scaleIndex (ST::Dorian) }, { bend, kOff } } },
        { "Harmony a Fifth Up", "In Key",
          { { snap, kOn }, { key, (float) C }, { scale, scaleIndex (ST::Major) }, { transpose, 7.0f }, { bend, kOff },
            { channel, 2.0f }, { audioThru, kOn } } },

        // ---- Instruments and rooms.
        { "Guitar to MIDI", "Instrument",
          { { tracking, kLow }, { gate, -50.0f }, { sensitivity, 60.0f }, { onset, 6.0f }, { release, 60.0f },
            { retrigger, 45.0f } } },
        { "Fixed Velocity Pads", "Instrument",
          { { onset, 40.0f }, { release, 200.0f }, { bend, kOff }, { velDynamic, kOff }, { velFixed, 90.0f } } },
        { "Noisy Room", "Instrument",
          { { gate, -35.0f }, { sensitivity, 40.0f }, { onset, 30.0f }, { release, 60.0f } } },
        { "Quiet Mic", "Instrument",
          { { gate, -65.0f }, { velMin, -55.0f }, { velMax, -20.0f }, { sensitivity, 60.0f } } },
        { "Monitor Check", "Instrument",
          { { monitor, kOn }, { monitorLevel, -12.0f }, { audioThru, kOn } } },
    };
}

} // namespace voxnote
