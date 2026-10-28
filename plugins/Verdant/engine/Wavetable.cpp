#include "Wavetable.h"

#include <algorithm>

namespace verdant
{

namespace
{
    constexpr int kStride = kFrameSize + 1;
}

std::shared_ptr<Wavetable> Wavetable::fromFrames (const float* frames, int numFrames, std::string name)
{
    if (frames == nullptr || numFrames < 1)
        return nullptr;

    std::shared_ptr<Wavetable> t (new Wavetable());
    t->tableName = std::move (name);
    t->frameCount = std::min (numFrames, kMaxFrames);
    t->levels = 1;
    t->data.resize ((size_t) t->frameCount * (size_t) kStride);

    for (int f = 0; f < t->frameCount; ++f)
    {
        float* dst = t->data.data() + (size_t) f * (size_t) kStride;
        std::copy (frames + (size_t) f * (size_t) kFrameSize, frames + (size_t) (f + 1) * (size_t) kFrameSize, dst);
        dst[kFrameSize] = dst[0];
    }
    return t;
}

const float* Wavetable::frame (int i) const noexcept
{
    return mipFrame (0, i);
}

int Wavetable::numMipLevels() const noexcept
{
    return levels;
}

const float* Wavetable::mipFrame (int level, int frameIndex) const noexcept
{
    level = std::clamp (level, 0, levels - 1);
    frameIndex = std::clamp (frameIndex, 0, frameCount - 1);
    return data.data() + ((size_t) level * (size_t) frameCount + (size_t) frameIndex) * (size_t) kStride;
}

int Wavetable::mipLevelFor (float) const noexcept
{
    return 0;
}

size_t Wavetable::memoryBytes() const noexcept
{
    return data.size() * sizeof (float) + sizeof (*this);
}

} // namespace verdant
