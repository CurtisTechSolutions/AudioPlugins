#include "Wavetable.h"

#include "FFT.h"

#include <algorithm>
#include <cmath>

namespace verdant
{

namespace
{
    constexpr int kNumBins = kFrameSize / 2 + 1;
    constexpr float kSilentPeak = 1.0e-9f;
}

int Wavetable::mipSize (int level) noexcept
{
    level = std::clamp (level, 0, kMaxMipLevels - 1);
    if (level == 0)
        return kFrameSize;   // frame() contract: level 0 is the plain kFrameSize frame
    // Oversampled storage (4x the critical size, capped at kFrameSize, floor kMinMipSize) so the
    // interpolation images of the kept harmonics stay far from them: 2048, 2048, 2048, 1024, 512, 256 ...
    return std::max (kMinMipSize, std::min (kFrameSize, 4 * (kFrameSize >> level)));
}

int Wavetable::harmonicLimit (int level) noexcept
{
    level = std::clamp (level, 0, kMaxMipLevels - 1);
    return ((kFrameSize / 2) >> level) - 1;
}

std::shared_ptr<Wavetable> Wavetable::fromFrames (const float* frames, int numFrames, std::string name)
{
    if (frames == nullptr || numFrames < 1)
        return nullptr;

    std::shared_ptr<Wavetable> t (new Wavetable());
    t->tableName = std::move (name);
    t->frameCount = std::min (numFrames, kMaxFrames);

    // Levels continue while at least the fundamental survives.
    t->levels = 0;
    while (t->levels < kMaxMipLevels && harmonicLimit (t->levels) >= 1)
        ++t->levels;

    size_t total = 0;
    for (int l = 0; l < t->levels; ++l)
    {
        t->levelOffset[(size_t) l] = total;
        total += (size_t) t->frameCount * (size_t) (mipSize (l) + 1);
    }
    t->data.assign (total, 0.0f);

    // One FFT per distinct storage size.
    std::vector<FFT> ffts;
    std::vector<int> fftSizes;
    for (int l = 0; l < t->levels; ++l)
        if (std::find (fftSizes.begin(), fftSizes.end(), mipSize (l)) == fftSizes.end())
        {
            fftSizes.push_back (mipSize (l));
            ffts.emplace_back (mipSize (l));
        }
    const auto fftFor = [&] (int size) -> const FFT&
    {
        return ffts[(size_t) (std::find (fftSizes.begin(), fftSizes.end(), size) - fftSizes.begin())];
    };

    std::vector<float> re ((size_t) kNumBins), im ((size_t) kNumBins);
    std::vector<float> lre ((size_t) kNumBins), lim ((size_t) kNumBins);
    std::vector<float> tmp ((size_t) kFrameSize);

    for (int f = 0; f < t->frameCount; ++f)
    {
        const float* src = frames + (size_t) f * (size_t) kFrameSize;

        // Non-finite input samples count as silence.
        for (int i = 0; i < kFrameSize; ++i)
            tmp[(size_t) i] = std::isfinite (src[i]) ? src[i] : 0.0f;

        // Level 0: remove DC and the Nyquist bin (harmonics <= 1023), then peak-normalise.
        fftFor (kFrameSize).realForward (tmp.data(), re.data(), im.data());
        re[0] = im[0] = 0.0f;
        re[(size_t) kNumBins - 1] = im[(size_t) kNumBins - 1] = 0.0f;
        fftFor (kFrameSize).realInverse (re.data(), im.data(), tmp.data());

        float peak = 0.0f;
        for (float s : tmp) peak = std::max (peak, std::fabs (s));
        const float gain = peak > kSilentPeak ? 1.0f / peak : 0.0f;

        float* level0 = t->data.data() + t->levelOffset[0] + (size_t) f * (size_t) (kFrameSize + 1);
        for (int i = 0; i < kFrameSize; ++i)
            level0[i] = tmp[(size_t) i] * gain;
        level0[kFrameSize] = level0[0];

        for (int b = 0; b < kNumBins; ++b)
        {
            re[(size_t) b] *= gain;
            im[(size_t) b] *= gain;
        }

        // Levels >= 1: truncate the spectrum and resynthesise at the smaller size. Bin magnitudes
        // scale with the transform length, so M / N keeps every harmonic's amplitude.
        for (int l = 1; l < t->levels; ++l)
        {
            const int size = mipSize (l);
            const int limit = harmonicLimit (l);
            const float scale = (float) size / (float) kFrameSize;

            std::fill (lre.begin(), lre.begin() + size / 2 + 1, 0.0f);
            std::fill (lim.begin(), lim.begin() + size / 2 + 1, 0.0f);
            for (int h = 1; h <= limit; ++h)
            {
                lre[(size_t) h] = re[(size_t) h] * scale;
                lim[(size_t) h] = im[(size_t) h] * scale;
            }

            float* dst = t->data.data() + t->levelOffset[(size_t) l] + (size_t) f * (size_t) (size + 1);
            fftFor (size).realInverse (lre.data(), lim.data(), dst);
            dst[size] = dst[0];
        }
    }

    return t;
}

const float* Wavetable::mipFrame (int level, int frameIndex) const noexcept
{
    level = std::clamp (level, 0, levels - 1);
    frameIndex = std::clamp (frameIndex, 0, frameCount - 1);
    return data.data() + levelOffset[(size_t) level] + (size_t) frameIndex * (size_t) (mipSize (level) + 1);
}

int Wavetable::mipLevelFor (float phaseIncrement) const noexcept
{
    const float inc = std::fabs (phaseIncrement);
    for (int l = 0; l < levels; ++l)
        if ((float) harmonicLimit (l) * inc < kMipTopHarmonic)
            return l;
    return levels - 1;
}

size_t Wavetable::memoryBytes() const noexcept
{
    return data.capacity() * sizeof (float) + sizeof (*this) + tableName.capacity();
}

} // namespace verdant
