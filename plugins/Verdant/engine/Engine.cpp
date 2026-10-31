#include "Engine.h"

#include "Patch.h"

#include <ap/Math.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace verdant
{

namespace
{
    using TableSet = std::array<std::shared_ptr<const Wavetable>, kNumFactoryTables>;

    // Factory tables are immutable and shared by every engine in the process (and with
    // anything else that asks factoryTable(), e.g. the UI's table views: one copy each).
    const TableSet& sharedFactoryTables()
    {
        static const TableSet tables = []
        {
            TableSet t;
            for (int i = 0; i < kNumFactoryTables; ++i)
                t[(size_t) i] = factoryTable (i);
            return t;
        }();
        return tables;
    }

    inline float pv (const ParamValues& v, P p) noexcept { return v[(size_t) p]; }

    ap::Scale scaleFrom (const ParamValues& v) noexcept
    {
        // key_scale lists the 14 named scales (Custom excluded).
        const int type = std::clamp ((int) std::lround (pv (v, P::KeyScale)), 0, (int) ap::ScaleType::WholeTone);
        return ap::Scale::fromType ((ap::ScaleType) type, (int) std::lround (pv (v, P::KeyRoot)));
    }

    ArpParams arpParamsFrom (const ParamValues& v) noexcept
    {
        ArpParams a;
        a.on       = pv (v, P::ArpOn) > 0.5f;
        a.mode     = (int) pv (v, P::ArpMode);
        a.octaves  = (int) pv (v, P::ArpOct);
        a.division = (ap::NoteDivision) (int) pv (v, P::ArpDiv);
        a.gate     = pv (v, P::ArpGate) * 0.01f;
        a.swing    = pv (v, P::ArpSwing) * 0.01f;
        a.snap     = pv (v, P::ArpSnap) > 0.5f;
        return a;
    }

    SeqParams seqParamsFrom (const ParamValues& v) noexcept
    {
        SeqParams s;
        s.on       = pv (v, P::SeqOn) > 0.5f;
        s.steps    = (int) pv (v, P::SeqSteps);
        s.division = (ap::NoteDivision) (int) pv (v, P::SeqDiv);
        s.gate     = pv (v, P::SeqGate) * 0.01f;
        s.swing    = pv (v, P::SeqSwing) * 0.01f;
        s.snap     = pv (v, P::SeqSnap) > 0.5f;
        for (int k = 0; k < kNumSeqSteps; ++k)
        {
            s.pitch[(size_t) k]    = (int) pv (v, seqStepParam (k, SeqStepParam::Pitch));
            s.velocity[(size_t) k] = pv (v, seqStepParam (k, SeqStepParam::Vel)) * 0.01f;
            s.gateOn[(size_t) k]   = pv (v, seqStepParam (k, SeqStepParam::Gate)) > 0.5f;
            s.mod[(size_t) k]      = pv (v, seqStepParam (k, SeqStepParam::Mod)) * 0.01f;
        }
        return s;
    }
}

//==============================================================================
void VisualState::writeScope (const float* L, const float* R, int n) noexcept
{
    uint32_t w = scopeWrite.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i, ++w)
        scope[w % (uint32_t) kScopeSize].store (R != nullptr ? 0.5f * (L[i] + R[i]) : L[i], std::memory_order_relaxed);
    scopeWrite.store (w, std::memory_order_release);
}

int VisualState::readScope (float* dest, int n) const noexcept
{
    n = std::clamp (n, 0, kScopeSize);
    const uint32_t start = scopeWrite.load (std::memory_order_acquire) - (uint32_t) n;
    for (int i = 0; i < n; ++i)
        dest[i] = scope[(start + (uint32_t) i) % (uint32_t) kScopeSize].load (std::memory_order_relaxed);
    return n;
}

