#include "TestHarness.h"

#include <ap/NoteQuantizer.h>
#include <ap/NoteTracker.h>
#include <ap/PitchTracker.h>
#include <ap/Scales.h>

#include <cmath>
#include <vector>

using namespace ap;

namespace
{
    constexpr double kSr = 48000.0;

    BeatClock clockAt (double bpm, int64_t startSample = 0, double startBeat = 0.0)
    {
        BeatClock c;
        c.startSample = startSample;
        c.startBeat = startBeat;
        c.beatsPerSample = bpm / (60.0 * kSr);
        return c;
    }

    TimedNoteEvent ev (TimedNoteEvent::Type type, int note, int64_t time, int vel = 100)
    {
        TimedNoteEvent e;
        e.type = type;
        e.note = note;
        e.velocity = vel;
        e.time = time;
        return e;
    }

    struct Log
    {
        std::vector<TimedNoteEvent> events;
        void operator() (const TimedNoteEvent& e) { events.push_back (e); }
        int count (TimedNoteEvent::Type t) const
        {
            int c = 0;
            for (auto& e : events) if (e.type == t) ++c;
            return c;
        }
    };

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

    /** A hummed phrase: a harmonic tone that follows a list of {midi, seconds} segments. */
    std::vector<float> hummedPhrase (const std::vector<std::pair<float, double>>& segments, int harmonics = 5)
    {
        std::vector<float> out;
        double phase = 0.0;
        Rng rng;
        for (auto& seg : segments)
        {
            const int n = (int) std::lround (seg.second * kSr);
            const double hz = seg.first > 0.0f ? midiToHz (seg.first) : 0.0;
            for (int i = 0; i < n; ++i)
            {
                float s = 0.0f;
                if (hz > 0.0)
                {
                    for (int h = 1; h <= harmonics; ++h)
                        s += (float) std::sin (kTwoPiD * phase * h) / (float) h;
                    s *= 0.3f / 1.5f;
                    phase += hz / kSr;
                }
                s += 0.002f * rng.nextBipolar();
                out.push_back (s);
            }
        }
        return out;
    }
}

//==============================================================================
AP_TEST (quantize_note_strength)
{
    const auto cMajor = Scale::fromType (ScaleType::Major, 0);

    // 0 % = chromatic rounding: C# stays C#.
    CHECK_EQ (quantizeNote (61.0f, cMajor, 0.0f), 61);
    CHECK_EQ (quantizeNote (61.3f, cMajor, 0.0f), 61);
    // 100 % = Scale::nearest.
    CHECK_EQ (quantizeNote (61.3f, cMajor, 1.0f), cMajor.nearest (61.3f));
    CHECK_EQ (quantizeNote (61.3f, cMajor, 1.0f), 62);
    CHECK_EQ (quantizeNote (60.9f, cMajor, 1.0f), 60);
    // 50 % pulls 61.3 halfway to 62 (61.65) -> rounds to 62.
    CHECK_EQ (quantizeNote (61.3f, cMajor, 0.5f), 62);
    // 25 % leaves a dead-centre C# alone (61.0 -> 61.0 pulled to 60.75 -> 61).
    CHECK_EQ (quantizeNote (61.0f, cMajor, 0.25f), 61);
    // In-scale notes never move.
    for (float m : { 60.0f, 62.2f, 63.6f, 67.0f, 71.4f })
        CHECK_EQ (quantizeNote (m, cMajor, 0.7f), (int) std::lround (m));
    // Strength is clamped.
    CHECK_EQ (quantizeNote (61.3f, cMajor, 4.0f), 62);
    CHECK_EQ (quantizeNote (61.3f, cMajor, -1.0f), 61);
}

AP_TEST (note_tracker_snap_strength_default_is_hard_snap)
{
    // The default snapStrength (1) must reproduce the old snapToScale behaviour exactly.
    NoteTracker::Params p;
    p.onsetFrames = 1;
    p.pitchBend = false;
    p.snapToScale = true;
    p.scale = Scale::fromType (ScaleType::MajorPentatonic, 0);

    for (float midi : { 61.1f, 63.4f, 65.9f, 66.5f, 70.2f, 72.0f })
    {
        NoteTracker nt;
        nt.setParams (p);
        int got = -1;
        nt.processFrame (frame (midi), [&] (const NoteTracker::Event& e) { if (e.type == NoteTracker::Event::Type::NoteOn) got = e.note; });
        CHECK_EQ (got, p.scale.nearest (midi));
    }

    // Strength 0 makes the same tracker chromatic.
    p.snapStrength = 0.0f;
    NoteTracker nt;
    nt.setParams (p);
    int got = -1;
    nt.processFrame (frame (61.1f), [&] (const NoteTracker::Event& e) { if (e.type == NoteTracker::Event::Type::NoteOn) got = e.note; });
    CHECK_EQ (got, 61);
}

