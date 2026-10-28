#pragma once

// Verdant parameter table: the single source of truth for every parameter's
// id, names, group, kind, range, default, skew and unit (CONTRACT §3).
//
//  * The plugin builds its APVTS layout from info(P) (same min / max / midpoint,
//    so host, UI and engine agree on the knob travel).
//  * The engine reads ParamValues (plain units, indexed by P).
//  * The mod matrix uses the destination list at the bottom of this file.
//
// IDs and names are frozen once presets and the host test use them.

#include "Common.h"

#include <array>
#include <cstdint>

namespace verdant
{

enum class ParamKind : uint8_t { Float, Int, Choice, Bool };

struct ParamInfo
{
    const char* id;
    const char* name;        // unique host-facing name; instanced params are qualified ("Osc A Position", "Env 1 Attack")
    const char* shortName;   // label inside the group's own panel ("Position", "Attack", "Slot 3")
    const char* group;
    ParamKind kind;
    float min, max, def, midpoint;   // Choice: 0 .. numChoices - 1, Bool: 0 .. 1; midpoint 0 = linear
    const char* unit;                // "%", "Hz", "ms", "dB", "ct", "st", "°", ":1", "notes", ""
    const char* const* choices;      // Choice only
    int numChoices;
    bool modulatable;
};

// Instanced parameters, in their order inside each instance block.
enum class OscParam     { On, Table, Pos, WarpMode, Warp, Oct, Semi, Fine, Pitch, Level, Pan, UniVoices, UniDetune, UniSpread, UniBlend, Phase, RandPhase, Route, Count };
enum class FilterParam  { On, Type, Cutoff, Res, Drive, Key, Mix, Count };
enum class EnvParam     { A, H, D, S, R, ACurve, DCurve, Count };
enum class LfoParam     { Shape, Rate, Sync, Div, Mode, Phase, Fade, Smooth, Polarity, Count };
enum class SeqStepParam { Pitch, Vel, Gate, Mod, Count };

inline constexpr int kNumOscs     = 2;
inline constexpr int kNumFilters  = 2;
inline constexpr int kNumEnvs     = 3;
inline constexpr int kNumLfos     = 4;
inline constexpr int kNumMacros   = 8;
inline constexpr int kModSlots    = 24;
inline constexpr int kNumFxSlots  = 8;
inline constexpr int kNumSeqSteps = 16;
inline constexpr int kNumModDestinations = 68;   // == numModDestinations()

/** One entry per parameter, in table order. Instanced blocks are reached through the helpers below. */
enum class P : uint16_t
{
    // Global. PitchSnap ("Pitch Snap to Key"): when on, each oscillator's pitch (note + bend + glide +
    // global pitch + oct / semi + osc pitch, before fine / detune) is snapped to the nearest note of the
    // global key / scale via ap::Scale::nearest.
    Voices, VoiceMode, Glide, BendRange, Master, Pitch, KeyRoot, KeyScale, PitchSnap,

    // Osc A, Osc B: OscParam::Count entries each (oscParam())
    OscABase,
    OscBBase = OscABase + (int) OscParam::Count,

    // Sub & Noise
    SubLevel = OscBBase + (int) OscParam::Count, SubWave, SubOct, SubRoute, NoiseLevel, NoiseColor, NoiseRoute,

    // Filter 1, Filter 2 (filterParam()), then the routing switch
    Filter1Base,
    Filter2Base   = Filter1Base + (int) FilterParam::Count,
    FilterRouting = Filter2Base + (int) FilterParam::Count,

    // Env 1..3 (envParam()), LFO 1..4 (lfoParam())
    EnvBase,
    LfoBase = EnvBase + kNumEnvs * (int) EnvParam::Count,

    // Macros 1..8 (macroParam())
    MacroBase = LfoBase + kNumLfos * (int) LfoParam::Count,

    // Mod matrix: per slot src, dst, amt (modSrc(), modDst(), modAmt())
    ModBase = MacroBase + kNumMacros,

