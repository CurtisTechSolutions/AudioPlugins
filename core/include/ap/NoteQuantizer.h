#pragma once

#include "Math.h"
#include "Tempo.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ap
{

/**
    A note event with absolute timing, as produced by a note tracker and
    consumed by the rhythm quantiser and the take capture.
*/
struct TimedNoteEvent
{
    enum class Type { NoteOn, NoteOff, PitchBend };
    Type    type     = Type::NoteOn;
    int     note     = 60;
    int     velocity = 100;
    float   bend     = 0.0f;       // -1..1 for PitchBend
    int64_t time     = 0;          // absolute sample index
};

/** Where a block sits on the host's beat grid: beat = startBeat + (t - startSample) * beatsPerSample. */
struct BeatClock
{
    int64_t startSample   = 0;
    double  startBeat     = 0.0;
    double  beatsPerSample = 120.0 / (60.0 * 48000.0);

    double beatAt (int64_t sample) const noexcept
    {
        return startBeat + (double) (sample - startSample) * beatsPerSample;
    }

    int64_t sampleAt (double beat) const noexcept
    {
        return startSample + (int64_t) std::llround ((beat - startBeat) / beatsPerSample);
    }
};

/**
    Live rhythmic quantiser for a monophonic note stream.

    A note-on is moved to the nearest grid line: if that line is already past
    the note starts now ("late snap"), otherwise it waits until the line. A
    pending note-on is cancelled by a note-off that arrives before it is due
    (the note was shorter than the wait) and replaced by a newer note-on.
    Note-offs and bends pass straight through. With the grid off everything
    passes through unchanged.

    Allocation-free; call advance() as the sample clock moves so deferred
    events are released at the right sample.
*/
class RhythmQuantizer
{
public:
    /** `beats` = grid size in quarter notes, 0 = off. */
    void setGrid (double beats) noexcept { gridBeats = beats > 0.0 ? beats : 0.0; }
    double grid() const noexcept         { return gridBeats; }

    void reset() noexcept { pending = false; sounding = false; soundingNote = -1; }

    bool hasPending() const noexcept  { return pending; }
    bool isSounding() const noexcept  { return sounding; }

    /** Push one event; `emit(const TimedNoteEvent&)` is called for anything released now. */
    template <typename Emit>
    void push (const TimedNoteEvent& e, const BeatClock& clock, Emit&& emit)
    {
        switch (e.type)
        {
            case TimedNoteEvent::Type::NoteOn:
            {
                if (gridBeats <= 0.0)
                {
                    pending = false;
                    forward (e, emit);
                    return;
                }
                const double beat    = clock.beatAt (e.time);
                const double nearest = std::round (beat / gridBeats) * gridBeats;
                if (nearest <= beat + 1.0e-9)
                {
                    pending = false;
                    forward (e, emit);          // the line has passed: late snap, start now
                    return;
                }
                pendingEvent = e;
                pendingEvent.time = clock.sampleAt (nearest);
                pending = true;
                return;
            }

            case TimedNoteEvent::Type::NoteOff:
                if (pending && pendingEvent.note == e.note)
                {
                    pending = false;            // never started: drop the pair
                    return;
                }
                forward (e, emit);
                return;

            case TimedNoteEvent::Type::PitchBend:
                if (pending) return;            // bends for a note that has not started yet are dropped
                forward (e, emit);
                return;
        }
    }

    /** Releases a pending note-on once `now` reaches its due time. */
    template <typename Emit>
    void advance (int64_t now, Emit&& emit)
    {
        if (pending && now >= pendingEvent.time)
        {
            pending = false;
            TimedNoteEvent e = pendingEvent;
            e.time = now;
            forward (e, emit);
        }
    }

    /** Force everything off (transport stop / reset). Emits a note-off if a note is sounding. */
    template <typename Emit>
    void flush (int64_t now, Emit&& emit)
    {
        pending = false;
        if (sounding)
        {
            TimedNoteEvent off;
            off.type = TimedNoteEvent::Type::NoteOff;
            off.note = soundingNote;
            off.velocity = 0;
            off.time = now;
            forward (off, emit);
        }
    }

private:
    template <typename Emit>
    void forward (const TimedNoteEvent& e, Emit&& emit)
    {
        if (e.type == TimedNoteEvent::Type::NoteOn)
        {
            if (sounding && soundingNote != e.note)
            {
                TimedNoteEvent off;            // keep the output stream monophonic
                off.type = TimedNoteEvent::Type::NoteOff;
                off.note = soundingNote;
                off.velocity = 0;
                off.time = e.time;
                emit (off);
            }
            sounding = true;
            soundingNote = e.note;
        }
        else if (e.type == TimedNoteEvent::Type::NoteOff)
        {
            if (! sounding || soundingNote != e.note) return;
            sounding = false;
            soundingNote = -1;
        }
        emit (e);
    }

    double gridBeats = 0.0;
    bool   pending = false;
    TimedNoteEvent pendingEvent;
    bool   sounding = false;
    int    soundingNote = -1;
};

/** One captured note of a take. */
struct CapturedNote
{
    int     note        = 60;
    int     velocity    = 100;
    int64_t startSample = 0;
    int64_t endSample   = 0;
    double  startBeat   = 0.0;
    double  endBeat     = 0.0;
};

/**
    Records the notes of a take. Preallocated so the audio thread can append
    without allocating; readers take a snapshot guarded by the generation
    counter (even = stable, odd = being written).
*/
class NoteCapture
{
public:
    explicit NoteCapture (int capacity = 4096) { notes.resize ((size_t) std::max (1, capacity)); }

    void setMinimumLength (int64_t samples) noexcept { minLength = std::max<int64_t> (0, samples); }

    void clear() noexcept
    {
        beginWrite();
        count = 0;
        openNote = -1;
        endWrite();
    }

    int  size() const noexcept     { return count.load (std::memory_order_acquire); }
    int  capacity() const noexcept { return (int) notes.size(); }
    bool isNoteOpen() const noexcept { return openNote >= 0; }
    int  openNoteNumber() const noexcept { return openNote; }

    /** Feed note on/off with their *corrected* time (already back-dated as needed). */
    void noteOn (int note, int velocity, int64_t sample, double beat) noexcept
    {
        if (openNote >= 0) noteOff (openNote, sample, beat);
        openNote = note;
        openVelocity = velocity;
        openStart = sample;
        openStartBeat = beat;
    }

    void noteOff (int note, int64_t sample, double beat) noexcept
    {
        if (openNote < 0 || note != openNote) return;
        const int64_t length = sample - openStart;
        if (length >= minLength && length > 0)
        {
            const int n = count.load (std::memory_order_relaxed);
            if (n < (int) notes.size())
            {
                beginWrite();
                auto& c = notes[(size_t) n];
                c.note = openNote;
                c.velocity = openVelocity;
                c.startSample = openStart;
                c.endSample = sample;
                c.startBeat = openStartBeat;
                c.endBeat = beat;
                count.store (n + 1, std::memory_order_release);
                endWrite();
            }
        }
        openNote = -1;
    }

    /** Copies a consistent snapshot of the take into `out` (resizes it; not for the audio thread). */
    void snapshot (std::vector<CapturedNote>& out) const
    {
        for (int attempt = 0; attempt < 64; ++attempt)
        {
            const uint32_t g0 = generation.load (std::memory_order_acquire);
            if (g0 & 1u) continue;
            const int n = count.load (std::memory_order_acquire);
            out.assign (notes.begin(), notes.begin() + n);
            const uint32_t g1 = generation.load (std::memory_order_acquire);
            if (g0 == g1) return;
        }
        out.clear();
    }

    uint32_t currentGeneration() const noexcept { return generation.load (std::memory_order_acquire); }

private:
    void beginWrite() noexcept { generation.fetch_add (1, std::memory_order_acq_rel); }
    void endWrite() noexcept   { generation.fetch_add (1, std::memory_order_acq_rel); }

    std::vector<CapturedNote> notes;
    std::atomic<int>      count { 0 };
    std::atomic<uint32_t> generation { 0 };
    int64_t minLength = 0;
    int     openNote = -1;
    int     openVelocity = 100;
    int64_t openStart = 0;
    double  openStartBeat = 0.0;
};

/**
    Offline quantise of a captured take (for export): starts move to the
    nearest grid line in either direction, lengths to the nearest multiple of
    the grid (at least one), and overlaps are trimmed so the result stays
    monophonic. Works on beats only. `gridBeats` <= 0 leaves timing alone but
    still trims overlaps.
*/
inline void quantizeTake (std::vector<CapturedNote>& take, double gridBeats)
{
    if (take.empty()) return;

    if (gridBeats > 0.0)
    {
        for (auto& n : take)
        {
            const double len = std::max (gridBeats, std::round ((n.endBeat - n.startBeat) / gridBeats) * gridBeats);
            n.startBeat = std::round (n.startBeat / gridBeats) * gridBeats;
            n.endBeat = n.startBeat + len;
        }
    }

    std::stable_sort (take.begin(), take.end(), [] (const CapturedNote& a, const CapturedNote& b)
    {
        return a.startBeat < b.startBeat;
    });

    for (size_t i = 0; i + 1 < take.size(); ++i)
        if (take[i].endBeat > take[i + 1].startBeat)
            take[i].endBeat = take[i + 1].startBeat;

    take.erase (std::remove_if (take.begin(), take.end(), [] (const CapturedNote& n) { return n.endBeat <= n.startBeat; }),
                take.end());
}

} // namespace ap
