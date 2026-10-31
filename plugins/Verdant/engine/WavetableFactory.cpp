#include "WavetableFactory.h"

#include "FFT.h"

#include <ap/Math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <vector>

namespace verdant
{

namespace
{
    constexpr const char* kNames[kNumFactoryTables] = {
        "Basic Shapes", "Analog Saws", "Pulse Width", "Harmonic Stack", "Odd Harmonics", "Formant Vowels", "Bell", "Organ",
        "Digital Glass", "FM Sweep", "Sync Sweep", "Noise Bloom", "Growl", "Pluck Decay", "Choir", "Sub Bass"
    };

    constexpr int kFrames = kFactoryTableFrames;
    constexpr int kMaxHarmonic = kFrameSize / 2 - 1;      // 1023
    constexpr int kOversample = 16;                       // time-domain recipes are sampled 16x, then band-limited
    constexpr double kPi = ap::kPiD;
    constexpr double kTwoPi = ap::kTwoPiD;

    /** One frame as a harmonic series: x (p) = sum_h a[h] cos (2 pi h p) + b[h] sin (2 pi h p), h = 1..1023. */
    struct Spectrum
    {
        std::vector<double> a = std::vector<double> ((size_t) kMaxHarmonic + 1, 0.0);
        std::vector<double> b = std::vector<double> ((size_t) kMaxHarmonic + 1, 0.0);

        /** amp x sin (2 pi h p + phase) */
        void add (int h, double amp, double phase = 0.0)
        {
            if (h < 1 || h > kMaxHarmonic) return;
            b[(size_t) h] += amp * std::cos (phase);
            a[(size_t) h] += amp * std::sin (phase);
        }
    };

    Spectrum mix (const Spectrum& x, const Spectrum& y, double t)
    {
        Spectrum s;
        for (size_t h = 0; h < s.a.size(); ++h)
        {
            s.a[h] = (1.0 - t) * x.a[h] + t * y.a[h];
            s.b[h] = (1.0 - t) * x.b[h] + t * y.b[h];
        }
        return s;
    }

    /** Deterministic per-(table, index) unit value. */
    double rand01 (uint32_t table, uint32_t index)
    {
        ap::Rng rng;
        uint32_t x = table * 0x9E3779B9u + index * 0x85EBCA6Bu + 0x5EED1234u;
        x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
        rng.state = x != 0 ? x : 1u;
        return (double) rng.nextUnipolar();
    }

    double odd (int h) { return (h & 1) != 0 ? 1.0 : 0.0; }
    double gauss (double x, double centre, double width) { const double d = (x - centre) / width; return std::exp (-0.5 * d * d); }

    // ---- Classic shapes (exact Fourier series)

    Spectrum sine() { Spectrum s; s.add (1, 1.0); return s; }

    Spectrum triangle()
    {
        Spectrum s;
        for (int h = 1; h <= kMaxHarmonic; h += 2)
            s.add (h, 8.0 / (kPi * kPi * h * h) * (((h - 1) / 2) % 2 == 0 ? 1.0 : -1.0));
        return s;
    }

    Spectrum saw()
    {
        Spectrum s;
        for (int h = 1; h <= kMaxHarmonic; ++h)
            s.add (h, 2.0 / (kPi * h));
        return s;
    }

    /** +1 on [0, w), -1 on [w, 1) (DC dropped). w = 0.5 is the square. */
    Spectrum pulse (double w)
    {
        Spectrum s;
        for (int h = 1; h <= kMaxHarmonic; ++h)
        {
            s.a[(size_t) h] = 2.0 * std::sin (kTwoPi * h * w) / (kPi * h);
            s.b[(size_t) h] = 2.0 * (1.0 - std::cos (kTwoPi * h * w)) / (kPi * h);
        }
        return s;
    }

    /** Builds frames from spectra or oversampled time functions. */
    class FrameBuilder
    {
    public:
        FrameBuilder() : fft (kFrameSize), bigFft (kFrameSize * kOversample) {}

        void toFrame (const Spectrum& s, float* out) const
        {
            const double half = 0.5 * (double) kFrameSize;
            std::vector<float> re ((size_t) kFrameSize / 2 + 1, 0.0f), im ((size_t) kFrameSize / 2 + 1, 0.0f);
            for (int h = 1; h <= kMaxHarmonic; ++h)
            {
                re[(size_t) h] = (float) (half * s.a[(size_t) h]);
                im[(size_t) h] = (float) (-half * s.b[(size_t) h]);
            }
            fft.realInverse (re.data(), im.data(), out);
        }

