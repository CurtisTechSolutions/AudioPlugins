// Factory bank: Drums/Perc (one-shot hits built from the engine: pitch envelopes on sine and bell
// tables, coloured noise, short envelopes). Tuned to sound right on C3; play other keys to retune.

#include "FactoryPresets_Recipes.h"

namespace verdant::factory
{

void addDrumPresets (std::vector<Patch>& bank)
{
    auto add = [&bank] (Recipe& r) { bank.push_back (r.p); };
    constexpr const char* cat = "Drums/Perc";

    {   // Deep kick: a sine an octave down with a fast Env 2 pitch drop and a brown-noise click.
        Recipe r ("Deep Kick", cat, { "percussive", "deep", "clean" });
        r.osc (0, Tbl::BasicShapes, 0.0f, 85.0f).oscPitch (0, -1).hardPhase (0, 90.0f)
         .noise (10.0f, Noise::Brown, Route::Direct)
         .filterOff (0)
         .env (0, 0.0f, 450.0f, 0.0f, 200.0f)
         .env (1, 0.0f, 70.0f, 0.0f, 50.0f, 0.0f, -60.0f)
         .env (2, 0.0f, 15.0f, 0.0f, 10.0f)
         .pitchEnv (MS::Env2, 30.0f, false)
         .mod (MS::Env3, P::NoiseLevel, 30.0f)
         .mono (VMode::Mono, 0.0f)
         .living (5.0f, 0.0f)
         .macro (0, "Punch", 0.0f).macro (1, "Length", 0.0f)
         .mod (MS::Macro1, envDecay (1), 30.0f)
         .mod (MS::Macro2, envDecay (0), 30.0f)
         .master (-3.5f);
        add (r);
    }
    {   // Punch kick: shorter, a harder pitch snap, clipped and compressed to hit through a mix.
        Recipe r ("Punch Kick", cat, { "percussive", "punchy", "aggressive" });
        r.osc (0, Tbl::BasicShapes, 4.0f, 85.0f).oscPitch (0, -1, 2).hardPhase (0, 90.0f)
         .noise (15.0f, Noise::White, Route::Filter1)
         .filter (0, Flt::LP24, 5000.0f, 5.0f)
         .env (0, 0.0f, 260.0f, 0.0f, 120.0f)
         .env (1, 0.0f, 40.0f, 0.0f, 30.0f, 0.0f, -70.0f)
         .env (2, 0.0f, 8.0f, 0.0f, 8.0f)
         .pitchEnv (MS::Env2, 40.0f, false)
         .mod (MS::Env3, P::NoiseLevel, 30.0f)
         .mono (VMode::Mono, 0.0f)
         .living (5.0f, 0.0f)
         .macro (0, "Click", 0.0f).macro (1, "Drive", 0.0f)
         .mod (MS::Macro1, P::NoiseLevel, 20.0f)
         .mod (MS::Macro2, P::DistDrive, 40.0f)
         .distortion (Dist::Hard, 30.0f, 6000.0f, 60.0f)
         .compressor (-18.0f, 6.0f, 3.0f, 1.0f, 80.0f)
         .master (-0.5f);
        add (r);
    }
    {   // Boom kick: the long, tuned sub-boom: a slow pitch glide down and a triangle sub, tube warmth.
        Recipe r ("Boom Kick", cat, { "sub", "deep", "percussive", "retro" });
        r.osc (0, Tbl::BasicShapes, 0.0f, 85.0f).oscPitch (0, -1).hardPhase (0, 90.0f)
         .sub (30.0f, Sub::Triangle, 1)
         .filterOff (0)
         .env (0, 0.0f, 900.0f, 0.0f, 300.0f, 0.0f, -20.0f)
         .env (1, 0.0f, 120.0f, 0.0f, 80.0f, 0.0f, -50.0f)
         .pitchEnv (MS::Env2, 20.0f, false)
         .mono (VMode::Mono, 0.0f)
         .living (5.0f, 0.0f)
         .macro (0, "Boom", 0.0f).macro (1, "Warmth", 0.0f)
         .mod (MS::Macro1, envDecay (0), 30.0f)
         .mod (MS::Macro2, P::DistDrive, 40.0f)
         .distortion (Dist::Tube, 20.0f, 3000.0f, 50.0f)
         .master (-7.0f);
        add (r);
    }
    {   // Snare crack: a pitched triangle body that dies fast under a band of white noise.
        Recipe r ("Snare Crack", cat, { "percussive", "bright", "noisy" });
        r.osc (0, Tbl::BasicShapes, 6.0f, 30.0f).oscPitch (0, 0, 7).hardPhase (0)
         .noise (60.0f, Noise::White, Route::Filter1)
         .filter (0, Flt::HP12, 400.0f, 10.0f)
         .filter (1, Flt::LP12, 9000.0f, 5.0f)
         .env (0, 0.0f, 220.0f, 0.0f, 120.0f, 0.0f, -60.0f)
         .env (1, 0.0f, 60.0f, 0.0f, 40.0f)
         .mod (MS::Env2, levelA(), 60.0f)
         .pitchEnv (MS::Env2, 6.0f, false)
         .living (8.0f, 0.0f)
         .macro (0, "Snappy", 0.0f).macro (1, "Body", 0.0f).macro (2, "Room", 0.0f)
         .mod (MS::Macro1, P::NoiseLevel, 25.0f)
         .mod (MS::Macro2, levelA(), 30.0f)
         .mod (MS::Macro3, P::ReverbMix, 30.0f)
         .eq (3.0f, 3.0f, 2000.0f, 3.0f)
         .reverb (30.0f, 20.0f, 8.0f, 50.0f)
         .master (9.5f);
        add (r);
    }
    {   // Clap room: band-passed noise with a drawn one-shot LFO for the flams, and a small room.
        Recipe r ("Clap Room", cat, { "percussive", "noisy", "classic" });
        r.oscOff (0)
         .noise (35.0f, Noise::Pink, Route::Filter1)
         .filter (0, Flt::BP12, 1300.0f, 20.0f)
         .filterMix (0, 85.0f)
         .env (0, 0.0f, 260.0f, 0.0f, 150.0f, 0.0f, -60.0f)
         .lfo (0, Wave::Custom, 4.0f, Mode::OneShot, true)
         .customShape (0, { { 0.0f, 1.0f, 0.0f }, { 0.04f, 0.15f, 0.0f }, { 0.05f, 0.8f, 0.0f }, { 0.09f, 0.15f, 0.0f },
                            { 0.1f, 0.7f, 0.0f }, { 0.4f, 0.4f, 0.0f }, { 1.0f, 0.25f, 0.0f } })
         .mod (MS::Lfo1, P::NoiseLevel, 65.0f)
         .living (8.0f, 0.0f)
         .macro (0, "Tone", 0.0f).macro (1, "Room", 0.0f)
         .mod (MS::Macro1, cutoff1(), 25.0f)
         .mod (MS::Macro2, P::ReverbMix, 30.0f)
         .eq (0.0f, 6.0f, 1300.0f, 0.0f)
         .reverb (35.0f, 25.0f, 15.0f, 40.0f)
         .master (12.0f);
        add (r);
    }
    {   // Closed hat: high-passed white noise with a metallic bell partial, 50 ms.
        Recipe r ("Closed Hat", cat, { "percussive", "metallic", "short" });
        r.osc (0, Tbl::Bell, 95.0f, 30.0f).oscPitch (0, 3).hardPhase (0)
         .noise (100.0f, Noise::White, Route::Filter1)
         .filter (0, Flt::HP24, 7500.0f, 15.0f)
         .env (0, 0.0f, 55.0f, 0.0f, 40.0f, 0.0f, -60.0f)
         .living (10.0f, 0.0f)
         .macro (0, "Tightness", 0.0f).macro (1, "Tone", 0.0f)
         .mod (MS::Macro1, envDecay (0), -20.0f)
         .mod (MS::Macro2, cutoff1(), 20.0f)
         .master (10.5f);
        add (r);
    }
    {   // Open hat: the same metal opened up, violet noise, a longer sizzle.
        Recipe r ("Open Hat", cat, { "percussive", "metallic", "bright" });
        r.osc (0, Tbl::Bell, 90.0f, 25.0f).oscPitch (0, 3, 5).hardPhase (0)
         .noise (90.0f, Noise::Violet, Route::Filter1)
         .filter (0, Flt::HP12, 6000.0f, 20.0f)
         .env (0, 0.0f, 420.0f, 0.0f, 200.0f, 0.0f, -50.0f)
         .living (10.0f, 0.0f)
         .macro (0, "Open", 0.0f).macro (1, "Sizzle", 0.0f)
         .mod (MS::Macro1, envDecay (0), 25.0f)
         .mod (MS::Macro2, res1(), 30.0f)
         .master (1.0f);
        add (r);
    }
    {   // Low tom: a tuned sine tom (a touch of sub FM for the skin), a gentle pitch drop and a little noise.
        Recipe r ("Low Tom", cat, { "percussive", "warm", "organic" });
        r.osc (0, Tbl::BasicShapes, 3.0f, 85.0f).oscPitch (0, -1, 7).hardPhase (0, 90.0f).warp (0, Warp::FmSub, 8.0f)
         .noise (8.0f, Noise::Pink, Route::Direct)
         .filterOff (0)
         .env (0, 0.0f, 500.0f, 0.0f, 250.0f)
         .env (1, 0.0f, 150.0f, 0.0f, 100.0f)
         .pitchEnv (MS::Env2, 8.0f, false)
         .mod (MS::Env2, P::NoiseLevel, 15.0f)
         .living (8.0f, 0.0f)
         .macro (0, "Pitch Drop", 0.0f).macro (1, "Room", 0.0f)
         .mod (MS::Macro1, pitchA(), 6.0f)
         .mod (MS::Macro2, P::ReverbMix, 30.0f)
         .reverb (35.0f, 25.0f, 10.0f, 50.0f)
         .master (-3.5f);
        add (r);
    }
    {   // Rim click: a short band-passed noise tick with a woody high sine through a comb.
        Recipe r ("Rim Click", cat, { "percussive", "short", "organic" });
        r.osc (0, Tbl::BasicShapes, 0.0f, 60.0f).oscPitch (0, 2, 2).hardPhase (0).route (0, Route::Filter2)
         .noise (100.0f, Noise::White, Route::Filter1)
         .filter (0, Flt::BP12, 3800.0f, 12.0f)
         .filter (1, Flt::CombPlus, 2600.0f, 30.0f)
         .filterMix (1, 35.0f)
         .env (0, 0.0f, 120.0f, 0.0f, 60.0f, 0.0f, -40.0f)
         .living (8.0f, 0.0f)
         .macro (0, "Wood", 0.0f).macro (1, "Tone", 0.0f)
         .mod (MS::Macro1, res2(), 35.0f)
         .mod (MS::Macro2, cutoff1(), 25.0f)
         .eq (0.0f, 6.0f, 2500.0f, 0.0f)
         .master (4.0f);
        add (r);
    }
    {   // Cowbell pair: two pulses a fifth-ish apart through a band-pass; the classic machine bell.
        Recipe r ("Cowbell Pair", cat, { "percussive", "retro", "metallic" });
        r.osc (0, Tbl::PulseWidth, 0.0f, 100.0f).oscPitch (0, 1, 4).hardPhase (0)
         .osc (1, Tbl::PulseWidth, 0.0f, 85.0f).oscPitch (1, 2, 0, 20.0f).hardPhase (1)
         .filter (0, Flt::BP12, 1000.0f, 20.0f, 20.0f)
         .env (0, 0.0f, 300.0f, 0.0f, 150.0f, 0.0f, -70.0f)
         .living (5.0f, 0.0f)
         .macro (0, "Tone", 0.0f).macro (1, "Decay", 0.0f)
         .mod (MS::Macro1, cutoff1(), 25.0f)
         .mod (MS::Macro2, envDecay (0), 30.0f)
         .master (10.0f);
        add (r);
    }
    {   // Shaker grain: blue noise swept by a fast one-shot saw, an off-beat shaker hit.
        Recipe r ("Shaker Grain", cat, { "percussive", "noisy", "airy" });
        r.oscOff (0)
         .noise (100.0f, Noise::Blue, Route::Filter1)
         .filter (0, Flt::BP12, 5000.0f, 10.0f)
         .env (0, 2.0f, 140.0f, 0.0f, 80.0f, 40.0f, -30.0f)
         .lfo (0, Wave::SawDown, 6.0f, Mode::OneShot, true)
         .mod (MS::Lfo1, cutoff1(), 12.0f)
         .living (12.0f, 0.0f)
         .macro (0, "Grain", 0.0f).macro (1, "Length", 0.0f)
         .mod (MS::Macro1, cutoff1(), 25.0f)
         .mod (MS::Macro2, envDecay (0), 30.0f)
         .master (8.5f);
        add (r);
    }
    {   // Conga slap: a formant-filtered sine skin with a quick upward slap from the Spring.
        Recipe r ("Conga Slap", cat, { "percussive", "organic", "physical" });
        r.osc (0, Tbl::BasicShapes, 8.0f, 85.0f).hardPhase (0, 90.0f)
         .noise (10.0f, Noise::Pink)
         .filter (0, Flt::Formant, 900.0f, 30.0f)
         .filterMix (0, 50.0f)
         .env (0, 0.0f, 280.0f, 0.0f, 150.0f, 0.0f, -50.0f)
         .spring (85.0f, 45.0f)
         .mod (MS::Spring, pitchA(), 1.5f)
         .env (1, 0.0f, 30.0f, 0.0f, 20.0f)
         .mod (MS::Env2, P::NoiseLevel, 30.0f)
         .living (10.0f, 0.0f)
         .macro (0, "Slap", 0.0f).macro (1, "Skin", 0.0f)
         .mod (MS::Macro1, P::NoiseLevel, 20.0f)
         .mod (MS::Macro2, cutoff1(), 25.0f)
         .master (6.0f);
        add (r);
    }
    {   // Anvil hit: an FM-struck bell into a resonant comb; a ringing, clangorous strike.
        Recipe r ("Anvil Hit", cat, { "metallic", "percussive", "aggressive" });
        r.osc (0, Tbl::Bell, 60.0f, 80.0f).warp (0, Warp::FmOsc, 40.0f).hardPhase (0)
         .osc (1, Tbl::BasicShapes, 0.0f, 30.0f).oscPitch (1, 1, 5, 30.0f)
         .filter (0, Flt::CombPlus, 1500.0f, 45.0f)
         .filter (1, Flt::HP12, 300.0f, 5.0f)
         .env (0, 0.0f, 600.0f, 0.0f, 400.0f, 0.0f, -60.0f)
         .env (1, 0.0f, 80.0f, 0.0f, 50.0f)
         .mod (MS::Env2, warpA(), 40.0f)
         .living (5.0f, 0.0f)
         .macro (0, "Strike", 0.0f).macro (1, "Ring", 0.0f)
         .mod (MS::Macro1, warpA(), 30.0f)
         .mod (MS::Macro2, res1(), 30.0f)
         .reverb (30.0f, 30.0f, 10.0f)
         .master (3.5f);
        add (r);
    }
    {   // Bit perc: a crushed FM blip; the Random source picks a new table position on every hit.
        Recipe r ("Bit Perc", cat, { "percussive", "digital", "glitchy", "lofi" });
        r.osc (0, Tbl::FmSweep, 70.0f, 80.0f).hardPhase (0)
         .filter (0, Flt::LP12, 7000.0f, 10.0f)
         .env (0, 0.0f, 120.0f, 0.0f, 60.0f, 0.0f, -60.0f)
         .env (1, 0.0f, 50.0f, 0.0f, 40.0f)
         .pitchEnv (MS::Env2, 15.0f, false)
         .mod (MS::Random, posA(), 30.0f)
         .living (10.0f, 0.0f)
         .macro (0, "Bits", 0.0f).macro (1, "Pitch", 0.0f)
         .mod (MS::Macro1, P::DistDrive, 40.0f)
         .mod (MS::Macro2, pitchA(), 8.0f)
         .distortion (Dist::Crush, 45.0f, 8000.0f, 80.0f)
         .master (4.5f);
        add (r);
    }
}

} // namespace verdant::factory
