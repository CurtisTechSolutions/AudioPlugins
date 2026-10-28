// Key-aware parts: scale snapping, arpeggiator order / timing / gate / swing /
// snap / release, step sequencer timing / pitches / rests / mod value, event
// ordering and output capacity.

#include "TestHarness.h"

#include "engine/KeyAware.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace verdant;

namespace
{
    constexpr double kSr = 48000.0;

    struct Ev
    {
        int64_t t;
        MidiEvent e;
    };

    MidiEvent noteOn (int note, float vel = 0.8f)
    {
        MidiEvent e;
        e.type = MidiEvent::Type::NoteOn;
        e.note = note;
        e.value = vel;
        return e;
    }

    MidiEvent noteOff (int note)
    {
        MidiEvent e;
        e.type = MidiEvent::Type::NoteOff;
        e.note = note;
        return e;
    }

    bool sortedOk = true;

    /** Drives a processor over `total` samples in blocks; input events at absolute
        times; returns every output event with its absolute time. `synced` = host
        playing with tempo, ppq advancing from 0 (or from ppqStart). */
    template <class Proc>
    std::vector<Ev> drive (Proc& p, const std::vector<Ev>& input, int64_t total, int block, bool synced,
                           double bpm = 120.0, double ppqStart = 0.0, int maxOut = kMaxBlock)
    {
        std::vector<Ev> all;
        std::vector<MidiEvent> inBlock;
        std::vector<MidiEvent> outBuf ((size_t) kMaxBlock);
        size_t next = 0;
        const double spb = 60.0 / bpm * kSr;

        for (int64_t start = 0; start < total; start += block)
        {
            const int n = (int) std::min<int64_t> (block, total - start);
            inBlock.clear();
            while (next < input.size() && input[next].t < start + n)
            {
                MidiEvent e = input[next].e;
                e.sampleOffset = (int) (input[next].t - start);
                inBlock.push_back (e);
                ++next;
            }

            Transport tr;
            tr.bpm = bpm;
            tr.hasTempo = synced;
            tr.playing = synced;
            tr.ppq = ppqStart + (double) start / spb;
            tr.sampleTime = start;

            const int m = p.process (inBlock.data(), (int) inBlock.size(), tr, n, outBuf.data(), maxOut);
            for (int j = 0; j < m; ++j)
            {
                if (j > 0 && outBuf[(size_t) j].sampleOffset < outBuf[(size_t) j - 1].sampleOffset) sortedOk = false;
                if (outBuf[(size_t) j].sampleOffset < 0 || outBuf[(size_t) j].sampleOffset >= n) sortedOk = false;
                all.push_back ({ start + outBuf[(size_t) j].sampleOffset, outBuf[(size_t) j] });
            }
        }
        return all;
    }

    std::vector<Ev> ons (const std::vector<Ev>& v)
    {
        std::vector<Ev> r;
        for (const auto& e : v)
            if (e.e.type == MidiEvent::Type::NoteOn) r.push_back (e);
        return r;
    }

    std::vector<Ev> offs (const std::vector<Ev>& v)
    {
        std::vector<Ev> r;
        for (const auto& e : v)
            if (e.e.type == MidiEvent::Type::NoteOff) r.push_back (e);
        return r;
    }

    std::vector<Ev> holdChord (std::initializer_list<int> notes, int64_t at = 0)
    {
        std::vector<Ev> v;
        for (int n : notes) v.push_back ({ at, noteOn (n) });
        return v;
    }

    const ap::Scale cMajor = ap::Scale::fromType (ap::ScaleType::Major, 0);
    const ap::Scale chromatic = ap::Scale::fromType (ap::ScaleType::Chromatic, 0);

    Arpeggiator makeArp (int mode, int octaves = 1, float swing = 0.0f, bool snap = false, const ap::Scale& scale = chromatic)
    {
        Arpeggiator a;
        a.prepare (kSr);
        ArpParams p;
        p.on = true;
        p.mode = mode;
        p.octaves = octaves;
        p.division = ap::NoteDivision::Eighth;
        p.gate = 0.8f;
        p.swing = swing;
        p.snap = snap;
        a.set (p, scale);
        return a;
    }

