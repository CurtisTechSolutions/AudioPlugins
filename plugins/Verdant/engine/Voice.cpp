#include "Voice.h"

#include <ap/Math.h>

#include <algorithm>
#include <cmath>

namespace verdant
{

namespace
{
    inline float pv (const ParamValues& v, P p) noexcept { return v[(size_t) p]; }
    inline int pi (const ParamValues& v, P p) noexcept   { return (int) std::lround (v[(size_t) p]); }

    constexpr float kVoiceGain = 0.35f;        // per-voice headroom (CONTRACT §2)
    constexpr float kMinVelocityGain = 0.25f;  // voice gain = lerp (0.25, 1, velocity)

    // Glide: a one-pole on log2 (Hz) with time constant glide / 3, normalised so it
    // lands exactly on the target after `glide` ms (a plain one-pole would still be
    // 5 % of the interval away, i.e. 35 ct after a fifth).
    const float kGlideTail = std::exp (-3.0f);

    enum Route { RouteFilter1 = 0, RouteFilter2, RouteBoth, RouteDirect };

    uint32_t hashSeed (uint32_t a, uint32_t b) noexcept
    {
        uint32_t x = a ^ (b * 0x9E3779B9u);
        x ^= x >> 16; x *= 0x85EBCA6Bu;
        x ^= x >> 13; x *= 0xC2B2AE35u;
        x ^= x >> 16;
        return x;
    }

    float hashUnit (uint32_t x) noexcept
    {
        x ^= x >> 16; x *= 0x7FEB352Du;
        x ^= x >> 15; x *= 0x846CA68Bu;
        x ^= x >> 16;
        return (float) (x >> 8) * (1.0f / 16777216.0f);
    }

    inline void addInto (float* dst, const float* src, int n) noexcept
    {
        for (int i = 0; i < n; ++i) dst[i] += src[i];
    }

    /** Adds a mono source with a per-sample linear gain ramp a -> b into a stereo pair. */
    inline void addMonoRamped (float* l, float* r, const float* src, int n, float a, float b) noexcept
    {
        const float step = (b - a) / (float) n;
        float g = a;
        for (int i = 0; i < n; ++i)
        {
            g += step;
            l[i] += src[i] * g;
            r[i] += src[i] * g;
        }
    }

    /** Routes a stereo source into the filter inputs / direct path (CONTRACT §3 routing semantics). */
    struct RouteTargets { bool f1 = false, f2 = false, direct = false; };

    RouteTargets routeFor (int route, bool serial) noexcept
    {
        RouteTargets t;
        switch (route)
        {
            case RouteFilter2: t.f2 = true; break;
            case RouteBoth:    if (serial) t.f1 = true; else t.f1 = t.f2 = true; break;   // serial "Both" = the Filter 1 path
            case RouteDirect:  t.direct = true; break;
            case RouteFilter1:
            default:           t.f1 = true; break;
        }
        return t;
    }
}

//==============================================================================
void Voice::Glide::jump (float pitch) noexcept
{
    from = to = current = pitch;
    elapsed = length = 0.0;
}

void Voice::Glide::begin (float fromPitch, float toPitch, double lengthSamples) noexcept
{
    from = current = fromPitch;
    to = toPitch;
    elapsed = 0.0;
    length = lengthSamples;
}

void Voice::Glide::advance (int numSamples) noexcept
{
    if (length <= 0.0 || elapsed >= length)
    {
        current = to;
        return;
    }
    elapsed += (double) numSamples;
    if (elapsed >= length)
    {
        current = to;
        return;
    }
    const float t = (float) (elapsed / length);
    const float remaining = (std::exp (-3.0f * t) - kGlideTail) / (1.0f - kGlideTail);   // 1 -> 0
    current = to + (from - to) * remaining;
}

//==============================================================================
void Voice::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    for (auto& o : oscs) o.prepare (sr);
    sub.prepare (sr);
    noise.prepare (sr);
    for (auto& f : filters) f.prepare (sr);
    for (auto& e : envs) e.prepare (sr);
    for (auto& l : lfos) l.prepare (sr);
    living.prepare (sr);
    fadeTotal = std::max (1, (int) std::lround ((double) kStealFadeMs * 0.001 * sr));
    kill();
}