        /** Harmonic series of a single-cycle function of phase 0..1, sampled 16x (aliasing of the naive sampling < -80 dB for 1/h spectra). */
        template <typename Fn>
        Spectrum analyse (Fn&& fn) const
        {
            const int m = kFrameSize * kOversample;
            std::vector<float> x ((size_t) m), re ((size_t) m / 2 + 1), im ((size_t) m / 2 + 1);
            for (int i = 0; i < m; ++i)
                x[(size_t) i] = (float) fn ((double) i / (double) m);
            bigFft.realForward (x.data(), re.data(), im.data());

            Spectrum s;
            const double scale = 2.0 / (double) m;
            for (int h = 1; h <= kMaxHarmonic; ++h)
            {
                s.a[(size_t) h] = scale * re[(size_t) h];
                s.b[(size_t) h] = -scale * im[(size_t) h];
            }
            return s;
        }

    private:
        FFT fft, bigFft;
    };

    // ---- Recipes. t = 0..1 across the table, f = frame index.

    Spectrum basicShapes (int f)
    {
        // Keyframes: sine 0, triangle 6, saw 12, square 19; then the pulse narrows to 25 % at 25 and 12.5 % at 31.
        if (f <= 6)  return mix (sine(), triangle(), f / 6.0);
        if (f <= 12) return mix (triangle(), saw(), (f - 6) / 6.0);
        if (f <= 19) return mix (saw(), pulse (0.5), (f - 12) / 7.0);
        if (f <= 25) return pulse (0.5 - 0.25 * (f - 19) / 6.0);
        return pulse (0.25 - 0.125 * (f - 25) / 6.0);
    }

    Spectrum analogSaws (double t)
    {
        // Rounding: a soft low-pass whose corner falls from 400 to 10 harmonics; drift: harmonic phases wander
        // further from the ideal saw; hum: a growing sine fundamental and second harmonic underneath.
        Spectrum s;
        const double corner = 400.0 * std::pow (10.0 / 400.0, t);
        for (int h = 1; h <= kMaxHarmonic; ++h)
        {
            const double amp = (1.0 / h) / std::sqrt (1.0 + std::pow (h / corner, 4.0));
            const double drift = h > 1 ? 0.35 * kTwoPi * t * (rand01 (1, (uint32_t) h) - 0.5) : 0.0;
            s.add (h, amp, drift);
        }
        s.add (1, 0.6 * t, 0.3);
        s.add (2, 0.12 * t, 1.1);
        return s;
    }

    Spectrum harmonicStack (int f)
    {
        Spectrum s;
        for (int h = 1; h <= f + 1; ++h)
            s.add (h, 1.0 / std::pow ((double) h, 0.7));
        return s;
    }

    Spectrum oddHarmonics (double t)
    {
        Spectrum s;
        const double rolloff = 3.0 - 2.5 * t;   // 1/h^3 (soft) .. 1/h^0.5 (glassy)
        for (int h = 1; h <= kMaxHarmonic; h += 2)
            s.add (h, 1.0 / std::pow ((double) h, rolloff));
        return s;
    }

    Spectrum formantVowels (double t)
    {
        // F1 / F2 / F3 (Hz) for A, E, I, O, U over a 130 Hz harmonic comb.
        constexpr double kVowels[5][3] = { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 },
                                           { 570, 840, 2410 },  { 300, 870, 2240 } };
        constexpr double kBase = 130.0;
        const double v = t * 4.0;
        const int v0 = std::min (3, (int) v);
        const double vt = v - v0;
        double fr[3];
        for (int k = 0; k < 3; ++k)
            fr[k] = kVowels[v0][k] + vt * (kVowels[v0 + 1][k] - kVowels[v0][k]);

        Spectrum s;
        for (int h = 1; h * kBase < 6000.0; ++h)
        {
            const double hz = kBase * h;
            const double env = gauss (hz, fr[0], 90.0) + 0.5 * gauss (hz, fr[1], 120.0) + 0.25 * gauss (hz, fr[2], 160.0) + 0.03;
            s.add (h, env / std::sqrt ((double) h));
        }
        return s;
    }

