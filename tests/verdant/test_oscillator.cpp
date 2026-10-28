// WavetableOscillator: pitch, band-limiting, morph continuity, unison, warp modes, ramps, determinism, CPU.

#include "TestHarness.h"

#include "engine/FFT.h"
#include "engine/WavetableFactory.h"
#include "engine/WavetableOscillator.h"

#include <ap/Math.h>
#include <ap/PitchDetector.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace verdant;

namespace
{
    struct Stereo { std::vector<float> l, r; };

    /** Renders like the Voice does: setFrequency / setParams once per 32-sample control block. */
    template <typename PerBlock>
    Stereo renderOsc (WavetableOscillator& osc, int numSamples, PerBlock&& perBlock, const float* fm = nullptr)
    {
        Stereo s { std::vector<float> ((size_t) numSamples), std::vector<float> ((size_t) numSamples) };
        for (int pos = 0; pos < numSamples; pos += kControlInterval)
        {
            const int n = std::min (kControlInterval, numSamples - pos);
            perBlock (osc, pos);
            osc.render (s.l.data() + pos, s.r.data() + pos, n, fm != nullptr ? fm + pos : nullptr);
        }
        return s;
    }

    Stereo renderSimple (const Wavetable& t, double sr, float hz, const OscParams& p, int numSamples, uint32_t seed = 1, const float* fm = nullptr)
    {
        WavetableOscillator osc;
        osc.prepare (sr);
        osc.setTable (&t);
        osc.setParams (p);
        osc.noteOn (hz, seed);
        return renderOsc (osc, numSamples, [&] (WavetableOscillator& o, int) { o.setFrequency (hz); o.setParams (p); }, fm);
    }

    double rms (const std::vector<float>& v, size_t from = 0)
    {
        double acc = 0.0;
        for (size_t i = from; i < v.size(); ++i) acc += (double) v[i] * v[i];
        return std::sqrt (acc / (double) std::max<size_t> (1, v.size() - from));
    }

    float maxStep (const std::vector<float>& v)
    {
        float m = 0.0f;
        for (size_t i = 1; i < v.size(); ++i) m = std::max (m, std::fabs (v[i] - v[i - 1]));
        return m;
    }

    float yinHz (const std::vector<float>& v, double sr, size_t from)
    {
        ap::YinPitchDetector yin;
        yin.prepare (sr, 2048, 50.0f, 1500.0f);
        if (from + (size_t) yin.requiredSamples() > v.size()) return 0.0f;
        const auto r = yin.analyze (v.data() + from);
        return r.voiced ? r.frequency : 0.0f;
    }

    float sawPosition() { return (float) kBasicShapesSawFrame / (float) (kFactoryTableFrames - 1); }

    /** Energy outside +-1 bin of the harmonics of an exactly bin-centred f0, relative to the total (dB). */
    double aliasDb (const std::vector<float>& x, int f0Bin)
    {
        const int n = (int) x.size();
        FFT fft (n);
        std::vector<float> re ((size_t) n / 2 + 1), im ((size_t) n / 2 + 1);
        fft.realForward (x.data(), re.data(), im.data());
        double total = 0.0, alias = 0.0;
        for (int k = 0; k <= n / 2; ++k)
        {
            const double p = (double) re[(size_t) k] * re[(size_t) k] + (double) im[(size_t) k] * im[(size_t) k];
            total += p;
            const int nearest = (int) std::lround ((double) k / f0Bin) * f0Bin;
            if (nearest == 0 || std::abs (k - nearest) > 1)
                alias += p;
        }
        return 10.0 * std::log10 ((alias + 1.0e-30) / total);
    }

    /** Renders the saw at f0 = bin x sr / 32768 and returns the aliased-energy ratio in dB. */
    double sawAliasDb (double sr, int f0Bin, int unison = 1)
    {
        constexpr int kN = 32768;
        const auto t = factoryTable (0);
        OscParams p;
        p.position = sawPosition();
        p.randomPhase = 0.0f;
        p.unison = unison;
        p.detuneCents = 0.0f;
        const float hz = (float) (f0Bin * sr / kN);
        const auto s = renderSimple (*t, sr, hz, p, kN + 4096);
        return aliasDb (std::vector<float> (s.l.begin() + 4096, s.l.end()), f0Bin);
    }

