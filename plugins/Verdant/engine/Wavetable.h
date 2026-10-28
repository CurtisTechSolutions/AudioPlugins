#pragma once

// A wavetable: up to kMaxFrames single-cycle frames of kFrameSize samples,
// plus band-limited mip levels for alias-free playback. Immutable once built;
// built off the audio thread and handed over through HandOff.
//
// Owner: W-A. FOUNDATION STUB: one mip level (level 0 with the wrap guard
// sample); mipFrame() returns level 0 for every level and mipLevelFor()
// returns 0.

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace verdant
{

inline constexpr int kFrameSize = 2048;
inline constexpr int kMaxFrames = 256;

class Wavetable
{
public:
    /** frames = numFrames x kFrameSize samples, frame-major. numFrames is clamped to kMaxFrames; null / empty input returns nullptr. */
    static std::shared_ptr<Wavetable> fromFrames (const float* frames, int numFrames, std::string name);

    int numFrames() const noexcept { return frameCount; }
    const std::string& name() const noexcept { return tableName; }

    /** Level-0 frame, kFrameSize samples. */
    const float* frame (int i) const noexcept;

    int numMipLevels() const noexcept;

    /** kFrameSize + 1 samples (the last one repeats sample 0 so interpolation never wraps). */
    const float* mipFrame (int level, int frame) const noexcept;

    /** Mip level for a phase increment (hz / sampleRate): the highest kept harmonic stays below Nyquist. */
    int mipLevelFor (float phaseIncrement) const noexcept;

    size_t memoryBytes() const noexcept;

    // Each mip level k keeps harmonics <= (kFrameSize / 2) / 2^k.

private:
    Wavetable() = default;

    std::string tableName;
    int frameCount = 0;
    int levels = 1;
    std::vector<float> data;   // [level][frame][kFrameSize + 1]
};

} // namespace verdant
