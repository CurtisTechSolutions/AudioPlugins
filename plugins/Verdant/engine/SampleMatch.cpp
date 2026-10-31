#include "SampleMatch.h"

#include "FFT.h"

#include <ap/Math.h>
#include <ap/PitchDetector.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace verdant
{

namespace
{
    // Pitch
    constexpr int   kYinWindow         = 2048;
    constexpr float kYinMinHz          = 40.0f;
    constexpr float kYinMaxHz          = 2000.0f;
    constexpr int   kPitchHop          = 512;
    constexpr int   kMaxPitchFrames    = 128;     // evenly spaced hops; the median needs far fewer, this bounds the cost
    constexpr float kMinVoicedFraction = 0.1f;    // below this the sample is treated as unpitched
    constexpr double kNominalHz        = 110.0;   // harmonic grid for unpitched samples

    // Envelope / region
    constexpr double kRegionFloor      = 0.01;    // -40 dB of the RMS peak
    constexpr double kEnvHopSeconds    = 0.001;
    constexpr double kAttackLevel      = 0.9;     // attack = time to 90 % of the peak
    constexpr double kReleaseMs        = 300.0;

    // Table
    constexpr int    kMaxTableFrames   = 64;
    constexpr double kMinFrameSpacing  = 0.02;    // seconds between analysis times (short sounds get fewer frames)
    constexpr double kAnalysisPeriods  = 4.0;     // Hann window length in periods
    constexpr int    kMaxHarmonic      = kFrameSize / 2 - 1;
    constexpr int    kMaxFftSize       = 65536;

    // Noisiness: long windows resolve the harmonics so the energy between them is measurable.
    constexpr int    kNoiseWindows     = 8;
    constexpr double kNoisePeriods     = 16.0;
    constexpr int    kNoiseMinWindow   = 4096;
    constexpr int    kNoiseMaxWindow   = 32768;
    constexpr float  kNoiseCapPercent  = 40.0f;

    // Filter envelope. The cutoff knob spans 20 Hz..20 kHz (about 10 octaves) with a centre skew, so one sixth
    // of the normalised range is roughly 1.7 octaves: an amount of log2 (early / late) / 6 moves the cutoff by
    // about 1.7 octaves per doubling of the measured brightness ratio - musically close without overshooting.
    constexpr double kCutoffEnvDivisor = 6.0;
    constexpr double kCentroidFall     = 0.8;     // late < 80 % of early -> the sound darkens: add the filter envelope
    constexpr double kEarlyFraction    = 0.1;     // first 10 % of the region
    constexpr double kLateFraction     = 0.7;     // last 30 % of the region
    constexpr double kSettleTolerance  = 1.1;     // Env 3 decay ends when the centroid is within 10 % of its late value

    constexpr double kPi = 3.14159265358979323846;

    int nextPow2 (int n) noexcept
    {
        int p = 2;
        while (p < n && p < kMaxFftSize) p <<= 1;
        return p;
    }

    double median (std::vector<double> v)
    {
        if (v.empty()) return 0.0;
        const size_t mid = v.size() / 2;
        std::nth_element (v.begin(), v.begin() + (std::ptrdiff_t) mid, v.end());
        const double hi = v[mid];
        if (v.size() % 2 == 1) return hi;
        const double lo = *std::max_element (v.begin(), v.begin() + (std::ptrdiff_t) mid);
        return 0.5 * (lo + hi);
    }

    /** Monotone progress reporting with cancellation. */
    struct Progress
    {
        const ProgressFn& fn;
        float last = 0.0f;
        bool cancelled = false;

        bool operator() (float p)
        {
            if (cancelled) return false;
            last = std::max (last, std::clamp (p, 0.0f, 1.0f));
            if (fn && ! fn (last)) cancelled = true;
            return ! cancelled;
        }
    };

    /** Centred RMS over `window` samples every `hop` samples (zero outside the sample), via prefix sums. */
    std::vector<double> rmsEnvelope (const std::vector<float>& x, int window, int hop)
    {
        const int n = (int) x.size();
        std::vector<double> prefix ((size_t) n + 1, 0.0);
        for (int i = 0; i < n; ++i) prefix[(size_t) i + 1] = prefix[(size_t) i] + (double) x[(size_t) i] * x[(size_t) i];

        const int points = n / hop + 1;
        std::vector<double> env ((size_t) points);
        for (int p = 0; p < points; ++p)
        {
            const int c = p * hop;
            const int a = std::clamp (c - window / 2, 0, n), b = std::clamp (c - window / 2 + window, 0, n);
            env[(size_t) p] = std::sqrt ((prefix[(size_t) b] - prefix[(size_t) a]) / (double) window);
        }
        return env;
    }

    /** Zero-phase Hann-windowed spectrum around sample `centre`: the window's centre sits at FFT index 0, so a
        bin's phase is the cosine phase of that component at `centre`. Returns sum (window). */
    double windowedSpectrum (const std::vector<float>& x, int centre, int halfLen, const FFT& fft,
                             std::vector<float>& buf, std::vector<float>& re, std::vector<float>& im)
    {
        const int n = fft.size();
        std::fill (buf.begin(), buf.end(), 0.0f);
        double sumW = 0.0;
        for (int j = -halfLen; j <= halfLen; ++j)
        {
            const double w = 0.5 + 0.5 * std::cos (kPi * (double) j / (double) (halfLen + 1));
            sumW += w;
            const int s = centre + j;
            if (s < 0 || s >= (int) x.size()) continue;
            buf[(size_t) ((j + n) % n)] = (float) (w * (double) x[(size_t) s]);
        }
        fft.realForward (buf.data(), re.data(), im.data());
        return sumW;
    }

    /** Magnitude-weighted spectral centroid in Hz (bins below -80 dB of the strongest ignored); < 0 if silent. */
    double centroidHz (const std::vector<float>& re, const std::vector<float>& im, int fftSize, double sampleRate)
    {
        const int bins = fftSize / 2;
        double maxMag = 0.0;
        for (int b = 1; b < bins; ++b) maxMag = std::max (maxMag, (double) std::hypot (re[(size_t) b], im[(size_t) b]));
        if (maxMag < 1.0e-9) return -1.0;

        const double floor = maxMag * 1.0e-4;
        double num = 0.0, den = 0.0;
        for (int b = 1; b < bins; ++b)
        {
            const double m = std::hypot (re[(size_t) b], im[(size_t) b]);
            if (m < floor) continue;
            num += m * (double) b * sampleRate / (double) fftSize;
            den += m;
        }
        return den > 0.0 ? num / den : -1.0;
    }

    struct Harmonic { double amp = 0.0, phase = 0.0; };   // cosine phase at the analysis centre
}

SampleMatchResult matchSample (const float* mono, int numSamples, double sampleRate, const ProgressFn& progressFn)
{
    SampleMatchResult r;
    r.values = defaultValues();
    Progress progress { progressFn };

    if (mono == nullptr || numSamples < 1 || ! (sampleRate > 0.0))
    {
        std::vector<float> sine ((size_t) kFrameSize);
        for (int i = 0; i < kFrameSize; ++i)
            sine[(size_t) i] = (float) std::sin (2.0 * kPi * (double) i / (double) kFrameSize);
        r.table = Wavetable::fromFrames (sine.data(), 1, "Matched");
        r.notes = "No audio to analyse";
        progress (1.0f);
        return r;
    }

    const double sr = sampleRate;
    std::vector<float> x ((size_t) numSamples);
    for (int i = 0; i < numSamples; ++i)
        x[(size_t) i] = std::isfinite (mono[i]) ? mono[i] : 0.0f;

    auto cancelledResult = [&r]
    {
        r.confidence = 0.0f;
        r.notes = "Analysis cancelled";
        return r;
    };

    if (! progress (0.0f)) return cancelledResult();

    const int envHop = std::max (1, (int) std::lround (sr * kEnvHopSeconds));
    const double hopMs = 1000.0 * (double) envHop / sr;

    //==========================================================================
    // 1. Pitch over the loud part.
    ap::YinPitchDetector yin;
    yin.prepare (sr, kYinWindow, kYinMinHz, kYinMaxHz);
    const int required = yin.requiredSamples();

    std::vector<int> hops;
    {
        const auto coarse = rmsEnvelope (x, std::max (1, (int) std::lround (sr * 0.01)), envHop);
        const double peak = *std::max_element (coarse.begin(), coarse.end());
        const int numHops = numSamples >= required ? (numSamples - required) / kPitchHop + 1 : 1;
        std::vector<int> eligible;
        for (int h = 0; h < numHops; ++h)
        {
            const int centre = std::min (numSamples - 1, h * kPitchHop + yin.analysisCentre());
            if (coarse[(size_t) (centre / envHop)] >= peak * kRegionFloor && peak > 0.0)
                eligible.push_back (h * kPitchHop);
        }
        if (eligible.empty())
            eligible.push_back (0);

        if ((int) eligible.size() <= kMaxPitchFrames)
            hops = eligible;
        else
            for (int i = 0; i < kMaxPitchFrames; ++i)
                hops.push_back (eligible[(size_t) std::lround ((double) i * (double) (eligible.size() - 1) / (double) (kMaxPitchFrames - 1))]);
    }

    std::vector<float> padded (x);
    padded.resize ((size_t) numSamples + (size_t) required, 0.0f);   // the last hop (or a short sample) reads zeros

    std::vector<double> voicedHz;
    for (size_t i = 0; i < hops.size(); ++i)
    {
        const auto res = yin.analyze (padded.data() + hops[i]);
        if (res.voiced && res.frequency >= kYinMinHz && res.frequency <= kYinMaxHz)
            voicedHz.push_back ((double) res.frequency);
        if ((i & 7) == 7 && ! progress (0.35f * (float) (i + 1) / (float) hops.size()))
            return cancelledResult();
    }

    const float voicedFraction = (float) voicedHz.size() / (float) hops.size();
    const bool pitched = voicedFraction >= kMinVoicedFraction && ! voicedHz.empty();
    const double f0 = pitched ? median (voicedHz) : 0.0;
    const double gridHz = pitched ? f0 : kNominalHz;
    r.detectedHz = (float) f0;
    r.confidence = voicedFraction;

    if (! progress (0.35f)) return cancelledResult();

    //==========================================================================
    // Amplitude envelope (window ~2 periods so it does not ripple at the pitch).
    const int envWindow = (int) std::lround (pitched ? std::clamp (2.0 * sr / f0, 0.005 * sr, 0.05 * sr) : 0.01 * sr);
    const auto env = rmsEnvelope (x, std::max (1, envWindow), envHop);
    const int envPoints = (int) env.size();
    const int peakIdx = (int) (std::max_element (env.begin(), env.end()) - env.begin());
    const double peak = env[(size_t) peakIdx];
    const double floorLevel = peak * kRegionFloor;

    int regionStart = 0;
    while (regionStart < envPoints - 1 && env[(size_t) regionStart] < floorLevel) ++regionStart;
    int lastAbove = envPoints - 1;
    while (lastAbove > regionStart && env[(size_t) lastAbove] < floorLevel) --lastAbove;

    // The sound "decays to silence" when the last fully covered envelope point is below -40 dB.
    const int lastFull = std::clamp ((numSamples - envWindow / 2) / envHop, 0, envPoints - 1);
    const bool decaysToSilence = peak > 0.0 && env[(size_t) lastFull] < floorLevel;
    const int regionEnd = decaysToSilence ? lastAbove : std::max (regionStart, lastFull);

    int attackIdx = regionStart;
    while (attackIdx < regionEnd && env[(size_t) attackIdx] < peak * kAttackLevel) ++attackIdx;
    const double attackMs = (double) (attackIdx - regionStart) * hopMs;

    double sustain = 0.0, decayMs = 0.0;
    if (decaysToSilence || peak <= 0.0)
    {
        decayMs = (double) std::max (1, lastAbove - peakIdx) * hopMs;   // peak to -40 dB
    }
    else
    {
        const int thirdStart = regionEnd - (regionEnd - regionStart) / 3;
        std::vector<double> tail (env.begin() + thirdStart, env.begin() + regionEnd + 1);
        const double sustainLevel = std::min (median (tail), peak);
        sustain = sustainLevel / peak;
        const double target = sustainLevel + (peak - sustainLevel) / std::exp (1.0);
        int i = peakIdx;
        while (i < regionEnd && env[(size_t) i] > target) ++i;
        decayMs = (double) std::max (1, i - peakIdx) * hopMs;
    }

    const int regionStartSample = std::min (numSamples - 1, regionStart * envHop);
    const int regionEndSample = std::clamp (regionEnd * envHop, regionStartSample, numSamples - 1);
    const int attackEndSample = std::clamp (attackIdx * envHop, regionStartSample, regionEndSample);
    const double regionMs = 1000.0 * (double) (regionEndSample - regionStartSample) / sr;

    if (! progress (0.4f)) return cancelledResult();

    //==========================================================================
    // 2. Table frames.
    const int regionLen = regionEndSample - regionStartSample;
    const int numFrames = std::clamp ((int) std::floor ((double) regionLen / sr / kMinFrameSpacing) + 1, 1, kMaxTableFrames);

    const int halfLen = std::clamp ((int) std::lround (0.5 * kAnalysisPeriods * sr / gridHz), 8, kMaxFftSize / 4 - 1);
    const FFT fft (nextPow2 (2 * (2 * halfLen + 1)));
    const int nfft = fft.size();
    std::vector<float> buf ((size_t) nfft), re ((size_t) nfft / 2 + 1), im ((size_t) nfft / 2 + 1);

    const int numHarmonics = std::max (1, std::min (kMaxHarmonic, (int) std::ceil (0.5 * sr / gridHz) - 1));
    const double spacing = gridHz * (double) nfft / sr;   // bins between harmonics

    // Unpitched: fixed pseudo-random phases (the same for every frame, so frames morph cleanly).
    std::vector<double> noisePhase ((size_t) numHarmonics + 1, 0.0);
    {
        ap::Rng rng;
        rng.state = 0x5A4D9E37u;
        for (auto& p : noisePhase) p = kPi * (double) rng.nextBipolar();
    }

    const FFT synth (kFrameSize);
    std::vector<float> sre ((size_t) kFrameSize / 2 + 1), sim ((size_t) kFrameSize / 2 + 1);
    std::vector<float> frames ((size_t) numFrames * (size_t) kFrameSize, 0.0f);
    std::vector<double> frameCentroid ((size_t) numFrames, -1.0), frameTime ((size_t) numFrames, 0.0);
    std::vector<Harmonic> harm ((size_t) numHarmonics + 1);

    for (int f = 0; f < numFrames; ++f)
    {
        const int centre = numFrames == 1 ? (regionStartSample + regionEndSample) / 2
                                          : regionStartSample + (int) std::lround ((double) regionLen * (double) f / (double) (numFrames - 1));
        frameTime[(size_t) f] = (double) (centre - regionStartSample);

        const double sumW = windowedSpectrum (x, centre, halfLen, fft, buf, re, im);
        frameCentroid[(size_t) f] = centroidHz (re, im, nfft, sr);
        const auto mag = [&] (int b) { return (double) std::hypot (re[(size_t) b], im[(size_t) b]); };

        for (int k = 1; k <= numHarmonics; ++k)
        {
            const double expected = (double) k * spacing;
            Harmonic h;
            if (pitched)
            {
                // Strongest bin near k f0, then log-parabolic interpolation of the peak height (exact for a Gaussian, close for Hann).
                const int reach = std::max (1, (int) std::floor (0.25 * spacing));
                const int lo = std::clamp ((int) std::lround (expected) - reach, 1, nfft / 2 - 1);
                const int hi = std::clamp ((int) std::lround (expected) + reach, 1, nfft / 2 - 1);
                int m = lo;
                for (int b = lo + 1; b <= hi; ++b) if (mag (b) > mag (m)) m = b;

                double peakMag = mag (m);
                if (m > 1 && m < nfft / 2 - 1 && peakMag > 0.0)
                {
                    const double a = std::log (mag (m - 1) + 1.0e-30), b = std::log (peakMag + 1.0e-30), c = std::log (mag (m + 1) + 1.0e-30);
                    const double denom = a - 2.0 * b + c;
                    if (denom < 0.0)
                    {
                        const double p = std::clamp (0.5 * (a - c) / denom, -0.5, 0.5);
                        peakMag = std::exp (b - 0.25 * (a - c) * p);
                    }
                }
                h.amp = 2.0 * peakMag / sumW;
                h.phase = std::atan2 ((double) im[(size_t) m], (double) re[(size_t) m]);
            }
            else
            {
                // Spectral envelope: RMS magnitude of the band around k x 110 Hz.
                const int lo = std::clamp ((int) std::lround (expected - 0.5 * spacing), 1, nfft / 2 - 1);
                const int hi = std::clamp ((int) std::lround (expected + 0.5 * spacing) - 1, lo, nfft / 2 - 1);
                double power = 0.0;
                for (int b = lo; b <= hi; ++b) power += mag (b) * mag (b);
                h.amp = 2.0 * std::sqrt (power / (double) (hi - lo + 1)) / sumW;
                h.phase = noisePhase[(size_t) k];
            }
            harm[(size_t) k] = h;
        }

        // Resynthesise one cycle as sines: cos phase phi -> sine phase phi + pi/2, rotated so harmonic 1 starts at 0.
        std::fill (sre.begin(), sre.end(), 0.0f);
        std::fill (sim.begin(), sim.end(), 0.0f);
        const double rot = pitched ? harm[1].phase + 0.5 * kPi : 0.0;
        for (int k = 1; k <= numHarmonics; ++k)
        {
            const double psi = (pitched ? harm[(size_t) k].phase + 0.5 * kPi : harm[(size_t) k].phase) - (double) k * rot;
            const double a = 0.5 * kFrameSize * harm[(size_t) k].amp;
            sre[(size_t) k] = (float) (a * std::sin (psi));
            sim[(size_t) k] = (float) (-a * std::cos (psi));
        }
        float* out = frames.data() + (size_t) f * (size_t) kFrameSize;
        synth.realInverse (sre.data(), sim.data(), out);

        // Equal RMS per frame: the amplitude envelope belongs to Env 1, not to the table.
        double energy = 0.0;
        for (int i = 0; i < kFrameSize; ++i) energy += (double) out[i] * out[i];
        const double rms = std::sqrt (energy / kFrameSize);
        if (rms > 1.0e-9)
            for (int i = 0; i < kFrameSize; ++i) out[i] = (float) ((double) out[i] / rms);
        else
            std::fill (out, out + kFrameSize, 0.0f);

        if (! progress (0.4f + 0.5f * (float) (f + 1) / (float) numFrames))
            return cancelledResult();
    }

    float tablePeak = 0.0f;
    for (float s : frames) tablePeak = std::max (tablePeak, std::fabs (s));
    if (tablePeak > 0.0f)
        for (float& s : frames) s /= tablePeak;
    r.table = Wavetable::fromFrames (frames.data(), numFrames, "Matched");

    //==========================================================================
    // Noisiness = 1 - harmonic energy fraction, from long windows over the sustained part.
    double noisiness = 1.0;
    if (pitched)
    {
        const int noiseHalf = std::clamp ((int) std::lround (0.5 * kNoisePeriods * sr / f0), kNoiseMinWindow / 2, kNoiseMaxWindow / 2);
        const FFT nf (nextPow2 (2 * (2 * noiseHalf + 1)));
        const int nn = nf.size();
        std::vector<float> nbuf ((size_t) nn), nre ((size_t) nn / 2 + 1), nim ((size_t) nn / 2 + 1);
        const double nspacing = f0 * (double) nn / sr;
        const int lobe = (int) std::ceil (2.0 * (double) nn / (double) (2 * noiseHalf + 1)) + 1;   // Hann main lobe +-2 bins (+1 margin)
        const int harmonics = std::max (1, std::min (kMaxHarmonic, (int) std::ceil (0.5 * sr / f0) - 1));
        const int totalLo = std::max (1, (int) std::lround (0.5 * nspacing));
        const int totalHi = std::min (nn / 2 - 1, (int) std::lround (((double) harmonics + 0.5) * nspacing));

        double harmonicEnergy = 0.0, totalEnergy = 0.0;
        std::vector<char> counted ((size_t) nn / 2 + 1, 0);
        const int windows = std::max (1, std::min (kNoiseWindows, (regionEndSample - attackEndSample) / std::max (1, noiseHalf) + 1));
        for (int w = 0; w < windows; ++w)
        {
            const int centre = windows == 1 ? (attackEndSample + regionEndSample) / 2
                                            : attackEndSample + (int) std::lround ((double) (regionEndSample - attackEndSample) * (double) w / (double) (windows - 1));
            windowedSpectrum (x, centre, noiseHalf, nf, nbuf, nre, nim);
            const auto power = [&] (int b) { return (double) nre[(size_t) b] * nre[(size_t) b] + (double) nim[(size_t) b] * nim[(size_t) b]; };

            for (int b = totalLo; b <= totalHi; ++b) totalEnergy += power (b);

            std::fill (counted.begin(), counted.end(), 0);
            for (int k = 1; k <= harmonics; ++k)
            {
                const double expected = (double) k * nspacing;
                const int reach = std::max (1, (int) std::floor (0.25 * nspacing));
                const int lo = std::clamp ((int) std::lround (expected) - reach, totalLo, totalHi);
                const int hi = std::clamp ((int) std::lround (expected) + reach, lo, totalHi);
                int m = lo;
                for (int b = lo + 1; b <= hi; ++b) if (power (b) > power (m)) m = b;
                for (int b = std::max (totalLo, m - lobe); b <= std::min (totalHi, m + lobe); ++b)
                    if (! counted[(size_t) b]) { counted[(size_t) b] = 1; harmonicEnergy += power (b); }
            }
        }
        noisiness = totalEnergy > 0.0 ? std::clamp (1.0 - harmonicEnergy / totalEnergy, 0.0, 1.0) : 0.0;
    }

    if (! progress (0.95f)) return cancelledResult();

    //==========================================================================
    // Brightness: the centroid of the sustained part, and its movement over the region.
    std::vector<double> sustainedCentroids, early, late;
    std::vector<int> validFrames;
    for (int f = 0; f < numFrames; ++f)
    {
        const double c = frameCentroid[(size_t) f];
        if (c <= 0.0) continue;
        validFrames.push_back (f);
        const double rel = regionLen > 0 ? frameTime[(size_t) f] / (double) regionLen : 0.0;
        if (frameTime[(size_t) f] + regionStartSample >= attackEndSample) sustainedCentroids.push_back (c);
        if (rel <= kEarlyFraction) early.push_back (c);
        if (rel >= kLateFraction) late.push_back (c);
    }
    auto mean = [] (const std::vector<double>& v) { double s = 0.0; for (double d : v) s += d; return v.empty() ? 0.0 : s / (double) v.size(); };
    if (sustainedCentroids.empty())
        for (int f : validFrames) sustainedCentroids.push_back (frameCentroid[(size_t) f]);
    if (early.empty() && ! validFrames.empty()) early.push_back (frameCentroid[(size_t) validFrames.front()]);
    if (late.empty() && ! validFrames.empty()) late.push_back (frameCentroid[(size_t) validFrames.back()]);

    const double sustainedCentroid = sustainedCentroids.empty() ? 1000.0 : mean (sustainedCentroids);
    const double earlyCentroid = mean (early), lateCentroid = mean (late);
    const bool filterEnv = validFrames.size() >= 3 && lateCentroid > 0.0 && lateCentroid < kCentroidFall * earlyCentroid;

    //==========================================================================
    // 3. Patch values.
    auto& v = r.values;
    auto set = [&v] (P p, double value) { v[(size_t) p] = clampToRange (p, (float) value); };

    set (oscParam (0, OscParam::On), 1);
    set (oscParam (0, OscParam::Table), 16);   // "User"
    set (oscParam (1, OscParam::On), 0);
    set (filterParam (0, FilterParam::On), 1);
    set (filterParam (0, FilterParam::Type), 1);   // "LP 24"
    set (P::NoiseLevel, std::clamp ((float) (noisiness * 100.0), 0.0f, kNoiseCapPercent));

    set (envParam (0, EnvParam::A), attackMs);
    set (envParam (0, EnvParam::H), 0);
    set (envParam (0, EnvParam::D), std::max (1.0, decayMs));
    set (envParam (0, EnvParam::S), 100.0 * sustain);
    set (envParam (0, EnvParam::R), kReleaseMs);

    // Table scan: Env 2 jumps to 1 and falls linearly to 0 over the analysed region. Position = 100 % - 100 % x Env 2,
    // so the oscillator starts on frame 0 (the start of the sample) and arrives at the last frame when the region ends.
    set (oscParam (0, OscParam::Pos), 100);
    set (envParam (1, EnvParam::A), 0);
    set (envParam (1, EnvParam::H), 0);
    set (envParam (1, EnvParam::D), std::max (1.0, regionMs));
    set (envParam (1, EnvParam::S), 0);
    set (envParam (1, EnvParam::DCurve), 0);   // linear: frames advance at the rate they were analysed
    set (modSrc (0), (double) ModSource::Env2);
    set (modDst (0), (double) modDestinationIndexOf (oscParam (0, OscParam::Pos)));
    set (modAmt (0), -100);

    double cutoff = 2.0 * sustainedCentroid;
    double filterAmount = 0.0, filterDecayMs = 0.0;
    if (filterEnv)
    {
        cutoff = 2.0 * lateCentroid;
        filterAmount = std::clamp (std::log2 (earlyCentroid / lateCentroid) / kCutoffEnvDivisor, 0.0, 1.0);

        double settle = frameTime[(size_t) validFrames.back()];
        for (int f : validFrames)
            if (frameCentroid[(size_t) f] <= kSettleTolerance * lateCentroid) { settle = frameTime[(size_t) f]; break; }
        filterDecayMs = std::max (1.0, 1000.0 * settle / sr);

        set (envParam (2, EnvParam::A), 0);
        set (envParam (2, EnvParam::H), 0);
        set (envParam (2, EnvParam::D), filterDecayMs);
        set (envParam (2, EnvParam::S), 0);
        set (modSrc (1), (double) ModSource::Env3);
        set (modDst (1), (double) modDestinationIndexOf (filterParam (0, FilterParam::Cutoff)));
        set (modAmt (1), 100.0 * filterAmount);
    }
    set (filterParam (0, FilterParam::Cutoff), std::clamp (cutoff, 200.0, 20000.0));

    for (int i = 0; i < kNumParams; ++i)
        v[(size_t) i] = clampToRange ((P) i, v[(size_t) i]);

    //==========================================================================
    char line[400];
    if (pitched)
    {
        const double midi = 69.0 + 12.0 * std::log2 (f0 / 440.0);
        static constexpr const char* kNotes[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int nearest = (int) std::lround (midi);
        std::snprintf (line, sizeof (line), "%.1f Hz (%s%d %+d ct), %d%% voiced, %d frames, attack %.0f ms, decay %.0f ms, sustain %.0f%%, cutoff %.0f Hz%s, noise %.0f%%",
                       f0, kNotes[((nearest % 12) + 12) % 12], nearest / 12 - 1, (int) std::lround (100.0 * (midi - nearest)),
                       (int) std::lround (100.0 * voicedFraction), numFrames, attackMs, decayMs, 100.0 * sustain,
                       (double) v[(size_t) filterParam (0, FilterParam::Cutoff)], filterEnv ? " with filter envelope" : "",
                       (double) v[(size_t) P::NoiseLevel]);
    }
    else
    {
        std::snprintf (line, sizeof (line), "Unpitched (%d%% voiced): spectral envelope on a 110 Hz grid, %d frames, attack %.0f ms, decay %.0f ms, cutoff %.0f Hz, noise %.0f%%",
                       (int) std::lround (100.0 * voicedFraction), numFrames, attackMs, decayMs,
                       (double) v[(size_t) filterParam (0, FilterParam::Cutoff)], (double) v[(size_t) P::NoiseLevel]);
    }
    r.notes = line;

    progress (1.0f);
    return r;
}

} // namespace verdant