    const WarpMode kAllModes[] = { WarpMode::Off, WarpMode::Sync, WarpMode::Bend, WarpMode::Squeeze, WarpMode::Mirror,
                                   WarpMode::FmOsc, WarpMode::FmSub, WarpMode::Remap };
}

AP_TEST (osc_pitch_accuracy)
{
    const auto t = factoryTable (0);
    for (float pos : { 0.0f, sawPosition(), 1.0f })
    {
        OscParams p;
        p.position = pos;
        const auto s = renderSimple (*t, 44100.0, 220.0f, p, 22050);
        const float hz = yinHz (s.l, 44100.0, 4096);
        CHECK (std::fabs (hz - 220.0f) < 220.0f * 0.003f);
    }
    // And with unison (detuned copies are symmetric about the note).
    OscParams p;
    p.unison = 5;
    p.detuneCents = 10.0f;
    p.position = sawPosition();
    const auto s = renderSimple (*t, 48000.0, 220.0f, p, 24000);
    CHECK (std::fabs (yinHz (s.l, 48000.0, 4096) - 220.0f) < 220.0f * 0.003f);
}

AP_TEST (osc_band_limited_saw_midi108)
{
    // MIDI 108 (4186 Hz) at 44.1 kHz; f0 put on bin 3110 of a 32768-point FFT (4185.5 Hz) so harmonics and
    // aliases land on exact bins with no window.
    const double db = sawAliasDb (44100.0, 3110);
    std::printf ("    saw MIDI 108 @ 44.1k aliasing: %.1f dB\n", db);
    CHECK (db < -60.0);

    // The rest of the top octaves too (MIDI ~90, ~96, ~100, ~115), and at 48 kHz.
    for (int bin : { 1100, 1555, 1960, 4400 })
    {
        const double d = sawAliasDb (44100.0, bin);
        std::printf ("    saw bin %d @ 44.1k aliasing: %.1f dB\n", bin, d);
        CHECK (d < -60.0);
    }
    const double d48 = sawAliasDb (48000.0, 2857);
    std::printf ("    saw MIDI 108 @ 48k aliasing: %.1f dB\n", d48);
    CHECK (d48 < -60.0);

    // The whole keyboard: MIDI 24..120 every 6 semitones at 44.1 kHz (f0 rounded to the nearest bin).
    double worst = -200.0;
    for (int midi = 24; midi <= 120; midi += 6)
    {
        const int bin = (int) std::lround (ap::midiToHz ((float) midi) * 32768.0 / 44100.0);
        const double d = sawAliasDb (44100.0, bin);
        std::printf ("    saw MIDI %3d @ 44.1k aliasing: %.1f dB\n", midi, d);
        CHECK (d < -60.0);
        worst = std::max (worst, d);
    }
    std::printf ("    worst over MIDI 24..120: %.1f dB\n", worst);
}

AP_TEST (osc_band_limit_keeps_brightness)
{
    // The alias-free choice must not dull a mid note: at 440 Hz / 48 kHz harmonics up to ~sr/4 (27) are kept.
    const auto t = factoryTable (0);
    OscParams p;
    p.position = sawPosition();
    p.randomPhase = 0.0f;
    constexpr int kN = 32768;
    const int bin = 300;   // 439.45 Hz
    const auto s = renderSimple (*t, 48000.0, (float) (bin * 48000.0 / kN), p, kN + 2048);
    std::vector<float> x (s.l.begin() + 2048, s.l.end());
    FFT fft (kN);
    std::vector<float> re (kN / 2 + 1), im (kN / 2 + 1);
    fft.realForward (x.data(), re.data(), im.data());
    const auto amp = [&] (int h) { const size_t k = (size_t) (h * bin); return std::sqrt ((double) re[k] * re[k] + (double) im[k] * im[k]); };
    // Saw: harmonic h has 1/h of the fundamental.
    CHECK_NEAR (amp (20) / amp (1), 1.0 / 20.0, 0.1 / 20.0);
    CHECK (amp (27) / amp (1) > 0.5 / 27.0);
}