//==============================================================================
Engine::Engine()
    : factoryTables (sharedFactoryTables())
{
    params = defaultValues();
    globalModulated = params;
    routing = routingFrom (params);
    scale = scaleFrom (params);
    arpOut.resize ((size_t) kMaxBlock);
    seqOut.resize ((size_t) kMaxBlock);
    chunkEvents.resize ((size_t) kMaxBlock);
    patternIn.resize ((size_t) kMaxBlock + 129);
    prepare (44100.0, 512);
}

Engine::~Engine() = default;

void Engine::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    maxBlockSize = std::clamp (maxBlock, 1, kMaxBlock);
    for (auto& v : voices)
        v.prepare (sr);
    for (auto& l : freeLfos)
        l.prepare (sr);
    arp.prepare (sr);
    seq.prepare (sr);
    sympathetic.prepare (sr, maxBlockSize);
    fx.prepare (sr, maxBlockSize);
    master.prepare (sr, 20.0f);
    arp.set (arpParamsFrom (params), scale);
    seq.set (seqParamsFrom (params), scale);
    reset();
}

void Engine::reset()
{
    for (auto& v : voices) v.kill();
    arp.reset();
    seq.reset();
    sympathetic.reset();
    fx.reset();
    bend = wheel = aftertouch = 0.0f;
    sustainDown = false;
    patternSustain = false;
    deferredOff.fill (false);
    numDeferred = 0;
    lastVoice = -1;
    noteOrder = 0;
    noteCounter = 0;
    numHeld = 0;
    haveLastNote = false;
    lastNotePitch = 60.0f;
    seqMod = 0.0f;
    offsets.fill (0.0f);
    globalOffsets.fill (0.0f);
    for (int l = 0; l < kNumLfos; ++l)
        freeLfos[(size_t) l].noteOn (0xF4EEu + (uint32_t) l);   // deterministic start (S&H seed, phase)
    master.snap (ap::dbToGain (pv (globalModulated, P::Master)));
    masterPrimed = false;   // the first block after a reset starts at its own master level (no ramp from a stale one)
}

void Engine::setParams (const ParamValues& v)
{
    const int oldMode = voiceMode();
    const int oldLimit = voiceLimit();
    if (routingParamsDiffer (params, v))
        routing = routingFrom (v);
    params = v;
    scale = scaleFrom (params);
    arp.set (arpParamsFrom (params), scale);
    seq.set (seqParamsFrom (params), scale);

    if (voiceMode() != oldMode)
    {
        // Switching between poly and mono / legato: let everything ring out and start clean.
        releaseAll();
        numHeld = 0;
    }
    else if (voiceMode() == 0 && voiceLimit() < oldLimit)
    {
        for (int i = voiceLimit(); i < kMaxVoices; ++i)
            voices[(size_t) i].fadeOut();
    }

    float release = 0.0f;
    for (int e = 0; e < kNumEnvs; ++e)
        release = std::max (release, pv (params, envParam (e, EnvParam::R)));
    tail.store (release * 0.001f + fx.tailSeconds() + 1.0f, std::memory_order_relaxed);
}

void Engine::setTransport (const Transport& t)
{
    transport = t;
}

void Engine::setUserTable (int osc, std::shared_ptr<const Wavetable> table)
{
    if (osc >= 0 && osc < 2)
        userTables[(size_t) osc].post (std::move (table));
}

void Engine::setLfoShape (int lfo, std::shared_ptr<const LfoShape> shape)
{
    if (lfo >= 0 && lfo < kNumLfos)
        lfoShapes[(size_t) lfo].post (std::move (shape));
}

void Engine::setLivingSeed (uint32_t seed)
{
    livingSeed.store (seed, std::memory_order_relaxed);
}

void Engine::releaseRetired()
{
    for (auto& t : userTables) t.drainRetired();
    for (auto& s : lfoShapes) s.drainRetired();
}

float Engine::tailSeconds() const noexcept
{
    return tail.load (std::memory_order_relaxed);
}

