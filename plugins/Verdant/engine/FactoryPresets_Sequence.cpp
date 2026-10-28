// Factory bank: Sequence (arpeggiator- and step-sequencer-driven patches: hold a note or a chord).

#include "FactoryPresets_Recipes.h"

namespace verdant::factory
{

void addSequencePresets (std::vector<Patch>& bank)
{
    auto add = [&bank] (Recipe& r) { bank.push_back (r.p); };

    {   // Arpeggiated bell garden.
        Recipe r ("Arp Garden", "Sequence", { "rhythmic", "bright", "sequenced" });
        r.osc (0, Tbl::Bell, 15.0f, 75.0f)
         .filter (0, Flt::LP12, 6000.0f, 10.0f)
         .env (0, 0.5f, 300.0f, 0.0f, 250.0f)
         .arp (ArpM::UpDown, 2, ND::Sixteenth, 60.0f)
         .macro (0, "Gate", 0.0f).macro (1, "Echo", 0.0f)
         .mod (MS::Macro1, envDecay (0), 30.0f)
         .mod (MS::Macro2, P::DelayMix, 30.0f)
         .delay (ND::Eighth, 30.0f, 20.0f)
         .reverb (40.0f, 40.0f, 15.0f)
         .master (2.0f);
        add (r);
    }
    {   // Sequenced pluck in C minor pentatonic (scale-degree steps).
        Recipe r ("Step Pluck", "Sequence", { "rhythmic", "sequenced", "dark" });
        r.osc (0, Tbl::PulseWidth, 30.0f, 80.0f)
         .filter (0, Flt::LP24, 1800.0f, 30.0f)
         .env (0, 0.5f, 220.0f, 0.0f, 150.0f)
         .env (1, 0.0f, 150.0f, 0.0f, 100.0f)
         .mod (MS::Env2, cutoff1(), 35.0f)
         .key (Key::C, ST::MinorPentatonic)
         .seq (ND::Sixteenth, 70.0f, true,
               { { 0, 100.0f, true, 50.0f }, { 2, 80.0f, true, 30.0f }, { 4, 90.0f, true, 70.0f }, { 1, 70.0f, true, 20.0f },
                 { 5, 100.0f, true, 90.0f }, { 3, 75.0f, true, 40.0f }, { 2, 85.0f, false, 50.0f }, { -1, 90.0f, true, 60.0f } })
         .macro (0, "Cutoff", 0.0f).macro (1, "Echo", 0.0f)
         .mod (MS::Macro1, cutoff1(), 25.0f)
         .mod (MS::Macro2, P::DelayMix, 30.0f)
         .delay (ND::EighthDotted, 30.0f, 18.0f)
         .master (0.5f);
        add (r);
    }
    {   // 16-step machine: Seq Mod opens the filter per step, folded drive.
        Recipe r ("Seq Machine", "Sequence", { "rhythmic", "glitchy", "sequenced" });
        r.osc (0, Tbl::SyncSweep, 40.0f, 75.0f)
         .filter (0, Flt::LP24, 900.0f, 40.0f)
         .env (0, 0.5f, 220.0f, 30.0f, 100.0f)
         .seq (ND::Sixteenth, 60.0f, false,
               { { 0, 100.0f, true, 90.0f }, { 0, 60.0f, true, 20.0f }, { 12, 80.0f, true, 60.0f }, { 0, 50.0f, false, 10.0f },
                 { 7, 100.0f, true, 100.0f }, { 0, 60.0f, true, 30.0f }, { -5, 75.0f, true, 50.0f }, { 0, 50.0f, true, 15.0f },
                 { 0, 100.0f, true, 85.0f }, { 3, 60.0f, true, 25.0f }, { 12, 90.0f, true, 70.0f }, { 0, 50.0f, false, 10.0f },
                 { 10, 100.0f, true, 95.0f }, { 0, 60.0f, true, 30.0f }, { 5, 80.0f, true, 55.0f }, { -12, 90.0f, true, 40.0f } })
         .mod (MS::SeqMod, cutoff1(), 45.0f)
         .macro (0, "Step Depth", 0.0f).macro (1, "Fold", 0.0f)
         .mod (MS::Macro1, res1(), 30.0f)
         .mod (MS::Macro2, P::DistDrive, 40.0f)
         .distortion (Dist::Fold, 30.0f, 6000.0f, 45.0f)
         .master (3.0f);
        add (r);
    }
    {   // Sparkling random arp in a high register.
        Recipe r ("Arp Rain", "Sequence", { "glassy", "rhythmic", "sequenced" });
        r.osc (0, Tbl::DigitalGlass, 50.0f, 70.0f).oscPitch (0, 1)
         .filter (0, Flt::HP12, 250.0f, 10.0f)
         .env (0, 0.5f, 450.0f, 35.0f, 300.0f)
         .arp (ArpM::Random, 3, ND::SixteenthTriplet, 50.0f)
         .macro (0, "Drops", 0.0f).macro (1, "Space", 0.0f)
         .mod (MS::Macro1, posA(), 30.0f)
         .mod (MS::Macro2, P::ReverbMix, 30.0f)
         .delay (ND::EighthTriplet, 40.0f, 22.0f)
         .reverb (70.0f, 65.0f, 22.0f)
         .master (8.0f);
        add (r);
    }
    {   // Berlin school: a 16-step semitone line on a ladder saw, step mod on the filter, long echoes.
        Recipe r ("Berlin Sequence", "Sequence", { "retro", "sequenced", "warm" });
        r.osc (0, Tbl::AnalogSaws, 25.0f, 75.0f)
         .osc (1, Tbl::BasicShapes, 61.0f, 45.0f).oscPitch (1, -1)
         .filter (0, Flt::Ladder, 700.0f, 45.0f, 20.0f, 40.0f)
         .env (0, 0.5f, 200.0f, 20.0f, 120.0f)
         .env (1, 0.0f, 120.0f, 0.0f, 80.0f)
         .mod (MS::Env2, cutoff1(), 25.0f)
         .mod (MS::SeqMod, cutoff1(), 25.0f)
         .seq (ND::Sixteenth, 55.0f, false,
               { { 0, 100.0f, true, 80.0f }, { 12, 70.0f, true, 30.0f }, { 3, 80.0f, true, 50.0f }, { 7, 70.0f, true, 20.0f },
                 { 0, 90.0f, true, 60.0f }, { 10, 70.0f, true, 40.0f }, { 3, 80.0f, true, 30.0f }, { 5, 70.0f, true, 70.0f },
                 { 0, 100.0f, true, 90.0f }, { 12, 70.0f, true, 30.0f }, { 3, 80.0f, true, 50.0f }, { 7, 70.0f, true, 20.0f },
                 { -2, 90.0f, true, 60.0f }, { 10, 70.0f, true, 40.0f }, { 3, 80.0f, true, 30.0f }, { 15, 75.0f, true, 100.0f } })
         .macro (0, "Cutoff", 0.0f).macro (1, "Echo", 0.0f)
         .mod (MS::Macro1, cutoff1(), 25.0f)
         .mod (MS::Macro2, P::DelayFeedback, 30.0f)
         .delay (ND::EighthDotted, 40.0f, 20.0f)
         .master (4.5f);
        add (r);
    }
    {   // Pentatonic runner: scale-degree steps in A minor pentatonic, and Pitch Snap keeps the
        // Spring wobble and any bends on the scale too.
        Recipe r ("Pentatonic Runner", "Sequence", { "sequenced", "plucky", "bright" });
        r.osc (0, Tbl::PluckDecay, 15.0f, 80.0f)
         .filter (0, Flt::LP12, 5000.0f, 10.0f)
         .env (0, 0.5f, 250.0f, 0.0f, 150.0f)
         .key (Key::A, ST::MinorPentatonic).pitchSnap()
         .spring (60.0f, 30.0f)
         .mod (MS::Spring, pitchA(), 3.0f)
         .seq (ND::Sixteenth, 70.0f, true,
               { { 0, 100.0f, true, 50.0f }, { 1, 70.0f, true, 50.0f }, { 2, 85.0f, true, 50.0f }, { 4, 70.0f, true, 50.0f },
                 { 3, 95.0f, true, 50.0f }, { 1, 70.0f, true, 50.0f }, { 5, 85.0f, true, 50.0f }, { 2, 70.0f, true, 50.0f } })
         .macro (0, "Wobble", 0.0f).macro (1, "Echo", 0.0f)
         .mod (MS::Macro1, pitchA(), 3.0f)
         .mod (MS::Macro2, P::DelayMix, 30.0f)
         .delay (ND::EighthDotted, 30.0f, 15.0f)
         .master (9.0f);
        add (r);
    }
    {   // Trance gate arp: a supersaw arpeggio up three octaves in sixteenths.
        Recipe r ("Trance Gate Arp", "Sequence", { "bright", "wide", "rhythmic", "modern" });
        r.osc (0, Tbl::AnalogSaws, 50.0f, 75.0f).unison (0, 8, 22.0f, 100.0f)
         .filter (0, Flt::LP24, 3500.0f, 25.0f)
         .env (0, 1.0f, 180.0f, 30.0f, 80.0f)
         .env (1, 0.0f, 150.0f, 0.0f, 80.0f)
         .mod (MS::Env2, cutoff1(), 20.0f)
         .arp (ArpM::Up, 3, ND::Sixteenth, 55.0f)
         .macro (0, "Gate", 0.0f).macro (1, "Brightness", 0.0f)
         .mod (MS::Macro1, envDecay (0), 30.0f)
         .mod (MS::Macro2, cutoff1(), 25.0f)
         .delay (ND::EighthDotted, 30.0f, 15.0f)
         .reverb (45.0f, 40.0f, 12.0f)
         .master (0.5f);
        add (r);
    }
    {   // Dorian bells: a key-snapped sequence in D Dorian; an LFO on pitch with Pitch Snap adds
        // grace notes that always land in the scale.
        Recipe r ("Dorian Bells", "Sequence", { "metallic", "sequenced", "glassy" });
        r.osc (0, Tbl::Bell, 30.0f, 75.0f).hardPhase (0)
         .filter (0, Flt::LP12, 7000.0f, 5.0f)
         .env (0, 0.5f, 400.0f, 0.0f, 250.0f)
         .key (Key::D, ST::Dorian).pitchSnap()
         .lfo (0, Wave::SampleHold, 2.0f).lfoSync (0, ND::Quarter)
         .mod (MS::Lfo1, pitchA(), 4.0f)
         .seq (ND::Eighth, 75.0f, true,
               { { 0, 100.0f, true, 50.0f }, { 2, 80.0f, true, 50.0f }, { 4, 90.0f, true, 50.0f }, { 6, 75.0f, true, 50.0f },
                 { 7, 100.0f, true, 50.0f }, { 4, 80.0f, true, 50.0f }, { 2, 85.0f, true, 50.0f }, { 1, 75.0f, true, 50.0f } })
         .macro (0, "Grace Notes", 0.0f).macro (1, "Space", 0.0f)
         .mod (MS::Macro1, pitchA(), 4.0f)
         .mod (MS::Macro2, P::ReverbMix, 30.0f)
         .reverb (45.0f, 40.0f, 15.0f)
         .master (4.0f);
        add (r);
    }
    {   // Acid sequence: semitone steps with accents (velocity) and Seq Mod on resonance and cutoff.
        Recipe r ("Acid Sequence", "Sequence", { "acid", "sequenced", "resonant", "mono" });
        r.osc (0, Tbl::BasicShapes, 39.0f, 80.0f)
         .filter (0, Flt::Ladder, 500.0f, 65.0f, 25.0f, 30.0f)
         .env (0, 0.5f, 250.0f, 50.0f, 60.0f)
         .env (1, 0.0f, 160.0f, 0.0f, 80.0f)
         .mod (MS::Env2, cutoff1(), 35.0f)
         .mod (MS::SeqMod, cutoff1(), 20.0f)
         .mod (MS::Velocity, res1(), 15.0f)
         .mono (VMode::Legato, 40.0f)
         .seq (ND::Sixteenth, 65.0f, false,
               { { 0, 100.0f, true, 90.0f }, { 0, 60.0f, true, 20.0f }, { 12, 70.0f, true, 40.0f }, { 0, 60.0f, true, 10.0f },
                 { 3, 100.0f, true, 80.0f }, { 0, 60.0f, false, 20.0f }, { 15, 80.0f, true, 60.0f }, { 0, 60.0f, true, 30.0f },
                 { 7, 100.0f, true, 100.0f }, { 0, 60.0f, true, 10.0f }, { 12, 70.0f, true, 50.0f }, { 10, 90.0f, true, 70.0f } })
         .macro (0, "Cutoff", 0.0f).macro (1, "Squelch", 0.0f).macro (2, "Drive", 0.0f)
         .mod (MS::Macro1, cutoff1(), 25.0f)
         .mod (MS::Macro2, envDecay (1), 35.0f)
         .mod (MS::Macro3, P::DistDrive, 40.0f)
         .distortion (Dist::Diode, 25.0f, 5000.0f, 50.0f)
         .delay (ND::EighthDotted, 25.0f, 12.0f)
         .master (5.0f);
        add (r);
    }
    {   // Chord pulse: the arp's Chord mode re-strikes the held chord in swung eighths.
        Recipe r ("Chord Pulse", "Sequence", { "chord", "rhythmic", "warm" });
        r.osc (0, Tbl::HarmonicStack, 35.0f, 75.0f).unison (0, 3, 10.0f, 60.0f)
         .osc (1, Tbl::PulseWidth, 30.0f, 45.0f)
         .filter (0, Flt::LP24, 2000.0f, 20.0f, 0.0f, 30.0f)
         .env (0, 1.0f, 250.0f, 0.0f, 150.0f)
         .env (1, 0.0f, 200.0f, 0.0f, 100.0f)
         .mod (MS::Env2, cutoff1(), 25.0f)
         .arp (ArpM::Chord, 1, ND::Eighth, 50.0f, 35.0f)
         .macro (0, "Brightness", 0.0f).macro (1, "Length", 0.0f)
         .mod (MS::Macro1, cutoff1(), 25.0f)
         .mod (MS::Macro2, envDecay (0), 30.0f)
         .chorus (0.5f, 30.0f, 25.0f)
         .reverb (40.0f, 35.0f, 12.0f)
         .master (6.5f);
        add (r);
    }
    {   // Lydian sparkle: a random arp snapped to F Lydian, plus a sequencer re-pitching the arp's notes.
        Recipe r ("Lydian Sparkle", "Sequence", { "glassy", "bright", "sequenced" });
        r.osc (0, Tbl::DigitalGlass, 60.0f, 70.0f).hardPhase (0)
         .osc (1, Tbl::BasicShapes, 6.0f, 40.0f).oscPitch (1, 1)
         .filter (0, Flt::LP12, 8000.0f, 5.0f)
         .env (0, 0.5f, 300.0f, 0.0f, 200.0f)
         .key (Key::F, ST::Lydian)
         .arp (ArpM::Random, 2, ND::Sixteenth, 60.0f).arpSnap()
         .seq (ND::Eighth, 90.0f, true,
               { { 0, 100.0f, true, 50.0f }, { 4, 80.0f, true, 50.0f }, { 2, 90.0f, true, 50.0f }, { 6, 70.0f, true, 50.0f } })
         .macro (0, "Sparkle", 0.0f).macro (1, "Echo", 0.0f)
         .mod (MS::Macro1, posA(), 30.0f)
         .mod (MS::Macro2, P::DelayMix, 30.0f)
         .delay (ND::QuarterDotted, 35.0f, 15.0f)
         .master (4.0f);
        add (r);
    }
    {   // Swing clav seq: a funky mono band-passed clav line with heavy swing and accents.
        Recipe r ("Swing Clav Seq", "Sequence", { "rhythmic", "percussive", "vintage" });
        r.osc (0, Tbl::PulseWidth, 55.0f, 100.0f)
         .filter (0, Flt::BP12, 1200.0f, 15.0f, 20.0f, 60.0f)
         .filterMix (0, 85.0f)
         .env (0, 0.5f, 250.0f, 25.0f, 80.0f)
         .mono (VMode::Mono, 0.0f)
         .env (1, 0.0f, 100.0f, 0.0f, 60.0f)
         .mod (MS::Env2, cutoff1(), 30.0f)
         .seq (ND::Sixteenth, 50.0f, false,
               { { 0, 100.0f, true, 50.0f }, { 0, 50.0f, true, 50.0f }, { 12, 80.0f, true, 50.0f }, { 0, 40.0f, false, 50.0f },
                 { 10, 90.0f, true, 50.0f }, { 0, 50.0f, true, 50.0f }, { 7, 85.0f, true, 50.0f }, { 5, 60.0f, true, 50.0f } }, 60.0f)
         .macro (0, "Wah", 0.0f).macro (1, "Swing", 0.0f)
         .mod (MS::Macro1, cutoff1(), 30.0f)
         .mod (MS::Macro2, envDecay (0), 25.0f)
         .compressor (-20.0f, 4.0f, 3.0f, 3.0f)
         .master (10.0f);
        add (r);
    }
    {   // Cascade down: a soft pad arpeggio falling two octaves in dotted eighths into a hall.
        Recipe r ("Cascade Down", "Sequence", { "soft", "ambient", "sequenced" });
        r.osc (0, Tbl::Choir, 30.0f, 70.0f).unison (0, 3, 10.0f, 70.0f)
         .osc (1, Tbl::Bell, 20.0f, 30.0f).oscPitch (1, 1)
         .filter (0, Flt::LP12, 4000.0f, 10.0f)
         .env (0, 2.0f, 350.0f, 0.0f, 300.0f)
         .arp (ArpM::Down, 2, ND::EighthDotted, 60.0f)
         .macro (0, "Bell", 0.0f).macro (1, "Space", 0.0f)
         .mod (MS::Macro1, levelB(), 30.0f)
         .mod (MS::Macro2, P::ReverbMix, 30.0f)
         .reverb (60.0f, 55.0f, 20.0f)
         .master (6.5f);
        add (r);
    }
    {   // Morph steps: an up-down arp feeding a Phrygian sequence whose Seq Mod lane morphs the wavetable.
        Recipe r ("Morph Steps", "Sequence", { "evolving", "sequenced", "digital" });
        r.osc (0, Tbl::FmSweep, 20.0f, 80.0f)
         .filter (0, Flt::LP24, 3000.0f, 20.0f)
         .env (0, 0.5f, 220.0f, 25.0f, 120.0f)
         .key (Key::E, ST::Phrygian)
         .mod (MS::SeqMod, posA(), 60.0f)
         .arp (ArpM::UpDown, 2, ND::Eighth, 90.0f)
         .seq (ND::Sixteenth, 65.0f, true,
               { { 0, 100.0f, true, 0.0f }, { 1, 70.0f, true, 30.0f }, { 0, 80.0f, true, 60.0f }, { 3, 70.0f, true, 90.0f },
                 { 4, 100.0f, true, 20.0f }, { 1, 70.0f, true, 80.0f }, { -1, 90.0f, true, 40.0f }, { 0, 70.0f, true, 100.0f } })
         .macro (0, "Morph", 0.0f).macro (1, "Cutoff", 0.0f)
         .mod (MS::Macro1, posA(), 30.0f)
         .mod (MS::Macro2, cutoff1(), 25.0f)
         .delay (ND::Eighth, 25.0f, 12.0f)
         .master (-2.5f);
        add (r);
    }
}

} // namespace verdant::factory