AP_TEST (osc_morph_continuity)
{
    // Basic Shapes, frames 0..1 region, position swept over 1 s at 220 Hz with per-block updates.
    const auto t = factoryTable (0);
    WavetableOscillator osc;
    osc.prepare (44100.0);
    osc.setTable (t.get());
    OscParams p;
    osc.setParams (p);
    osc.noteOn (220.0f, 3u);
    const int n = 44100;
    const float endPos = 1.0f / (float) (kFactoryTableFrames - 1);
    const auto s = renderOsc (osc, n, [&] (WavetableOscillator& o, int pos)
    {
        p.position = endPos * (float) (pos + kControlInterval) / (float) n;
        o.setFrequency (220.0f);
        o.setParams (p);
    });
    CHECK (maxStep (s.l) < 0.05f);

    // A 16-frame table of phase-rotated sines swept across the whole table.
    std::vector<float> frames ((size_t) 16 * kFrameSize);
    for (int f = 0; f < 16; ++f)
        for (int i = 0; i < kFrameSize; ++i)
            frames[(size_t) f * kFrameSize + (size_t) i] = (float) std::sin (ap::kTwoPiD * i / kFrameSize + 0.4 * f);
    const auto rot = Wavetable::fromFrames (frames.data(), 16, "Rot");
    osc.setTable (rot.get());
    p.position = 0.0f;
    osc.setParams (p);
    osc.noteOn (220.0f, 3u);
    const auto s2 = renderOsc (osc, n, [&] (WavetableOscillator& o, int pos)
    {
        p.position = (float) (pos + kControlInterval) / (float) n;
        o.setFrequency (220.0f);
        o.setParams (p);
    });
    CHECK (maxStep (s2.l) < 0.05f);
    CHECK_NEAR (osc.currentPosition(), 1.0, 1.0e-6);
}

AP_TEST (osc_ramps_are_smooth)
{
    // A level jump 1 -> 0 inside one control block ramps instead of stepping, and pitch sweeps
    // through every mip crossfade without a jump on the sine (whose levels are identical).
    const auto t = factoryTable (0);
    WavetableOscillator osc;
    osc.prepare (48000.0);
    osc.setTable (t.get());
    OscParams p;
    p.randomPhase = 0.0f;
    p.startPhase = 0.25f;   // start at the sine's peak
    osc.setParams (p);
    osc.noteOn (1.0f, 1u);
    std::vector<float> l (32), r (32);
    p.level = 0.0f;
    osc.setParams (p);
    osc.render (l.data(), r.data(), 32);
    CHECK (std::fabs (l[0] - 1.0f) < 0.05f);
    CHECK (maxStep (l) < 0.04f);
    CHECK (std::fabs (l[31]) < 1.0e-6f);

    // Exponential sweep 30 Hz -> 20 kHz over 2 s.
    p = OscParams();
    p.randomPhase = 0.0f;
    osc.setParams (p);
    osc.noteOn (30.0f, 1u);
    const int n = 96000;
    const auto s = renderOsc (osc, n, [&] (WavetableOscillator& o, int pos)
    {
        o.setFrequency (30.0f * std::pow (20000.0f / 30.0f, (float) pos / (float) n));
        o.setParams (p);
    });
    // On a sine every level holds the same sine, so the crossfades must be invisible: the RMS of every
    // 100 ms stays at 1/sqrt 2 (no dips or bumps where levels switch).
    double lo = 2.0, hi = 0.0;
    for (int b = 0; b + 4800 <= n; b += 4800)
    {
        double acc = 0.0;
        for (int i = b; i < b + 4800; ++i) acc += (double) s.l[(size_t) i] * s.l[(size_t) i];
        const double r = std::sqrt (acc / 4800.0);
        lo = std::min (lo, r);
        hi = std::max (hi, r);
    }
    CHECK (lo > 0.70);
    CHECK (hi < 0.715);
}

