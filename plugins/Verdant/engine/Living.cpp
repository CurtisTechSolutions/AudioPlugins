#include "Living.h"

namespace verdant
{

void LivingVoice::prepare (double sampleRate)
{
    sr = sampleRate;
}

void LivingVoice::noteOn (uint32_t, uint32_t, float, float)
{
    current = LivingOffsets();
}

void LivingVoice::advance (int) noexcept
{
}

const LivingOffsets& LivingVoice::offsets() const noexcept
{
    return current;
}

} // namespace verdant
