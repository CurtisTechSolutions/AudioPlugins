#include "FactoryPresets.h"

#include <ap/Scales.h>
#include <ap/Tempo.h>

#include <initializer_list>
#include <string>

namespace verdant
{

namespace
{
    // Choice indices, named so the recipes below read like a patch sheet.
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

    constexpr const char* kAuthor = "Curtis Tech Solutions";

    float divIndex (ap::NoteDivision d) { return (float) (int) d; }
    float scale (ap::ScaleType s)  { return (float) (int) s; }

    /** A recipe builder over initPatch(): every setter clamps into the parameter's range. */
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

        Recipe& osc (int o, int table, float pos, float level = 80.0f)
        {
            set (oscParam (o, OscParam::On), 1.0f);
            set (oscParam (o, OscParam::Table), (float) table);
            set (oscParam (o, OscParam::Pos), pos);
            return set (oscParam (o, OscParam::Level), level);
        }
        Recipe& oscOff (int o)                    { return set (oscParam (o, OscParam::On), 0.0f); }
        Recipe& oscPitch (int o, int oct, int semi = 0, float fine = 0.0f)
        {
            set (oscParam (o, OscParam::Oct), (float) oct);
            set (oscParam (o, OscParam::Semi), (float) semi);
            return set (oscParam (o, OscParam::Fine), fine);
        }
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
        Recipe& pan (int o, float pan)            { return set (oscParam (o, OscParam::Pan), pan); }
        Recipe& route (int o, int r)              { return set (oscParam (o, OscParam::Route), (float) r); }

        Recipe& sub (float level, int wave = Sub::Sine, int octChoice = 1)
        {
            set (P::SubLevel, level);
            set (P::SubWave, (float) wave);
            return set (P::SubOct, (float) octChoice);
        }
        Recipe& noise (float level, int colour, int r = Route::Filter1)
        {
            set (P::NoiseLevel, level);
            set (P::NoiseColor, (float) colour);
            return set (P::NoiseRoute, (float) r);
        }

        Recipe& filter (int f, int type, float cutoff, float res, float drive = 0.0f, float key = 0.0f)
        {
            set (filterParam (f, FilterParam::On), 1.0f);
            set (filterParam (f, FilterParam::Type), (float) type);
            set (filterParam (f, FilterParam::Cutoff), cutoff);
            set (filterParam (f, FilterParam::Res), res);
            set (filterParam (f, FilterParam::Drive), drive);
            return set (filterParam (f, FilterParam::Key), key);
        }

        Recipe& env (int e, float a, float d, float s, float r, float aCurve = 0.0f, float dCurve = -40.0f)
        {
            set (envParam (e, EnvParam::A), a);
            set (envParam (e, EnvParam::D), d);
            set (envParam (e, EnvParam::S), s);
            set (envParam (e, EnvParam::R), r);
            set (envParam (e, EnvParam::ACurve), aCurve);
            return set (envParam (e, EnvParam::DCurve), dCurve);
        }

        Recipe& lfo (int l, int shape, float rateHz, int mode = Mode::Retrig, bool unipolar = false)
        {
            set (lfoParam (l, LfoParam::Shape), (float) shape);
            set (lfoParam (l, LfoParam::Rate), rateHz);
            set (lfoParam (l, LfoParam::Mode), (float) mode);
            return set (lfoParam (l, LfoParam::Polarity), unipolar ? 1.0f : 0.0f);
        }
        Recipe& lfoSync (int l, ap::NoteDivision d)
        {
            set (lfoParam (l, LfoParam::Sync), 1.0f);
            return set (lfoParam (l, LfoParam::Div), divIndex (d));
        }
        Recipe& lfoFade (int l, float ms)         { return set (lfoParam (l, LfoParam::Fade), ms); }
        Recipe& lfoSmooth (int l, float pct)      { return set (lfoParam (l, LfoParam::Smooth), pct); }

        /** A drawn shape for LFO l (sets its Shape to Custom). Points: x ascending 0..1, y 0..1, curve -1..1. */
        Recipe& customShape (int l, std::initializer_list<LfoShapePoint> pts)
        {
            p.lfoShapes[(size_t) l].points.assign (pts.begin(), pts.end());
            return set (lfoParam (l, LfoParam::Shape), (float) Wave::Custom);
        }

        /** Next free matrix slot: source -> destination, amount -100..100 %. */
        Recipe& mod (ModSource src, P dest, float amount)
        {
            if (nextMod >= kModSlots) return *this;
            set (modSrc (nextMod), (float) (int) src);
            set (modDst (nextMod), (float) modDestinationIndexOf (dest));
            set (modAmt (nextMod), amount);
            ++nextMod;
            return *this;
        }

        Recipe& fx (int type)
        {
            if (nextFx < kNumFxSlots)
                set (fxSlotType (nextFx++), (float) type);
            return *this;
        }

        Recipe& macro (int m, const char* name, float value)
        {
            p.macroNames[(size_t) m] = name;
            return set (macroParam (m), value);
        }

