// Factory bank: FX (risers, impacts, sirens, glitches: sound design on one key).

#include "FactoryPresets_Recipes.h"

namespace verdant::factory
{

void addFxPresets (std::vector<Patch>& bank)
{
    auto add = [&bank] (Recipe& r) { bank.push_back (r.p); };

    {   // Riser: a one-shot LFO opens the filter over the note, noise and big space.
        Recipe r ("Rising Tide", "FX", { "cinematic", "evolving", "noisy" });
        r.osc (0, Tbl::NoiseBloom, 60.0f, 70.0f)
         .noise (35.0f, Noise::White)
         .filter (0, Flt::LP24, 300.0f, 35.0f)
         .env (0, 600.0f, 2000.0f, 90.0f, 2500.0f)
         .lfo (0, Wave::SawUp, 0.3f, Mode::OneShot, true)
         .mod (MS::Lfo1, cutoff1(), 70.0f)
         .macro (0, "Rise Time", 0.0f).macro (1, "Space", 0.0f)
         .mod (MS::Macro1, lfoRate (0), -30.0f)
         .mod (MS::Macro2, P::ReverbMix, 35.0f)
         .delay (ND::QuarterDotted, 45.0f, 25.0f)
         .reverb (90.0f, 80.0f, 35.0f)
         .master (-3.0f);
        add (r);
    }
    {   // Ball drop: pitch follows the bouncing ball down to rest.
        Recipe r ("Ball Drop", "FX", { "bouncy", "physical", "digital" });
        r.osc (0, Tbl::DigitalGlass, 30.0f, 75.0f)
         .filter (0, Flt::LP12, 7000.0f, 10.0f)
         .env (0, 1.0f, 1500.0f, 50.0f, 800.0f)
         .ball (45.0f, 80.0f)
         .mod (MS::Ball, pitchA(), 15.0f)
         .macro (0, "Height", 0.0f).macro (1, "Space", 0.0f)
         .mod (MS::Macro1, pitchA(), 8.0f)
         .mod (MS::Macro2, P::ReverbMix, 30.0f)
         .reverb (55.0f, 50.0f, 25.0f)
         .master (3.0f);
        add (r);
    }
    {   // Laser zap: fast Env 3 pitch dive on an FM table, crushed.
        Recipe r ("Laser Zap", "FX", { "digital", "short", "aggressive" });
        r.osc (0, Tbl::FmSweep, 60.0f, 75.0f)
         .filter (0, Flt::LP12, 9000.0f, 10.0f)
         .env (0, 0.5f, 600.0f, 0.0f, 300.0f, 0.0f, -10.0f)
         .env (2, 0.0f, 250.0f, 0.0f, 150.0f)
         .mod (MS::Env3, pitchA(), 40.0f)
         .mod (MS::Env3, posA(), 40.0f)
         .macro (0, "Zap Length", 0.0f).macro (1, "Crush", 0.0f)
         .mod (MS::Macro1, envDecay (2), 35.0f)
         .mod (MS::Macro2, P::DistMix, 40.0f)
         .distortion (Dist::Crush, 35.0f, 8000.0f, 40.0f)
         .master (3.0f);
        add (r);
    }
    {   // Downlifter: filtered noise and a falling saw, a one-shot sweep closing everything down.
        Recipe r ("Downlifter", "FX", { "cinematic", "noisy", "dark" });
        r.osc (0, Tbl::AnalogSaws, 60.0f, 60.0f).unison (0, 6, 30.0f, 90.0f).oscPitch (0, 1)
         .noise (30.0f, Noise::Pink)
         .filter (0, Flt::LP24, 9000.0f, 25.0f)
         .env (0, 5.0f, 1500.0f, 80.0f, 1500.0f)
         .lfo (0, Wave::SawDown, 0.5f, Mode::OneShot, true)
         .mod (MS::Lfo1, cutoff1(), 50.0f)
         .mod (MS::Lfo1, pitchA(), 12.0f)
         .macro (0, "Fall", 0.0f).macro (1, "Space", 0.0f)
         .mod (MS::Macro1, lfoRate (0), -30.0f)
         .mod (MS::Macro2, P::ReverbMix, 35.0f)
         .reverb (80.0f, 70.0f, 30.0f)
         .master (-3.0f);
        add (r);
    }
    {   // Siren call: a triangle LFO swings the pitch of a pulse up and down a fourth, through a phaser.
        Recipe r ("Siren Call", "FX", { "retro", "rhythmic", "bright" });
        r.osc (0, Tbl::PulseWidth, 25.0f, 75.0f).oscPitch (0, 1)
         .filter (0, Flt::LP12, 5000.0f, 15.0f)
         .env (0, 5.0f, 300.0f, 90.0f, 300.0f)
         .lfo (0, Wave::Triangle, 0.8f)
         .mod (MS::Lfo1, pitchA(), 6.0f)
         .macro (0, "Speed", 0.0f).macro (1, "Phase", 0.0f)
         .mod (MS::Macro1, lfoRate (0), 30.0f)
         .mod (MS::Macro2, P::PhaserMix, 40.0f)
         .phaser (0.5f, 50.0f, 40.0f, 30.0f)
         .master (-2.0f);
        add (r);
    }
    {   // Glitch bits: a 32nd-note random arp re-pitched by a 5-step sequence, S&H on the table, crushed and gated.
        Recipe r ("Glitch Bits", "FX", { "glitchy", "digital", "rhythmic" });
        r.osc (0, Tbl::FmSweep, 30.0f, 75.0f)
         .osc (1, Tbl::SyncSweep, 50.0f, 45.0f).oscPitch (1, 1)
         .filter (0, Flt::BP12, 2000.0f, 20.0f)
         .env (0, 0.5f, 200.0f, 90.0f, 150.0f)
         .lfo (0, Wave::SampleHold, 12.0f).lfoSync (0, ND::ThirtySecond)
         .lfo (1, Wave::Square, 4.0f).lfoSync (1, ND::Sixteenth)
         .mod (MS::Lfo1, posA(), 50.0f)
         .mod (MS::Lfo1, pitchB(), 12.0f)
         .mod (MS::Lfo1, cutoff1(), 20.0f)
         .mod (MS::Lfo2, levelA(), 30.0f)
         .arp (ArpM::Random, 2, ND::ThirtySecond, 70.0f)
         .seq (ND::Sixteenth, 90.0f, false, { { 0, 100.0f, true, 50.0f }, { 7, 80.0f, true, 50.0f }, { -5, 90.0f, true, 50.0f }, { 12, 70.0f, true, 50.0f }, { 1, 90.0f, false, 50.0f } })
         .macro (0, "Glitch", 0.0f).macro (1, "Bits", 0.0f)
         .mod (MS::Macro1, lfoRate (0), 25.0f)
         .mod (MS::Macro2, P::DistDrive, 40.0f)
         .distortion (Dist::Crush, 40.0f, 9000.0f, 70.0f)
         .master (0.5f);
        add (r);
    }
    {   // Wind gust: pink noise through a resonant band-pass blown by a drawn gust shape.
        Recipe r ("Wind Gust", "FX", { "noisy", "airy", "organic" });
        r.oscOff (0)
         .noise (100.0f, Noise::Pink)
         .filter (0, Flt::BP24, 800.0f, 25.0f)
         .env (0, 400.0f, 1000.0f, 90.0f, 1500.0f)
         .lfo (0, Wave::Custom, 0.35f, Mode::Free)
         .customShape (0, { { 0.0f, 0.1f, 0.0f }, { 0.2f, 0.7f, 0.5f }, { 0.3f, 0.4f, 0.0f }, { 0.55f, 1.0f, 0.6f }, { 1.0f, 0.1f, -0.5f } })
         .mod (MS::Lfo1, cutoff1(), 40.0f)
         .mod (MS::Lfo1, P::NoiseLevel, 15.0f)
         .macro (0, "Gust", 0.0f).macro (1, "Whistle", 0.0f)
         .mod (MS::Macro1, lfoRate (0), 30.0f)
         .mod (MS::Macro2, res1(), 30.0f)
         .reverb (60.0f, 50.0f, 25.0f)
         .master (9.0f);
        add (r);
    }
    {   // Teleporter: an FM chirp climbing on a one-shot LFO through a screaming flanger.
        Recipe r ("Teleporter", "FX", { "digital", "fm", "cinematic" });
        r.osc (0, Tbl::BasicShapes, 20.0f, 75.0f).warp (0, Warp::FmOsc, 50.0f)
         .osc (1, Tbl::BasicShapes, 0.0f, 35.0f).oscPitch (1, 0, 5)
         .filter (0, Flt::LP12, 8000.0f, 10.0f)
         .env (0, 50.0f, 1500.0f, 80.0f, 1000.0f)
         .lfo (0, Wave::SawUp, 0.8f, Mode::OneShot)
         .mod (MS::Lfo1, pitchA(), 20.0f)
         .mod (MS::Lfo1, pitchB(), 25.0f)
         .macro (0, "Beam", 0.0f).macro (1, "Flange", 0.0f)
         .mod (MS::Macro1, warpA(), 30.0f)
         .mod (MS::Macro2, P::FlangerMix, 40.0f)
         .flanger (2.0f, 70.0f, 70.0f, 50.0f)
         .reverb (55.0f, 50.0f, 20.0f)
         .master (-3.0f);
        add (r);
    }
    {   // Radio static: blue noise, a notch hunting the band, crushed, with a buried carrier hopping stations (seq).
        Recipe r ("Radio Static", "FX", { "noisy", "lofi", "glitchy" });
        r.osc (0, Tbl::FormantVowels, 50.0f, 35.0f).oscPitch (0, 1)
         .noise (60.0f, Noise::Blue)
         .filter (0, Flt::Notch, 1500.0f, 30.0f)
         .filter (1, Flt::BP12, 1800.0f, 10.0f)
         .env (0, 5.0f, 300.0f, 90.0f, 300.0f)
         .lfo (0, Wave::SampleHold, 6.0f).lfoSmooth (0, 20.0f)
         .mod (MS::Lfo1, cutoff1(), 30.0f)
         .mod (MS::Lfo1, posA(), 40.0f)
         .macro (0, "Tuning", 0.0f).macro (1, "Crush", 0.0f)
         .mod (MS::Macro1, cutoff2(), 30.0f)
         .mod (MS::Macro2, P::DistMix, 40.0f)
         .seq (ND::Eighth, 100.0f, false, { { 0, 100.0f, true, 50.0f }, { 5, 100.0f, true, 50.0f }, { 0, 100.0f, true, 50.0f }, { -7, 100.0f, true, 50.0f } })
         .distortion (Dist::Crush, 50.0f, 6000.0f, 60.0f)
         .master (9.0f);
        add (r);
    }
    {   // Deep whale: a formant voice an octave down, gliding moans on a drawn pitch shape, a deep ocean.
        Recipe r ("Deep Whale", "FX", { "vocal", "deep", "ambient" });
        r.osc (0, Tbl::FormantVowels, 30.0f, 100.0f).oscPitch (0, -1)
         .filter (0, Flt::Formant, 800.0f, 15.0f)
         .filterMix (0, 75.0f)
         .env (0, 300.0f, 1000.0f, 90.0f, 2000.0f)
         .lfo (0, Wave::Custom, 0.25f)
         .customShape (0, { { 0.0f, 0.5f, 0.0f }, { 0.3f, 0.9f, 0.6f }, { 0.6f, 0.4f, -0.4f }, { 1.0f, 0.5f, 0.0f } })
         .mod (MS::Lfo1, pitchA(), 4.0f)
         .mod (MS::Lfo1, cutoff1(), 20.0f)
         .macro (0, "Moan", 0.0f).macro (1, "Ocean", 0.0f)
         .mod (MS::Macro1, pitchA(), 3.0f)
         .mod (MS::Macro2, P::ReverbMix, 35.0f)
         .delay (ND::Half, 40.0f, 20.0f)
         .reverb (85.0f, 80.0f, 35.0f, 60.0f)
         .master (5.5f);
        add (r);
    }
    {   // Temple gong: bell partials frequency-modulated by the sub, a long sympathetic shimmer.
        Recipe r ("Temple Gong", "FX", { "metallic", "cinematic", "resonant" });
        r.osc (0, Tbl::Bell, 85.0f, 75.0f).warp (0, Warp::FmSub, 25.0f).hardPhase (0)
         .sub (20.0f, Sub::Sine, 2)
         .filter (0, Flt::LP12, 5000.0f, 10.0f)
         .env (0, 1.0f, 3000.0f, 20.0f, 3000.0f)
         .env (1, 0.0f, 1500.0f, 0.0f, 1000.0f)
         .mod (MS::Env2, warpA(), 25.0f)
         .sympathetic (40.0f, 2, 8, 80.0f, 45.0f)
         .macro (0, "Strike", 0.0f).macro (1, "Shimmer", 0.0f)
         .mod (MS::Macro1, warpA(), 30.0f)
         .mod (MS::Macro2, P::SympAmount, 35.0f)
         .reverb (75.0f, 75.0f, 30.0f)
         .master (-6.5f);
        add (r);
    }
    {   // Sproing: a loose spring flings the pitch around a resonant, ringing filter.
        Recipe r ("Sproing", "FX", { "bouncy", "physical", "resonant" });
        r.osc (0, Tbl::OddHarmonics, 20.0f, 80.0f)
         .filter (0, Flt::LP24, 1500.0f, 60.0f)
         .env (0, 0.5f, 1200.0f, 0.0f, 600.0f)
         .spring (30.0f, 8.0f)
         .mod (MS::Spring, pitchA(), 10.0f)
         .mod (MS::Spring, cutoff1(), 30.0f)
         .macro (0, "Boing", 0.0f).macro (1, "Ring", 0.0f)
         .mod (MS::Macro1, pitchA(), 4.0f)
         .mod (MS::Macro2, res1(), 30.0f)
         .reverb (35.0f, 35.0f, 15.0f)
         .master (-1.0f);
        add (r);
    }
    {   // Orbit UFO: the Orbit source circles the pitch and the pan; a warbling saucer.
        Recipe r ("Orbit UFO", "FX", { "retro", "physical", "wide" });
        r.osc (0, Tbl::BasicShapes, 10.0f, 80.0f).oscPitch (0, 1).warp (0, Warp::Squeeze, 30.0f)
         .filter (0, Flt::LP12, 6000.0f, 10.0f)
         .env (0, 80.0f, 1000.0f, 90.0f, 1000.0f)
         .orbit (85.0f, 50.0f)
         .mod (MS::Orbit, pitchA(), 4.0f)
         .mod (MS::Orbit, panA(), 70.0f)
         .lfo (0, Wave::Sine, 9.0f)
         .mod (MS::Lfo1, pitchA(), 1.0f)
         .macro (0, "Warble", 0.0f).macro (1, "Echo", 0.0f)
         .mod (MS::Macro1, lfoRate (0), 25.0f)
         .mod (MS::Macro2, P::DelayMix, 35.0f)
         .flanger (0.4f, 50.0f, 50.0f, 25.0f)
         .delay (ND::Eighth, 45.0f, 20.0f)
         .master (-0.5f);
        add (r);
    }
    {   // Supersaw riser: a 12-voice saw climbing an octave on a one-shot LFO, opening as it goes.
        Recipe r ("Supersaw Riser", "FX", { "cinematic", "bright", "wide" });
        r.osc (0, Tbl::AnalogSaws, 70.0f, 70.0f).unison (0, 12, 25.0f, 100.0f, 80.0f)
         .filter (0, Flt::HP12, 200.0f, 10.0f)
         .filter (1, Flt::LP24, 1000.0f, 20.0f)
         .env (0, 300.0f, 1000.0f, 90.0f, 1200.0f)
         .lfo (0, Wave::SawUp, 0.6f, Mode::OneShot, true)
         .mod (MS::Lfo1, pitchA(), 12.0f)
         .mod (MS::Lfo1, cutoff2(), 45.0f)
         .mod (MS::Lfo1, detuneA(), 30.0f)
         .macro (0, "Rise Time", 0.0f).macro (1, "Width", 0.0f)
         .mod (MS::Macro1, lfoRate (0), -30.0f)
         .mod (MS::Macro2, spreadA(), 30.0f)
         .flanger (0.25f, 60.0f, 45.0f, 25.0f)
         .reverb (70.0f, 60.0f, 25.0f)
         .master (0.5f);
        add (r);
    }
    {   // Reverse swell: a backwards-sounding bloom: slow curved attack, instant release.
        Recipe r ("Reverse Swell", "FX", { "cinematic", "airy", "evolving" });
        r.osc (0, Tbl::Bell, 40.0f, 70.0f)
         .osc (1, Tbl::NoiseBloom, 70.0f, 40.0f)
         .filter (0, Flt::LP12, 1200.0f, 15.0f)
         .env (0, 950.0f, 50.0f, 100.0f, 40.0f, -80.0f)
         .env (1, 950.0f, 50.0f, 100.0f, 40.0f, -80.0f)
         .mod (MS::Env2, cutoff1(), 40.0f)
         .macro (0, "Length", 0.0f).macro (1, "Air", 0.0f)
         .mod (MS::Macro1, envAttack (0), 30.0f)
         .mod (MS::Macro2, levelB(), 30.0f)
         .reverb (60.0f, 40.0f, 25.0f, 60.0f)
         .master (4.5f);
        add (r);
    }
}

} // namespace verdant::factory