    /** Checks the NoteOn notes (first expected.size()) and their spacing. */
    void checkOrder (const std::vector<Ev>& out, std::initializer_list<int> expected, int64_t start, int64_t step)
    {
        const auto on = ons (out);
        CHECK (on.size() >= expected.size());
        size_t k = 0;
        for (int note : expected)
        {
            if (k >= on.size()) break;
            CHECK_EQ (on[k].e.note, note);
            CHECK_NEAR ((double) on[k].t, (double) (start + (int64_t) k * step), 1.0);
            ++k;
        }
    }
}

//==============================================================================
AP_TEST (keyaware_snap_to_scale)
{
    CHECK_EQ (snapToScale (61, cMajor), 60);   // C# -> C (tie -> lower)
    CHECK_EQ (snapToScale (66, cMajor), 65);   // F# -> F (tie -> lower)
    CHECK_EQ (snapToScale (64, cMajor), 64);
    CHECK_EQ (snapToScale (70, cMajor), 69);   // A# -> A
    CHECK_EQ (snapToScale (61, chromatic), 61);

    CHECK_EQ (scaleDegreeOffset (60, 2, cMajor), 64);
    CHECK_EQ (scaleDegreeOffset (60, 7, cMajor), 72);
    CHECK_EQ (scaleDegreeOffset (60, -1, cMajor), 59);
    CHECK_EQ (scaleDegreeOffset (60, -7, cMajor), 48);
    CHECK_EQ (scaleDegreeOffset (61, 1, cMajor), 62);   // snaps C# to C first
    CHECK_EQ (scaleDegreeOffset (60, 5, chromatic), 65);
    CHECK_EQ (scaleDegreeOffset (60, 0, cMajor), 60);

    const auto aMinorPent = ap::Scale::fromType (ap::ScaleType::MinorPentatonic, 9);
    CHECK_EQ (scaleDegreeOffset (57, 5, aMinorPent), 69);   // one octave of a 5-note scale
}

AP_TEST (keyaware_arp_up_timing_synced_and_free)
{
    for (bool synced : { true, false })
    {
        sortedOk = true;
        auto arp = makeArp (ArpParams::Up);
        const auto out = drive (arp, holdChord ({ 60, 64, 67 }), 12000 * 8, 512, synced);
        checkOrder (out, { 60, 64, 67, 60, 64, 67, 60, 64 }, 0, 12000);
        CHECK_EQ ((int) ons (out).size(), 8);
        CHECK (sortedOk);

        // Gate 80 %: each NoteOff 9600 samples after its NoteOn, same note.
        const auto on = ons (out);
        const auto off = offs (out);
        CHECK (off.size() >= on.size() - 1);
        for (size_t k = 0; k < std::min (on.size(), off.size()); ++k)
        {
            CHECK_EQ (off[k].e.note, on[k].e.note);
            CHECK_NEAR ((double) (off[k].t - on[k].t), 9600.0, 1.0);
        }
        CHECK_NEAR (on[0].e.value, 0.8, 1.0e-6);   // the held note's velocity
    }
}

