#pragma once

// Private to the factory bank (FactoryPresets*.cpp): named choice indices and
// the Recipe builder the per-category files write their patches with, so a
// recipe reads like a patch sheet. Not part of the engine's public API.
//
// Owner: W-F.

#include "Patch.h"

#include <ap/Scales.h>
#include <ap/Tempo.h>

#include <initializer_list>
#include <string>
#include <vector>

namespace verdant::factory
{

// Choice indices, named after the parameter table's choice lists.
namespace Tbl
{
    enum : int { BasicShapes, AnalogSaws, PulseWidth, HarmonicStack, OddHarmonics, FormantVowels, Bell, Organ,
                 DigitalGlass, FmSweep, SyncSweep, NoiseBloom, Growl, PluckDecay, Choir, SubBass };
}
namespace Warp   { enum : int { Off, Sync, Bend, Squeeze, Mirror, FmOsc, FmSub, Remap }; }
namespace Route  { enum : int { Filter1, Filter2, Both, Direct }; }
namespace Flt    { enum : int { LP12, LP24, HP12, HP24, BP12, BP24, Notch, CombPlus, CombMinus, Formant, Ladder }; }
namespace Wave   { enum : int { Sine, Triangle, SawUp, SawDown, Square, SampleHold, Custom }; }
namespace Mode   { enum : int { Retrig, Free, OneShot }; }
namespace FxT    { enum : int { None, Distortion, Chorus, Phaser, Flanger, Delay, Reverb, Eq, Compressor }; }
namespace Sub    { enum : int { Sine, Triangle, Square, Saw }; }
namespace Noise  { enum : int { White, Pink, Brown, Blue, Violet }; }
namespace VMode  { enum : int { Poly, Mono, Legato }; }
namespace ArpM   { enum : int { Up, Down, UpDown, DownUp, Random, AsPlayed, Chord }; }
namespace Dist   { enum : int { Soft, Hard, Tube, Fold, Crush, Diode }; }
namespace Key    { enum : int { C, Cs, D, Ds, E, F, Fs, G, Gs, A, As, B }; }

inline constexpr const char* kAuthor = "Curtis Tech Solutions";

using ND = ap::NoteDivision;
using ST = ap::ScaleType;
using MS = ModSource;

inline float divIndex (ND d)     { return (float) (int) d; }
inline float scaleIndex (ST s)   { return (float) (int) s; }

/** A recipe builder over initPatch(): every setter clamps into the parameter's range. Effects take the
    next free FX slot in call order; matrix routes the next free matrix slot. */
struct Recipe
{
    Patch p = initPatch();
    int nextMod = 0, nextFx = 0;

    Recipe (const char* name, const char* category, std::initializer_list<const char*> tags)
    {
        p.name = name;
        p.category = category;
        p.author = kAuthor;
        for (const char* t : tags) p.tags.emplace_back (t);
    }

    Recipe& set (P param, float value) { p.values[(size_t) param] = clampToRange (param, value); return *this; }

    // ---- Global
    Recipe& master (float db)                    { return set (P::Master, db); }
    Recipe& living (float alive, float evolve)   { set (P::Alive, alive); return set (P::Evolve, evolve); }
    Recipe& mono (int mode, float glideMs)       { set (P::VoiceMode, (float) mode); return set (P::Glide, glideMs); }
    Recipe& voices (int n)                       { return set (P::Voices, (float) n); }
    Recipe& glide (float ms)                     { return set (P::Glide, ms); }
    Recipe& bendRange (int st)                   { return set (P::BendRange, (float) st); }
    Recipe& key (int root, ST s)                 { set (P::KeyRoot, (float) root); return set (P::KeyScale, scaleIndex (s)); }
    Recipe& pitchSnap()                          { return set (P::PitchSnap, 1.0f); }
    Recipe& seed (uint32_t s)                    { p.livingSeed = s; return *this; }