AP_TEST (osc_unison_stereo_and_level)
{
    const auto t = factoryTable (0);
    OscParams p;
    p.position = sawPosition();
    p.unison = 8;
    p.detuneCents = 50.0f;
    p.spread = 1.0f;
    p.blend = 1.0f;
    const auto wide = renderSimple (*t, 48000.0, 220.0f, p, 48000, 77u);
    double diff = 0.0;
    for (size_t i = 0; i < wide.l.size(); ++i) diff = std::max (diff, (double) std::fabs (wide.l[i] - wide.r[i]));
    CHECK (diff > 0.05);   // L != R

    p.spread = 0.0f;
    const auto narrow = renderSimple (*t, 48000.0, 220.0f, p, 48000, 77u);
    bool equal = true;
    for (size_t i = 0; i < narrow.l.size(); ++i) equal = equal && narrow.l[i] == narrow.r[i];
    CHECK (equal);   // spread 0 -> L == R exactly

    // Level: N = 8 at blend 1 is within +-3 dB of N = 1.
    OscParams one = p;
    one.unison = 1;
    const auto single = renderSimple (*t, 48000.0, 220.0f, one, 48000, 77u);
    const double ratioDb = 20.0 * std::log10 (rms (narrow.l, 2048) / rms (single.l, 2048));
    std::printf ("    unison 8 vs 1: %.2f dB\n", ratioDb);
    CHECK (std::fabs (ratioDb) < 3.0);
    // Wide stereo keeps the power too.
    const double wideDb = 10.0 * std::log10 ((rms (wide.l, 2048) * rms (wide.l, 2048) + rms (wide.r, 2048) * rms (wide.r, 2048))
                                             / (2.0 * rms (single.l, 2048) * rms (single.l, 2048)));
    CHECK (std::fabs (wideDb) < 3.0);

    // Spread keeps the image centred: L and R carry about the same power.
    const double balanceDb = 20.0 * std::log10 (rms (wide.l, 2048) / rms (wide.r, 2048));
    CHECK (std::fabs (balanceDb) < 1.5);

    // Blend 0 leaves only the centre copy (N odd): equal to N = 1 with the same phase for that copy.
    OscParams b0 = one;
    b0.unison = 3;
    b0.blend = 0.0f;
    b0.randomPhase = 0.0f;
    OscParams b1 = one;
    b1.randomPhase = 0.0f;
    const auto centreOnly = renderSimple (*t, 48000.0, 220.0f, b0, 4096, 5u);
    const auto mono = renderSimple (*t, 48000.0, 220.0f, b1, 4096, 5u);
    double err = 0.0;
    for (size_t i = 0; i < mono.l.size(); ++i) err = std::max (err, (double) std::fabs (centreOnly.l[i] - mono.l[i]));
    CHECK (err < 1.0e-5);
}

AP_TEST (osc_level_pan_and_count_changes)
{
    const auto t = factoryTable (0);
    OscParams p;
    p.randomPhase = 0.0f;
    p.pan = 1.0f;   // hard right: left silent, right unity
    const auto s = renderSimple (*t, 48000.0, 220.0f, p, 4800);
    CHECK (rms (s.l) < 1.0e-6);
    CHECK_NEAR (rms (s.r), std::sqrt (0.5), 0.01);

    // Changing the unison count mid-note ramps (no jump beyond the waveform's own slope).
    WavetableOscillator osc;
    osc.prepare (48000.0);
    osc.setTable (t.get());
    OscParams q;
    osc.setParams (q);
    osc.noteOn (110.0f, 9u);
    const auto out = renderOsc (osc, 9600, [&] (WavetableOscillator& o, int pos)
    {
        q.unison = pos < 4800 ? 1 : 16;
        o.setFrequency (110.0f);
        o.setParams (q);
    });
    CHECK (maxStep (out.l) < 0.05f);
    bool finite = true;
    for (float v : out.l) finite = finite && std::isfinite (v);
    CHECK (finite);
}