AP_TEST (keyaware_arp_free_runs_from_first_note)
{
    // Not playing: the clock starts at the first NoteOn (sample 1000) at 120 bpm.
    auto arp = makeArp (ArpParams::Up);
    const auto out = drive (arp, holdChord ({ 64, 60 }, 1000), 1000 + 12000 * 4, 256, false);
    checkOrder (out, { 60, 64, 60, 64 }, 1000, 12000);

    // Playing with tempo: steps lock to the ppq grid, so a note at sample 1000
    // waits for the next eighth (sample 12000).
    auto arp2 = makeArp (ArpParams::Up);
    const auto out2 = drive (arp2, holdChord ({ 64, 60 }, 1000), 12000 * 4, 256, true);
    checkOrder (out2, { 60, 64, 60 }, 12000, 12000);

    // Host tempo 90 bpm, not playing: free-runs at transport.bpm (eighth = 16000).
    {
        Arpeggiator a = makeArp (ArpParams::Up);
        std::vector<MidiEvent> outBuf (64);
        Transport tr;
        tr.bpm = 90.0;
        tr.hasTempo = true;
        tr.playing = false;
        MidiEvent in = noteOn (60);
        std::vector<int64_t> times;
        for (int64_t start = 0; start < 16000 * 3; start += 500)
        {
            const int m = a.process (start == 0 ? &in : nullptr, start == 0 ? 1 : 0, tr, 500, outBuf.data(), 64);
            for (int j = 0; j < m; ++j)
                if (outBuf[(size_t) j].type == MidiEvent::Type::NoteOn) times.push_back (start + outBuf[(size_t) j].sampleOffset);
        }
        CHECK_EQ ((int) times.size(), 3);
        if (times.size() == 3)
        {
            CHECK_EQ (times[1] - times[0], (int64_t) 16000);
            CHECK_EQ (times[2] - times[1], (int64_t) 16000);
        }
    }
}

AP_TEST (keyaware_arp_modes)
{
    {
        auto arp = makeArp (ArpParams::Down);
        checkOrder (drive (arp, holdChord ({ 60, 64, 67 }), 12000 * 6, 512, true), { 67, 64, 60, 67, 64, 60 }, 0, 12000);
    }
    {
        auto arp = makeArp (ArpParams::UpDown);
        checkOrder (drive (arp, holdChord ({ 60, 64, 67 }), 12000 * 9, 512, true), { 60, 64, 67, 64, 60, 64, 67, 64, 60 }, 0, 12000);
    }
    {
        auto arp = makeArp (ArpParams::DownUp);
        checkOrder (drive (arp, holdChord ({ 60, 64, 67 }), 12000 * 5, 512, true), { 67, 64, 60, 64, 67 }, 0, 12000);
    }
    {
        // As played: order of the key presses, not pitch.
        std::vector<Ev> in = { { 0, noteOn (67) }, { 0, noteOn (60) }, { 0, noteOn (64) } };
        auto arp = makeArp (ArpParams::AsPlayed);
        checkOrder (drive (arp, in, 12000 * 6, 512, true), { 67, 60, 64, 67, 60, 64 }, 0, 12000);
    }
    {
        // Chord: all held notes on every step, all released at the gate.
        auto arp = makeArp (ArpParams::Chord);
        const auto out = drive (arp, holdChord ({ 60, 64, 67 }), 12000 * 3, 512, true);
        const auto on = ons (out);
        CHECK_EQ ((int) on.size(), 9);
        for (size_t k = 0; k < on.size(); ++k)
            CHECK_EQ (on[k].t, (int64_t) (k / 3) * 12000);
        CHECK_EQ ((int) offs (out).size(), 9);
    }
    {
        // Octaves 2: the pattern repeats one octave up.
        auto arp = makeArp (ArpParams::Up, 2);
        checkOrder (drive (arp, holdChord ({ 60, 64, 67 }), 12000 * 7, 512, true), { 60, 64, 67, 72, 76, 79, 60 }, 0, 12000);
    }
    {
        // Random: deterministic after reset, every note from the set, no immediate repeat.
        auto run = [] ()
        {
            auto arp = makeArp (ArpParams::Random, 2);
            return ons (drive (arp, holdChord ({ 60, 64, 67 }), 12000 * 64, 512, true));
        };
        const auto a = run();
        const auto b = run();
        CHECK_EQ ((int) a.size(), 64);
        CHECK_EQ (a.size(), b.size());
        bool same = true, repeat = false, inSet = true;
        int seen[128] = {};
        for (size_t k = 0; k < a.size() && k < b.size(); ++k)
        {
            same = same && a[k].e.note == b[k].e.note;
            if (k > 0 && a[k].e.note == a[k - 1].e.note) repeat = true;
            const int n = a[k].e.note;
            inSet = inSet && (n == 60 || n == 64 || n == 67 || n == 72 || n == 76 || n == 79);
            seen[n] = 1;
        }
        CHECK (same);
        CHECK (! repeat);
        CHECK (inSet);
        CHECK_EQ (seen[60] + seen[64] + seen[67] + seen[72] + seen[76] + seen[79], 6);
    }
}