int Engine::voiceLimit() const noexcept
{
    return std::clamp ((int) std::lround (pv (params, P::Voices)), 1, kMaxVoices);
}

int Engine::voiceMode() const noexcept
{
    return std::clamp ((int) std::lround (pv (params, P::VoiceMode)), 0, 2);   // Poly, Mono, Legato
}

Transport Engine::transportAt (int sampleOffset) const noexcept
{
    Transport t = transport;
    t.sampleTime += sampleOffset;
    if (t.playing && t.hasTempo && t.bpm > 0.0)
        t.ppq += (double) sampleOffset / sr * t.bpm / 60.0;
    return t;
}

//==============================================================================
NoteStart Engine::makeNoteStart (int note, float velocity) noexcept
{
    NoteStart s;
    s.note = note;
    s.velocity = velocity;
    s.order = ++noteOrder;
    s.globalSeed = livingSeed.load (std::memory_order_relaxed);
    s.noteCounter = noteCounter++;
    s.glideMs = std::max (0.0f, pv (params, P::Glide));
    return s;
}

void Engine::stackPush (int note, float velocity) noexcept
{
    stackRemove (note);
    if (numHeld >= (int) heldNotes.size())
    {
        std::copy (heldNotes.begin() + 1, heldNotes.end(), heldNotes.begin());
        --numHeld;
    }
    heldNotes[(size_t) numHeld++] = HeldNote { note, velocity };
}

bool Engine::stackRemove (int note) noexcept
{
    for (int i = numHeld - 1; i >= 0; --i)
    {
        if (heldNotes[(size_t) i].note != note)
            continue;
        const bool wasTop = i == numHeld - 1;
        std::copy (heldNotes.begin() + i + 1, heldNotes.begin() + numHeld, heldNotes.begin() + i);
        --numHeld;
        return wasTop;
    }
    return false;
}

void Engine::noteOn (int note, float velocity) noexcept
{
    updateGlobalSources();   // wheel / bend / aftertouch events at this offset count for the note-on matrix pass
    NoteStart s = makeNoteStart (note, velocity);
    const int mode = voiceMode();
    int idx = 0;

    if (mode == 0)
    {
        // Poly: a free voice in 0..voices-1, else steal the oldest (released voices first).
        // Glide starts from the last note-on's pitch.
        s.glide = haveLastNote;
        s.glideFrom = lastNotePitch;

        const int limit = voiceLimit();
        idx = -1;
        for (int i = 0; i < limit && idx < 0; ++i)
            if (! voices[(size_t) i].isActive()) idx = i;

        if (idx >= 0)
        {
            voices[(size_t) idx].start (s, ctx);
        }
        else
        {
            uint64_t oldest = std::numeric_limits<uint64_t>::max();
            for (int pass = 0; pass < 2 && idx < 0; ++pass)
                for (int i = 0; i < limit; ++i)
                {
                    const auto& v = voices[(size_t) i];
                    if ((pass == 1 || v.isReleased()) && v.order() < oldest)
                    {
                        oldest = v.order();
                        idx = i;
                    }
                }
            if (idx < 0) idx = 0;
            voices[(size_t) idx].steal (s);   // 2 ms fade, then the new note
        }
    }
    else
    {
        // Mono / Legato: one voice, last-note priority. Mono retriggers the envelopes on every
        // note; Legato only when no other note is held. Glide runs from the sounding pitch.
        const bool othersHeld = numHeld > 0;
        stackPush (note, velocity);
        auto& v = voices[0];
        const bool sounding = v.isActive() && ! v.isFading();
        s.glide = sounding || haveLastNote;
        s.glideFrom = sounding ? v.currentPitch() : lastNotePitch;
        if (sounding)
            v.retarget (s, ctx, mode == 1 || ! othersHeld);
        else
            v.start (s, ctx);
    }

    lastVoice = idx;
    lastNotePitch = (float) note;
    haveLastNote = true;
    sympathetic.noteOn (note);
}

