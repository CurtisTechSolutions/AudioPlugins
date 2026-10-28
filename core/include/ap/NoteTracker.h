#pragma once

#include "Math.h"
#include "PitchTracker.h"
#include "Scales.h"

#include <cmath>
#include <cstdint>

namespace ap
{

/**
    Turns a stream of PitchTracker frames into note on/off/bend events.

    State machine:
      Idle   --(voiced & above gate for `onsetFrames`)-->  Playing (note on)
      Playing --(pitch moved > `retriggerCents` for `changeFrames`)--> Playing (off + on)
      Playing --(unvoiced/below gate for `releaseFrames`)--> Idle (note off)
*/
class NoteTracker
{
public:
    struct Params
    {
        float gateDb          = -45.0f;  // level gate
        float minConfidence   = 0.6f;    // YIN confidence needed for "voiced"
        int   onsetFrames     = 2;       // consecutive voiced frames before a note starts
        int   releaseFrames   = 4;       // consecutive silent frames before a note ends
        int   changeFrames    = 2;       // frames a new pitch must hold before retriggering
        float retriggerCents  = 60.0f;   // deviation that counts as a new note
        bool  pitchBend       = true;    // emit bend for deviation inside the retrigger zone
        float bendRangeSemis  = 2.0f;    // host pitch-bend range to scale against
        int   transpose       = 0;       // semitones added to the output note
        bool  snapToScale     = false;
        float snapStrength    = 1.0f;    // 0 = chromatic, 1 = always on the scale (see quantizeNote)
        Scale scale {};
        float velocityMinDb   = -40.0f;  // level mapped to velocity 1
        float velocityMaxDb   = -6.0f;   // level mapped to velocity 127
        bool  dynamicVelocity = true;
        int   fixedVelocity   = 100;
    };

    struct Event
    {
        enum class Type { NoteOn, NoteOff, PitchBend };
        Type  type     = Type::NoteOn;
        int   note     = 60;
        int   velocity = 100;
        float bend     = 0.0f;     // -1..1, scaled to bendRangeSemis
    };

    void reset() noexcept
    {
        playing = false;
        currentNote = -1;
        voicedRun = silentRun = changeRun = 0;
        candidate = -1;
        lastBend = 0.0f;
    }

    void setParams (const Params& p) noexcept { params = p; }
    const Params& getParams() const noexcept  { return params; }

    bool isPlaying() const noexcept   { return playing; }
    int  currentMidi() const noexcept { return currentNote; }

    /** Feed one frame; `emit(const Event&)` is called zero or more times. */
    template <typename Emit>
    void processFrame (const PitchTracker::Frame& f, Emit&& emit)
    {
        const bool active = f.voiced && f.confidence >= params.minConfidence && f.rmsDb >= params.gateDb;

        if (! active)
        {
            voicedRun = 0;
            changeRun = 0;
            if (playing && ++silentRun >= params.releaseFrames)
                noteOff (emit);
            return;
        }

        silentRun = 0;
        ++voicedRun;

        const float midiFloat = f.midi + (float) params.transpose;
        const int   nearest   = params.snapToScale ? quantizeNote (midiFloat, params.scale, params.snapStrength)
                                                   : (int) std::lround (midiFloat);

        if (! playing)
        {
            if (voicedRun >= params.onsetFrames)
                noteOn (nearest, f.rmsDb, emit);
            return;
        }

        // Playing: decide between bend and retrigger.
        const float deviationCents = 100.0f * (midiFloat - (float) currentNote);
        const bool  outside        = std::fabs (deviationCents) > params.retriggerCents;

        if (outside)
        {
            if (candidate == nearest) ++changeRun; else { candidate = nearest; changeRun = 1; }
            if (changeRun >= params.changeFrames && nearest != currentNote)
            {
                noteOff (emit);
                noteOn (nearest, f.rmsDb, emit);
                return;
            }
        }
        else
        {
            changeRun = 0;
            candidate = -1;
        }

        if (params.pitchBend && params.bendRangeSemis > 0.0f)
        {
            float bend = clamp ((midiFloat - (float) currentNote) / params.bendRangeSemis, -1.0f, 1.0f);
            if (std::fabs (bend - lastBend) > 0.002f)
            {
                lastBend = bend;
                Event e; e.type = Event::Type::PitchBend; e.note = currentNote; e.bend = bend;
                emit (e);
            }
        }
    }

    /** Force any sounding note off (e.g. on transport stop). */
    template <typename Emit>
    void flush (Emit&& emit)
    {
        if (playing) noteOff (emit);
        reset();
    }

private:
    template <typename Emit>
    void noteOn (int note, float levelDb, Emit&& emit)
    {
        note = clamp (note, 0, 127);
        int vel = params.fixedVelocity;
        if (params.dynamicVelocity)
        {
            const float t = (levelDb - params.velocityMinDb) / std::max (1.0f, params.velocityMaxDb - params.velocityMinDb);
            vel = (int) std::lround (1.0f + 126.0f * clamp (t, 0.0f, 1.0f));
        }
        if (params.pitchBend && std::fabs (lastBend) > 1.0e-6f)
        {
            lastBend = 0.0f;
            Event b; b.type = Event::Type::PitchBend; b.note = note; b.bend = 0.0f;
            emit (b);
        }
        Event e; e.type = Event::Type::NoteOn; e.note = note; e.velocity = clamp (vel, 1, 127);
        emit (e);
        playing = true;
        currentNote = note;
        changeRun = 0;
        candidate = -1;
    }

    template <typename Emit>
    void noteOff (Emit&& emit)
    {
        Event e; e.type = Event::Type::NoteOff; e.note = currentNote; e.velocity = 0;
        emit (e);
        playing = false;
        currentNote = -1;
        voicedRun = 0;
        changeRun = 0;
        candidate = -1;
    }

    Params params;
    bool  playing = false;
    int   currentNote = -1;
    int   voicedRun = 0, silentRun = 0, changeRun = 0;
    int   candidate = -1;
    float lastBend = 0.0f;
};

} // namespace ap