AP_TEST (osc_warp_modes_bounded_and_continuous)
{
    const auto t = factoryTable (0);
    std::vector<float> fm (9600);
    for (size_t i = 0; i < fm.size(); ++i) fm[i] = (float) std::sin (ap::kTwoPiD * 330.0 * (double) i / 48000.0);

    for (WarpMode m : kAllModes)
    {
        for (float pos : { 0.0f, sawPosition(), 0.8f })
        {
            OscParams p;
            p.position = pos;
            p.warp = m;
            p.randomPhase = 0.0f;

            OscParams off = p;
            off.warp = WarpMode::Off;
            const auto ref = renderSimple (*t, 48000.0, 220.0f, off, 9600, 1u, fm.data());

            // a = 0 equals Off.
            p.warpAmount = 0.0f;
            const auto zero = renderSimple (*t, 48000.0, 220.0f, p, 9600, 1u, fm.data());
            double err = 0.0;
            for (size_t i = 0; i < ref.l.size(); ++i) err = std::max (err, (double) std::fabs (zero.l[i] - ref.l[i]));
            CHECK (err < 1.0e-5);

            // Small a stays close to Off (continuous in a). Only on the smooth sine frame: on the saw a tiny
            // sync ratio legitimately moves the reset by a sample.
            if (pos == 0.0f)
            {
                p.warpAmount = 0.0002f;   // sync: m = 1.0014, the slave runs 0.14 % fast (deviation ~ 2 pi 0.0014)
                const auto tiny = renderSimple (*t, 48000.0, 220.0f, p, 9600, 1u, fm.data());
                double near = 0.0;
                for (size_t i = 0; i < ref.l.size(); ++i) near = std::max (near, (double) std::fabs (tiny.l[i] - ref.l[i]));
                std::printf ("    warp mode %d at a = 0.0002: max deviation from Off %.4f\n", (int) m, near);
                CHECK (near < 0.02);
            }

            // a = +-1: finite and bounded.
            for (float a : { -1.0f, 1.0f })
            {
                p.warpAmount = a;
                const auto s = renderSimple (*t, 48000.0, 220.0f, p, 9600, 1u, fm.data());
                bool finite = true;
                float peak = 0.0f;
                for (size_t i = 0; i < s.l.size(); ++i)
                {
                    finite = finite && std::isfinite (s.l[i]) && std::isfinite (s.r[i]);
                    peak = std::max (peak, std::max (std::fabs (s.l[i]), std::fabs (s.r[i])));
                }
                CHECK (finite);
                CHECK (peak <= 1.5f);
                CHECK (peak > 0.1f);
            }
        }
    }
}

