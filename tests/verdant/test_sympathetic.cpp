// Sympathetic memory (W-G): exact bypass, tuning to the remembered notes,
// decay after the excitation stops, note memory / string assignment rules,
// glitch-free retuning, key snapping, long-run stability and CPU.

#include "TestHarness.h"
#include "FxTestHelpers.h"

#include "engine/Sympathetic.h"

#include <ap/Math.h>
#include <ap/Scales.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

using namespace verdant;
using namespace fxtest;

namespace
{
    constexpr int kBlock = 512;
    constexpr double kRate = 48000.0;

    struct Settings
    {
        float amount = 1.0f;
        int memory = 4;
        float decay = 0.5f, bright = 0.5f, damp = 0.3f;
        int strings = 6;
        bool snap = false;
        ap::Scale scale = ap::Scale::fromType (ap::ScaleType::Chromatic, 0);
    };

    void apply (Sympathetic& s, const Settings& p)
    {
        s.setParams (p.amount, p.memory, p.decay, p.bright, p.damp, p.strings, p.snap, p.scale);
    }

    std::unique_ptr<Sympathetic> makeBody (const Settings& p, double sr = kRate)
    {
        auto s = std::make_unique<Sympathetic>();
        s->prepare (sr, kBlock);
        apply (*s, p);
        return s;
    }

    std::vector<float> noise (size_t n, uint32_t seed, float amp)
    {
        ap::Rng rng;
        rng.state = seed;
        std::vector<float> v (n);
        for (auto& x : v) x = rng.nextBipolar() * amp;
        return v;
    }

    /** Excites the body with `excite` seconds of white noise then renders `silence` seconds; returns mono wet output. */
    std::vector<float> ringOut (Sympathetic& s, const Settings& p, double excite, double silence, double sr = kRate, float amp = 0.3f)
    {
        const size_t ne = (size_t) (excite * sr), n = ne + (size_t) (silence * sr);
        auto L = noise (n, 0x1234u, amp);
        std::fill (L.begin() + (long) ne, L.end(), 0.0f);
        auto R = L;
        const auto dry = L;
        inBlocks (L, R, kBlock, [&] (float* l, float* r, int len, size_t)
        {
            apply (s, p);
            s.process (l, r, len);
        });
        std::vector<float> mono (n);
        for (size_t i = 0; i < n; ++i) mono[i] = 0.5f * (L[i] + R[i]) - dry[i];
        return mono;
    }

    /** Largest spectral peak between loHz and hiHz, in Hz. */
    double peakHz (const std::vector<float>& v, size_t from, double sr, double loHz, double hiHz)
    {
        constexpr int kFft = 65536;
        const auto mag = magnitudeSpectrum (v, from, kFft);
        size_t best = 1;
        for (size_t k = 1; k + 1 < mag.size(); ++k)
        {
            const double hz = (double) k * sr / kFft;
            if (hz < loHz || hz > hiHz) continue;
            if (mag[k] > mag[best] || best == 1) best = k;
        }
        // Parabolic interpolation around the peak bin.
        const double a = mag[best - 1], b = mag[best], c = mag[best + 1];
        const double denom = a - 2.0 * b + c;
        const double shift = std::fabs (denom) > 1.0e-12 ? 0.5 * (a - c) / denom : 0.0;
        return ((double) best + shift) * sr / kFft;
    }

    /** Median tone amplitude at off-note probe frequencies (the "floor" between partials). */
    double floorAmp (const std::vector<float>& v, size_t from, size_t to, std::initializer_list<double> probes)
    {
        std::vector<double> a;
        for (double hz : probes) a.push_back (toneAmp (v, from, to, hz, kRate));
        std::sort (a.begin(), a.end());
        return a[a.size() / 2];
    }

    int assignedStrings (const Sympathetic& s)
    {
        int n = 0;
        for (int i = 0; i < Sympathetic::kMaxStrings; ++i) n += s.stringNote (i) >= 0 ? 1 : 0;
        return n;
    }
}

