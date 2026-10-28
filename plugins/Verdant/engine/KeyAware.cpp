#include "KeyAware.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace verdant
{

int snapToScale (int midiNote, const ap::Scale& scale)
{
    return scale.nearest ((float) midiNote);
}

int scaleDegreeOffset (int midiNote, int degrees, const ap::Scale& scale)
{
    int n = snapToScale (midiNote, scale);
    const int dir = degrees < 0 ? -1 : 1;
    for (int d = 0; d < std::abs (degrees); ++d)
    {
        int next = n + dir;
        for (int guard = 0; guard < 12 && ! scale.contains (next); ++guard)
            next += dir;
        n = next;
    }
    return n;
}

//==============================================================================
namespace detail
{
    void StepCore::clear() noexcept
    {
        numHeld = 0;
        numSounding = 0;
        running = false;
        wasSynced = false;
        blockStart = 0;
        counter = 0;
        lastGridStep = -1;
        freeBase = 0.0;
        lastFireBase = 0.0;
    }

    void StepCore::emit (const MidiEvent& e) noexcept
    {
        if (out != nullptr && numOut < maxOut)
            out[numOut++] = e;
    }

    void StepCore::noteOff (int note, int channel, int offset) noexcept
    {
        MidiEvent e;
        e.type = MidiEvent::Type::NoteOff;
        e.sampleOffset = offset;
        e.channel = channel;
        e.note = note;
        e.value = 0.0f;
        emit (e);
    }

    void StepCore::releaseAll (int offset) noexcept
    {
        for (int i = 0; i < numSounding; ++i)
            noteOff (sounding[(size_t) i].note, sounding[(size_t) i].channel, offset);
        numSounding = 0;
    }

    void StepCore::releaseSource (int source, int offset) noexcept
    {
        int w = 0;
        for (int i = 0; i < numSounding; ++i)
        {
            const auto& s = sounding[(size_t) i];
            if (s.source == source)
                noteOff (s.note, s.channel, offset);
            else
                sounding[(size_t) w++] = s;
        }
        numSounding = w;
    }

    void StepCore::startNote (int note, int channel, float velocity, int source, int offset, int64_t offTime) noexcept
    {
        // Two held notes can land on the same output note (snap, octave overlap):
        // sound it once, so its single NoteOff really ends it.
        for (int i = 0; i < numSounding; ++i)
            if (sounding[(size_t) i].note == note && sounding[(size_t) i].channel == channel)
                return;
        if (numSounding >= kMaxSounding || out == nullptr || numOut >= maxOut)
            return;   // never start a note whose NoteOff could not be tracked

        MidiEvent e;
        e.type = MidiEvent::Type::NoteOn;
        e.sampleOffset = offset;
        e.channel = channel;
        e.note = note;
        e.value = velocity;
        emit (e);
        sounding[(size_t) numSounding++] = { note, channel, source, offTime };
    }

    int StepCore::findHeld (int note) const noexcept
    {
        for (int i = 0; i < numHeld; ++i)
            if (held[(size_t) i].note == note)
                return i;
        return -1;
    }
}

namespace
{
    constexpr int64_t kNever = std::numeric_limits<int64_t>::max();

    /** Runs one block: merges the input events, gate-end NoteOffs and step
        boundaries in time order (at equal times: NoteOffs, then input, then the
        step), calling fire (gridStep, offset, stepSamples) on each step boundary
        and advance (numSamples) for the time between events. */
    template <class Fire, class Advance>
    int runBlock (detail::StepCore& c, bool on, const MidiEvent* in, int numIn, const Transport& t, int numSamples,
                  MidiEvent* out, int maxOut, ap::NoteDivision division, float swing01, Fire&& fire, Advance&& advance) noexcept
    {
        c.out = out;
        c.maxOut = out != nullptr ? std::max (0, maxOut) : 0;
        c.numOut = 0;
        if (in == nullptr) numIn = 0;
        numSamples = std::max (0, numSamples);

        if (! on)
        {
            // Bypassed: release anything we started, forget held notes, pass through.
            if (c.numSounding > 0) c.releaseAll (0);
            c.numHeld = 0;
            c.running = false;
            for (int i = 0; i < numIn; ++i)
                c.emit (in[i]);
            advance (numSamples);
            c.blockStart += numSamples;
            return c.numOut;
        }

        const bool synced = t.playing && t.hasTempo && t.bpm > 0.0 && std::isfinite (t.ppq);
        const double bpm = ap::clamp ((t.hasTempo && t.bpm > 0.0) ? t.bpm : 120.0, 1.0, 1000.0);
        const double spb = 60.0 / bpm * c.sr;                       // samples per beat
        const double beats = ap::noteDivisionBeats (division);
        const double stepSamples = std::max (1.0, beats * spb);
        const double swing = (double) ap::clamp (swing01, 0.0f, 1.0f);
        const double swingSamples = swing * stepSamples / 3.0;
        const double ppq0 = t.ppq;

        // Synced: relative sample position of absolute grid step k in this block.
        auto gridFire = [&] (int64_t k) noexcept
        {
            const double p = (double) k * beats + ((k & 1) != 0 ? swing * beats / 3.0 : 0.0);
            return (p - ppq0) * spb;
        };
        auto firstGridAtOrAfter = [&] (double rel) noexcept
        {
            auto k = (int64_t) std::floor ((ppq0 + rel / spb) / beats) - 1;
            while ((double) std::llround (gridFire (k)) < rel)
                ++k;
            return k;
        };

        if (c.running)
        {
            if (synced)
            {
                // The next step is the first grid step at or after the block start
                // (2 samples of rounding slack). Anything else means the transport
                // jumped (loop, relocate) or we just switched to sync: re-lock.
                const int64_t kFirst = firstGridAtOrAfter (-2.0);
                if (! c.wasSynced || c.lastGridStep + 1 < kFirst || c.lastGridStep > kFirst)
                    c.lastGridStep = kFirst - 1;
            }
            else if (c.wasSynced)
            {
                // Transport stopped: keep stepping one step after the last one.
                c.freeBase = std::max ((double) c.blockStart, c.lastFireBase + stepSamples);
            }
        }
        c.wasSynced = synced;

        auto nextStepRel = [&]() noexcept -> int64_t
        {
            if (synced)
                return std::llround (gridFire (c.lastGridStep + 1));
            return std::llround (c.freeBase + ((c.counter & 1) != 0 ? swingSamples : 0.0)) - c.blockStart;
        };

        auto startClock = [&] (int offset) noexcept
        {
            c.running = true;
            c.counter = 0;
            c.freeBase = (double) (c.blockStart + offset);
            if (synced)
                c.lastGridStep = firstGridAtOrAfter ((double) offset) - 1;
        };

        auto handleInput = [&] (const MidiEvent& e, int offset) noexcept
        {
            using T = MidiEvent::Type;
            const bool isOn  = e.type == T::NoteOn && e.value > 0.0f;
            const bool isOff = e.type == T::NoteOff || (e.type == T::NoteOn && e.value <= 0.0f);

            if (isOn)
            {
                const int idx = c.findHeld (e.note);
                if (idx >= 0)
                {
                    c.held[(size_t) idx].velocity = e.value;
                    c.held[(size_t) idx].channel = e.channel;
                }
                else if (c.numHeld < detail::StepCore::kMaxHeld)
                {
                    c.held[(size_t) c.numHeld++] = { e.note, e.channel, e.value };
                }
                if (! c.running)
                    startClock (offset);
            }
            else if (isOff)
            {
                const int idx = c.findHeld (e.note);
                if (idx < 0)
                {
                    // Not ours (pressed while bypassed): let the voice release.
                    MidiEvent pass = e;
                    pass.sampleOffset = offset;
                    c.emit (pass);
                    return;
                }
                for (int i = idx; i + 1 < c.numHeld; ++i)
                    c.held[(size_t) i] = c.held[(size_t) i + 1];
                --c.numHeld;
                c.releaseSource (e.note, offset);
                if (c.numHeld == 0)
                {
                    c.releaseAll (offset);
                    c.running = false;
                }
            }
            else
            {
                if (e.type == T::AllNotesOff)
                {
                    c.releaseAll (offset);
                    c.numHeld = 0;
                    c.running = false;
                }
                MidiEvent pass = e;
                pass.sampleOffset = offset;
                c.emit (pass);
            }
        };

        const int lastSample = std::max (0, numSamples - 1);
        int pos = 0, i = 0, lastIn = 0;

        for (;;)
        {
            int64_t tIn = kNever, tStep = kNever, tOff = kNever;

            if (i < numIn)
                tIn = std::clamp (in[i].sampleOffset, std::max (lastIn, pos), std::max (lastSample, pos));

            if (c.running)
            {
                tStep = std::max (nextStepRel(), (int64_t) pos);
                if (tStep >= numSamples) tStep = kNever;
            }

            if (c.numSounding > 0)
            {
                int64_t earliest = kNever;
                for (int s = 0; s < c.numSounding; ++s)
                    earliest = std::min (earliest, c.sounding[(size_t) s].offTime);
                tOff = std::max (earliest - c.blockStart, (int64_t) pos);
                if (tOff >= numSamples) tOff = kNever;
            }

            const int64_t tNext = std::min ({ tIn, tStep, tOff });
            if (tNext == kNever)
                break;

            const int now = (int) tNext;
            if (now > pos)
            {
                advance (now - pos);
                pos = now;
            }

            if (tOff == tNext)
            {
                int w = 0;
                for (int s = 0; s < c.numSounding; ++s)
                {
                    const auto& snd = c.sounding[(size_t) s];
                    if (snd.offTime - c.blockStart <= now)
                        c.noteOff (snd.note, snd.channel, now);
                    else
                        c.sounding[(size_t) w++] = snd;
                }
                c.numSounding = w;
                continue;
            }

            if (tIn == tNext)
            {
                handleInput (in[i], now);
                lastIn = now;
                ++i;
                continue;
            }

            // Step boundary: end the previous step's notes, then fire.
            c.releaseAll (now);
            const int64_t grid = synced ? c.lastGridStep + 1 : c.counter;
            const double base = synced ? (double) c.blockStart + ((double) grid * beats - ppq0) * spb : c.freeBase;
            fire (grid, now, stepSamples);
            c.lastFireBase = base;
            if (synced)
                c.lastGridStep = grid;
            else
                c.freeBase += stepSamples;
            ++c.counter;
        }

        if (numSamples > pos)
            advance (numSamples - pos);
        c.blockStart += numSamples;
        return c.numOut;
    }

    inline int64_t gateOffTime (const detail::StepCore& c, int offset, float gate01, double stepSamples) noexcept
    {
        const double gate = (double) ap::clamp (gate01, 0.0f, 1.0f);
        return c.blockStart + offset + std::max<int64_t> (1, std::llround (gate * stepSamples));
    }

    inline int clampNote (int n) noexcept { return std::clamp (n, 0, 127); }
}

//==============================================================================
void Arpeggiator::prepare (double sampleRate)
{
    core.sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    reset();
}

void Arpeggiator::set (const ArpParams& p, const ap::Scale& s)
{
    params = p;
    scale = s;
}

void Arpeggiator::reset()
{
    core.clear();
    rng.state = 0xA4B9E7C3u;   // fixed: the Random mode is reproducible after reset
    lastRandomIndex = -1;
}

void Arpeggiator::fireStep (int64_t, int offset, double stepSamples) noexcept
{
    const int numHeld = core.numHeld;
    if (numHeld <= 0)
        return;

    // Held notes sorted by pitch (insertion sort of indices; at most 128).
    std::array<int, detail::StepCore::kMaxHeld> sorted {};
    for (int i = 0; i < numHeld; ++i)
    {
        int j = i;
        while (j > 0 && core.held[(size_t) sorted[(size_t) j - 1]].note > core.held[(size_t) i].note)
        {
            sorted[(size_t) j] = sorted[(size_t) j - 1];
            --j;
        }
        sorted[(size_t) j] = i;
    }

    const int octaves = std::clamp (params.octaves, 1, 4);
    const int mode = std::clamp (params.mode, 0, (int) ArpParams::NumModes - 1);
    const int64_t offTime = gateOffTime (core, offset, params.gate, stepSamples);
    const int64_t pos = core.counter;

    auto play = [&] (int heldIndex, int octave) noexcept
    {
        const auto& h = core.held[(size_t) heldIndex];
        int n = clampNote (h.note + 12 * octave);
        if (params.snap)
            n = clampNote (snapToScale (n, scale));
        core.startNote (n, h.channel, h.velocity, h.note, offset, offTime);
    };

    if (mode == ArpParams::Chord)
    {
        const int octave = (int) (pos % octaves);
        for (int i = 0; i < numHeld; ++i)
            play (sorted[(size_t) i], octave);
        return;
    }

    const int len = numHeld * octaves;
    int idx = 0;
    switch (mode)
    {
        case ArpParams::Up:
        case ArpParams::AsPlayed:
            idx = (int) (pos % len);
            break;
        case ArpParams::Down:
            idx = len - 1 - (int) (pos % len);
            break;
        case ArpParams::UpDown:
        case ArpParams::DownUp:
        {
            // Bounce without repeating the end points: 0 1 2 1 | 0 1 2 1 ...
            if (len > 1)
            {
                const int period = 2 * len - 2;
                const int p = (int) (pos % period);
                idx = p < len ? p : period - p;
            }
            if (mode == ArpParams::DownUp)
                idx = len - 1 - idx;
            break;
        }
        case ArpParams::Random:
        {
            if (len > 1)
            {
                if (lastRandomIndex >= 0 && lastRandomIndex < len)
                {
                    // Draw from the other len - 1 indices: never an immediate repeat.
                    idx = (int) (rng.nextU32() % (uint32_t) (len - 1));
                    if (idx >= lastRandomIndex) ++idx;
                }
                else
                {
                    idx = (int) (rng.nextU32() % (uint32_t) len);
                }
            }
            lastRandomIndex = idx;
            break;
        }
        default:
            break;
    }

    const int noteIndex = idx % numHeld;
    const int octave = idx / numHeld;
    play (mode == ArpParams::AsPlayed ? noteIndex : sorted[(size_t) noteIndex], octave);
}

int Arpeggiator::process (const MidiEvent* in, int numIn, const Transport& t, int numSamples, MidiEvent* out, int maxOut) noexcept
{
    return runBlock (core, params.on, in, numIn, t, numSamples, out, maxOut, params.division, params.swing,
                     [this] (int64_t grid, int offset, double stepSamples) noexcept { fireStep (grid, offset, stepSamples); },
                     [] (int) noexcept {});
}

//==============================================================================
void StepSequencer::prepare (double sampleRate)
{
    core.sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    reset();
}

void StepSequencer::set (const SeqParams& p, const ap::Scale& s)
{
    params = p;
    scale = s;
}

void StepSequencer::reset()
{
    core.clear();
    step = -1;
    modTarget = modSmoothed = 0.0f;
}

void StepSequencer::advanceMod (int numSamples) noexcept
{
    if (numSamples <= 0)
        return;
    // 2 ms one-pole, advanced exactly over numSamples.
    const float a = (float) std::exp (-(double) numSamples / (0.002 * core.sr));
    modSmoothed = modTarget + (modSmoothed - modTarget) * a;
}

void StepSequencer::fireStep (int64_t grid, int offset, double stepSamples) noexcept
{
    const int numSteps = std::clamp (params.steps, 1, 16);
    const int k = (int) (((grid % numSteps) + numSteps) % numSteps);
    step = k;
    modTarget = ap::clamp (params.mod[(size_t) k], 0.0f, 1.0f);

    const float stepVel = ap::clamp (params.velocity[(size_t) k], 0.0f, 1.0f);
    if (! params.gateOn[(size_t) k] || stepVel <= 0.0f)
        return;   // rest: the previous step's notes were released, nothing new starts

    const int64_t offTime = gateOffTime (core, offset, params.gate, stepSamples);
    const int pitch = params.pitch[(size_t) k];
    for (int i = 0; i < core.numHeld; ++i)
    {
        const auto& h = core.held[(size_t) i];
        const int n = clampNote (params.snap ? scaleDegreeOffset (h.note, pitch, scale) : h.note + pitch);
        const float vel = ap::clamp (h.velocity * stepVel, 0.0f, 1.0f);
        if (vel > 0.0f)
            core.startNote (n, h.channel, vel, h.note, offset, offTime);
    }
}

int StepSequencer::process (const MidiEvent* in, int numIn, const Transport& t, int numSamples, MidiEvent* out, int maxOut) noexcept
{
    if (! params.on)
        modTarget = 0.0f;

    const int n = runBlock (core, params.on, in, numIn, t, numSamples, out, maxOut, params.division, params.swing,
                            [this] (int64_t grid, int offset, double stepSamples) noexcept { fireStep (grid, offset, stepSamples); },
                            [this] (int samples) noexcept { advanceMod (samples); });
    if (! core.running)
        step = -1;
    return n;
}

float StepSequencer::modValue() const noexcept { return modSmoothed; }
int StepSequencer::currentStep() const noexcept { return step; }

} // namespace verdant