    // ---- Oscillators
    Recipe& osc (int o, int table, float pos, float level = 80.0f)
    {
        set (oscParam (o, OscParam::On), 1.0f);
        set (oscParam (o, OscParam::Table), (float) table);
        set (oscParam (o, OscParam::Pos), pos);
        return set (oscParam (o, OscParam::Level), level);
    }
    Recipe& oscOff (int o)                       { return set (oscParam (o, OscParam::On), 0.0f); }
    Recipe& oscPitch (int o, int oct, int semi = 0, float fine = 0.0f)
    {
        set (oscParam (o, OscParam::Oct), (float) oct);
        set (oscParam (o, OscParam::Semi), (float) semi);
        return set (oscParam (o, OscParam::Fine), fine);
    }
    Recipe& oscSemis (int o, float st)           { return set (oscParam (o, OscParam::Pitch), st); }
    Recipe& unison (int o, int voices, float detune, float spread = 70.0f, float blend = 75.0f)
    {
        set (oscParam (o, OscParam::UniVoices), (float) voices);
        set (oscParam (o, OscParam::UniDetune), detune);
        set (oscParam (o, OscParam::UniSpread), spread);
        return set (oscParam (o, OscParam::UniBlend), blend);
    }
    Recipe& warp (int o, int mode, float amount)
    {
        set (oscParam (o, OscParam::WarpMode), (float) mode);
        return set (oscParam (o, OscParam::Warp), amount);
    }
    Recipe& pan (int o, float pan)               { return set (oscParam (o, OscParam::Pan), pan); }
    Recipe& route (int o, int r)                 { return set (oscParam (o, OscParam::Route), (float) r); }
    /** Fixed start phase (degrees) with no randomisation: a repeatable transient for drums and plucks. */
    Recipe& hardPhase (int o, float degrees = 0.0f)
    {
        set (oscParam (o, OscParam::Phase), degrees);
        return set (oscParam (o, OscParam::RandPhase), 0.0f);
    }

    Recipe& sub (float level, int wave = Sub::Sine, int octChoice = 1, int r = Route::Direct)
    {
        set (P::SubLevel, level);
        set (P::SubWave, (float) wave);
        set (P::SubOct, (float) octChoice);
        return set (P::SubRoute, (float) r);
    }
    Recipe& noise (float level, int colour, int r = Route::Filter1)
    {
        set (P::NoiseLevel, level);
        set (P::NoiseColor, (float) colour);
        return set (P::NoiseRoute, (float) r);
    }

    // ---- Filters
    Recipe& filter (int f, int type, float cutoff, float res, float drive = 0.0f, float key = 0.0f)
    {
        set (filterParam (f, FilterParam::On), 1.0f);
        set (filterParam (f, FilterParam::Type), (float) type);
        set (filterParam (f, FilterParam::Cutoff), cutoff);
        set (filterParam (f, FilterParam::Res), res);
        set (filterParam (f, FilterParam::Drive), drive);
        return set (filterParam (f, FilterParam::Key), key);
    }
    Recipe& filterOff (int f)                    { return set (filterParam (f, FilterParam::On), 0.0f); }
    Recipe& filterMix (int f, float mix)         { return set (filterParam (f, FilterParam::Mix), mix); }
    Recipe& parallel()                           { return set (P::FilterRouting, 1.0f); }

    // ---- Envelopes and LFOs
    Recipe& env (int e, float a, float d, float s, float r, float aCurve = 0.0f, float dCurve = -40.0f)
    {
        set (envParam (e, EnvParam::A), a);
        set (envParam (e, EnvParam::D), d);
        set (envParam (e, EnvParam::S), s);
        set (envParam (e, EnvParam::R), r);
        set (envParam (e, EnvParam::ACurve), aCurve);
        return set (envParam (e, EnvParam::DCurve), dCurve);
    }
    Recipe& hold (int e, float ms)               { return set (envParam (e, EnvParam::H), ms); }

    Recipe& lfo (int l, int shape, float rateHz, int mode = Mode::Retrig, bool unipolar = false)
    {
        set (lfoParam (l, LfoParam::Shape), (float) shape);
        set (lfoParam (l, LfoParam::Rate), rateHz);
        set (lfoParam (l, LfoParam::Mode), (float) mode);
        return set (lfoParam (l, LfoParam::Polarity), unipolar ? 1.0f : 0.0f);
    }
    Recipe& lfoSync (int l, ND d)
    {
        set (lfoParam (l, LfoParam::Sync), 1.0f);
        return set (lfoParam (l, LfoParam::Div), divIndex (d));
    }
    Recipe& lfoFade (int l, float ms)            { return set (lfoParam (l, LfoParam::Fade), ms); }
    Recipe& lfoSmooth (int l, float pct)         { return set (lfoParam (l, LfoParam::Smooth), pct); }
    Recipe& lfoPhase (int l, float pct)          { return set (lfoParam (l, LfoParam::Phase), pct); }

    /** A drawn shape for LFO l (sets its Shape to Custom). Points: x ascending 0..1, y 0..1, curve -1..1. */
    Recipe& customShape (int l, std::initializer_list<LfoShapePoint> pts)
    {
        p.lfoShapes[(size_t) l].points.assign (pts.begin(), pts.end());
        return set (lfoParam (l, LfoParam::Shape), (float) Wave::Custom);
    }

