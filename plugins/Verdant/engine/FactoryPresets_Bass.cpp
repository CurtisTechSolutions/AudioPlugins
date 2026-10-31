// Factory bank: Bass (previewed and levelled on C2).

#include "FactoryPresets_Recipes.h"

namespace verdant::factory
{

void addBassPresets (std::vector<Patch>& bank)
{
    auto add = [&bank] (Recipe& r) { bank.push_back (r.p); };

    {   // Clean sub bass, mono with a short glide; Weight adds the sine sub, Tone opens the top.
        Recipe r ("Deep Sub", "Bass", { "deep", "sub", "mono", "clean" });
        r.osc (0, Tbl::SubBass, 10.0f, 70.0f)
         .sub (65.0f, Sub::Sine, 1)
         .filter (0, Flt::LP24, 450.0f, 10.0f)
         .env (0, 3.0f, 400.0f, 85.0f, 150.0f)
         .mono (VMode::Mono, 40.0f)
         .living (10.0f, 0.0f)
         .macro (0, "Tone", 0.0f).macro (1, "Weight", 0.0f)
         .mod (MS::Macro1, cutoff1(), 30.0f)
         .mod (MS::Macro2, P::SubLevel, 30.0f)
         .master (-8.0f);
        add (r);
    }
    {   // Growl bass: tempo-synced LFO scanning the growl table, ladder filter, tube drive.
        Recipe r ("Growl Bass", "Bass", { "aggressive", "growl", "rhythmic", "mono" });
        r.osc (0, Tbl::Growl, 25.0f, 75.0f)
         .sub (45.0f, Sub::Sine, 1)
         .filter (0, Flt::Ladder, 900.0f, 40.0f, 30.0f)
         .env (0, 2.0f, 300.0f, 80.0f, 120.0f)
         .lfo (0, Wave::Triangle, 2.0f).lfoSync (0, ND::Eighth)
         .mod (MS::Lfo1, posA(), 40.0f)
         .mod (MS::Lfo1, cutoff1(), 20.0f)
         .mono (VMode::Mono, 25.0f)
         .macro (0, "Growl", 0.0f).macro (1, "Drive", 0.0f)
         .mod (MS::Macro1, lfoRate (0), 25.0f)
         .mod (MS::Macro2, P::DistDrive, 45.0f)
         .distortion (Dist::Tube, 40.0f, 5000.0f, 70.0f)
         .master (-3.5f);
        add (r);
    }
    {   // Classic analog bass: two saws, ladder, filter envelope.
        Recipe r ("Analog Bass", "Bass", { "warm", "classic", "mono" });
        r.osc (0, Tbl::AnalogSaws, 15.0f, 75.0f)
         .osc (1, Tbl::AnalogSaws, 15.0f, 60.0f).oscPitch (1, 0, 0, -9.0f)
         .sub (40.0f, Sub::Square, 1)
         .filter (0, Flt::Ladder, 500.0f, 30.0f, 15.0f, 30.0f)
         .env (0, 1.0f, 500.0f, 70.0f, 120.0f)
         .env (1, 0.0f, 260.0f, 10.0f, 150.0f)
         .mod (MS::Env2, cutoff1(), 45.0f)
         .mono (VMode::Legato, 30.0f)
         .macro (0, "Cutoff", 0.0f).macro (1, "Env Depth", 0.0f)
         .mod (MS::Macro1, cutoff1(), 35.0f)
         .mod (MS::Macro2, envDecay (1), 30.0f)
         .master (-4.0f);
        add (r);
    }
    {   // Reese: a wide unison saw stack and a slowly breathing filter.
        Recipe r ("Reese Unison", "Bass", { "dark", "wide", "detuned", "mono" });
        r.osc (0, Tbl::AnalogSaws, 30.0f, 80.0f).unison (0, 7, 25.0f, 60.0f, 85.0f)
         .sub (45.0f, Sub::Sine, 1)
         .filter (0, Flt::LP24, 1100.0f, 20.0f, 10.0f)
         .env (0, 3.0f, 500.0f, 85.0f, 200.0f)
         .lfo (1, Wave::Sine, 0.25f, Mode::Free)
         .mod (MS::Lfo2, cutoff1(), 15.0f)
         .mono (VMode::Mono, 20.0f)
         .macro (0, "Detune", 0.0f).macro (1, "Brightness", 0.0f)
         .mod (MS::Macro1, detuneA(), 40.0f)
         .mod (MS::Macro2, cutoff1(), 30.0f)
         .master (-6.0f);
        add (r);
    }
    {   // A bouncing ball opens the filter in a decelerating rhythm on every note.
        Recipe r ("Bounce Bass", "Bass", { "bouncy", "physical", "rhythmic", "mono" });
        r.osc (0, Tbl::OddHarmonics, 30.0f, 75.0f)
         .sub (40.0f, Sub::Triangle, 1)
         .filter (0, Flt::LP24, 400.0f, 35.0f, 10.0f)
         .env (0, 2.0f, 600.0f, 75.0f, 150.0f)
         .ball (60.0f, 75.0f)
         .mod (MS::Ball, cutoff1(), 45.0f)
         .mono (VMode::Mono, 0.0f)
         .macro (0, "Bounce", 0.0f)
         .mod (MS::Macro1, cutoff1(), 25.0f)
         .master (-6.0f);
        add (r);
    }
    {   // FM thump: the sub frequency-modulates the FM table, Env 2 sweeps it, compressed.
        Recipe r ("FM Thump", "Bass", { "punchy", "fm", "digital", "mono" });
        r.osc (0, Tbl::FmSweep, 10.0f, 75.0f).warp (0, Warp::FmSub, 35.0f)
         .sub (55.0f, Sub::Sine, 1)
         .filter (0, Flt::LP24, 2500.0f, 10.0f)
         .env (0, 0.5f, 350.0f, 60.0f, 120.0f)
         .env (1, 0.0f, 150.0f, 0.0f, 100.0f)
         .mod (MS::Env2, posA(), 50.0f)
         .mono (VMode::Mono, 0.0f)
         .macro (0, "FM Depth", 0.0f)
         .mod (MS::Macro1, warpA(), 30.0f)
         .compressor (-18.0f, 5.0f, 3.0f)
         .master (-5.0f);
        add (r);
    }
    {   // Wobble with a drawn LFO; the Wobble macro speeds it up.
        Recipe r ("Wobble Macro", "Bass", { "rhythmic", "aggressive", "growl", "mono" });
        r.osc (0, Tbl::SyncSweep, 30.0f, 75.0f)
         .sub (45.0f, Sub::Sine, 1)
         .filter (0, Flt::LP24, 350.0f, 45.0f, 20.0f)
         .env (0, 2.0f, 400.0f, 90.0f, 150.0f)
         .lfo (0, Wave::Custom, 3.0f)
         .customShape (0, { { 0.0f, 0.0f, 0.0f }, { 0.15f, 1.0f, 0.6f }, { 0.5f, 0.35f, -0.4f }, { 0.65f, 0.9f, 0.0f }, { 1.0f, 0.0f, -0.5f } })
         .macro (0, "Wobble", 30.0f).macro (1, "Grit", 0.0f)
         .mod (MS::Lfo1, cutoff1(), 50.0f)
         .mod (MS::Macro1, lfoRate (0), 35.0f)
         .mod (MS::Macro2, P::DistDrive, 50.0f)
         .mono (VMode::Mono, 15.0f)
         .distortion (Dist::Soft, 25.0f, 6000.0f, 60.0f)
         .master (-8.5f);
        add (r);
    }
    {   // Acid line: a saw into a screaming ladder, Env 2 squelch, accent on velocity, legato slides.
        Recipe r ("Acid Squelch", "Bass", { "acid", "resonant", "mono", "retro" });
        r.osc (0, Tbl::BasicShapes, 39.0f, 80.0f)
         .sub (20.0f, Sub::Sine, 1)
         .filter (0, Flt::Ladder, 320.0f, 72.0f, 25.0f, 20.0f)
         .env (0, 1.0f, 400.0f, 70.0f, 80.0f)
         .env (1, 0.0f, 220.0f, 0.0f, 120.0f, 0.0f, -60.0f)
         .mod (MS::Env2, cutoff1(), 50.0f)
         .mod (MS::Velocity, cutoff1(), 15.0f)
         .mono (VMode::Legato, 60.0f)
         .macro (0, "Squelch", 0.0f).macro (1, "Resonance", 0.0f).macro (2, "Drive", 0.0f)
         .mod (MS::Macro1, cutoff1(), 25.0f)
         .mod (MS::Macro2, res1(), 25.0f)
         .mod (MS::Macro3, P::DistDrive, 40.0f)
         .distortion (Dist::Diode, 20.0f, 4500.0f, 50.0f)
         .delay (ND::EighthDotted, 25.0f, 10.0f)
         .master (1.0f);
        add (r);
    }
    {   // Rubber bass: a square, roughened by FM from the sub, that boings: the Spring source bends pitch and filter.
        Recipe r ("Rubber Spring", "Bass", { "bouncy", "physical", "mono", "punchy" });
        r.osc (0, Tbl::BasicShapes, 61.0f, 75.0f).warp (0, Warp::FmSub, 15.0f)
         .sub (35.0f, Sub::Sine, 1)
         .filter (0, Flt::LP24, 600.0f, 25.0f)
         .env (0, 1.0f, 450.0f, 55.0f, 120.0f)
         .spring (70.0f, 25.0f)
         .mod (MS::Spring, pitchA(), 3.0f)
         .mod (MS::Spring, cutoff1(), 30.0f)
         .mono (VMode::Mono, 0.0f)
         .macro (0, "Stretch", 0.0f).macro (1, "Tone", 0.0f)
         .mod (MS::Macro1, cutoff1(), 15.0f)
         .mod (MS::Macro2, cutoff1(), 30.0f)
         .master (-6.0f);
        add (r);
    }
    {   // Hollow square: odd harmonics through a band-pass + low-pass pair in parallel.
        Recipe r ("Hollow Square", "Bass", { "hollow", "clean", "mono" });
        r.osc (0, Tbl::OddHarmonics, 15.0f, 80.0f).route (0, Route::Both)
         .sub (30.0f, Sub::Square, 2)
         .filter (0, Flt::LP12, 1100.0f, 10.0f)
         .filter (1, Flt::BP12, 900.0f, 35.0f)
         .parallel()
         .env (0, 2.0f, 300.0f, 80.0f, 120.0f)
         .mono (VMode::Mono, 10.0f)
         .macro (0, "Body", 0.0f).macro (1, "Hollow", 0.0f)
         .mod (MS::Macro1, cutoff1(), 25.0f)
         .mod (MS::Macro2, cutoff2(), 30.0f)
         .mod (MS::Macro2, res2(), 30.0f)
         .master (-7.0f);
        add (r);
    }
    {   // Pedal organ bass: low organ drawbars, a slow chorus, no filter movement at all.
        Recipe r ("Pedal Organ Bass", "Bass", { "vintage", "warm", "clean" });
        r.osc (0, Tbl::Organ, 10.0f, 75.0f)
         .osc (1, Tbl::Organ, 45.0f, 30.0f).oscPitch (1, 1)
         .sub (25.0f, Sub::Sine, 1)
         .filter (0, Flt::LP12, 1600.0f, 5.0f)
         .env (0, 6.0f, 100.0f, 100.0f, 90.0f)
         .lfo (0, Wave::Sine, 5.8f).lfoFade (0, 400.0f)
         .mod (MS::Lfo1, fineA(), 6.0f)
         .macro (0, "Drawbars", 0.0f).macro (1, "Rotor", 0.0f)
         .mod (MS::Macro1, posA(), 40.0f)
         .mod (MS::Macro2, P::ChorusMix, 50.0f)
         .chorus (0.4f, 30.0f, 25.0f)
         .master (-3.0f);
        add (r);
    }
    {   // Talking bass: formant table through the formant filter, a synced LFO walks the vowels.
        Recipe r ("Talking Bass", "Bass", { "vocal", "rhythmic", "mono", "growl" });
        r.osc (0, Tbl::FormantVowels, 30.0f, 80.0f)
         .sub (35.0f, Sub::Sine, 1)
         .filter (0, Flt::Formant, 700.0f, 30.0f)
         .env (0, 3.0f, 400.0f, 85.0f, 150.0f)
         .lfo (0, Wave::Triangle, 2.0f).lfoSync (0, ND::Quarter)
         .mod (MS::Lfo1, cutoff1(), 30.0f)
         .mod (MS::Lfo1, posA(), 25.0f)
         .mono (VMode::Mono, 20.0f)
         .macro (0, "Vowel", 0.0f).macro (1, "Talk Rate", 0.0f)
         .mod (MS::Macro1, cutoff1(), 30.0f)
         .mod (MS::Macro2, lfoRate (0), 30.0f)
         .master (0.5f);
        add (r);
    }
    {   // Bent saw: Bend warp swept by Env 2 gives a vocal "yow" at the front of each note.
        Recipe r ("Bent Saw Bass", "Bass", { "growl", "modern", "mono" });
        r.osc (0, Tbl::AnalogSaws, 5.0f, 80.0f).warp (0, Warp::Bend, -10.0f)
         .sub (40.0f, Sub::Sine, 1)
         .filter (0, Flt::LP24, 900.0f, 25.0f, 15.0f)
         .env (0, 2.0f, 350.0f, 80.0f, 140.0f)
         .env (1, 0.0f, 300.0f, 0.0f, 150.0f)
         .mod (MS::Env2, warpA(), 55.0f)
         .mod (MS::Env2, cutoff1(), 20.0f)
         .mono (VMode::Mono, 15.0f)
         .macro (0, "Bend", 0.0f).macro (1, "Brightness", 0.0f)
         .mod (MS::Macro1, warpA(), 40.0f)
         .mod (MS::Macro2, cutoff1(), 30.0f)
         .master (-5.5f);
        add (r);
    }
    {   // Squeezed pulse: Squeeze warp narrows the pulse table; a free LFO keeps it moving.
        Recipe r ("Squeeze Pulse", "Bass", { "retro", "digital", "mono" });
        r.osc (0, Tbl::PulseWidth, 20.0f, 80.0f).warp (0, Warp::Squeeze, 20.0f)
         .sub (35.0f, Sub::Sine, 1)
         .filter (0, Flt::LP24, 1200.0f, 15.0f)
         .env (0, 1.0f, 300.0f, 75.0f, 100.0f)
         .lfo (0, Wave::Sine, 0.6f, Mode::Free)
         .mod (MS::Lfo1, warpA(), 25.0f)
         .mono (VMode::Mono, 0.0f)
         .macro (0, "Squeeze", 0.0f).macro (1, "Cutoff", 0.0f)
         .mod (MS::Macro1, warpA(), 40.0f)
         .mod (MS::Macro2, cutoff1(), 30.0f)
         .compressor (-20.0f, 3.0f, 2.0f)
         .master (-2.5f);
        add (r);
    }
    {   // Mirror bass: the harmonic stack folded by the Mirror warp, tight and round.
        Recipe r ("Mirror Bass", "Bass", { "modern", "punchy", "mono" });
        r.osc (0, Tbl::HarmonicStack, 35.0f, 80.0f).warp (0, Warp::Mirror, 40.0f)
         .sub (45.0f, Sub::Sine, 1)
         .filter (0, Flt::LP24, 800.0f, 15.0f)
         .env (0, 1.0f, 250.0f, 70.0f, 120.0f)
         .env (1, 0.0f, 180.0f, 0.0f, 100.0f)
         .mod (MS::Env2, cutoff1(), 35.0f)
         .mono (VMode::Mono, 5.0f)
         .macro (0, "Fold", 0.0f).macro (1, "Snap", 0.0f)
         .mod (MS::Macro1, warpA(), 45.0f)
         .mod (MS::Macro2, envDecay (1), 30.0f)
         .master (-1.5f);
        add (r);
    }
    {   // Remap grit: a remapped growl table into a diode distortion; dirty but low.
        Recipe r ("Remap Grit", "Bass", { "dirty", "aggressive", "mono" });
        r.osc (0, Tbl::Growl, 55.0f, 80.0f).warp (0, Warp::Remap, 45.0f)
         .sub (40.0f, Sub::Sine, 1)
         .filter (0, Flt::LP24, 700.0f, 30.0f, 35.0f)
         .env (0, 2.0f, 400.0f, 85.0f, 130.0f)
         .lfo (1, Wave::SampleHold, 4.0f).lfoSync (1, ND::Sixteenth).lfoSmooth (1, 30.0f)
         .mod (MS::Lfo2, posA(), 15.0f)
         .mono (VMode::Mono, 10.0f)
         .macro (0, "Grit", 0.0f).macro (1, "Remap", 0.0f)
         .mod (MS::Macro1, P::DistDrive, 50.0f)
         .mod (MS::Macro2, warpA(), 40.0f)
         .distortion (Dist::Diode, 45.0f, 3500.0f, 80.0f)
         .eq (3.0f, -3.0f, 800.0f, -6.0f)
         .master (-4.0f);
        add (r);
    }
    {   // Hard-sync punch: the sync sweep table with Sync warp and a quick Env 2 dive.
        Recipe r ("Sync Punch Bass", "Bass", { "punchy", "aggressive", "mono" });
        r.osc (0, Tbl::SyncSweep, 10.0f, 80.0f).warp (0, Warp::Sync, 10.0f)
         .sub (40.0f, Sub::Sine, 1)
         .filter (0, Flt::LP24, 1000.0f, 20.0f)
         .env (0, 0.5f, 300.0f, 70.0f, 100.0f)
         .env (1, 0.0f, 120.0f, 0.0f, 80.0f)
         .mod (MS::Env2, warpA(), 60.0f)
         .mod (MS::Env2, cutoff1(), 25.0f)
         .mono (VMode::Mono, 0.0f)
         .macro (0, "Sync", 0.0f).macro (1, "Punch", 0.0f)
         .mod (MS::Macro1, warpA(), 40.0f)
         .mod (MS::Macro2, envDecay (1), 40.0f)
         .compressor (-22.0f, 4.0f, 3.0f, 15.0f)
         .master (0.0f);
        add (r);
    }
    {   // Dusty upright: a plucked decay table with a woody resonant low-pass and a touch of room.
        Recipe r ("Dusty Upright", "Bass", { "dusty", "organic", "plucky" });
        r.osc (0, Tbl::PluckDecay, 20.0f, 80.0f)
         .sub (25.0f, Sub::Triangle, 1)
         .filter (0, Flt::LP12, 900.0f, 20.0f, 10.0f, 30.0f)
         .env (0, 2.0f, 900.0f, 20.0f, 250.0f)
         .env (1, 0.0f, 600.0f, 0.0f, 300.0f)
         .mod (MS::Env2, posA(), 50.0f)
         .mod (MS::Velocity, cutoff1(), 20.0f)
         .living (25.0f, 0.0f)
         .macro (0, "Wood", 0.0f).macro (1, "Room", 0.0f)
         .mod (MS::Macro1, cutoff1(), 25.0f)
         .mod (MS::Macro2, P::ReverbMix, 30.0f)
         .eq (2.0f, 3.0f, 300.0f, -6.0f)
         .reverb (25.0f, 25.0f, 8.0f, 60.0f)
         .master (2.0f);
        add (r);
    }
    {   // Dirt floor: noise-bloom grain over brown noise and a driven ladder; the floor of a mix.
        Recipe r ("Dirt Floor", "Bass", { "dirty", "noisy", "dark", "mono" });
        r.osc (0, Tbl::NoiseBloom, 15.0f, 70.0f)
         .sub (55.0f, Sub::Sine, 1)
         .noise (12.0f, Noise::Brown)
         .filter (0, Flt::Ladder, 550.0f, 30.0f, 55.0f)
         .env (0, 2.0f, 400.0f, 85.0f, 150.0f)
         .mono (VMode::Mono, 20.0f)
         .macro (0, "Dirt", 0.0f).macro (1, "Grain", 0.0f)
         .mod (MS::Macro1, drive1(), 40.0f)
         .mod (MS::Macro2, posA(), 40.0f)
         .mod (MS::Macro2, P::NoiseLevel, 20.0f)
         .distortion (Dist::Crush, 15.0f, 3000.0f, 35.0f)
         .master (-5.0f);
        add (r);
    }
    {   // Orbit reese: two 8-voice saw stacks whose fine tune circles each other on the Orbit source.
        Recipe r ("Orbit Reese", "Bass", { "wide", "detuned", "physical", "dark" });
        r.osc (0, Tbl::AnalogSaws, 40.0f, 75.0f).unison (0, 8, 18.0f, 55.0f, 80.0f)
         .osc (1, Tbl::AnalogSaws, 40.0f, 60.0f).unison (1, 8, 22.0f, 65.0f, 80.0f).oscPitch (1, -1)
         .sub (35.0f, Sub::Sine, 1)
         .filter (0, Flt::LP24, 650.0f, 25.0f, 15.0f)
         .env (0, 4.0f, 500.0f, 90.0f, 200.0f)
         .orbit (30.0f, 60.0f)
         .mod (MS::Orbit, fineA(), 12.0f)
         .mod (MS::Orbit, fineB(), -12.0f)
         .mod (MS::Orbit, cutoff1(), 10.0f)
         .mono (VMode::Legato, 40.0f)
         .macro (0, "Orbit", 0.0f).macro (1, "Brightness", 0.0f)
         .mod (MS::Macro1, detuneA(), 30.0f)
         .mod (MS::Macro2, cutoff1(), 30.0f)
         .master (-8.0f);
        add (r);
    }
    {   // Tape drive: square + saw into a hot ladder, tube saturation, glued with a compressor.
        Recipe r ("Tape Drive Bass", "Bass", { "vintage", "warm", "dirty", "mono" });
        r.osc (0, Tbl::BasicShapes, 19.0f, 75.0f)
         .osc (1, Tbl::BasicShapes, 12.0f, 55.0f).oscPitch (1, 0, 0, 6.0f)
         .sub (25.0f, Sub::Square, 1, Route::Filter1)
         .filter (0, Flt::Ladder, 750.0f, 25.0f, 45.0f, 25.0f)
         .env (0, 2.0f, 500.0f, 80.0f, 150.0f)
         .env (1, 0.0f, 350.0f, 20.0f, 200.0f)
         .mod (MS::Env2, cutoff1(), 30.0f)
         .mono (VMode::Legato, 25.0f)
         .macro (0, "Tape", 0.0f).macro (1, "Cutoff", 0.0f)
         .mod (MS::Macro1, P::DistDrive, 45.0f)
         .mod (MS::Macro2, cutoff1(), 30.0f)
         .distortion (Dist::Tube, 35.0f, 4000.0f, 70.0f)
         .compressor (-20.0f, 3.0f, 2.0f, 20.0f, 150.0f)
         .master (-1.0f);
        add (r);
    }
    {   // Phase shift bass: a round pulse through a slow 12-stage phaser; the low end stays put.
        Recipe r ("Phase Shift Bass", "Bass", { "smooth", "evolving", "mono" });
        r.osc (0, Tbl::PulseWidth, 5.0f, 80.0f)
         .sub (40.0f, Sub::Sine, 1)
         .filter (0, Flt::LP24, 1300.0f, 15.0f)
         .env (0, 3.0f, 400.0f, 85.0f, 150.0f)
         .mono (VMode::Mono, 30.0f)
         .macro (0, "Phase", 0.0f).macro (1, "Tone", 0.0f)
         .mod (MS::Macro1, P::PhaserRate, 30.0f)
         .mod (MS::Macro1, P::PhaserMix, 30.0f)
         .mod (MS::Macro2, cutoff1(), 25.0f)
         .phaser (0.12f, 75.0f, 55.0f, 50.0f, 3)
         .master (-9.0f);
        add (r);
    }
    {   // Tube comb bass: a negative comb tuned below the note makes a hollow, pipe-like body.
        Recipe r ("Tube Comb Bass", "Bass", { "hollow", "resonant", "mono" });
        r.osc (0, Tbl::HarmonicStack, 20.0f, 80.0f)
         .sub (30.0f, Sub::Sine, 1)
         .filter (0, Flt::CombMinus, 180.0f, 55.0f, 0.0f, 100.0f)
         .filter (1, Flt::LP12, 1400.0f, 5.0f)
         .env (0, 2.0f, 400.0f, 80.0f, 150.0f)
         .env (1, 0.0f, 400.0f, 0.0f, 150.0f)
         .mod (MS::Env2, cutoff1(), 10.0f)
         .mono (VMode::Mono, 10.0f)
         .macro (0, "Tube", 0.0f).macro (1, "Tone", 0.0f)
         .mod (MS::Macro1, res1(), 35.0f)
         .mod (MS::Macro2, cutoff2(), 30.0f)
         .master (-3.5f);
        add (r);
    }
    {   // FM wood: Osc B frequency-modulates Osc A for a struck, woody electric-bass attack.
        Recipe r ("FM Wood Bass", "Bass", { "fm", "punchy", "clean" });
        r.osc (0, Tbl::BasicShapes, 0.0f, 80.0f).warp (0, Warp::FmOsc, 30.0f)
         .osc (1, Tbl::BasicShapes, 0.0f, 25.0f).oscPitch (1, 1)
         .sub (25.0f, Sub::Sine, 1)
         .filter (0, Flt::LP12, 2200.0f, 5.0f)
         .env (0, 1.0f, 800.0f, 45.0f, 150.0f)
         .env (1, 0.0f, 200.0f, 0.0f, 100.0f)
         .mod (MS::Env2, warpA(), 40.0f)
         .mod (MS::Velocity, warpA(), 15.0f)
         .macro (0, "Wood", 0.0f).macro (1, "Tone", 0.0f)
         .mod (MS::Macro1, warpA(), 30.0f)
         .mod (MS::Macro2, cutoff1(), 25.0f)
         .compressor (-18.0f, 3.0f, 2.0f)
         .master (-3.5f);
        add (r);
    }
    {   // Hoover low: a big detuned pulse stack with a pitch scoop, chorus and a jet flanger, filtered dark.
        Recipe r ("Hoover Low", "Bass", { "retro", "wide", "aggressive", "detuned" });
        r.osc (0, Tbl::PulseWidth, 35.0f, 75.0f).unison (0, 9, 35.0f, 80.0f, 80.0f)
         .sub (35.0f, Sub::Saw, 1, Route::Filter1)
         .filter (0, Flt::LP24, 900.0f, 15.0f, 10.0f)
         .env (0, 4.0f, 600.0f, 85.0f, 250.0f)
         .env (2, 0.0f, 180.0f, 0.0f, 100.0f)
         .mod (MS::Env3, pitchA(), 4.0f)
         .mono (VMode::Legato, 80.0f)
         .macro (0, "Hoover", 0.0f).macro (1, "Brightness", 0.0f)
         .mod (MS::Macro1, posA(), 40.0f)
         .mod (MS::Macro1, P::FlangerMix, 30.0f)
         .mod (MS::Macro2, cutoff1(), 30.0f)
         .chorus (0.6f, 40.0f, 30.0f)
         .flanger (0.15f, 45.0f, 35.0f, 15.0f)
         .master (0.0f);
        add (r);
    }
}

} // namespace verdant::factory
