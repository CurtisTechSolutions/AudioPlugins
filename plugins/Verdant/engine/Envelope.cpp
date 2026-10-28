#include "Envelope.h"

#include <algorithm>

namespace verdant
{

void Envelope::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
}

void Envelope::set (const EnvParams& p)
{
    params = p;
    if (current == Stage::Sustain || current == Stage::Decay)
        latched.sustain = std::clamp (p.sustain, 0.0f, 1.0f);   // sustain level follows live
}

void Envelope::noteOn()
{
    latched = params;
    latched.sustain = std::clamp (latched.sustain, 0.0f, 1.0f);
    current = Stage::Attack;
    stagePos = 0.0;
}

void Envelope::noteOff()
{
    if (current == Stage::Idle || current == Stage::Release)
        return;
    current = Stage::Release;
    const double samples = std::max (1.0, (double) latched.releaseMs * 0.001 * sr);
    releaseStep = (float) ((double) level / samples);
}

void Envelope::kill()
{
    current = Stage::Idle;
    level = 0.0f;
}

bool Envelope::isActive() const noexcept
{
    return current != Stage::Idle;
}

Envelope::Stage Envelope::stage() const noexcept
{
    return current;
}

float Envelope::next() noexcept
{
    switch (current)
    {
        case Stage::Idle:
            level = 0.0f;
            break;

        case Stage::Attack:
        {
            const double len = (double) latched.attackMs * 0.001 * sr;
            if (len < 1.0) { level = 1.0f; current = Stage::Hold; stagePos = 0.0; break; }
            level = (float) std::min (1.0, (double) level + 1.0 / len);
            if (level >= 1.0f) { current = Stage::Hold; stagePos = 0.0; }
            break;
        }

        case Stage::Hold:
            level = 1.0f;
            stagePos += 1.0;
            if (stagePos >= (double) latched.holdMs * 0.001 * sr) { current = Stage::Decay; stagePos = 0.0; }
            break;

        case Stage::Decay:
        {
            const double len = std::max (1.0, (double) latched.decayMs * 0.001 * sr);
            level = (float) std::max ((double) latched.sustain, (double) level - (1.0 - (double) latched.sustain) / len);
            if (level <= latched.sustain) { level = latched.sustain; current = Stage::Sustain; }
            break;
        }

        case Stage::Sustain:
            level = latched.sustain;
            break;

        case Stage::Release:
            level -= releaseStep;
            if (level <= 0.0f) { level = 0.0f; current = Stage::Idle; }
            break;
    }
    return level;
}

float Envelope::value() const noexcept
{
    return level;
}

void Envelope::skip (int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
        next();
}

} // namespace verdant
