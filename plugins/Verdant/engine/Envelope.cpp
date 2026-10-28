#include "Envelope.h"

#include <algorithm>
#include <cmath>

namespace verdant
{

namespace
{
    constexpr float kSustainSlewMs = 10.0f;      // full-scale sustain change takes this long
    constexpr float kMaxTimeMs     = 60000.0f;

    inline float finiteOr (float v, float fallback) noexcept
    {
        return std::isfinite (v) ? v : fallback;
    }

    inline float clampTime (float ms) noexcept
    {
        return std::clamp (finiteOr (ms, 0.0f), 0.0f, kMaxTimeMs);
    }

    /** Exponent of the contract's shape (t, c). */
    inline float curveExponent (float c) noexcept
    {
        c = std::clamp (finiteOr (c, 0.0f), -1.0f, 1.0f);
        return c >= 0.0f ? 1.0f / (1.0f + 3.0f * c) : 1.0f - 3.0f * c;
    }

    inline float powShape (double t, float exponent) noexcept
    {
        if (t <= 0.0) return 0.0f;
        if (t >= 1.0) return 1.0f;
        return exponent == 1.0f ? (float) t : (float) std::pow (t, (double) exponent);
    }
}

float Envelope::shape (float t, float c) noexcept
{
    return powShape ((double) std::clamp (finiteOr (t, 0.0f), 0.0f, 1.0f), curveExponent (c));
}

void Envelope::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 && std::isfinite (sampleRate) ? sampleRate : 44100.0;
    sustainSlew = (float) (1.0 / (0.001 * (double) kSustainSlewMs * sr));
    kill();
}

void Envelope::set (const EnvParams& p) noexcept
{
    params = p;
    const float target = std::clamp (finiteOr (p.sustain, 0.0f), 0.0f, 1.0f);
    if (target != sustainTarget)
    {
        // Restart the slew from where the level is now; the slew position is a
        // sample count, so next() and skip() land on exactly the same value.
        sustainTarget = target;
        slewFrom = sustainNow;
        slewCount = 0;
    }
    if (current == Stage::Idle)
        sustainNow = slewFrom = sustainTarget;
}

void Envelope::noteOn() noexcept
{
    latched.attackMs    = clampTime (params.attackMs);
    latched.holdMs      = clampTime (params.holdMs);
    latched.decayMs     = clampTime (params.decayMs);
    latched.releaseMs   = clampTime (params.releaseMs);
    latched.sustain     = sustainTarget;
    latched.attackCurve = params.attackCurve;
    latched.decayCurve  = params.decayCurve;
    attackExp = curveExponent (params.attackCurve);
    decayExp  = curveExponent (-finiteOr (params.decayCurve, 0.0f));

    if (current == Stage::Idle)
        sustainNow = slewFrom = sustainTarget;

    startLevel = level;   // retrigger: the attack rises from wherever we are
    enterStage (Stage::Attack);
}

void Envelope::noteOff() noexcept
{
    if (current == Stage::Idle || current == Stage::Release)
        return;
    enterStage (Stage::Release);
}

void Envelope::kill() noexcept
{
    current = Stage::Idle;
    level = 0.0f;
    pos = segEnd = 0;
    segLen = 0.0;
}

bool Envelope::isActive() const noexcept
{
    return current != Stage::Idle;
}

Envelope::Stage Envelope::stage() const noexcept
{
    return current;
}

float Envelope::value() const noexcept
{
    return level;
}

float Envelope::next() noexcept
{
    advance (1);
    return level;
}

void Envelope::skip (int numSamples) noexcept
{
    advance (numSamples);
}

void Envelope::enterStage (Stage s) noexcept
{
    current = s;
    pos = 0;

    const auto segment = [this] (float ms)
    {
        segLen = (double) ms * 0.001 * sr;
        segEnd = std::max<int64_t> (1, (int64_t) std::ceil (segLen - 1.0e-9));
    };

    switch (s)
    {
        case Stage::Idle:
            level = 0.0f;
            break;

        case Stage::Attack:
            segment (latched.attackMs);
            break;

        case Stage::Hold:
            level = 1.0f;
            if ((double) latched.holdMs * 0.001 * sr < 0.5)
                enterStage (Stage::Decay);   // no hold: straight into the decay
            else
                segment (latched.holdMs);
            break;

        case Stage::Decay:
            segment (latched.decayMs);
            break;

        case Stage::Sustain:
            level = sustainNow;
            break;

        case Stage::Release:
            startLevel = level;
            segment (latched.releaseMs);
            break;
    }
}

void Envelope::slewSustain (int numSamples) noexcept
{
    if (sustainNow == sustainTarget)
        return;
    slewCount += numSamples;
    const float dist = sustainTarget - slewFrom;
    const float moved = (float) ((double) sustainSlew * (double) slewCount);
    sustainNow = std::fabs (dist) <= moved ? sustainTarget : slewFrom + std::copysign (moved, dist);
}

float Envelope::levelAt() const noexcept
{
    const double t = segLen > 0.0 ? std::min (1.0, (double) pos / segLen) : 1.0;
    switch (current)
    {
        case Stage::Attack:  return startLevel + (1.0f - startLevel) * powShape (t, attackExp);
        case Stage::Hold:    return 1.0f;
        case Stage::Decay:   return 1.0f - (1.0f - sustainNow) * powShape (t, decayExp);
        case Stage::Sustain: return sustainNow;
        case Stage::Release: return startLevel * (1.0f - powShape (t, decayExp));
        case Stage::Idle:
        default:             return 0.0f;
    }
}

void Envelope::advance (int numSamples) noexcept
{
    while (numSamples > 0)
    {
        if (current == Stage::Idle)
        {
            sustainNow = slewFrom = sustainTarget;
            level = 0.0f;
            return;
        }

        if (current == Stage::Sustain)
        {
            slewSustain (numSamples);
            level = sustainNow;
            if (sustainNow <= 0.0f && sustainTarget <= 0.0f)
                enterStage (Stage::Idle);   // sustain turned down to 0 while held: free the voice
            return;
        }

        const int m = (int) std::min<int64_t> (numSamples, segEnd - pos);
        pos += m;
        numSamples -= m;
        slewSustain (m);
        level = levelAt();

        if (pos < segEnd)
            continue;

        switch (current)
        {
            case Stage::Attack:
                level = 1.0f;
                enterStage (Stage::Hold);
                break;
            case Stage::Hold:
                enterStage (Stage::Decay);
                break;
            case Stage::Decay:
                level = sustainNow;
                enterStage (sustainNow <= 0.0f && sustainTarget <= 0.0f ? Stage::Idle : Stage::Sustain);
                break;
            case Stage::Release:
                enterStage (Stage::Idle);
                break;
            case Stage::Idle:
            case Stage::Sustain:
            default:
                break;
        }
    }
}

} // namespace verdant
