// Wavetable: frame storage, mip levels (band-limit, amplitude, size), level choice, memory, DC.

#include "TestHarness.h"

#include "engine/FFT.h"
#include "engine/Wavetable.h"

#include <ap/Math.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace verdant;

namespace
{
    std::vector<float> sineFrames (int numFrames, double amp = 1.0, double dc = 0.0)
    {
        std::vector<float> v ((size_t) numFrames * kFrameSize);
        for (int f = 0; f < numFrames; ++f)
            for (int i = 0; i < kFrameSize; ++i)
                v[(size_t) f * kFrameSize + (size_t) i] = (float) (dc + amp * std::sin (ap::kTwoPiD * i / kFrameSize));
        return v;
    }

    std::vector<float> noiseFrames (int numFrames, uint32_t seed)
    {
        ap::Rng rng;
        rng.state = seed;
        std::vector<float> v ((size_t) numFrames * kFrameSize);
        for (auto& s : v) s = rng.nextBipolar();
        return v;
    }

    /** Power per harmonic bin of one mip frame (size samples). */
    std::vector<double> binPower (const float* frame, int size)
    {
        FFT fft (size);
        std::vector<float> in (frame, frame + size), re ((size_t) size / 2 + 1), im ((size_t) size / 2 + 1);
        fft.realForward (in.data(), re.data(), im.data());
        std::vector<double> p ((size_t) size / 2 + 1);
        for (size_t k = 0; k < p.size(); ++k)
            p[k] = (double) re[k] * re[k] + (double) im[k] * im[k];
        return p;
    }
}

AP_TEST (wavetable_from_frames_shape)
{
    CHECK (Wavetable::fromFrames (nullptr, 4, "x") == nullptr);
    const auto one = sineFrames (1);
    CHECK (Wavetable::fromFrames (one.data(), 0, "x") == nullptr);

    const auto many = noiseFrames (300, 7u);
    const auto t = Wavetable::fromFrames (many.data(), 300, "Many");
    CHECK (t != nullptr);
    CHECK_EQ (t->numFrames(), kMaxFrames);
    CHECK (t->name() == "Many");

    // Harmonic limits halve per level down to the fundamental: 10 levels. Level 0 is the plain 2048-sample
    // frame; the others are stored oversampled (4x, capped at 2048, floor 256).
    CHECK_EQ (t->numMipLevels(), 10);
    const int sizes[10]  = { 2048, 2048, 2048, 1024, 512, 256, 256, 256, 256, 256 };
    const int limits[10] = { 1023, 511, 255, 127, 63, 31, 15, 7, 3, 1 };
    for (int l = 0; l < 10; ++l)
    {
        CHECK_EQ (Wavetable::mipSize (l), sizes[l]);
        CHECK_EQ (Wavetable::harmonicLimit (l), limits[l]);
        CHECK (Wavetable::mipSize (l) >= 64);
        CHECK (Wavetable::harmonicLimit (l) <= Wavetable::mipSize (l) / 2 - 1);
    }

    // Frames of a level are contiguous with stride mipSize + 1, every frame carries the wrap guard.
    for (int l = 0; l < t->numMipLevels(); ++l)
    {
        const int size = Wavetable::mipSize (l);
        CHECK (t->mipFrame (l, 1) == t->mipFrame (l, 0) + size + 1);
        bool guards = true;
        for (int f = 0; f < t->numFrames(); f += 17)
            guards = guards && t->mipFrame (l, f)[size] == t->mipFrame (l, f)[0];
        CHECK (guards);
    }
    CHECK (t->frame (3) == t->mipFrame (0, 3));
    // Out-of-range requests clamp.
    CHECK (t->mipFrame (99, 999) == t->mipFrame (t->numMipLevels() - 1, t->numFrames() - 1));
}

AP_TEST (wavetable_sine_survives_every_level)
{
    const auto frames = sineFrames (2, 0.4);   // also checks the peak normalisation (0.4 -> 1)
    const auto t = Wavetable::fromFrames (frames.data(), 2, "Sine");
    CHECK (t != nullptr);
    for (int l = 0; l < t->numMipLevels(); ++l)
    {
        const int size = Wavetable::mipSize (l);
        const float* m = t->mipFrame (l, 1);
        double peak = 0.0, maxErr = 0.0;
        for (int i = 0; i < size; ++i)
        {
            peak = std::max (peak, (double) std::fabs (m[i]));
            maxErr = std::max (maxErr, std::fabs (m[i] - std::sin (ap::kTwoPiD * i / size)));
        }
        CHECK_NEAR (peak, 1.0, 1.0e-3);
        CHECK (maxErr < 1.0e-3);   // amplitude and phase preserved
    }
}

AP_TEST (wavetable_levels_band_limited)
{
    const auto frames = noiseFrames (3, 12345u);
    const auto t = Wavetable::fromFrames (frames.data(), 3, "Noise");
    CHECK (t != nullptr);

    for (int l = 0; l < t->numMipLevels(); ++l)
    {
        const int size = Wavetable::mipSize (l);
        const int limit = Wavetable::harmonicLimit (l);
        for (int f = 0; f < 3; ++f)
        {
            const auto p = binPower (t->mipFrame (l, f), size);
            double inBand = 0.0, outBand = 0.0;
            for (size_t k = 0; k < p.size(); ++k)
                ((int) k >= 1 && (int) k <= limit ? inBand : outBand) += p[k];
            CHECK (inBand > 0.0);
            CHECK (10.0 * std::log10 ((outBand + 1.0e-30) / inBand) < -80.0);   // nothing above the limit, no DC
        }
    }
}

