// Factory bank: the two openers (host programs 0 and 1) and Keys.

#include "FactoryPresets_Recipes.h"

namespace verdant::factory
{

void addOpeningPresets (std::vector<Patch>& bank)
{
    auto add = [&bank] (Recipe& r) { bank.push_back (r.p); };

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
         .macro (0, "Brightness", 0.0f).macro (1, "Bell", 0.0f).macro (2, "Space", 0.0f)
         .mod (MS::Macro1, cutoff1(), 30.0f)
         .mod (MS::Macro2, levelB(), 40.0f)
         .mod (MS::Macro3, P::ReverbMix, 35.0f)
         .chorus (0.6f, 30.0f, 30.0f)
         .reverb (35.0f, 35.0f, 18.0f)
         .master (2.5f);
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
         .macro (0, "Resonance", 0.0f).macro (1, "Brightness", 0.0f)
         .mod (MS::Macro1, P::SympAmount, 35.0f)
         .mod (MS::Macro2, cutoff1(), 25.0f)
         .sympathetic (35.0f, 4, 8, 60.0f, 65.0f)
         .reverb (55.0f, 45.0f, 22.0f)
         .master (3.0f);
        add (r);
    }
}

void addKeysPresets (std::vector<Patch>& bank)
{
    auto add = [&bank] (Recipe& r) { bank.push_back (r.p); };

    {   // Drawbar organ: organ table at two footages, LFO vibrato, phaser and chorus as a rotary stand-in.
        Recipe r ("Drawbar Organ", "Keys", { "vintage", "classic", "warm" });
        r.osc (0, Tbl::Organ, 35.0f, 70.0f)
         .osc (1, Tbl::Organ, 70.0f, 40.0f).oscPitch (1, 1)
         .filter (0, Flt::LP12, 9000.0f, 5.0f)
         .env (0, 4.0f, 50.0f, 100.0f, 60.0f)
         .lfo (0, Wave::Sine, 6.2f).lfoFade (0, 300.0f)
         .mod (MS::Lfo1, pitchA(), 1.0f)
         .macro (0, "Drawbars", 0.0f).macro (1, "Rotor Speed", 0.0f)
         .mod (MS::Macro1, posA(), 35.0f)
         .mod (MS::Macro2, P::PhaserRate, 35.0f)
         .mod (MS::Macro2, P::ChorusRate, 30.0f)
         .phaser (0.8f, 40.0f, 20.0f, 35.0f)
         .chorus (1.2f, 25.0f, 30.0f)
         .master (2.5f);
        add (r);
    }
    {   // Mellow tine keys with a Tone macro on the filter.
        Recipe r ("Tine Dream", "Keys", { "soft", "warm", "expressive" });
        r.osc (0, Tbl::BasicShapes, 8.0f, 80.0f)
         .osc (1, Tbl::Bell, 25.0f, 35.0f).oscPitch (1, 2)
         .filter (0, Flt::LP24, 1800.0f, 15.0f, 0.0f, 40.0f)
         .env (0, 2.0f, 1600.0f, 35.0f, 500.0f)
         .macro (0, "Tone", 40.0f).macro (1, "Chorus", 0.0f)
         .mod (MS::Macro1, cutoff1(), 45.0f)
         .mod (MS::Macro2, P::ChorusMix, 40.0f)
         .mod (MS::Velocity, cutoff1(), 20.0f)
         .chorus (0.4f, 35.0f, 35.0f)
         .reverb (40.0f, 40.0f, 20.0f)
         .master (1.0f);
        add (r);
    }
    {   // Felted upright: dark, soft attack, a gentle sympathetic body, EQ to tame the lows.
        Recipe r ("Felt Upright", "Keys", { "soft", "organic", "dark" });
        r.osc (0, Tbl::HarmonicStack, 4.0f, 80.0f)
         .filter (0, Flt::LP24, 1500.0f, 8.0f, 0.0f, 50.0f)
         .env (0, 8.0f, 1500.0f, 15.0f, 600.0f)
         .macro (0, "Felt", 0.0f).macro (1, "Body", 0.0f)
         .mod (MS::Macro1, cutoff1(), -25.0f)
         .mod (MS::Macro2, P::SympAmount, 30.0f)
         .sympathetic (25.0f, 6, 6, 45.0f, 35.0f)
         .eq (-3.0f, 2.0f, 900.0f, -4.0f)
         .reverb (30.0f, 30.0f, 15.0f)
         .master (-3.5f);
        add (r);
    }
    {   // Funky clav: narrow pulse into a resonant band-pass, Env 2 snap, compressed.
        Recipe r ("Pulse Clav", "Keys", { "percussive", "bright", "vintage" });
        r.osc (0, Tbl::PulseWidth, 45.0f, 80.0f)
         .filter (0, Flt::BP12, 1000.0f, 12.0f, 15.0f, 70.0f)
         .env (0, 0.5f, 400.0f, 30.0f, 120.0f)
         .env (1, 0.0f, 180.0f, 0.0f, 100.0f)
         .mod (MS::Env2, cutoff1(), 35.0f)
         .macro (0, "Wah", 0.0f).macro (1, "Pulse", 0.0f)
         .mod (MS::Macro1, cutoff1(), 30.0f)
         .mod (MS::Macro2, posA(), 30.0f)
         .compressor (-20.0f, 4.0f, 4.0f)
         .master (6.0f);
        add (r);
    }
    {   // Wurly bark: odd harmonics that growl harder with velocity, amplitude tremolo, tube amp.
        Recipe r ("Wurly Bark", "Keys", { "vintage", "dirty", "warm" });
        r.osc (0, Tbl::OddHarmonics, 10.0f, 80.0f)
         .osc (1, Tbl::BasicShapes, 0.0f, 40.0f)
         .filter (0, Flt::LP24, 1500.0f, 10.0f, 20.0f, 50.0f)
         .env (0, 2.0f, 1200.0f, 45.0f, 300.0f)
         .env (1, 0.0f, 400.0f, 0.0f, 300.0f)
         .mod (MS::Env2, posA(), 20.0f)
         .mod (MS::Velocity, drive1(), 40.0f)
         .lfo (0, Wave::Sine, 5.0f, Mode::Free)
         .macro (0, "Tremolo", 30.0f).macro (1, "Bark", 0.0f)
         .mod (MS::Lfo1, levelA(), 10.0f)
         .mod (MS::Lfo1, levelB(), 10.0f)
         .mod (MS::Macro1, lfoRate (0), 15.0f)
         .mod (MS::Macro2, P::DistDrive, 45.0f)
         .distortion (Dist::Tube, 25.0f, 5000.0f, 60.0f)
         .reverb (30.0f, 30.0f, 12.0f)
         .master (-1.0f);
        add (r);
    }
    {   // Motor vibes: bell bars with a motor tremolo and a sympathetic halo, panned by a slow orbit.
        Recipe r ("Motor Vibes", "Keys", { "metallic", "warm", "physical" });
        r.osc (0, Tbl::Bell, 5.0f, 80.0f).hardPhase (0)
         .filter (0, Flt::LP12, 6000.0f, 5.0f)
         .env (0, 1.0f, 2500.0f, 10.0f, 900.0f)
         .lfo (0, Wave::Triangle, 4.5f, Mode::Free)
         .mod (MS::Lfo1, levelA(), 12.0f)
         .orbit (20.0f, 40.0f)
         .mod (MS::Orbit, panA(), 20.0f)
         .macro (0, "Motor", 0.0f).macro (1, "Halo", 0.0f)
         .mod (MS::Macro1, lfoRate (0), 20.0f)
         .mod (MS::Macro2, P::SympAmount, 35.0f)
         .sympathetic (20.0f, 4, 8, 55.0f, 50.0f)
         .reverb (45.0f, 45.0f, 18.0f)
         .master (0.5f);
        add (r);
    }
    {   // FM ballad keys: Osc B modulates Osc A two octaves up for the classic struck-tine bite.
        Recipe r ("FM Ballad Keys", "Keys", { "fm", "classic", "glassy" });
        r.osc (0, Tbl::BasicShapes, 0.0f, 80.0f).warp (0, Warp::FmOsc, 20.0f)
         .osc (1, Tbl::BasicShapes, 0.0f, 20.0f).oscPitch (1, 2)
         .filter (0, Flt::LP12, 8000.0f, 5.0f)
         .env (0, 1.0f, 2000.0f, 25.0f, 600.0f)
         .env (1, 0.0f, 450.0f, 0.0f, 300.0f)
         .mod (MS::Env2, warpA(), 45.0f)
         .mod (MS::Velocity, warpA(), 20.0f)
         .macro (0, "Tine", 0.0f).macro (1, "Chorus", 0.0f)
         .mod (MS::Macro1, warpA(), 30.0f)
         .mod (MS::Macro2, P::ChorusMix, 40.0f)
         .chorus (0.5f, 30.0f, 30.0f)
         .reverb (40.0f, 40.0f, 15.0f)
         .master (0.0f);
        add (r);
    }
    {   // Toy box piano: glass partials with a music-box decay, slightly out of tune with lots of alive.
        Recipe r ("Toy Box Piano", "Keys", { "glassy", "bright", "lofi" });
        r.osc (0, Tbl::DigitalGlass, 45.0f, 75.0f).hardPhase (0)
         .osc (1, Tbl::Bell, 60.0f, 35.0f).oscPitch (1, 1, 0, 14.0f)
         .filter (0, Flt::HP12, 300.0f, 10.0f)
         .env (0, 0.5f, 1600.0f, 0.0f, 500.0f)
         .living (35.0f, 0.0f)
         .macro (0, "Tine", 0.0f).macro (1, "Detune", 0.0f)
         .mod (MS::Macro1, levelB(), 40.0f)
         .mod (MS::Macro2, fineB(), 20.0f)
         .reverb (35.0f, 30.0f, 15.0f)
         .master (4.5f);
        add (r);
    }
    {   // Lattice harpsichord: a narrow pulse and its octave, high-passed, plucked quick and bright.
        Recipe r ("Lattice Harpsichord", "Keys", { "bright", "classic", "plucky" });
        r.osc (0, Tbl::PulseWidth, 80.0f, 75.0f)
         .osc (1, Tbl::PulseWidth, 65.0f, 45.0f).oscPitch (1, 1)
         .filter (0, Flt::HP12, 220.0f, 5.0f)
         .filter (1, Flt::LP12, 7000.0f, 10.0f)
         .env (0, 0.5f, 1100.0f, 0.0f, 350.0f)
         .env (1, 0.0f, 300.0f, 0.0f, 200.0f)
         .mod (MS::Env2, cutoff2(), 20.0f)
         .macro (0, "Buff Stop", 0.0f).macro (1, "Coupler", 40.0f)
         .mod (MS::Macro1, cutoff2(), -35.0f)
         .mod (MS::Macro2, levelB(), 35.0f)
         .reverb (35.0f, 35.0f, 15.0f)
         .master (7.5f);
        add (r);
    }
    {   // Chapel organ: a bright organ registration with a choir layer, a soft speak and a big room.
        Recipe r ("Chapel Organ", "Keys", { "airy", "vocal", "cinematic" });
        r.osc (0, Tbl::Organ, 95.0f, 70.0f)
         .osc (1, Tbl::Choir, 25.0f, 40.0f)
         .filter (0, Flt::LP12, 9000.0f, 5.0f)
         .env (0, 30.0f, 100.0f, 100.0f, 700.0f)
         .lfo (0, Wave::Sine, 5.5f).lfoFade (0, 600.0f)
         .mod (MS::Lfo1, fineB(), 8.0f)
         .macro (0, "Stops", 0.0f).macro (1, "Choir", 0.0f).macro (2, "Space", 0.0f)
         .mod (MS::Macro1, posA(), 35.0f)
         .mod (MS::Macro2, levelB(), 35.0f)
         .mod (MS::Macro3, P::ReverbMix, 35.0f)
         .reverb (80.0f, 65.0f, 28.0f, 30.0f, 40.0f)
         .master (-2.5f);
        add (r);
    }
    {   // Lo-fi tape keys: a wobbly (wow + flutter) electric piano, bit-crushed and band-limited.
        Recipe r ("Lofi Tape Keys", "Keys", { "lofi", "dusty", "warm", "vintage" });
        r.osc (0, Tbl::HarmonicStack, 8.0f, 80.0f)
         .osc (1, Tbl::Bell, 15.0f, 25.0f).oscPitch (1, 1)
         .filter (0, Flt::LP12, 2200.0f, 10.0f)
         .env (0, 3.0f, 1500.0f, 35.0f, 500.0f)
         .lfo (0, Wave::Sine, 0.7f, Mode::Free)
         .lfo (1, Wave::SampleHold, 9.0f, Mode::Free).lfoSmooth (1, 70.0f)
         .mod (MS::Lfo1, fineA(), 10.0f)
         .mod (MS::Lfo1, fineB(), 10.0f)
         .mod (MS::Lfo2, fineA(), 4.0f)
         .macro (0, "Wow", 30.0f).macro (1, "Crush", 0.0f).macro (2, "Age", 0.0f)
         .mod (MS::Macro1, lfoRate (0), 20.0f)
         .mod (MS::Macro2, P::DistMix, 40.0f)
         .mod (MS::Macro3, P::EqHighGain, -30.0f)
         .distortion (Dist::Crush, 20.0f, 5000.0f, 35.0f)
         .eq (2.0f, 2.0f, 700.0f, -8.0f)
         .master (-2.0f);
        add (r);
    }
    {   // Mirror keys: a mirrored bell for an almost-sine keyboard with a glassy fold on top.
        Recipe r ("Mirror Keys", "Keys", { "glassy", "modern", "soft" });
        r.osc (0, Tbl::Bell, 20.0f, 80.0f).warp (0, Warp::Mirror, 25.0f)
         .filter (0, Flt::LP24, 4000.0f, 10.0f, 0.0f, 50.0f)
         .env (0, 2.0f, 1600.0f, 35.0f, 500.0f)
         .env (1, 0.0f, 900.0f, 0.0f, 400.0f)
         .mod (MS::Env2, warpA(), 30.0f)
         .macro (0, "Fold", 0.0f).macro (1, "Space", 0.0f)
         .mod (MS::Macro1, warpA(), 40.0f)
         .mod (MS::Macro2, P::DelayMix, 30.0f)
         .delay (ND::QuarterDotted, 30.0f, 12.0f)
         .reverb (45.0f, 45.0f, 18.0f)
         .master (2.5f);
        add (r);
    }
    {   // Sync keys: velocity drives the hard-sync amount; soft playing stays round, hard playing barks.
        Recipe r ("Sync Keys", "Keys", { "digital", "bright", "expressive" });
        r.osc (0, Tbl::SyncSweep, 0.0f, 80.0f).warp (0, Warp::Sync, 0.0f)
         .filter (0, Flt::LP24, 3500.0f, 15.0f, 0.0f, 50.0f)
         .env (0, 1.0f, 1200.0f, 40.0f, 400.0f)
         .env (1, 0.0f, 600.0f, 0.0f, 300.0f)
         .mod (MS::Velocity, warpA(), 35.0f)
         .mod (MS::Env2, posA(), 30.0f)
         .macro (0, "Sync", 0.0f).macro (1, "Brightness", 0.0f)
         .mod (MS::Macro1, warpA(), 35.0f)
         .mod (MS::Macro2, cutoff1(), 30.0f)
         .phaser (0.3f, 40.0f, 25.0f, 25.0f)
         .master (3.0f);
        add (r);
    }
    {   // Celesta dust: high bell partials, a notch carving the middle, shimmering delay.
        Recipe r ("Celesta Dust", "Keys", { "glassy", "airy", "bright" });
        r.osc (0, Tbl::Bell, 35.0f, 75.0f).oscPitch (0, 1).hardPhase (0)
         .osc (1, Tbl::BasicShapes, 0.0f, 35.0f).oscPitch (1, 1)
         .filter (0, Flt::Notch, 2500.0f, 30.0f)
         .env (0, 0.5f, 1300.0f, 0.0f, 600.0f)
         .macro (0, "Sparkle", 0.0f).macro (1, "Echo", 0.0f)
         .mod (MS::Macro1, posA(), 40.0f)
         .mod (MS::Macro2, P::DelayMix, 30.0f)
         .delay (ND::EighthDotted, 35.0f, 15.0f)
         .reverb (55.0f, 50.0f, 20.0f)
         .master (3.0f);
        add (r);
    }
    {   // Harmonium air: reedy odd harmonics through a formant filter, breath noise, a soft bellows swell.
        Recipe r ("Harmonium Air", "Keys", { "vocal", "airy", "organic" });
        r.osc (0, Tbl::OddHarmonics, 40.0f, 75.0f)
         .osc (1, Tbl::OddHarmonics, 55.0f, 40.0f).oscPitch (1, 0, 0, 8.0f)
         .noise (5.0f, Noise::Pink)
         .filter (0, Flt::Formant, 2500.0f, 20.0f)
         .filterMix (0, 70.0f)
         .env (0, 12.0f, 200.0f, 100.0f, 250.0f)
         .lfo (0, Wave::Custom, 0.4f).lfoPhase (0, 45.0f)
         .customShape (0, { { 0.0f, 0.3f, 0.0f }, { 0.45f, 1.0f, 0.4f }, { 0.6f, 0.8f, 0.0f }, { 1.0f, 0.3f, -0.4f } })
         .mod (MS::Lfo1, levelA(), 5.0f)
         .macro (0, "Bellows", 0.0f).macro (1, "Reed", 0.0f)
         .mod (MS::Macro1, cutoff1(), 25.0f)
         .mod (MS::Macro2, posA(), 35.0f)
         .reverb (35.0f, 35.0f, 15.0f)
         .master (0.5f);
        add (r);
    }
    {   // Stage piano: harmonic stack plus a plucked attack, velocity-opened low-pass, compressed and EQ'd.
        Recipe r ("Stage Piano", "Keys", { "bright", "clean", "punchy" });
        r.osc (0, Tbl::HarmonicStack, 25.0f, 75.0f).hardPhase (0)
         .osc (1, Tbl::PluckDecay, 5.0f, 40.0f).hardPhase (1)
         .filter (0, Flt::LP24, 3500.0f, 8.0f, 0.0f, 60.0f)
         .env (0, 0.5f, 2200.0f, 20.0f, 500.0f)
         .env (1, 0.0f, 1200.0f, 0.0f, 500.0f)
         .mod (MS::Env2, posB(), 50.0f)
         .mod (MS::Velocity, cutoff1(), 25.0f)
         .macro (0, "Hammer", 0.0f).macro (1, "Brightness", 0.0f)
         .mod (MS::Macro1, levelB(), 35.0f)
         .mod (MS::Macro2, cutoff1(), 30.0f)
         .compressor (-20.0f, 3.0f, 3.0f, 5.0f)
         .eq (-2.0f, -2.0f, 400.0f, 3.0f)
         .reverb (35.0f, 35.0f, 14.0f)
         .master (0.0f);
        add (r);
    }
}

} // namespace verdant::factory