OscParams Voice::oscParamsFrom (const ParamValues& v, int o) noexcept
{
    OscParams p;
    p.position    = std::clamp (pv (v, oscParam (o, OscParam::Pos)) * 0.01f, 0.0f, 1.0f);
    p.warp        = (WarpMode) std::clamp (pi (v, oscParam (o, OscParam::WarpMode)), 0, (int) WarpMode::Count - 1);
    p.warpAmount  = pv (v, oscParam (o, OscParam::Warp)) * 0.01f;
    p.unison      = std::clamp (pi (v, oscParam (o, OscParam::UniVoices)), 1, kMaxUnison);
    p.detuneCents = pv (v, oscParam (o, OscParam::UniDetune));
    p.spread      = pv (v, oscParam (o, OscParam::UniSpread)) * 0.01f;
    p.blend       = pv (v, oscParam (o, OscParam::UniBlend)) * 0.01f;
    p.startPhase  = pv (v, oscParam (o, OscParam::Phase)) / 360.0f;
    p.randomPhase = pv (v, oscParam (o, OscParam::RandPhase)) * 0.01f;
    p.level       = pv (v, oscParam (o, OscParam::Level)) * 0.01f;
    p.pan         = pv (v, oscParam (o, OscParam::Pan)) * 0.01f;
    return p;
}

void Voice::configureEnvelopes (const ParamValues& v) noexcept
{
    // Living's attack multiplier is latched at note-on (it is constant per note) and
    // scales every envelope's attack so the whole articulation varies coherently.
    const float attackMul = std::max (0.0f, living.offsets().attackMul);
    for (int e = 0; e < kNumEnvs; ++e)
    {
        EnvParams p;
        p.attackMs    = pv (v, envParam (e, EnvParam::A)) * attackMul;
        p.holdMs      = pv (v, envParam (e, EnvParam::H));
        p.decayMs     = pv (v, envParam (e, EnvParam::D));
        p.sustain     = pv (v, envParam (e, EnvParam::S)) * 0.01f;
        p.releaseMs   = pv (v, envParam (e, EnvParam::R));
        p.attackCurve = pv (v, envParam (e, EnvParam::ACurve)) * 0.01f;
        p.decayCurve  = pv (v, envParam (e, EnvParam::DCurve)) * 0.01f;
        envs[(size_t) e].set (p);
    }
}

void Voice::configureLfos (const ParamValues& v) noexcept
{
    for (int l = 0; l < kNumLfos; ++l)
    {
        LfoParams p;
        p.shape      = (LfoWave) std::clamp (pi (v, lfoParam (l, LfoParam::Shape)), 0, (int) LfoWave::Count - 1);
        p.rateHz     = pv (v, lfoParam (l, LfoParam::Rate));
        p.sync       = pv (v, lfoParam (l, LfoParam::Sync)) > 0.5f;
        p.division   = (ap::NoteDivision) std::clamp (pi (v, lfoParam (l, LfoParam::Div)), 0, (int) ap::NoteDivision::Count - 1);
        p.mode       = (LfoMode) std::clamp (pi (v, lfoParam (l, LfoParam::Mode)), 0, 2);
        p.startPhase = pv (v, lfoParam (l, LfoParam::Phase)) * 0.01f;
        p.fadeMs     = pv (v, lfoParam (l, LfoParam::Fade));
        p.smooth     = pv (v, lfoParam (l, LfoParam::Smooth)) * 0.01f;
        p.unipolar   = pv (v, lfoParam (l, LfoParam::Polarity)) > 0.5f;
        lfos[(size_t) l].set (p);
    }
}

float Voice::pitchToHz (float semitones) const noexcept
{
    if (! std::isfinite (semitones))
        semitones = 60.0f;
    const float hz = ap::midiToHz (std::clamp (semitones, -48.0f, 160.0f));
    return std::min (hz, (float) (sr * 0.45));
}

