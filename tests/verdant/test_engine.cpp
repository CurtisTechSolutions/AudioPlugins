// Engine / Voice integration (CONTRACT §2, §8): pitch accuracy, release,
// polyphony limit and stealing, mono / legato retriggering, glide time, pitch
// bend, pitch snap, matrix-driven vibrato and filter sweeps, determinism,
// sample-accurate events, routing, sustain, user tables, sub / noise and the
// CPU budget. 48 kHz, default patch unless stated.
//
// The tests measure audio, so they are meaningful with the foundation stubs
// (frame-0 sine oscillator, biquad low-pass, linear envelopes, identity living)
// and with the real modules alike; where a test needs real-module behaviour it
// says so in a comment.

#include "TestHarness.h"

#include "engine/Engine.h"
#include "engine/Patch.h"
#include "engine/SubNoise.h"

#include <ap/Math.h>
#include <ap/PitchDetector.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

using namespace verdant;

namespace
{
    constexpr double kRate = 48000.0;
    constexpr int kBlock = 512;

    MidiEvent ev (MidiEvent::Type t, int note, float value, int offset = 0) { return MidiEvent { t, offset, 1, note, value, 0 }; }
    MidiEvent noteOn (int note, float velocity = 100.0f / 127.0f, int offset = 0) { return ev (MidiEvent::Type::NoteOn, note, velocity, offset); }
    MidiEvent noteOff (int note, int offset = 0) { return ev (MidiEvent::Type::NoteOff, note, 0.0f, offset); }

    struct Rig
    {
        std::unique_ptr<Engine> engine = std::make_unique<Engine>();
        std::vector<float> L = std::vector<float> ((size_t) kBlock), R = std::vector<float> ((size_t) kBlock);
        std::vector<float> mono, left, right;
        bool finite = true;

        explicit Rig (const ParamValues& values = defaultValues(), uint32_t seed = 0x5EED1234u)
        {
            engine->setLivingSeed (seed);
            engine->prepare (kRate, kBlock);
            engine->setParams (values);
        }

        /** One block; `events` apply to it. */
        void block (const std::vector<MidiEvent>& events = {})
        {
            engine->process (L.data(), R.data(), kBlock, events.empty() ? nullptr : events.data(), (int) events.size());
            for (int i = 0; i < kBlock; ++i)
            {
                if (! std::isfinite (L[(size_t) i]) || ! std::isfinite (R[(size_t) i])) finite = false;
                mono.push_back (0.5f * (L[(size_t) i] + R[(size_t) i]));
                left.push_back (L[(size_t) i]);
                right.push_back (R[(size_t) i]);
            }
        }

        void blocks (int count, const std::vector<MidiEvent>& firstBlockEvents = {})
        {
            for (int b = 0; b < count; ++b)
                block (b == 0 ? firstBlockEvents : std::vector<MidiEvent> {});
        }

        void seconds (double s, const std::vector<MidiEvent>& firstBlockEvents = {})
        {
            blocks ((int) std::ceil (s * kRate / kBlock), firstBlockEvents);
        }

        size_t now() const { return mono.size(); }
    };

    int blocksFor (double seconds) { return (int) std::ceil (seconds * kRate / kBlock); }

    double rms (const std::vector<float>& v, size_t from, size_t to)
    {
        to = std::min (to, v.size());
        if (to <= from) return 0.0;
        double acc = 0.0;
        for (size_t i = from; i < to; ++i) acc += (double) v[i] * v[i];
        return std::sqrt (acc / (double) (to - from));
    }

    double diffRms (const std::vector<float>& v, size_t from, size_t to)
    {
        to = std::min (to, v.size());
        if (to <= from + 1) return 0.0;
        double acc = 0.0;
        for (size_t i = from + 1; i < to; ++i) { const double d = (double) v[i] - v[i - 1]; acc += d * d; }
        return std::sqrt (acc / (double) (to - from - 1));
    }

    float yinHz (const std::vector<float>& v, size_t from)
    {
        ap::YinPitchDetector yin;
        yin.prepare (kRate, 2048, 50.0f, 1500.0f);
        if (from + (size_t) yin.requiredSamples() > v.size()) return 0.0f;
        const auto r = yin.analyze (v.data() + from);
        return r.voiced ? r.frequency : 0.0f;
    }