AP_TEST (wavetable_level_harmonics_match_level0)
{
    // Every kept harmonic has the same amplitude at every level (decimation scaling is right).
    const auto frames = noiseFrames (1, 99u);
    const auto t = Wavetable::fromFrames (frames.data(), 1, "Noise");
    const auto p0 = binPower (t->frame (0), kFrameSize);
    for (int l = 1; l < t->numMipLevels(); ++l)
    {
        const int size = Wavetable::mipSize (l);
        const auto p = binPower (t->mipFrame (l, 0), size);
        const double scale = (double) kFrameSize / (double) size;
        double worst = 0.0;
        for (int h = 1; h <= Wavetable::harmonicLimit (l); ++h)
        {
            const double a0 = std::sqrt (p0[(size_t) h]);
            const double a = std::sqrt (p[(size_t) h]) * scale;
            worst = std::max (worst, std::fabs (a - a0) / (a0 + 1.0e-9));
        }
        CHECK (worst < 1.0e-3);
    }
}

AP_TEST (wavetable_mip_level_choice)
{
    const auto frames = sineFrames (1);
    const auto t = Wavetable::fromFrames (frames.data(), 1, "Sine");
    const int last = t->numMipLevels() - 1;

    int prev = 0;
    bool monotone = true, belowNyquist = true;
    for (double inc = 1.0e-6; inc < 0.5; inc *= 1.01)
    {
        const int l = t->mipLevelFor ((float) inc);
        monotone = monotone && l >= prev;
        belowNyquist = belowNyquist && (Wavetable::harmonicLimit (l) * inc < 0.5);
        // ... and it is the smallest such level.
        if (l > 0)
            belowNyquist = belowNyquist && (Wavetable::harmonicLimit (l - 1) * inc >= 0.5);
        prev = l;
    }
    CHECK (monotone);
    CHECK (belowNyquist);
    CHECK_EQ (t->mipLevelFor (0.0f), 0);
    CHECK_EQ (t->mipLevelFor (0.49f), last);
    CHECK_EQ (t->mipLevelFor (2.0f), last);   // clamped
    // MIDI 108 at 44.1 kHz: 4186 Hz -> 5.27 harmonics fit -> the 3-harmonic level.
    CHECK_EQ (Wavetable::harmonicLimit (t->mipLevelFor (4186.01f / 44100.0f)), 3);
    // A low note keeps the full spectrum.
    CHECK_EQ (t->mipLevelFor (20.0f / 48000.0f), 0);
}

AP_TEST (wavetable_memory_bound)
{
    const auto frames = noiseFrames (kMaxFrames, 3u);
    const auto t = Wavetable::fromFrames (frames.data(), kMaxFrames, "Big");
    const double bytes = (double) t->memoryBytes();
    const double raw = (double) kMaxFrames * kFrameSize * sizeof (float);
    std::printf ("    256-frame table: %.2f MB (%.2f x raw)\n", bytes / 1.0e6, bytes / raw);
    CHECK (bytes <= 10.0e6);             // <= 10 MB at kMaxFrames
    CHECK (bytes >= 4.2 * raw);          // reports the real footprint (all levels) ...
    CHECK (bytes <= 4.5 * raw);          // ... of about 4.4 x the raw frames
}

AP_TEST (wavetable_dc_removed_and_normalised)
{
    // Pulse-like frame with a large DC offset, a scaled sine, and a silent frame.
    std::vector<float> v ((size_t) 3 * kFrameSize, 0.0f);
    for (int i = 0; i < kFrameSize; ++i)
    {
        v[(size_t) i] = i < kFrameSize / 4 ? 1.0f : -1.0f;
        v[(size_t) kFrameSize + (size_t) i] = 0.5f + 0.25f * (float) std::sin (ap::kTwoPiD * 3 * i / kFrameSize);
    }
    const auto t = Wavetable::fromFrames (v.data(), 3, "DC");
    for (int l = 0; l < t->numMipLevels(); ++l)
    {
        const int size = Wavetable::mipSize (l);
        for (int f = 0; f < 2; ++f)
        {
            double mean = 0.0;
            for (int i = 0; i < size; ++i) mean += t->mipFrame (l, f)[i];
            CHECK (std::fabs (mean / size) < 1.0e-5);
        }
    }
    for (int f = 0; f < 2; ++f)
    {
        float peak = 0.0f;
        for (int i = 0; i < kFrameSize; ++i) peak = std::max (peak, std::fabs (t->frame (f)[i]));
        CHECK_NEAR (peak, 1.0, 1.0e-5);
    }
    float silentPeak = 0.0f;
    for (int l = 0; l < t->numMipLevels(); ++l)
        for (int i = 0; i <= Wavetable::mipSize (l); ++i)
            silentPeak = std::max (silentPeak, std::fabs (t->mipFrame (l, 2)[i]));
    CHECK (silentPeak == 0.0f);   // silent frames stay silent
}

AP_TEST (wavetable_non_finite_input_is_silence)
{
    std::vector<float> v ((size_t) kFrameSize, std::nanf (""));
    v[5] = INFINITY;
    const auto t = Wavetable::fromFrames (v.data(), 1, "NaN");
    bool finite = true;
    for (int l = 0; l < t->numMipLevels(); ++l)
        for (int i = 0; i <= Wavetable::mipSize (l); ++i)
            finite = finite && std::isfinite (t->mipFrame (l, 0)[i]);
    CHECK (finite);
}
