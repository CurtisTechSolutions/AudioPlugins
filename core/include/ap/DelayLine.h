#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ap
{

/** Fixed-maximum delay line with integer and linearly-interpolated taps. */
class DelayLine
{
public:
    void prepare (int maxDelaySamples)
    {
        int size = 16;
        while (size < maxDelaySamples + 2) size <<= 1;
        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        reset();
    }

    void reset() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
    }

    int maxDelay() const noexcept { return mask - 1; }

    inline void push (float x) noexcept
    {
        buffer[(size_t) writePos] = x;
        writePos = (writePos + 1) & mask;
    }

    /** The sample pushed `delay` pushes ago: read(0) is the most recent push. */
    inline float read (int delay) const noexcept
    {
        return buffer[(size_t) ((writePos - 1 - delay) & mask)];
    }

    inline float readFractional (float delay) const noexcept
    {
        const int   d0 = (int) delay;
        const float t  = delay - (float) d0;
        const float a  = read (d0);
        const float b  = read (d0 + 1);
        return a + (b - a) * t;
    }

    /** Convenience: push then read; delay 0 returns x itself. */
    inline float process (float x, int delay) noexcept
    {
        push (x);
        return read (delay);
    }

private:
    std::vector<float> buffer;
    int mask = 15;
    int writePos = 0;
};

} // namespace ap