    /** Goertzel magnitude (normalised to a sine's amplitude) at `hz` over [from, to). */
    double toneLevel (const std::vector<float>& v, double hz, size_t from, size_t to)
    {
        to = std::min (to, v.size());
        if (to <= from) return 0.0;
        const double w = ap::kTwoPiD * hz / kRate, c = 2.0 * std::cos (w);
        double s1 = 0.0, s2 = 0.0;
        for (size_t i = from; i < to; ++i)
        {
            const double s0 = (double) v[i] + c * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        const double power = s1 * s1 + s2 * s2 - c * s1 * s2;
        return 2.0 * std::sqrt (std::max (0.0, power)) / (double) (to - from);
    }

    struct CyclePitch { double timeSec; double midi; };

    /** Instantaneous pitch per cycle from interpolated rising zero crossings (for pure tones). */
    std::vector<CyclePitch> cyclePitches (const std::vector<float>& v, size_t from, size_t to)
    {
        std::vector<CyclePitch> out;
        to = std::min (to, v.size());
        double last = -1.0;
        for (size_t i = std::max<size_t> (from, 1); i < to; ++i)
        {
            if (v[i - 1] < 0.0f && v[i] >= 0.0f)
            {
                const double t = (double) (i - 1) + (double) v[i - 1] / ((double) v[i - 1] - (double) v[i]);
                if (last >= 0.0 && t > last)
                    out.push_back ({ 0.5 * (t + last) / kRate, (double) ap::hzToMidi ((float) (kRate / (t - last))) });
                last = t;
            }
        }
        return out;
    }

    /** Defaults with the per-note living variation off (pitch-exact tests). */
    ParamValues still()
    {
        auto v = defaultValues();
        v[(size_t) P::Alive] = 0.0f;
        v[(size_t) P::Evolve] = 0.0f;
        return v;
    }

    void setSlot (ParamValues& v, int slot, ModSource src, P target, float amountPercent)
    {
        v[(size_t) modSrc (slot)] = (float) src;
        v[(size_t) modDst (slot)] = (float) modDestinationIndexOf (target);
        v[(size_t) modAmt (slot)] = amountPercent;
    }
}

//==============================================================================
AP_TEST (engine_c4_pitch_level_and_release)
{
    Rig r;
    r.seconds (1.0, { noteOn (60) });
    CHECK (r.finite);
    CHECK (rms (r.mono, 2400, r.now()) > 0.01);
    for (size_t at : { (size_t) 4800, (size_t) 24000, (size_t) 40000 })
        CHECK_NEAR (yinHz (r.mono, at), 261.63, 261.63 * 0.005);

    // Release: below 1e-4 RMS within 3 s of the note-off, and the voice frees itself.
    const size_t offAt = r.now();
    r.seconds (3.0, { noteOff (60) });
    CHECK (r.finite);
    size_t silentAt = 0;
    for (size_t b = offAt; b + kBlock <= r.now(); b += kBlock)
        if (rms (r.mono, b, b + kBlock) < 1.0e-4) { silentAt = b; break; }
    CHECK (silentAt > offAt);
    CHECK ((double) (silentAt - offAt) / kRate < 3.0);
    CHECK (rms (r.mono, r.now() - 4 * kBlock, r.now()) < 1.0e-4);
    CHECK_EQ (r.engine->visuals().activeVoices.load(), 0);
}

AP_TEST (engine_polyphony_limit_keeps_newest_notes)
{
    auto v = still();
    v[(size_t) P::Voices] = 4.0f;
    Rig r (v);
    const int notes[6] = { 48, 52, 55, 59, 62, 65 };
    int maxActive = 0;
    for (int k = 0; k < 6; ++k)
    {
        r.block ({ noteOn (notes[k]) });   // one new note per block
        maxActive = std::max (maxActive, r.engine->visuals().activeVoices.load());
    }
    const size_t from = r.now() + (size_t) (0.05 * kRate);
    for (int b = 0; b < blocksFor (0.5); ++b)
    {
        r.block();
        maxActive = std::max (maxActive, r.engine->visuals().activeVoices.load());
    }
    CHECK (r.finite);
    CHECK (maxActive <= 4);
    CHECK_EQ (r.engine->visuals().activeVoices.load(), 4);

    double newest = 1.0e9, stolen = 0.0;
    for (int k = 0; k < 6; ++k)
    {
        const double level = toneLevel (r.mono, ap::midiToHz ((float) notes[k]), from, r.now());
        if (k < 2) stolen = std::max (stolen, level);
        else newest = std::min (newest, level);
    }
    std::printf ("  poly 4 of 6: newest notes >= %.4f, stolen <= %.6f\n", newest, stolen);
    CHECK (newest > 0.005);
    CHECK (stolen < newest * 0.05);
}

AP_TEST (engine_single_voice_steals)
{
    auto v = still();
    v[(size_t) P::Voices] = 1.0f;
    Rig r (v);
    r.seconds (0.2, { noteOn (60) });
    const size_t from = r.now() + (size_t) (0.05 * kRate);
    r.seconds (0.5, { noteOn (67) });
    CHECK (r.finite);
    CHECK_EQ (r.engine->visuals().activeVoices.load(), 1);
    const double c4 = toneLevel (r.mono, ap::midiToHz (60.0f), from, r.now());
    const double g4 = toneLevel (r.mono, ap::midiToHz (67.0f), from, r.now());
    CHECK (g4 > 0.005);
    CHECK (g4 > c4 * 20.0);
    CHECK_NEAR (yinHz (r.mono, from), ap::midiToHz (67.0f), ap::midiToHz (67.0f) * 0.005);

    // The steal is a short fade, not a click: no sample-to-sample jump near the
    // switch is larger than the steady tone's own largest step.
    double steadyStep = 0.0, switchStep = 0.0;
    const size_t at = (size_t) blocksFor (0.2) * kBlock;
    for (size_t i = at - 2000; i < at - 10; ++i) steadyStep = std::max (steadyStep, (double) std::fabs (r.mono[i] - r.mono[i - 1]));
    for (size_t i = at; i < at + 300; ++i) switchStep = std::max (switchStep, (double) std::fabs (r.mono[i] - r.mono[i - 1]));
    CHECK (switchStep < steadyStep * 1.5 + 1.0e-4);
}

AP_TEST (engine_mono_retriggers_legato_does_not)
{
    float envAfter[2] = {};
    for (int mode = 1; mode <= 2; ++mode)   // Mono, Legato
    {
        auto v = still();
        v[(size_t) P::VoiceMode] = (float) mode;
        v[(size_t) envParam (0, EnvParam::A)] = 200.0f;
        v[(size_t) envParam (0, EnvParam::D)] = 200.0f;
        v[(size_t) envParam (0, EnvParam::S)] = 50.0f;
        Rig r (v);
        r.seconds (0.6, { noteOn (60) });
        CHECK_NEAR (r.engine->visuals().envValue[0].load(), 0.5, 0.02);   // in sustain
        r.seconds (0.1, { noteOn (64) });                                   // overlapping second note
        envAfter[mode - 1] = r.engine->visuals().envValue[0].load();
        CHECK_EQ (r.engine->visuals().activeVoices.load(), 1);
        CHECK (r.finite);

        // Both glide-free modes play the new note.
        CHECK_NEAR (yinHz (r.mono, r.now() - 4200), ap::midiToHz (64.0f), ap::midiToHz (64.0f) * 0.005);
    }
    std::printf ("  Env 1 100 ms after the 2nd note: mono %.3f, legato %.3f\n", envAfter[0], envAfter[1]);
    CHECK (envAfter[0] > 0.6f);              // Mono: the attack restarted from 0.5
    CHECK_NEAR (envAfter[1], 0.5, 0.02);     // Legato: still in sustain
}

AP_TEST (engine_mono_note_stack_returns_to_held_note)
{
    auto v = still();
    v[(size_t) P::VoiceMode] = 1.0f;   // Mono
    Rig r (v);
    r.seconds (0.2, { noteOn (60) });
    r.seconds (0.2, { noteOn (64) });
    CHECK_NEAR (yinHz (r.mono, r.now() - 4000), ap::midiToHz (64.0f), 2.0);
    r.seconds (0.3, { noteOff (64) });   // back to the held C4
    CHECK_NEAR (yinHz (r.mono, r.now() - 4000), ap::midiToHz (60.0f), 2.0);
    CHECK_EQ (r.engine->visuals().activeVoices.load(), 1);
    r.seconds (1.0, { noteOff (60) });
    CHECK (rms (r.mono, r.now() - 2048, r.now()) < 1.0e-4);
}

AP_TEST (engine_glide_time)
{
    auto v = still();
    v[(size_t) P::VoiceMode] = 1.0f;   // Mono: one voice, so the pitch is measurable
    v[(size_t) P::Glide] = 200.0f;
    Rig r (v);
    r.seconds (0.5, { noteOn (60) });
    const size_t start = r.now();
    r.seconds (0.6, { noteOn (67) });
    CHECK (r.finite);

    const auto pitches = cyclePitches (r.mono, start, r.now());
    const double t0 = (double) start / kRate;
    double settled = -1.0, at50 = 0.0;
    for (size_t i = 0; i < pitches.size(); ++i)
    {
        const double t = pitches[i].timeSec - t0;
        const double cents = std::fabs (pitches[i].midi - 67.0) * 100.0;
        if (t <= 0.05) at50 = cents;
        if (cents > 5.0) settled = -1.0;
        else if (settled < 0.0) settled = t;
    }
    std::printf ("  glide 200 ms: %.0f ct away at 50 ms, within 5 ct after %.1f ms\n", at50, settled * 1000.0);
    CHECK (at50 >= 50.0);
    CHECK (settled >= 0.16 && settled <= 0.24);

    // Poly glide starts from the last note-on's pitch even on a fresh voice.
    auto pv = still();
    pv[(size_t) P::Glide] = 200.0f;
    Rig p (pv);
    p.seconds (0.1, { noteOn (60) });
    p.seconds (1.0, { noteOff (60) });   // fully released
    const size_t s2 = p.now();
    p.seconds (0.3, { noteOn (67) });
    const auto pp = cyclePitches (p.mono, s2, p.now());
    bool earlyFar = false, lateClose = false;
    for (const auto& c : pp)
    {
        const double t = c.timeSec - (double) s2 / kRate;
        if (t > 0.01 && t < 0.05 && std::fabs (c.midi - 67.0) > 0.5) earlyFar = true;
        if (t > 0.25 && std::fabs (c.midi - 67.0) < 0.05) lateClose = true;
    }
    CHECK (earlyFar);
    CHECK (lateClose);
}

AP_TEST (engine_pitch_bend_ratio)
{
    for (float bend : { 1.0f, -1.0f })
    {
        Rig r (still());
        r.seconds (0.5, { ev (MidiEvent::Type::PitchBend, 0, bend), noteOn (60) });
        const double expected = 261.63 * std::pow (2.0, 2.0 * bend / 12.0);   // bend_range 2 st
        CHECK_NEAR (yinHz (r.mono, 8000), expected, expected * 0.005);
    }

    // A bend during the note moves the sounding pitch.
    Rig r (still());
    r.seconds (0.3, { noteOn (60) });
    r.seconds (0.3, { ev (MidiEvent::Type::PitchBend, 0, 0.5f) });
    CHECK_NEAR (yinHz (r.mono, r.now() - 4000), 261.63 * std::pow (2.0, 1.0 / 12.0), 261.63 * 0.006);
}

AP_TEST (engine_pitch_snap_to_key)
{
    auto v = still();
    v[(size_t) P::KeyRoot] = 0.0f;     // C
    v[(size_t) P::KeyScale] = 1.0f;    // Major
    v[(size_t) P::Pitch] = 1.0f;       // +1 st global: C4 -> C#4 unsnapped
    v[(size_t) P::PitchSnap] = 1.0f;
    Rig snapped (v);
    snapped.seconds (0.5, { noteOn (60) });
    const float hz = yinHz (snapped.mono, 8000);
    const double midi = ap::hzToMidi (hz);
    std::printf ("  pitch snap: C4 + 1 st in C major plays MIDI %.2f\n", midi);
    CHECK (std::fabs (midi - 60.0) < 0.05 || std::fabs (midi - 62.0) < 0.05);
    CHECK (std::fabs (midi - 61.0) > 0.5);

    v[(size_t) P::PitchSnap] = 0.0f;
    Rig unsnapped (v);
    unsnapped.seconds (0.5, { noteOn (60) });
    CHECK_NEAR (ap::hzToMidi (yinHz (unsnapped.mono, 8000)), 61.0, 0.05);
}

AP_TEST (engine_mod_wheel_drives_vibrato_through_the_matrix)
{
    // LFO 1 -> Osc A Pitch (+-1.92 st); Mod Wheel -> LFO 1 Rate. With the wheel down
    // the LFO crawls at 0.01 Hz (almost no spread); wheel up = 1 Hz vibrato.
    auto v = still();
    v[(size_t) lfoParam (0, LfoParam::Rate)] = 0.01f;
    setSlot (v, 0, ModSource::Lfo1, oscParam (0, OscParam::Pitch), 2.0f);
    setSlot (v, 1, ModSource::ModWheel, lfoParam (0, LfoParam::Rate), 50.0f);

    double spread[2] = {};
    for (int w = 0; w < 2; ++w)
    {
        Rig r (v);
        r.seconds (1.2, { ev (MidiEvent::Type::ModWheel, 0, (float) w), noteOn (60) });
        CHECK (r.finite);
        double lo = 1.0e9, hi = -1.0e9;
        for (const auto& c : cyclePitches (r.mono, (size_t) (0.1 * kRate), r.now()))
        {
            lo = std::min (lo, c.midi);
            hi = std::max (hi, c.midi);
        }
        spread[w] = (hi - lo) * 100.0;
    }
    std::printf ("  vibrato spread: wheel 0 %.1f ct, wheel 1 %.1f ct\n", spread[0], spread[1]);
    CHECK (spread[0] < 50.0);
    CHECK (spread[1] > 200.0);
}

AP_TEST (engine_free_lfo_is_shared_retrig_restarts)
{
    // LFO 1 (1 Hz) -> Osc A Pitch (+-1.92 st). Two short notes 350 ms apart: in Retrig mode
    // both start on the same LFO phase (same starting pitch); in Free mode the second
    // note joins the running LFO, so it starts on a different pitch.
    auto startPitches = [] (int mode)
    {
        auto v = still();
        v[(size_t) lfoParam (0, LfoParam::Mode)] = (float) mode;
        v[(size_t) envParam (0, EnvParam::R)] = 1.0f;
        setSlot (v, 0, ModSource::Lfo1, oscParam (0, OscParam::Pitch), 2.0f);
        Rig r (v);
        double result[2] = {};
        for (int k = 0; k < 2; ++k)
        {
            const size_t onset = r.now();
            r.seconds (0.1, { noteOn (60) });
            r.seconds (0.25, { noteOff (60) });
            double sum = 0.0;
            int count = 0;
            for (const auto& c : cyclePitches (r.mono, onset + 200, onset + (size_t) (0.03 * kRate)))
            {
                sum += c.midi;
                ++count;
            }
            result[k] = count > 0 ? sum / count : 0.0;
        }
        return std::make_pair (result[0], result[1]);
    };
    const auto retrig = startPitches ((int) LfoMode::Retrig);
    const auto freeRun = startPitches ((int) LfoMode::Free);
    std::printf ("  note starts: retrig %.3f / %.3f, free %.3f / %.3f\n", retrig.first, retrig.second, freeRun.first, freeRun.second);
    CHECK_NEAR (retrig.first, retrig.second, 0.02);
    CHECK (std::fabs (freeRun.first - freeRun.second) > 0.5);
}

AP_TEST (engine_poly_aftertouch_overrides_channel)
{
    // Aftertouch -> Osc A Pitch (+0.96 st at full pressure).
    auto v = still();
    setSlot (v, 0, ModSource::Aftertouch, oscParam (0, OscParam::Pitch), 1.0f);
    Rig r (v);
    r.seconds (0.3, { noteOn (60) });
    const double basePitch = ap::hzToMidi (yinHz (r.mono, r.now() - 4200));
    r.seconds (0.3, { ev (MidiEvent::Type::Aftertouch, 0, 1.0f) });
    const double channel = ap::hzToMidi (yinHz (r.mono, r.now() - 4200));
    r.seconds (0.3, { ev (MidiEvent::Type::PolyAftertouch, 60, 0.0f) });
    const double poly = ap::hzToMidi (yinHz (r.mono, r.now() - 4200));
    std::printf ("  aftertouch: base %.3f, channel AT %.3f, poly AT 0 %.3f\n", basePitch, channel, poly);
    CHECK_NEAR (basePitch, 60.0, 0.03);
    CHECK_NEAR (channel, 60.96, 0.05);
    CHECK_NEAR (poly, 60.0, 0.03);
}

AP_TEST (engine_env2_sweeps_filter_cutoff)
{
    // White noise into Filter 1 (LP 24 at 200 Hz); Env 2 (decay 80 ms) -> cutoff opens
    // the filter during the first 100 ms only.
    auto v = still();
    v[(size_t) oscParam (0, OscParam::On)] = 0.0f;
    v[(size_t) P::NoiseLevel] = 100.0f;
    v[(size_t) filterParam (0, FilterParam::Cutoff)] = 200.0f;
    v[(size_t) envParam (1, EnvParam::A)] = 1.0f;
    v[(size_t) envParam (1, EnvParam::D)] = 80.0f;
    setSlot (v, 0, ModSource::Env2, filterParam (0, FilterParam::Cutoff), 100.0f);
    Rig r (v);
    r.seconds (0.3, { noteOn (60) });
    CHECK (r.finite);
    const size_t w = (size_t) (0.1 * kRate);
    const double first = diffRms (r.mono, 0, w), second = diffRms (r.mono, w, 2 * w);
    std::printf ("  Env 2 -> cutoff: HF level first 100 ms %.5f, next 100 ms %.5f\n", first, second);
    CHECK (first > second * 4.0);

    // The UI ring: modOffset of the last voice shows the (decayed) env amount.
    CHECK (r.engine->visuals().modOffset[modDestinationIndexOf (filterParam (0, FilterParam::Cutoff))].load() < 0.1f);
}

AP_TEST (engine_determinism_and_seed)
{
    auto v = defaultValues();
    v[(size_t) P::Alive] = 50.0f;
    v[(size_t) oscParam (0, OscParam::RandPhase)] = 100.0f;
    v[(size_t) oscParam (0, OscParam::UniVoices)] = 3.0f;
    v[(size_t) P::NoiseLevel] = 10.0f;

    auto render = [&] (uint32_t seed)
    {
        Rig r (v, seed);
        r.blocks (20, { noteOn (60), noteOn (64, 0.5f, 33), noteOn (67, 0.9f, 400) });
        r.blocks (20, { noteOff (64, 7), noteOff (60, 100) });
        return r.mono;
    };
    const auto a = render (42), b = render (42), c = render (43);
    CHECK_EQ (a.size(), b.size());
    CHECK (a.size() == b.size() && std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0);
    double diff = 0.0;
    for (size_t i = 0; i < std::min (a.size(), c.size()); ++i) diff = std::max (diff, (double) std::fabs (a[i] - c[i]));
    CHECK (diff > 1.0e-3);

    // reset() returns to a deterministic start too.
    Rig r (v, 42);
    r.blocks (5, { noteOn (50) });
    r.engine->reset();
    r.mono.clear();
    r.blocks (20, { noteOn (60), noteOn (64, 0.5f, 33), noteOn (67, 0.9f, 400) });
    r.blocks (20, { noteOff (64, 7), noteOff (60, 100) });
    CHECK (r.mono.size() == a.size() && std::memcmp (r.mono.data(), a.data(), a.size() * sizeof (float)) == 0);
}

AP_TEST (engine_events_at_exact_offsets)
{
    // Filter 1 off so the check is about the engine's event timing, not a filter's latency.
    auto dry = still();
    dry[(size_t) filterParam (0, FilterParam::On)] = 0.0f;
    Rig r (dry);
    r.block ({ noteOn (60, 0.8f, 100) });
    size_t firstNonZero = r.now();
    for (size_t i = 0; i < r.now(); ++i)
        if (r.left[i] != 0.0f || r.right[i] != 0.0f) { firstNonZero = i; break; }
    CHECK (firstNonZero >= 100);
    CHECK (firstNonZero <= 101);

    // A note-off at an offset releases there: an instant-release patch is silent right after it.
    auto v = still();
    v[(size_t) envParam (0, EnvParam::R)] = 1.0f;
    Rig q (v);
    q.blocks (4, { noteOn (60) });
    q.block ({ noteOff (60, 200) });
    CHECK (rms (q.mono, q.now() - kBlock, q.now() - kBlock + 190) > 0.01);
    CHECK (rms (q.mono, q.now() - kBlock + 260, q.now()) < 1.0e-6);
}

AP_TEST (engine_sustain_pedal)
{
    Rig r (still());
    r.seconds (0.2, { ev (MidiEvent::Type::Sustain, 0, 1.0f), noteOn (60) });
    r.seconds (1.0, { noteOff (60) });
    CHECK_EQ (r.engine->visuals().activeVoices.load(), 1);           // held by the pedal
    CHECK (rms (r.mono, r.now() - 2048, r.now()) > 0.01);
    r.seconds (1.5, { ev (MidiEvent::Type::Sustain, 0, 0.0f) });
    CHECK_EQ (r.engine->visuals().activeVoices.load(), 0);
    CHECK (rms (r.mono, r.now() - 2048, r.now()) < 1.0e-4);
}

AP_TEST (engine_routing_direct_bypasses_filters)
{
    // Filter 1 nearly closed (20 Hz): Osc A routed to Filter 1 is (almost) silent,
    // routed Direct it is untouched; Filter 1 off = bypass.
    auto v = still();
    v[(size_t) filterParam (0, FilterParam::Cutoff)] = 20.0f;
    v[(size_t) filterParam (0, FilterParam::Res)] = 0.0f;

    auto level = [&] (const ParamValues& p)
    {
        Rig r (p);
        r.seconds (0.3, { noteOn (72) });
        return rms (r.mono, (size_t) (0.1 * kRate), r.now());
    };
    const double filtered = level (v);
    auto direct = v;
    direct[(size_t) oscParam (0, OscParam::Route)] = 3.0f;   // Direct
    const double bypassed = level (direct);
    auto off = v;
    off[(size_t) filterParam (0, FilterParam::On)] = 0.0f;
    const double filterOff = level (off);
    auto viaF2 = v;
    viaF2[(size_t) oscParam (0, OscParam::Route)] = 1.0f;     // Filter 2 (off by default) -> bypass
    const double secondFilter = level (viaF2);
    std::printf ("  routing: F1 %.5f, direct %.5f, F1 off %.5f, via F2 %.5f\n", filtered, bypassed, filterOff, secondFilter);
    CHECK (bypassed > 0.02);
    CHECK (filtered < bypassed * 0.05);
    CHECK_NEAR (filterOff, bypassed, bypassed * 0.02);
    CHECK_NEAR (secondFilter, bypassed, bypassed * 0.02);

    // Parallel: "Both" feeds the source to both filters and sums them.
    auto par = v;
    par[(size_t) P::FilterRouting] = 1.0f;
    par[(size_t) oscParam (0, OscParam::Route)] = 2.0f;        // Both
    par[(size_t) filterParam (0, FilterParam::On)] = 0.0f;     // F1 bypass + F2 bypass = 2 x dry
    CHECK_NEAR (level (par), 2.0 * bypassed, bypassed * 0.05);
}

AP_TEST (engine_sub_and_noise)
{
    // Sub alone (osc A off), routed direct: one octave below C4.
    auto v = still();
    v[(size_t) oscParam (0, OscParam::On)] = 0.0f;
    v[(size_t) P::SubLevel] = 100.0f;
    v[(size_t) P::SubOct] = 1.0f;   // -1
    Rig r (v);
    r.seconds (0.5, { noteOn (60) });
    CHECK (r.finite);
    CHECK_NEAR (yinHz (r.mono, 8000), 261.63 * 0.5, 261.63 * 0.5 * 0.005);
    CHECK (rms (r.mono, 4800, r.now()) > 0.01);

    // Sub level 0 and noise 0 with osc A off: silence.
    v[(size_t) P::SubLevel] = 0.0f;
    Rig s (v);
    s.seconds (0.3, { noteOn (60) });
    CHECK (rms (s.mono, 0, s.now()) == 0.0);

    // Noise colours: similar level, bounded, spectral tilt in the right order.
    double tilt[(int) NoiseColour::Count] = {};
    for (int c = 0; c < (int) NoiseColour::Count; ++c)
    {
        NoiseGenerator n;
        n.prepare (kRate);
        n.setColour ((NoiseColour) c);
        n.reset (1234u);
        std::vector<float> buf ((size_t) kRate);
        for (size_t i = 0; i < buf.size(); i += kControlInterval)
            n.render (buf.data() + i, kControlInterval);
        float peak = 0.0f;
        for (float x : buf) peak = std::max (peak, std::fabs (x));
        const double level = rms (buf, 0, buf.size());
        tilt[c] = diffRms (buf, 0, buf.size()) / level;
        CHECK (level > 0.3 && level < 0.5);
        CHECK (peak < 2.5f);

        // Seeded: the same seed repeats, another seed differs.
        NoiseGenerator m;
        m.prepare (kRate);
        m.setColour ((NoiseColour) c);
        m.reset (1234u);
        std::vector<float> again ((size_t) 256);
        m.render (again.data(), 256);
        CHECK (std::memcmp (again.data(), buf.data(), 256 * sizeof (float)) == 0);
    }
    std::printf ("  noise HF tilt: white %.3f pink %.3f brown %.3f blue %.3f violet %.3f\n",
                 tilt[0], tilt[1], tilt[2], tilt[3], tilt[4]);
    CHECK (tilt[(int) NoiseColour::Brown] < tilt[(int) NoiseColour::Pink]);
    CHECK (tilt[(int) NoiseColour::Pink] < tilt[(int) NoiseColour::White]);
    CHECK (tilt[(int) NoiseColour::White] < tilt[(int) NoiseColour::Violet]);
    CHECK (tilt[(int) NoiseColour::Pink] < tilt[(int) NoiseColour::Blue]);
}

AP_TEST (engine_control_changes_are_ramped)
{
    // Macro 1 -> Sub Level jumps 0 -> 100 % between blocks: the sub fades in over a
    // control block instead of stepping (no step > 10 % of the tone's peak).
    auto v = still();
    v[(size_t) oscParam (0, OscParam::On)] = 0.0f;
    v[(size_t) P::SubRoute] = 3.0f;   // Direct
    setSlot (v, 0, ModSource::Macro1, P::SubLevel, 100.0f);
    Rig r (v);
    r.seconds (0.2, { noteOn (60) });
    CHECK (rms (r.mono, 0, r.now()) == 0.0);
    const size_t jump = r.now();
    v[(size_t) macroParam (0)] = 100.0f;
    r.engine->setParams (v);
    r.seconds (0.2);
    float peak = 0.0f, step = 0.0f;
    for (size_t i = jump + 1; i < r.now(); ++i)
    {
        peak = std::max (peak, std::fabs (r.mono[i]));
        step = std::max (step, std::fabs (r.mono[i] - r.mono[i - 1]));
    }
    std::printf ("  sub level jump: peak %.4f, largest step %.4f\n", peak, step);
    CHECK (peak > 0.05f);
    CHECK (step < 0.1f * peak);
}

AP_TEST (engine_user_table_and_fallback)
{
    // a_table = User with no table posted -> factory table 0 (plays in tune).
    auto v = still();
    v[(size_t) oscParam (0, OscParam::Table)] = 16.0f;
    Rig r (v);
    r.seconds (0.4, { noteOn (60) });
    CHECK_NEAR (yinHz (r.mono, 8000), 261.63, 261.63 * 0.005);
    const double sineThird = toneLevel (r.mono, 3.0 * 261.63, 4800, r.now()) / toneLevel (r.mono, 261.63, 4800, r.now());

    // A one-frame square user table arrives through the HandOff: odd harmonics appear.
    std::vector<float> square ((size_t) kFrameSize);
    for (int i = 0; i < kFrameSize; ++i) square[(size_t) i] = i < kFrameSize / 2 ? 0.8f : -0.8f;
    Rig u (v);
    u.engine->setUserTable (0, Wavetable::fromFrames (square.data(), 1, "Square"));
    u.seconds (0.4, { noteOn (60) });
    const double squareThird = toneLevel (u.mono, 3.0 * 261.63, 4800, u.now()) / toneLevel (u.mono, 261.63, 4800, u.now());
    std::printf ("  3rd harmonic ratio: fallback %.4f, user square %.4f\n", sineThird, squareThird);
    CHECK (sineThird < 0.05);
    CHECK (squareThird > 0.2);

    // Replacing the table retires the old one; the message thread frees it.
    u.engine->setUserTable (0, Wavetable::fromFrames (square.data(), 1, "Square 2"));
    u.blocks (2);
    u.engine->releaseRetired();
    CHECK (u.finite);
}

AP_TEST (engine_long_blocks_and_tail)
{
    // A block longer than the prepared size is split internally (events keep their offsets).
    auto engine = std::make_unique<Engine>();
    engine->prepare (kRate, 256);
    auto dry = still();
    dry[(size_t) filterParam (0, FilterParam::On)] = 0.0f;
    engine->setParams (dry);
    std::vector<float> L (3000), R (3000);
    const MidiEvent events[] = { noteOn (60, 0.8f, 1000) };
    engine->process (L.data(), R.data(), 3000, events, 1);
    size_t first = L.size();
    for (size_t i = 0; i < L.size(); ++i) if (L[i] != 0.0f) { first = i; break; }
    CHECK (first >= 1000 && first <= 1001);
    CHECK (rms (L, 1200, 3000) > 0.01);

    // tailSeconds = max release / 1000 + FX tail + 1.
    auto v = still();
    v[(size_t) envParam (0, EnvParam::R)] = 2000.0f;
    engine->setParams (v);
    CHECK (engine->tailSeconds() >= 3.0f - 1.0e-3f);
    CHECK_EQ (engine->latencySamples(), 0);
}

AP_TEST (engine_render_preview_returns_requested_length)
{
    const auto audio = renderPreview (initPatch(), 60, 0.5f, 1.5f, kRate);
    CHECK_EQ ((int) audio.size(), (int) std::lround (1.5 * kRate));
    CHECK (rms (std::vector<float> (audio.begin(), audio.begin() + 24000), 0, 24000) > 0.01);
}

AP_TEST (engine_cpu_budget)
{
    // 8-voice chord x unison 4 on both oscillators, both filters on, all 4 LFOs routed,
    // 2 s at 48 kHz: wall time < 25 % of real time (asserted in Release builds only).
    auto v = defaultValues();
    v[(size_t) P::Voices] = 8.0f;
    v[(size_t) oscParam (1, OscParam::On)] = 1.0f;
    for (int o = 0; o < 2; ++o)
    {
        v[(size_t) oscParam (o, OscParam::UniVoices)] = 4.0f;
        v[(size_t) oscParam (o, OscParam::Pos)] = 40.0f;
    }
    v[(size_t) filterParam (1, FilterParam::On)] = 1.0f;
    v[(size_t) filterParam (1, FilterParam::Type)] = 2.0f;   // HP 12
    v[(size_t) filterParam (1, FilterParam::Cutoff)] = 200.0f;
    setSlot (v, 0, ModSource::Lfo1, oscParam (0, OscParam::Pos), 30.0f);
    setSlot (v, 1, ModSource::Lfo2, oscParam (1, OscParam::Pos), 30.0f);
    setSlot (v, 2, ModSource::Lfo3, filterParam (0, FilterParam::Cutoff), -30.0f);
    setSlot (v, 3, ModSource::Lfo4, filterParam (1, FilterParam::Cutoff), 20.0f);
    for (int l = 0; l < kNumLfos; ++l)
        v[(size_t) lfoParam (l, LfoParam::Rate)] = 0.5f + (float) l;

    auto engine = std::make_unique<Engine>();
    engine->prepare (kRate, kBlock);
    engine->setParams (v);
    std::vector<float> L ((size_t) kBlock), R ((size_t) kBlock);
    std::vector<MidiEvent> chord;
    for (int n : { 48, 52, 55, 59, 60, 64, 67, 71 })
        chord.push_back (noteOn (n, 0.8f));

    const int blocks = blocksFor (2.0);
    bool finite = true;
    const auto t0 = std::chrono::steady_clock::now();
    for (int b = 0; b < blocks; ++b)
    {
        engine->process (L.data(), R.data(), kBlock, b == 0 ? chord.data() : nullptr, b == 0 ? (int) chord.size() : 0);
        finite = finite && std::isfinite (L[0]) && std::isfinite (R[(size_t) kBlock - 1]);
    }
    const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    const double ratio = seconds / ((double) blocks * kBlock / kRate);
    std::printf ("  CPU budget: %.2f %% of real time (8 voices x 2 osc x unison 4, 2 filters, 4 LFOs)\n", ratio * 100.0);
    CHECK (finite);
    CHECK_EQ (engine->visuals().activeVoices.load(), 8);
#ifdef NDEBUG
    CHECK (ratio < 0.25);
#endif
}
