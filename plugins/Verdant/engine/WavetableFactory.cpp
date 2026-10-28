#include "WavetableFactory.h"

#include "FFT.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace verdant
{

namespace
{
    constexpr const char* kNames[kNumFactoryTables] = {
        "Basic Shapes", "Analog Saws", "Pulse Width", "Harmonic Stack", "Odd Harmonics", "Formant Vowels", "Bell", "Organ",
        "Digital Glass", "FM Sweep", "Sync Sweep", "Noise Bloom", "Growl", "Pluck Decay", "Choir", "Sub Bass"
    };

    constexpr int kFrames = 32;
    constexpr int kMaxHarmonic = kFrameSize / 2 - 1;
    constexpr double kTwoPi = 6.283185307179586476925;

    using Frame = std::vector<float>;

    /** Sum of sines: amp (h) for harmonics 1..kMaxHarmonic. */
    Frame additive (const FFT& fft, const std::function<double (int)>& amp)
    {
        std::vector<float> re ((size_t) kFrameSize / 2 + 1, 0.0f), im ((size_t) kFrameSize / 2 + 1, 0.0f);
        for (int h = 1; h <= kMaxHarmonic; ++h)
            im[(size_t) h] = (float) (-amp (h) * (double) kFrameSize * 0.5);
        Frame out ((size_t) kFrameSize);
        fft.realInverse (re.data(), im.data(), out.data());
        return out;
    }

    /** Naive time-domain cycle (phase 0..1) band-limited to kMaxHarmonic, DC removed. */
    Frame bandLimited (const FFT& fft, const std::function<double (double)>& wave, int maxHarmonic = kMaxHarmonic)
    {
        Frame x ((size_t) kFrameSize);
        for (int i = 0; i < kFrameSize; ++i)
            x[(size_t) i] = (float) wave ((double) i / (double) kFrameSize);

        std::vector<float> re ((size_t) kFrameSize / 2 + 1), im ((size_t) kFrameSize / 2 + 1);
        fft.realForward (x.data(), re.data(), im.data());
        re[0] = im[0] = 0.0f;
        for (int h = maxHarmonic + 1; h <= kFrameSize / 2; ++h)
            re[(size_t) h] = im[(size_t) h] = 0.0f;
        fft.realInverse (re.data(), im.data(), x.data());
        return x;
    }

    Frame mix (const Frame& a, const Frame& b, double t)
    {
        Frame out (a.size());
        for (size_t i = 0; i < a.size(); ++i)
            out[i] = (float) ((1.0 - t) * a[i] + t * b[i]);
        return out;
    }

    double odd (int h) { return (h & 1) != 0 ? 1.0 : 0.0; }

    double formant (double hz, double centre, double width) { const double d = (hz - centre) / width; return std::exp (-0.5 * d * d); }

    // Vowel formants (F1, F2) for a, e, i, o, u.
    constexpr double kVowels[5][2] = { { 800.0, 1150.0 }, { 400.0, 1600.0 }, { 300.0, 2300.0 }, { 450.0, 800.0 }, { 325.0, 700.0 } };

    Frame makeFrame (const FFT& fft, int table, int f)
    {
        const double t = (double) f / (double) (kFrames - 1);   // 0..1 across the table

        switch (table)
        {
            case 0:   // Basic Shapes: sine -> triangle -> saw -> square -> narrowing pulse
            {
                const auto sine = [&] { return additive (fft, [] (int h) { return h == 1 ? 1.0 : 0.0; }); };
                const auto tri  = [&] { return additive (fft, [] (int h) { return odd (h) * 8.0 / (9.8696044 * h * h) * (((h - 1) / 2) % 2 == 0 ? 1.0 : -1.0); }); };
                const auto saw  = [&] { return additive (fft, [] (int h) { return 2.0 / (3.14159265 * h); }); };
                const auto sq   = [&] { return additive (fft, [] (int h) { return odd (h) * 4.0 / (3.14159265 * h); }); };
                const auto pulse = [&] (double w) { return bandLimited (fft, [w] (double p) { return p < w ? 1.0 : -1.0; }); };

                const double seg = t * 4.0;
                if (seg <= 1.0) return mix (sine(), tri(), seg);
                if (seg <= 2.0) return mix (tri(), saw(), seg - 1.0);
                if (seg <= 3.0) return mix (saw(), sq(), seg - 2.0);
                return pulse (0.5 - 0.42 * (seg - 3.0));
            }
            case 1:   // Analog Saws: saw opening up from dark to bright
                return additive (fft, [t] (int h) { return 1.0 / h / (1.0 + std::pow (h / (2.0 + 120.0 * t * t), 2.0)); });
            case 2:   // Pulse Width
                return bandLimited (fft, [t] (double p) { return p < 0.5 - 0.45 * t ? 1.0 : -1.0; });
            case 3:   // Harmonic Stack: one more harmonic per frame
                return additive (fft, [f] (int h) { return h <= f + 1 ? 1.0 / std::sqrt ((double) (f + 1)) : 0.0; });
            case 4:   // Odd Harmonics: steep to bright roll-off
                return additive (fft, [t] (int h) { return odd (h) / std::pow ((double) h, 2.0 - 1.2 * t); });
            case 5:   // Formant Vowels: a -> e -> i -> o -> u over a 110 Hz harmonic comb
            {
                const double v = t * 4.0;
                const int v0 = std::min (3, (int) v);
                const double vt = v - v0;
                return additive (fft, [=] (int h)
                {
                    const double hz = 110.0 * h;
                    const double f1 = kVowels[v0][0] + vt * (kVowels[v0 + 1][0] - kVowels[v0][0]);
                    const double f2 = kVowels[v0][1] + vt * (kVowels[v0 + 1][1] - kVowels[v0][1]);
                    return (formant (hz, f1, 90.0) + 0.6 * formant (hz, f2, 120.0)) / std::sqrt ((double) h);
                });
            }
            case 6:   // Bell: sparse upper partials fading in
                return additive (fft, [t] (int h) { return h == 1 ? 1.0 : (h == 3 || h == 7 || h == 11 || h == 17) ? (0.2 + 0.6 * t) / std::sqrt ((double) h) : 0.0; });
            case 7:   // Organ: drawbar-style partials
                return additive (fft, [t] (int h) { return h == 1 ? 1.0 : h == 2 ? 0.7 * t : h == 3 ? 0.5 : h == 4 ? 0.4 * t : h == 6 ? 0.3 * t : h == 8 ? 0.25 * t * t : 0.0; });
            case 8:   // Digital Glass: a moving band of high harmonics
                return additive (fft, [t] (int h) { return h == 1 ? 0.6 : 0.5 * formant ((double) h, 8.0 + 40.0 * t, 3.0); });
            case 9:   // FM Sweep: 2:1 FM, index rising
                return bandLimited (fft, [t] (double p) { return std::sin (kTwoPi * p + 4.0 * t * std::sin (kTwoPi * 2.0 * p)); });
            case 10:  // Sync Sweep: hard-synced saw, slave ratio 1 -> 5
                return bandLimited (fft, [t] (double p) { const double s = p * (1.0 + 4.0 * t); return 2.0 * (s - std::floor (s)) - 1.0; });
            case 11:  // Noise Bloom: seeded random partial amplitudes, opening up
                return additive (fft, [t] (int h)
                {
                    uint32_t x = (uint32_t) h * 2654435761u ^ 0x5EEDu;
                    x ^= x >> 15; x *= 0x2C1B3C6Du; x ^= x >> 12;
                    const double r = (double) (x & 0xFFFF) / 65535.0;
                    return h == 1 ? 1.0 : r / h * std::exp (-(double) h / (4.0 + 200.0 * t));
                });
            case 12:  // Growl: increasingly driven saw
                return bandLimited (fft, [t] (double p) { return std::tanh ((1.0 + 8.0 * t) * (2.0 * p - 1.0)) + 0.3 * t * std::sin (kTwoPi * 3.0 * p); });
            case 13:  // Pluck Decay: bright to dull
                return additive (fft, [t] (int h) { return 1.0 / h * std::exp (-(double) h * (0.01 + 0.4 * t)); });
            case 14:  // Choir: soft "ah" -> "oo" formants
                return additive (fft, [t] (int h)
                {
                    const double hz = 220.0 * h;
                    return (formant (hz, 700.0 - 350.0 * t, 130.0) + 0.5 * formant (hz, 1100.0 - 300.0 * t, 160.0) + 0.15 * formant (hz, 2600.0, 300.0)) / std::sqrt ((double) h);
                });
            case 15:  // Sub Bass: sine with a little 2nd / 3rd harmonic
            default:
                return additive (fft, [t] (int h) { return h == 1 ? 1.0 : h == 2 ? 0.3 * t : h == 3 ? 0.15 * t : 0.0; });
        }
    }
}

const char* const* factoryTableNames()
{
    return kNames;
}

std::shared_ptr<Wavetable> makeFactoryTable (int index)
{
    index = std::clamp (index, 0, kNumFactoryTables - 1);
    const FFT fft (kFrameSize);

    std::vector<float> data ((size_t) kFrames * (size_t) kFrameSize);
    for (int f = 0; f < kFrames; ++f)
    {
        Frame frame = makeFrame (fft, index, f);

        float peak = 0.0f;
        for (float s : frame) peak = std::max (peak, std::fabs (s));
        const float gain = peak > 1.0e-9f ? 0.95f / peak : 0.0f;
        for (int i = 0; i < kFrameSize; ++i)
            data[(size_t) f * (size_t) kFrameSize + (size_t) i] = frame[(size_t) i] * gain;
    }

    return Wavetable::fromFrames (data.data(), kFrames, kNames[index]);
}

} // namespace verdant
