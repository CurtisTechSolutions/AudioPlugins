#include "Params.h"

#include <ap/Scales.h>
#include <ap/Tempo.h>

#include <cmath>
#include <cstdlib>
#include <deque>
#include <iterator>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace verdant
{

namespace
{
    // ---- Choice lists (constant-initialised, so they are safe to use from any static initialiser)
    constexpr const char* kVoiceModes[]   = { "Poly", "Mono", "Legato" };
    constexpr const char* kKeys[]         = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    constexpr const char* kScales[]       = { ap::scaleName (ap::ScaleType::Chromatic),       ap::scaleName (ap::ScaleType::Major),
                                              ap::scaleName (ap::ScaleType::NaturalMinor),    ap::scaleName (ap::ScaleType::HarmonicMinor),
                                              ap::scaleName (ap::ScaleType::MelodicMinor),    ap::scaleName (ap::ScaleType::Dorian),
                                              ap::scaleName (ap::ScaleType::Phrygian),        ap::scaleName (ap::ScaleType::Lydian),
                                              ap::scaleName (ap::ScaleType::Mixolydian),      ap::scaleName (ap::ScaleType::Locrian),
                                              ap::scaleName (ap::ScaleType::MajorPentatonic), ap::scaleName (ap::ScaleType::MinorPentatonic),
                                              ap::scaleName (ap::ScaleType::Blues),           ap::scaleName (ap::ScaleType::WholeTone) };
    // 16 factory tables (WavetableFactory, same order) + the oscillator's user table.
    constexpr const char* kTables[]       = { "Basic Shapes", "Analog Saws", "Pulse Width", "Harmonic Stack", "Odd Harmonics",
                                              "Formant Vowels", "Bell", "Organ", "Digital Glass", "FM Sweep", "Sync Sweep",
                                              "Noise Bloom", "Growl", "Pluck Decay", "Choir", "Sub Bass", "User" };
    constexpr const char* kWarpModes[]    = { "Off", "Sync", "Bend", "Squeeze", "Mirror", "FM Osc", "FM Sub", "Remap" };
    constexpr const char* kRoutes[]       = { "Filter 1", "Filter 2", "Both", "Direct" };
    constexpr const char* kSubWaves[]     = { "Sine", "Triangle", "Square", "Saw" };
    constexpr const char* kSubOctaves[]   = { "0", "-1", "-2" };
    constexpr const char* kNoiseColours[] = { "White", "Pink", "Brown", "Blue", "Violet" };
    constexpr const char* kFilterTypes[]  = { "LP 12", "LP 24", "HP 12", "HP 24", "BP 12", "BP 24", "Notch", "Comb +", "Comb -", "Formant", "Ladder" };
    constexpr const char* kFilterRouting[] = { "Serial", "Parallel" };
    constexpr const char* kLfoShapes[]    = { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "S&H", "Custom" };
    constexpr const char* kDivisions[]    = { ap::noteDivisionName (ap::NoteDivision::Whole),            ap::noteDivisionName (ap::NoteDivision::Half),
                                              ap::noteDivisionName (ap::NoteDivision::Quarter),          ap::noteDivisionName (ap::NoteDivision::Eighth),
                                              ap::noteDivisionName (ap::NoteDivision::Sixteenth),        ap::noteDivisionName (ap::NoteDivision::ThirtySecond),
                                              ap::noteDivisionName (ap::NoteDivision::QuarterTriplet),   ap::noteDivisionName (ap::NoteDivision::EighthTriplet),
                                              ap::noteDivisionName (ap::NoteDivision::SixteenthTriplet), ap::noteDivisionName (ap::NoteDivision::HalfDotted),
                                              ap::noteDivisionName (ap::NoteDivision::QuarterDotted),    ap::noteDivisionName (ap::NoteDivision::EighthDotted) };
    constexpr const char* kLfoModes[]     = { "Retrig", "Free", "One-shot" };
    constexpr const char* kPolarities[]   = { "Bipolar", "Unipolar" };
    constexpr const char* kModSources[]   = { "None", "LFO 1", "LFO 2", "LFO 3", "LFO 4", "Env 1", "Env 2", "Env 3",
                                              "Velocity", "Aftertouch", "Mod Wheel", "Pitch Bend", "Note", "Random",
                                              "Macro 1", "Macro 2", "Macro 3", "Macro 4", "Macro 5", "Macro 6", "Macro 7", "Macro 8",
                                              "Ball", "Spring", "Orbit", "Alive", "Seq Mod" };
    constexpr const char* kArpModes[]     = { "Up", "Down", "Up-Down", "Down-Up", "Random", "As Played", "Chord" };
    constexpr const char* kFxTypes[]      = { "None", "Distortion", "Chorus", "Phaser", "Flanger", "Delay", "Reverb", "EQ", "Compressor" };
    constexpr const char* kDistTypes[]    = { "Soft", "Hard", "Tube", "Fold", "Crush", "Diode" };
    constexpr const char* kPhaserStages[] = { "4", "6", "8", "12" };

    static_assert (std::size (kScales) == 14, "14 scales, Custom excluded");
    static_assert (std::size (kDivisions) == (size_t) ap::NoteDivision::Count, "every note division");
    static_assert (std::size (kModSources) == (size_t) ModSource::Count, "one name per mod source");

    constexpr const char* kDegree = "\xC2\xB0";   // "°" in UTF-8

    //==============================================================================
    struct Table
    {
        std::array<ParamInfo, kNumParams> infos {};
        std::array<double, kNumParams> skew {};
        std::array<bool, kNumParams> skewed {};
        std::deque<std::string> strings;                 // storage for generated ids / names (stable addresses)
        std::unordered_map<std::string_view, int> byId;
        std::vector<int> destParams;                     // dest index -> param index; [0] = None (kNumParams)
        std::array<int, kNumParams> destIndex {};
        std::vector<const char*> destNames;
        int count = 0;

        Table() { fill (*this); }
        Table (const Table&) = delete;
        Table& operator= (const Table&) = delete;

        static void fill (Table&);

        const char* keep (std::string s)
        {
            strings.push_back (std::move (s));
            return strings.back().c_str();
        }

        void add (const char* id, const char* name, const char* shortName, const char* group, ParamKind kind,
                  float min, float max, float def, float mid, const char* unit, bool mod,
                  const char* const* choices = nullptr, int numChoices = 0)
        {
            if (count >= kNumParams) { ++count; return; }   // caught by the size check below
            infos[(size_t) count] = ParamInfo { id, name, shortName, group, kind, min, max, def, mid, unit, choices, numChoices, mod };
            ++count;
        }

        // Row helpers: `q` is the instance qualifier ("Osc A", "Env 1", ...) or nullptr for plain rows.
        void flt (const char* id, const char* name, const char* shortName, const char* group,
                  float min, float max, float def, float mid, const char* unit, bool mod)
        {
            add (id, name, shortName, group, ParamKind::Float, min, max, def, mid, unit, mod);
        }

        void integer (const char* id, const char* name, const char* shortName, const char* group,
                      int min, int max, int def, const char* unit = "")
        {
            add (id, name, shortName, group, ParamKind::Int, (float) min, (float) max, (float) def, 0.0f, unit, false);
        }

        template <size_t N>
        void choice (const char* id, const char* name, const char* shortName, const char* group,
                     const char* const (&names)[N], int def)
        {
            add (id, name, shortName, group, ParamKind::Choice, 0.0f, (float) (N - 1), (float) def, 0.0f, "", false, names, (int) N);
        }

        void toggle (const char* id, const char* name, const char* shortName, const char* group, bool def)
        {
            add (id, name, shortName, group, ParamKind::Bool, 0.0f, 1.0f, def ? 1.0f : 0.0f, 0.0f, "", false);
        }

        // Generated strings for instanced rows.
        const char* makeId (const std::string& prefix, const char* suffix) { return keep (prefix + "_" + suffix); }
        const char* makeName (const std::string& qualifier, const char* rowName) { return keep (qualifier + " " + rowName); }
    };

    void Table::fill (Table& t)
    {

        // ---- Global
        {
            const char* g = "Global";
            t.integer ("voices",     "Voices",     "Voices", g, 1, 32, 16);
            t.choice  ("voice_mode", "Voice Mode", "Mode",   g, kVoiceModes, 0);
            t.flt     ("glide",      "Glide",      "Glide",  g, 0.0f, 2000.0f, 0.0f, 100.0f, "ms", false);
            t.integer ("bend_range", "Bend Range", "Bend",   g, 0, 24, 2, "st");
            t.flt     ("master",     "Master",     "Master", g, -60.0f, 12.0f, -6.0f, 0.0f, "dB", true);
            t.flt     ("pitch",      "Pitch",      "Pitch",  g, -48.0f, 48.0f, 0.0f, 0.0f, "st", true);
            t.choice  ("key_root",   "Key",        "Key",    g, kKeys, 0);
            t.choice  ("key_scale",  "Scale",      "Scale",  g, kScales, 0);
        }

        // ---- Osc A / Osc B
        for (int o = 0; o < kNumOscs; ++o)
        {
            const std::string pre = o == 0 ? "a" : "b";
            const std::string q = o == 0 ? "Osc A" : "Osc B";
            const char* g = t.keep (q);

            t.add (t.makeId (pre, "on"), t.makeName (q, "On"), "On", g, ParamKind::Bool, 0.0f, 1.0f, o == 0 ? 1.0f : 0.0f, 0.0f, "", false);
            t.add (t.makeId (pre, "table"), t.makeName (q, "Table"), "Table", g, ParamKind::Choice, 0.0f, (float) (std::size (kTables) - 1), 0.0f, 0.0f, "", false, kTables, (int) std::size (kTables));
            t.flt (t.makeId (pre, "pos"), t.makeName (q, "Position"), "Position", g, 0.0f, 100.0f, 0.0f, 0.0f, "%", true);
            t.add (t.makeId (pre, "warp_mode"), t.makeName (q, "Warp"), "Warp", g, ParamKind::Choice, 0.0f, (float) (std::size (kWarpModes) - 1), 0.0f, 0.0f, "", false, kWarpModes, (int) std::size (kWarpModes));
            t.flt (t.makeId (pre, "warp"), t.makeName (q, "Warp Amount"), "Warp Amount", g, -100.0f, 100.0f, 0.0f, 0.0f, "%", true);
            t.integer (t.makeId (pre, "oct"), t.makeName (q, "Octave"), "Octave", g, -3, 3, 0);
            t.integer (t.makeId (pre, "semi"), t.makeName (q, "Semi"), "Semi", g, -12, 12, 0);
            t.flt (t.makeId (pre, "fine"), t.makeName (q, "Fine"), "Fine", g, -100.0f, 100.0f, 0.0f, 0.0f, "ct", true);
            t.flt (t.makeId (pre, "pitch"), t.makeName (q, "Pitch"), "Pitch", g, -48.0f, 48.0f, 0.0f, 0.0f, "st", true);
            t.flt (t.makeId (pre, "level"), t.makeName (q, "Level"), "Level", g, 0.0f, 100.0f, 80.0f, 0.0f, "%", true);
            t.flt (t.makeId (pre, "pan"), t.makeName (q, "Pan"), "Pan", g, -100.0f, 100.0f, 0.0f, 0.0f, "%", true);
            t.integer (t.makeId (pre, "uni_voices"), t.makeName (q, "Unison"), "Unison", g, 1, 16, 1);
            t.flt (t.makeId (pre, "uni_detune"), t.makeName (q, "Detune"), "Detune", g, 0.0f, 100.0f, 15.0f, 0.0f, "ct", true);
            t.flt (t.makeId (pre, "uni_spread"), t.makeName (q, "Spread"), "Spread", g, 0.0f, 100.0f, 50.0f, 0.0f, "%", true);
            t.flt (t.makeId (pre, "uni_blend"), t.makeName (q, "Blend"), "Blend", g, 0.0f, 100.0f, 75.0f, 0.0f, "%", true);
            t.flt (t.makeId (pre, "phase"), t.makeName (q, "Phase"), "Phase", g, 0.0f, 360.0f, 0.0f, 0.0f, kDegree, false);
            t.flt (t.makeId (pre, "rand_phase"), t.makeName (q, "Random Phase"), "Random Phase", g, 0.0f, 100.0f, 100.0f, 0.0f, "%", false);
            t.add (t.makeId (pre, "route"), t.makeName (q, "Route"), "Route", g, ParamKind::Choice, 0.0f, (float) (std::size (kRoutes) - 1), 0.0f, 0.0f, "", false, kRoutes, (int) std::size (kRoutes));
        }

        // ---- Sub & Noise
        {
            const char* g = "Sub & Noise";
            t.flt    ("sub_level",   "Sub Level",    "Sub",    g, 0.0f, 100.0f, 0.0f, 0.0f, "%", true);
            t.choice ("sub_wave",    "Sub Wave",     "Wave",   g, kSubWaves, 0);
            t.choice ("sub_oct",     "Sub Octave",   "Octave", g, kSubOctaves, 1);
            t.choice ("sub_route",   "Sub Route",    "Route",  g, kRoutes, 3);
            t.flt    ("noise_level", "Noise",        "Noise",  g, 0.0f, 100.0f, 0.0f, 0.0f, "%", true);
            t.choice ("noise_color", "Noise Colour", "Colour", g, kNoiseColours, 0);
            t.choice ("noise_route", "Noise Route",  "Route",  g, kRoutes, 0);
        }

        // ---- Filter 1 / Filter 2
        for (int f = 0; f < kNumFilters; ++f)
        {
            const std::string pre = f == 0 ? "f1" : "f2";
            const std::string q = f == 0 ? "Filter 1" : "Filter 2";
            const char* g = t.keep (q);

            t.add (t.makeId (pre, "on"), t.makeName (q, "On"), "On", g, ParamKind::Bool, 0.0f, 1.0f, f == 0 ? 1.0f : 0.0f, 0.0f, "", false);
            t.add (t.makeId (pre, "type"), t.makeName (q, "Type"), "Type", g, ParamKind::Choice, 0.0f, (float) (std::size (kFilterTypes) - 1), 1.0f, 0.0f, "", false, kFilterTypes, (int) std::size (kFilterTypes));
            t.flt (t.makeId (pre, "cutoff"), t.makeName (q, "Cutoff"), "Cutoff", g, 20.0f, 20000.0f, 12000.0f, std::sqrt (20.0f * 20000.0f), "Hz", true);
            t.flt (t.makeId (pre, "res"), t.makeName (q, "Resonance"), "Resonance", g, 0.0f, 100.0f, 15.0f, 0.0f, "%", true);
            t.flt (t.makeId (pre, "drive"), t.makeName (q, "Drive"), "Drive", g, 0.0f, 100.0f, 0.0f, 0.0f, "%", true);
            t.flt (t.makeId (pre, "key"), t.makeName (q, "Key Track"), "Key Track", g, 0.0f, 100.0f, 0.0f, 0.0f, "%", false);
            t.flt (t.makeId (pre, "mix"), t.makeName (q, "Mix"), "Mix", g, 0.0f, 100.0f, 100.0f, 0.0f, "%", true);
        }
        t.choice ("filter_routing", "Filter Routing", "Routing", "Filter 1", kFilterRouting, 0);

        // ---- Envelopes (Env 1 = amp)
        for (int e = 0; e < kNumEnvs; ++e)
        {
            const std::string pre = "env" + std::to_string (e + 1);
            const std::string q = "Env " + std::to_string (e + 1);
            const char* g = t.keep (q);

            t.flt (t.makeId (pre, "a"), t.makeName (q, "Attack"), "Attack", g, 0.0f, 10000.0f, 5.0f, 250.0f, "ms", true);
            t.flt (t.makeId (pre, "h"), t.makeName (q, "Hold"), "Hold", g, 0.0f, 5000.0f, 0.0f, 100.0f, "ms", false);
            t.flt (t.makeId (pre, "d"), t.makeName (q, "Decay"), "Decay", g, 1.0f, 10000.0f, 300.0f, 400.0f, "ms", true);
            t.flt (t.makeId (pre, "s"), t.makeName (q, "Sustain"), "Sustain", g, 0.0f, 100.0f, e == 0 ? 80.0f : 0.0f, 0.0f, "%", false);
            t.flt (t.makeId (pre, "r"), t.makeName (q, "Release"), "Release", g, 1.0f, 10000.0f, 300.0f, 400.0f, "ms", true);
            t.flt (t.makeId (pre, "acurve"), t.makeName (q, "Attack Curve"), "Attack Curve", g, -100.0f, 100.0f, 0.0f, 0.0f, "%", false);
            t.flt (t.makeId (pre, "dcurve"), t.makeName (q, "Decay Curve"), "Decay Curve", g, -100.0f, 100.0f, -40.0f, 0.0f, "%", false);
        }

        // ---- LFOs
        for (int l = 0; l < kNumLfos; ++l)
        {
            const std::string pre = "lfo" + std::to_string (l + 1);
            const std::string q = "LFO " + std::to_string (l + 1);
            const char* g = t.keep (q);

            t.add (t.makeId (pre, "shape"), t.makeName (q, "Shape"), "Shape", g, ParamKind::Choice, 0.0f, (float) (std::size (kLfoShapes) - 1), 0.0f, 0.0f, "", false, kLfoShapes, (int) std::size (kLfoShapes));
            t.flt (t.makeId (pre, "rate"), t.makeName (q, "Rate"), "Rate", g, 0.01f, 50.0f, 1.0f, 1.0f, "Hz", true);
            t.add (t.makeId (pre, "sync"), t.makeName (q, "Sync"), "Sync", g, ParamKind::Bool, 0.0f, 1.0f, 0.0f, 0.0f, "", false);
            t.add (t.makeId (pre, "div"), t.makeName (q, "Division"), "Division", g, ParamKind::Choice, 0.0f, (float) (std::size (kDivisions) - 1), (float) ap::NoteDivision::Quarter, 0.0f, "", false, kDivisions, (int) std::size (kDivisions));
            t.add (t.makeId (pre, "mode"), t.makeName (q, "Mode"), "Mode", g, ParamKind::Choice, 0.0f, (float) (std::size (kLfoModes) - 1), 0.0f, 0.0f, "", false, kLfoModes, (int) std::size (kLfoModes));
            t.flt (t.makeId (pre, "phase"), t.makeName (q, "Start Phase"), "Start Phase", g, 0.0f, 100.0f, 0.0f, 0.0f, "%", false);
            t.flt (t.makeId (pre, "fade"), t.makeName (q, "Fade In"), "Fade In", g, 0.0f, 5000.0f, 0.0f, 250.0f, "ms", false);
            t.flt (t.makeId (pre, "smooth"), t.makeName (q, "Smooth"), "Smooth", g, 0.0f, 100.0f, 0.0f, 0.0f, "%", false);
            t.add (t.makeId (pre, "polarity"), t.makeName (q, "Polarity"), "Polarity", g, ParamKind::Choice, 0.0f, (float) (std::size (kPolarities) - 1), 0.0f, 0.0f, "", false, kPolarities, (int) std::size (kPolarities));
        }

        // ---- Macros (sources, not destinations)
        for (int m = 0; m < kNumMacros; ++m)
        {
            const char* name = t.keep ("Macro " + std::to_string (m + 1));
            t.flt (t.keep ("macro" + std::to_string (m + 1)), name, name, "Macros", 0.0f, 100.0f, 0.0f, 0.0f, "%", false);
        }

        // ---- Mod matrix (dst choices are patched in once the destination list exists)
        const int firstModRow = t.count;
        for (int s = 0; s < kModSlots; ++s)
        {
            const std::string pre = "mod" + std::to_string (s + 1);
            const std::string q = "Mod " + std::to_string (s + 1);
            const char* g = "Matrix";

            t.add (t.makeId (pre, "src"), t.makeName (q, "Source"), "Source", g, ParamKind::Choice, 0.0f, (float) (std::size (kModSources) - 1), 0.0f, 0.0f, "", false, kModSources, (int) std::size (kModSources));
            t.add (t.makeId (pre, "dst"), t.makeName (q, "Dest"), "Dest", g, ParamKind::Choice, 0.0f, 0.0f, 0.0f, 0.0f, "", false);
            t.flt (t.makeId (pre, "amt"), t.makeName (q, "Amount"), "Amount", g, -100.0f, 100.0f, 0.0f, 0.0f, "%", false);
        }

        // ---- Physics
        {
            const char* g = "Physics";
            t.flt ("ball_gravity",     "Ball Gravity",     "Gravity",   g, 0.0f, 100.0f, 50.0f, 0.0f, "%", false);
            t.flt ("ball_bounce",      "Ball Bounce",      "Bounce",    g, 0.0f, 100.0f, 70.0f, 0.0f, "%", false);
            t.flt ("spring_stiffness", "Spring Stiffness", "Stiffness", g, 0.0f, 100.0f, 50.0f, 0.0f, "%", false);
            t.flt ("spring_damping",   "Spring Damping",   "Damping",   g, 0.0f, 100.0f, 30.0f, 0.0f, "%", false);
            t.flt ("orbit_speed",      "Orbit Speed",      "Speed",     g, 0.0f, 100.0f, 50.0f, 0.0f, "%", false);
            t.flt ("orbit_shape",      "Orbit Shape",      "Shape",     g, 0.0f, 100.0f, 30.0f, 0.0f, "%", false);
        }

        // ---- Alive
        t.flt ("alive",  "Alive",  "Alive",  "Alive", 0.0f, 100.0f, 15.0f, 0.0f, "%", false);
        t.flt ("evolve", "Evolve", "Evolve", "Alive", 0.0f, 100.0f, 0.0f, 0.0f, "%", false);

        // ---- Arp
        {
            const char* g = "Arp";
            t.toggle  ("arp_on",    "Arp",             "On",          g, false);
            t.choice  ("arp_mode",  "Arp Mode",        "Mode",        g, kArpModes, 0);
            t.integer ("arp_oct",   "Arp Octaves",     "Octaves",     g, 1, 4, 1);
            t.choice  ("arp_div",   "Arp Rate",        "Rate",        g, kDivisions, (int) ap::NoteDivision::Eighth);
            t.flt     ("arp_gate",  "Arp Gate",        "Gate",        g, 5.0f, 100.0f, 80.0f, 0.0f, "%", false);
            t.flt     ("arp_swing", "Arp Swing",       "Swing",       g, 0.0f, 100.0f, 0.0f, 0.0f, "%", false);
            t.toggle  ("arp_snap",  "Arp Snap to Key", "Snap to Key", g, false);
        }

        // ---- Step sequencer
        {
            const char* g = "Seq";
            t.toggle  ("seq_on",    "Seq",             "On",          g, false);
            t.integer ("seq_steps", "Seq Steps",       "Steps",       g, 1, 16, 8);
            t.choice  ("seq_div",   "Seq Rate",        "Rate",        g, kDivisions, (int) ap::NoteDivision::Sixteenth);
            t.flt     ("seq_gate",  "Seq Gate",        "Gate",        g, 5.0f, 100.0f, 80.0f, 0.0f, "%", false);
            t.flt     ("seq_swing", "Seq Swing",       "Swing",       g, 0.0f, 100.0f, 0.0f, 0.0f, "%", false);
            t.toggle  ("seq_snap",  "Seq Snap to Key", "Snap to Key", g, true);

            for (int s = 0; s < kNumSeqSteps; ++s)
            {
                const std::string pre = "seq" + std::to_string (s + 1);
                const std::string q = "Step " + std::to_string (s + 1);
                t.integer (t.makeId (pre, "pitch"), t.makeName (q, "Pitch"), "Pitch", g, -24, 24, 0, "st");
                t.flt (t.makeId (pre, "vel"), t.makeName (q, "Velocity"), "Velocity", g, 0.0f, 100.0f, 100.0f, 0.0f, "%", false);
                t.toggle (t.makeId (pre, "gate"), t.makeName (q, "Gate"), "Gate", g, true);
                t.flt (t.makeId (pre, "mod"), t.makeName (q, "Mod"), "Mod", g, 0.0f, 100.0f, 50.0f, 0.0f, "%", false);
            }
        }

        // ---- FX rack
        {
            const char* g = "FX";
            for (int s = 0; s < kNumFxSlots; ++s)
                t.add (t.keep ("fx" + std::to_string (s + 1) + "_type"), t.keep ("FX Slot " + std::to_string (s + 1)),
                       t.keep ("Slot " + std::to_string (s + 1)), g, ParamKind::Choice, 0.0f, (float) (std::size (kFxTypes) - 1),
                       0.0f, 0.0f, "", false, kFxTypes, (int) std::size (kFxTypes));

            t.choice ("dist_type",  "Dist Type",  "Type",  g, kDistTypes, 0);
            t.flt    ("dist_drive", "Dist Drive", "Drive", g, 0.0f, 100.0f, 30.0f, 0.0f, "%", true);
            t.flt    ("dist_tone",  "Dist Tone",  "Tone",  g, 200.0f, 20000.0f, 8000.0f, 3000.0f, "Hz", true);
            t.flt    ("dist_mix",   "Dist Mix",   "Mix",   g, 0.0f, 100.0f, 100.0f, 0.0f, "%", true);

            t.flt ("chorus_rate",     "Chorus Rate",     "Rate",     g, 0.05f, 10.0f, 0.8f, 1.0f, "Hz", true);
            t.flt ("chorus_depth",    "Chorus Depth",    "Depth",    g, 0.0f, 100.0f, 40.0f, 0.0f, "%", true);
            t.flt ("chorus_feedback", "Chorus Feedback", "Feedback", g, 0.0f, 100.0f, 10.0f, 0.0f, "%", false);
            t.flt ("chorus_mix",      "Chorus Mix",      "Mix",      g, 0.0f, 100.0f, 50.0f, 0.0f, "%", true);

            t.flt    ("phaser_rate",     "Phaser Rate",     "Rate",     g, 0.02f, 10.0f, 0.3f, 0.5f, "Hz", true);
            t.flt    ("phaser_depth",    "Phaser Depth",    "Depth",    g, 0.0f, 100.0f, 60.0f, 0.0f, "%", true);
            t.flt    ("phaser_feedback", "Phaser Feedback", "Feedback", g, 0.0f, 100.0f, 30.0f, 0.0f, "%", false);
            t.choice ("phaser_stages",   "Phaser Stages",   "Stages",   g, kPhaserStages, 1);
            t.flt    ("phaser_mix",      "Phaser Mix",      "Mix",      g, 0.0f, 100.0f, 50.0f, 0.0f, "%", true);

            t.flt ("flanger_rate",     "Flanger Rate",     "Rate",     g, 0.02f, 10.0f, 0.25f, 0.5f, "Hz", true);
            t.flt ("flanger_depth",    "Flanger Depth",    "Depth",    g, 0.0f, 100.0f, 50.0f, 0.0f, "%", true);
            t.flt ("flanger_feedback", "Flanger Feedback", "Feedback", g, -100.0f, 100.0f, 40.0f, 0.0f, "%", false);
            t.flt ("flanger_mix",      "Flanger Mix",      "Mix",      g, 0.0f, 100.0f, 50.0f, 0.0f, "%", true);

            t.flt    ("delay_time",     "Delay Time",     "Time",      g, 1.0f, 2000.0f, 375.0f, 200.0f, "ms", true);
            t.toggle ("delay_sync",     "Delay Sync",     "Sync",      g, false);
            t.choice ("delay_div",      "Delay Division", "Division",  g, kDivisions, (int) ap::NoteDivision::Eighth);
            t.flt    ("delay_feedback", "Delay Feedback", "Feedback",  g, 0.0f, 100.0f, 35.0f, 0.0f, "%", true);
            t.toggle ("delay_pingpong", "Ping-Pong",      "Ping-Pong", g, true);
            t.flt    ("delay_lowcut",   "Delay Low Cut",  "Low Cut",   g, 20.0f, 2000.0f, 100.0f, 200.0f, "Hz", false);
            t.flt    ("delay_highcut",  "Delay High Cut", "High Cut",  g, 1000.0f, 20000.0f, 8000.0f, 5000.0f, "Hz", false);
            t.flt    ("delay_mix",      "Delay Mix",      "Mix",       g, 0.0f, 100.0f, 30.0f, 0.0f, "%", true);

            t.flt ("reverb_size",     "Reverb Size",      "Size",      g, 0.0f, 100.0f, 60.0f, 0.0f, "%", true);
            t.flt ("reverb_decay",    "Reverb Decay",     "Decay",     g, 0.0f, 100.0f, 50.0f, 0.0f, "%", true);
            t.flt ("reverb_damp",     "Reverb Damping",   "Damping",   g, 0.0f, 100.0f, 40.0f, 0.0f, "%", false);
            t.flt ("reverb_predelay", "Reverb Pre-delay", "Pre-delay", g, 0.0f, 200.0f, 10.0f, 40.0f, "ms", false);
            t.flt ("reverb_width",    "Reverb Width",     "Width",     g, 0.0f, 100.0f, 100.0f, 0.0f, "%", false);
            t.flt ("reverb_mix",      "Reverb Mix",       "Mix",       g, 0.0f, 100.0f, 25.0f, 0.0f, "%", true);

            t.flt ("eq_low_gain",  "EQ Low",       "Low",       g, -18.0f, 18.0f, 0.0f, 0.0f, "dB", true);
            t.flt ("eq_low_freq",  "EQ Low Freq",  "Low Freq",  g, 30.0f, 500.0f, 120.0f, 120.0f, "Hz", false);
            t.flt ("eq_mid_gain",  "EQ Mid",       "Mid",       g, -18.0f, 18.0f, 0.0f, 0.0f, "dB", true);
            t.flt ("eq_mid_freq",  "EQ Mid Freq",  "Mid Freq",  g, 200.0f, 8000.0f, 1000.0f, 1000.0f, "Hz", true);
            t.flt ("eq_mid_q",     "EQ Mid Q",     "Mid Q",     g, 0.3f, 8.0f, 0.8f, 1.0f, "", false);
            t.flt ("eq_high_gain", "EQ High",      "High",      g, -18.0f, 18.0f, 0.0f, 0.0f, "dB", true);
            t.flt ("eq_high_freq", "EQ High Freq", "High Freq", g, 2000.0f, 18000.0f, 8000.0f, 6000.0f, "Hz", false);

            t.flt ("comp_threshold", "Comp Threshold", "Threshold", g, -60.0f, 0.0f, -18.0f, 0.0f, "dB", false);
            t.flt ("comp_ratio",     "Comp Ratio",     "Ratio",     g, 1.0f, 20.0f, 4.0f, 4.0f, ":1", false);
            t.flt ("comp_attack",    "Comp Attack",    "Attack",    g, 0.1f, 100.0f, 10.0f, 10.0f, "ms", false);
            t.flt ("comp_release",   "Comp Release",   "Release",   g, 10.0f, 1000.0f, 100.0f, 100.0f, "ms", false);
            t.flt ("comp_makeup",    "Comp Makeup",    "Makeup",    g, 0.0f, 24.0f, 0.0f, 0.0f, "dB", false);
            t.flt ("comp_mix",       "Comp Mix",       "Mix",       g, 0.0f, 100.0f, 100.0f, 0.0f, "%", false);
        }

        // ---- Sympathetic memory
        {
            const char* g = "Sympathetic";
            t.flt     ("symp_amount",  "Sympathetic",      "Amount",      g, 0.0f, 100.0f, 0.0f, 0.0f, "%", true);
            t.integer ("symp_memory",  "Memory",           "Memory",      g, 1, 8, 4, "notes");
            t.flt     ("symp_decay",   "Symp Decay",       "Decay",       g, 0.0f, 100.0f, 50.0f, 0.0f, "%", true);
            t.flt     ("symp_bright",  "Symp Brightness",  "Brightness",  g, 0.0f, 100.0f, 50.0f, 0.0f, "%", true);
            t.flt     ("symp_damp",    "Symp Damping",     "Damping",     g, 0.0f, 100.0f, 30.0f, 0.0f, "%", false);
            t.integer ("symp_strings", "Strings",          "Strings",     g, 1, 12, 6);
            t.toggle  ("symp_snap",    "Symp Snap to Key", "Snap to Key", g, false);
        }

        // A wrong row count is a programming error in this file; fail loudly rather than index garbage.
        if (t.count != kNumParams)
            std::abort();

        // ---- Skews, id lookup, destination list
        t.destParams.push_back (kNumParams);
        t.destNames.push_back ("None");
        for (int i = 0; i < kNumParams; ++i)
        {
            auto& in = t.infos[(size_t) i];
            const bool skewed = in.kind == ParamKind::Float && in.midpoint > in.min && in.midpoint < in.max && in.midpoint > 0.0f;
            t.skewed[(size_t) i] = skewed;
            t.skew[(size_t) i] = skewed ? std::log (0.5) / std::log (((double) in.midpoint - in.min) / ((double) in.max - in.min)) : 1.0;
            t.byId.emplace (std::string_view (in.id), i);

            if (in.modulatable)
            {
                t.destIndex[(size_t) i] = (int) t.destParams.size();
                t.destParams.push_back (i);
                t.destNames.push_back (in.name);
            }
        }

        for (int s = 0; s < kModSlots; ++s)
        {
            auto& dst = t.infos[(size_t) (firstModRow + s * 3 + 1)];
            dst.choices = t.destNames.data();
            dst.numChoices = (int) t.destNames.size();
            dst.max = (float) (t.destNames.size() - 1);
        }
    }

    const Table& table()
    {
        static const Table t;
        return t;
    }

    // Build the table during static initialisation so the first audio-thread call never pays for it.
    [[maybe_unused]] const Table& forcedTable = table();

    inline double clamp01 (double v) noexcept { return v >= 0.0 ? (v <= 1.0 ? v : 1.0) : 0.0; }   // NaN -> 0
}

//==============================================================================
const ParamInfo& info (P p)
{
    static const ParamInfo invalid { "", "", "", "", ParamKind::Float, 0.0f, 1.0f, 0.0f, 0.0f, "", nullptr, 0, false };
    const int i = (int) p;
    return i >= 0 && i < kNumParams ? table().infos[(size_t) i] : invalid;
}

int indexOf (const char* id)
{
    if (id == nullptr) return -1;
    const auto& t = table();
    const auto it = t.byId.find (std::string_view (id));
    return it != t.byId.end() ? it->second : -1;
}

ParamValues defaultValues()
{
    ParamValues v {};
    const auto& t = table();
    for (int i = 0; i < kNumParams; ++i)
        v[(size_t) i] = t.infos[(size_t) i].def;
    return v;
}

float toNormalised (P p, float plain)
{
    const int i = (int) p;
    if (i < 0 || i >= kNumParams) return 0.0f;
    const auto& t = table();
    const auto& in = t.infos[(size_t) i];
    const double range = (double) in.max - (double) in.min;
    if (range <= 0.0) return 0.0f;

    double v = (double) plain;
    if (in.kind != ParamKind::Float) v = std::floor (v + 0.5);
    double n = clamp01 ((v - (double) in.min) / range);
    if (t.skewed[(size_t) i] && n > 0.0) n = std::pow (n, t.skew[(size_t) i]);
    return (float) n;
}

float fromNormalised (P p, float normalised)
{
    const int i = (int) p;
    if (i < 0 || i >= kNumParams) return 0.0f;
    const auto& t = table();
    const auto& in = t.infos[(size_t) i];
    const double range = (double) in.max - (double) in.min;
    double n = clamp01 ((double) normalised);

    if (in.kind != ParamKind::Float)
        return (float) ((double) in.min + std::floor (n * range + 0.5));

    if (t.skewed[(size_t) i] && n > 0.0) n = std::exp (std::log (n) / t.skew[(size_t) i]);
    const double v = (double) in.min + range * n;
    return (float) (v < in.min ? in.min : (v > in.max ? in.max : v));
}

float clampToRange (P p, float plain)
{
    const auto& in = info (p);
    double v = (double) plain;
    if (std::isnan (v)) v = (double) in.def;
    if (in.kind != ParamKind::Float) v = std::floor (v + 0.5);
    return (float) (v < in.min ? in.min : (v > in.max ? in.max : v));
}

//==============================================================================
P oscParam (int osc, OscParam w)          { return (P) ((int) P::OscABase + osc * (int) OscParam::Count + (int) w); }
P filterParam (int filter, FilterParam w) { return (P) ((int) P::Filter1Base + filter * (int) FilterParam::Count + (int) w); }
P envParam (int env, EnvParam w)          { return (P) ((int) P::EnvBase + env * (int) EnvParam::Count + (int) w); }
P lfoParam (int lfo, LfoParam w)          { return (P) ((int) P::LfoBase + lfo * (int) LfoParam::Count + (int) w); }
P macroParam (int macro)                  { return (P) ((int) P::MacroBase + macro); }
P modSrc (int slot)                       { return (P) ((int) P::ModBase + slot * 3); }
P modDst (int slot)                       { return (P) ((int) P::ModBase + slot * 3 + 1); }
P modAmt (int slot)                       { return (P) ((int) P::ModBase + slot * 3 + 2); }
P fxSlotType (int slot)                   { return (P) ((int) P::FxSlotBase + slot); }
P seqStepParam (int step, SeqStepParam w) { return (P) ((int) P::SeqStepBase + step * (int) SeqStepParam::Count + (int) w); }

const char* modSourceName (ModSource s)
{
    const int i = (int) s;
    return i >= 0 && i < (int) ModSource::Count ? kModSources[i] : "None";
}

int numModDestinations()
{
    return (int) table().destParams.size() - 1;
}

P modDestination (int destIndex)
{
    const auto& t = table();
    return destIndex >= 1 && destIndex < (int) t.destParams.size() ? (P) t.destParams[(size_t) destIndex] : P::Count;
}

int modDestinationIndexOf (P p)
{
    const int i = (int) p;
    return i >= 0 && i < kNumParams ? table().destIndex[(size_t) i] : 0;
}

const char* modDestinationName (int destIndex)
{
    const auto& t = table();
    return destIndex >= 0 && destIndex < (int) t.destNames.size() ? t.destNames[(size_t) destIndex] : "None";
}

} // namespace verdant
