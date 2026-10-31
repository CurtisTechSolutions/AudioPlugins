// Engine smoke test (foundation): the default patch plays C4 in tune, decays
// after note-off, respects the voice limit, routes the mod matrix, and the
// offline preview helper renders the requested length.

#include "TestHarness.h"

#include "engine/Engine.h"
#include "engine/FactoryPresets.h"
#include "engine/Patch.h"
#include "engine/WavetableFactory.h"

#include <ap/Math.h>
#include <ap/PitchDetector.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using namespace verdant;

namespace
{
    constexpr double kRate = 48000.0;
    constexpr int kBlock = 512;

    struct Render
    {
        std::unique_ptr<Engine> engine = std::make_unique<Engine>();
        std::vector<float> L = std::vector<float> ((size_t) kBlock), R = std::vector<float> ((size_t) kBlock);

        Render (const ParamValues& values = defaultValues())
        {
            engine->prepare (kRate, kBlock);
            engine->setParams (values);
        }

        /** Renders `blocks` blocks; `events` go into the first block. Appends mono output to `out`. */
        void run (int blocks, std::vector<MidiEvent> events, std::vector<float>& out, bool& finite)
        {
            for (int b = 0; b < blocks; ++b)
            {
                engine->process (L.data(), R.data(), kBlock, b == 0 ? events.data() : nullptr, b == 0 ? (int) events.size() : 0);
                for (int i = 0; i < kBlock; ++i)
                {
                    if (! std::isfinite (L[(size_t) i]) || ! std::isfinite (R[(size_t) i])) finite = false;
                    out.push_back (0.5f * (L[(size_t) i] + R[(size_t) i]));
                }
            }
        }
    };

    MidiEvent noteOn (int note, float velocity, int offset = 0)  { return MidiEvent { MidiEvent::Type::NoteOn, offset, 1, note, velocity, 0 }; }
    MidiEvent noteOff (int note, int offset = 0)                 { return MidiEvent { MidiEvent::Type::NoteOff, offset, 1, note, 0.0f, 0 }; }

    double rms (const std::vector<float>& v, size_t from, size_t to)
    {
        to = std::min (to, v.size());
        if (to <= from) return 0.0;
        double acc = 0.0;
        for (size_t i = from; i < to; ++i) acc += (double) v[i] * v[i];
        return std::sqrt (acc / (double) (to - from));
    }

    float yinHz (const std::vector<float>& v, size_t from)
    {
        ap::YinPitchDetector yin;
        yin.prepare (kRate, 2048, 50.0f, 1500.0f);
        if (from + (size_t) yin.requiredSamples() > v.size()) return 0.0f;
        const auto r = yin.analyze (v.data() + from);
        return r.voiced ? r.frequency : 0.0f;
    }
}

AP_TEST (engine_smoke_default_patch_c4)
{
    Render r;
    std::vector<float> out;
    bool finite = true;
    const int oneSecond = (int) (kRate / kBlock) + 1;
    r.run (oneSecond, { noteOn (60, 100.0f / 127.0f) }, out, finite);

    CHECK (finite);
    const double level = rms (out, 2000, out.size());
    CHECK (level > 0.01);
    CHECK_NEAR (yinHz (out, 4096), 261.63, 2.0);
    CHECK_NEAR (yinHz (out, 24000), 261.63, 2.0);
    CHECK (r.engine->visuals().activeVoices.load() == 1);
    CHECK (r.engine->visuals().outputPeak.load() > 0.01f);

    std::vector<float> tail;
    r.run ((int) (3.0 * kRate / kBlock), { noteOff (60) }, tail, finite);
    CHECK (finite);
    CHECK (rms (tail, tail.size() - (size_t) kBlock * 4, tail.size()) < 1.0e-4);
    CHECK (r.engine->visuals().activeVoices.load() == 0);

    // The scope ring holds the most recent output.
    std::vector<float> scope ((size_t) VisualState::kScopeSize);
    CHECK_EQ (r.engine->visuals().readScope (scope.data(), (int) scope.size()), VisualState::kScopeSize);
}

