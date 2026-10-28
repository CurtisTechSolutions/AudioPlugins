#pragma once

// A wavetable: up to kMaxFrames single-cycle frames of kFrameSize samples,
// plus band-limited mip levels for alias-free playback. Immutable once built;
// built off the audio thread and handed over through HandOff.
//
// Mip levels: level k keeps harmonics 1 .. harmonicLimit (k) = (kFrameSize / 2 >> k) - 1
// (1023, 511, 255, ... 3, 1). Level 0 is the plain kFrameSize frame (frame()); level k >= 1
// is stored oversampled at mipSize (k) = max (kMinMipSize, min (kFrameSize, 4 x (kFrameSize >> k)))
// samples: 2048, 2048, 2048, 1024, 512, 256, 256, 256, 256, 256 (2x for level 1, 4x for 2..5,
// more above). The headroom keeps the oscillator's interpolation images of the kept harmonics
// far below them (a raw saw stays under -60 dB of aliased energy at every note, MIDI 24..120,
// 44.1 kHz). The harmonic halving continues below the storage floor, so even the top of the
// keyboard has an alias-free level. DC (bin 0) is removed from every level.
// Memory: about 4.4 x the raw frames (9.2 MB at kMaxFrames, bound 10 MB).
//
// Layout: within one level, frames are contiguous with a stride of
// mipSize (level) + 1 samples (the last sample of each frame repeats sample 0).
// mipSize is always a power of two, so readers can also wrap taps with mipSize - 1.
//
// Owner: W-A.

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace verdant
{

inline constexpr int kFrameSize   = 2048;
inline constexpr int kMaxFrames   = 256;
inline constexpr int kMinMipSize  = 256;   // storage floor for the small mip levels
inline constexpr int kMaxMipLevels = 10;   // harmonic limits 1023, 511, ..., 3, 1

/** Playback headroom: the level mipLevelFor() picks keeps its highest harmonic below this many cycles per
    sample. Nyquist (0.5): the oversampled storage plus the oscillator's cubic interpolation keep the
    images low on their own, so no brightness is given away (lowering this would trade top end for margin). */
inline constexpr float kMipTopHarmonic = 0.5f;

class Wavetable
{
public:
    /** frames = numFrames x kFrameSize samples, frame-major. numFrames is clamped to kMaxFrames;
        null / empty input returns nullptr. Each frame is band-limited to harmonics <= 1023, its DC
        removed and then peak-normalised to 1 (silent frames stay silent) before the mips are built.
        Allocates and runs FFTs: never call on the audio thread. */
    static std::shared_ptr<Wavetable> fromFrames (const float* frames, int numFrames, std::string name);

    int numFrames() const noexcept { return frameCount; }
    const std::string& name() const noexcept { return tableName; }

    /** Level-0 frame, kFrameSize samples (+ 1 guard sample). */
    const float* frame (int i) const noexcept { return mipFrame (0, i); }

    int numMipLevels() const noexcept { return levels; }

    /** Samples per frame at a level: kFrameSize at level 0, else 4 x (kFrameSize >> level) capped at kFrameSize,
        never below kMinMipSize (always a power of two). */
    static int mipSize (int level) noexcept;

    /** Highest harmonic kept at a level: (kFrameSize / 2 >> level) - 1. */
    static int harmonicLimit (int level) noexcept;

    /** mipSize (level) + 1 samples (the last one repeats sample 0). Level and frame are clamped. */
    const float* mipFrame (int level, int frameIndex) const noexcept;

    /** The smallest level whose highest kept harmonic stays below Nyquist for a phase increment
        (hz / sampleRate): harmonicLimit (level) * phaseIncrement < kMipTopHarmonic (0.5), clamped to the last level. */
    int mipLevelFor (float phaseIncrement) const noexcept;

    /** Real footprint of the sample data and the object (about 4.4 x frames x kFrameSize x 4 bytes). */
    size_t memoryBytes() const noexcept;

private:
    Wavetable() = default;

    std::string tableName;
    int frameCount = 0;
    int levels = 1;
    std::array<size_t, kMaxMipLevels> levelOffset {};   // start of each level in `data`
    std::vector<float> data;                            // [level][frame][mipSize (level) + 1]
};

} // namespace verdant