AP_TEST (keyaware_arp_swing_and_snap)
{
    // Swing 100 %: odd steps delayed by step / 3 = 4000 samples; even steps on the grid.
    for (bool synced : { true, false })
    {
        auto arp = makeArp (ArpParams::Up, 1, 1.0f);
        const auto on = ons (drive (arp, holdChord ({ 60, 64, 67 }), 12000 * 6, 512, synced));
        CHECK_EQ ((int) on.size(), 6);
        for (size_t k = 0; k < on.size(); ++k)
            CHECK_NEAR ((double) on[k].t, (double) ((int64_t) k * 12000 + ((k & 1) ? 4000 : 0)), 1.0);
    }

    // Swing 50 % -> 2000 samples.
    {
        auto arp = makeArp (ArpParams::Up, 1, 0.5f);
        const auto on = ons (drive (arp, holdChord ({ 60 }), 12000 * 4, 480, true));
        CHECK (on.size() >= 4);
        if (on.size() >= 4)
        {
            CHECK_NEAR ((double) on[1].t, 14000.0, 1.0);
            CHECK_NEAR ((double) on[3].t, 38000.0, 1.0);
        }
    }

    // Snap: a held C# plays C in C major; off it stays C#.
    {
        auto arp = makeArp (ArpParams::Up, 1, 0.0f, true, cMajor);
        const auto on = ons (drive (arp, holdChord ({ 61 }), 12000 * 2, 512, true));
        CHECK (! on.empty());
        for (const auto& e : on) CHECK_EQ (e.e.note, 60);
    }
    {
        auto arp = makeArp (ArpParams::Up, 1, 0.0f, false, cMajor);
        const auto on = ons (drive (arp, holdChord ({ 61 }), 12000 * 2, 512, true));
        CHECK (! on.empty());
        for (const auto& e : on) CHECK_EQ (e.e.note, 61);
    }
}

AP_TEST (keyaware_arp_release_stops)
{
    // Release all three notes mid-step (sample 30000, during the 3rd step's gate):
    // the sounding note is released in that block, at that sample; nothing after.
    std::vector<Ev> in = holdChord ({ 60, 64, 67 });
    in.push_back ({ 30000, noteOff (60) });
    in.push_back ({ 30000, noteOff (64) });
    in.push_back ({ 30000, noteOff (67) });

    auto arp = makeArp (ArpParams::Up);
    sortedOk = true;
    const auto out = drive (arp, in, 12000 * 6, 512, true);
    CHECK (sortedOk);
    const auto on = ons (out);
    const auto off = offs (out);
    CHECK_EQ ((int) on.size(), 3);   // steps at 0, 12000, 24000
    CHECK_EQ ((int) off.size(), 3);
    if (off.size() == 3)
    {
        CHECK_EQ (off[2].e.note, 67);
        CHECK_EQ (off[2].t, (int64_t) 30000);
    }
    CHECK (! arp.isRunning());

    // Releasing one note of three: it leaves the pattern; the others continue.
    std::vector<Ev> in2 = holdChord ({ 60, 64, 67 });
    in2.push_back ({ 13000, noteOff (64) });   // releases the sounding E immediately
    auto arp2 = makeArp (ArpParams::Up);
    const auto out2 = drive (arp2, in2, 12000 * 4, 512, true);
    const auto on2 = ons (out2);
    CHECK_EQ ((int) on2.size(), 4);
    if (on2.size() == 4)
    {
        CHECK_EQ (on2[0].e.note, 60);
        CHECK_EQ (on2[1].e.note, 64);
        CHECK_EQ (on2[2].e.note, 60);   // counter 2 over {60, 67}
        CHECK_EQ (on2[3].e.note, 67);
    }
    bool eReleasedAt13000 = false;
    for (const auto& e : offs (out2))
        if (e.e.note == 64 && e.t == 13000) eReleasedAt13000 = true;
    CHECK (eReleasedAt13000);

    // AllNotesOff: everything stops and the event is forwarded.
    std::vector<Ev> in3 = holdChord ({ 60 });
    MidiEvent all;
    all.type = MidiEvent::Type::AllNotesOff;
    in3.push_back ({ 5000, all });
    auto arp3 = makeArp (ArpParams::Up);
    const auto out3 = drive (arp3, in3, 12000 * 3, 512, true);
    CHECK_EQ ((int) ons (out3).size(), 1);
    bool forwarded = false;
    for (const auto& e : out3) if (e.e.type == MidiEvent::Type::AllNotesOff && e.t == 5000) forwarded = true;
    CHECK (forwarded);
}

