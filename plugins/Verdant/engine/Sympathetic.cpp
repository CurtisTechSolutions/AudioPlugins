#include "Sympathetic.h"

namespace verdant
{

void Sympathetic::prepare (double sampleRate, int)
{
    sr = sampleRate;
}

void Sympathetic::reset() {}

void Sympathetic::setParams (float amount01, int, float, float, float, int, bool, const ap::Scale&)
{
    amount = amount01;
}

void Sympathetic::noteOn (int) {}

void Sympathetic::process (float*, float*, int) {}

} // namespace verdant
