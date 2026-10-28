#pragma once

// The 16 factory wavetables, generated additively / procedurally (deterministic,
// no third-party content). Choice index for {o}_table: 0..15 = these tables,
// 16 = "User" (the oscillator's user table from the patch extras).
//
// Owner: W-A. FOUNDATION STUB: 32 frames per table from simple harmonic
// recipes; "Basic Shapes" frame 0 is a pure sine, morphing to triangle, saw,
// square and narrowing pulses.

#include "Wavetable.h"

#include <memory>

namespace verdant
{

inline constexpr int kNumFactoryTables = 16;

/** "Basic Shapes", "Analog Saws", "Pulse Width", "Harmonic Stack", "Odd Harmonics", "Formant Vowels", "Bell", "Organ",
    "Digital Glass", "FM Sweep", "Sync Sweep", "Noise Bloom", "Growl", "Pluck Decay", "Choir", "Sub Bass" */
const char* const* factoryTableNames();

/** Builds a factory table (allocates; message thread / static init only). Index is clamped to 0..15. */
std::shared_ptr<Wavetable> makeFactoryTable (int index);

} // namespace verdant