//==============================================================================
AP_TEST (rhythm_quantizer_off_passes_through)
{
    RhythmQuantizer q;
    q.setGrid (0.0);
    Log log;
    const auto clock = clockAt (120.0);

    q.push (ev (TimedNoteEvent::Type::NoteOn, 60, 1234), clock, log);
    q.push (ev (TimedNoteEvent::Type::PitchBend, 60, 1300), clock, log);
    q.push (ev (TimedNoteEvent::Type::NoteOff, 60, 5000), clock, log);
    CHECK_EQ ((int) log.events.size(), 3);
    CHECK_EQ ((int) log.events[0].time, 1234);
    CHECK_EQ ((int) log.events[1].time, 1300);
    CHECK_EQ ((int) log.events[2].time, 5000);
    CHECK (! q.hasPending());
    CHECK (! q.isSounding());
}

AP_TEST (rhythm_quantizer_late_snap_and_defer)
{
    // 120 bpm: a beat is 24000 samples, a 1/8 is 12000.
    RhythmQuantizer q;
    q.setGrid (noteDivisionBeats (NoteDivision::Eighth));
    Log log;
    const auto clock = clockAt (120.0);

    // 10 % past a line (1200 samples after 12000): starts immediately.
    q.push (ev (TimedNoteEvent::Type::NoteOn, 60, 13200), clock, log);
    CHECK_EQ ((int) log.events.size(), 1);
    CHECK_EQ ((int) log.events[0].time, 13200);
    CHECK (q.isSounding());
    q.push (ev (TimedNoteEvent::Type::NoteOff, 60, 18000), clock, log);
    CHECK_EQ ((int) log.events.size(), 2);
    CHECK (! q.isSounding());

    // 40 % before the next line (24000 - 4800 = 19200): deferred to 24000, sample-accurate.
    q.push (ev (TimedNoteEvent::Type::NoteOn, 64, 19200), clock, log);
    CHECK_EQ ((int) log.events.size(), 2);
    CHECK (q.hasPending());
    for (int64_t t = 19200; t < 23999; ++t) q.advance (t, log);
    CHECK_EQ ((int) log.events.size(), 2);
    q.advance (24000, log);
    CHECK_EQ ((int) log.events.size(), 3);
    CHECK (log.events[2].type == TimedNoteEvent::Type::NoteOn);
    CHECK_EQ (log.events[2].note, 64);
    CHECK_EQ ((int) log.events[2].time, 24000);
    CHECK (! q.hasPending());

    // Note-off after the start passes through.
    q.push (ev (TimedNoteEvent::Type::NoteOff, 64, 30000), clock, log);
    CHECK_EQ (log.count (TimedNoteEvent::Type::NoteOff), 2);
}

AP_TEST (rhythm_quantizer_drops_short_pending_and_replaces)
{
    RhythmQuantizer q;
    q.setGrid (noteDivisionBeats (NoteDivision::Quarter));    // 24000 samples
    Log log;
    const auto clock = clockAt (120.0);

    // Deferred note that ends before its line: dropped entirely.
    q.push (ev (TimedNoteEvent::Type::NoteOn, 60, 14000), clock, log);
    CHECK (q.hasPending());
    q.push (ev (TimedNoteEvent::Type::NoteOff, 60, 16000), clock, log);
    CHECK (! q.hasPending());
    for (int64_t t = 14000; t <= 24000; t += 100) q.advance (t, log);
    CHECK_EQ ((int) log.events.size(), 0);

    // A newer note-on replaces the pending one; only the newest fires.
    q.push (ev (TimedNoteEvent::Type::NoteOn, 60, 38000), clock, log);
    q.push (ev (TimedNoteEvent::Type::NoteOn, 67, 40000), clock, log);
    CHECK (q.hasPending());
    q.advance (48000, log);
    CHECK_EQ (log.count (TimedNoteEvent::Type::NoteOn), 1);
    CHECK_EQ (log.events.back().note, 67);
    CHECK_EQ ((int) log.events.back().time, 48000);

    // Monophonic output: an immediate note-on while another sounds emits the old note-off first.
    q.push (ev (TimedNoteEvent::Type::NoteOn, 69, 49000), clock, log);
    const auto n = log.events.size();
    CHECK (n >= 3);
    CHECK (log.events[n - 2].type == TimedNoteEvent::Type::NoteOff);
    CHECK_EQ (log.events[n - 2].note, 67);
    CHECK (log.events[n - 1].type == TimedNoteEvent::Type::NoteOn);
    CHECK_EQ (log.events[n - 1].note, 69);

    // flush() closes the sounding note.
    q.flush (50000, log);
    CHECK (log.events.back().type == TimedNoteEvent::Type::NoteOff);
    CHECK_EQ (log.events.back().note, 69);
    CHECK (! q.isSounding());
}