        Recipe& reverb (float size, float decay, float mix, float damp = 40.0f)
        {
            fx (FxT::Reverb);
            set (P::ReverbSize, size);
            set (P::ReverbDecay, decay);
            set (P::ReverbDamp, damp);
            return set (P::ReverbMix, mix);
        }
        Recipe& delay (ap::NoteDivision d, float feedback, float mix)
        {
            fx (FxT::Delay);
            set (P::DelaySync, 1.0f);
            set (P::DelayDiv, divIndex (d));
            set (P::DelayFeedback, feedback);
            return set (P::DelayMix, mix);
        }
        Recipe& chorus (float rate, float depth, float mix)
        {
            fx (FxT::Chorus);
            set (P::ChorusRate, rate);
            set (P::ChorusDepth, depth);
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
        Recipe& phaser (float rate, float depth, float feedback, float mix)
        {
            fx (FxT::Phaser);
            set (P::PhaserRate, rate);
            set (P::PhaserDepth, depth);
            set (P::PhaserFeedback, feedback);
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
        Recipe& compressor (float threshold, float ratio, float makeup)
        {
            fx (FxT::Compressor);
            set (P::CompThreshold, threshold);
            set (P::CompRatio, ratio);
            return set (P::CompMakeup, makeup);
        }

        Recipe& sympathetic (float amount, int memory, int strings, float decay, float bright)
        {
            set (P::SympAmount, amount);
            set (P::SympMemory, (float) memory);
            set (P::SympStrings, (float) strings);
            set (P::SympDecay, decay);
            return set (P::SympBright, bright);
        }

        Recipe& living (float alive, float evolve)  { set (P::Alive, alive); return set (P::Evolve, evolve); }
        Recipe& mono (int mode, float glideMs)      { set (P::VoiceMode, (float) mode); return set (P::Glide, glideMs); }
        Recipe& key (int root, ap::ScaleType s)     { set (P::KeyRoot, (float) root); return set (P::KeyScale, scale (s)); }
        Recipe& master (float db)                   { return set (P::Master, db); }

        Recipe& arp (int mode, int octaves, ap::NoteDivision d, float gate, float swing = 0.0f)
        {
            set (P::ArpOn, 1.0f);
            set (P::ArpMode, (float) mode);
            set (P::ArpOct, (float) octaves);
            set (P::ArpDiv, divIndex (d));
            set (P::ArpGate, gate);
            return set (P::ArpSwing, swing);
        }

        /** Step sequencer: one entry per step (pitch, velocity %, gate on, mod %); steps = the list length. */
        struct Step { int pitch; float vel; bool gate; float mod; };
        Recipe& seq (ap::NoteDivision d, float gate, bool snap, std::initializer_list<Step> steps)
        {
            set (P::SeqOn, 1.0f);
            set (P::SeqDiv, divIndex (d));
            set (P::SeqGate, gate);
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

    // Shorthands for destinations used often below.
    P posA()     { return oscParam (0, OscParam::Pos); }
    P posB()     { return oscParam (1, OscParam::Pos); }
    P pitchA()   { return oscParam (0, OscParam::Pitch); }
    P warpA()    { return oscParam (0, OscParam::Warp); }
    P cutoff1()  { return filterParam (0, FilterParam::Cutoff); }

    using ND = ap::NoteDivision;
    using ST = ap::ScaleType;
    using MS = ModSource;

    std::vector<Patch> build()
    {
        std::vector<Patch> bank;
        auto add = [&bank] (Recipe& r) { bank.push_back (r.p); };

        // ==== Keys ===================================================================
        {   // 0: the default sound. Warm tine-ish keys: harmonic stack + a quiet bell an octave up,
            // a velocity- and Env 2-driven filter, a little life, chorus and room.
            Recipe r ("Verdant Keys", "Keys", { "warm", "classic", "soft" });
            r.osc (0, Tbl::HarmonicStack, 12.0f, 80.0f)
             .osc (1, Tbl::Bell, 8.0f, 30.0f).oscPitch (1, 1, 0, 3.0f)
             .filter (0, Flt::LP24, 2600.0f, 12.0f, 0.0f, 50.0f)
             .env (0, 2.0f, 1400.0f, 40.0f, 450.0f)
             .env (1, 0.0f, 600.0f, 0.0f, 400.0f)
             .mod (MS::Env2, cutoff1(), 22.0f)
             .mod (MS::Velocity, cutoff1(), 18.0f)
             .living (25.0f, 10.0f)
             .chorus (0.6f, 30.0f, 30.0f)
             .reverb (35.0f, 35.0f, 18.0f)
             .master (-8.0f);
            add (r);
        }
        {   // 1: glassy piano with a sympathetic body that rings with the last notes.
            Recipe r ("Glass Piano", "Keys", { "bright", "glassy", "resonant" });
            r.osc (0, Tbl::DigitalGlass, 20.0f, 75.0f)
             .osc (1, Tbl::PluckDecay, 0.0f, 45.0f)
             .filter (0, Flt::LP12, 7000.0f, 10.0f, 0.0f, 60.0f)
             .env (0, 1.0f, 1800.0f, 25.0f, 700.0f)
             .env (1, 0.0f, 900.0f, 0.0f, 500.0f)
             .mod (MS::Env2, posA(), 30.0f)
             .sympathetic (35.0f, 4, 8, 60.0f, 65.0f)
             .reverb (55.0f, 45.0f, 22.0f)
             .master (-9.0f);
            add (r);
        }
        {   // 2: drawbar organ: organ table at two footages, LFO vibrato, phaser and chorus as a rotary stand-in.
            Recipe r ("Drawbar Organ", "Keys", { "vintage", "organ", "classic" });
            r.osc (0, Tbl::Organ, 35.0f, 70.0f)
             .osc (1, Tbl::Organ, 70.0f, 40.0f).oscPitch (1, 1)
             .filter (0, Flt::LP12, 9000.0f, 5.0f)
             .env (0, 4.0f, 50.0f, 100.0f, 60.0f)
             .lfo (0, Wave::Sine, 6.2f).lfoFade (0, 300.0f)
             .mod (MS::Lfo1, pitchA(), 1.0f)
             .phaser (0.8f, 40.0f, 20.0f, 35.0f)
             .chorus (1.2f, 25.0f, 30.0f)
             .master (-11.0f);
            add (r);
        }
        {   // 3: mellow tine keys with a Tone macro on the filter.
            Recipe r ("Tine Dream", "Keys", { "mellow", "warm", "expressive" });
            r.osc (0, Tbl::BasicShapes, 8.0f, 80.0f)
             .osc (1, Tbl::Bell, 25.0f, 35.0f).oscPitch (1, 2)
             .filter (0, Flt::LP24, 1800.0f, 15.0f, 0.0f, 40.0f)
             .env (0, 2.0f, 1600.0f, 35.0f, 500.0f)
             .macro (0, "Tone", 40.0f)
             .mod (MS::Macro1, cutoff1(), 45.0f)
             .mod (MS::Velocity, cutoff1(), 20.0f)
             .chorus (0.4f, 35.0f, 35.0f)
             .reverb (40.0f, 40.0f, 20.0f)
             .master (-8.0f);
            add (r);
        }
        {   // 4: felted upright: dark, soft attack, a gentle sympathetic body, EQ to tame the lows.
            Recipe r ("Felt Upright", "Keys", { "soft", "intimate", "dark" });
            r.osc (0, Tbl::HarmonicStack, 4.0f, 80.0f)
             .filter (0, Flt::LP24, 1500.0f, 8.0f, 0.0f, 50.0f)
             .env (0, 8.0f, 1500.0f, 15.0f, 600.0f)
             .sympathetic (25.0f, 6, 6, 45.0f, 35.0f)
             .eq (-3.0f, 2.0f, 900.0f, -4.0f)
             .reverb (30.0f, 30.0f, 15.0f)
             .master (-7.0f);
            add (r);
        }
        {   // 5: funky clav: narrow pulse into a resonant band-pass, Env 2 snap, compressed.
            Recipe r ("Pulse Clav", "Keys", { "funky", "percussive", "bright" });
            r.osc (0, Tbl::PulseWidth, 45.0f, 80.0f)
             .filter (0, Flt::BP12, 1000.0f, 12.0f, 15.0f, 70.0f)
             .env (0, 0.5f, 350.0f, 20.0f, 120.0f)
             .env (1, 0.0f, 180.0f, 0.0f, 100.0f)
             .mod (MS::Env2, cutoff1(), 35.0f)
             .compressor (-20.0f, 4.0f, 4.0f)
             .master (-8.0f);
            add (r);
        }

        // ==== Bass ===================================================================
        {   // 6: clean sub bass, mono with a short glide.
            Recipe r ("Deep Sub", "Bass", { "deep", "mono", "clean" });
            r.osc (0, Tbl::SubBass, 10.0f, 70.0f)
             .sub (65.0f, Sub::Sine, 1)
             .filter (0, Flt::LP24, 450.0f, 10.0f)
             .env (0, 3.0f, 400.0f, 85.0f, 150.0f)
             .mono (VMode::Mono, 40.0f)
             .living (5.0f, 0.0f)
             .master (-6.0f);
            add (r);
        }
        {   // 7: growl bass: tempo-synced LFO scanning the growl table, ladder filter, drive.
            Recipe r ("Growl Bass", "Bass", { "aggressive", "rhythmic", "mono" });
            r.osc (0, Tbl::Growl, 25.0f, 75.0f)
             .sub (45.0f, Sub::Sine, 1)
             .filter (0, Flt::Ladder, 900.0f, 40.0f, 30.0f)
             .env (0, 2.0f, 300.0f, 80.0f, 120.0f)
             .lfo (0, Wave::Triangle, 2.0f).lfoSync (0, ND::Eighth)
             .mod (MS::Lfo1, posA(), 40.0f)
             .mod (MS::Lfo1, cutoff1(), 20.0f)
             .mono (VMode::Mono, 25.0f)
             .distortion (Dist::Tube, 40.0f, 5000.0f, 70.0f)
             .master (-10.0f);
            add (r);
        }
        {   // 8: classic analog bass: two saws, ladder, filter envelope.
            Recipe r ("Analog Bass", "Bass", { "warm", "classic", "mono" });
            r.osc (0, Tbl::AnalogSaws, 15.0f, 75.0f)
             .osc (1, Tbl::AnalogSaws, 15.0f, 60.0f).oscPitch (1, 0, 0, -9.0f)
             .sub (40.0f, Sub::Square, 1)
             .filter (0, Flt::Ladder, 500.0f, 30.0f, 15.0f, 30.0f)
             .env (0, 1.0f, 500.0f, 70.0f, 120.0f)
             .env (1, 0.0f, 260.0f, 10.0f, 150.0f)
             .mod (MS::Env2, cutoff1(), 45.0f)
             .mono (VMode::Legato, 30.0f)
             .master (-9.0f);
            add (r);
        }
        {   // 9: reese: a wide unison saw stack, a slowly breathing filter.
            Recipe r ("Reese Unison", "Bass", { "dark", "wide", "mono" });
            r.osc (0, Tbl::AnalogSaws, 30.0f, 80.0f).unison (0, 7, 25.0f, 60.0f, 85.0f)
             .sub (45.0f, Sub::Sine, 1)
             .filter (0, Flt::LP24, 1100.0f, 20.0f, 10.0f)
             .env (0, 3.0f, 500.0f, 85.0f, 200.0f)
             .lfo (1, Wave::Sine, 0.25f, Mode::Free)
             .mod (MS::Lfo2, cutoff1(), 15.0f)
             .mono (VMode::Mono, 20.0f)
             .master (-10.0f);
            add (r);
        }
        {   // 10: bouncing ball opens the filter in a decelerating rhythm.
            Recipe r ("Bounce Bass", "Bass", { "bouncy", "rhythmic", "mono" });
            r.osc (0, Tbl::OddHarmonics, 30.0f, 75.0f)
             .sub (40.0f, Sub::Triangle, 1)
             .filter (0, Flt::LP24, 400.0f, 35.0f, 10.0f)
             .env (0, 2.0f, 600.0f, 75.0f, 150.0f)
             .set (P::BallGravity, 60.0f).set (P::BallBounce, 75.0f)
             .mod (MS::Ball, cutoff1(), 45.0f)
             .mono (VMode::Mono, 0.0f)
             .master (-8.0f);
            add (r);
        }
        {   // 11: FM thump: FM from the sub, Env 2 sweeps the FM table, compressed.
            Recipe r ("FM Thump", "Bass", { "punchy", "digital", "mono" });
            r.osc (0, Tbl::FmSweep, 10.0f, 75.0f).warp (0, Warp::FmSub, 35.0f)
             .sub (55.0f, Sub::Sine, 1)
             .filter (0, Flt::LP24, 2500.0f, 10.0f)
             .env (0, 0.5f, 350.0f, 60.0f, 120.0f)
             .env (1, 0.0f, 150.0f, 0.0f, 100.0f)
             .mod (MS::Env2, posA(), 50.0f)
             .mono (VMode::Mono, 0.0f)
             .compressor (-18.0f, 5.0f, 3.0f)
             .master (-9.0f);
            add (r);
        }
        {   // 12: wobble with a drawn LFO; the Wobble macro speeds it up.
            Recipe r ("Wobble Macro", "Bass", { "rhythmic", "aggressive", "mono" });
            r.osc (0, Tbl::SyncSweep, 30.0f, 75.0f)
             .sub (45.0f, Sub::Sine, 1)
             .filter (0, Flt::LP24, 350.0f, 45.0f, 20.0f)
             .env (0, 2.0f, 400.0f, 90.0f, 150.0f)
             .lfo (0, Wave::Custom, 3.0f)
             .customShape (0, { { 0.0f, 0.0f, 0.0f }, { 0.15f, 1.0f, 0.6f }, { 0.5f, 0.35f, -0.4f }, { 0.65f, 0.9f, 0.0f }, { 1.0f, 0.0f, -0.5f } })
             .macro (0, "Wobble", 30.0f)
             .mod (MS::Lfo1, cutoff1(), 50.0f)
             .mod (MS::Macro1, lfoParam (0, LfoParam::Rate), 35.0f)
             .mono (VMode::Mono, 15.0f)
             .distortion (Dist::Soft, 25.0f, 6000.0f, 60.0f)
             .master (-10.0f);
            add (r);
        }

        // ==== Lead ===================================================================
        {   // 13: classic detuned saw lead.
            Recipe r ("Saw Lead", "Lead", { "bright", "classic" });
            r.osc (0, Tbl::AnalogSaws, 50.0f, 75.0f).unison (0, 3, 12.0f, 50.0f)
             .osc (1, Tbl::AnalogSaws, 50.0f, 55.0f).oscPitch (1, 0, 0, 7.0f)
             .filter (0, Flt::LP24, 5000.0f, 25.0f, 0.0f, 40.0f)
             .env (0, 2.0f, 300.0f, 85.0f, 200.0f)
             .mono (VMode::Legato, 30.0f)
             .delay (ND::EighthDotted, 30.0f, 18.0f)
             .master (-10.0f);
            add (r);
        }
        {   // 14: hard-sync scream: Env 2 sweeps the sync table, driven and delayed.
            Recipe r ("Sync Scream", "Lead", { "aggressive", "bright", "mono" });
            r.osc (0, Tbl::SyncSweep, 20.0f, 75.0f).warp (0, Warp::Sync, 40.0f)
             .filter (0, Flt::LP12, 7000.0f, 20.0f)
             .env (0, 1.0f, 400.0f, 80.0f, 200.0f)
             .env (1, 5.0f, 700.0f, 20.0f, 300.0f)
             .mod (MS::Env2, posA(), 45.0f)
             .mod (MS::Env2, warpA(), 30.0f)
             .mono (VMode::Mono, 10.0f)
             .distortion (Dist::Hard, 30.0f, 7000.0f, 50.0f)
             .delay (ND::Eighth, 25.0f, 15.0f)
             .master (-12.0f);
            add (r);
        }
        {   // 15: PWM hero lead: LFO-scanned pulse table, glide.
            Recipe r ("Pulse Hero", "Lead", { "vintage", "mono", "smooth" });
            r.osc (0, Tbl::PulseWidth, 40.0f, 80.0f)
             .filter (0, Flt::LP24, 3800.0f, 20.0f, 0.0f, 50.0f)
             .env (0, 4.0f, 500.0f, 80.0f, 250.0f)
             .lfo (0, Wave::Triangle, 0.45f, Mode::Free)
             .mod (MS::Lfo1, posA(), 30.0f)
             .lfo (1, Wave::Sine, 5.5f).lfoFade (1, 450.0f)
             .mod (MS::Lfo2, pitchA(), 1.0f)
             .mono (VMode::Legato, 70.0f)
             .delay (ND::QuarterDotted, 35.0f, 20.0f)
             .master (-9.0f);
            add (r);
        }
        {   // 16: vowel lead: formant table through the formant filter; wheel opens the vowel.
            Recipe r ("Vowel Lead", "Lead", { "vocal", "expressive" });
            r.osc (0, Tbl::FormantVowels, 20.0f, 80.0f)
             .filter (0, Flt::Formant, 1800.0f, 15.0f)
             .env (0, 10.0f, 500.0f, 85.0f, 250.0f)
             .mod (MS::ModWheel, posA(), 60.0f)
             .mod (MS::ModWheel, cutoff1(), 30.0f)
             .lfo (1, Wave::Sine, 5.0f).lfoFade (1, 600.0f)
             .mod (MS::Lfo2, cutoff1(), 6.0f)
             .mono (VMode::Legato, 50.0f)
             .reverb (40.0f, 40.0f, 18.0f)
             .master (-9.0f);
            add (r);
        }
        {   // 17: soft sine / triangle lead with a long glide.
            Recipe r ("Glide Sine", "Lead", { "soft", "mono", "smooth" });
            r.osc (0, Tbl::BasicShapes, 0.0f, 85.0f)
             .osc (1, Tbl::BasicShapes, 33.0f, 25.0f).oscPitch (1, 1)
             .filter (0, Flt::LP12, 6000.0f, 5.0f)
             .env (0, 15.0f, 400.0f, 90.0f, 350.0f)
             .mono (VMode::Legato, 140.0f)
             .delay (ND::Quarter, 40.0f, 22.0f)
             .reverb (50.0f, 50.0f, 22.0f)
             .master (-7.0f);
            add (r);
        }

        // ==== Pad ====================================================================
        {   // 18: soft, living pad.
            Recipe r ("Soft Pad", "Pad", { "soft", "warm", "wide" });
            r.osc (0, Tbl::HarmonicStack, 20.0f, 75.0f).unison (0, 3, 12.0f)
             .osc (1, Tbl::HarmonicStack, 35.0f, 45.0f).oscPitch (1, 0, 0, -6.0f)
             .filter (0, Flt::LP24, 3000.0f, 12.0f)
             .env (0, 400.0f, 800.0f, 70.0f, 1200.0f)
             .living (40.0f, 45.0f)
             .chorus (0.5f, 45.0f, 40.0f)
             .reverb (65.0f, 60.0f, 30.0f)
             .master (-9.0f);
            add (r);
        }
        {   // 19: blooming noise table into choir, a slow Env 2 scan and lots of evolve.
            Recipe r ("Verdant Bloom", "Pad", { "evolving", "lush", "wide" });
            r.osc (0, Tbl::NoiseBloom, 0.0f, 65.0f)
             .osc (1, Tbl::Choir, 30.0f, 55.0f)
             .filter (0, Flt::LP24, 4500.0f, 15.0f)
             .env (0, 900.0f, 1500.0f, 80.0f, 2500.0f)
             .env (1, 2500.0f, 3000.0f, 60.0f, 2000.0f)
             .mod (MS::Env2, posA(), 60.0f)
             .mod (MS::Env2, posB(), 25.0f)
             .living (35.0f, 60.0f)
             .reverb (80.0f, 70.0f, 35.0f)
             .master (-10.0f);
            add (r);
        }
        {   // 20: choir of glass: wide unison choir, a drawn LFO scanning the glass table.
            Recipe r ("Choir of Glass", "Pad", { "vocal", "evolving", "airy" });
            r.osc (0, Tbl::Choir, 40.0f, 70.0f).unison (0, 6, 14.0f, 80.0f)
             .osc (1, Tbl::DigitalGlass, 10.0f, 40.0f).oscPitch (1, 1)
             .filter (0, Flt::LP12, 6000.0f, 10.0f)
             .env (0, 700.0f, 1000.0f, 80.0f, 1800.0f)
             .lfo (2, Wave::Custom, 0.2f, Mode::Free)
             .customShape (2, { { 0.0f, 0.2f, 0.0f }, { 0.3f, 0.9f, 0.5f }, { 0.55f, 0.5f, 0.0f }, { 0.8f, 1.0f, -0.3f }, { 1.0f, 0.2f, 0.4f } })
             .mod (MS::Lfo3, posB(), 50.0f)
             .reverb (75.0f, 65.0f, 32.0f)
             .master (-11.0f);
            add (r);
        }
        {   // 21: orbiting pad: the Orbit source walks position and cutoff in a slow ellipse.
            Recipe r ("Orbit Pad", "Pad", { "evolving", "slow", "wide" });
            r.osc (0, Tbl::HarmonicStack, 50.0f, 75.0f).unison (0, 4, 10.0f)
             .filter (0, Flt::LP24, 2200.0f, 20.0f)
             .env (0, 600.0f, 1000.0f, 80.0f, 1500.0f)
             .set (P::OrbitSpeed, 25.0f).set (P::OrbitShape, 55.0f)
             .mod (MS::Orbit, posA(), 35.0f)
             .mod (MS::Orbit, cutoff1(), 20.0f)
             .living (35.0f, 50.0f)
             .chorus (0.3f, 40.0f, 35.0f)
             .reverb (70.0f, 60.0f, 30.0f)
             .master (-6.0f);
            add (r);
        }
        {   // 22: string ensemble from a wide saw stack.
            Recipe r ("Warm Strings", "Pad", { "warm", "lush", "classic" });
            r.osc (0, Tbl::AnalogSaws, 35.0f, 75.0f).unison (0, 8, 18.0f, 85.0f)
             .filter (0, Flt::LP12, 2600.0f, 8.0f, 0.0f, 30.0f)
             .env (0, 550.0f, 900.0f, 85.0f, 1500.0f)
             .lfo (0, Wave::Sine, 4.8f).lfoFade (0, 800.0f)
             .mod (MS::Lfo1, pitchA(), 1.0f)
             .chorus (0.7f, 40.0f, 40.0f)
             .reverb (60.0f, 55.0f, 28.0f)
             .master (-11.0f);
            add (r);
        }
        {   // 23: dark matter: low growl table, resonant LP, S&H-stepped cutoff, phaser.
            Recipe r ("Dark Matter", "Pad", { "dark", "cinematic" });
            r.osc (0, Tbl::Growl, 70.0f, 75.0f)
             .osc (1, Tbl::SubBass, 40.0f, 40.0f).oscPitch (1, -1)
             .filter (0, Flt::LP24, 700.0f, 45.0f, 10.0f)
             .env (0, 800.0f, 1200.0f, 80.0f, 2000.0f)
             .lfo (0, Wave::SampleHold, 2.0f).lfoSmooth (0, 60.0f)
             .mod (MS::Lfo1, cutoff1(), 18.0f)
             .phaser (0.15f, 70.0f, 40.0f, 45.0f)
             .reverb (70.0f, 70.0f, 30.0f)
             .master (-9.0f);
            add (r);
        }
        {   // 24: three macros: Morph (both positions), Air (reverb), Bright (cutoff).
            Recipe r ("Macro Morph Pad", "Pad", { "evolving", "versatile", "lush" });
            r.osc (0, Tbl::Choir, 10.0f, 70.0f)
             .osc (1, Tbl::FormantVowels, 10.0f, 50.0f)
             .filter (0, Flt::LP24, 2000.0f, 15.0f)
             .env (0, 500.0f, 1000.0f, 80.0f, 1600.0f)
             .macro (0, "Morph", 25.0f).macro (1, "Air", 30.0f).macro (2, "Bright", 35.0f)
             .mod (MS::Macro1, posA(), 70.0f)
             .mod (MS::Macro1, posB(), 70.0f)
             .mod (MS::Macro2, P::ReverbMix, 50.0f)
             .mod (MS::Macro3, cutoff1(), 50.0f)
             .reverb (65.0f, 60.0f, 15.0f)
             .master (-10.0f);
            add (r);
        }
        {   // 25: breathing pad: a drawn inhale / exhale LFO on the filter, pink noise, flanger.
            Recipe r ("Breathing Pad", "Pad", { "airy", "evolving", "soft" });
            r.osc (0, Tbl::FormantVowels, 40.0f, 70.0f)
             .noise (12.0f, Noise::Pink)
             .filter (0, Flt::LP24, 1600.0f, 20.0f)
             .env (0, 700.0f, 1000.0f, 85.0f, 1800.0f)
             .lfo (0, Wave::Custom, 0.18f, Mode::Free)
             .customShape (0, { { 0.0f, 0.0f, 0.0f }, { 0.4f, 1.0f, 0.7f }, { 0.55f, 1.0f, 0.0f }, { 1.0f, 0.0f, -0.7f } })
             .mod (MS::Lfo1, cutoff1(), 35.0f)
             .flanger (0.1f, 40.0f, 30.0f, 30.0f)
             .reverb (70.0f, 60.0f, 30.0f)
             .master (-9.0f);
            add (r);
        }

        // ==== Pluck ==================================================================
        {   // 26: crystal pluck: Env 2 runs down the pluck table, dotted delay.
            Recipe r ("Crystal Pluck", "Pluck", { "bright", "short", "glassy" });
            r.osc (0, Tbl::PluckDecay, 0.0f, 80.0f)
             .filter (0, Flt::LP12, 8000.0f, 15.0f, 0.0f, 60.0f)
             .env (0, 0.5f, 650.0f, 0.0f, 350.0f)
             .env (1, 0.0f, 500.0f, 0.0f, 300.0f)
             .mod (MS::Env2, posA(), 70.0f)
             .delay (ND::EighthDotted, 35.0f, 25.0f)
             .reverb (45.0f, 45.0f, 18.0f)
             .master (-8.0f);
            add (r);
        }
        {   // 27: arpeggiated bell garden.
            Recipe r ("Arp Garden", "Pluck", { "rhythmic", "bright", "sequenced" });
            r.osc (0, Tbl::Bell, 15.0f, 75.0f)
             .filter (0, Flt::LP12, 6000.0f, 10.0f)
             .env (0, 0.5f, 300.0f, 0.0f, 250.0f)
             .arp (ArpM::UpDown, 2, ND::Sixteenth, 60.0f)
             .delay (ND::Eighth, 30.0f, 20.0f)
             .reverb (40.0f, 40.0f, 15.0f)
             .master (-8.0f);
            add (r);
        }
        {   // 28: harp with a key-snapped sympathetic memory.
            Recipe r ("Harp Memory", "Pluck", { "resonant", "plucked", "warm" });
            r.osc (0, Tbl::PluckDecay, 10.0f, 75.0f)
             .osc (1, Tbl::BasicShapes, 15.0f, 30.0f).oscPitch (1, 1)
             .filter (0, Flt::LP24, 5000.0f, 10.0f, 0.0f, 60.0f)
             .env (0, 1.0f, 900.0f, 0.0f, 700.0f)
             .key (0, ST::Major)
             .sympathetic (50.0f, 6, 10, 65.0f, 55.0f).set (P::SympSnap, 1.0f)
             .reverb (50.0f, 50.0f, 20.0f)
             .master (-9.0f);
            add (r);
        }
        {   // 29: springy pluck: the Spring source bends the pitch and the filter after each note.
            Recipe r ("Spring Pluck", "Pluck", { "bouncy", "quirky" });
            r.osc (0, Tbl::OddHarmonics, 45.0f, 80.0f)
             .filter (0, Flt::LP24, 2500.0f, 30.0f)
             .env (0, 0.5f, 500.0f, 0.0f, 300.0f)
             .set (P::SpringStiffness, 65.0f).set (P::SpringDamping, 20.0f)
             .mod (MS::Spring, pitchA(), 4.0f)
             .mod (MS::Spring, cutoff1(), 25.0f)
             .reverb (35.0f, 35.0f, 15.0f)
             .master (-8.0f);
            add (r);
        }
        {   // 30: sequenced pluck in C minor pentatonic (scale-degree steps).
            Recipe r ("Step Pluck", "Pluck", { "rhythmic", "sequenced", "dark" });
            r.osc (0, Tbl::PulseWidth, 30.0f, 80.0f)
             .filter (0, Flt::LP24, 1800.0f, 30.0f)
             .env (0, 0.5f, 220.0f, 0.0f, 150.0f)
             .env (1, 0.0f, 150.0f, 0.0f, 100.0f)
             .mod (MS::Env2, cutoff1(), 35.0f)
             .key (0, ST::MinorPentatonic)
             .seq (ND::Sixteenth, 70.0f, true,
                   { { 0, 100.0f, true, 50.0f }, { 2, 80.0f, true, 30.0f }, { 4, 90.0f, true, 70.0f }, { 1, 70.0f, true, 20.0f },
                     { 5, 100.0f, true, 90.0f }, { 3, 75.0f, true, 40.0f }, { 2, 85.0f, false, 50.0f }, { -1, 90.0f, true, 60.0f } })
             .delay (ND::EighthDotted, 30.0f, 18.0f)
             .master (-8.0f);
            add (r);
        }
        {   // 31: woody mallet: bell table through a positive comb.
            Recipe r ("Marimba Wood", "Pluck", { "percussive", "woody", "warm" });
            r.osc (0, Tbl::Bell, 40.0f, 75.0f)
             .filter (0, Flt::CombPlus, 260.0f, 35.0f)
             .filter (1, Flt::LP12, 4000.0f, 5.0f)
             .env (0, 0.5f, 380.0f, 0.0f, 250.0f)
             .reverb (30.0f, 30.0f, 15.0f)
             .master (-8.0f);
            add (r);
        }

        // ==== FX =====================================================================
        {   // 32: riser: a one-shot LFO opens the filter over the note, noise and big space.
            Recipe r ("Rising Tide", "FX", { "cinematic", "riser", "evolving" });
            r.osc (0, Tbl::NoiseBloom, 60.0f, 70.0f)
             .noise (35.0f, Noise::White)
             .filter (0, Flt::LP24, 300.0f, 35.0f)
             .env (0, 600.0f, 2000.0f, 90.0f, 2500.0f)
             .lfo (0, Wave::SawUp, 0.3f, Mode::OneShot, true)
             .mod (MS::Lfo1, cutoff1(), 70.0f)
             .delay (ND::QuarterDotted, 45.0f, 25.0f)
             .reverb (90.0f, 80.0f, 35.0f)
             .master (-10.0f);
            add (r);
        }
        {   // 33: ball drop: pitch follows the bouncing ball down to rest.
            Recipe r ("Ball Drop", "FX", { "quirky", "physical" });
            r.osc (0, Tbl::DigitalGlass, 30.0f, 75.0f)
             .filter (0, Flt::LP12, 7000.0f, 10.0f)
             .env (0, 1.0f, 1500.0f, 50.0f, 800.0f)
             .set (P::BallGravity, 45.0f).set (P::BallBounce, 80.0f)
             .mod (MS::Ball, pitchA(), 15.0f)
             .reverb (55.0f, 50.0f, 25.0f)
             .master (-9.0f);
            add (r);
        }
        {   // 34: laser zap: fast Env 2 pitch dive on an FM table, crushed.
            Recipe r ("Laser Zap", "FX", { "digital", "short", "aggressive" });
            r.osc (0, Tbl::FmSweep, 60.0f, 75.0f)
             .filter (0, Flt::LP12, 9000.0f, 10.0f)
             .env (0, 0.5f, 300.0f, 0.0f, 200.0f)
             .env (2, 0.0f, 250.0f, 0.0f, 150.0f)
             .mod (MS::Env3, pitchA(), 40.0f)
             .mod (MS::Env3, posA(), 40.0f)
             .distortion (Dist::Crush, 35.0f, 8000.0f, 40.0f)
             .master (-10.0f);
            add (r);
        }
        {   // 35: 16-step machine: Seq Mod opens the filter per step, folded drive.
            Recipe r ("Seq Machine", "FX", { "rhythmic", "glitchy", "sequenced" });
            r.osc (0, Tbl::SyncSweep, 40.0f, 75.0f)
             .filter (0, Flt::LP24, 900.0f, 40.0f)
             .env (0, 0.5f, 220.0f, 30.0f, 100.0f)
             .seq (ND::Sixteenth, 60.0f, false,
                   { { 0, 100.0f, true, 90.0f }, { 0, 60.0f, true, 20.0f }, { 12, 80.0f, true, 60.0f }, { 0, 50.0f, false, 10.0f },
                     { 7, 100.0f, true, 100.0f }, { 0, 60.0f, true, 30.0f }, { -5, 75.0f, true, 50.0f }, { 0, 50.0f, true, 15.0f },
                     { 0, 100.0f, true, 85.0f }, { 3, 60.0f, true, 25.0f }, { 12, 90.0f, true, 70.0f }, { 0, 50.0f, false, 10.0f },
                     { 10, 100.0f, true, 95.0f }, { 0, 60.0f, true, 30.0f }, { 5, 80.0f, true, 55.0f }, { -12, 90.0f, true, 40.0f } })
             .mod (MS::SeqMod, cutoff1(), 45.0f)
             .distortion (Dist::Fold, 30.0f, 6000.0f, 45.0f)
             .master (-8.0f);
            add (r);
        }
        {   // 36: sparkling random arp in a high register.
            Recipe r ("Arp Rain", "FX", { "sparkling", "random", "rhythmic" });
            r.osc (0, Tbl::DigitalGlass, 50.0f, 70.0f).oscPitch (0, 1)
             .filter (0, Flt::HP12, 250.0f, 10.0f)
             .env (0, 0.5f, 450.0f, 35.0f, 300.0f)
             .arp (ArpM::Random, 3, ND::SixteenthTriplet, 50.0f)
             .delay (ND::EighthTriplet, 45.0f, 30.0f)
             .reverb (70.0f, 65.0f, 30.0f)
             .master (-5.0f);
            add (r);
        }

        // ==== Texture ================================================================
        {   // 37: living drone: everything alive, a notch drifting through it.
            Recipe r ("Living Drone", "Texture", { "evolving", "drone", "dark" });
            r.osc (0, Tbl::Choir, 60.0f, 70.0f)
             .osc (1, Tbl::HarmonicStack, 70.0f, 45.0f).oscPitch (1, -1)
             .filter (0, Flt::Notch, 1200.0f, 40.0f)
             .env (0, 1200.0f, 2000.0f, 90.0f, 3000.0f)
             .lfo (0, Wave::Sine, 0.07f, Mode::Free)
             .mod (MS::Lfo1, cutoff1(), 30.0f)
             .living (80.0f, 90.0f)
             .reverb (85.0f, 75.0f, 35.0f)
             .master (-10.0f);
            add (r);
        }
        {   // 38: noise garden: band-passed bloom with S&H position steps and a phaser.
            Recipe r ("Noise Garden", "Texture", { "noisy", "airy", "evolving" });
            r.osc (0, Tbl::NoiseBloom, 80.0f, 70.0f)
             .osc (1, Tbl::NoiseBloom, 40.0f, 50.0f).oscPitch (1, 1)
             .noise (20.0f, Noise::Pink)
             .filter (0, Flt::BP24, 1800.0f, 30.0f)
             .env (0, 500.0f, 1500.0f, 80.0f, 2000.0f)
             .lfo (1, Wave::SampleHold, 3.0f).lfoSmooth (1, 40.0f)
             .mod (MS::Lfo2, posB(), 40.0f)
             .phaser (0.2f, 60.0f, 35.0f, 40.0f)
             .reverb (75.0f, 65.0f, 32.0f)
             .master (-7.0f);
            add (r);
        }
        {   // 39: halo of sympathetic strings around a soft sine.
            Recipe r ("Sympathetic Halo", "Texture", { "resonant", "ambient", "soft" });
            r.osc (0, Tbl::BasicShapes, 5.0f, 75.0f)
             .filter (0, Flt::LP12, 5000.0f, 5.0f)
             .env (0, 80.0f, 1200.0f, 70.0f, 1500.0f)
             .sympathetic (70.0f, 8, 12, 75.0f, 60.0f)
             .reverb (70.0f, 65.0f, 28.0f)
             .master (-11.0f);
            add (r);
        }
        {   // 40: mirror shards: mirrored glass, a drawn LFO on the warp, high-passed and flanged.
            Recipe r ("Mirror Shards", "Texture", { "glassy", "glitchy", "bright" });
            r.osc (0, Tbl::DigitalGlass, 70.0f, 75.0f).warp (0, Warp::Mirror, 30.0f)
             .osc (1, Tbl::Bell, 60.0f, 35.0f).warp (1, Warp::Remap, 40.0f)
             .filter (0, Flt::HP24, 350.0f, 15.0f)
             .env (0, 30.0f, 1000.0f, 75.0f, 1200.0f)
             .lfo (3, Wave::Custom, 1.5f)
             .customShape (3, { { 0.0f, 0.5f, 0.0f }, { 0.2f, 1.0f, 0.8f }, { 0.25f, 0.1f, 0.0f }, { 0.6f, 0.7f, -0.6f }, { 1.0f, 0.5f, 0.0f } })
             .mod (MS::Lfo4, warpA(), 35.0f)
             .flanger (0.3f, 55.0f, 50.0f, 40.0f)
             .reverb (55.0f, 55.0f, 22.0f)
             .master (-11.0f);
            add (r);
        }
        {   // 41: tidal pad-texture: aftertouch and wheel play the bloom; a performance texture.
            Recipe r ("Tidal Glass", "Texture", { "expressive", "evolving", "wide" });
            r.osc (0, Tbl::DigitalGlass, 20.0f, 65.0f).unison (0, 5, 20.0f, 90.0f)
             .osc (1, Tbl::Choir, 50.0f, 45.0f)
             .filter (0, Flt::LP24, 2400.0f, 20.0f)
             .env (0, 900.0f, 1500.0f, 85.0f, 2200.0f)
             .mod (MS::Aftertouch, cutoff1(), 35.0f)
             .mod (MS::ModWheel, posA(), 50.0f)
             .living (45.0f, 55.0f)
             .chorus (0.25f, 50.0f, 35.0f)
             .reverb (80.0f, 70.0f, 33.0f)
             .master (-11.0f);
            add (r);
        }

        return bank;
    }
}

const std::vector<Patch>& factoryPresets()
{
    static const std::vector<Patch> bank = build();
    return bank;
}

const std::vector<std::string>& factoryCategories()
{
    static const std::vector<std::string> categories { "Bass", "Lead", "Pad", "Pluck", "Keys", "FX", "Texture" };
    return categories;
}

} // namespace verdant