AP_TEST (keyaware_arp_off_passes_through)
{
    Arpeggiator arp;
    arp.prepare (kSr);
    ArpParams p;   // off
    arp.set (p, cMajor);

    std::vector<MidiEvent> in = { noteOn (61), noteOff (61) };
    in[0].sampleOffset = 3;
    in[1].sampleOffset = 100;
    MidiEvent bend;
    bend.type = MidiEvent::Type::PitchBend;
    bend.value = -0.5f;
    bend.sampleOffset = 200;
    in.push_back (bend);

    std::vector<MidiEvent> out (16);
    Transport tr;
    const int n = arp.process (in.data(), (int) in.size(), tr, 512, out.data(), (int) out.size());
    CHECK_EQ (n, 3);
    for (int i = 0; i < std::min (n, 3); ++i)
    {
        CHECK (out[(size_t) i].type == in[(size_t) i].type);
        CHECK_EQ (out[(size_t) i].note, in[(size_t) i].note);
        CHECK_EQ (out[(size_t) i].sampleOffset, in[(size_t) i].sampleOffset);
        CHECK_EQ (out[(size_t) i].value, in[(size_t) i].value);
    }

    // On: non-note events still pass through, note events are consumed.
    auto on = makeArp (ArpParams::Up);
    const int m = on.process (in.data(), (int) in.size(), tr, 512, out.data(), (int) out.size());
    int bends = 0, noteOns = 0;
    for (int i = 0; i < m; ++i)
    {
        if (out[(size_t) i].type == MidiEvent::Type::PitchBend) ++bends;
        if (out[(size_t) i].type == MidiEvent::Type::NoteOn) ++noteOns;
    }
    CHECK_EQ (bends, 1);
    CHECK_EQ (noteOns, 1);   // the first step fired at sample 3 (then released at 100)
}

AP_TEST (keyaware_max_out_respected)
{
    // Chord of 8 notes, capacity 3: never more than 3 events per call.
    auto arp = makeArp (ArpParams::Chord);
    std::vector<MidiEvent> in;
    for (int n = 60; n < 68; ++n) in.push_back (noteOn (n));
    std::vector<MidiEvent> out (16);
    Transport tr;
    int maxSeen = 0;
    for (int b = 0; b < 100; ++b)
    {
        const int m = arp.process (b == 0 ? in.data() : nullptr, b == 0 ? (int) in.size() : 0, tr, 512, out.data(), 3);
        maxSeen = std::max (maxSeen, m);
    }
    CHECK (maxSeen <= 3);
    CHECK (maxSeen > 0);

    // Null output / zero capacity: nothing written, no crash.
    CHECK_EQ (arp.process (in.data(), (int) in.size(), tr, 512, nullptr, 16), 0);
    CHECK_EQ (arp.process (in.data(), (int) in.size(), tr, 512, out.data(), 0), 0);
}

