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

    // Factory tables are immutable and shared by every engine in the process.
    const TableSet& sharedFactoryTables()
    {
        static const TableSet tables = []
        {
            TableSet t;
            for (int i = 0; i < kNumFactoryTables; ++i)
                t[(size_t) i] = makeFactoryTable (i);
            return t;
        }();
        return tables;
    }

    inline float pv (const ParamValues& v, P p) noexcept { return v[(size_t) p]; }

    uint32_t mixSeed (uint32_t a, uint32_t b) noexcept
    {
        uint32_t x = a ^ (b * 0x9E3779B9u);
        x ^= x >> 16; x *= 0x85EBCA6Bu;
        x ^= x >> 13; x *= 0xC2B2AE35u;
        x ^= x >> 16;
        return x;
    }

    ap::Scale scaleFrom (const ParamValues& v) noexcept
    {
        return ap::Scale::fromType ((ap::ScaleType) (int) pv (v, P::KeyScale), (int) pv (v, P::KeyRoot));
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
    arpOut.resize ((size_t) kMaxBlock);
    seqOut.resize ((size_t) kMaxBlock);
    prepare (44100.0, 512);
}

Engine::~Engine() = default;

void Engine::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    for (auto& v : voices)
    {
        v.prepare (sr);
        v.kill();
    }
    arp.prepare (sr);
    seq.prepare (sr);
    sympathetic.prepare (sr, maxBlock);
    fx.prepare (sr, maxBlock);
    master.prepare (sr, 20.0f);
    master.snap (ap::dbToGain (pv (params, P::Master)));
    lastVoice = -1;
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
    lastVoice = -1;
    master.snap (ap::dbToGain (pv (params, P::Master)));
}

void Engine::setParams (const ParamValues& v)
{
    params = v;
    routing = routingFrom (v);
    tail.store (pv (v, envParam (0, EnvParam::R)) * 0.001f + fx.tailSeconds(), std::memory_order_relaxed);
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

float Engine::tailSeconds() const noexcept
{
    return tail.load (std::memory_order_relaxed);
}

//==============================================================================
void Engine::noteOn (int note, float velocity) noexcept
{
    const int limit = std::clamp ((int) pv (params, P::Voices), 1, kMaxVoices);

    int idx = -1;
    for (int i = 0; i < limit && idx < 0; ++i)
        if (! voices[(size_t) i].isActive()) idx = i;

    if (idx < 0)   // steal: the oldest released voice, else the oldest voice
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
    }
    if (idx < 0) idx = 0;

    VoiceBlockContext ctx;
    ctx.base = &params;
    ctx.routing = &routing;
    voices[(size_t) idx].start (note, velocity, ++noteOrder, mixSeed (livingSeed.load (std::memory_order_relaxed), noteCounter++), ctx);
    lastVoice = idx;
    sympathetic.noteOn (note);
}

void Engine::noteOff (int note) noexcept
{
    for (auto& v : voices)
    {
        if (! v.isActive() || v.isReleased() || v.note() != note)
            continue;
        if (sustainDown) v.sustained = true;
        else v.release();
    }
}

void Engine::handleEvent (const MidiEvent& e) noexcept
{
    switch (e.type)
    {
        case MidiEvent::Type::NoteOn:
            if (e.value > 0.0f) noteOn (std::clamp (e.note, 0, 127), std::clamp (e.value, 0.0f, 1.0f));
            else noteOff (e.note);
            break;
        case MidiEvent::Type::NoteOff:        noteOff (e.note); break;
        case MidiEvent::Type::PitchBend:      bend = std::clamp (e.value, -1.0f, 1.0f); break;
        case MidiEvent::Type::ModWheel:       wheel = std::clamp (e.value, 0.0f, 1.0f); break;
        case MidiEvent::Type::Aftertouch:     aftertouch = std::clamp (e.value, 0.0f, 1.0f); break;
        case MidiEvent::Type::PolyAftertouch:
            for (auto& v : voices)
                if (v.isActive() && ! v.isReleased() && v.note() == e.note)
                    v.setPolyAftertouch (std::clamp (e.value, 0.0f, 1.0f));
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
            for (auto& v : voices)
                if (v.isActive()) { v.sustained = false; v.release(); }
            sustainDown = false;
            break;
        case MidiEvent::Type::Cc:
            break;
    }
}

void Engine::updateGlobalModulation() noexcept
{
    ModSourceValues src;
    for (int m = 0; m < kNumMacros; ++m)
        src.v[(size_t) ModSource::Macro1 + (size_t) m] = pv (params, macroParam (m)) * 0.01f;
    src.v[(size_t) ModSource::ModWheel]   = wheel;
    src.v[(size_t) ModSource::PitchBend]  = bend;
    src.v[(size_t) ModSource::Aftertouch] = aftertouch;
    src.v[(size_t) ModSource::SeqMod]     = seq.modValue();

    if (lastVoice >= 0 && voices[(size_t) lastVoice].isActive())
    {
        const auto& v = voices[(size_t) lastVoice];
        for (int e = 0; e < kNumEnvs; ++e) src.v[(size_t) ModSource::Env1 + (size_t) e] = v.envValue (e);
        for (int l = 0; l < kNumLfos; ++l) src.v[(size_t) ModSource::Lfo1 + (size_t) l] = v.lfoValue (l);
    }

    ModMatrix::apply (params, routing, src, globalModulated, nullptr);
}