AP_TEST (osc_warp_cycle_continuity_and_sweep)
{
    // The warped reads are continuous across the cycle wrap: on a sine frame (smooth), the biggest
    // sample step stays near what the warp's own slope allows, for every mode, while a sweeps -1 -> 1.
    const auto t = factoryTable (0);
    for (WarpMode m : kAllModes)
    {
        if (m == WarpMode::FmOsc || m == WarpMode::FmSub) continue;   // covered with an FM input below
        WavetableOscillator osc;
        osc.prepare (48000.0);
        osc.setTable (t.get());
        // Bend with a < 0 is p^k, k < 1, which is (by definition) infinitely steep at p = 0: sweep its
        // a >= 0 half here (a < 0 is covered by the bounded / finite test above).
        const float from = m == WarpMode::Bend ? 0.0f : -1.0f;
        OscParams p;
        p.warp = m;
        p.warpAmount = from;
        p.randomPhase = 0.0f;
        osc.setParams (p);
        osc.noteOn (100.0f, 1u);
        const int n = 48000;
        const auto s = renderOsc (osc, n, [&] (WavetableOscillator& o, int pos)
        {
            p.warpAmount = from + (1.0f - from) * (float) pos / (float) n;
            o.setFrequency (100.0f);
            o.setParams (p);
        });
        // Steepest legal slope: 8x sync on a sine at 100 Hz = 2 pi 800 / 48000 = 0.105; Squeeze 10x = 0.13;
        // Bend 4x = 0.052. The sync reset fades over 1/32 cycle (15 samples).
        const float step = maxStep (s.l);
        std::printf ("    warp mode %d sweep max step %.3f\n", (int) m, (double) step);
        CHECK (step < 0.25f);
    }

    // FM: null input behaves as Off; a NaN / huge FM input stays finite.
    OscParams p;
    p.warp = WarpMode::FmOsc;
    p.warpAmount = 1.0f;
    p.randomPhase = 0.0f;
    OscParams off = p;
    off.warp = WarpMode::Off;
    const auto a = renderSimple (*t, 48000.0, 220.0f, p, 2048, 1u, nullptr);
    const auto b = renderSimple (*t, 48000.0, 220.0f, off, 2048, 1u, nullptr);
    CHECK (a.l == b.l);
    std::vector<float> bad (2048, std::nanf (""));
    for (size_t i = 0; i < bad.size(); i += 3) bad[i] = 1.0e30f;
    const auto c = renderSimple (*t, 48000.0, 220.0f, p, 2048, 1u, bad.data());
    bool finite = true;
    for (float v : c.l) finite = finite && std::isfinite (v) && std::fabs (v) <= 1.5f;
    CHECK (finite);
}

AP_TEST (osc_determinism_and_phase)
{
    const auto t = factoryTable (3);
    OscParams p;
    p.unison = 6;
    p.position = 0.5f;
    const auto a = renderSimple (*t, 48000.0, 330.0f, p, 4096, 1234u);
    const auto b = renderSimple (*t, 48000.0, 330.0f, p, 4096, 1234u);
    const auto c = renderSimple (*t, 48000.0, 330.0f, p, 4096, 4321u);
    CHECK (a.l == b.l && a.r == b.r);   // same seed -> bit-identical
    CHECK (a.l != c.l);                  // random phase depends on the seed

    // Random phase 0: the start phase is exact (sine frame at phase 0.25 starts at its peak).
    OscParams q;
    q.randomPhase = 0.0f;
    q.startPhase = 0.25f;
    const auto s = renderSimple (*factoryTable (0), 48000.0, 100.0f, q, 64, 999u);
    CHECK_NEAR (s.l[0], 1.0, 1.0e-3);

    // No table: silence, no crash.
    WavetableOscillator osc;
    osc.prepare (48000.0);
    osc.noteOn (440.0f, 1u);
    std::vector<float> l (64, 1.0f), r (64, 1.0f);
    osc.render (l.data(), r.data(), 64);
    CHECK (rms (l) == 0.0 && rms (r) == 0.0);
}

AP_TEST (osc_cpu_budget)
{
    // 16 unison copies at 48 kHz, 2 s, per-block parameter updates with a moving position.
    const auto t = factoryTable (0);
    WavetableOscillator osc;
    osc.prepare (48000.0);
    osc.setTable (t.get());
    OscParams p;
    p.unison = 16;
    p.detuneCents = 30.0f;
    osc.setParams (p);
    osc.noteOn (110.0f, 5u);
    std::vector<float> l (kControlInterval), r (kControlInterval);
    const int blocks = 2 * 48000 / kControlInterval;
    double sink = 0.0;
    const auto start = std::chrono::steady_clock::now();
    for (int b = 0; b < blocks; ++b)
    {
        p.position = 0.5f + 0.4f * std::sin ((float) b * 0.01f);
        osc.setFrequency (110.0f);
        osc.setParams (p);
        osc.render (l.data(), r.data(), kControlInterval);
        sink += l[0];
    }
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
    const double load = secs / 2.0;
    std::printf ("    16-copy oscillator: %.2f %% of one core (sink %.3f)\n", load * 100.0, sink);
#ifdef NDEBUG
    CHECK (load < 0.03);   // target well under 1 %; loose bound for shared CI machines
#endif
}