    Spectrum bell (double t)
    {
        // Bell partial ratios (hum 0.5, prime 1, tierce 1.2, quint 1.5, nominal 2, ...) on a prime of 4,
        // rounded to the nearest harmonic; higher partials die away faster across the table.
        struct Partial { int h; double amp; };
        constexpr Partial kPartials[] = { { 1, 0.35 }, { 2, 0.8 }, { 4, 1.0 }, { 5, 0.6 }, { 6, 0.45 }, { 8, 0.7 }, { 10, 0.4 },
                                          { 11, 0.35 }, { 13, 0.3 }, { 17, 0.25 }, { 21, 0.2 }, { 27, 0.15 }, { 34, 0.12 }, { 43, 0.1 } };
        Spectrum s;
        uint32_t i = 0;
        for (const auto& p : kPartials)
            s.add (p.h, p.amp * std::exp (-t * p.h * 0.12), kTwoPi * rand01 (6, i++));
        return s;
    }

    Spectrum organ (double t)
    {
        // Drawbars 8', 4', 2 2/3', 2', 1 3/5', 1 1/3', 1' -> harmonics 1, 2, 3, 4, 5, 6, 8; each step is 3 dB.
        constexpr int kHarm[7] = { 1, 2, 3, 4, 5, 6, 8 };
        constexpr int kRegs[7][7] = { { 8, 0, 0, 0, 0, 0, 0 }, { 8, 8, 0, 0, 0, 0, 0 }, { 8, 8, 8, 0, 0, 0, 0 }, { 8, 6, 8, 4, 0, 0, 0 },
                                      { 8, 8, 8, 8, 0, 0, 0 }, { 8, 8, 8, 8, 6, 4, 2 }, { 8, 8, 8, 8, 8, 8, 8 } };
        const auto level = [] (double n) { return n <= 0.0 ? 0.0 : std::pow (10.0, (n - 8.0) * 3.0 / 20.0); };

        const double r = t * 6.0;
        const int r0 = std::min (5, (int) r);
        const double rt = r - r0;
        Spectrum s;
        for (int d = 0; d < 7; ++d)
            s.add (kHarm[d], (1.0 - rt) * level (kRegs[r0][d]) + rt * level (kRegs[r0 + 1][d]));
        return s;
    }

    Spectrum digitalGlass (double t)
    {
        // A soft fundamental plus 24 sparse partials (seeded positions and phases); a one-octave-wide window
        // slides from harmonic 8 up to 250 across the table.
        Spectrum s;
        s.add (1, 0.4);
        const double centre = std::log2 (8.0) + t * (std::log2 (250.0) - std::log2 (8.0));
        for (uint32_t k = 0; k < 24; ++k)
        {
            const int h = 5 + (int) (std::pow (rand01 (8, k), 1.6) * 295.0);
            s.add (h, gauss (std::log2 ((double) h), centre, 0.7), kTwoPi * rand01 (8, 100 + k));
        }
        return s;
    }

    Spectrum noiseBloom (double t)
    {
        // Seeded random amplitudes and phases; the band widens from 1 to 400 harmonics.
        Spectrum s;
        const double width = std::pow (400.0, t);
        for (int h = 1; h <= kMaxHarmonic; ++h)
        {
            const double taper = 1.0 / (1.0 + std::pow (h / width, 6.0));
            const double amp = (0.2 + 0.8 * rand01 (11, (uint32_t) h)) / std::sqrt ((double) h) * taper;
            s.add (h, amp, h == 1 ? 0.0 : kTwoPi * rand01 (11, 5000u + (uint32_t) h));
        }
        return s;
    }

    Spectrum growl (double t)
    {
        // A strong sub (fundamental) under a thin saw, with a resonant formant moving from harmonic 4 to 24.
        Spectrum s;
        const double centre = 4.0 * std::pow (6.0, t);
        const double width = 1.2 + 0.15 * centre;
        s.add (1, 1.0);
        for (int h = 2; h <= kMaxHarmonic; ++h)
            s.add (h, 0.35 / h + 0.6 * gauss ((double) h, centre, width) / std::sqrt ((double) h), (h & 1) != 0 ? 0.0 : kPi);
        return s;
    }

    Spectrum pluckDecay (double t)
    {
        Spectrum s;
        const double corner = 2.0 + 600.0 * std::pow (1.0 - t, 3.0);
        for (int h = 1; h <= kMaxHarmonic; ++h)
            s.add (h, std::pow ((double) h, -0.8) / (1.0 + std::pow (h / corner, 4.0)));
        return s;
    }

