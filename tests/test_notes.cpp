#include "TestHarness.h"

#include <ap/NoteTracker.h>

#include <vector>

using namespace ap;

namespace
{
    PitchTracker::Frame frame (float midi, float rmsDb = -20.0f, float conf = 0.95f)
    {
        PitchTracker::Frame f;
        f.voiced = midi > 0.0f;
        f.midi = midi;
        f.frequency = midi > 0.0f ? midiToHz (midi) : 0.0f;
        f.confidence = conf;
        f.rmsDb = rmsDb;
        return f;
    }

    struct Log
    {
        std::vector<NoteTracker::Event> events;
        void operator() (const NoteTracker::Event& e) { events.push_back (e); }
        int count (NoteTracker::Event::Type t) const
        {
            int c = 0;
            for (auto& e : events) if (e.type == t) ++c;
            return c;
        }
    };
}

AP_TEST (note_tracker_basic_note)
{
    NoteTracker nt;
    NoteTracker::Params p;
    p.onsetFrames = 2;
    p.releaseFrames = 3;
    p.pitchBend = false;
    nt.setParams (p);
    Log log;

    nt.processFrame (frame (60.1f), log);
    CHECK_EQ ((int) log.events.size(), 0);           // first voiced frame: not yet
    nt.processFrame (frame (60.05f), log);
    CHECK_EQ ((int) log.events.size(), 1);           // onset after 2 frames
    CHECK (log.events[0].type == NoteTracker::Event::Type::NoteOn);
    CHECK_EQ (log.events[0].note, 60);

    for (int i = 0; i < 10; ++i) nt.processFrame (frame (60.2f), log);
    CHECK_EQ ((int) log.events.size(), 1);           // steady note: nothing new

    nt.processFrame (frame (0.0f), log);
    nt.processFrame (frame (0.0f), log);
    CHECK_EQ ((int) log.events.size(), 1);           // not released yet
    nt.processFrame (frame (0.0f), log);
    CHECK_EQ ((int) log.events.size(), 2);
    CHECK (log.events[1].type == NoteTracker::Event::Type::NoteOff);
    CHECK_EQ (log.events[1].note, 60);
    CHECK (! nt.isPlaying());
}

AP_TEST (note_tracker_retrigger_on_pitch_change)
{
    NoteTracker nt;
    NoteTracker::Params p;
    p.onsetFrames = 1;
    p.changeFrames = 2;
    p.pitchBend = false;
    nt.setParams (p);
    Log log;

    nt.processFrame (frame (64.0f), log);
    for (int i = 0; i < 5; ++i) nt.processFrame (frame (64.0f), log);
    CHECK_EQ (log.count (NoteTracker::Event::Type::NoteOn), 1);

    nt.processFrame (frame (67.0f), log);            // one frame of new pitch: ignored
    CHECK_EQ (log.count (NoteTracker::Event::Type::NoteOn), 1);
    nt.processFrame (frame (67.0f), log);            // second: retrigger
    CHECK_EQ (log.count (NoteTracker::Event::Type::NoteOff), 1);
    CHECK_EQ (log.count (NoteTracker::Event::Type::NoteOn), 2);
    CHECK_EQ (log.events.back().note, 67);
    CHECK_EQ (nt.currentMidi(), 67);

    // A brief blip back to the old pitch should not retrigger.
    nt.processFrame (frame (64.0f), log);
    nt.processFrame (frame (67.0f), log);
    CHECK_EQ (log.count (NoteTracker::Event::Type::NoteOn), 2);
}

AP_TEST (note_tracker_gate_and_confidence)
{
    NoteTracker nt;
    NoteTracker::Params p;
    p.onsetFrames = 1;
    p.gateDb = -40.0f;
    p.minConfidence = 0.7f;
    nt.setParams (p);
    Log log;

    nt.processFrame (frame (60.0f, -50.0f, 0.95f), log);   // too quiet
    CHECK_EQ ((int) log.events.size(), 0);
    nt.processFrame (frame (60.0f, -20.0f, 0.5f), log);    // not confident
    CHECK_EQ ((int) log.events.size(), 0);
    nt.processFrame (frame (60.0f, -20.0f, 0.9f), log);
    CHECK_EQ ((int) log.events.size(), 1);
}

AP_TEST (note_tracker_pitch_bend_and_scale_snap)
{
    NoteTracker nt;
    NoteTracker::Params p;
    p.onsetFrames = 1;
    p.pitchBend = true;
    p.bendRangeSemis = 2.0f;
    p.retriggerCents = 70.0f;
    p.snapToScale = true;
    p.scale = Scale::fromType (ScaleType::Major, 0);
    nt.setParams (p);
    Log log;

    nt.processFrame (frame (61.1f), log);            // C# is not in C major -> snaps to C or D
    CHECK_EQ (log.count (NoteTracker::Event::Type::NoteOn), 1);
    const int note = log.events.back().note;
    CHECK (note == 60 || note == 62);

    // Within the retrigger zone of the current note, a bend is emitted.
    nt.processFrame (frame ((float) note + 0.5f), log);
    CHECK (log.count (NoteTracker::Event::Type::PitchBend) >= 1);
    CHECK_NEAR (log.events.back().bend, 0.25f, 1e-4);   // 0.5 semitone over a 2-semitone range
}

AP_TEST (note_tracker_velocity_mapping)
{
    NoteTracker nt;
    NoteTracker::Params p;
    p.onsetFrames = 1;
    p.dynamicVelocity = true;
    p.velocityMinDb = -40.0f;
    p.velocityMaxDb = -10.0f;
    nt.setParams (p);
    Log log;

    nt.processFrame (frame (60.0f, -10.0f), log);
    CHECK_EQ (log.events.back().velocity, 127);
    nt.flush (log);
    nt.processFrame (frame (60.0f, -25.0f), log);
    CHECK_EQ (log.events.back().velocity, 64);
    nt.flush (log);
    nt.processFrame (frame (60.0f, -40.0f), log);
    CHECK_EQ (log.events.back().velocity, 1);
}
