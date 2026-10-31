#pragma once

// The 16 factory wavetables, generated additively / procedurally from
// per-frame harmonic recipes (deterministic, original, no third-party
// content), 32 frames each, all built through Wavetable::fromFrames (which
// band-limits, removes DC and peak-normalises every frame).
// Choice index for {o}_table: 0..15 = these tables, 16 = "User" (the
// oscillator's user table from the patch extras).
//
// Owner: W-A.

#include "Wavetable.h"

#include <memory>

namespace verdant
{

inline constexpr int kNumFactoryTables = 16;
inline constexpr int kFactoryTableFrames = 32;

/** "Basic Shapes", "Analog Saws", "Pulse Width", "Harmonic Stack", "Odd Harmonics", "Formant Vowels", "Bell", "Organ",
    "Digital Glass", "FM Sweep", "Sync Sweep", "Noise Bloom", "Growl", "Pluck Decay", "Choir", "Sub Bass" */
const char* const* factoryTableNames();

/** Builds a fresh factory table (allocates, runs FFTs; message thread / static init only). An index outside 0..15 builds table 0.
    "Basic Shapes": frame 0 = sine, frame 6 = triangle, frame 12 = saw, frame 19 = square, frame 25 = 25 % pulse,
    frame 31 = 12.5 % pulse, morphing smoothly in between. */
std::shared_ptr<Wavetable> makeFactoryTable (int index);

/** The shared, immutable copy of a factory table: built on first request and cached for the life of the
    process (guarded by a mutex; call from the message thread / setup code, never the audio thread).
    An index outside 0..15 returns table 0. Repeated calls return the same pointer. */
std::shared_ptr<const Wavetable> factoryTable (int index);

/** Frame index of the pure saw in "Basic Shapes" (useful for tests and presets). */
inline constexpr int kBasicShapesSawFrame = 12;

} // namespace verdant
