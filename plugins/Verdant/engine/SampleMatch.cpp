#include "SampleMatch.h"

#include <cmath>
#include <vector>

namespace verdant
{

SampleMatchResult matchSample (const float*, int, double, const ProgressFn& progress)
{
    SampleMatchResult r;
    r.values = defaultValues();

    std::vector<float> sine ((size_t) kFrameSize);
    for (int i = 0; i < kFrameSize; ++i)
        sine[(size_t) i] = 0.95f * (float) std::sin (6.283185307179586 * (double) i / (double) kFrameSize);
    r.table = Wavetable::fromFrames (sine.data(), 1, "Matched");
    r.notes = "Sample matching is not implemented yet";

    if (progress) progress (1.0f);
    return r;
}

} // namespace verdant
