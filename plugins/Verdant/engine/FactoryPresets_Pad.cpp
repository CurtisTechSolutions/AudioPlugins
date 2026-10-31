// Factory bank: Pad.

#include "FactoryPresets_Recipes.h"

namespace verdant::factory
{

void addPadPresets (std::vector<Patch>& bank)
{
    auto add = [&bank] (Recipe& r) { bank.push_back (r.p); };

    {   // Soft, living pad.
        Recipe r ("Soft Pad", "Pad", { "soft", "warm", "wide" });
        r.osc (0, Tbl::HarmonicStack, 20.0f, 75.0f).unison (0, 3, 12.0f)
         .osc (1, Tbl::HarmonicStack, 35.0f, 45.0f).oscPitch (1, 0, 0, -6.0f)
         .filter (0, Flt::LP24, 3000.0f, 12.0f)
         .env (0, 400.0f, 800.0f, 70.0f, 1200.0f)
         .living (30.0f, 45.0f)
         .macro (0, "Brightness", 0.0f).macro (1, "Space", 0.0f)
         .mod (MS::Macro1, cutoff1(), 25.0f)
         .mod (MS::Macro2, P::ReverbMix, 30.0f)
         .chorus (0.5f, 45.0f, 40.0f)
         .reverb (65.0f, 60.0f, 30.0f)
         .master (3.5f);
        add (r);
    }
    {   // Blooming noise table into choir, a slow Env 2 scan and lots of evolve.
        Recipe r ("Verdant Bloom", "Pad", { "evolving", "lush", "wide" });
        r.osc (0, Tbl::NoiseBloom, 0.0f, 65.0f)
         .osc (1, Tbl::Choir, 30.0f, 55.0f)
         .filter (0, Flt::LP24, 4500.0f, 15.0f)
         .env (0, 900.0f, 1500.0f, 80.0f, 2500.0f)
         .env (1, 2500.0f, 3000.0f, 60.0f, 2000.0f)
         .mod (MS::Env2, posA(), 60.0f)
         .mod (MS::Env2, posB(), 25.0f)
         .living (35.0f, 60.0f)
         .macro (0, "Bloom", 0.0f).macro (1, "Brightness", 0.0f)
         .mod (MS::Macro1, posA(), 35.0f)
         .mod (MS::Macro2, cutoff1(), 25.0f)
         .reverb (80.0f, 70.0f, 35.0f)
         .master (-3.5f);
        add (r);
    }
    {   // Choir of glass: wide unison choir, a drawn LFO scanning the glass table.
        Recipe r ("Choir of Glass", "Pad", { "vocal", "evolving", "airy" });
        r.osc (0, Tbl::Choir, 40.0f, 70.0f).unison (0, 8, 14.0f, 80.0f)
         .osc (1, Tbl::DigitalGlass, 10.0f, 40.0f).oscPitch (1, 1)
         .filter (0, Flt::LP12, 6000.0f, 10.0f)
         .env (0, 700.0f, 1000.0f, 80.0f, 1800.0f)
         .lfo (2, Wave::Custom, 0.2f, Mode::Free)
         .customShape (2, { { 0.0f, 0.2f, 0.0f }, { 0.3f, 0.9f, 0.5f }, { 0.55f, 0.5f, 0.0f }, { 0.8f, 1.0f, -0.3f }, { 1.0f, 0.2f, 0.4f } })
         .mod (MS::Lfo3, posB(), 50.0f)
         .living (25.0f, 35.0f)
         .macro (0, "Glass", 0.0f).macro (1, "Motion", 0.0f)
         .mod (MS::Macro1, levelB(), 35.0f)
         .mod (MS::Macro2, lfoRate (2), 35.0f)
         .reverb (75.0f, 65.0f, 32.0f)
         .master (-3.5f);
        add (r);
    }
    {   // Orbiting pad: the Orbit source walks position and cutoff in a slow ellipse.
        Recipe r ("Orbit Pad", "Pad", { "evolving", "physical", "wide" });
        r.osc (0, Tbl::HarmonicStack, 50.0f, 75.0f).unison (0, 4, 10.0f)
         .filter (0, Flt::LP24, 2200.0f, 20.0f)
         .env (0, 600.0f, 1000.0f, 80.0f, 1500.0f)
         .orbit (25.0f, 55.0f)
         .mod (MS::Orbit, posA(), 35.0f)
         .mod (MS::Orbit, cutoff1(), 20.0f)
         .living (35.0f, 50.0f)
         .macro (0, "Orbit Depth", 0.0f).macro (1, "Brightness", 0.0f)
         .mod (MS::Macro1, posA(), 25.0f)
         .mod (MS::Macro2, cutoff1(), 25.0f)
         .chorus (0.3f, 40.0f, 35.0f)
         .reverb (70.0f, 60.0f, 30.0f)
         .master (2.0f);
        add (r);
    }
    {   // String ensemble from a wide saw stack.
        Recipe r ("Warm Strings", "Pad", { "warm", "lush", "classic" });
        r.osc (0, Tbl::AnalogSaws, 35.0f, 75.0f).unison (0, 8, 18.0f, 85.0f)
         .filter (0, Flt::LP12, 2600.0f, 8.0f, 0.0f, 30.0f)
         .env (0, 550.0f, 900.0f, 85.0f, 1500.0f)
         .lfo (0, Wave::Sine, 4.8f).lfoFade (0, 800.0f)
         .mod (MS::Lfo1, pitchA(), 1.0f)
         .living (25.0f, 30.0f)
         .macro (0, "Bow", 0.0f).macro (1, "Ensemble", 0.0f)
         .mod (MS::Macro1, cutoff1(), 25.0f)
         .mod (MS::Macro2, detuneA(), 30.0f)
         .chorus (0.7f, 40.0f, 40.0f)
         .reverb (60.0f, 55.0f, 28.0f)
         .master (1.0f);
        add (r);
    }
    {   // Dark matter: low growl table, resonant LP, S&H-stepped cutoff, phaser.
        Recipe r ("Dark Matter", "Pad", { "dark", "cinematic", "growl" });
        r.osc (0, Tbl::Growl, 70.0f, 75.0f)
         .osc (1, Tbl::SubBass, 40.0f, 40.0f).oscPitch (1, -1)
         .filter (0, Flt::LP24, 700.0f, 45.0f, 10.0f)
         .env (0, 800.0f, 1200.0f, 80.0f, 2000.0f)
         .lfo (0, Wave::SampleHold, 2.0f).lfoSmooth (0, 60.0f)
         .mod (MS::Lfo1, cutoff1(), 18.0f)
         .living (25.0f, 40.0f)
         .macro (0, "Unrest", 0.0f).macro (1, "Depth", 0.0f)
         .mod (MS::Macro1, lfoRate (0), 30.0f)
         .mod (MS::Macro2, levelB(), 35.0f)
         .phaser (0.15f, 70.0f, 40.0f, 45.0f)
         .reverb (70.0f, 70.0f, 30.0f)
         .master (-7.5f);
        add (r);
    }
    {   // Three macros: Morph (both positions), Air (reverb), Bright (cutoff).
        Recipe r ("Macro Morph Pad", "Pad", { "evolving", "vocal", "lush" });
        r.osc (0, Tbl::Choir, 10.0f, 70.0f)
         .osc (1, Tbl::FormantVowels, 10.0f, 50.0f)
         .filter (0, Flt::LP24, 2000.0f, 15.0f)
         .env (0, 500.0f, 1000.0f, 80.0f, 1600.0f)
         .living (25.0f, 30.0f)
         .macro (0, "Morph", 25.0f).macro (1, "Air", 30.0f).macro (2, "Bright", 35.0f)
         .mod (MS::Macro1, posA(), 70.0f)
         .mod (MS::Macro1, posB(), 70.0f)
         .mod (MS::Macro2, P::ReverbMix, 50.0f)
         .mod (MS::Macro3, cutoff1(), 50.0f)
         .reverb (65.0f, 60.0f, 15.0f)
         .master (-4.0f);
        add (r);
    }
    {   // Breathing pad: a drawn inhale / exhale LFO on the filter, pink noise, flanger.
        Recipe r ("Breathing Pad", "Pad", { "airy", "evolving", "soft" });
        r.osc (0, Tbl::FormantVowels, 40.0f, 70.0f)
         .noise (12.0f, Noise::Pink)
         .filter (0, Flt::LP24, 1600.0f, 20.0f)
         .env (0, 700.0f, 1000.0f, 85.0f, 1800.0f)
         .lfo (0, Wave::Custom, 0.18f, Mode::Free)
         .customShape (0, { { 0.0f, 0.0f, 0.0f }, { 0.4f, 1.0f, 0.7f }, { 0.55f, 1.0f, 0.0f }, { 1.0f, 0.0f, -0.7f } })
         .mod (MS::Lfo1, cutoff1(), 35.0f)
         .living (20.0f, 40.0f)
         .macro (0, "Breath Rate", 0.0f).macro (1, "Air", 0.0f)
         .mod (MS::Macro1, lfoRate (0), 30.0f)
         .mod (MS::Macro2, P::NoiseLevel, 25.0f)
         .flanger (0.1f, 40.0f, 30.0f, 30.0f)
         .reverb (70.0f, 60.0f, 30.0f)
         .master (1.5f);
        add (r);
    }
    {   // Glacier wall: two 16-voice saw stacks an octave apart, a slow swell and a huge hall.
        Recipe r ("Glacier Wall", "Pad", { "wide", "bright", "cinematic", "detuned" });
        r.osc (0, Tbl::AnalogSaws, 55.0f, 70.0f).unison (0, 16, 22.0f, 100.0f, 80.0f)
         .osc (1, Tbl::AnalogSaws, 70.0f, 40.0f).unison (1, 8, 18.0f, 100.0f, 80.0f).oscPitch (1, 1)
         .filter (0, Flt::LP24, 5000.0f, 10.0f)
         .env (0, 900.0f, 1000.0f, 90.0f, 2500.0f, 30.0f)
         .living (20.0f, 35.0f)
         .macro (0, "Width", 0.0f).macro (1, "Brightness", 0.0f).macro (2, "Space", 0.0f)
         .mod (MS::Macro1, detuneA(), 30.0f)
         .mod (MS::Macro2, cutoff1(), 25.0f)
         .mod (MS::Macro3, P::ReverbDecay, 30.0f)
         .reverb (85.0f, 75.0f, 30.0f, 35.0f, 40.0f)
         .master (-4.5f);
        add (r);
    }
    {   // Polysynth sweep: pulse-width pad whose filter slowly opens over each held chord.
        Recipe r ("Polysynth Sweep", "Pad", { "retro", "warm", "evolving" });
        r.osc (0, Tbl::PulseWidth, 30.0f, 75.0f).unison (0, 2, 10.0f, 60.0f)
         .osc (1, Tbl::BasicShapes, 39.0f, 55.0f).oscPitch (1, 0, 0, -8.0f)
         .filter (0, Flt::LP24, 500.0f, 30.0f, 0.0f, 40.0f)
         .env (0, 300.0f, 1000.0f, 85.0f, 1200.0f)
         .env (1, 1500.0f, 2000.0f, 70.0f, 1500.0f)
         .mod (MS::Env2, cutoff1(), 35.0f)
         .lfo (0, Wave::Triangle, 0.35f, Mode::Free)
         .mod (MS::Lfo1, posA(), 25.0f)
         .living (25.0f, 30.0f)
         .macro (0, "Sweep", 0.0f).macro (1, "Chorus", 0.0f)
         .mod (MS::Macro1, cutoff1(), 25.0f)
         .mod (MS::Macro2, P::ChorusMix, 40.0f)
         .chorus (0.5f, 45.0f, 35.0f)
         .reverb (55.0f, 55.0f, 22.0f)
         .master (-2.0f);
        add (r);
    }
    {   // Hollow halls: odd harmonics through a positive comb that tracks the keyboard.
        Recipe r ("Hollow Halls", "Pad", { "hollow", "resonant", "ambient" });
        r.osc (0, Tbl::OddHarmonics, 50.0f, 70.0f).unison (0, 4, 12.0f, 80.0f)
         .filter (0, Flt::CombPlus, 260.0f, 45.0f, 0.0f, 100.0f)
         .filter (1, Flt::LP12, 3500.0f, 5.0f)
         .env (0, 800.0f, 1000.0f, 80.0f, 2000.0f)
         .lfo (0, Wave::Sine, 0.12f, Mode::Free)
         .mod (MS::Lfo1, posA(), 30.0f)
         .living (25.0f, 45.0f)
         .macro (0, "Resonance", 0.0f).macro (1, "Brightness", 0.0f)
         .mod (MS::Macro1, res1(), 30.0f)
         .mod (MS::Macro2, cutoff2(), 25.0f)
         .reverb (75.0f, 65.0f, 30.0f)
         .master (-8.5f);
        add (r);
    }
    {   // Cathedral air: organ pipes and choir far away, a long pre-delayed hall.
        Recipe r ("Cathedral Air", "Pad", { "ambient", "airy", "vocal" });
        r.osc (0, Tbl::Organ, 20.0f, 60.0f)
         .osc (1, Tbl::Choir, 60.0f, 55.0f).unison (1, 4, 10.0f, 80.0f)
         .filter (0, Flt::LP12, 3000.0f, 5.0f)
         .env (0, 1200.0f, 1000.0f, 90.0f, 3000.0f)
         .living (20.0f, 40.0f)
         .macro (0, "Choir", 0.0f).macro (1, "Distance", 0.0f)
         .mod (MS::Macro1, levelB(), 30.0f)
         .mod (MS::Macro2, P::ReverbMix, 40.0f)
         .eq (-4.0f, 0.0f, 1000.0f, 2.0f)
         .reverb (95.0f, 85.0f, 40.0f, 35.0f, 80.0f)
         .master (-1.0f);
        add (r);
    }
    {   // FM silk: a soft FM pad; Osc B at the octave modulates A, LFO-breathed index.
        Recipe r ("FM Silk", "Pad", { "fm", "soft", "glassy" });
        r.osc (0, Tbl::BasicShapes, 0.0f, 80.0f).warp (0, Warp::FmOsc, 15.0f).unison (0, 3, 8.0f, 70.0f)
         .osc (1, Tbl::BasicShapes, 0.0f, 25.0f).oscPitch (1, 1)
         .filter (0, Flt::LP12, 6000.0f, 5.0f)
         .env (0, 600.0f, 1000.0f, 85.0f, 1800.0f)
         .lfo (0, Wave::Sine, 0.15f, Mode::Free)
         .mod (MS::Lfo1, warpA(), 12.0f)
         .living (25.0f, 45.0f)
         .macro (0, "Index", 0.0f).macro (1, "Space", 0.0f)
         .mod (MS::Macro1, warpA(), 30.0f)
         .mod (MS::Macro2, P::ReverbMix, 30.0f)
         .chorus (0.3f, 35.0f, 30.0f)
         .reverb (70.0f, 65.0f, 28.0f)
         .master (-3.0f);
        add (r);
    }
    {   // Bell cloud: slow-attack bells with a sympathetic halo that keeps the last chord ringing.
        Recipe r ("Bell Cloud", "Pad", { "metallic", "ambient", "resonant" });
        r.osc (0, Tbl::Bell, 15.0f, 70.0f).unison (0, 4, 8.0f, 90.0f)
         .osc (1, Tbl::HarmonicStack, 10.0f, 40.0f).oscPitch (1, -1)
         .filter (0, Flt::LP12, 5000.0f, 5.0f)
         .env (0, 500.0f, 1500.0f, 75.0f, 2500.0f)
         .living (25.0f, 40.0f)
         .macro (0, "Halo", 0.0f).macro (1, "Bell", 0.0f)
         .mod (MS::Macro1, P::SympAmount, 30.0f)
         .mod (MS::Macro2, posA(), 35.0f)
         .sympathetic (35.0f, 8, 10, 70.0f, 55.0f)
         .reverb (70.0f, 65.0f, 28.0f)
         .master (-6.0f);
        add (r);
    }
    {   // Sync aurora: hard-sync shimmer that drifts as a slow LFO sweeps the sync ratio.
        Recipe r ("Sync Aurora", "Pad", { "bright", "evolving", "digital" });
        r.osc (0, Tbl::SyncSweep, 20.0f, 70.0f).warp (0, Warp::Sync, 20.0f).unison (0, 5, 14.0f, 90.0f)
         .filter (0, Flt::LP24, 4000.0f, 15.0f)
         .env (0, 900.0f, 1000.0f, 85.0f, 2000.0f)
         .lfo (0, Wave::Triangle, 0.08f, Mode::Free)
         .mod (MS::Lfo1, warpA(), 30.0f)
         .living (25.0f, 50.0f)
         .macro (0, "Sync", 0.0f).macro (1, "Drift", 0.0f)
         .mod (MS::Macro1, warpA(), 30.0f)
         .mod (MS::Macro2, lfoRate (0), 35.0f)
         .phaser (0.1f, 50.0f, 30.0f, 30.0f, 2)
         .reverb (75.0f, 70.0f, 30.0f)
         .master (1.5f);
        add (r);
    }
    {   // Low pressure: a dark sub-bass-table swell over a growl layer, the weather under a mix.
        Recipe r ("Low Pressure", "Pad", { "dark", "deep", "sub", "cinematic" });
        r.osc (0, Tbl::SubBass, 60.0f, 75.0f).unison (0, 3, 10.0f, 60.0f)
         .osc (1, Tbl::Growl, 20.0f, 30.0f)
         .filter (0, Flt::LP24, 600.0f, 25.0f, 15.0f)
         .env (0, 1500.0f, 1000.0f, 90.0f, 3000.0f)
         .lfo (0, Wave::Sine, 0.07f, Mode::Free)
         .mod (MS::Lfo1, cutoff1(), 15.0f)
         .living (20.0f, 50.0f)
         .macro (0, "Pressure", 0.0f).macro (1, "Grit", 0.0f)
         .mod (MS::Macro1, cutoff1(), 25.0f)
         .mod (MS::Macro2, levelB(), 35.0f)
         .reverb (70.0f, 70.0f, 25.0f, 60.0f)
         .master (-2.5f);
        add (r);
    }
    {   // Ice lattice: mirrored digital glass, a slow drawn LFO folding it, high-passed and glittering.
        Recipe r ("Ice Lattice", "Pad", { "glassy", "bright", "digital", "evolving" });
        r.osc (0, Tbl::DigitalGlass, 40.0f, 70.0f).warp (0, Warp::Mirror, 20.0f).unison (0, 4, 10.0f, 90.0f)
         .filter (0, Flt::HP12, 400.0f, 10.0f)
         .env (0, 700.0f, 1000.0f, 80.0f, 2000.0f)
         .lfo (1, Wave::Custom, 0.1f, Mode::Free)
         .customShape (1, { { 0.0f, 0.0f, 0.0f }, { 0.2f, 0.6f, 0.0f }, { 0.35f, 0.3f, 0.0f }, { 0.7f, 1.0f, 0.5f }, { 1.0f, 0.0f, -0.5f } })
         .mod (MS::Lfo2, warpA(), 30.0f)
         .mod (MS::Lfo2, posA(), 15.0f)
         .living (25.0f, 45.0f)
         .macro (0, "Fold", 0.0f).macro (1, "Shimmer", 0.0f)
         .mod (MS::Macro1, warpA(), 30.0f)
         .mod (MS::Macro2, P::DelayMix, 30.0f)
         .delay (ND::QuarterDotted, 45.0f, 15.0f)
         .reverb (75.0f, 70.0f, 30.0f)
         .master (3.0f);
        add (r);
    }
    {   // Vox cloud: a vowel pad; the formant filter wanders on a drawn LFO through the vowels.
        Recipe r ("Vox Cloud", "Pad", { "vocal", "lush", "evolving" });
        r.osc (0, Tbl::FormantVowels, 20.0f, 100.0f).unison (0, 5, 12.0f, 80.0f)
         .filter (0, Flt::Formant, 1500.0f, 10.0f)
         .filterMix (0, 80.0f)
         .env (0, 800.0f, 1000.0f, 85.0f, 1800.0f)
         .lfo (0, Wave::Custom, 0.15f, Mode::Free)
         .customShape (0, { { 0.0f, 0.1f, 0.3f }, { 0.25f, 0.6f, 0.0f }, { 0.5f, 0.3f, -0.3f }, { 0.75f, 0.9f, 0.0f }, { 1.0f, 0.1f, 0.0f } })
         .mod (MS::Lfo1, cutoff1(), 25.0f)
         .living (25.0f, 45.0f)
         .macro (0, "Vowel", 0.0f).macro (1, "Motion", 0.0f)
         .mod (MS::Macro1, cutoff1(), 25.0f)
         .mod (MS::Macro2, lfoRate (0), 35.0f)
         .chorus (0.35f, 40.0f, 30.0f)
         .reverb (70.0f, 65.0f, 30.0f)
         .master (10.0f);
        add (r);
    }
    {   // Dust halo: noise-bloom grain, high-passed and airy, with violet noise sparkle.
        Recipe r ("Dust Halo", "Pad", { "airy", "dusty", "noisy", "ambient" });
        r.osc (0, Tbl::NoiseBloom, 50.0f, 70.0f).unison (0, 8, 15.0f, 90.0f)
         .noise (8.0f, Noise::Violet)
         .filter (0, Flt::HP24, 700.0f, 10.0f)
         .filter (1, Flt::LP12, 9000.0f, 5.0f)
         .env (0, 1000.0f, 1000.0f, 85.0f, 2500.0f)
         .lfo (0, Wave::Triangle, 0.05f, Mode::Free)
         .mod (MS::Lfo1, posA(), 35.0f)
         .living (20.0f, 50.0f)
         .macro (0, "Dust", 0.0f).macro (1, "Body", 0.0f)
         .mod (MS::Macro1, P::NoiseLevel, 20.0f)
         .mod (MS::Macro2, cutoff1(), -25.0f)
         .reverb (85.0f, 75.0f, 35.0f)
         .master (4.0f);
        add (r);
    }
    {   // Pebble pad: the bouncing ball skips across the wavetable after each chord, then settles.
        Recipe r ("Pebble Pad", "Pad", { "physical", "soft", "evolving" });
        r.osc (0, Tbl::HarmonicStack, 30.0f, 70.0f).unison (0, 3, 10.0f, 70.0f)
         .osc (1, Tbl::Bell, 10.0f, 30.0f).oscPitch (1, 1)
         .filter (0, Flt::LP24, 3000.0f, 15.0f)
         .env (0, 400.0f, 1000.0f, 80.0f, 1800.0f)
         .ball (35.0f, 85.0f)
         .mod (MS::Ball, posA(), 35.0f)
         .mod (MS::Ball, levelB(), 20.0f)
         .living (25.0f, 35.0f)
         .macro (0, "Skip", 0.0f).macro (1, "Brightness", 0.0f)
         .mod (MS::Macro1, posA(), 25.0f)
         .mod (MS::Macro2, cutoff1(), 25.0f)
         .reverb (65.0f, 60.0f, 28.0f)
         .master (-2.0f);
        add (r);
    }
    {   // Elastic pad: the Spring source detunes the two oscillators apart and back on every chord.
        Recipe r ("Elastic Pad", "Pad", { "physical", "detuned", "warm" });
        r.osc (0, Tbl::AnalogSaws, 25.0f, 70.0f).unison (0, 4, 10.0f, 70.0f)
         .osc (1, Tbl::PulseWidth, 20.0f, 50.0f)
         .filter (0, Flt::LP24, 2200.0f, 15.0f)
         .env (0, 500.0f, 1000.0f, 80.0f, 1500.0f)
         .spring (25.0f, 15.0f)
         .mod (MS::Spring, fineA(), 15.0f)
         .mod (MS::Spring, fineB(), -15.0f)
         .living (25.0f, 30.0f)
         .macro (0, "Stretch", 0.0f).macro (1, "Brightness", 0.0f)
         .mod (MS::Macro1, detuneA(), 30.0f)
         .mod (MS::Macro2, cutoff1(), 25.0f)
         .chorus (0.4f, 40.0f, 30.0f)
         .reverb (60.0f, 55.0f, 25.0f)
         .master (-2.5f);
        add (r);
    }
    {   // Squeeze swell: Env 2 squeezes a pulse stack open over two seconds, warm and rising.
        Recipe r ("Squeeze Swell", "Pad", { "warm", "evolving", "retro" });
        r.osc (0, Tbl::PulseWidth, 50.0f, 70.0f).warp (0, Warp::Squeeze, -40.0f).unison (0, 8, 14.0f, 85.0f)
         .filter (0, Flt::LP24, 1800.0f, 15.0f)
         .env (0, 600.0f, 1000.0f, 85.0f, 1800.0f)
         .env (1, 2000.0f, 1000.0f, 100.0f, 1500.0f)
         .mod (MS::Env2, warpA(), 60.0f)
         .mod (MS::Env2, cutoff1(), 15.0f)
         .living (25.0f, 35.0f)
         .macro (0, "Squeeze", 0.0f).macro (1, "Brightness", 0.0f)
         .mod (MS::Macro1, warpA(), 35.0f)
         .mod (MS::Macro2, cutoff1(), 25.0f)
         .reverb (65.0f, 60.0f, 25.0f)
         .master (-4.5f);
        add (r);
    }
    {   // Remap nebula: a remapped choir whose curve and pan wander on two free LFOs.
        Recipe r ("Remap Nebula", "Pad", { "evolving", "wide", "digital" });
        r.osc (0, Tbl::Choir, 70.0f, 70.0f).warp (0, Warp::Remap, -30.0f).unison (0, 8, 12.0f, 100.0f)
         .osc (1, Tbl::NoiseBloom, 30.0f, 30.0f).oscPitch (1, 1)
         .filter (0, Flt::LP24, 3500.0f, 15.0f)
         .env (0, 900.0f, 1000.0f, 85.0f, 2200.0f)
         .lfo (0, Wave::Sine, 0.09f, Mode::Free)
         .lfo (1, Wave::Triangle, 0.23f, Mode::Free)
         .mod (MS::Lfo1, warpA(), 35.0f)
         .mod (MS::Lfo2, panB(), 50.0f)
         .living (25.0f, 55.0f)
         .macro (0, "Remap", 0.0f).macro (1, "Space", 0.0f)
         .mod (MS::Macro1, warpA(), 30.0f)
         .mod (MS::Macro2, P::ReverbMix, 30.0f)
         .reverb (80.0f, 70.0f, 30.0f)
         .master (0.0f);
        add (r);
    }
    {   // Bending light: a bent analog saw pad; a slow LFO leans the bend back and forth.
        Recipe r ("Bending Light", "Pad", { "smooth", "evolving", "warm" });
        r.osc (0, Tbl::AnalogSaws, 15.0f, 70.0f).warp (0, Warp::Bend, 0.0f).unison (0, 5, 12.0f, 80.0f)
         .filter (0, Flt::LP12, 2800.0f, 10.0f)
         .env (0, 700.0f, 1000.0f, 85.0f, 1800.0f)
         .lfo (0, Wave::Sine, 0.11f, Mode::Free)
         .mod (MS::Lfo1, warpA(), 35.0f)
         .living (25.0f, 40.0f)
         .macro (0, "Bend", 0.0f).macro (1, "Haze", 0.0f)
         .mod (MS::Macro1, warpA(), 30.0f)
         .mod (MS::Macro2, P::FlangerMix, 35.0f)
         .flanger (0.07f, 40.0f, 25.0f, 20.0f)
         .reverb (65.0f, 60.0f, 28.0f)
         .master (-6.0f);
        add (r);
    }
    {   // Split horizon: two filters in parallel, a low-pass body and a band-pass that sweeps above it.
        Recipe r ("Split Horizon", "Pad", { "wide", "evolving", "cinematic" });
        r.osc (0, Tbl::HarmonicStack, 40.0f, 70.0f).unison (0, 4, 12.0f, 90.0f)
         .osc (1, Tbl::AnalogSaws, 30.0f, 55.0f).unison (1, 4, 15.0f, 90.0f).route (1, Route::Filter2)
         .filter (0, Flt::LP24, 1200.0f, 15.0f)
         .filter (1, Flt::BP24, 2500.0f, 35.0f)
         .parallel()
         .env (0, 800.0f, 1000.0f, 85.0f, 2000.0f)
         .lfo (0, Wave::Triangle, 0.07f, Mode::Free)
         .mod (MS::Lfo1, cutoff2(), 30.0f)
         .living (25.0f, 45.0f)
         .macro (0, "Horizon", 0.0f).macro (1, "Body", 0.0f)
         .mod (MS::Macro1, cutoff2(), 25.0f)
         .mod (MS::Macro2, cutoff1(), 25.0f)
         .reverb (75.0f, 65.0f, 30.0f)
         .master (0.5f);
        add (r);
    }
}

} // namespace verdant::factory
