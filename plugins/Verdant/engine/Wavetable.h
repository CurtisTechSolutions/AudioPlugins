#pragma once

// A wavetable: up to kMaxFrames single-cycle frames of kFrameSize samples,
// plus band-limited mip levels for alias-free playback. Immutable once built;
// built off the audio thread and handed over through HandOff.
//
// Mip levels: level k keeps harmonics 1 .. harmonicLimit (k) = (kFrameSize / 2 >> k) - 1
// (1023, 511, 255, ... 3, 1) and is stored at mipSize (k) = max (kMinMipSize, kFrameSize >> k)
// samples. The halving continues below the storage floor (levels 4..9 are all
// stored at 128 samples, 4x+ oversampled, which keeps linear-interpolation
// images low), so even the top of the keyboard has an alias-free level: a
// note's top kept harmonic is always below Nyquist when the oscillator reads
// the level mipLevelFor() names. DC (bin 0) is removed from every level.
//
// Layout: within one level, frames are contiguous with a stride of
// mipSize (level) + 1 samples (the last sample of each frame repeats sample 0,
// so linear interpolation never has to wrap).
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
inline constexpr int kMinMipSize  = 128;   // storage floor for the small mip levels
inline constexpr int kMaxMipLevels = 10;   // harmonic limits 1023, 511, ..., 3, 1

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

    /** Samples per frame at a level: kFrameSize >> level, never below kMinMipSize (always a power of two). */
    static int mipSize (int level) noexcept;

    /** Highest harmonic kept at a level: (kFrameSize / 2 >> level) - 1. */
    static int harmonicLimit (int level) noexcept;

    /** mipSize (level) + 1 samples (the last one repeats sample 0). Level and frame are clamped. */
    const float* mipFrame (int level, int frameIndex) const noexcept;

    /** The smallest level whose highest kept harmonic stays below Nyquist for a phase increment
        (hz / sampleRate): harmonicLimit (level) * phaseIncrement < 0.5, clamped to the last level. */
    int mipLevelFor (float phaseIncrement) const noexcept;

    /** Real footprint of the sample data and the object (about 2.25 x frames x kFrameSize x 4 bytes). */
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