//==============================================================================
AP_TEST (symp_amount_zero_is_bit_exact_bypass)
{
    Settings p;
    p.amount = 0.0f;
    auto s = makeBody (p);
    s->noteOn (60);
    const size_t n = 30000;
    const auto inL = noise (n, 7u, 0.5f), inR = noise (n, 8u, 0.5f);
    auto L = inL, R = inR;
    inBlocks (L, R, kBlock, [&] (float* l, float* r, int len, size_t) { apply (*s, p); s->process (l, r, len); });
    CHECK (std::memcmp (L.data(), inL.data(), n * sizeof (float)) == 0);
    CHECK (std::memcmp (R.data(), inR.data(), n * sizeof (float)) == 0);

    // After ringing at full amount, turning it off returns to an exact bypass (the
    // strings keep running underneath, so the output is exactly the input).
    p.amount = 1.0f;
    L = inL; R = inR;
    inBlocks (L, R, kBlock, [&] (float* l, float* r, int len, size_t) { apply (*s, p); s->process (l, r, len); });
    p.amount = 0.0f;
    L = inL; R = inR;
    inBlocks (L, R, kBlock, [&] (float* l, float* r, int len, size_t) { apply (*s, p); s->process (l, r, len); });
    const size_t settled = 2048;   // the 20 ms amount ramp
    CHECK (std::memcmp (L.data() + settled, inL.data() + settled, (n - settled) * sizeof (float)) == 0);
    CHECK (std::memcmp (R.data() + settled, inR.data() + settled, (n - settled) * sizeof (float)) == 0);
}

AP_TEST (symp_rings_at_the_remembered_note)
{
    Settings p;
    auto s = makeBody (p);
    s->noteOn (60);
    const auto out = ringOut (*s, p, 0.5, 1.5);
    CHECK (allFinite (out));

    const size_t from = (size_t) (0.55 * kRate);
    const double c4 = ap::midiToHz (60.0f);
    const double peak = peakHz (out, from, kRate, 100.0, 5000.0);
    const double harmonic = std::max (1.0, std::round (peak / c4));
    CHECK (std::fabs (peak - harmonic * c4) < 0.02 * harmonic * c4);   // the peak is the note or one of its harmonics
    CHECK_NEAR (peakHz (out, from, kRate, 200.0, 330.0), c4, 0.02 * c4);  // and the fundamental is there

    const size_t to = from + (size_t) (0.5 * kRate);
    const double fundamental = toneAmp (out, from, to, c4, kRate);
    const double between = floorAmp (out, from, to, { 1.5 * c4, 2.5 * c4, 1.25 * c4, 1.75 * c4, 2.25 * c4 });
    CHECK (db (fundamental / between) > 30.0);
    CHECK (rms (out, from, to) > 1.0e-3);
}

AP_TEST (symp_decays_after_the_excitation_stops)
{
    Settings p;   // decay 50 %
    auto s = makeBody (p);
    s->noteOn (60);
    const double rt60 = Sympathetic::rt60Seconds (p.decay);
    CHECK_NEAR (rt60, 0.5 * (0.3 + 8.0), 1.0e-5);

    const double excite = 0.5;
    const auto out = ringOut (*s, p, excite, rt60 * 2.0 + 0.2);
    const size_t win = (size_t) (0.05 * kRate);
    const size_t stop = (size_t) (excite * kRate);
    const double ref = rms (out, stop, stop + win);
    const size_t at = stop + (size_t) (rt60 * 2.0 * kRate);
    CHECK (ref > 1.0e-3);
    CHECK (db (rms (out, at, at + win) / ref) < -60.0);

    // Monotone decay in 0.5 s steps (strings = memory: no detuned pair, so no unison beating).
    Settings q;
    q.strings = 4;
    auto t = makeBody (q);
    t->noteOn (60);
    const auto mono = ringOut (*t, q, excite, 4.0);
    bool grows = false;
    for (size_t t0 = stop; t0 + (size_t) kRate <= mono.size(); t0 += (size_t) (0.5 * kRate))
        if (rms (mono, t0 + (size_t) (0.5 * kRate), t0 + (size_t) (0.5 * kRate) + win) > rms (mono, t0, t0 + win) * 1.1) grows = true;
    CHECK (! grows);
}

AP_TEST (symp_two_notes_both_present)
{
    Settings p;
    auto s = makeBody (p);
    s->noteOn (60);
    s->noteOn (67);
    const auto out = ringOut (*s, p, 0.5, 1.0);
    const size_t from = (size_t) (0.55 * kRate), to = from + (size_t) (0.5 * kRate);
    const double c = toneAmp (out, from, to, ap::midiToHz (60.0f), kRate);
    const double g = toneAmp (out, from, to, ap::midiToHz (67.0f), kRate);
    const double between = floorAmp (out, from, to, { 300.0, 340.0, 460.0, 480.0, 600.0 });
    CHECK (db (c / between) > 20.0);
    CHECK (db (g / between) > 20.0);

    int remembered[Sympathetic::kMaxMemory];
    CHECK_EQ (s->rememberedNotes (remembered, Sympathetic::kMaxMemory), 2);
    CHECK_EQ (remembered[0], 67);
    CHECK_EQ (remembered[1], 60);
}

