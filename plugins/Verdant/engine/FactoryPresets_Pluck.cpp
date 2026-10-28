// Factory bank: Pluck.

#include "FactoryPresets_Recipes.h"

namespace verdant::factory
{

void addPluckPresets (std::vector<Patch>& bank)
{
    auto add = [&bank] (Recipe& r) { bank.push_back (r.p); };

    {   // Crystal pluck: Env 2 runs down the pluck table, dotted delay.
        Recipe r ("Crystal Pluck", "Pluck", { "bright", "plucky", "glassy" });
        r.osc (0, Tbl::PluckDecay, 0.0f, 80.0f)
         .filter (0, Flt::LP12, 8000.0f, 8.0f, 0.0f, 60.0f)
         .env (0, 0.5f, 650.0f, 0.0f, 350.0f)
         .env (1, 0.0f, 500.0f, 0.0f, 300.0f)
         .mod (MS::Env2, posA(), 70.0f)
         .macro (0, "Decay", 0.0f).macro (1, "Echo", 0.0f)
         .mod (MS::Macro1, envDecay (0), 25.0f)
         .mod (MS::Macro2, P::DelayMix, 30.0f)
         .delay (ND::EighthDotted, 30.0f, 18.0f)
         .reverb (45.0f, 45.0f, 15.0f)
         .master (3.0f);
        add (r);
    }
    {   // Harp with a key-snapped sympathetic memory.
        Recipe r ("Harp Memory", "Pluck", { "resonant", "plucky", "warm" });
        r.osc (0, Tbl::PluckDecay, 10.0f, 75.0f)
         .osc (1, Tbl::BasicShapes, 15.0f, 30.0f).oscPitch (1, 1)
         .filter (0, Flt::LP24, 5000.0f, 10.0f, 0.0f, 60.0f)
         .env (0, 1.0f, 500.0f, 0.0f, 400.0f)
         .key (Key::C, ST::Major)
         .sympathetic (25.0f, 6, 10, 20.0f, 55.0f).sympSnap()
         .macro (0, "Strings", 0.0f).macro (1, "Brightness", 0.0f)
         .mod (MS::Macro1, P::SympDecay, 30.0f)
         .mod (MS::Macro2, cutoff1(), 25.0f)
         .reverb (50.0f, 50.0f, 20.0f)
         .master (0.5f);
        add (r);
    }
    {   // Springy pluck: the Spring source bends the pitch and the filter after each note.
        Recipe r ("Spring Pluck", "Pluck", { "bouncy", "physical", "plucky" });
        r.osc (0, Tbl::OddHarmonics, 45.0f, 80.0f)
         .filter (0, Flt::LP24, 2500.0f, 30.0f)
         .env (0, 0.5f, 500.0f, 0.0f, 300.0f)
         .spring (65.0f, 20.0f)
         .mod (MS::Spring, pitchA(), 4.0f)
         .mod (MS::Spring, cutoff1(), 25.0f)
         .macro (0, "Wobble", 0.0f).macro (1, "Tone", 0.0f)
         .mod (MS::Macro1, pitchA(), 2.0f)
         .mod (MS::Macro2, cutoff1(), 25.0f)
         .reverb (35.0f, 35.0f, 15.0f)
         .master (-4.0f);
        add (r);
    }
    {   // Woody mallet: bell table through a positive comb.
        Recipe r ("Marimba Wood", "Pluck", { "percussive", "organic", "warm" });
        r.osc (0, Tbl::Bell, 40.0f, 75.0f)
         .filter (0, Flt::CombPlus, 260.0f, 35.0f)
         .filter (1, Flt::LP12, 4000.0f, 5.0f)
         .env (0, 0.5f, 380.0f, 0.0f, 250.0f)
         .macro (0, "Wood", 0.0f).macro (1, "Mallet", 0.0f)
         .mod (MS::Macro1, res1(), 30.0f)
         .mod (MS::Macro2, cutoff2(), 30.0f)
         .reverb (30.0f, 30.0f, 15.0f)
         .master (0.0f);
        add (r);
    }
    {   // Koto snap: odd harmonics plucked hard into a negative comb, pentatonic sympathetic ring.
        Recipe r ("Koto Snap", "Pluck", { "plucky", "organic", "bright" });
        r.osc (0, Tbl::OddHarmonics, 70.0f, 80.0f).hardPhase (0)
         .filter (0, Flt::CombMinus, 520.0f, 30.0f, 0.0f, 100.0f)
         .filter (1, Flt::LP12, 6000.0f, 10.0f, 0.0f, 50.0f)
         .env (0, 0.5f, 400.0f, 0.0f, 300.0f, 0.0f, -60.0f)
         .env (1, 0.0f, 150.0f, 0.0f, 100.0f)
         .mod (MS::Env2, cutoff2(), 25.0f)
         .key (Key::D, ST::MinorPentatonic)
         .sympathetic (20.0f, 5, 5, 30.0f, 60.0f).sympSnap()
         .macro (0, "Snap", 0.0f).macro (1, "Strings", 0.0f)
         .mod (MS::Macro1, cutoff2(), 25.0f)
         .mod (MS::Macro2, P::SympAmount, 30.0f)
         .master (-6.0f);
        add (r);
    }
    {   // Nylon body: a plucked-decay string with a sympathetic guitar body and a warm room.
        Recipe r ("Nylon Body", "Pluck", { "warm", "organic", "resonant" });
        r.osc (0, Tbl::PluckDecay, 30.0f, 80.0f).hardPhase (0, 90.0f)
         .filter (0, Flt::LP24, 3000.0f, 10.0f, 0.0f, 70.0f)
         .env (0, 0.5f, 600.0f, 0.0f, 400.0f, 0.0f, -75.0f)
         .env (1, 0.0f, 400.0f, 0.0f, 200.0f)
         .mod (MS::Env2, posA(), 40.0f)
         .mod (MS::Velocity, cutoff1(), 20.0f)
         .sympathetic (20.0f, 3, 6, 45.0f, 40.0f)
         .macro (0, "Body", 0.0f).macro (1, "Brightness", 0.0f)
         .mod (MS::Macro1, P::SympAmount, 30.0f)
         .mod (MS::Macro2, cutoff1(), 25.0f)
         .reverb (30.0f, 30.0f, 12.0f)
         .master (3.5f);
        add (r);
    }
    {   // Thumb tines: a sine and a detuned bell partial, quick decay, like a kalimba.
        Recipe r ("Thumb Tines", "Pluck", { "soft", "metallic", "organic" });
        r.osc (0, Tbl::BasicShapes, 0.0f, 80.0f).hardPhase (0)
         .osc (1, Tbl::Bell, 50.0f, 35.0f).oscPitch (1, 2, 0, -12.0f).hardPhase (1)
         .filter (0, Flt::LP12, 5000.0f, 5.0f)
         .env (0, 0.5f, 500.0f, 0.0f, 400.0f)
         .env (1, 0.0f, 80.0f, 0.0f, 60.0f)
         .mod (MS::Env2, levelB(), 50.0f)
         .macro (0, "Tine", 0.0f).macro (1, "Space", 0.0f)
         .mod (MS::Macro1, levelB(), 30.0f)
         .mod (MS::Macro2, P::ReverbMix, 30.0f)
         .reverb (40.0f, 40.0f, 15.0f)
         .master (-3.5f);
        add (r);
    }
    {   // Analog snap: two detuned saws, a fast filter envelope, a short decay; the house-stab pluck.
        Recipe r ("Analog Snap", "Pluck", { "classic", "bright", "plucky" });
        r.osc (0, Tbl::AnalogSaws, 20.0f, 75.0f)
         .osc (1, Tbl::AnalogSaws, 20.0f, 60.0f).oscPitch (1, 0, 0, 10.0f)
         .filter (0, Flt::LP24, 400.0f, 25.0f, 10.0f, 50.0f)
         .env (0, 0.5f, 300.0f, 0.0f, 200.0f)
         .env (1, 0.0f, 160.0f, 0.0f, 100.0f)
         .mod (MS::Env2, cutoff1(), 50.0f)
         .macro (0, "Snap", 0.0f).macro (1, "Length", 0.0f)
         .mod (MS::Macro1, cutoff1(), 25.0f)
         .mod (MS::Macro2, envDecay (0), 25.0f)
         .reverb (35.0f, 30.0f, 12.0f)
         .master (1.0f);
        add (r);
    }
    {   // Glass harp: digital glass scanned downward by Env 2, a ping-pong shimmer.
        Recipe r ("Glass Harp", "Pluck", { "glassy", "digital", "bright" });
        r.osc (0, Tbl::DigitalGlass, 80.0f, 75.0f)
         .filter (0, Flt::LP12, 9000.0f, 5.0f)
         .env (0, 0.5f, 550.0f, 0.0f, 350.0f)
         .env (1, 0.0f, 400.0f, 0.0f, 200.0f)
         .mod (MS::Env2, posA(), -60.0f)
         .macro (0, "Glass", 0.0f).macro (1, "Shimmer", 0.0f)
         .mod (MS::Macro1, posA(), -30.0f)
         .mod (MS::Macro2, P::DelayMix, 30.0f)
         .delay (ND::Sixteenth, 25.0f, 12.0f)
         .master (6.0f);
        add (r);
    }
    {   // FM pizzicato: a fast FM index envelope makes the bow-less string bite.
        Recipe r ("FM Pizz", "Pluck", { "fm", "short", "plucky" });
        r.osc (0, Tbl::BasicShapes, 6.0f, 80.0f).warp (0, Warp::FmOsc, 10.0f).hardPhase (0)
         .osc (1, Tbl::BasicShapes, 0.0f, 25.0f).oscPitch (1, 1, 7)
         .filter (0, Flt::LP12, 4000.0f, 5.0f, 0.0f, 50.0f)
         .env (0, 0.5f, 250.0f, 0.0f, 150.0f)
         .env (1, 0.0f, 60.0f, 0.0f, 50.0f)
         .mod (MS::Env2, warpA(), 50.0f)
         .macro (0, "Bite", 0.0f).macro (1, "Room", 0.0f)
         .mod (MS::Macro1, warpA(), 30.0f)
         .mod (MS::Macro2, P::ReverbMix, 35.0f)
         .reverb (40.0f, 35.0f, 15.0f)
         .master (0.0f);
        add (r);
    }
    {   // Sync blip: the hard-sync amount falls with Env 2: a zippy digital blip.
        Recipe r ("Sync Blip", "Pluck", { "digital", "short", "retro" });
        r.osc (0, Tbl::SyncSweep, 50.0f, 80.0f).warp (0, Warp::Sync, 0.0f).hardPhase (0)
         .filter (0, Flt::LP24, 7000.0f, 10.0f)
         .env (0, 0.5f, 220.0f, 0.0f, 150.0f)
         .env (1, 0.0f, 120.0f, 0.0f, 80.0f)
         .mod (MS::Env2, warpA(), 70.0f)
         .macro (0, "Sync", 0.0f).macro (1, "Echo", 0.0f)
         .mod (MS::Macro1, posA(), 35.0f)
         .mod (MS::Macro2, P::DelayMix, 30.0f)
         .delay (ND::Sixteenth, 20.0f, 10.0f)
         .master (5.5f);
        add (r);
    }
    {   // Bouncing pluck: the ball's bounces re-open the filter, so each note ricochets into silence.
        Recipe r ("Bouncing Pluck", "Pluck", { "bouncy", "physical", "rhythmic" });
        r.osc (0, Tbl::HarmonicStack, 50.0f, 80.0f)
         .filter (0, Flt::LP24, 300.0f, 25.0f)
         .env (0, 0.5f, 900.0f, 0.0f, 300.0f, 0.0f, -75.0f)
         .ball (75.0f, 65.0f)
         .mod (MS::Ball, cutoff1(), 55.0f)
         .macro (0, "Bounce", 0.0f).macro (1, "Tone", 0.0f)
         .mod (MS::Macro1, res1(), 30.0f)
         .mod (MS::Macro2, cutoff1(), 25.0f)
         .master (2.0f);
        add (r);
    }
    {   // Steel pan isle: inharmonic bell partials through a band-pass, bright and round.
        Recipe r ("Steel Pan Isle", "Pluck", { "metallic", "bright", "percussive" });
        r.osc (0, Tbl::Bell, 70.0f, 75.0f).hardPhase (0)
         .osc (1, Tbl::BasicShapes, 0.0f, 45.0f)
         .filter (0, Flt::BP12, 1500.0f, 20.0f, 0.0f, 80.0f)
         .filterMix (0, 70.0f)
         .env (0, 0.5f, 600.0f, 0.0f, 400.0f)
         .env (1, 0.0f, 100.0f, 0.0f, 80.0f)
         .mod (MS::Env2, cutoff1(), 20.0f)
         .macro (0, "Steel", 0.0f).macro (1, "Space", 0.0f)
         .mod (MS::Macro1, posA(), 30.0f)
         .mod (MS::Macro2, P::ReverbMix, 30.0f)
         .reverb (40.0f, 35.0f, 15.0f)
         .master (4.5f);
        add (r);
    }
    {   // Hammered dulcimer: paired strings (unison 2), metallic attack and a sympathetic body.
        Recipe r ("Hammered Dulcimer", "Pluck", { "resonant", "metallic", "organic" });
        r.osc (0, Tbl::PluckDecay, 50.0f, 75.0f).unison (0, 2, 6.0f, 40.0f, 100.0f)
         .osc (1, Tbl::Organ, 80.0f, 25.0f).oscPitch (1, 1)
         .filter (0, Flt::LP12, 6000.0f, 10.0f, 0.0f, 50.0f)
         .env (0, 0.5f, 500.0f, 0.0f, 500.0f)
         .env (1, 0.0f, 300.0f, 0.0f, 150.0f)
         .mod (MS::Env2, posA(), 40.0f)
         .sympathetic (20.0f, 6, 12, 40.0f, 60.0f)
         .macro (0, "Courses", 0.0f).macro (1, "Ring", 0.0f)
         .mod (MS::Macro1, detuneA(), 25.0f)
         .mod (MS::Macro2, P::SympDecay, 30.0f)
         .reverb (35.0f, 35.0f, 12.0f)
         .master (1.0f);
        add (r);
    }
    {   // Twang: a narrow pulse through a notch and a key-tracked comb in parallel; a picked, nasal string.
        Recipe r ("Twang Comb", "Pluck", { "plucky", "hollow", "retro" });
        r.osc (0, Tbl::PulseWidth, 70.0f, 80.0f).hardPhase (0).route (0, Route::Both)
         .filter (0, Flt::CombPlus, 700.0f, 40.0f, 0.0f, 100.0f)
         .filter (1, Flt::Notch, 1800.0f, 30.0f)
         .parallel()
         .env (0, 0.5f, 450.0f, 0.0f, 250.0f)
         .env (1, 0.0f, 250.0f, 0.0f, 100.0f)
         .mod (MS::Env2, cutoff2(), 25.0f)
         .macro (0, "Twang", 0.0f).macro (1, "Nasal", 0.0f)
         .mod (MS::Macro1, res1(), 30.0f)
         .mod (MS::Macro2, res2(), 35.0f)
         .master (-3.5f);
        add (r);
    }
    {   // Water drop: a sine whose pitch leaps up as it fades, like a drop into a pool.
        Recipe r ("Water Drop", "Pluck", { "soft", "clean", "short" });
        r.osc (0, Tbl::BasicShapes, 0.0f, 85.0f).hardPhase (0)
         .filterOff (0)
         .env (0, 0.5f, 250.0f, 0.0f, 150.0f)
         .env (1, 0.0f, 120.0f, 0.0f, 100.0f, 0.0f, 40.0f)
         .mod (MS::Env2, pitchA(), -12.0f)
         .macro (0, "Drop", 0.0f).macro (1, "Pool", 0.0f)
         .mod (MS::Macro1, envDecay (1), 30.0f)
         .mod (MS::Macro2, P::ReverbMix, 35.0f)
         .reverb (50.0f, 40.0f, 15.0f)
         .master (-0.5f);
        add (r);
    }
    {   // Growl stab: a short growl-table hit, Env 2 scanning the table, folded and compressed.
        Recipe r ("Growl Stab", "Pluck", { "growl", "aggressive", "short" });
        r.osc (0, Tbl::Growl, 80.0f, 80.0f).hardPhase (0)
         .sub (30.0f, Sub::Sine, 1)
         .filter (0, Flt::LP24, 3000.0f, 20.0f)
         .env (0, 0.5f, 300.0f, 0.0f, 120.0f)
         .env (1, 0.0f, 250.0f, 0.0f, 100.0f)
         .mod (MS::Env2, posA(), -60.0f)
         .macro (0, "Growl", 0.0f).macro (1, "Drive", 0.0f)
         .mod (MS::Macro1, posA(), -30.0f)
         .mod (MS::Macro2, P::DistDrive, 40.0f)
         .distortion (Dist::Fold, 20.0f, 6000.0f, 50.0f)
         .compressor (-18.0f, 4.0f, 3.0f, 2.0f, 80.0f)
         .master (1.5f);
        add (r);
    }
    {   // Vocal chop: a choir syllable cut short, the vowel flipping with Env 2.
        Recipe r ("Vocal Chop", "Pluck", { "vocal", "short", "modern" });
        r.osc (0, Tbl::Choir, 80.0f, 100.0f).hardPhase (0)
         .filter (0, Flt::Formant, 2500.0f, 15.0f)
         .env (0, 0.5f, 280.0f, 0.0f, 120.0f)
         .env (1, 0.0f, 200.0f, 0.0f, 100.0f)
         .mod (MS::Env2, cutoff1(), 15.0f)
         .mod (MS::Env2, posA(), -40.0f)
         .macro (0, "Vowel", 0.0f).macro (1, "Echo", 0.0f)
         .mod (MS::Macro1, cutoff1(), 25.0f)
         .mod (MS::Macro2, P::DelayMix, 30.0f)
         .delay (ND::Eighth, 20.0f, 10.0f)
         .master (11.0f);
        add (r);
    }
}

} // namespace verdant::factory
