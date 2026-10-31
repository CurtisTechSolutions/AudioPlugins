// Factory bank: the first expansion (host programs 172 onwards), three presets per category in
// browser order. It sits after the original bank so the program numbers hosts have stored stay put.

#include "FactoryPresets_Recipes.h"

namespace verdant::factory
{

void addExpansionPresets (std::vector<Patch>& bank)
{
    auto add = [&bank] (Recipe& r) { bank.push_back (r.p); };

    // ---- Bass (C2)
    {   // Rubber bass: a spring wobbles the ladder after every note, legato slides.
        Recipe r ("Rubber Band Bass", "Bass", { "bouncy", "physical", "mono", "warm" });
        r.osc (0, Tbl::PulseWidth, 40.0f, 75.0f)
         .sub (45.0f, Sub::Sine, 1)
         .filter (0, Flt::Ladder, 450.0f, 40.0f, 10.0f, 20.0f)
         .env (0, 1.0f, 500.0f, 80.0f, 140.0f)
         .spring (55.0f, 35.0f)
         .mod (MS::Spring, cutoff1(), 40.0f)
         .mono (VMode::Legato, 35.0f)
         .living (15.0f, 0.0f)
         .macro (0, "Stretch", 0.0f).macro (1, "Tone", 0.0f)
         .mod (MS::Macro1, cutoff1(), 25.0f)
         .mod (MS::Macro2, posA(), 40.0f)
         .master (-5.0f);
        add (r);
    }
    {   // Vowel bass: the formant table swept by Env 2, a formant filter on top, a little drive.
        Recipe r ("Vowel Morph Bass", "Bass", { "vocal", "growl", "mono" });
        r.osc (0, Tbl::FormantVowels, 20.0f, 75.0f)
         .sub (40.0f, Sub::Triangle, 1)
         .filter (0, Flt::Formant, 600.0f, 35.0f, 15.0f)
         .env (0, 2.0f, 400.0f, 85.0f, 150.0f)
         .env (1, 0.0f, 450.0f, 20.0f, 200.0f)
         .mod (MS::Env2, posA(), 45.0f)
         .mono (VMode::Mono, 20.0f)
         .living (15.0f, 0.0f)
         .macro (0, "Vowel", 0.0f).macro (1, "Grit", 0.0f)
         .mod (MS::Macro1, posA(), 40.0f)
         .mod (MS::Macro2, P::DistDrive, 40.0f)
         .distortion (Dist::Soft, 20.0f, 4000.0f, 50.0f)
         .master (0.5f);
        add (r);
    }
    {   // Organ bass: organ drawbars, a square sub two octaves down, tight and percussive.
        Recipe r ("Drawbar Bass", "Bass", { "vintage", "deep", "punchy" });
        r.osc (0, Tbl::Organ, 30.0f, 70.0f)
         .sub (50.0f, Sub::Square, 1)
         .filter (0, Flt::LP12, 1200.0f, 10.0f)
         .env (0, 1.0f, 300.0f, 60.0f, 90.0f)
         .env (1, 0.0f, 120.0f, 0.0f, 80.0f)
         .mod (MS::Env2, cutoff1(), 30.0f)
         .voices (4)
         .living (12.0f, 0.0f)
         .macro (0, "Click", 0.0f).macro (1, "Body", 0.0f)
         .mod (MS::Macro1, envDecay (1), 30.0f)
         .mod (MS::Macro2, P::SubLevel, 30.0f)
         .master (-4.0f);
        add (r);
    }

    // ---- Lead (C3)
    {   // Glass lead: digital glass through a band-pass, vibrato after a fade, ping-pong echoes.
        Recipe r ("Prism Lead", "Lead", { "glassy", "bright", "digital" });
        r.osc (0, Tbl::DigitalGlass, 35.0f, 75.0f).unison (0, 3, 8.0f, 50.0f)
         .filter (0, Flt::BP12, 2200.0f, 25.0f, 0.0f, 60.0f)
         .filterMix (0, 70.0f)
         .env (0, 4.0f, 300.0f, 90.0f, 300.0f)
         .lfo (0, Wave::Sine, 5.5f).lfoFade (0, 500.0f)
         .mod (MS::Lfo1, fineA(), 8.0f)
         .mono (VMode::Legato, 50.0f)
         .living (15.0f, 10.0f)
         .macro (0, "Facet", 0.0f).macro (1, "Echo", 0.0f)
         .mod (MS::Macro1, posA(), 40.0f)
         .mod (MS::Macro2, P::DelayMix, 30.0f)
         .delay (ND::EighthDotted, 35.0f, 20.0f)
         .master (8.5f);
        add (r);
    }
    {   // Hollow flute: odd harmonics, breathy pink noise and a slow, human vibrato.
        Recipe r ("Hollow Flute", "Lead", { "hollow", "airy", "soft", "expressive" });
        r.osc (0, Tbl::OddHarmonics, 10.0f, 70.0f)
         .noise (10.0f, Noise::Pink)
         .filter (0, Flt::LP12, 3500.0f, 5.0f, 0.0f, 50.0f)
         .env (0, 25.0f, 300.0f, 90.0f, 250.0f)
         .lfo (0, Wave::Sine, 4.8f).lfoFade (0, 700.0f)
         .mod (MS::Lfo1, fineA(), 10.0f)
         .mono (VMode::Legato, 60.0f)
         .living (20.0f, 10.0f)
         .macro (0, "Breath", 0.0f).macro (1, "Vibrato", 0.0f)
         .mod (MS::Macro1, P::NoiseLevel, 25.0f)
         .mod (MS::Macro2, fineA(), 15.0f)
         .reverb (55.0f, 50.0f, 20.0f)
         .master (-5.5f);
        add (r);
    }
    {   // Screamer: a synced saw into the diode, a phaser for motion.
        Recipe r ("Sync Screamer", "Lead", { "aggressive", "dirty", "mono" });
        r.osc (0, Tbl::SyncSweep, 50.0f, 75.0f).warp (0, Warp::Sync, 35.0f)
         .filter (0, Flt::LP24, 3000.0f, 30.0f, 20.0f, 50.0f)
         .env (0, 2.0f, 300.0f, 95.0f, 200.0f)
         .env (1, 0.0f, 600.0f, 30.0f, 300.0f)
         .mod (MS::Env2, warpA(), 35.0f)
         .mono (VMode::Mono, 30.0f)
         .living (12.0f, 5.0f)
         .macro (0, "Sync", 0.0f).macro (1, "Scream", 0.0f)
         .mod (MS::Macro1, warpA(), 40.0f)
         .mod (MS::Macro2, P::DistDrive, 40.0f)
         .distortion (Dist::Diode, 30.0f, 5000.0f, 55.0f)
         .phaser (0.4f, 50.0f, 40.0f, 30.0f)
         .master (-3.8f);
        add (r);
    }

    // ---- Pad (C3)
    {   // Aurora: choir and glass crossfaded by an orbit, a slow swell and a long hall.
        Recipe r ("Aurora Choir", "Pad", { "vocal", "lush", "evolving", "cinematic" });
        r.osc (0, Tbl::Choir, 40.0f, 65.0f).unison (0, 4, 12.0f, 90.0f)
         .osc (1, Tbl::DigitalGlass, 30.0f, 40.0f).oscPitch (1, 1)
         .filter (0, Flt::LP24, 3200.0f, 10.0f)
         .env (0, 1100.0f, 1200.0f, 85.0f, 2500.0f, 30.0f)
         .orbit (20.0f, 50.0f)
         .mod (MS::Orbit, posA(), 30.0f)
         .mod (MS::Orbit, levelB(), 20.0f)
         .living (25.0f, 45.0f)
         .macro (0, "Shimmer", 0.0f).macro (1, "Space", 0.0f)
         .mod (MS::Macro1, levelB(), 30.0f)
         .mod (MS::Macro2, P::ReverbMix, 30.0f)
         .reverb (85.0f, 75.0f, 32.0f, 30.0f, 30.0f)
         .master (-1.0f);
        add (r);
    }
    {   // Velvet: warm pulse-width pad through a notch that drifts, chorus.
        Recipe r ("Velvet Notch", "Pad", { "warm", "smooth", "soft" });
        r.osc (0, Tbl::PulseWidth, 50.0f, 70.0f).unison (0, 3, 10.0f, 70.0f)
         .filter (0, Flt::Notch, 900.0f, 30.0f)
         .filter (1, Flt::LP12, 2500.0f, 5.0f)
         .env (0, 600.0f, 1000.0f, 85.0f, 1800.0f)
         .lfo (0, Wave::Triangle, 0.15f, Mode::Free)
         .mod (MS::Lfo1, cutoff1(), 30.0f)
         .living (20.0f, 35.0f)
         .macro (0, "Drift", 0.0f).macro (1, "Warmth", 0.0f)
         .mod (MS::Macro1, lfoRate (0), 30.0f)
         .mod (MS::Macro2, cutoff2(), -25.0f)
         .chorus (0.4f, 40.0f, 35.0f)
         .reverb (60.0f, 55.0f, 22.0f)
         .master (2.7f);
        add (r);
    }
    {   // Tidal: the Noise Bloom table rises and falls on a drawn tide LFO, deep and dark.
        Recipe r ("Tidal Bloom", "Pad", { "dark", "evolving", "ambient" });
        r.osc (0, Tbl::NoiseBloom, 20.0f, 60.0f)
         .osc (1, Tbl::SubBass, 20.0f, 45.0f).oscPitch (1, -1)
         .filter (0, Flt::LP24, 1200.0f, 20.0f)
         .env (0, 1500.0f, 1500.0f, 90.0f, 3000.0f, 40.0f)
         .lfo (0, Wave::Custom, 0.1f, Mode::Free)
         .customShape (0, { { 0.0f, 0.0f, 0.0f }, { 0.35f, 1.0f, 0.5f }, { 0.6f, 0.7f, 0.0f }, { 1.0f, 0.0f, -0.5f } })
         .mod (MS::Lfo1, posA(), 45.0f)
         .mod (MS::Lfo1, cutoff1(), 20.0f)
         .living (30.0f, 55.0f)
         .macro (0, "Tide", 0.0f).macro (1, "Depth", 0.0f)
         .mod (MS::Macro1, lfoRate (0), 30.0f)
         .mod (MS::Macro2, levelB(), 30.0f)
         .reverb (80.0f, 70.0f, 30.0f)
         .master (-5.1f);
        add (r);
    }

    // ---- Pluck (C3)
    {   // Marimba: a tuned bell partial, short and woody, hard phase for a repeatable mallet.
        Recipe r ("Bamboo Mallet", "Pluck", { "organic", "percussive", "warm" });
        r.osc (0, Tbl::Bell, 5.0f, 75.0f).hardPhase (0)
         .osc (1, Tbl::BasicShapes, 0.0f, 35.0f).oscPitch (1, 2).hardPhase (1)
         .filter (0, Flt::LP12, 4000.0f, 5.0f, 0.0f, 50.0f)
         .env (0, 0.0f, 450.0f, 0.0f, 300.0f, 0.0f, -60.0f)
         .env (1, 0.0f, 60.0f, 0.0f, 40.0f)
         .mod (MS::Env2, levelB(), 40.0f)
         .living (10.0f, 0.0f)
         .macro (0, "Mallet", 0.0f).macro (1, "Length", 0.0f)
         .mod (MS::Macro1, cutoff1(), 30.0f)
         .mod (MS::Macro2, envDecay (0), 30.0f)
         .reverb (40.0f, 35.0f, 15.0f)
         .master (4.6f);
        add (r);
    }
    {   // Koto: pluck-decay table, a positive comb tuned to the key and sympathetic strings.
        Recipe r ("Koto Strings", "Pluck", { "plucky", "resonant", "organic" });
        r.osc (0, Tbl::PluckDecay, 10.0f, 75.0f).hardPhase (0)
         .filter (0, Flt::CombPlus, 260.0f, 25.0f, 0.0f, 100.0f)
         .filterMix (0, 40.0f)
         .env (0, 0.0f, 420.0f, 0.0f, 300.0f, 0.0f, -65.0f)
         .env (1, 0.0f, 400.0f, 0.0f, 200.0f)
         .mod (MS::Env2, posA(), 40.0f)
         .sympathetic (20.0f, 1, 6, 30.0f, 50.0f)
         .living (15.0f, 0.0f)
         .macro (0, "Twang", 0.0f).macro (1, "Strings", 0.0f)
         .mod (MS::Macro1, res1(), 30.0f)
         .mod (MS::Macro2, P::SympAmount, 30.0f)
         .reverb (45.0f, 40.0f, 15.0f)
         .master (4.7f);
        add (r);
    }
    {   // Digital pluck: an FM sweep snapped shut by Env 2, a dotted echo.
        Recipe r ("Pixel Pluck", "Pluck", { "digital", "bright", "short", "fm" });
        r.osc (0, Tbl::FmSweep, 60.0f, 75.0f).hardPhase (0)
         .osc (1, Tbl::BasicShapes, 0.0f, 50.0f).oscPitch (1, 1).warp (1, Warp::FmOsc, 30.0f)
         .filter (0, Flt::LP24, 6000.0f, 15.0f)
         .env (0, 0.0f, 250.0f, 0.0f, 200.0f, 0.0f, -60.0f)
         .env (1, 0.0f, 120.0f, 0.0f, 80.0f)
         .mod (MS::Env2, posA(), -50.0f)
         .living (10.0f, 0.0f)
         .macro (0, "Bite", 0.0f).macro (1, "Echo", 0.0f)
         .mod (MS::Macro1, warpB(), 40.0f)
         .mod (MS::Macro2, P::DelayMix, 30.0f)
         .delay (ND::EighthDotted, 30.0f, 18.0f)
         .master (0.3f);
        add (r);
    }

    // ---- Keys (C3)
    {   // Rhodes-like tine: sine body, bell tine on Env 2, tremolo on an LFO.
        Recipe r ("Tine Electric", "Keys", { "vintage", "warm", "soft" });
        r.osc (0, Tbl::BasicShapes, 0.0f, 75.0f)
         .osc (1, Tbl::Bell, 10.0f, 35.0f).oscPitch (1, 1)
         .filter (0, Flt::LP12, 4000.0f, 5.0f, 0.0f, 40.0f)
         .env (0, 1.0f, 1800.0f, 30.0f, 400.0f)
         .env (1, 0.0f, 250.0f, 0.0f, 150.0f)
         .mod (MS::Env2, levelB(), 40.0f)
         .lfo (0, Wave::Sine, 4.0f, Mode::Free)
         .mod (MS::Lfo1, panA(), 25.0f)
         .living (15.0f, 5.0f)
         .macro (0, "Tine", 0.0f).macro (1, "Tremolo", 0.0f)
         .mod (MS::Macro1, levelB(), 30.0f)
         .mod (MS::Macro2, panA(), 40.0f)
         .chorus (0.6f, 30.0f, 20.0f)
         .master (0.3f);
        add (r);
    }
    {   // Harpsichord: bright pulse pair a fifth-free octave apart, quick decay, a small room.
        Recipe r ("Baroque Harpsichord", "Keys", { "bright", "classic", "plucky" });
        r.osc (0, Tbl::PulseWidth, 70.0f, 70.0f).hardPhase (0)
         .osc (1, Tbl::PulseWidth, 60.0f, 45.0f).oscPitch (1, 1, 0, 3.0f)
         .filter (0, Flt::HP12, 200.0f, 5.0f)
         .env (0, 0.0f, 1200.0f, 10.0f, 250.0f, 0.0f, -50.0f)
         .voices (12)
         .living (10.0f, 0.0f)
         .macro (0, "Brightness", 0.0f).macro (1, "Second Rank", 0.0f)
         .mod (MS::Macro1, posA(), 30.0f)
         .mod (MS::Macro2, levelB(), 30.0f)
         .reverb (30.0f, 35.0f, 14.0f)
         .master (5.3f);
        add (r);
    }
    {   // Lo-fi keys: dusty organ through crushed bits, a wobbling pitch drift and a soft top.
        Recipe r ("Cassette Keys", "Keys", { "lofi", "dusty", "warm" });
        r.osc (0, Tbl::HarmonicStack, 25.0f, 75.0f)
         .filter (0, Flt::LP12, 2200.0f, 10.0f)
         .env (0, 2.0f, 1500.0f, 40.0f, 500.0f)
         .lfo (0, Wave::Sine, 0.6f, Mode::Free)
         .mod (MS::Lfo1, fineA(), 6.0f)
         .noise (4.0f, Noise::Brown, Route::Direct)
         .living (30.0f, 10.0f)
         .macro (0, "Wow", 0.0f).macro (1, "Crush", 0.0f)
         .mod (MS::Macro1, fineA(), 10.0f)
         .mod (MS::Macro2, P::DistMix, 40.0f)
         .distortion (Dist::Crush, 20.0f, 4000.0f, 30.0f)
         .master (1.2f);
        add (r);
    }

    // ---- FX (C3)
    {   // Riser: noise and saws sweep up over four seconds via Env 3 on pitch and cutoff.
        Recipe r ("Tension Riser", "FX", { "cinematic", "noisy", "evolving" });
        r.osc (0, Tbl::AnalogSaws, 40.0f, 60.0f).unison (0, 6, 25.0f, 90.0f)
         .noise (35.0f, Noise::White)
         .filter (0, Flt::HP12, 200.0f, 20.0f)
         .env (0, 300.0f, 500.0f, 100.0f, 800.0f)
         .env (2, 4000.0f, 100.0f, 100.0f, 500.0f, 30.0f)
         .mod (MS::Env3, pitchA(), 12.0f)
         .mod (MS::Env3, cutoff1(), 50.0f)
         .living (15.0f, 20.0f)
         .macro (0, "Rise", 0.0f).macro (1, "Noise", 0.0f)
         .mod (MS::Macro1, pitchA(), 10.0f)
         .mod (MS::Macro2, P::NoiseLevel, 30.0f)
         .reverb (80.0f, 70.0f, 30.0f)
         .master (2.0f);
        add (r);
    }
    {   // Radio static: S&H on cutoff and position, a band-pass and a bit crusher.
        Recipe r ("Shortwave Static", "FX", { "glitchy", "lofi", "noisy" });
        r.osc (0, Tbl::Growl, 50.0f, 80.0f)
         .noise (60.0f, Noise::Blue)
         .filter (0, Flt::BP24, 1500.0f, 50.0f)
         .env (0, 5.0f, 500.0f, 90.0f, 400.0f)
         .lfo (0, Wave::SampleHold, 9.0f, Mode::Free)
         .mod (MS::Lfo1, cutoff1(), 45.0f)
         .mod (MS::Lfo1, posA(), 40.0f)
         .living (20.0f, 20.0f)
         .macro (0, "Tuning", 0.0f).macro (1, "Crush", 0.0f)
         .mod (MS::Macro1, lfoRate (0), 30.0f)
         .mod (MS::Macro2, P::DistDrive, 40.0f)
         .distortion (Dist::Crush, 35.0f, 6000.0f, 60.0f)
         .master (9.0f);
        add (r);
    }
    {   // Metallic impact: a bell hit with a falling pitch and a flanged, long tail.
        Recipe r ("Metal Impact", "FX", { "metallic", "cinematic", "percussive" });
        r.osc (0, Tbl::Bell, 60.0f, 75.0f).hardPhase (0)
         .osc (1, Tbl::DigitalGlass, 50.0f, 45.0f).oscPitch (1, 0, 6)
         .filter (0, Flt::LP24, 6000.0f, 20.0f)
         .env (0, 0.0f, 2500.0f, 0.0f, 2000.0f, 0.0f, -50.0f)
         .env (1, 0.0f, 600.0f, 0.0f, 400.0f)
         .pitchEnv (MS::Env2, -6.0f)
         .living (10.0f, 10.0f)
         .macro (0, "Weight", 0.0f).macro (1, "Space", 0.0f)
         .mod (MS::Macro1, pitchA(), -6.0f)
         .mod (MS::Macro2, P::ReverbMix, 35.0f)
         .flanger (0.15f, 50.0f, 50.0f, 35.0f)
         .reverb (85.0f, 75.0f, 35.0f)
         .master (3.3f);
        add (r);
    }

    // ---- Texture (C3)
    {   // Rainfall: violet noise through a resonant band, a bouncing ball sprinkles the cutoff.
        Recipe r ("Rainfall Glass", "Texture", { "glassy", "noisy", "ambient" });
        r.osc (0, Tbl::DigitalGlass, 60.0f, 65.0f)
         .noise (85.0f, Noise::Violet)
         .filter (0, Flt::BP12, 3000.0f, 45.0f)
         .env (0, 400.0f, 500.0f, 100.0f, 2000.0f)
         .ball (40.0f, 85.0f)
         .mod (MS::Ball, cutoff1(), 40.0f)
         .lfo (0, Wave::Sine, 0.07f, Mode::Free)
         .mod (MS::Lfo1, posA(), 40.0f)
         .living (25.0f, 45.0f)
         .macro (0, "Drops", 0.0f).macro (1, "Glass", 0.0f)
         .mod (MS::Macro1, res1(), 30.0f)
         .mod (MS::Macro2, levelA(), 30.0f)
         .reverb (75.0f, 70.0f, 30.0f)
         .master (8.0f);
        add (r);
    }
    {   // Engine room: a low hum, brown noise and a slow comb throb.
        Recipe r ("Engine Room", "Texture", { "dark", "drone", "metallic" });
        r.osc (0, Tbl::OddHarmonics, 30.0f, 60.0f).oscPitch (0, -1)
         .noise (25.0f, Noise::Brown)
         .filter (0, Flt::CombMinus, 120.0f, 50.0f, 10.0f)
         .filter (1, Flt::LP24, 1800.0f, 15.0f)
         .env (0, 500.0f, 500.0f, 100.0f, 1500.0f)
         .lfo (0, Wave::Triangle, 0.25f, Mode::Free)
         .mod (MS::Lfo1, cutoff1(), 25.0f)
         .living (30.0f, 40.0f)
         .macro (0, "Throb", 0.0f).macro (1, "Rumble", 0.0f)
         .mod (MS::Macro1, lfoRate (0), 35.0f)
         .mod (MS::Macro2, P::NoiseLevel, 30.0f)
         .reverb (50.0f, 55.0f, 15.0f)
         .master (-3.0f);
        add (r);
    }
    {   // Wind chimes: bell table scanned by S&H, sympathetic strings ringing underneath.
        Recipe r ("Wind Chimes", "Texture", { "metallic", "airy", "evolving", "tonal" });
        r.osc (0, Tbl::Bell, 40.0f, 55.0f)
         .noise (15.0f, Noise::Pink)
         .filter (0, Flt::HP12, 600.0f, 10.0f)
         .env (0, 300.0f, 500.0f, 100.0f, 2500.0f)
         .lfo (0, Wave::SampleHold, 3.0f, Mode::Free)
         .lfoSmooth (0, 40.0f)
         .mod (MS::Lfo1, posA(), 50.0f)
         .sympathetic (35.0f, 2, 8, 70.0f, 60.0f)
         .living (25.0f, 50.0f)
         .macro (0, "Breeze", 0.0f).macro (1, "Ring", 0.0f)
         .mod (MS::Macro1, lfoRate (0), 30.0f)
         .mod (MS::Macro2, P::SympAmount, 30.0f)
         .reverb (80.0f, 70.0f, 35.0f)
         .master (-4.3f);
        add (r);
    }

    // ---- Sequence (C3)
    {   // Dorian walk: an 8-step scale-degree line in D Dorian, snapped, with a filter accent per step.
        Recipe r ("Dorian Walk", "Sequence", { "sequenced", "warm", "rhythmic" });
        r.osc (0, Tbl::AnalogSaws, 20.0f, 75.0f)
         .sub (25.0f, Sub::Sine, 1)
         .filter (0, Flt::LP24, 1200.0f, 30.0f)
         .env (0, 0.5f, 220.0f, 0.0f, 120.0f)
         .key (Key::D, ST::Dorian)
         .seq (ND::Sixteenth, 65.0f, true,
               { { 0, 100.0f, true, 80.0f }, { 2, 70.0f, true, 30.0f }, { 4, 85.0f, true, 60.0f }, { 3, 70.0f, true, 20.0f },
                 { 1, 100.0f, true, 90.0f }, { 4, 75.0f, false, 40.0f }, { 6, 90.0f, true, 70.0f }, { 2, 80.0f, true, 30.0f } })
         .mod (MS::SeqMod, cutoff1(), 35.0f)
         .living (15.0f, 0.0f)
         .macro (0, "Accent", 0.0f).macro (1, "Echo", 0.0f)
         .mod (MS::Macro1, res1(), 30.0f)
         .mod (MS::Macro2, P::DelayMix, 30.0f)
         .delay (ND::Eighth, 30.0f, 15.0f)
         .master (1.0f);
        add (r);
    }
    {   // Triplet arp: a chord arpeggio in eighth triplets over two octaves, glassy and bright.
        Recipe r ("Triplet Cascade", "Sequence", { "bright", "glassy", "sequenced" });
        r.osc (0, Tbl::DigitalGlass, 20.0f, 75.0f)
         .filter (0, Flt::LP12, 5000.0f, 10.0f)
         .env (0, 0.5f, 200.0f, 0.0f, 150.0f)
         .arp (ArpM::Up, 2, ND::EighthTriplet, 55.0f)
         .living (12.0f, 0.0f)
         .macro (0, "Gate", 0.0f).macro (1, "Space", 0.0f)
         .mod (MS::Macro1, envDecay (0), 30.0f)
         .mod (MS::Macro2, P::ReverbMix, 30.0f)
         .delay (ND::QuarterTriplet, 25.0f, 15.0f)
         .reverb (50.0f, 45.0f, 15.0f)
         .master (6.5f);
        add (r);
    }
    {   // Acid sequence: a 16-step acid line, ladder squelch on Seq Mod, legato slides.
        Recipe r ("Acid Steps", "Sequence", { "acid", "resonant", "sequenced", "mono" });
        r.osc (0, Tbl::AnalogSaws, 0.0f, 75.0f)
         .filter (0, Flt::Ladder, 500.0f, 65.0f, 25.0f, 30.0f)
         .env (0, 0.5f, 200.0f, 20.0f, 80.0f)
         .seq (ND::Sixteenth, 55.0f, false,
               { { 0, 100.0f, true, 90.0f }, { 0, 60.0f, true, 20.0f }, { 12, 90.0f, true, 70.0f }, { 0, 60.0f, false, 10.0f },
                 { 3, 100.0f, true, 100.0f }, { 0, 60.0f, true, 30.0f }, { 10, 80.0f, true, 60.0f }, { 0, 60.0f, true, 20.0f },
                 { 0, 100.0f, true, 85.0f }, { 7, 70.0f, true, 40.0f }, { 12, 90.0f, true, 75.0f }, { 0, 60.0f, false, 10.0f },
                 { 5, 100.0f, true, 95.0f }, { 0, 60.0f, true, 30.0f }, { 3, 80.0f, true, 50.0f }, { -12, 90.0f, true, 40.0f } })
         .mod (MS::SeqMod, cutoff1(), 50.0f)
         .mono (VMode::Mono, 0.0f)
         .living (15.0f, 0.0f)
         .macro (0, "Squelch", 0.0f).macro (1, "Drive", 0.0f)
         .mod (MS::Macro1, res1(), 25.0f)
         .mod (MS::Macro2, P::DistDrive, 40.0f)
         .distortion (Dist::Tube, 25.0f, 6000.0f, 50.0f)
         .master (6.1f);
        add (r);
    }

    // ---- Drums/Perc (C3)
    {   const char* cat = "Drums/Perc";
        {   // Tom: a tuned sine with a pitch drop and a little noise skin.
            Recipe r ("Floor Tom", cat, { "percussive", "deep", "punchy" });
            r.osc (0, Tbl::BasicShapes, 0.0f, 85.0f).oscPitch (0, -1, 7).hardPhase (0, 90.0f)
             .noise (10.0f, Noise::Pink, Route::Filter1)
             .filter (0, Flt::LP24, 3000.0f, 5.0f)
             .env (0, 0.0f, 450.0f, 0.0f, 200.0f, 0.0f, -45.0f)
             .env (1, 0.0f, 150.0f, 0.0f, 100.0f, 0.0f, -50.0f)
             .pitchEnv (MS::Env2, 8.0f, false)
             .mono (VMode::Mono, 0.0f)
             .living (5.0f, 0.0f)
             .macro (0, "Tune", 0.0f).macro (1, "Length", 0.0f)
             .mod (MS::Macro1, pitchA(), 8.0f)
             .mod (MS::Macro2, envDecay (0), 30.0f)
             .master (-2.8f);
            add (r);
        }
        {   // Cross stick: a high, hard-phased triangle tick and a white-noise snap, gone in 80 ms.
            Recipe r ("Cross Stick", cat, { "percussive", "short", "bright" });
            r.osc (0, Tbl::BasicShapes, 6.0f, 100.0f).oscPitch (0, 1, 5).hardPhase (0)
             .noise (70.0f, Noise::White, Route::Filter1)
             .filter (0, Flt::HP12, 900.0f, 15.0f)
             .env (0, 0.0f, 80.0f, 0.0f, 60.0f, 0.0f, -70.0f)
             .env (1, 0.0f, 25.0f, 0.0f, 20.0f)
             .mod (MS::Env2, P::NoiseLevel, 40.0f)
             .living (6.0f, 0.0f)
             .macro (0, "Tone", 0.0f).macro (1, "Snap", 0.0f)
             .mod (MS::Macro1, cutoff1(), 30.0f)
             .mod (MS::Macro2, P::NoiseLevel, 30.0f)
             .reverb (25.0f, 25.0f, 10.0f)
             .master (8.0f);
            add (r);
        }
        {   // Cowbell: two detuned pulses a minor sixth-ish apart through a band-pass.
            Recipe r ("Disco Cowbell", cat, { "percussive", "metallic", "retro" });
            r.osc (0, Tbl::PulseWidth, 50.0f, 75.0f).oscPitch (0, 1, 1).hardPhase (0)
             .osc (1, Tbl::PulseWidth, 50.0f, 75.0f).oscPitch (1, 1, 8).hardPhase (1)
             .filter (0, Flt::BP12, 1600.0f, 20.0f)
             .env (0, 0.0f, 350.0f, 0.0f, 150.0f, 0.0f, -60.0f)
             .voices (4)
             .living (5.0f, 0.0f)
             .macro (0, "Tone", 0.0f).macro (1, "Decay", 0.0f)
             .mod (MS::Macro1, cutoff1(), 30.0f)
             .mod (MS::Macro2, envDecay (0), 30.0f)
             .master (8.5f);
            add (r);
        }
    }
}

} // namespace verdant::factory