    // ---- Matrix and macros
    /** Next free matrix slot: source -> destination, amount -100..100 %. */
    Recipe& mod (MS src, P dest, float amount)
    {
        if (nextMod >= kModSlots) return *this;
        set (modSrc (nextMod), (float) (int) src);
        set (modDst (nextMod), (float) modDestinationIndexOf (dest));
        set (modAmt (nextMod), amount);
        ++nextMod;
        return *this;
    }
    /** Names macro m (0-based) and sets its resting value; route it with mod (MS::Macro1 + m, ...). */
    Recipe& macro (int m, const char* name, float value)
    {
        p.macroNames[(size_t) m] = name;
        return set (macroParam (m), value);
    }
    /** An envelope (Env 2 / 3) pitch sweep on both oscillators: amount % of the +-48 st range (100 % = 96 st). */
    Recipe& pitchEnv (MS env, float amount, bool bothOscs = true)
    {
        mod (env, oscParam (0, OscParam::Pitch), amount);
        return bothOscs ? mod (env, oscParam (1, OscParam::Pitch), amount) : *this;
    }

    // ---- Effects (each takes the next slot)
    Recipe& fx (int type)
    {
        if (nextFx < kNumFxSlots)
            set (fxSlotType (nextFx++), (float) type);
        return *this;
    }
    Recipe& reverb (float size, float decay, float mix, float damp = 40.0f, float predelay = 10.0f)
    {
        fx (FxT::Reverb);
        set (P::ReverbSize, size);
        set (P::ReverbDecay, decay);
        set (P::ReverbDamp, damp);
        set (P::ReverbPredelay, predelay);
        return set (P::ReverbMix, mix);
    }
    Recipe& delay (ND d, float feedback, float mix, bool pingPong = true)
    {
        fx (FxT::Delay);
        set (P::DelaySync, 1.0f);
        set (P::DelayDiv, divIndex (d));
        set (P::DelayFeedback, feedback);
        set (P::DelayPingPong, pingPong ? 1.0f : 0.0f);
        return set (P::DelayMix, mix);
    }
    Recipe& delayMs (float ms, float feedback, float mix, bool pingPong = false)
    {
        fx (FxT::Delay);
        set (P::DelaySync, 0.0f);
        set (P::DelayTime, ms);
        set (P::DelayFeedback, feedback);
        set (P::DelayPingPong, pingPong ? 1.0f : 0.0f);
        return set (P::DelayMix, mix);
    }
    Recipe& delayTone (float lowCut, float highCut)
    {
        set (P::DelayLowCut, lowCut);
        return set (P::DelayHighCut, highCut);
    }
    Recipe& chorus (float rate, float depth, float mix, float feedback = 10.0f)
    {
        fx (FxT::Chorus);
        set (P::ChorusRate, rate);
        set (P::ChorusDepth, depth);
        set (P::ChorusFeedback, feedback);
        return set (P::ChorusMix, mix);
    }
    Recipe& distortion (int type, float drive, float tone, float mix = 100.0f)
    {
        fx (FxT::Distortion);
        set (P::DistType, (float) type);
        set (P::DistDrive, drive);
        set (P::DistTone, tone);
        return set (P::DistMix, mix);
    }
    Recipe& phaser (float rate, float depth, float feedback, float mix, int stagesChoice = 1)
    {
        fx (FxT::Phaser);
        set (P::PhaserRate, rate);
        set (P::PhaserDepth, depth);
        set (P::PhaserFeedback, feedback);
        set (P::PhaserStages, (float) stagesChoice);
        return set (P::PhaserMix, mix);
    }
    Recipe& flanger (float rate, float depth, float feedback, float mix)
    {
        fx (FxT::Flanger);
        set (P::FlangerRate, rate);
        set (P::FlangerDepth, depth);
        set (P::FlangerFeedback, feedback);
        return set (P::FlangerMix, mix);
    }
    Recipe& eq (float lowGain, float midGain, float midFreq, float highGain)
    {
        fx (FxT::Eq);
        set (P::EqLowGain, lowGain);
        set (P::EqMidGain, midGain);
        set (P::EqMidFreq, midFreq);
        return set (P::EqHighGain, highGain);
    }
    Recipe& eqFreqs (float lowHz, float midQ, float highHz)
    {
        set (P::EqLowFreq, lowHz);
        set (P::EqMidQ, midQ);
        return set (P::EqHighFreq, highHz);
    }
    Recipe& compressor (float threshold, float ratio, float makeup, float attackMs = 10.0f, float releaseMs = 100.0f)
    {
        fx (FxT::Compressor);
        set (P::CompThreshold, threshold);
        set (P::CompRatio, ratio);
        set (P::CompAttack, attackMs);
        set (P::CompRelease, releaseMs);
        return set (P::CompMakeup, makeup);
    }

    Recipe& sympathetic (float amount, int memory, int strings, float decay, float bright, float damp = 30.0f)
    {
        set (P::SympAmount, amount);
        set (P::SympMemory, (float) memory);
        set (P::SympStrings, (float) strings);
        set (P::SympDecay, decay);
        set (P::SympDamp, damp);
        return set (P::SympBright, bright);
    }
    Recipe& sympSnap()                           { return set (P::SympSnap, 1.0f); }