//==============================================================================
AP_TEST (note_capture_records_and_backdates)
{
    NoteCapture cap (16);
    cap.setMinimumLength (2400);     // 50 ms
    const auto clock = clockAt (120.0);
    const uint32_t g0 = cap.currentGeneration();

    cap.noteOn (60, 100, 10000, clock.beatAt (10000));
    cap.noteOff (60, 34000, clock.beatAt (34000));
    cap.noteOn (64, 80, 40000, clock.beatAt (40000));
    cap.noteOff (64, 41000, clock.beatAt (41000));      // 1000 samples: too short, dropped
    cap.noteOn (67, 90, 50000, clock.beatAt (50000));
    cap.noteOn (69, 70, 60000, clock.beatAt (60000));   // implies the 67 note-off at 60000
    cap.noteOff (69, 70000, clock.beatAt (70000));
    cap.noteOff (71, 71000, clock.beatAt (71000));      // unknown note: ignored

    CHECK_EQ (cap.size(), 3);
    std::vector<CapturedNote> notes;
    cap.snapshot (notes);
    CHECK_EQ ((int) notes.size(), 3);
    CHECK_EQ (notes[0].note, 60);
    CHECK_EQ (notes[0].velocity, 100);
    CHECK_EQ ((int) notes[0].startSample, 10000);
    CHECK_EQ ((int) notes[0].endSample, 34000);
    CHECK_NEAR (notes[0].startBeat, 10000.0 * 120.0 / (60.0 * kSr), 1e-9);
    CHECK_NEAR (notes[0].endBeat - notes[0].startBeat, 1.0, 1e-9);   // 24000 samples = 1 beat
    CHECK_EQ (notes[1].note, 67);
    CHECK_EQ ((int) notes[1].endSample, 60000);
    CHECK_EQ (notes[2].note, 69);

    // clear() bumps the generation and empties the take.
    cap.clear();
    CHECK (cap.currentGeneration() != g0);
    CHECK_EQ (cap.size(), 0);
    CHECK ((cap.currentGeneration() & 1u) == 0u);     // stable after every write

    // Capacity is respected: no growth past the preallocated size.
    for (int i = 0; i < 40; ++i)
    {
        cap.noteOn (60 + (i % 12), 100, i * 10000, 0.0);
        cap.noteOff (60 + (i % 12), i * 10000 + 5000, 0.0);
    }
    CHECK_EQ (cap.size(), 16);
}

AP_TEST (note_capture_offline_quantize)
{
    std::vector<CapturedNote> take;
    auto add = [&] (int note, double start, double end)
    {
        CapturedNote n;
        n.note = note;
        n.startBeat = start;
        n.endBeat = end;
        take.push_back (n);
    };
    add (60, 0.10, 0.95);    // slightly late, nearly a beat long   -> 0..1
    add (62, 1.45, 1.70);    // snaps *forward* to 1.5, 0.25 long   -> 1.5..1.75
    add (64, 1.90, 2.35);    // snaps *back* to 2.0, 0.45 -> 0.5    -> 2..2.5
    add (65, 2.55, 2.60);    // tiny: min one grid                  -> 2.5..3
    add (67, 3.02, 3.40);    // 0.38 -> 0.5                          -> 3..3.5

    quantizeTake (take, 0.5);
    CHECK_EQ ((int) take.size(), 5);
    CHECK_NEAR (take[0].startBeat, 0.0, 1e-9); CHECK_NEAR (take[0].endBeat, 1.0, 1e-9);
    CHECK_NEAR (take[1].startBeat, 1.5, 1e-9); CHECK_NEAR (take[1].endBeat, 2.0, 1e-9);
    CHECK_NEAR (take[2].startBeat, 2.0, 1e-9); CHECK_NEAR (take[2].endBeat, 2.5, 1e-9);
    CHECK_NEAR (take[3].startBeat, 2.5, 1e-9); CHECK_NEAR (take[3].endBeat, 3.0, 1e-9);
    CHECK_NEAR (take[4].startBeat, 3.0, 1e-9); CHECK_NEAR (take[4].endBeat, 3.5, 1e-9);

    // No two notes overlap after quantising.
    for (size_t i = 0; i + 1 < take.size(); ++i)
        CHECK (take[i].endBeat <= take[i + 1].startBeat + 1e-9);

    // Overlap trimming on its own (grid off) keeps order and shortens the earlier note.
    take.clear();
    add (60, 0.0, 1.2);
    add (62, 1.0, 2.0);
    quantizeTake (take, 0.0);
    CHECK_NEAR (take[0].endBeat, 1.0, 1e-9);
    CHECK_NEAR (take[1].startBeat, 1.0, 1e-9);

    // A note that collapses to nothing is removed.
    take.clear();
    add (60, 0.0, 1.0);
    add (62, 0.0, 0.5);
    quantizeTake (take, 0.0);
    CHECK_EQ ((int) take.size(), 1);
}