//==============================================================================
namespace
{
    StepSequencer makeSeq (bool snap, std::initializer_list<int> pitches, const ap::Scale& scale = cMajor)
    {
        StepSequencer s;
        s.prepare (kSr);
        SeqParams p;
        p.on = true;
        p.steps = (int) pitches.size();
        p.division = ap::NoteDivision::Sixteenth;
        p.gate = 0.8f;
        p.snap = snap;
        int k = 0;
        for (int v : pitches) p.pitch[(size_t) k++] = v;
        for (int i = 0; i < 16; ++i)
        {
            p.velocity[(size_t) i] = 1.0f;
            p.gateOn[(size_t) i] = true;
            p.mod[(size_t) i] = 0.25f * (float) ((i % 4) + 1);
        }
        s.set (p, scale);
        return s;
    }
}

AP_TEST (keyaware_seq_timing_and_pitch)
{
    for (bool synced : { true, false })
    {
        sortedOk = true;
        auto seq = makeSeq (false, { 0, 3, 7, 12 });
        const auto out = drive (seq, holdChord ({ 60 }), 6000 * 9, 512, synced);
        CHECK (sortedOk);
        const auto on = ons (out);
        CHECK_EQ ((int) on.size(), 9);
        const int expected[] = { 60, 63, 67, 72, 60, 63, 67, 72, 60 };
        for (size_t k = 0; k < std::min<size_t> (on.size(), 9); ++k)
        {
            CHECK_EQ (on[k].e.note, expected[k]);
            CHECK_NEAR ((double) on[k].t, (double) ((int64_t) k * 6000), 1.0);
            CHECK_NEAR (on[k].e.value, 0.8, 1.0e-6);
        }
        // Gate 80 % of 6000 = 4800.
        const auto off = offs (out);
        CHECK (off.size() >= 8);
        if (! off.empty())
            CHECK_NEAR ((double) (off[0].t - on[0].t), 4800.0, 1.0);
    }

    // Snap on: pitches are scale degrees of C major from the held note.
    {
        auto seq = makeSeq (true, { 0, 3, 7, 12 });
        const auto on = ons (drive (seq, holdChord ({ 60 }), 6000 * 4, 512, true));
        CHECK_EQ ((int) on.size(), 4);
        const int expected[] = { 60, 65, 72, 81 };
        for (size_t k = 0; k < std::min<size_t> (on.size(), 4); ++k)
            CHECK_EQ (on[k].e.note, expected[k]);
    }
    // Snap on with a held D: degrees from D in C major (D, G, D+8va, B+8va).
    {
        auto seq = makeSeq (true, { 0, 3, 7, 12 });
        const auto on = ons (drive (seq, holdChord ({ 62 }), 6000 * 4, 512, true));
        const int expected[] = { 62, 67, 74, 83 };
        for (size_t k = 0; k < std::min<size_t> (on.size(), 4); ++k)
            CHECK_EQ (on[k].e.note, expected[k]);
    }
}

AP_TEST (keyaware_seq_rests_velocity_and_chords)
{
    StepSequencer seq;
    seq.prepare (kSr);
    SeqParams p;
    p.on = true;
    p.steps = 4;
    p.division = ap::NoteDivision::Sixteenth;
    p.snap = false;
    p.pitch = {};
    for (int i = 0; i < 16; ++i) { p.velocity[(size_t) i] = 1.0f; p.gateOn[(size_t) i] = true; }
    p.gateOn[1] = false;           // rest
    p.velocity[2] = 0.5f;
    p.pitch[3] = 12;
    seq.set (p, chromatic);

    const auto out = drive (seq, holdChord ({ 60, 64 }), 6000 * 4, 512, true);
    const auto on = ons (out);
    CHECK_EQ ((int) on.size(), 6);   // 2 notes x steps 0, 2, 3
    bool noteAtRest = false;
    for (const auto& e : on)
    {
        if (e.t >= 6000 && e.t < 12000) noteAtRest = true;
        if (e.t == 12000) CHECK_NEAR (e.e.value, 0.4, 1.0e-6);   // 0.8 x 0.5
        if (e.t == 18000) CHECK (e.e.note == 72 || e.e.note == 76);
    }
    CHECK (! noteAtRest);
    // The rest step still released step 0's notes (at its gate end, 4800).
    int offsBeforeRestEnd = 0;
    for (const auto& e : offs (out)) if (e.t <= 6000) ++offsBeforeRestEnd;
    CHECK_EQ (offsBeforeRestEnd, 2);
}

