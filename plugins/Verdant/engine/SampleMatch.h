#pragma once

// Sample-to-patch: analyses a dropped sample and builds a user wavetable plus
// a starting patch that gets close to it. Runs off the audio thread.
//
// Owner: W-B. FOUNDATION STUB: returns defaultValues() and a one-frame sine table.

#include "Params.h"
#include "Wavetable.h"

#include <functional>
#include <memory>
#include <string>

namespace verdant
{

struct SampleMatchResult
{
    std::shared_ptr<Wavetable> table;
    ParamValues values;          // a full patch starting from defaultValues()
    float detectedHz = 0;
    float confidence = 0;
    std::string notes;
};

using ProgressFn = std::function<bool (float progress01)>;   // return false to cancel

SampleMatchResult matchSample (const float* mono, int numSamples, double sampleRate, const ProgressFn& progress);

} // namespace verdant