//==============================================================================
AP_TEST (humline_pipeline_end_to_end)
{
    // Synthetic humming: C4 a bit flat, E4 a bit sharp with a 15 ms blip to F#4
    // in the middle, a 60 ms breath, then G4. Tracker -> NoteTracker (hold 60 ms)
    // -> capture, in C major with hard snapping.
    const auto phrase = hummedPhrase ({
        { 0.0f, 0.10 },
        { 59.8f, 0.50 },           // C4 -20 ct
        { 64.3f, 0.25 },           // E4 +30 ct
        { 66.0f, 0.015 },          // blip
        { 64.3f, 0.235 },
        { 0.0f, 0.06 },            // breath
        { 67.0f, 0.40 },           // G4
        { 0.0f, 0.30 },
    });

    PitchTracker tracker;
    tracker.prepare (kSr, 256, 1024, 60.0f, 1600.0f);
    tracker.setThreshold (0.15f);

    const float hopMs = 1000.0f * 256.0f / (float) kSr;
    const int hold = (int) std::lround (60.0f / hopMs);
    NoteTracker nt;
    NoteTracker::Params p;
    p.gateDb = -45.0f;
    p.minConfidence = 0.6f;
    p.onsetFrames = hold;
    p.changeFrames = hold;
    p.releaseFrames = (int) std::lround (50.0f / hopMs);
    p.retriggerCents = 60.0f;
    p.pitchBend = false;
    p.snapToScale = true;
    p.snapStrength = 1.0f;
    p.scale = Scale::fromType (ScaleType::Major, 0);
    nt.setParams (p);

    const int latency = tracker.latencySamples() + hold * 256;
    NoteCapture cap (64);
    cap.setMinimumLength (hold * 256);
    const auto clock = clockAt (120.0);

    int64_t now = 0;
    for (size_t i = 0; i < phrase.size(); ++i)
    {
        tracker.process (&phrase[i], 1, [&] (const PitchTracker::Frame& f)
        {
            nt.processFrame (f, [&] (const NoteTracker::Event& e)
            {
                const int64_t when = now - latency;
                if (e.type == NoteTracker::Event::Type::NoteOn)       cap.noteOn (e.note, e.velocity, when, clock.beatAt (when));
                else if (e.type == NoteTracker::Event::Type::NoteOff) cap.noteOff (e.note, when, clock.beatAt (when));
            });
        });
        ++now;
    }
    nt.flush ([&] (const NoteTracker::Event& e)
    {
        if (e.type == NoteTracker::Event::Type::NoteOff) cap.noteOff (e.note, now - latency, clock.beatAt (now - latency));
    });

    std::vector<CapturedNote> notes;
    cap.snapshot (notes);
    CHECK_EQ ((int) notes.size(), 3);
    if (notes.size() == 3)
    {
        CHECK_EQ (notes[0].note, 60);
        CHECK_EQ (notes[1].note, 64);
        CHECK_EQ (notes[2].note, 67);

        // Durations within 15 % of the sung ones (0.5 s, 0.5 s, 0.4 s).
        const double d0 = (double) (notes[0].endSample - notes[0].startSample) / kSr;
        const double d1 = (double) (notes[1].endSample - notes[1].startSample) / kSr;
        const double d2 = (double) (notes[2].endSample - notes[2].startSample) / kSr;
        CHECK_NEAR (d0, 0.50, 0.075);
        CHECK_NEAR (d1, 0.50, 0.075);
        CHECK_NEAR (d2, 0.40, 0.060);

        // Back-dating puts the first onset near where the humming started (0.10 s), not a window later.
        CHECK_NEAR ((double) notes[0].startSample / kSr, 0.10, 0.03);
    }
}
