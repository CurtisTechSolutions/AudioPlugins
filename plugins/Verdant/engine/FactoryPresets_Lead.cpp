// Factory bank: Lead.

#include "FactoryPresets_Recipes.h"

namespace verdant::factory
{

void addLeadPresets (std::vector<Patch>& bank)
{
    auto add = [&bank] (Recipe& r) { bank.push_back (r.p); };

    {   // Classic detuned saw lead.
        Recipe r ("Saw Lead", "Lead", { "bright", "classic", "mono" });
        r.osc (0, Tbl::AnalogSaws, 50.0f, 75.0f).unison (0, 3, 12.0f, 50.0f)
         .osc (1, Tbl::AnalogSaws, 50.0f, 55.0f).oscPitch (1, 0, 0, 7.0f)
         .filter (0, Flt::LP24, 5000.0f, 25.0f, 0.0f, 40.0f)
         .env (0, 2.0f, 300.0f, 85.0f, 200.0f)
         .mono (VMode::Legato, 30.0f)
         .macro (0, "Brightness", 0.0f).macro (1, "Echo", 0.0f)
         .mod (MS::Macro1, cutoff1(), 25.0f)
         .mod (MS::Macro2, P::DelayMix, 30.0f)
         .delay (ND::EighthDotted, 30.0f, 18.0f)
         .master (-7.0f);
        add (r);
    }
    {   // Hard-sync scream: Env 2 sweeps the sync table, driven and delayed.
        Recipe r ("Sync Scream", "Lead", { "aggressive", "bright", "mono" });
        r.osc (0, Tbl::SyncSweep, 20.0f, 75.0f).warp (0, Warp::Sync, 40.0f)
         .filter (0, Flt::LP12, 7000.0f, 20.0f)
         .env (0, 1.0f, 400.0f, 80.0f, 200.0f)
         .env (1, 5.0f, 700.0f, 20.0f, 300.0f)
         .mod (MS::Env2, posA(), 45.0f)
         .mod (MS::Env2, warpA(), 30.0f)
         .mono (VMode::Mono, 10.0f)
         .macro (0, "Scream", 0.0f).macro (1, "Drive", 0.0f)
         .mod (MS::Macro1, warpA(), 35.0f)
         .mod (MS::Macro2, P::DistDrive, 40.0f)
         .distortion (Dist::Hard, 30.0f, 7000.0f, 50.0f)
         .delay (ND::Eighth, 25.0f, 15.0f)
         .master (-1.0f);
        add (r);
    }
    {   // PWM hero lead: LFO-scanned pulse table, delayed vibrato, long glide.
        Recipe r ("Pulse Hero", "Lead", { "vintage", "mono", "smooth" });
        r.osc (0, Tbl::PulseWidth, 40.0f, 80.0f)
         .filter (0, Flt::LP24, 3800.0f, 20.0f, 0.0f, 50.0f)
         .env (0, 4.0f, 500.0f, 80.0f, 250.0f)
         .lfo (0, Wave::Triangle, 0.45f, Mode::Free)
         .mod (MS::Lfo1, posA(), 30.0f)
         .lfo (1, Wave::Sine, 5.5f).lfoFade (1, 450.0f)
         .mod (MS::Lfo2, pitchA(), 1.0f)
         .mono (VMode::Legato, 70.0f)
         .macro (0, "PWM", 0.0f).macro (1, "Vibrato", 0.0f)
         .mod (MS::Macro1, lfoRate (0), 30.0f)
         .mod (MS::Macro2, pitchA(), 1.0f)
         .delay (ND::QuarterDotted, 35.0f, 20.0f)
         .master (-3.5f);
        add (r);
    }
    {   // Vowel lead: formant table through the formant filter; wheel opens the vowel.
        Recipe r ("Vowel Lead", "Lead", { "vocal", "expressive", "mono" });
        r.osc (0, Tbl::FormantVowels, 20.0f, 80.0f)
         .filter (0, Flt::Formant, 1800.0f, 15.0f)
         .env (0, 10.0f, 500.0f, 85.0f, 250.0f)
         .mod (MS::ModWheel, posA(), 60.0f)
         .mod (MS::ModWheel, cutoff1(), 30.0f)
         .lfo (1, Wave::Sine, 5.0f).lfoFade (1, 600.0f)
         .mod (MS::Lfo2, cutoff1(), 6.0f)
         .mono (VMode::Legato, 50.0f)
         .macro (0, "Vowel", 0.0f).macro (1, "Space", 0.0f)
         .mod (MS::Macro1, posA(), 45.0f)
         .mod (MS::Macro2, P::ReverbMix, 30.0f)
         .reverb (40.0f, 40.0f, 18.0f)
         .master (4.5f);
        add (r);
    }
    {   // Soft sine / triangle lead with a long glide.
        Recipe r ("Glide Sine", "Lead", { "soft", "mono", "smooth", "clean" });
        r.osc (0, Tbl::BasicShapes, 0.0f, 85.0f)
         .osc (1, Tbl::BasicShapes, 33.0f, 25.0f).oscPitch (1, 1)
         .filter (0, Flt::LP12, 6000.0f, 5.0f)
         .env (0, 15.0f, 400.0f, 90.0f, 350.0f)
         .mono (VMode::Legato, 140.0f)
         .macro (0, "Shape", 0.0f).macro (1, "Space", 0.0f)
         .mod (MS::Macro1, posA(), 30.0f)
         .mod (MS::Macro2, P::ReverbMix, 30.0f)
         .delay (ND::Quarter, 40.0f, 22.0f)
         .reverb (50.0f, 50.0f, 22.0f)
         .master (-7.0f);
        add (r);
    }
    {   // Supersaw anthem: 16-voice saw wall plus an octave, wide and bright, for big toplines.
        Recipe r ("Supersaw Anthem", "Lead", { "wide", "bright", "modern", "detuned" });
        r.osc (0, Tbl::AnalogSaws, 60.0f, 75.0f).unison (0, 16, 28.0f, 100.0f, 70.0f)
         .osc (1, Tbl::AnalogSaws, 60.0f, 35.0f).unison (1, 4, 15.0f, 80.0f).oscPitch (1, 1)
         .filter (0, Flt::LP12, 9000.0f, 10.0f)
         .env (0, 5.0f, 300.0f, 90.0f, 400.0f)
         .mono (VMode::Legato, 15.0f)
         .macro (0, "Width", 0.0f).macro (1, "Brightness", 0.0f).macro (2, "Space", 0.0f)
         .mod (MS::Macro1, detuneA(), 30.0f)
         .mod (MS::Macro2, cutoff1(), 20.0f)
         .mod (MS::Macro3, P::ReverbMix, 30.0f)
         .delay (ND::EighthDotted, 30.0f, 15.0f)
         .reverb (50.0f, 45.0f, 15.0f)
         .master (-4.5f);
        add (r);
    }
    {   // Chiptune hero: bare square, fast delayed vibrato, an octave of pitch bend for chip-style dives.
        Recipe r ("Chiptune Hero", "Lead", { "retro", "digital", "mono" });
        r.osc (0, Tbl::PulseWidth, 0.0f, 80.0f).hardPhase (0)
         .filterOff (0)
         .env (0, 0.5f, 100.0f, 90.0f, 60.0f)
         .lfo (0, Wave::Triangle, 7.0f).lfoFade (0, 250.0f)
         .mod (MS::Lfo1, pitchA(), 0.5f)
         .mono (VMode::Mono, 0.0f)
         .bendRange (12)
         .macro (0, "Duty", 0.0f).macro (1, "Vibrato", 0.0f)
         .mod (MS::Macro1, posA(), 50.0f)
         .mod (MS::Macro2, lfoRate (0), 20.0f)
         .eq (0.0f, 0.0f, 1000.0f, -4.0f)
         .master (-8.0f);
        add (r);
    }
    {   // Ladder hero: saw + square + sub into a driven ladder with glide; the classic fat mono solo.
        Recipe r ("Ladder Hero", "Lead", { "warm", "vintage", "mono", "classic" });
        r.osc (0, Tbl::BasicShapes, 39.0f, 75.0f)
         .osc (1, Tbl::BasicShapes, 61.0f, 55.0f).oscPitch (1, 0, 0, -5.0f)
         .sub (30.0f, Sub::Square, 1, Route::Filter1)
         .filter (0, Flt::Ladder, 1800.0f, 35.0f, 30.0f, 50.0f)
         .env (0, 3.0f, 600.0f, 80.0f, 250.0f)
         .env (1, 5.0f, 600.0f, 35.0f, 300.0f)
         .mod (MS::Env2, cutoff1(), 25.0f)
         .mono (VMode::Legato, 60.0f)
         .macro (0, "Cutoff", 0.0f).macro (1, "Emphasis", 0.0f)
         .mod (MS::Macro1, cutoff1(), 30.0f)
         .mod (MS::Macro2, res1(), 35.0f)
         .master (-1.5f);
        add (r);
    }
    {   // Ether theremin: a pure tone with deep delayed vibrato, very long glide, a wide hall.
        Recipe r ("Ether Theremin", "Lead", { "soft", "expressive", "cinematic", "mono" });
        r.osc (0, Tbl::BasicShapes, 3.0f, 85.0f)
         .filterOff (0)
         .env (0, 30.0f, 300.0f, 95.0f, 500.0f)
         .lfo (0, Wave::Sine, 6.0f).lfoFade (0, 900.0f)
         .mod (MS::Lfo1, pitchA(), 1.2f)
         .mono (VMode::Legato, 350.0f)
         .bendRange (12)
         .macro (0, "Vibrato", 0.0f).macro (1, "Space", 0.0f)
         .mod (MS::Macro1, pitchA(), 1.0f)
         .mod (MS::Macro2, P::ReverbMix, 30.0f)
         .reverb (75.0f, 65.0f, 25.0f, 30.0f, 30.0f)
         .master (-7.5f);
        add (r);
    }
    {   // FM shriek: Osc B a fifth above frequency-modulates A; drive and an Env 2 index bloom.
        Recipe r ("FM Shriek", "Lead", { "fm", "aggressive", "metallic", "mono" });
        r.osc (0, Tbl::BasicShapes, 6.0f, 80.0f).warp (0, Warp::FmOsc, 45.0f)
         .osc (1, Tbl::BasicShapes, 0.0f, 30.0f).oscPitch (1, 1, 7)
         .filter (0, Flt::LP24, 6000.0f, 15.0f)
         .env (0, 2.0f, 500.0f, 85.0f, 200.0f)
         .env (1, 150.0f, 800.0f, 50.0f, 300.0f)
         .mod (MS::Env2, warpA(), 25.0f)
         .mono (VMode::Mono, 20.0f)
         .macro (0, "Index", 0.0f).macro (1, "Drive", 0.0f)
         .mod (MS::Macro1, warpA(), 30.0f)
         .mod (MS::Macro2, P::DistDrive, 40.0f)
         .distortion (Dist::Hard, 20.0f, 6000.0f, 40.0f)
         .delay (ND::Eighth, 30.0f, 12.0f)
         .master (-6.0f);
        add (r);
    }
    {   // Glass whistle: a bent triangle with breath, high-passed so only the whistle is left.
        Recipe r ("Glass Whistle", "Lead", { "airy", "soft", "mono" });
        r.osc (0, Tbl::BasicShapes, 6.0f, 80.0f).oscPitch (0, 1).warp (0, Warp::Bend, 25.0f)
         .noise (6.0f, Noise::Blue)
         .filter (0, Flt::HP12, 500.0f, 10.0f)
         .env (0, 40.0f, 400.0f, 85.0f, 300.0f)
         .lfo (0, Wave::Sine, 5.2f).lfoFade (0, 500.0f)
         .mod (MS::Lfo1, pitchA(), 0.6f)
         .mono (VMode::Legato, 90.0f)
         .macro (0, "Breath", 0.0f).macro (1, "Bend", 0.0f)
         .mod (MS::Macro1, P::NoiseLevel, 20.0f)
         .mod (MS::Macro2, warpA(), 35.0f)
         .reverb (55.0f, 50.0f, 20.0f)
         .master (4.5f);
        add (r);
    }
    {   // Solo choir lead: one sung voice, legato, chorused, the vowel on the mod wheel.
        Recipe r ("Solo Choir Lead", "Lead", { "vocal", "lush", "smooth" });
        r.osc (0, Tbl::Choir, 15.0f, 80.0f).unison (0, 3, 10.0f, 40.0f)
         .filter (0, Flt::LP24, 3500.0f, 10.0f)
         .env (0, 15.0f, 500.0f, 90.0f, 400.0f)
         .lfo (0, Wave::Sine, 5.0f).lfoFade (0, 700.0f)
         .mod (MS::Lfo1, pitchA(), 0.5f)
         .mod (MS::ModWheel, posA(), 50.0f)
         .mono (VMode::Legato, 80.0f)
         .macro (0, "Vowel", 0.0f).macro (1, "Ensemble", 0.0f)
         .mod (MS::Macro1, posA(), 45.0f)
         .mod (MS::Macro2, P::ChorusMix, 40.0f)
         .chorus (0.4f, 35.0f, 30.0f)
         .reverb (60.0f, 55.0f, 22.0f)
         .master (-0.5f);
        add (r);
    }
    {   // Overdrive organ lead: a cranked organ through a tube amp and a fast rotor phaser.
        Recipe r ("Overdrive Organ", "Lead", { "dirty", "vintage", "aggressive" });
        r.osc (0, Tbl::Organ, 80.0f, 75.0f)
         .osc (1, Tbl::Organ, 50.0f, 45.0f).oscPitch (1, -1)
         .filter (0, Flt::LP12, 5000.0f, 5.0f)
         .env (0, 3.0f, 100.0f, 100.0f, 80.0f)
         .mono (VMode::Legato, 20.0f)
         .macro (0, "Overdrive", 0.0f).macro (1, "Rotor", 0.0f)
         .mod (MS::Macro1, P::DistDrive, 45.0f)
         .mod (MS::Macro2, P::PhaserRate, 40.0f)
         .distortion (Dist::Tube, 55.0f, 4500.0f, 85.0f)
         .phaser (2.5f, 35.0f, 15.0f, 35.0f, 0)
         .master (0.0f);
        add (r);
    }
    {   // Neuro lead: a remapped growl table with a synced LFO on the position and the filter.
        Recipe r ("Neuro Lead", "Lead", { "growl", "aggressive", "rhythmic", "modern" });
        r.osc (0, Tbl::Growl, 45.0f, 80.0f).warp (0, Warp::Remap, 30.0f)
         .filter (0, Flt::LP24, 2500.0f, 35.0f, 20.0f)
         .env (0, 2.0f, 400.0f, 85.0f, 150.0f)
         .lfo (0, Wave::SawDown, 4.0f).lfoSync (0, ND::Sixteenth)
         .mod (MS::Lfo1, posA(), 35.0f)
         .mod (MS::Lfo1, cutoff1(), 20.0f)
         .mono (VMode::Mono, 10.0f)
         .macro (0, "Rate", 0.0f).macro (1, "Remap", 0.0f)
         .mod (MS::Macro1, lfoRate (0), 30.0f)
         .mod (MS::Macro2, warpA(), 40.0f)
         .distortion (Dist::Fold, 25.0f, 7000.0f, 40.0f)
         .master (-5.0f);
        add (r);
    }
    {   // Chime lead: bell on top of a soft saw, sustained, with a sparkling delay.
        Recipe r ("Chime Lead", "Lead", { "glassy", "bright", "metallic" });
        r.osc (0, Tbl::Bell, 25.0f, 70.0f).oscPitch (0, 1)
         .osc (1, Tbl::AnalogSaws, 20.0f, 50.0f)
         .filter (0, Flt::LP12, 5500.0f, 10.0f)
         .env (0, 2.0f, 600.0f, 75.0f, 350.0f)
         .mono (VMode::Legato, 40.0f)
         .macro (0, "Chime", 0.0f).macro (1, "Echo", 0.0f)
         .mod (MS::Macro1, posA(), 40.0f)
         .mod (MS::Macro2, P::DelayFeedback, 35.0f)
         .delay (ND::EighthDotted, 40.0f, 20.0f)
         .reverb (45.0f, 45.0f, 15.0f)
         .master (-3.0f);
        add (r);
    }
    {   // Brass squeeze: a squeezed harmonic stack with a slow brassy filter swell on every note.
        Recipe r ("Brass Squeeze", "Lead", { "warm", "expressive", "vintage" });
        r.osc (0, Tbl::HarmonicStack, 60.0f, 75.0f).warp (0, Warp::Squeeze, -25.0f).unison (0, 2, 8.0f, 30.0f)
         .filter (0, Flt::LP24, 900.0f, 15.0f, 10.0f, 60.0f)
         .env (0, 25.0f, 400.0f, 85.0f, 250.0f)
         .env (1, 80.0f, 500.0f, 40.0f, 300.0f)
         .mod (MS::Env2, cutoff1(), 35.0f)
         .mod (MS::Velocity, cutoff1(), 15.0f)
         .mono (VMode::Legato, 35.0f)
         .macro (0, "Swell", 0.0f).macro (1, "Squeeze", 0.0f)
         .mod (MS::Macro1, cutoff1(), 25.0f)
         .mod (MS::Macro2, warpA(), 40.0f)
         .reverb (40.0f, 40.0f, 15.0f)
         .master (-1.0f);
        add (r);
    }
    {   // Mirror flute: a mirrored sine, a breath of band-passed noise, gentle tongued attack.
        Recipe r ("Mirror Flute", "Lead", { "airy", "organic", "soft" });
        r.osc (0, Tbl::BasicShapes, 4.0f, 80.0f).warp (0, Warp::Mirror, 20.0f)
         .noise (10.0f, Noise::Pink, Route::Filter2)
         .filter (0, Flt::LP12, 4500.0f, 5.0f)
         .filter (1, Flt::BP12, 2000.0f, 20.0f, 0.0f, 100.0f)
         .parallel()
         .env (0, 45.0f, 300.0f, 90.0f, 250.0f)
         .lfo (0, Wave::Sine, 4.8f).lfoFade (0, 600.0f)
         .mod (MS::Lfo1, levelA(), 6.0f)
         .mono (VMode::Legato, 40.0f)
         .macro (0, "Breath", 0.0f).macro (1, "Overblow", 0.0f)
         .mod (MS::Macro1, P::NoiseLevel, 25.0f)
         .mod (MS::Macro2, warpA(), 40.0f)
         .reverb (45.0f, 45.0f, 18.0f)
         .master (-5.0f);
        add (r);
    }
    {   // Crystal remap lead: digital glass with a drawn LFO bending the remap curve every bar.
        Recipe r ("Crystal Remap", "Lead", { "digital", "glassy", "evolving" });
        r.osc (0, Tbl::DigitalGlass, 35.0f, 80.0f).warp (0, Warp::Remap, 10.0f)
         .filter (0, Flt::LP24, 6000.0f, 20.0f)
         .env (0, 5.0f, 500.0f, 80.0f, 300.0f)
         .lfo (0, Wave::Custom, 0.5f).lfoSync (0, ND::Whole)
         .customShape (0, { { 0.0f, 0.5f, 0.0f }, { 0.25f, 1.0f, -0.6f }, { 0.5f, 0.2f, 0.5f }, { 0.75f, 0.7f, 0.0f }, { 1.0f, 0.5f, 0.0f } })
         .mod (MS::Lfo1, warpA(), 35.0f)
         .mono (VMode::Legato, 30.0f)
         .macro (0, "Remap", 0.0f).macro (1, "Echo", 0.0f)
         .mod (MS::Macro1, warpA(), 30.0f)
         .mod (MS::Macro2, P::DelayMix, 30.0f)
         .delay (ND::Quarter, 35.0f, 15.0f)
         .master (-2.5f);
        add (r);
    }
    {   // Harmonic rise: Env 2 climbs the harmonic stack over a second, so held notes open up.
        Recipe r ("Harmonic Rise", "Lead", { "evolving", "bright", "smooth" });
        r.osc (0, Tbl::HarmonicStack, 0.0f, 80.0f)
         .osc (1, Tbl::HarmonicStack, 0.0f, 50.0f).oscPitch (1, 0, 0, 6.0f)
         .filter (0, Flt::LP12, 7000.0f, 10.0f)
         .env (0, 8.0f, 500.0f, 90.0f, 300.0f)
         .env (1, 1000.0f, 1000.0f, 100.0f, 500.0f, -50.0f)
         .mod (MS::Env2, posA(), 70.0f)
         .mod (MS::Env2, posB(), 50.0f)
         .mono (VMode::Legato, 50.0f)
         .macro (0, "Rise Time", 0.0f).macro (1, "Space", 0.0f)
         .mod (MS::Macro1, envAttack (1), 30.0f)
         .mod (MS::Macro2, P::ReverbMix, 30.0f)
         .reverb (45.0f, 45.0f, 18.0f)
         .master (-3.0f);
        add (r);
    }
    {   // Reed clarinet: odd harmonics only, a velocity-opened low-pass, breath and a woody body.
        Recipe r ("Reed Clarinet", "Lead", { "hollow", "organic", "soft", "mono" });
        r.osc (0, Tbl::OddHarmonics, 25.0f, 80.0f)
         .noise (4.0f, Noise::Pink)
         .filter (0, Flt::LP12, 1800.0f, 15.0f, 0.0f, 80.0f)
         .env (0, 30.0f, 300.0f, 90.0f, 200.0f)
         .mod (MS::Velocity, cutoff1(), 20.0f)
         .mod (MS::Aftertouch, cutoff1(), 20.0f)
         .lfo (0, Wave::Sine, 4.5f).lfoFade (0, 800.0f)
         .mod (MS::Lfo1, cutoff1(), 3.0f)
         .mono (VMode::Legato, 30.0f)
         .macro (0, "Reed", 0.0f).macro (1, "Breath", 0.0f)
         .mod (MS::Macro1, posA(), 40.0f)
         .mod (MS::Macro2, P::NoiseLevel, 20.0f)
         .eq (-2.0f, 3.0f, 1200.0f, -3.0f)
         .reverb (35.0f, 40.0f, 15.0f)
         .master (-7.0f);
        add (r);
    }
    {   // Acid line lead: a squelchy square an octave up, accent on velocity, slides and a dotted echo.
        Recipe r ("Acid Line Lead", "Lead", { "acid", "resonant", "mono" });
        r.osc (0, Tbl::BasicShapes, 70.0f, 80.0f)
         .filter (0, Flt::Ladder, 900.0f, 65.0f, 20.0f, 40.0f)
         .env (0, 1.0f, 300.0f, 75.0f, 100.0f)
         .env (1, 0.0f, 300.0f, 15.0f, 150.0f)
         .mod (MS::Env2, cutoff1(), 40.0f)
         .mod (MS::Velocity, res1(), 15.0f)
         .mono (VMode::Legato, 70.0f)
         .macro (0, "Cutoff", 0.0f).macro (1, "Decay", 0.0f)
         .mod (MS::Macro1, cutoff1(), 25.0f)
         .mod (MS::Macro2, envDecay (1), 35.0f)
         .distortion (Dist::Soft, 30.0f, 6000.0f, 50.0f)
         .delay (ND::EighthDotted, 40.0f, 20.0f)
         .master (-1.5f);
        add (r);
    }
    {   // Boing lead: the Spring source wobbles the pitch after each attack, a cartoon vibrato.
        Recipe r ("Boing Lead", "Lead", { "bouncy", "physical", "retro" });
        r.osc (0, Tbl::OddHarmonics, 50.0f, 80.0f)
         .filter (0, Flt::LP24, 3000.0f, 20.0f)
         .env (0, 2.0f, 400.0f, 80.0f, 200.0f)
         .spring (45.0f, 15.0f)
         .mod (MS::Spring, pitchA(), 1.5f)
         .mod (MS::Spring, cutoff1(), 15.0f)
         .mono (VMode::Mono, 0.0f)
         .macro (0, "Boing", 0.0f).macro (1, "Tone", 0.0f)
         .mod (MS::Macro1, cutoff1(), 20.0f)
         .mod (MS::Macro2, posA(), 35.0f)
         .delay (ND::Eighth, 25.0f, 12.0f)
         .master (-6.5f);
        add (r);
    }
    {   // Comet lead: the Orbit source traces an ellipse through position and pan while you hold.
        Recipe r ("Comet Lead", "Lead", { "evolving", "wide", "physical" });
        r.osc (0, Tbl::SyncSweep, 30.0f, 75.0f)
         .osc (1, Tbl::DigitalGlass, 20.0f, 40.0f).oscPitch (1, 1)
         .filter (0, Flt::LP24, 5000.0f, 20.0f)
         .env (0, 10.0f, 500.0f, 85.0f, 400.0f)
         .orbit (55.0f, 70.0f)
         .mod (MS::Orbit, posA(), 30.0f)
         .mod (MS::Orbit, panA(), 40.0f)
         .mod (MS::Orbit, panB(), -40.0f)
         .mono (VMode::Legato, 45.0f)
         .macro (0, "Tail", 0.0f).macro (1, "Brightness", 0.0f)
         .mod (MS::Macro1, P::DelayFeedback, 35.0f)
         .mod (MS::Macro2, cutoff1(), 25.0f)
         .flanger (0.2f, 40.0f, 30.0f, 20.0f)
         .delay (ND::QuarterDotted, 40.0f, 20.0f)
         .reverb (55.0f, 50.0f, 18.0f)
         .master (1.5f);
        add (r);
    }
    {   // Fold lead: a pulse wave-folded hard, then tamed by a tone control; buzzy and present.
        Recipe r ("Fold Lead", "Lead", { "dirty", "digital", "mono" });
        r.osc (0, Tbl::PulseWidth, 25.0f, 80.0f)
         .filter (0, Flt::LP24, 4500.0f, 15.0f)
         .env (0, 2.0f, 300.0f, 85.0f, 150.0f)
         .lfo (0, Wave::Triangle, 0.3f, Mode::Free)
         .mod (MS::Lfo1, P::DistDrive, 15.0f)
         .mono (VMode::Mono, 15.0f)
         .macro (0, "Fold", 0.0f).macro (1, "Tone", 0.0f)
         .mod (MS::Macro1, P::DistDrive, 40.0f)
         .mod (MS::Macro2, P::DistTone, 35.0f)
         .distortion (Dist::Fold, 40.0f, 5000.0f, 75.0f)
         .master (-5.5f);
        add (r);
    }
    {   // Shout lead: vowels swept from "o" to "a" by Env 2 on every note, driven like a shouted line.
        Recipe r ("Shout Lead", "Lead", { "vocal", "aggressive", "mono" });
        r.osc (0, Tbl::FormantVowels, 70.0f, 80.0f).unison (0, 3, 12.0f, 50.0f)
         .filter (0, Flt::BP24, 1200.0f, 15.0f, 10.0f, 60.0f)
         .filterMix (0, 75.0f)
         .env (0, 5.0f, 400.0f, 85.0f, 200.0f)
         .env (1, 0.0f, 450.0f, 0.0f, 200.0f)
         .mod (MS::Env2, posA(), -40.0f)
         .mod (MS::Env2, cutoff1(), 20.0f)
         .mono (VMode::Mono, 25.0f)
         .macro (0, "Shout", 0.0f).macro (1, "Drive", 0.0f)
         .mod (MS::Macro1, envDecay (1), 35.0f)
         .mod (MS::Macro2, P::DistDrive, 40.0f)
         .distortion (Dist::Soft, 35.0f, 5000.0f, 60.0f)
         .master (5.5f);
        add (r);
    }
}

} // namespace verdant::factory
