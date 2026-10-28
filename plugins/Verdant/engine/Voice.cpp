#include "Voice.h"

#include <ap/Math.h>

#include <algorithm>
#include <cmath>

namespace verdant
{

namespace
{
    inline float pv (const ParamValues& v, P p) noexcept { return v[(size_t) p]; }

    float hashUnit (uint32_t x) noexcept
    {
        x ^= x >> 16; x *= 0x7FEB352Du;
        x ^= x >> 15; x *= 0x846CA68Bu;
        x ^= x >> 16;
        return (float) (x >> 8) * (1.0f / 16777216.0f);
    }
}

void Voice::prepare (double sampleRate)
{
    sr = sampleRate;
    for (auto& o : oscs) o.prepare (sampleRate);
    for (auto& f : filters) f.prepare (sampleRate);
    for (auto& e : envs) e.prepare (sampleRate);
    for (auto& l : lfos) l.prepare (sampleRate);
    living.prepare (sampleRate);
}

OscParams Voice::oscParamsFrom (const ParamValues& v, int o) noexcept
{
    OscParams p;
    p.position    = pv (v, oscParam (o, OscParam::Pos)) * 0.01f;
    p.warp        = (WarpMode) (int) pv (v, oscParam (o, OscParam::WarpMode));
    p.warpAmount  = pv (v, oscParam (o, OscParam::Warp)) * 0.01f;
    p.unison      = (int) pv (v, oscParam (o, OscParam::UniVoices));
    p.detuneCents = pv (v, oscParam (o, OscParam::UniDetune));
    p.spread      = pv (v, oscParam (o, OscParam::UniSpread)) * 0.01f;
    p.blend       = pv (v, oscParam (o, OscParam::UniBlend)) * 0.01f;
    p.startPhase  = pv (v, oscParam (o, OscParam::Phase)) / 360.0f;
    p.randomPhase = pv (v, oscParam (o, OscParam::RandPhase)) * 0.01f;
    p.level       = pv (v, oscParam (o, OscParam::Level)) * 0.01f;
    p.pan         = pv (v, oscParam (o, OscParam::Pan)) * 0.01f;
    return p;
}

void Voice::configureEnvelopes (const ParamValues& v)
{
    for (int e = 0; e < kNumEnvs; ++e)
    {
        EnvParams p;
        p.attackMs    = pv (v, envParam (e, EnvParam::A));
        p.holdMs      = pv (v, envParam (e, EnvParam::H));
        p.decayMs     = pv (v, envParam (e, EnvParam::D));
        p.sustain     = pv (v, envParam (e, EnvParam::S)) * 0.01f;
        p.releaseMs   = pv (v, envParam (e, EnvParam::R));
        p.attackCurve = pv (v, envParam (e, EnvParam::ACurve)) * 0.01f;
        p.decayCurve  = pv (v, envParam (e, EnvParam::DCurve)) * 0.01f;
        envs[(size_t) e].set (p);
    }
}

void Voice::configureLfos (const ParamValues& v)
{
    for (int l = 0; l < kNumLfos; ++l)
    {
        LfoParams p;
        p.shape      = (LfoWave) (int) pv (v, lfoParam (l, LfoParam::Shape));
        p.rateHz     = pv (v, lfoParam (l, LfoParam::Rate));
        p.sync       = pv (v, lfoParam (l, LfoParam::Sync)) > 0.5f;
        p.division   = (ap::NoteDivision) (int) pv (v, lfoParam (l, LfoParam::Div));
        p.mode       = (LfoMode) (int) pv (v, lfoParam (l, LfoParam::Mode));
        p.startPhase = pv (v, lfoParam (l, LfoParam::Phase)) * 0.01f;
        p.fadeMs     = pv (v, lfoParam (l, LfoParam::Fade));
        p.smooth     = pv (v, lfoParam (l, LfoParam::Smooth)) * 0.01f;
        p.unipolar   = pv (v, lfoParam (l, LfoParam::Polarity)) > 0.5f;
        lfos[(size_t) l].set (p);
    }
}

void Voice::start (int note, float vel, uint64_t order, uint32_t seed, const VoiceBlockContext& ctx)
{
    const ParamValues& base = *ctx.base;
    midiNote = note;
    velocity = vel;
    startOrder = order;
    active = true;
    released = false;
    sustained = false;
    polyAftertouch = 0.0f;
    randomValue = hashUnit (seed ^ 0xA511E9B3u);

    configureEnvelopes (base);
    for (auto& e : envs) e.noteOn();

    configureLfos (base);
    for (int l = 0; l < kNumLfos; ++l)
        lfos[(size_t) l].noteOn (seed + 0x9E3779B9u * (uint32_t) (l + 1));

    living.noteOn (seed, (uint32_t) order, pv (base, P::Alive) * 0.01f, pv (base, P::Evolve) * 0.01f);

    for (int o = 0; o < 2; ++o)
    {
        oscs[(size_t) o].setParams (oscParamsFrom (base, o));
        oscs[(size_t) o].noteOn (ap::midiToHz ((float) note), seed + 0x632BE5ABu * (uint32_t) (o + 1));
    }
    for (auto& f : filters) f.reset();
}

void Voice::release()
{
    released = true;
    for (auto& e : envs) e.noteOff();
}

void Voice::kill()
{
    active = false;
    released = false;
    sustained = false;
    for (auto& e : envs) e.kill();
}

void Voice::renderBlock (float* L, float* R, int numSamples, const VoiceBlockContext& ctx, float* offsets) noexcept
{
    if (! active || numSamples <= 0)
        return;
    numSamples = std::min (numSamples, kControlInterval);

    // ---- Mod sources for this block
    ModSourceValues src = ctx.globalSources;
    for (int l = 0; l < kNumLfos; ++l)
    {
        lfos[(size_t) l].setCustomShape (ctx.lfoShapes[(size_t) l]);
        src.v[(size_t) ModSource::Lfo1 + (size_t) l] = lfos[(size_t) l].next (numSamples);
    }
    for (int e = 0; e < kNumEnvs; ++e)
        src.v[(size_t) ModSource::Env1 + (size_t) e] = envs[(size_t) e].value();
    src.v[(size_t) ModSource::Velocity] = velocity;
    src.v[(size_t) ModSource::Note]     = std::clamp (((float) midiNote - 60.0f) / 60.0f, -1.0f, 1.0f);
    src.v[(size_t) ModSource::Random]   = randomValue;
    src.v[(size_t) ModSource::Alive]    = living.offsets().drift;
    if (polyAftertouch > src.v[(size_t) ModSource::Aftertouch])
        src.v[(size_t) ModSource::Aftertouch] = polyAftertouch;

    ModMatrix::apply (*ctx.base, *ctx.routing, src, modulated, offsets);
    const ParamValues& v = modulated;

    // ---- Oscillators
    std::fill (bufL.begin(), bufL.begin() + numSamples, 0.0f);
    std::fill (bufR.begin(), bufR.begin() + numSamples, 0.0f);
    const float notePitch = (float) midiNote + ctx.pitchOffset;

    for (int o = 1; o >= 0; --o)   // Osc B first, then Osc A (CONTRACT §2)
    {
        if (pv (v, oscParam (o, OscParam::On)) < 0.5f)
            continue;
        auto& osc = oscs[(size_t) o];
        float semis = notePitch + 12.0f * pv (v, oscParam (o, OscParam::Oct)) + pv (v, oscParam (o, OscParam::Semi))
                    + pv (v, oscParam (o, OscParam::Pitch));
        if (ctx.pitchSnap)
            semis = (float) ctx.scale.nearest (semis);
        semis += 0.01f * pv (v, oscParam (o, OscParam::Fine));
        osc.setTable (ctx.tables[(size_t) o]);
        osc.setFrequency (std::min (ap::midiToHz (semis), (float) (sr * 0.45)));
        osc.setParams (oscParamsFrom (v, o));
        osc.render (oscL.data(), oscR.data(), numSamples);
        for (int i = 0; i < numSamples; ++i)
        {
            bufL[(size_t) i] += oscL[(size_t) i];
            bufR[(size_t) i] += oscR[(size_t) i];
        }
    }

    // ---- Filters (serial)
    for (int f = 0; f < kNumFilters; ++f)
    {
        if (pv (v, filterParam (f, FilterParam::On)) < 0.5f)
            continue;
        auto& flt = filters[(size_t) f];
        flt.set ((FilterType) (int) pv (v, filterParam (f, FilterParam::Type)),
                 std::clamp (pv (v, filterParam (f, FilterParam::Cutoff)), 20.0f, (float) (sr * 0.45)),
                 pv (v, filterParam (f, FilterParam::Res)) * 0.01f,
                 pv (v, filterParam (f, FilterParam::Drive)) * 0.01f);
        flt.process (bufL.data(), bufR.data(), numSamples);
    }

    // ---- Amp envelope, voice gain, output
    auto& amp = envs[0];
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = amp.next() * 0.35f;
        L[i] += bufL[(size_t) i] * g;
        R[i] += bufR[(size_t) i] * g;
    }
    envs[1].skip (numSamples);
    envs[2].skip (numSamples);
    living.advance (numSamples);

    if (! amp.isActive())
        kill();
}

} // namespace verdant