void Engine::noteOff (int note) noexcept
{
    if (voiceMode() != 0)
    {
        const bool wasHeld = numHeld > 0;
        const bool wasTop = stackRemove (note);
        auto& v = voices[0];
        if (wasHeld && numHeld == 0)
        {
            if (v.isActive() && ! v.isReleased())
            {
                if (sustainDown) v.sustained = true;
                else v.release();
            }
            return;
        }
        if (wasTop && numHeld > 0 && v.isActive())
        {
            // Return to the held note below: Mono retriggers, Legato just glides there.
            const auto& top = heldNotes[(size_t) numHeld - 1];
            NoteStart s = makeNoteStart (top.note, top.velocity);
            s.glide = true;
            s.glideFrom = v.currentPitch();
            v.retarget (s, ctx, voiceMode() == 1);
            lastNotePitch = (float) top.note;
            return;
        }
        if (wasHeld)
            return;
        // Not in the stack (e.g. a note from before a mode switch): fall through to the poly release.
    }

    for (auto& v : voices)
    {
        if (! v.isActive() || v.isReleased() || v.note() != note)
            continue;
        if (sustainDown) v.sustained = true;
        else v.release();
    }
}

void Engine::releaseAll() noexcept
{
    for (auto& v : voices)
        if (v.isActive())
        {
            v.sustained = false;
            v.release();
        }
}

void Engine::handleEvent (const MidiEvent& e) noexcept
{
    switch (e.type)
    {
        case MidiEvent::Type::NoteOn:
            if (e.value > 0.0f) noteOn (std::clamp (e.note, 0, 127), std::clamp (e.value, 0.0f, 1.0f));
            else noteOff (std::clamp (e.note, 0, 127));
            break;
        case MidiEvent::Type::NoteOff:        noteOff (std::clamp (e.note, 0, 127)); break;
        case MidiEvent::Type::PitchBend:      bend = std::isfinite (e.value) ? std::clamp (e.value, -1.0f, 1.0f) : 0.0f; break;
        case MidiEvent::Type::ModWheel:       wheel = std::isfinite (e.value) ? std::clamp (e.value, 0.0f, 1.0f) : 0.0f; break;
        case MidiEvent::Type::Aftertouch:     aftertouch = std::isfinite (e.value) ? std::clamp (e.value, 0.0f, 1.0f) : 0.0f; break;
        case MidiEvent::Type::PolyAftertouch:
            for (auto& v : voices)
                if (v.isActive() && ! v.isReleased() && v.note() == e.note)
                    v.setPolyAftertouch (std::isfinite (e.value) ? e.value : 0.0f);
            break;
        case MidiEvent::Type::Sustain:
        {
            const bool down = e.value >= 0.5f;
            if (sustainDown && ! down)
                for (auto& v : voices)
                    if (v.isActive() && v.sustained) { v.sustained = false; v.release(); }
            sustainDown = down;
            break;
        }
        case MidiEvent::Type::AllNotesOff:
            sustainDown = false;
            numHeld = 0;
            releaseAll();
            break;
        case MidiEvent::Type::Cc:
            break;
    }
}

//==============================================================================
void Engine::updateBlockContext() noexcept
{
    ctx.base = &params;
    ctx.routing = &routing;
    for (int o = 0; o < 2; ++o)
    {
        const int t = std::clamp ((int) std::lround (pv (params, oscParam (o, OscParam::Table))), 0, kNumFactoryTables);
        const Wavetable* table = t < kNumFactoryTables ? factoryTables[(size_t) t].get() : userTablePtr[(size_t) o];
        ctx.tables[(size_t) o] = table != nullptr ? table : factoryTables[0].get();   // "User" without a table -> table 0
    }
    ctx.lfoShapes = lfoShapePtr;
    ctx.scale = scale;
    ctx.pitchSnap = pv (params, P::PitchSnap) > 0.5f;
    for (int l = 0; l < kNumLfos; ++l)
        freeLfos[(size_t) l].setCustomShape (lfoShapePtr[(size_t) l]);
}