float Voice::physicsValue (int i) const noexcept
{
    switch (i)
    {
        case 0:  return ball.height();
        case 1:  return spring.displacement();
        case 2:  return orbit.x();
        case 3:  return orbit.y();
        default: return 0.0f;
    }
}

void Voice::setPolyAftertouch (float value) noexcept
{
    polyAftertouch = std::clamp (value, 0.0f, 1.0f);
    hasPolyAftertouch = true;
}

//==============================================================================
void Voice::noteOnSources (const VoiceBlockContext& ctx, ModSourceValues& src) noexcept
{
    // Sources as they stand at the note start: the matrix pass that latches the
    // envelope times uses them (Velocity -> Env 1 Attack, Note -> Env 2 Decay, ...).
    const auto& base = *ctx.base;
    src = ctx.globalSources;
    for (int l = 0; l < kNumLfos; ++l)
    {
        const bool freeMode = pi (base, lfoParam (l, LfoParam::Mode)) == (int) LfoMode::Free;
        src.v[(size_t) ModSource::Lfo1 + (size_t) l] = freeMode ? ctx.freeLfoValues[(size_t) l]
                                                                : sources.v[(size_t) ModSource::Lfo1 + (size_t) l];
    }
    for (int e = 0; e < kNumEnvs; ++e)
        src.v[(size_t) ModSource::Env1 + (size_t) e] = envs[(size_t) e].value();
    src.v[(size_t) ModSource::Velocity] = velocity;
    src.v[(size_t) ModSource::Note]     = std::clamp (((float) midiNote - 60.0f) / 60.0f, -1.0f, 1.0f);
    src.v[(size_t) ModSource::Random]   = randomValue;
    src.v[(size_t) ModSource::Ball]     = ball.height();
    src.v[(size_t) ModSource::Spring]   = spring.displacement();
    src.v[(size_t) ModSource::Orbit]    = orbit.x();
    src.v[(size_t) ModSource::Alive]    = living.offsets().drift;
    if (hasPolyAftertouch)
        src.v[(size_t) ModSource::Aftertouch] = polyAftertouch;
}

void Voice::triggerModulators (uint32_t seed, const VoiceBlockContext& ctx) noexcept
{
    const auto& base = *ctx.base;

    ball.set (pv (base, P::BallGravity) * 0.01f, pv (base, P::BallBounce) * 0.01f);
    spring.set (pv (base, P::SpringStiffness) * 0.01f, pv (base, P::SpringDamping) * 0.01f);
    orbit.set (pv (base, P::OrbitSpeed) * 0.01f, pv (base, P::OrbitShape) * 0.01f);
    ball.trigger (velocity);
    spring.trigger (velocity);
    orbit.trigger();

    // Envelope times (incl. their modulation) latch here, so the matrix runs once
    // with the note-on sources before the envelopes start.
    ModSourceValues src;
    noteOnSources (ctx, src);
    ModMatrix::apply (base, *ctx.routing, src, modulated, nullptr);

    configureEnvelopes (modulated);
    for (auto& e : envs) e.noteOn();   // restarts the attack from the current level (no click)

    configureLfos (modulated);
    for (int l = 0; l < kNumLfos; ++l)
    {
        lfos[(size_t) l].setCustomShape (ctx.lfoShapes[(size_t) l]);
        lfos[(size_t) l].syncToTransport (ctx.transport);
        if (pi (base, lfoParam (l, LfoParam::Mode)) != (int) LfoMode::Free)
            lfos[(size_t) l].noteOn (hashSeed (seed, 0x1F0u + (uint32_t) l));
    }
    sources = src;
}

void Voice::setupGlide (const NoteStart& s) noexcept
{
    const float target = (float) s.note;
    if (s.glide && s.glideMs > 0.0f && std::isfinite (s.glideFrom) && s.glideFrom != target)
        glide.begin (s.glideFrom, target, (double) s.glideMs * 0.001 * sr);
    else
        glide.jump (target);
}