AP_TEST (symp_memory_one_keeps_only_the_latest)
{
    Settings p;
    p.memory = 1;
    auto s = makeBody (p);
    s->noteOn (60);
    s->noteOn (64);
    int remembered[Sympathetic::kMaxMemory];
    CHECK_EQ (s->rememberedNotes (remembered, Sympathetic::kMaxMemory), 1);
    CHECK_EQ (remembered[0], 64);

    const auto out = ringOut (*s, p, 0.5, 1.0);
    const size_t from = (size_t) (0.55 * kRate), to = from + (size_t) (0.5 * kRate);
    const double c = toneAmp (out, from, to, ap::midiToHz (60.0f), kRate);
    const double e = toneAmp (out, from, to, ap::midiToHz (64.0f), kRate);
    CHECK (db (e / c) > 30.0);

    // Duplicates move to the front instead of taking two places; memory caps the list.
    Settings q;
    q.memory = 3;
    auto t = makeBody (q);
    for (int note : { 60, 62, 64, 60, 65 }) t->noteOn (note);
    const int count = t->rememberedNotes (remembered, Sympathetic::kMaxMemory);
    CHECK_EQ (count, 3);
    CHECK (remembered[0] == 65 && remembered[1] == 60 && remembered[2] == 64);
}

AP_TEST (symp_string_count_caps_and_pairs)
{
    // Strings < remembered notes: only the most recent notes get a string.
    Settings p;
    p.strings = 1;
    p.memory = 4;
    auto s = makeBody (p);
    s->noteOn (60);
    s->noteOn (64);
    CHECK_EQ (assignedStrings (*s), 1);
    const auto out = ringOut (*s, p, 0.5, 1.0);
    const size_t from = (size_t) (0.55 * kRate), to = from + (size_t) (0.5 * kRate);
    CHECK (db (toneAmp (out, from, to, ap::midiToHz (64.0f), kRate) / toneAmp (out, from, to, ap::midiToHz (60.0f), kRate)) > 30.0);

    // Strings 3, memory 4, four notes: three strings, the three newest notes.
    Settings q;
    q.strings = 3;
    auto t = makeBody (q);
    for (int note : { 48, 52, 55, 59 }) t->noteOn (note);
    CHECK_EQ (assignedStrings (*t), 3);
    bool has48 = false;
    for (int i = 0; i < Sympathetic::kMaxStrings; ++i) has48 = has48 || t->stringNote (i) == 48;
    CHECK (! has48);

    // Strings > memory: the extra strings pair up with the notes (shimmer), all strings sound.
    Settings r;
    r.strings = 8;
    r.memory = 2;
    auto u = makeBody (r);
    u->noteOn (60);
    u->noteOn (67);
    CHECK_EQ (assignedStrings (*u), 8);
    int on60 = 0, on67 = 0;
    for (int i = 0; i < Sympathetic::kMaxStrings; ++i)
    {
        on60 += u->stringNote (i) == 60 ? 1 : 0;
        on67 += u->stringNote (i) == 67 ? 1 : 0;
    }
    CHECK_EQ (on60, 4);
    CHECK_EQ (on67, 4);
}

AP_TEST (symp_retuning_while_ringing_is_glitch_free)
{
    // One string, one note of memory: a new note must retune the ringing string.
    Settings p;
    p.memory = 1;
    p.strings = 1;
    p.decay = 0.8f;
    auto s = makeBody (p);
    s->noteOn (60);

    const size_t n = (size_t) (1.5 * kRate);
    auto L = noise (n, 99u, 0.25f);
    std::fill (L.begin() + (long) (0.3 * kRate), L.end(), 0.0f);
    auto R = L;
    const auto dry = L;
    const size_t retuneAt = (size_t) (0.5 * kRate);
    inBlocks (L, R, kBlock, [&] (float* l, float* r, int len, size_t pos)
    {
        if (pos <= retuneAt && retuneAt < pos + (size_t) len)
        {
            s->process (l, r, (int) (retuneAt - pos));
            s->noteOn (67);   // the ringing string retunes C4 -> G4
            s->process (l + (retuneAt - pos), r + (retuneAt - pos), len - (int) (retuneAt - pos));
            return;
        }
        apply (*s, p);
        s->process (l, r, len);
    });
    std::vector<float> wet (n);
    for (size_t i = 0; i < n; ++i) wet[i] = L[i] - dry[i];

    const double before = rms (wet, retuneAt - (size_t) (0.05 * kRate), retuneAt);
    CHECK (before > 0.01);   // really ringing when it retunes
    CHECK (maxStep (L, (size_t) (0.31 * kRate), n) < 0.1);
    CHECK (maxStep (R, (size_t) (0.31 * kRate), n) < 0.1);
    // No jump right at the retune beyond what the ringing itself does.
    const double ringingStep = maxStep (wet, retuneAt - (size_t) (0.1 * kRate), retuneAt);
    CHECK (maxStep (wet, retuneAt, retuneAt + (size_t) (0.02 * kRate)) < 1.5 * ringingStep + 1.0e-3);

    // It now rings at G4.
    const size_t from = (size_t) (0.7 * kRate), to = from + (size_t) (0.5 * kRate);
    CHECK (db (toneAmp (wet, from, to, ap::midiToHz (67.0f), kRate) / toneAmp (wet, from, to, ap::midiToHz (60.0f), kRate)) > 20.0);
}