void Engine::updateGlobalModulation() noexcept
{
    // Global sources plus the most recently triggered voice's per-voice sources.
    ModSourceValues src;
    if (lastVoice >= 0 && voices[(size_t) lastVoice].isActive())
        src = voices[(size_t) lastVoice].lastSources();
    for (int m = 0; m < kNumMacros; ++m)
        src.v[(size_t) ModSource::Macro1 + (size_t) m] = pv (params, macroParam (m)) * 0.01f;
    src.v[(size_t) ModSource::ModWheel]   = wheel;
    src.v[(size_t) ModSource::PitchBend]  = bend;
    src.v[(size_t) ModSource::Aftertouch] = aftertouch;
    src.v[(size_t) ModSource::SeqMod]     = seqMod;
    for (int l = 0; l < kNumLfos; ++l)
        if ((int) std::lround (pv (params, lfoParam (l, LfoParam::Mode))) == (int) LfoMode::Free)
            src.v[(size_t) ModSource::Lfo1 + (size_t) l] = ctx.freeLfoValues[(size_t) l];

    ModMatrix::apply (params, routing, src, globalModulated, globalOffsets.data());

    // The shared LFOs run in Free mode with the (globally modulated) rate.
    for (int l = 0; l < kNumLfos; ++l)
    {
        LfoParams p;
        p.shape      = (LfoWave) std::clamp ((int) std::lround (pv (globalModulated, lfoParam (l, LfoParam::Shape))), 0, (int) LfoWave::Count - 1);
        p.rateHz     = pv (globalModulated, lfoParam (l, LfoParam::Rate));
        p.sync       = pv (globalModulated, lfoParam (l, LfoParam::Sync)) > 0.5f;
        p.division   = (ap::NoteDivision) std::clamp ((int) std::lround (pv (globalModulated, lfoParam (l, LfoParam::Div))), 0, (int) ap::NoteDivision::Count - 1);
        p.mode       = LfoMode::Free;
        p.startPhase = pv (globalModulated, lfoParam (l, LfoParam::Phase)) * 0.01f;
        p.fadeMs     = 0.0f;   // a free-running LFO has no note to fade in from
        p.smooth     = pv (globalModulated, lfoParam (l, LfoParam::Smooth)) * 0.01f;
        p.unipolar   = pv (globalModulated, lfoParam (l, LfoParam::Polarity)) > 0.5f;
        freeLfos[(size_t) l].set (p);
    }
}

void Engine::updateGlobalSources() noexcept
{
    auto& g = ctx.globalSources;
    for (int m = 0; m < kNumMacros; ++m)
        g.v[(size_t) ModSource::Macro1 + (size_t) m] = pv (params, macroParam (m)) * 0.01f;
    g.v[(size_t) ModSource::ModWheel]   = wheel;
    g.v[(size_t) ModSource::PitchBend]  = bend;
    g.v[(size_t) ModSource::Aftertouch] = aftertouch;
    g.v[(size_t) ModSource::SeqMod]     = seqMod;
    ctx.bendSemitones = bend * pv (params, P::BendRange);
}

void Engine::renderControlBlock (float* L, float* R, int numSamples, int blockOffset) noexcept
{
    ctx.transport = transportAt (blockOffset);

    for (int l = 0; l < kNumLfos; ++l)
    {
        auto& lfo = freeLfos[(size_t) l];
        lfo.syncToTransport (ctx.transport);
        ctx.freeLfoValues[(size_t) l] = lfo.next (numSamples);
    }

    // Seq Mod: the current step's value, stepped, smoothed by 2 ms.
    const float seqTarget = std::clamp (seq.modValue(), 0.0f, 1.0f);
    seqMod += (seqTarget - seqMod) * (1.0f - std::exp (-(float) numSamples / (0.002f * (float) sr)));

    updateGlobalSources();

    for (int i = 0; i < kMaxVoices; ++i)
    {
        auto& v = voices[(size_t) i];
        if (v.isActive())
            v.renderBlock (L, R, numSamples, ctx, i == lastVoice ? offsets.data() : nullptr);
    }
}