AP_TEST (keyaware_seq_mod_value_and_step)
{
    auto seq = makeSeq (false, { 0, 3, 7, 12 });
    CHECK_EQ (seq.currentStep(), -1);

    // 1/16 at 120 bpm = 6000 samples per step; blocks of 48 samples (1 ms).
    const int block = 48;
    std::vector<MidiEvent> outBuf (64);
    MidiEvent in = noteOn (60);
    Transport tr;
    tr.hasTempo = true;
    tr.playing = true;
    const double spb = 24000.0;
    float prev = 0.0f;
    float maxStepPerMs = 0.0f;
    for (int64_t start = 0; start < 6000 * 8; start += block)
    {
        tr.ppq = (double) start / spb;
        seq.process (start == 0 ? &in : nullptr, start == 0 ? 1 : 0, tr, block, outBuf.data(), 64);

        const int64_t inStep = (start + block) % 6000;   // samples since the current step began
        const int expectedStep = (int) ((start / 6000) % 4);
        CHECK_EQ (seq.currentStep(), expectedStep);
        const float target = 0.25f * (float) (expectedStep + 1);
        if (inStep >= 480)       // 10 ms (5 time constants) after the step: settled
            CHECK_NEAR (seq.modValue(), target, 0.01);
        if (inStep == 96 && start > 6000)   // 2 ms after: at least 60 % of the way there
        {
            const float prevTarget = 0.25f * (float) (((expectedStep + 3) % 4) + 1);
            CHECK (std::fabs (seq.modValue() - target) <= 0.4f * std::fabs (target - prevTarget) + 1.0e-4f);
        }
        maxStepPerMs = std::max (maxStepPerMs, std::fabs (seq.modValue() - prev));
        prev = seq.modValue();
    }
    // Smoothed: never the whole 0.75 jump (1.0 -> 0.25) inside one 1 ms block.
    CHECK (maxStepPerMs < 0.75f * 0.45f);
}

AP_TEST (keyaware_ppq_jump_relocks)
{
    // Host loops back from ppq 4 to ppq 0: the arp keeps to the grid (no burst, no stall).
    auto arp = makeArp (ArpParams::Up);
    std::vector<MidiEvent> outBuf (64);
    MidiEvent in = noteOn (60);
    Transport tr;
    tr.hasTempo = true;
    tr.playing = true;
    std::vector<double> onPpq;
    const int block = 500;
    const double spb = 24000.0;
    double ppq = 3.0;
    for (int b = 0; b < 200; ++b)
    {
        tr.ppq = ppq;
        const int m = arp.process (b == 0 ? &in : nullptr, b == 0 ? 1 : 0, tr, block, outBuf.data(), 64);
        for (int j = 0; j < m; ++j)
            if (outBuf[(size_t) j].type == MidiEvent::Type::NoteOn)
                onPpq.push_back (ppq + outBuf[(size_t) j].sampleOffset / spb);
        ppq += block / spb;
        if (ppq >= 4.0 - 1.0e-9) ppq -= 1.0;   // loop the last beat
    }
    // Every NoteOn sits on an eighth-note grid line; 200 blocks = 4.17 beats = 8 steps.
    CHECK (onPpq.size() >= 8 && onPpq.size() <= 9);
    for (double p : onPpq)
    {
        const double frac = p / 0.5 - std::round (p / 0.5);
        CHECK_NEAR (frac, 0.0, 1.0 / 12000.0 * 1.01);
    }
}
