// Factory bank: Texture (drones, atmospheres, beds).

#include "FactoryPresets_Recipes.h"

namespace verdant::factory
{

void addTexturePresets (std::vector<Patch>& bank)
{
    auto add = [&bank] (Recipe& r) { bank.push_back (r.p); };

    {   // Living drone: everything alive, a notch drifting through it.
        Recipe r ("Living Drone", "Texture", { "evolving", "drone", "dark" });
        r.osc (0, Tbl::Choir, 60.0f, 70.0f)
         .osc (1, Tbl::HarmonicStack, 70.0f, 45.0f).oscPitch (1, -1)
         .filter (0, Flt::Notch, 1200.0f, 40.0f)
         .env (0, 1200.0f, 2000.0f, 90.0f, 3000.0f)
         .lfo (0, Wave::Sine, 0.07f, Mode::Free)
         .mod (MS::Lfo1, cutoff1(), 30.0f)
         .living (80.0f, 90.0f)
         .macro (0, "Drift", 0.0f).macro (1, "Space", 0.0f)
         .mod (MS::Macro1, lfoRate (0), 30.0f)
         .mod (MS::Macro2, P::ReverbMix, 30.0f)
         .reverb (85.0f, 75.0f, 35.0f)
         .master (-0.5f);
        add (r);
    }
    {   // Noise garden: band-passed bloom with S&H position steps and a phaser.
        Recipe r ("Noise Garden", "Texture", { "noisy", "airy", "evolving" });
        r.osc (0, Tbl::NoiseBloom, 80.0f, 70.0f)
         .osc (1, Tbl::NoiseBloom, 40.0f, 50.0f).oscPitch (1, 1)
         .noise (20.0f, Noise::Pink)
         .filter (0, Flt::BP24, 1800.0f, 30.0f)
         .env (0, 500.0f, 1500.0f, 80.0f, 2000.0f)
         .lfo (1, Wave::SampleHold, 3.0f).lfoSmooth (1, 40.0f)
         .mod (MS::Lfo2, posB(), 40.0f)
         .living (25.0f, 50.0f)
         .macro (0, "Bloom", 0.0f).macro (1, "Focus", 0.0f)
         .mod (MS::Macro1, posA(), -35.0f)
         .mod (MS::Macro2, res1(), 30.0f)
         .phaser (0.2f, 60.0f, 35.0f, 40.0f)
         .reverb (75.0f, 65.0f, 32.0f)
         .master (10.0f);
        add (r);
    }
    {   // Halo of sympathetic strings around a soft sine.
        Recipe r ("Sympathetic Halo", "Texture", { "resonant", "ambient", "soft" });
        r.osc (0, Tbl::BasicShapes, 5.0f, 75.0f)
         .filter (0, Flt::LP12, 5000.0f, 5.0f)
         .env (0, 80.0f, 1200.0f, 70.0f, 1500.0f)
         .living (20.0f, 40.0f)
         .macro (0, "Halo", 0.0f).macro (1, "Ring", 0.0f)
         .mod (MS::Macro1, P::SympBright, 30.0f)
         .mod (MS::Macro2, P::SympDecay, 30.0f)
         .sympathetic (70.0f, 8, 12, 75.0f, 60.0f)
         .reverb (70.0f, 65.0f, 28.0f)
         .master (-19.5f);
        add (r);
    }
    {   // Mirror shards: mirrored glass, a drawn LFO on the warp, a legato random arp scattering it, flanged.
        Recipe r ("Mirror Shards", "Texture", { "glassy", "glitchy", "bright" });
        r.osc (0, Tbl::DigitalGlass, 70.0f, 75.0f).warp (0, Warp::Mirror, 30.0f)
         .osc (1, Tbl::Bell, 60.0f, 35.0f).warp (1, Warp::Remap, 40.0f)
         .filter (0, Flt::HP24, 350.0f, 15.0f)
         .env (0, 30.0f, 1000.0f, 75.0f, 1200.0f)
         .lfo (3, Wave::Custom, 1.5f)
         .customShape (3, { { 0.0f, 0.5f, 0.0f }, { 0.2f, 1.0f, 0.8f }, { 0.25f, 0.1f, 0.0f }, { 0.6f, 0.7f, -0.6f }, { 1.0f, 0.5f, 0.0f } })
         .mod (MS::Lfo4, warpA(), 35.0f)
         .living (25.0f, 40.0f)
         .macro (0, "Shatter", 0.0f).macro (1, "Flange", 0.0f)
         .mod (MS::Macro1, lfoRate (3), 30.0f)
         .mod (MS::Macro2, P::FlangerMix, 35.0f)
         .arp (ArpM::Random, 3, ND::Sixteenth, 100.0f)
         .flanger (0.3f, 55.0f, 50.0f, 40.0f)
         .reverb (55.0f, 55.0f, 22.0f)
         .master (2.5f);
        add (r);
    }
    {   // Tidal glass: aftertouch and wheel play the bloom; a performance texture.
        Recipe r ("Tidal Glass", "Texture", { "expressive", "evolving", "wide" });
        r.osc (0, Tbl::DigitalGlass, 20.0f, 65.0f).unison (0, 5, 20.0f, 90.0f)
         .osc (1, Tbl::Choir, 50.0f, 45.0f)
         .filter (0, Flt::LP24, 2400.0f, 20.0f)
         .env (0, 900.0f, 1500.0f, 85.0f, 2200.0f)
         .mod (MS::Aftertouch, cutoff1(), 35.0f)
         .mod (MS::ModWheel, posA(), 50.0f)
         .living (35.0f, 55.0f)
         .macro (0, "Tide", 0.0f).macro (1, "Brightness", 0.0f)
         .mod (MS::Macro1, posA(), 40.0f)
         .mod (MS::Macro2, cutoff1(), 30.0f)
         .chorus (0.25f, 50.0f, 35.0f)
         .reverb (80.0f, 70.0f, 33.0f)
         .master (1.0f);
        add (r);
    }
    {   // Fog bank: noise-bloom grains scattered by a smoothed S&H across the table, very wide.
        Recipe r ("Fog Bank", "Texture", { "ambient", "noisy", "wide", "evolving" });
        r.osc (0, Tbl::NoiseBloom, 30.0f, 70.0f).unison (0, 8, 30.0f, 100.0f, 90.0f)
         .filter (0, Flt::LP24, 2500.0f, 10.0f)
         .env (0, 1500.0f, 1000.0f, 90.0f, 3000.0f)
         .lfo (0, Wave::SampleHold, 5.0f, Mode::Free).lfoSmooth (0, 80.0f)
         .mod (MS::Lfo1, posA(), 40.0f)
         .living (30.0f, 70.0f)
         .macro (0, "Density", 0.0f).macro (1, "Brightness", 0.0f)
         .mod (MS::Macro1, lfoRate (0), 30.0f)
         .mod (MS::Macro2, cutoff1(), 30.0f)
         .reverb (90.0f, 80.0f, 40.0f)
         .master (3.0f);
        add (r);
    }
    {   // Machine room: an organ hum through a key-tracked comb, mains-buzz drive, slowly beating.
        Recipe r ("Machine Room", "Texture", { "drone", "dark", "metallic" });
        r.osc (0, Tbl::Organ, 5.0f, 75.0f).oscPitch (0, -1)
         .osc (1, Tbl::Organ, 5.0f, 55.0f).oscPitch (1, -1, 0, 4.0f)
         .noise (6.0f, Noise::Brown)
         .filter (0, Flt::CombPlus, 120.0f, 60.0f, 20.0f, 100.0f)
         .filter (1, Flt::LP24, 1800.0f, 10.0f)
         .env (0, 1000.0f, 1000.0f, 95.0f, 2500.0f)
         .lfo (0, Wave::Sine, 0.04f, Mode::Free)
         .mod (MS::Lfo1, cutoff2(), 15.0f)
         .living (20.0f, 60.0f)
         .macro (0, "Load", 0.0f).macro (1, "Hum", 0.0f)
         .mod (MS::Macro1, drive1(), 40.0f)
         .mod (MS::Macro2, res1(), 25.0f)
         .reverb (60.0f, 60.0f, 25.0f, 60.0f)
         .master (-1.0f);
        add (r);
    }
    {   // Ghost choir tape: a choir on a worn cassette: wow, hiss, crush, top end gone.
        Recipe r ("Ghost Choir Tape", "Texture", { "lofi", "vocal", "dusty", "ambient" });
        r.osc (0, Tbl::Choir, 55.0f, 70.0f).unison (0, 4, 12.0f, 70.0f)
         .noise (7.0f, Noise::Pink, Route::Direct)
         .filter (0, Flt::LP12, 2600.0f, 10.0f)
         .env (0, 900.0f, 1000.0f, 85.0f, 2500.0f)
         .lfo (0, Wave::Sine, 0.5f, Mode::Free)
         .mod (MS::Lfo1, fineA(), 12.0f)
         .living (25.0f, 60.0f)
         .macro (0, "Wear", 0.0f).macro (1, "Hiss", 0.0f)
         .mod (MS::Macro1, P::DistMix, 40.0f)
         .mod (MS::Macro2, P::NoiseLevel, 15.0f)
         .distortion (Dist::Crush, 15.0f, 3500.0f, 40.0f)
         .reverb (70.0f, 70.0f, 30.0f)
         .master (-0.5f);
        add (r);
    }
    {   // Aurora drone: harmonic stack and choir with long ping-pong echoes, evolving forever.
        Recipe r ("Aurora Drone", "Texture", { "drone", "lush", "evolving" });
        r.osc (0, Tbl::HarmonicStack, 80.0f, 65.0f).unison (0, 8, 10.0f, 90.0f)
         .osc (1, Tbl::Choir, 90.0f, 45.0f).oscPitch (1, 1)
         .filter (0, Flt::LP24, 3500.0f, 15.0f)
         .env (0, 1800.0f, 1000.0f, 90.0f, 3500.0f)
         .lfo (0, Wave::Triangle, 0.05f, Mode::Free)
         .mod (MS::Lfo1, posA(), 30.0f)
         .mod (MS::Lfo1, posB(), -25.0f)
         .living (25.0f, 80.0f)
         .macro (0, "Aurora", 0.0f).macro (1, "Echoes", 0.0f)
         .mod (MS::Macro1, cutoff1(), 25.0f)
         .mod (MS::Macro2, P::DelayFeedback, 30.0f)
         .delay (ND::HalfDotted, 55.0f, 25.0f)
         .reverb (85.0f, 80.0f, 35.0f)
         .master (3.5f);
        add (r);
    }
    {   // Steel cavern: noise excites two resonant combs in parallel; metallic, cavernous, shifting.
        Recipe r ("Steel Cavern", "Texture", { "metallic", "resonant", "dark" });
        r.osc (0, Tbl::SubBass, 30.0f, 40.0f).route (0, Route::Both)
         .noise (40.0f, Noise::White, Route::Both)
         .filter (0, Flt::CombPlus, 330.0f, 75.0f, 0.0f, 100.0f)
         .filter (1, Flt::CombMinus, 1100.0f, 50.0f)
         .parallel()
         .env (0, 700.0f, 1000.0f, 90.0f, 2500.0f)
         .lfo (0, Wave::Sine, 0.06f, Mode::Free)
         .mod (MS::Lfo1, cutoff2(), 12.0f)
         .living (20.0f, 60.0f)
         .macro (0, "Resonance", 0.0f).macro (1, "Cavern", 0.0f)
         .mod (MS::Macro1, res1(), 25.0f)
         .mod (MS::Macro2, P::ReverbSize, 30.0f)
         .reverb (85.0f, 80.0f, 30.0f, 50.0f)
         .master (-6.0f);
        add (r);
    }
    {   // Underwater cave: a muffled 12-stage phaser over a low formant bubble line.
        Recipe r ("Underwater Cave", "Texture", { "dark", "hollow", "ambient" });
        r.osc (0, Tbl::FormantVowels, 60.0f, 70.0f).oscPitch (0, -1)
         .osc (1, Tbl::OddHarmonics, 30.0f, 45.0f)
         .filter (0, Flt::LP24, 700.0f, 30.0f)
         .env (0, 800.0f, 1000.0f, 85.0f, 2200.0f)
         .lfo (0, Wave::SampleHold, 3.5f, Mode::Free).lfoSmooth (0, 50.0f)
         .mod (MS::Lfo1, posA(), 40.0f)
         .living (25.0f, 55.0f)
         .macro (0, "Depth", 0.0f).macro (1, "Bubbles", 0.0f)
         .mod (MS::Macro1, cutoff1(), -25.0f)
         .mod (MS::Macro2, lfoRate (0), 30.0f)
         .phaser (0.07f, 85.0f, 60.0f, 60.0f, 3)
         .reverb (75.0f, 70.0f, 30.0f, 70.0f)
         .master (-2.5f);
        add (r);
    }
    {   // Insect swarm: a 12-voice band-passed buzz with fast random pitch jitter.
        Recipe r ("Insect Swarm", "Texture", { "noisy", "organic", "glitchy" });
        r.osc (0, Tbl::PulseWidth, 75.0f, 100.0f).oscPitch (0, 1).unison (0, 12, 40.0f, 100.0f, 90.0f)
         .filter (0, Flt::BP12, 2200.0f, 15.0f)
         .filterMix (0, 80.0f)
         .env (0, 300.0f, 1000.0f, 90.0f, 1200.0f)
         .lfo (0, Wave::SampleHold, 18.0f).lfoSmooth (0, 30.0f)
         .lfo (1, Wave::Sine, 0.3f, Mode::Free)
         .mod (MS::Lfo1, pitchA(), 1.0f)
         .mod (MS::Lfo2, cutoff1(), 20.0f)
         .living (35.0f, 60.0f)
         .macro (0, "Swarm", 0.0f).macro (1, "Distance", 0.0f)
         .mod (MS::Macro1, detuneA(), 30.0f)
         .mod (MS::Macro2, P::ReverbMix, 35.0f)
         .reverb (55.0f, 50.0f, 25.0f)
         .master (8.0f);
        add (r);
    }
    {   // Bowed glass: the glass table bent by a bow-pressure LFO; slow attack, glassy sympathetic ring.
        Recipe r ("Bowed Glass", "Texture", { "glassy", "resonant", "evolving" });
        r.osc (0, Tbl::DigitalGlass, 15.0f, 70.0f).warp (0, Warp::Bend, 10.0f).unison (0, 3, 6.0f, 60.0f)
         .filter (0, Flt::LP12, 6000.0f, 10.0f)
         .env (0, 900.0f, 1000.0f, 85.0f, 2000.0f)
         .lfo (0, Wave::Custom, 0.3f)
         .customShape (0, { { 0.0f, 0.2f, 0.0f }, { 0.5f, 0.8f, 0.3f }, { 0.6f, 0.6f, 0.0f }, { 1.0f, 0.2f, -0.3f } })
         .mod (MS::Lfo1, warpA(), 25.0f)
         .sympathetic (30.0f, 6, 9, 70.0f, 70.0f)
         .living (25.0f, 50.0f)
         .macro (0, "Bow Pressure", 0.0f).macro (1, "Ring", 0.0f)
         .mod (MS::Macro1, warpA(), 30.0f)
         .mod (MS::Macro2, P::SympAmount, 30.0f)
         .reverb (70.0f, 65.0f, 28.0f)
         .master (-1.0f);
        add (r);
    }
    {   // Pulse field: a drawn rhythmic gate pulses a wide pulse stack in sixteenths.
        Recipe r ("Pulse Field", "Texture", { "rhythmic", "wide", "evolving" });
        r.osc (0, Tbl::PulseWidth, 40.0f, 70.0f).unison (0, 6, 15.0f, 100.0f)
         .osc (1, Tbl::HarmonicStack, 60.0f, 40.0f).oscPitch (1, 1)
         .filter (0, Flt::LP24, 3000.0f, 20.0f)
         .env (0, 400.0f, 1000.0f, 90.0f, 1500.0f)
         .lfo (0, Wave::Custom, 2.0f).lfoSync (0, ND::Quarter)
         .customShape (0, { { 0.0f, 1.0f, 0.0f }, { 0.2f, 0.1f, 0.0f }, { 0.25f, 0.8f, 0.0f }, { 0.45f, 0.1f, 0.0f }, { 0.5f, 1.0f, 0.0f }, { 0.75f, 0.0f, 0.0f }, { 1.0f, 0.6f, 0.0f } })
         .mod (MS::Lfo1, levelA(), 35.0f)
         .mod (MS::Lfo1, cutoff1(), 15.0f)
         .lfo (1, Wave::Triangle, 0.1f, Mode::Free)
         .mod (MS::Lfo2, posA(), 30.0f)
         .living (25.0f, 45.0f)
         .macro (0, "Pulse", 0.0f).macro (1, "Echo", 0.0f)
         .mod (MS::Macro1, levelA(), 20.0f)
         .mod (MS::Macro2, P::DelayMix, 30.0f)
         .delay (ND::EighthDotted, 40.0f, 18.0f)
         .reverb (60.0f, 55.0f, 22.0f)
         .master (-4.0f);
        add (r);
    }
    {   // Tin roof rain: violet-noise drops; the ball re-triggers a resonant band-pass as it bounces.
        Recipe r ("Tin Roof Rain", "Texture", { "noisy", "physical", "metallic" });
        r.osc (0, Tbl::Bell, 90.0f, 35.0f).oscPitch (0, 2)
         .noise (90.0f, Noise::Violet)
         .filter (0, Flt::BP24, 3500.0f, 30.0f)
         .env (0, 200.0f, 1000.0f, 90.0f, 1500.0f)
         .ball (90.0f, 90.0f)
         .mod (MS::Ball, cutoff1(), 30.0f)
         .lfo (0, Wave::SampleHold, 14.0f).lfoSmooth (0, 10.0f)
         .mod (MS::Lfo1, cutoff1(), 15.0f)
         .living (25.0f, 50.0f)
         .macro (0, "Rain", 0.0f).macro (1, "Tin", 0.0f)
         .mod (MS::Macro1, lfoRate (0), 30.0f)
         .mod (MS::Macro2, levelA(), 30.0f)
         .reverb (65.0f, 55.0f, 30.0f)
         .master (7.5f);
        add (r);
    }
    {   // Earth rumble: sub-bass table (FM from the silent sub) and brown noise, heaving on a spring and a slow LFO.
        Recipe r ("Earth Rumble", "Texture", { "deep", "sub", "dark", "cinematic" });
        r.osc (0, Tbl::SubBass, 85.0f, 75.0f).oscPitch (0, -1).warp (0, Warp::FmSub, 30.0f)
         .noise (25.0f, Noise::Brown)
         .filter (0, Flt::LP24, 300.0f, 25.0f, 30.0f)
         .env (0, 1000.0f, 1000.0f, 90.0f, 2500.0f)
         .spring (20.0f, 10.0f)
         .mod (MS::Spring, cutoff1(), 20.0f)
         .lfo (0, Wave::Sine, 0.2f, Mode::Free)
         .mod (MS::Lfo1, P::NoiseLevel, 15.0f)
         .living (20.0f, 60.0f)
         .macro (0, "Quake", 0.0f).macro (1, "Grind", 0.0f)
         .mod (MS::Macro1, cutoff1(), 25.0f)
         .mod (MS::Macro2, drive1(), 35.0f)
         .reverb (70.0f, 70.0f, 25.0f, 80.0f)
         .master (-8.0f);
        add (r);
    }
    {   // Whisper room: pink noise through the formant filter, a drawn LFO forming whispered vowels.
        Recipe r ("Whisper Room", "Texture", { "vocal", "airy", "noisy" });
        r.oscOff (0)
         .noise (100.0f, Noise::Pink)
         .filter (0, Flt::Formant, 1500.0f, 20.0f)
         .env (0, 600.0f, 1000.0f, 90.0f, 1800.0f)
         .lfo (0, Wave::Custom, 0.4f, Mode::Free)
         .customShape (0, { { 0.0f, 0.0f, 0.0f }, { 0.2f, 0.7f, 0.3f }, { 0.4f, 0.3f, 0.0f }, { 0.65f, 1.0f, -0.3f }, { 1.0f, 0.0f, 0.0f } })
         .mod (MS::Lfo1, cutoff1(), 35.0f)
         .living (25.0f, 60.0f)
         .macro (0, "Words", 0.0f).macro (1, "Room", 0.0f)
         .mod (MS::Macro1, lfoRate (0), 35.0f)
         .mod (MS::Macro2, P::ReverbMix, 35.0f)
         .eq (0.0f, 5.0f, 1500.0f, 0.0f)
         .reverb (70.0f, 60.0f, 30.0f)
         .master (11.0f);
        add (r);
    }
    {   // Orbit mist: plucked-decay frames smeared by the Orbit source and a slow flanger.
        Recipe r ("Orbit Mist", "Texture", { "physical", "soft", "ambient" });
        r.osc (0, Tbl::PluckDecay, 60.0f, 70.0f).unison (0, 4, 10.0f, 90.0f)
         .osc (1, Tbl::Choir, 40.0f, 40.0f)
         .filter (0, Flt::LP12, 3500.0f, 10.0f)
         .env (0, 900.0f, 1000.0f, 85.0f, 2200.0f)
         .orbit (15.0f, 80.0f)
         .mod (MS::Orbit, posA(), 40.0f)
         .mod (MS::Orbit, posB(), -30.0f)
         .living (25.0f, 60.0f)
         .macro (0, "Mist", 0.0f).macro (1, "Flange", 0.0f)
         .mod (MS::Macro1, cutoff1(), -25.0f)
         .mod (MS::Macro2, P::FlangerMix, 35.0f)
         .flanger (0.05f, 60.0f, 40.0f, 30.0f)
         .reverb (80.0f, 75.0f, 35.0f)
         .master (4.0f);
        add (r);
    }
}

} // namespace verdant::factory