void Engine::process (float* L, float* R, int numSamples, const MidiEvent* events, int numEvents)
{
    if (numSamples <= 0 || L == nullptr || R == nullptr)
        return;
    if (events == nullptr)
        numEvents = 0;
    numEvents = std::max (0, numEvents);

    if (numSamples <= maxBlockSize)
    {
        processChunk (L, R, numSamples, events, numEvents);
        return;
    }

    // Longer than prepared: split, handing each chunk its events (offsets rebased).
    int ei = 0;
    for (int pos = 0; pos < numSamples; pos += maxBlockSize)
    {
        const int len = std::min (maxBlockSize, numSamples - pos);
        const bool lastChunk = pos + len >= numSamples;
        int count = 0;
        while (ei < numEvents && (lastChunk || events[ei].sampleOffset < pos + len) && count < (int) chunkEvents.size())
        {
            MidiEvent e = events[ei++];
            e.sampleOffset = std::clamp (e.sampleOffset - pos, 0, len - 1);
            chunkEvents[(size_t) count++] = e;
        }
        processChunk (L + pos, R + pos, len, chunkEvents.data(), count);
    }
}

int Engine::applyPatternSustain (const MidiEvent* events, int numEvents) noexcept
{
    // With the arp or sequencer on, the sustain pedal acts on their input: while it is down,
    // input note-offs are held back (the pattern keeps running on the held notes) and released
    // at the pedal-up offset. The pedal-down never reaches the voices, so the generated notes
    // keep their gate instead of piling up as sustained voices. Pedal-ups always pass (they
    // release anything sustained from before the pattern was switched on).
    const bool patternOn = pv (params, P::ArpOn) > 0.5f || pv (params, P::SeqOn) > 0.5f;
    const int capacity = (int) patternIn.size();
    int n = 0;
    auto emit = [&] (const MidiEvent& e) noexcept
    {
        if (n < capacity) patternIn[(size_t) n++] = e;
    };
    auto flushDeferred = [&] (int offset) noexcept
    {
        for (int note = 0; note < 128 && numDeferred > 0; ++note)
            if (deferredOff[(size_t) note])
            {
                deferredOff[(size_t) note] = false;
                --numDeferred;
                emit (MidiEvent { MidiEvent::Type::NoteOff, offset, 1, note, 0.0f, 0 });
            }
        numDeferred = 0;
    };

    if (! patternOn && (patternSustain || numDeferred > 0))
    {
        // The pattern was switched off with the pedal down: the pedal moves to the voices.
        if (patternSustain)
            emit (MidiEvent { MidiEvent::Type::Sustain, 0, 1, 0, 1.0f, 64 });
        patternSustain = false;
        flushDeferred (0);
    }

    for (int i = 0; i < numEvents; ++i)
    {
        const MidiEvent& e = events[i];
        if (! patternOn)
        {
            emit (e);
            continue;
        }
        const bool isOff = e.type == MidiEvent::Type::NoteOff || (e.type == MidiEvent::Type::NoteOn && e.value <= 0.0f);
        const int note = std::clamp (e.note, 0, 127);
        switch (e.type)
        {
            case MidiEvent::Type::Sustain:
                if (e.value >= 0.5f)
                {
                    patternSustain = true;
                }
                else
                {
                    patternSustain = false;
                    flushDeferred (e.sampleOffset);
                    emit (e);
                }
                break;
            case MidiEvent::Type::AllNotesOff:
                patternSustain = false;
                deferredOff.fill (false);
                numDeferred = 0;
                emit (e);
                break;
            default:
                if (isOff && patternSustain)
                {
                    if (! deferredOff[(size_t) note]) { deferredOff[(size_t) note] = true; ++numDeferred; }
                    break;
                }
                if (e.type == MidiEvent::Type::NoteOn && ! isOff && deferredOff[(size_t) note])
                {
                    deferredOff[(size_t) note] = false;   // struck again while held by the pedal
                    --numDeferred;
                }
                emit (e);
                break;
        }
    }
    return n;
}