    // Physics
    BallGravity = ModBase + kModSlots * 3, BallBounce, SpringStiffness, SpringDamping, OrbitSpeed, OrbitShape,

    // Alive
    Alive, Evolve,

    // Arp
    ArpOn, ArpMode, ArpOct, ArpDiv, ArpGate, ArpSwing, ArpSnap,

    // Step sequencer, then per step pitch, vel, gate, mod (seqStepParam())
    SeqOn, SeqSteps, SeqDiv, SeqGate, SeqSwing, SeqSnap,
    SeqStepBase,

    // FX rack: slot types (fxSlotType()), then the effect parameters
    FxSlotBase = SeqStepBase + kNumSeqSteps * (int) SeqStepParam::Count,
    DistType = FxSlotBase + kNumFxSlots, DistDrive, DistTone, DistMix,
    ChorusRate, ChorusDepth, ChorusFeedback, ChorusMix,
    PhaserRate, PhaserDepth, PhaserFeedback, PhaserStages, PhaserMix,
    FlangerRate, FlangerDepth, FlangerFeedback, FlangerMix,
    DelayTime, DelaySync, DelayDiv, DelayFeedback, DelayPingPong, DelayLowCut, DelayHighCut, DelayMix,
    ReverbSize, ReverbDecay, ReverbDamp, ReverbPredelay, ReverbWidth, ReverbMix,
    EqLowGain, EqLowFreq, EqMidGain, EqMidFreq, EqMidQ, EqHighGain, EqHighFreq,
    CompThreshold, CompRatio, CompAttack, CompRelease, CompMakeup, CompMix,

    // Sympathetic memory
    SympAmount, SympMemory, SympDecay, SympBright, SympDamp, SympStrings, SympSnap,

    Count
};

inline constexpr int kNumParams = (int) P::Count;
static_assert (kNumParams == 348, "CONTRACT §3 (+ pitch_snap amendment): 348 parameters");

const ParamInfo& info (P);                         // O(1) table lookup
int indexOf (const char* id);                       // -1 if unknown
using ParamValues = std::array<float, kNumParams>;  // plain units; Choice / Int / Bool as float
ParamValues defaultValues();
float toNormalised (P, float plain);                // skewed 0..1, same mapping as the host knob
float fromNormalised (P, float normalised);
float clampToRange (P, float plain);                // clamped, and rounded for Int / Choice / Bool

// Instanced-parameter helpers (indices are 0-based)
P oscParam (int osc, OscParam);                     // osc 0 = A, 1 = B
P filterParam (int filter, FilterParam);            // 0..1
P envParam (int env, EnvParam);                     // 0..2
P lfoParam (int lfo, LfoParam);                     // 0..3
P macroParam (int macro);                           // 0..7
P modSrc (int slot);                                // 0..23
P modDst (int slot);
P modAmt (int slot);
P fxSlotType (int slot);                            // 0..7
P seqStepParam (int step, SeqStepParam);            // 0..15

// Modulation sources (CONTRACT §4); their names are the mod{n}_src choices.
enum class ModSource : uint8_t
{
    None, Lfo1, Lfo2, Lfo3, Lfo4, Env1, Env2, Env3, Velocity, Aftertouch,
    ModWheel, PitchBend, Note, Random, Macro1, Macro2, Macro3, Macro4, Macro5, Macro6, Macro7, Macro8,
    Ball, Spring, Orbit, Alive, SeqMod, Count
};

const char* modSourceName (ModSource);   // "None", "LFO 1", ..., "Seq Mod"

// Modulation destinations: index 0 = None, 1..68 = the modulatable params in table order.
int numModDestinations();                      // 68
P modDestination (int destIndex);              // 1..68; anything else returns P::Count
int modDestinationIndexOf (P);                 // 0 if not modulatable
const char* modDestinationName (int destIndex); // "None", "Master", "Osc A Position", ...

} // namespace verdant