void Voice::beginNote (const NoteStart& s, const VoiceBlockContext& ctx) noexcept
{
    const ParamValues& base = *ctx.base;
    midiNote = std::clamp (s.note, 0, 127);
    velocity = std::clamp (s.velocity, 0.0f, 1.0f);
    startOrder = s.order;
    active = true;
    released = false;
    polyAftertouch = 0.0f;
    hasPolyAftertouch = false;
    fadeLeft = 0;
    hasPending = pendingRelease = false;

    // One seed per note: hash (living seed, note counter) -> random phase, Random, noise, LFO S&H.
    const uint32_t seed = hashSeed (s.globalSeed, s.noteCounter);
    randomValue = hashUnit (seed ^ 0xA511E9B3u);
    setupGlide (s);

    living.noteOn (s.globalSeed, s.noteCounter, std::clamp (pv (base, P::Alive) * 0.01f, 0.0f, 1.0f),
                   std::clamp (pv (base, P::Evolve) * 0.01f, 0.0f, 1.0f));

    for (auto& e : envs) e.kill();     // a fresh note starts from 0
    for (auto& v : sources.v) v = 0.0f;
    triggerModulators (seed, ctx);

    const auto& lv = living.offsets();
    const float common = glide.current + ctx.bendSemitones + pv (modulated, P::Pitch);
    for (int o = 0; o < 2; ++o)
    {
        auto& osc = oscs[(size_t) o];
        OscParams p = oscParamsFrom (modulated, o);
        p.position = std::clamp (p.position + lv.position, 0.0f, 1.0f);
        float semis = common + 12.0f * pv (modulated, oscParam (o, OscParam::Oct)) + pv (modulated, oscParam (o, OscParam::Semi))
                    + pv (modulated, oscParam (o, OscParam::Pitch));
        if (ctx.pitchSnap)
            semis = (float) ctx.scale.nearest (semis);
        semis += 0.01f * (pv (modulated, oscParam (o, OscParam::Fine)) + lv.pitchCents);
        osc.setTable (ctx.tables[(size_t) o]);
        osc.setParams (p);
        osc.noteOn (pitchToHz (semis), hashSeed (seed, 0x05C0u + (uint32_t) o));
    }

    float subSemis = common - 12.0f * (float) std::clamp (pi (modulated, P::SubOct), 0, 2);
    if (ctx.pitchSnap)
        subSemis = (float) ctx.scale.nearest (subSemis);
    sub.setWave ((SubWave) std::clamp (pi (modulated, P::SubWave), 0, (int) SubWave::Count - 1));
    sub.noteOn (pitchToHz (subSemis + 0.01f * lv.pitchCents));
    noise.reset (hashSeed (seed, 0x0153u));

    for (int f = 0; f < kNumFilters; ++f)
    {
        filters[(size_t) f].reset();
        filterWasOn[(size_t) f] = pv (modulated, filterParam (f, FilterParam::On)) > 0.5f;
        filterMix[(size_t) f].snap (std::clamp (pv (modulated, filterParam (f, FilterParam::Mix)) * 0.01f, 0.0f, 1.0f));
    }
    subLevel.snap (std::clamp (pv (modulated, P::SubLevel) * 0.01f, 0.0f, 1.0f));
    noiseLevel.snap (std::clamp (pv (modulated, P::NoiseLevel) * 0.01f, 0.0f, 1.0f));
    voicePan.snap (std::clamp (lv.pan, -1.0f, 1.0f));
    velocityGain.snap (ap::lerp (kMinVelocityGain, 1.0f, velocity));
    std::fill (prevA.begin(), prevA.end(), 0.0f);
    prevALength = 0;
}

void Voice::start (const NoteStart& s, const VoiceBlockContext& ctx) noexcept
{
    sustained = false;
    beginNote (s, ctx);
}

void Voice::steal (const NoteStart& s) noexcept
{
    if (! active)
    {
        // Nothing to fade: start at the next control block.
        fadeLeft = 0;
    }
    else if (fadeLeft <= 0)
    {
        fadeLeft = fadeTotal;
    }
    pending = s;
    hasPending = true;
    pendingRelease = false;
    sustained = false;
    active = true;
}