void Engine::processChunk (float* L, float* R, int numSamples, const MidiEvent* events, int numEvents) noexcept
{
    std::fill (L, L + numSamples, 0.0f);
    std::fill (R, R + numSamples, 0.0f);

    for (int o = 0; o < 2; ++o) userTablePtr[(size_t) o] = userTables[(size_t) o].take();
    for (int l = 0; l < kNumLfos; ++l) lfoShapePtr[(size_t) l] = lfoShapes[(size_t) l].take();

    // MIDI -> (pattern sustain) -> Arp -> StepSeq (both pass through when off)
    const int numIn = applyPatternSustain (events, numEvents);
    const int numArp = arp.process (patternIn.data(), numIn, transport, numSamples, arpOut.data(), (int) arpOut.size());
    const int numSeq = seq.process (arpOut.data(), std::clamp (numArp, 0, (int) arpOut.size()), transport, numSamples,
                                    seqOut.data(), (int) seqOut.size());
    const int numOut = std::clamp (numSeq, 0, (int) seqOut.size());

    updateBlockContext();
    updateGlobalModulation();

    // Voices, in control blocks of <= 32 samples split at the event offsets.
    int ei = 0;
    for (int pos = 0; pos < numSamples;)
    {
        if (ei < numOut && seqOut[(size_t) ei].sampleOffset <= pos)
        {
            ctx.transport = transportAt (pos);
            while (ei < numOut && seqOut[(size_t) ei].sampleOffset <= pos)
                handleEvent (seqOut[(size_t) ei++]);
        }

        int len = std::min (kControlInterval, numSamples - pos);
        if (ei < numOut)
            len = std::min (len, std::max (1, seqOut[(size_t) ei].sampleOffset - pos));

        renderControlBlock (L + pos, R + pos, len, pos);
        pos += len;
    }
    while (ei < numOut)
        handleEvent (seqOut[(size_t) ei++]);

    // Global stages: Sympathetic body -> FX rack -> master.
    sympathetic.setParams (pv (globalModulated, P::SympAmount) * 0.01f, (int) std::lround (pv (params, P::SympMemory)),
                           pv (globalModulated, P::SympDecay) * 0.01f, pv (globalModulated, P::SympBright) * 0.01f,
                           pv (params, P::SympDamp) * 0.01f, (int) std::lround (pv (params, P::SympStrings)),
                           pv (params, P::SympSnap) > 0.5f, scale);
    sympathetic.process (L, R, numSamples);
    fx.setParams (globalModulated, transport);
    fx.process (L, R, numSamples);

    const float masterGain = ap::dbToGain (pv (globalModulated, P::Master));
    if (masterPrimed)
    {
        master.setTarget (masterGain);
    }
    else
    {
        master.snap (masterGain);
        masterPrimed = true;
    }
    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = master.next();
        float l = L[i] * g, r = R[i] * g;
        if (! std::isfinite (l) || std::fabs (l) < 1.0e-30f) l = 0.0f;   // finite, denormal-free output
        if (! std::isfinite (r) || std::fabs (r) < 1.0e-30f) r = 0.0f;
        L[i] = l;
        R[i] = r;
        peak = std::max (peak, std::max (std::fabs (l), std::fabs (r)));
    }

    // Transport moves on (the processor normally overwrites it every block).
    transport.sampleTime += numSamples;
    if (transport.playing && transport.hasTempo && transport.bpm > 0.0)
        transport.ppq += (double) numSamples / sr * transport.bpm / 60.0;

    // Visual state (relaxed atomics).
    int active = 0;
    for (const auto& v : voices) active += v.isActive() ? 1 : 0;
    vis.activeVoices.store (active, std::memory_order_relaxed);
    vis.outputPeak.store (peak, std::memory_order_relaxed);
    vis.seqStep.store (seq.currentStep(), std::memory_order_relaxed);
    if (lastVoice >= 0 && voices[(size_t) lastVoice].isActive())
    {
        const auto& v = voices[(size_t) lastVoice];
        for (int e = 0; e < kNumEnvs; ++e) vis.envValue[e].store (v.envValue (e), std::memory_order_relaxed);
        for (int l = 0; l < kNumLfos; ++l) vis.lfoValue[l].store (v.lfoValue (l), std::memory_order_relaxed);
        for (int o = 0; o < 2; ++o) vis.oscPosition[o].store (v.oscPosition (o), std::memory_order_relaxed);
        for (int p = 0; p < 4; ++p) vis.physics[p].store (v.physicsValue (p), std::memory_order_relaxed);
        for (int d = 0; d <= kNumModDestinations; ++d) vis.modOffset[d].store (offsets[(size_t) d], std::memory_order_relaxed);
    }
    else
    {
        // No note: show the global offsets (macros, wheel, ...) and the free LFOs.
        for (int e = 0; e < kNumEnvs; ++e) vis.envValue[e].store (0.0f, std::memory_order_relaxed);
        for (int l = 0; l < kNumLfos; ++l) vis.lfoValue[l].store (ctx.freeLfoValues[(size_t) l], std::memory_order_relaxed);
        for (int d = 0; d <= kNumModDestinations; ++d) vis.modOffset[d].store (globalOffsets[(size_t) d], std::memory_order_relaxed);
    }
    vis.writeScope (L, R, numSamples);
}

