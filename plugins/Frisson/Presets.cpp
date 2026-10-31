#include "Presets.h"

#include "PluginProcessor.h"

#include <initializer_list>

namespace frisson
{

namespace
{
    /** Dose plus the nine knobs, in parameter order; every preset sets all ten. */
    struct Knobs { float dose, weight, warmth, focus, groove, shine, space, rush, chills, life; };

    ap::plug::FactoryPreset make (const char* name, const char* category, Knobs k,
                                  std::initializer_list<ap::plug::PresetValue> extra = {})
    {
        ap::plug::FactoryPreset p { name, category,
            { { ids::dose, k.dose }, { ids::weight, k.weight }, { ids::warmth, k.warmth }, { ids::focus, k.focus },
              { ids::groove, k.groove }, { ids::shine, k.shine }, { ids::space, k.space }, { ids::rush, k.rush },
              { ids::chills, k.chills }, { ids::life, k.life } } };
        p.values.insert (p.values.end(), extra.begin(), extra.end());
        return p;
    }
}

std::vector<ap::plug::FactoryPreset> factoryPresets()
{
    using namespace ids;
    //                                                    dose  wgt  wrm  foc  grv  shn  spc  rsh  chl  life
    return {
        { "Default", "", {} },

        // ---- Mix bus and master.
        make ("Gentle Polish",      "Mix & Master", { 30,  20,  15,  25,  10,  30,  15,  20,  20,  15 }),
        make ("Master Sheen",       "Mix & Master", { 45,  30,  20,  30,  10,  55,  25,  35,  25,  15 }, { { rushCeiling, -1.0f } }),
        make ("Streaming Ready",    "Mix & Master", { 40,  30,  20,  35,  10,  40,  15,  45,  20,  15 }, { { rushCeiling, -1.0f } }),
        make ("Loud & Proud",       "Mix & Master", { 60,  45,  30,  35,  20,  45,  20,  70,  35,  15 }),
        make ("Analog Console",     "Mix & Master", { 50,  25,  55,  20,  10,  20,  15,  30,  10,  45 }, { { lifeRate, 0.8f } }),
        make ("Wide Open",          "Mix & Master", { 45,  25,  15,  25,  10,  45,  60,  25,  30,  20 },
              { { spaceMonoHz, 160.0f }, { spaceRoom, 20.0f } }),
        make ("Late-Night Warmth",  "Mix & Master", { 40,  35,  45,  15,   5,  10,  20,  25,  15,  25 }),

        // ---- Drums.
        make ("Drum Bus Punch",     "Drums",        { 60,  55,  30,  25,  65,  30,  10,  50,  15,  10 }),
        make ("Tight Pocket",       "Drums",        { 55,  30,  20,  20,  85,  25,   5,  30,  10,   5 }),
        make ("Loose & Live",       "Drums",        { 50,  35,  35,  20,  40,  25,  30,  30,  20,  50 }, { { groovePocket, 0.0f } }),
        make ("Big Room Drums",     "Drums",        { 60,  50,  25,  20,  45,  30,  50,  45,  25,  15 }, { { spaceRoom, 70.0f } }),
        make ("Crushed Breaks",     "Drums",        { 70,  40,  60,  30,  55,  20,   0,  85,  10,  20 }, { { rushAutoGain, 1.0f } }),

        // ---- Vocals.
        make ("Vocal Forward",      "Vocals",       { 50,   5,  25,  70,   0,  45,  20,  30,  35,  10 }),
        make ("Intimate Vocal",     "Vocals",       { 45,  10,  45,  45,   0,  25,  10,  20,  20,  20 }, { { spaceRoom, 15.0f } }),
        make ("Airy Pop Vocal",     "Vocals",       { 55,   5,  15,  50,   0,  70,  30,  35,  45,  10 }),
        make ("Rap Vocal Bite",     "Vocals",       { 55,  15,  35,  65,  20,  35,   5,  50,  10,   5 }),

        // ---- Instruments.
        make ("Bass Weight",        "Instruments",  { 55,  75,  40,  20,  25,   0,   0,  30,  10,  10 }, { { spaceMonoHz, 200.0f } }),
        make ("Guitar Bloom",       "Instruments",  { 50,  20,  50,  40,  10,  30,  45,  25,  25,  25 }),
        make ("Lush Keys & Pads",   "Instruments",  { 55,  15,  30,  15,   0,  40,  65,  20,  40,  45 }, { { spaceRoom, 55.0f } }),
        make ("Strings Lift",       "Instruments",  { 55,  20,  25,  25,   0,  40,  50,  20,  70,  30 }),
        make ("Synth Lead Edge",    "Instruments",  { 50,  10,  40,  55,  10,  50,  25,  40,  30,  15 }),

        // ---- Character.
        make ("Goosebumps",         "Character",    { 70,  25,  20,  30,  10,  50,  50,  30,  90,  20 }, { { chillsSense, 75.0f } }),
        make ("Vintage Tape",       "Character",    { 60,  30,  75,  15,  10,  10,  15,  35,  10,  70 }, { { lifeRate, 0.6f } }),
        make ("Lo-Fi Haze",         "Character",    { 65,  30,  80,  10,   0,   0,  40,  40,  15,  85 }, { { lifeRate, 1.6f } }),
        make ("Breathing Hardware", "Character",    { 50,  20,  30,  20,  10,  20,  20,  25,  20,  90 }),
        make ("Subtle Touch",       "Character",    { 15,  25,  20,  30,  15,  35,  20,  25,  30,  20 }),
        make ("Full Dose",          "Character",    { 100, 50,  50,  50,  50,  50,  50,  50,  50,  50 }, { { output, -3.0f } }),
    };
}

} // namespace frisson