    Spectrum choir (double t)
    {
        // "ah" -> "oo" formants over a 220 Hz comb, partials grouped by a gentle comb, and each partial's phase
        // rotating at its own (seeded) rate across the table, which reads as slow ensemble beating when scanned.
        constexpr double kBase = 220.0;
        const double f1 = 700.0 - 350.0 * t, f2 = 1150.0 - 500.0 * t, f3 = 2600.0 - 200.0 * t;
        Spectrum s;
        for (int h = 1; h * kBase < 8000.0; ++h)
        {
            const double hz = kBase * h;
            const double env = gauss (hz, f1, 130.0) + 0.5 * gauss (hz, f2, 160.0) + 0.15 * gauss (hz, f3, 300.0) + 0.02;
            const double comb = 0.6 + 0.4 * std::pow (std::cos (kPi * h / 6.0), 2.0);
            const double beat = h == 1 ? 0.0 : kTwoPi * t * 3.0 * (2.0 * rand01 (14, (uint32_t) h) - 1.0);
            s.add (h, env * comb / std::sqrt ((double) h), beat);
        }
        return s;
    }

    Spectrum subBass (double t)
    {
        // Sine -> rounded square: odd harmonics under a Gaussian roll-off that opens up.
        Spectrum s;
        const double sigma = 0.01 + 7.0 * t;
        for (int h = 1; h <= kMaxHarmonic; h += 2)
        {
            const double amp = odd (h) / h * std::exp (-0.5 * std::pow ((h - 1) / sigma, 2.0));
            if (amp > 1.0e-12) s.add (h, amp);
        }
        return s;
    }

    void buildFrame (const FrameBuilder& fb, int table, int f, float* out)
    {
        const double t = (double) f / (double) (kFrames - 1);

        switch (table)
        {
            case 1:  fb.toFrame (analogSaws (t), out); return;
            case 2:  fb.toFrame (pulse (0.5 - 0.45 * t), out); return;
            case 3:  fb.toFrame (harmonicStack (f), out); return;
            case 4:  fb.toFrame (oddHarmonics (t), out); return;
            case 5:  fb.toFrame (formantVowels (t), out); return;
            case 6:  fb.toFrame (bell (t), out); return;
            case 7:  fb.toFrame (organ (t), out); return;
            case 8:  fb.toFrame (digitalGlass (t), out); return;
            case 9:  // 2-op FM, ratio 2, index 0 -> 8
                fb.toFrame (fb.analyse ([t] (double p) { return std::sin (kTwoPi * p + 8.0 * t * std::sin (kTwoPi * 2.0 * p)); }), out);
                return;
            case 10: // hard-synced saw, slave ratio 1 -> 6
                fb.toFrame (fb.analyse ([t] (double p) { const double s = p * (1.0 + 5.0 * t); return 1.0 - 2.0 * (s - std::floor (s)); }), out);
                return;
            case 11: fb.toFrame (noiseBloom (t), out); return;
            case 12: fb.toFrame (growl (t), out); return;
            case 13: fb.toFrame (pluckDecay (t), out); return;
            case 14: fb.toFrame (choir (t), out); return;
            case 15: fb.toFrame (subBass (t), out); return;
            case 0:
            default: fb.toFrame (basicShapes (f), out); return;
        }
    }
}

const char* const* factoryTableNames()
{
    return kNames;
}

std::shared_ptr<Wavetable> makeFactoryTable (int index)
{
    if (index < 0 || index >= kNumFactoryTables)
        index = 0;

    const FrameBuilder builder;
    std::vector<float> data ((size_t) kFrames * (size_t) kFrameSize);
    for (int f = 0; f < kFrames; ++f)
        buildFrame (builder, index, f, data.data() + (size_t) f * (size_t) kFrameSize);

    return Wavetable::fromFrames (data.data(), kFrames, kNames[index]);
}

std::shared_ptr<const Wavetable> factoryTable (int index)
{
    if (index < 0 || index >= kNumFactoryTables)
        index = 0;

    static std::mutex lock;
    static std::array<std::shared_ptr<const Wavetable>, kNumFactoryTables> cache;

    const std::lock_guard<std::mutex> guard (lock);
    auto& slot = cache[(size_t) index];
    if (slot == nullptr)
        slot = makeFactoryTable (index);
    return slot;
}

} // namespace verdant