//==============================================================================
std::vector<float> renderPreview (const Patch& patch, int midiNote, float noteSeconds, float totalSeconds, double sampleRate)
{
    const int total = std::max (0, (int) std::lround ((double) totalSeconds * sampleRate));
    std::vector<float> out ((size_t) total, 0.0f);
    if (total == 0)
        return out;

    auto engine = std::make_unique<Engine>();
    constexpr int block = 512;
    engine->prepare (sampleRate, block);
    engine->setParams (patch.values);
    for (int o = 0; o < 2; ++o)
    {
        const auto& ut = patch.userTables[(size_t) o];
        if (ut.numFrames > 0 && ut.frames.size() >= (size_t) ut.numFrames * (size_t) kFrameSize)
            engine->setUserTable (o, Wavetable::fromFrames (ut.frames.data(), ut.numFrames, ut.name));
    }
    for (int l = 0; l < kNumLfos; ++l)
        engine->setLfoShape (l, std::make_shared<LfoShape> (patch.lfoShapes[(size_t) l]));
    engine->setLivingSeed (patch.livingSeed);

    const int noteOffAt = std::clamp ((int) std::lround ((double) noteSeconds * sampleRate), 0, total);
    std::vector<float> L ((size_t) block), R ((size_t) block);
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        MidiEvent events[2];
        int count = 0;
        if (pos == 0)
            events[count++] = MidiEvent { MidiEvent::Type::NoteOn, 0, 1, midiNote, 100.0f / 127.0f, 0 };
        if (noteOffAt >= pos && noteOffAt < pos + n)
            events[count++] = MidiEvent { MidiEvent::Type::NoteOff, noteOffAt - pos, 1, midiNote, 0.0f, 0 };

        engine->process (L.data(), R.data(), n, events, count);
        for (int i = 0; i < n; ++i)
            out[(size_t) (pos + i)] = 0.5f * (L[(size_t) i] + R[(size_t) i]);
    }
    return out;
}

} // namespace verdant