void Voice::retarget (const NoteStart& s, const VoiceBlockContext& ctx, bool retrigger) noexcept
{
    if (! active || hasPending)
    {
        start (s, ctx);
        return;
    }

    midiNote = std::clamp (s.note, 0, 127);
    velocity = std::clamp (s.velocity, 0.0f, 1.0f);
    startOrder = s.order;
    released = false;
    sustained = false;
    hasPolyAftertouch = false;
    setupGlide (s);   // oscillator phases continue: no click

    if (retrigger)
    {
        const uint32_t seed = hashSeed (s.globalSeed, s.noteCounter);
        randomValue = hashUnit (seed ^ 0xA511E9B3u);
        living.noteOn (s.globalSeed, s.noteCounter, std::clamp (pv (*ctx.base, P::Alive) * 0.01f, 0.0f, 1.0f),
                       std::clamp (pv (*ctx.base, P::Evolve) * 0.01f, 0.0f, 1.0f));
        triggerModulators (seed, ctx);
    }
}

void Voice::release() noexcept
{
    if (hasPending)
    {
        pendingRelease = true;
        return;
    }
    if (! active)
        return;
    released = true;
    for (auto& e : envs) e.noteOff();
}

void Voice::fadeOut() noexcept
{
    if (! active)
        return;
    hasPending = pendingRelease = false;
    if (fadeLeft <= 0)
        fadeLeft = fadeTotal;
}

void Voice::kill() noexcept
{
    active = false;
    released = false;
    sustained = false;
    hasPending = pendingRelease = false;
    fadeLeft = 0;
    for (auto& e : envs) e.kill();
}

//==============================================================================
void Voice::renderBlock (float* L, float* R, int numSamples, const VoiceBlockContext& ctx, float* offsets) noexcept
{
    if (! active || numSamples <= 0)
        return;
    numSamples = std::min (numSamples, kControlInterval);

    int done = 0;
    if (fadeLeft > 0 || hasPending)
    {
        if (fadeLeft > 0)
        {
            // The old sound fades out (renderCore applies the fade gain); a stolen voice then restarts.
            done = std::min (numSamples, fadeLeft);
            renderCore (L, R, done, ctx, offsets);
            if (fadeLeft > 0)
                return;
        }
        if (! hasPending)
        {
            kill();
            return;
        }
        const NoteStart next = pending;
        const bool releaseAfter = pendingRelease, wasSustained = sustained;
        beginNote (next, ctx);
        sustained = wasSustained;
        if (releaseAfter)
            release();
        if (done >= numSamples)
            return;
    }

    renderCore (L + done, R + done, numSamples - done, ctx, offsets);
}

