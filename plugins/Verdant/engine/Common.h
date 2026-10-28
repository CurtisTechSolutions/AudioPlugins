#pragma once

// Verdant engine: constants and plain data types shared by every engine part.
// The engine is plain C++ on top of ap_core (no JUCE), so it compiles into the
// plugin, the verdant_tests binary and the offline helpers alike.

#include <cstdint>

namespace verdant
{

inline constexpr int kControlInterval = 32;   // samples per modulation (control) block
inline constexpr int kMaxVoices       = 32;
inline constexpr int kMaxUnison       = 16;
inline constexpr int kMaxBlock        = 4096; // longest block the plugin hands to Engine::process in one call

/** Host transport for tempo-aware parts; hasTempo = false means "unknown, free-run at bpm". */
struct Transport
{
    double  bpm        = 120.0;
    double  ppq        = 0.0;     // quarter notes at the first sample of the block
    bool    playing    = false;
    bool    hasTempo   = false;
    int64_t sampleTime = 0;
};

/** A MIDI-ish event, already decoded by the plugin wrapper. */
struct MidiEvent
{
    enum class Type : uint8_t { NoteOn, NoteOff, PitchBend, ModWheel, Aftertouch, PolyAftertouch, Sustain, AllNotesOff, Cc };

    Type  type         = Type::NoteOn;
    int   sampleOffset = 0;      // within the block passed to process()
    int   channel      = 1;
    int   note         = 0;
    float value        = 0.0f;   // velocity 0..1, bend -1..1, wheel / aftertouch / sustain / cc value 0..1
    int   cc           = 0;
};

} // namespace verdant