void Engine::renderControlBlock (float* L, float* R, int numSamples) noexcept
{
    VoiceBlockContext ctx;
    ctx.base = &params;
    ctx.routing = &routing;
    for (int m = 0; m < kNumMacros; ++m)
        ctx.globalSources.v[(size_t) ModSource::Macro1 + (size_t) m] = pv (params, macroParam (m)) * 0.01f;
    ctx.globalSources.v[(size_t) ModSource::ModWheel]   = wheel;
    ctx.globalSources.v[(size_t) ModSource::PitchBend]  = bend;
    ctx.globalSources.v[(size_t) ModSource::Aftertouch] = aftertouch;
    ctx.globalSources.v[(size_t) ModSource::SeqMod]     = seq.modValue();

    for (int o = 0; o < 2; ++o)
    {
        const int t = std::clamp ((int) pv (params, oscParam (o, OscParam::Table)), 0, kNumFactoryTables);
        const Wavetable* table = t < kNumFactoryTables ? factoryTables[(size_t) t].get() : userTablePtr[(size_t) o];
        ctx.tables[(size_t) o] = table != nullptr ? table : factoryTables[0].get();
    }
    ctx.lfoShapes = lfoShapePtr;
    ctx.pitchOffset = pv (globalModulated, P::Pitch) + bend * pv (params, P::BendRange);
    ctx.scale = scaleFrom (params);
    ctx.pitchSnap = pv (params, P::PitchSnap) > 0.5f;

    for (int i = 0; i < kMaxVoices; ++i)
    {
        auto& v = voices[(size_t) i];
        if (v.isActive())
            v.renderBlock (L, R, numSamples, ctx, i == lastVoice ? offsets.data() : nullptr);
    }
}

void Engine::process (float* L, float* R, int numSamples, const MidiEvent* events, int numEvents)
{
    if (numSamples <= 0)
        return;
    std::fill (L, L + numSamples, 0.0f);
    std::fill (R, R + numSamples, 0.0f);

    for (int o = 0; o < 2; ++o) userTablePtr[(size_t) o] = userTables[(size_t) o].take();
    for (int l = 0; l < kNumLfos; ++l) lfoShapePtr[(size_t) l] = lfoShapes[(size_t) l].take();

    // MIDI -> Arp -> StepSeq
    const auto scale = scaleFrom (params);
    arp.set (arpParamsFrom (params), scale);
    seq.set (seqParamsFrom (params), scale);
    const int numArp = arp.process (events, events != nullptr ? std::max (0, numEvents) : 0, transport, numSamples, arpOut.data(), (int) arpOut.size());
    const int numSeq = seq.process (arpOut.data(), numArp, transport, numSamples, seqOut.data(), (int) seqOut.size());

    updateGlobalModulation();

    // Voices, in control blocks split at event positions.
    int ei = 0;
    for (int pos = 0; pos < numSamples;)
    {
        while (ei < numSeq && seqOut[(size_t) ei].sampleOffset <= pos)
            handleEvent (seqOut[(size_t) ei++]);

        int len = std::min (kControlInterval, numSamples - pos);
        if (ei < numSeq)
            len = std::min (len, std::max (1, seqOut[(size_t) ei].sampleOffset - pos));

        renderControlBlock (L + pos, R + pos, len);
        pos += len;
    }
    while (ei < numSeq)
        handleEvent (seqOut[(size_t) ei++]);

    // Global stages: Sympathetic body -> FX rack -> master.
    sympathetic.setParams (pv (globalModulated, P::SympAmount) * 0.01f, (int) pv (params, P::SympMemory),
                           pv (globalModulated, P::SympDecay) * 0.01f, pv (globalModulated, P::SympBright) * 0.01f,
                           pv (params, P::SympDamp) * 0.01f, (int) pv (params, P::SympStrings), pv (params, P::SympSnap) > 0.5f, scale);
    sympathetic.process (L, R, numSamples);
    fx.setParams (globalModulated, transport);
    fx.process (L, R, numSamples);

    master.setTarget (ap::dbToGain (pv (globalModulated, P::Master)));
    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = master.next();
        float l = L[i] * g, r = R[i] * g;
        if (! std::isfinite (l)) l = 0.0f;
        if (! std::isfinite (r)) r = 0.0f;
        L[i] = l;
        R[i] = r;
        peak = std::max (peak, std::max (std::fabs (l), std::fabs (r)));
    }

    // Visual state.
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
        for (int d = 0; d <= kNumModDestinations; ++d) vis.modOffset[d].store (offsets[(size_t) d], std::memory_order_relaxed);
    }
    else
    {
        for (int e = 0; e < kNumEnvs; ++e) vis.envValue[e].store (0.0f, std::memory_order_relaxed);
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