    // ---- Physics
    Recipe& ball (float gravity, float bounce)   { set (P::BallGravity, gravity); return set (P::BallBounce, bounce); }
    Recipe& spring (float stiffness, float damping) { set (P::SpringStiffness, stiffness); return set (P::SpringDamping, damping); }
    Recipe& orbit (float speed, float shape)     { set (P::OrbitSpeed, speed); return set (P::OrbitShape, shape); }

    // ---- Arp and step sequencer
    Recipe& arp (int mode, int octaves, ND d, float gate, float swing = 0.0f)
    {
        set (P::ArpOn, 1.0f);
        set (P::ArpMode, (float) mode);
        set (P::ArpOct, (float) octaves);
        set (P::ArpDiv, divIndex (d));
        set (P::ArpGate, gate);
        return set (P::ArpSwing, swing);
    }
    Recipe& arpSnap()                            { return set (P::ArpSnap, 1.0f); }

    /** Step sequencer: one entry per step (pitch, velocity %, gate on, mod %); steps = the list length.
        With snap the pitches are scale degrees in the global key, else semitones. */
    struct Step { int pitch; float vel; bool gate; float mod; };
    Recipe& seq (ND d, float gate, bool snap, std::initializer_list<Step> steps, float swing = 0.0f)
    {
        set (P::SeqOn, 1.0f);
        set (P::SeqDiv, divIndex (d));
        set (P::SeqGate, gate);
        set (P::SeqSwing, swing);
        set (P::SeqSnap, snap ? 1.0f : 0.0f);
        set (P::SeqSteps, (float) steps.size());
        int s = 0;
        for (const auto& st : steps)
        {
            if (s >= kNumSeqSteps) break;
            set (seqStepParam (s, SeqStepParam::Pitch), (float) st.pitch);
            set (seqStepParam (s, SeqStepParam::Vel), st.vel);
            set (seqStepParam (s, SeqStepParam::Gate), st.gate ? 1.0f : 0.0f);
            set (seqStepParam (s, SeqStepParam::Mod), st.mod);
            ++s;
        }
        return *this;
    }
};

// Shorthands for the destinations used most.
inline P posA()      { return oscParam (0, OscParam::Pos); }
inline P posB()      { return oscParam (1, OscParam::Pos); }
inline P pitchA()    { return oscParam (0, OscParam::Pitch); }
inline P pitchB()    { return oscParam (1, OscParam::Pitch); }
inline P warpA()     { return oscParam (0, OscParam::Warp); }
inline P warpB()     { return oscParam (1, OscParam::Warp); }
inline P levelA()    { return oscParam (0, OscParam::Level); }
inline P levelB()    { return oscParam (1, OscParam::Level); }
inline P fineA()     { return oscParam (0, OscParam::Fine); }
inline P fineB()     { return oscParam (1, OscParam::Fine); }
inline P panA()      { return oscParam (0, OscParam::Pan); }
inline P panB()      { return oscParam (1, OscParam::Pan); }
inline P detuneA()   { return oscParam (0, OscParam::UniDetune); }
inline P detuneB()   { return oscParam (1, OscParam::UniDetune); }
inline P spreadA()   { return oscParam (0, OscParam::UniSpread); }
inline P cutoff1()   { return filterParam (0, FilterParam::Cutoff); }
inline P cutoff2()   { return filterParam (1, FilterParam::Cutoff); }
inline P res1()      { return filterParam (0, FilterParam::Res); }
inline P res2()      { return filterParam (1, FilterParam::Res); }
inline P drive1()    { return filterParam (0, FilterParam::Drive); }
inline P mix2()      { return filterParam (1, FilterParam::Mix); }
inline P lfoRate (int l)   { return lfoParam (l, LfoParam::Rate); }
inline P envDecay (int e)  { return envParam (e, EnvParam::D); }
inline P envAttack (int e) { return envParam (e, EnvParam::A); }
inline P envRelease (int e){ return envParam (e, EnvParam::R); }

// The per-category halves of the bank (FactoryPresets_<Category>.cpp), assembled in this order by
// FactoryPresets.cpp after the two fixed openers.
void addOpeningPresets (std::vector<Patch>&);   // "Verdant Keys", "Glass Piano" (host program 0 and 1)
void addBassPresets (std::vector<Patch>&);
void addLeadPresets (std::vector<Patch>&);
void addPadPresets (std::vector<Patch>&);
void addPluckPresets (std::vector<Patch>&);
void addKeysPresets (std::vector<Patch>&);
void addFxPresets (std::vector<Patch>&);
void addTexturePresets (std::vector<Patch>&);
void addSequencePresets (std::vector<Patch>&);
void addDrumPresets (std::vector<Patch>&);
void addExpansionPresets (std::vector<Patch>&);   // after the original 172: three per category, browser order

} // namespace verdant::factory