AP_TEST (engine_voice_limit_and_stealing)
{
    auto values = defaultValues();
    values[(size_t) P::Voices] = 2.0f;
    Render r (values);
    std::vector<float> out;
    bool finite = true;
    r.run (4, { noteOn (60, 0.8f), noteOn (64, 0.8f), noteOn (67, 0.8f) }, out, finite);
    CHECK (finite);
    CHECK_EQ (r.engine->visuals().activeVoices.load(), 2);

    // Voices = 1: the newest note steals the only voice.
    values[(size_t) P::Voices] = 1.0f;
    Render mono (values);
    std::vector<float> m;
    mono.run (4, { noteOn (60, 0.8f) }, m, finite);
    m.clear();
    mono.run (40, { noteOn (67, 0.8f) }, m, finite);
    CHECK_EQ (mono.engine->visuals().activeVoices.load(), 1);
    CHECK_NEAR (yinHz (m, 8192), ap::midiToHz (67.0f), 3.0);
}

AP_TEST (engine_mod_matrix_routes_macro_to_level)
{
    auto values = defaultValues();
    values[(size_t) modSrc (0)] = (float) ModSource::Macro1;
    values[(size_t) modDst (0)] = (float) modDestinationIndexOf (oscParam (0, OscParam::Level));
    values[(size_t) modAmt (0)] = -100.0f;
    values[(size_t) macroParam (0)] = 100.0f;   // Level 80 % - 100 % -> 0

    Render r (values);
    std::vector<float> out;
    bool finite = true;
    r.run (20, { noteOn (60, 0.8f) }, out, finite);
    CHECK (finite);
    CHECK (rms (out, 2000, out.size()) < 1.0e-4);
    CHECK_NEAR (r.engine->visuals().modOffset[modDestinationIndexOf (oscParam (0, OscParam::Level))].load(), -1.0, 1.0e-6);
}

AP_TEST (engine_render_preview_length)
{
    const auto audio = renderPreview (initPatch(), 60, 0.5f, 1.25f, 44100.0);
    CHECK_EQ ((int) audio.size(), (int) std::lround (1.25 * 44100.0));
    bool finite = true;
    for (float s : audio) finite = finite && std::isfinite (s);
    CHECK (finite);
    CHECK (rms (audio, 0, audio.size()) > 0.01);
    CHECK (rms (audio, audio.size() - 2048, audio.size()) < 1.0e-4);   // released at 0.5 s, 300 ms release

    CHECK (renderPreview (initPatch(), 60, 0.1f, 0.0f, 44100.0).empty());
}

AP_TEST (engine_factory_presets_render)
{
    CHECK (factoryPresets().size() >= 2);
    for (const auto& p : factoryPresets())
    {
        const auto audio = renderPreview (p, 60, 1.0f, 1.5f, 44100.0);
        float peak = 0.0f;
        for (float s : audio) peak = std::max (peak, std::fabs (s));
        CHECK (peak > 1.0e-3f);
        CHECK (peak < 1.0f);
    }
}

AP_TEST (engine_basic_shapes_frame0_is_sine)
{
    const auto t = makeFactoryTable (0);
    CHECK (t != nullptr);
    CHECK (t->numFrames() >= 16);
    double dot = 0.0, ee = 0.0, ss = 0.0;
    for (int i = 0; i < kFrameSize; ++i)
    {
        const double s = std::sin (ap::kTwoPiD * i / kFrameSize);
        const double v = t->frame (0)[i];
        dot += v * s; ee += v * v; ss += s * s;
    }
    CHECK (dot / std::sqrt (ee * ss) > 0.9999);
    CHECK (std::string (factoryTableNames()[0]) == "Basic Shapes");
    CHECK (std::string (factoryTableNames()[15]) == "Sub Bass");
}
