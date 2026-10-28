#include "Fx.h"

namespace verdant
{

void FxRack::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    maxBlockSize = maxBlock;
}

void FxRack::reset() {}

void FxRack::setParams (const ParamValues&, const Transport&) {}

void FxRack::process (float*, float*, int) {}

float FxRack::tailSeconds() const
{
    return 0.0f;
}

} // namespace verdant