void Voice::renderCore (float* L, float* R, int n, const VoiceBlockContext& ctx, float* offsets) noexcept
{
    if (n <= 0)
        return;
    const ParamValues& base = *ctx.base;
    const float dt = (float) ((double) n / sr);

    // ---- Mod sources for this control block
    ModSourceValues src = ctx.globalSources;
    for (int l = 0; l < kNumLfos; ++l)
    {
        auto& lfo = lfos[(size_t) l];
        float value;
        if (pi (base, lfoParam (l, LfoParam::Mode)) == (int) LfoMode::Free)
        {
            value = ctx.freeLfoValues[(size_t) l];   // one shared phase for every voice
        }
        else
        {
            lfo.setCustomShape (ctx.lfoShapes[(size_t) l]);
            lfo.syncToTransport (ctx.transport);
            value = lfo.next (n);
        }
        src.v[(size_t) ModSource::Lfo1 + (size_t) l] = value;
    }
    for (int e = 0; e < kNumEnvs; ++e)
        src.v[(size_t) ModSource::Env1 + (size_t) e] = envs[(size_t) e].value();   // value at the block start
    src.v[(size_t) ModSource::Velocity] = velocity;
    src.v[(size_t) ModSource::Note]     = std::clamp (((float) midiNote - 60.0f) / 60.0f, -1.0f, 1.0f);
    src.v[(size_t) ModSource::Random]   = randomValue;
    if (hasPolyAftertouch)
        src.v[(size_t) ModSource::Aftertouch] = polyAftertouch;   // poly AT overrides channel AT for this voice

    ball.set (pv (base, P::BallGravity) * 0.01f, pv (base, P::BallBounce) * 0.01f);
    spring.set (pv (base, P::SpringStiffness) * 0.01f, pv (base, P::SpringDamping) * 0.01f);
    orbit.set (pv (base, P::OrbitSpeed) * 0.01f, pv (base, P::OrbitShape) * 0.01f);
    ball.advance (dt);
    spring.advance (dt);
    orbit.advance (dt);
    src.v[(size_t) ModSource::Ball]   = ball.height();
    src.v[(size_t) ModSource::Spring] = spring.displacement();
    src.v[(size_t) ModSource::Orbit]  = orbit.x();

    living.advance (n);
    const auto& lv = living.offsets();
    src.v[(size_t) ModSource::Alive] = lv.drift;
    sources = src;

    // ---- Matrix -> this voice's params
    ModMatrix::apply (base, *ctx.routing, src, modulated, offsets);
    const ParamValues& v = modulated;
    configureEnvelopes (v);   // sustain follows live; times stay latched from the note-on
    configureLfos (v);        // rate modulation applies from the next block

    // ---- Pitch: note + glide + bend + global pitch (+ osc oct / semi / pitch), snapped, then fine / living cents
    glide.advance (n);
    const float common = glide.current + ctx.bendSemitones + pv (v, P::Pitch);
    const float livingSemis = 0.01f * lv.pitchCents;
    const bool serial = pi (base, P::FilterRouting) == 0;

    std::fill (f1L.begin(), f1L.begin() + n, 0.0f);
    std::fill (f1R.begin(), f1R.begin() + n, 0.0f);
    std::fill (f2L.begin(), f2L.begin() + n, 0.0f);
    std::fill (f2R.begin(), f2R.begin() + n, 0.0f);
    std::fill (dirL.begin(), dirL.begin() + n, 0.0f);
    std::fill (dirR.begin(), dirR.begin() + n, 0.0f);
    bool usedF1 = false, usedF2 = false;

    auto route = [&] (const RouteTargets& t, const float* l, const float* r)
    {
        if (t.f1)     { addInto (f1L.data(), l, n); addInto (f1R.data(), r, n); usedF1 = true; }
        if (t.f2)     { addInto (f2L.data(), l, n); addInto (f2R.data(), r, n); usedF2 = true; }
        if (t.direct) { addInto (dirL.data(), l, n); addInto (dirR.data(), r, n); }
    };

    // ---- Sub (renders first: both oscillators can FM from it)
    const bool aOn = pv (base, oscParam (0, OscParam::On)) > 0.5f;
    const bool bOn = pv (base, oscParam (1, OscParam::On)) > 0.5f;
    const auto warpA = (WarpMode) pi (v, oscParam (0, OscParam::WarpMode));
    const auto warpB = (WarpMode) pi (v, oscParam (1, OscParam::WarpMode));
    subLevel.next (std::clamp (pv (v, P::SubLevel) * 0.01f, 0.0f, 1.0f));
    const bool subNeeded = ! subLevel.silent() || (aOn && warpA == WarpMode::FmSub) || (bOn && warpB == WarpMode::FmSub);
    if (subNeeded)
    {
        float subSemis = common - 12.0f * (float) std::clamp (pi (v, P::SubOct), 0, 2);
        if (ctx.pitchSnap)
            subSemis = (float) ctx.scale.nearest (subSemis);
        sub.setWave ((SubWave) std::clamp (pi (v, P::SubWave), 0, (int) SubWave::Count - 1));
        sub.setFrequency (pitchToHz (subSemis + livingSemis));
        sub.render (subBuf.data(), n);
        if (! subLevel.silent())
        {
            std::fill (oscL.begin(), oscL.begin() + n, 0.0f);
            std::fill (oscR.begin(), oscR.begin() + n, 0.0f);
            addMonoRamped (oscL.data(), oscR.data(), subBuf.data(), n, subLevel.previous, subLevel.target);
            route (routeFor (pi (base, P::SubRoute), serial), oscL.data(), oscR.data());
        }
    }
    else
    {
        std::fill (subBuf.begin(), subBuf.begin() + n, 0.0f);
    }

    // ---- Noise
    noiseLevel.next (std::clamp (pv (v, P::NoiseLevel) * 0.01f, 0.0f, 1.0f));
    if (! noiseLevel.silent())
    {
        noise.setColour ((NoiseColour) std::clamp (pi (v, P::NoiseColor), 0, (int) NoiseColour::Count - 1));
        noise.render (noiseBuf.data(), n);
        std::fill (oscL.begin(), oscL.begin() + n, 0.0f);
        std::fill (oscR.begin(), oscR.begin() + n, 0.0f);
        addMonoRamped (oscL.data(), oscR.data(), noiseBuf.data(), n, noiseLevel.previous, noiseLevel.target);
        route (routeFor (pi (base, P::NoiseRoute), serial), oscL.data(), oscR.data());
    }

    // ---- Oscillators: B first (its FM Osc input is A's previous block), then A (FM Osc = B's fresh block)
    auto renderOsc = [&] (int o, const float* fm)
    {
        auto& osc = oscs[(size_t) o];
        OscParams p = oscParamsFrom (v, o);
        p.position = std::clamp (p.position + lv.position, 0.0f, 1.0f);
        float semis = common + 12.0f * pv (v, oscParam (o, OscParam::Oct)) + pv (v, oscParam (o, OscParam::Semi))
                    + pv (v, oscParam (o, OscParam::Pitch));
        if (ctx.pitchSnap)
            semis = (float) ctx.scale.nearest (semis);
        semis += 0.01f * pv (v, oscParam (o, OscParam::Fine)) + livingSemis;
        osc.setTable (ctx.tables[(size_t) o]);
        osc.setFrequency (pitchToHz (semis));
        osc.setParams (p);
        osc.render (oscL.data(), oscR.data(), n, fm);
        route (routeFor (pi (base, oscParam (o, OscParam::Route)), serial), oscL.data(), oscR.data());
    };

    if (bOn)
    {
        const float* fm = nullptr;
        if (warpB == WarpMode::FmOsc)
        {
            // A's previous block; hold its last sample if that block was shorter.
            const float hold = prevALength > 0 ? prevA[(size_t) prevALength - 1] : 0.0f;
            for (int i = 0; i < n; ++i)
                fmBuf[(size_t) i] = i < prevALength ? prevA[(size_t) i] : hold;
            fm = fmBuf.data();
        }
        else if (warpB == WarpMode::FmSub)
        {
            fm = subBuf.data();
        }
        renderOsc (1, fm);
        for (int i = 0; i < n; ++i)
            monoB[(size_t) i] = 0.5f * (oscL[(size_t) i] + oscR[(size_t) i]);
    }
    else
    {
        std::fill (monoB.begin(), monoB.begin() + n, 0.0f);
    }

    if (aOn)
    {
        const float* fm = warpA == WarpMode::FmOsc ? monoB.data() : (warpA == WarpMode::FmSub ? subBuf.data() : nullptr);
        renderOsc (0, fm);
        for (int i = 0; i < n; ++i)
            prevA[(size_t) i] = 0.5f * (oscL[(size_t) i] + oscR[(size_t) i]);
        prevALength = n;
    }
    else
    {
        prevALength = 0;
    }

    // ---- Filters (CONTRACT §3): serial F1 -> F2, or parallel; off = bypass; mix = dry / wet
    auto runFilter = [&] (int f, float* l, float* r)
    {
        auto& flt = filters[(size_t) f];
        auto& mix = filterMix[(size_t) f];
        const bool on = pv (base, filterParam (f, FilterParam::On)) > 0.5f;
        if (! on)
        {
            filterWasOn[(size_t) f] = false;
            return;   // bypass
        }
        if (! filterWasOn[(size_t) f])
        {
            flt.reset();   // re-enabled: start from a clean state rather than a stale tail
            filterWasOn[(size_t) f] = true;
            mix.snap (std::clamp (pv (v, filterParam (f, FilterParam::Mix)) * 0.01f, 0.0f, 1.0f));
        }

        const float key = pv (base, filterParam (f, FilterParam::Key)) * 0.01f;
        const float octaves = key * (glide.current - 60.0f) / 12.0f + lv.cutoffOctaves;
        const float cutoff = std::clamp (pv (v, filterParam (f, FilterParam::Cutoff)) * std::exp2 (octaves),
                                         20.0f, std::min (20000.0f, (float) (sr * 0.45)));
        flt.set ((FilterType) std::clamp (pi (v, filterParam (f, FilterParam::Type)), 0, (int) FilterType::Count - 1),
                 cutoff,
                 std::clamp (pv (v, filterParam (f, FilterParam::Res)) * 0.01f, 0.0f, 1.0f),
                 std::clamp (pv (v, filterParam (f, FilterParam::Drive)) * 0.01f, 0.0f, 1.0f));

        mix.next (std::clamp (pv (v, filterParam (f, FilterParam::Mix)) * 0.01f, 0.0f, 1.0f));
        const bool fullyWet = mix.previous >= 1.0f && mix.target >= 1.0f;
        if (! fullyWet)
        {
            std::copy (l, l + n, dryL.begin());
            std::copy (r, r + n, dryR.begin());
        }
        flt.process (l, r, n);
        if (! fullyWet)
        {
            const float step = (mix.target - mix.previous) / (float) n;
            float m = mix.previous;
            for (int i = 0; i < n; ++i)
            {
                m += step;
                l[i] = dryL[(size_t) i] + (l[i] - dryL[(size_t) i]) * m;
                r[i] = dryR[(size_t) i] + (r[i] - dryR[(size_t) i]) * m;
            }
        }
    };

    if (serial)
    {
        if (usedF1)
        {
            runFilter (0, f1L.data(), f1R.data());
            addInto (f2L.data(), f1L.data(), n);
            addInto (f2R.data(), f1R.data(), n);
            usedF2 = true;
        }
        if (usedF2)
        {
            runFilter (1, f2L.data(), f2R.data());
            addInto (dirL.data(), f2L.data(), n);
            addInto (dirR.data(), f2R.data(), n);
        }
    }
    else
    {
        if (usedF1)
        {
            runFilter (0, f1L.data(), f1R.data());
            addInto (dirL.data(), f1L.data(), n);
            addInto (dirR.data(), f1R.data(), n);
        }
        if (usedF2)
        {
            runFilter (1, f2L.data(), f2R.data());
            addInto (dirL.data(), f2L.data(), n);
            addInto (dirR.data(), f2R.data(), n);
        }
    }

    // ---- Amp envelope (per sample) x velocity x headroom x steal fade -> voice pan
    voicePan.next (std::clamp (lv.pan, -1.0f, 1.0f));
    const float panStep = (voicePan.target - voicePan.previous) / (float) n;
    float pan = voicePan.previous;
    velocityGain.next (ap::lerp (kMinVelocityGain, 1.0f, velocity));   // a legato note's new velocity glides in
    const float velStep = (velocityGain.target - velocityGain.previous) * kVoiceGain / (float) n;
    float velGain = velocityGain.previous * kVoiceGain;
    const float fadeStep = 1.0f / (float) fadeTotal;
    auto& amp = envs[0];
    for (int i = 0; i < n; ++i)
    {
        pan += panStep;
        velGain += velStep;
        float g = amp.next() * velGain;
        if (fadeLeft > 0)
        {
            g *= (float) fadeLeft * fadeStep;
            --fadeLeft;
        }
        const float gl = g * std::min (1.0f, 1.0f - pan);   // balance law: centre = unity on both sides
        const float gr = g * std::min (1.0f, 1.0f + pan);
        L[i] += dirL[(size_t) i] * gl;
        R[i] += dirR[(size_t) i] * gr;
    }
    envs[1].skip (n);
    envs[2].skip (n);

    if (! amp.isActive() && fadeLeft <= 0 && ! hasPending)
        kill();
    else if (! amp.isActive() && fadeLeft > 0)
        fadeLeft = 0;   // nothing left to fade
}

} // namespace verdant