AP_TEST (symp_snap_pulls_notes_into_the_key)
{
    Settings p;
    p.snap = true;
    p.scale = ap::Scale::fromType (ap::ScaleType::Major, 0);
    p.strings = 1;
    p.memory = 1;
    auto s = makeBody (p);
    s->noteOn (61);
    const int tuned = s->stringNote (0) >= 0 ? s->stringNote (0) : s->stringNote (1);
    CHECK (tuned == 60 || tuned == 62);

    int remembered[Sympathetic::kMaxMemory];
    CHECK_EQ (s->rememberedNotes (remembered, Sympathetic::kMaxMemory), 1);
    CHECK_EQ (remembered[0], 61);   // memory keeps the played note; snapping happens on tuning

    const auto out = ringOut (*s, p, 0.5, 1.0);
    const size_t from = (size_t) (0.55 * kRate);
    const double hz = peakHz (out, from, kRate, 200.0, 330.0);
    const double target = ap::midiToHz ((float) tuned);
    CHECK_NEAR (hz, target, 0.02 * target);

    // Snap off: the same note rings at C#.
    Settings q = p;
    q.snap = false;
    apply (*s, q);
    int found = -1;
    for (int i = 0; i < Sympathetic::kMaxStrings; ++i) if (s->stringNote (i) >= 0) found = s->stringNote (i);
    CHECK_EQ (found, 61);
}

AP_TEST (symp_long_random_run_is_stable)
{
    // Worst case: full amount, longest decay, brightest, 12 strings over 8 notes, 60 s.
    Settings p;
    p.amount = 1.0f;
    p.memory = 8;
    p.strings = 12;
    p.decay = 1.0f;
    p.bright = 1.0f;
    p.damp = 0.0f;
    const double sr = 44100.0;
    auto s = makeBody (p, sr);

    ap::Rng rng;
    rng.state = 0xC0FFEEu;
    std::vector<float> L ((size_t) kBlock), R ((size_t) kBlock);
    const int blocks = (int) (60.0 * sr / kBlock);
    bool finite = true;
    double peak = 0.0;
    for (int b = 0; b < blocks; ++b)
    {
        if (rng.nextUnipolar() < 0.15f)
            s->noteOn (24 + (int) (rng.nextUnipolar() * 84.0f));
        if (rng.nextUnipolar() < 0.02f)
        {
            p.strings = 1 + (int) (rng.nextUnipolar() * 12.0f);
            p.memory = 1 + (int) (rng.nextUnipolar() * 8.0f);
            p.decay = rng.nextUnipolar();
            p.bright = rng.nextUnipolar();
            p.snap = rng.nextUnipolar() < 0.5f;
        }
        const float level = (b / 200) % 2 == 0 ? 0.9f : 0.0f;   // bursts and silences
        for (int i = 0; i < kBlock; ++i)
        {
            L[(size_t) i] = rng.nextBipolar() * level;
            R[(size_t) i] = rng.nextBipolar() * level;
        }
        apply (*s, p);
        s->process (L.data(), R.data(), kBlock);
        for (int i = 0; i < kBlock; ++i)
        {
            finite = finite && std::isfinite (L[(size_t) i]) && std::isfinite (R[(size_t) i]);
            peak = std::max ({ peak, (double) std::fabs (L[(size_t) i]), (double) std::fabs (R[(size_t) i]) });
        }
    }
    CHECK (finite);
    CHECK (peak < 4.0);
}

AP_TEST (symp_cpu_twelve_strings)
{
    Settings p;
    p.memory = 8;
    p.strings = 12;
    auto s = makeBody (p);
    for (int note : { 48, 52, 55, 59, 62, 65, 69, 72 }) s->noteOn (note);
    CHECK_EQ (assignedStrings (*s), 12);

    const size_t n = (size_t) (2.0 * kRate);
    auto L = noise (n, 5u, 0.3f), R = noise (n, 6u, 0.3f);
    const auto start = std::chrono::steady_clock::now();
    inBlocks (L, R, kBlock, [&] (float* l, float* r, int len, size_t) { apply (*s, p); s->process (l, r, len); });
    const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
    CHECK (allFinite (L) && allFinite (R));
#ifdef NDEBUG
    CHECK (seconds < 0.2);   // < 10 % of real time for 2 s of audio
#else
    (void) seconds;
#endif
}
