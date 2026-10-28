// End-to-end check of a built VST3: load it through JUCE's hosting code, run
// audio/MIDI through processBlock, inspect the output.
//
//   VST3HostTest <path/to/Plugin.vst3> <Strata|VoxNote|Intone|Ducker|Frisson|Humline>

#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

#include <ap/Math.h>
#include <ap/PitchDetector.h>
#include <ap/Scales.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

namespace
{

int failures = 0;

void check (bool ok, const char* what)
{
    std::printf ("  [%s] %s\n", ok ? " ok " : "FAIL", what);
    if (! ok) ++failures;
}

void checkNear (double a, double b, double tol, const char* what)
{
    const bool ok = std::fabs (a - b) <= tol;
    std::printf ("  [%s] %s (got %.4f, want %.4f +/- %.4f)\n", ok ? " ok " : "FAIL", what, a, b, tol);
    if (! ok) ++failures;
}

constexpr double kSampleRate = 48000.0;
constexpr int    kBlock = 512;

struct TestPlayHead : public juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm);
        p.setPpqPosition (ppq);
        p.setTimeInSamples (samples);
        p.setIsPlaying (playing);
        return p;
    }
    double bpm = 120.0, ppq = 0.0;
    juce::int64 samples = 0;
    bool playing = true;
};

struct Host
{
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midi;
    TestPlayHead playHead;
    std::vector<juce::MidiMessage> midiOut;
    int numChannels = 2;

    bool load (const juce::String& path)
    {
        juce::AudioPluginFormatManager fm;
        juce::addHeadlessDefaultFormatsToManager (fm);

        juce::OwnedArray<juce::PluginDescription> descs;
        for (auto* format : fm.getFormats())
            if (format->fileMightContainThisPluginType (path))
                format->findAllTypesForFile (descs, path);

        if (descs.isEmpty())
        {
            std::printf ("No plugin found at %s\n", path.toRawUTF8());
            return false;
        }

        juce::String err;
        plugin = fm.createPluginInstance (*descs[0], kSampleRate, kBlock, err);
        if (plugin == nullptr)
        {
            std::printf ("Failed to instantiate: %s\n", err.toRawUTF8());
            return false;
        }
        std::printf ("Loaded '%s' (%s) in=%d out=%d\n", plugin->getName().toRawUTF8(),
                     descs[0]->pluginFormatName.toRawUTF8(),
                     plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels());
        return true;
    }

    void prepare()
    {
        plugin->setPlayHead (&playHead);
        plugin->setNonRealtime (true);
        plugin->prepareToPlay (kSampleRate, kBlock);
        numChannels = juce::jmax (plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels(), 1);
        buffer.setSize (numChannels, kBlock);
        buffer.clear();
    }

    juce::AudioProcessorParameter* param (const juce::String& name)
    {
        for (auto* p : plugin->getParameters())
            if (p->getName (64) == name) return p;
        std::printf ("  parameter '%s' not found\n", name.toRawUTF8());
        ++failures;
        return nullptr;
    }

    void setParam (const juce::String& name, const juce::String& text)
    {
        if (auto* p = param (name))
            p->setValueNotifyingHost (p->getValueForText (text));
    }

    void setParamNormalised (const juce::String& name, float v)
    {
        if (auto* p = param (name))
            p->setValueNotifyingHost (v);
    }

    /** Runs one block. `fill(channel, sampleIndex, absoluteSample)` provides input. */
    template <typename Fill>
    void processBlock (Fill&& fill, std::vector<float>* capture = nullptr, int captureChannel = 0)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* d = buffer.getWritePointer (ch);
            for (int i = 0; i < kBlock; ++i) d[i] = fill (ch, i, playHead.samples + i);
        }
        plugin->processBlock (buffer, midi);
        for (const auto meta : midi) midiOut.push_back (meta.getMessage());
        midi.clear();
        if (capture != nullptr)
        {
            const float* d = buffer.getReadPointer (juce::jmin (captureChannel, numChannels - 1));
            capture->insert (capture->end(), d, d + kBlock);
        }
        playHead.samples += kBlock;
        playHead.ppq += kBlock * playHead.bpm / (60.0 * kSampleRate);
    }

    bool outputIsFinite() const
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                if (! std::isfinite (buffer.getReadPointer (ch)[i])) return false;
        return true;
    }
};

double bandRmsOf (const std::vector<float>& v, double lowHz, double highHz, size_t from);

double rmsOf (const std::vector<float>& v, size_t from, size_t to)
{
    to = std::min (to, v.size());
    if (to <= from) return 0.0;
    double acc = 0.0;
    for (size_t i = from; i < to; ++i) acc += (double) v[i] * v[i];
    return std::sqrt (acc / (double) (to - from));
}

float measureHz (const std::vector<float>& v, size_t from)
{
    ap::YinPitchDetector yin;
    yin.prepare (kSampleRate, 2048, 50.0f, 1500.0f);
    if (from + (size_t) yin.requiredSamples() > v.size()) return 0.0f;
    const auto r = yin.analyze (v.data() + from);
    return r.voiced ? r.frequency : 0.0f;
}

float tone (double hz, juce::int64 n, double amp, int harmonics = 1)
{
    double s = 0.0;
    for (int h = 1; h <= harmonics; ++h)
        s += std::sin (ap::kTwoPiD * hz * h * (double) n / kSampleRate) / h;
    return (float) (amp * s / (harmonics > 1 ? 1.5 : 1.0));
}

//==============================================================================
void commonChecks (Host& host)
{
    std::printf ("Common checks\n");
    check (host.plugin->getLatencySamples() >= 0, "latency is non-negative");

    for (int b = 0; b < 8; ++b)
        host.processBlock ([] (int, int, juce::int64) { return 0.0f; });
    check (host.outputIsFinite(), "silence in -> finite output");

    // State round trip.
    juce::MemoryBlock state;
    host.plugin->getStateInformation (state);
    check (state.getSize() > 0, "state information is produced");
    host.plugin->setStateInformation (state.getData(), (int) state.getSize());
    for (int b = 0; b < 2; ++b)
        host.processBlock ([] (int, int, juce::int64) { return 0.0f; });
    check (host.outputIsFinite(), "still processing after state restore");

}

void testStrata (Host& host)
{
    std::printf ("Strata checks\n");
    std::vector<float> out;

    host.midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    for (int b = 0; b < 30; ++b)
        host.processBlock ([] (int, int, juce::int64) { return 0.0f; }, &out);

    check (host.outputIsFinite(), "synth output is finite");
    const double level = rmsOf (out, 2000, out.size());
    check (level > 0.01, "note-on produces sound");
    checkNear (measureHz (out, 4096), 261.63, 4.0, "C4 is in tune");

    host.midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
    std::vector<float> tail;
    for (int b = 0; b < 300; ++b)
        host.processBlock ([] (int, int, juce::int64) { return 0.0f; }, &tail);
    const double tailLevel = rmsOf (tail, tail.size() - kBlock * 4, tail.size());
    check (tailLevel < 1.0e-4, "release decays to silence");

    // Polyphony: a chord is louder than a single note.
    std::vector<float> chord;
    for (int n : { 60, 64, 67 }) host.midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
    for (int b = 0; b < 30; ++b)
        host.processBlock ([] (int, int, juce::int64) { return 0.0f; }, &chord);
    check (rmsOf (chord, 2000, chord.size()) > level * 1.2, "three notes are louder than one");
    for (int n : { 60, 64, 67 }) host.midi.addEvent (juce::MidiMessage::noteOff (1, n), 0);
    for (int b = 0; b < 100; ++b) host.processBlock ([] (int, int, juce::int64) { return 0.0f; });

    // Polyphony limit: Voices = 1 must steal, so a second note silences the first.
    host.setParam ("Voices", "1");
    host.setParam ("Release", "20 ms");
    host.midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    for (int b = 0; b < 10; ++b) host.processBlock ([] (int, int, juce::int64) { return 0.0f; });
    host.midi.addEvent (juce::MidiMessage::noteOn (1, 67, (juce::uint8) 100), 0);
    std::vector<float> mono;
    for (int b = 0; b < 30; ++b) host.processBlock ([] (int, int, juce::int64) { return 0.0f; }, &mono);
    const double c4 = bandRmsOf (mono, 255.0, 268.0, 4096);
    const double g4 = bandRmsOf (mono, 385.0, 400.0, 4096);
    check (g4 > c4 * 4.0, "Voices = 1 steals: only the newest note sounds");
    for (int n : { 60, 67 }) host.midi.addEvent (juce::MidiMessage::noteOff (1, n), 0);
    for (int b = 0; b < 50; ++b) host.processBlock ([] (int, int, juce::int64) { return 0.0f; });
    host.setParam ("Voices", "8");
}

void testVoxNote (Host& host)
{
    std::printf ("VoxNote checks\n");
    host.midiOut.clear();

    // One second of a "voice" (harmonic series) 31 cents above A3, then silence.
    for (int b = 0; b < (int) (kSampleRate / kBlock); ++b)
        host.processBlock ([] (int, int, juce::int64 n) { return tone (224.0, n, 0.3, 6); });
    const size_t afterTone = host.midiOut.size();
    for (int b = 0; b < 40; ++b)
        host.processBlock ([] (int, int, juce::int64) { return 0.0f; });

    int noteOns = 0, noteOffs = 0, bends = 0, lastNote = -1;
    for (size_t i = 0; i < host.midiOut.size(); ++i)
    {
        const auto& m = host.midiOut[i];
        if (m.isNoteOn()) { ++noteOns; lastNote = m.getNoteNumber(); }
        else if (m.isNoteOff()) ++noteOffs;
        else if (m.isPitchWheel()) ++bends;
    }
    check (afterTone > 0, "MIDI emitted while the tone plays");
    check (noteOns >= 1 && noteOns <= 3, "a steady tone yields one note (no chatter)");
    check (lastNote == 57, "224 Hz maps to A3 (MIDI 57)");
    check (noteOffs == noteOns, "every note-on gets a note-off after silence");
    check (bends >= 1, "a sharp note emits pitch bend");
    check (host.outputIsFinite(), "audio thru stays finite");

    // Audio-thru off -> output silent.
    host.setParam ("Audio Thru", "Off");
    std::vector<float> out;
    for (int b = 0; b < 10; ++b)
        host.processBlock ([] (int, int, juce::int64 n) { return tone (220.0, n, 0.3); }, &out);
    check (rmsOf (out, kBlock * 2, out.size()) < 1.0e-6, "Audio Thru off mutes the pass-through");
}

void testIntone (Host& host)
{
    std::printf ("Intone checks\n");
    const int latency = host.plugin->getLatencySamples();
    check (latency > 0 && latency < (int) kSampleRate / 5, "reports a sensible latency");

    host.setParam ("Retune Speed", "0 ms");
    host.setParam ("Amount", "100 %");
    host.setParam ("Mix", "100 %");
    host.setParam ("Scale", "Chromatic");

    // 224 Hz is ~31 cents sharp of A3 (220) -> should be pulled to 220.
    std::vector<float> out;
    for (int b = 0; b < 150; ++b)
        host.processBlock ([] (int, int, juce::int64 n) { return tone (224.0, n, 0.3, 6); }, &out);
    check (host.outputIsFinite(), "corrected output is finite");
    checkNear (measureHz (out, 40000), 220.0, 2.5, "sharp A3 is corrected to 220 Hz");
    check (rmsOf (out, 40000, out.size()) > 0.05, "corrected signal keeps its level");

    // Amount 0 -> the pitch passes through unchanged.
    host.setParam ("Amount", "0 %");
    out.clear();
    for (int b = 0; b < 150; ++b)
        host.processBlock ([] (int, int, juce::int64 n) { return tone (224.0, n, 0.3, 6); }, &out);
    checkNear (measureHz (out, 40000), 224.0, 2.5, "Amount 0 leaves the pitch alone");

    // Scale snapping: C major pulls a quarter-tone-above-C# (277.18 * 2^(0.25/12)) to D (293.66) or C (261.63).
    host.setParam ("Amount", "100 %");
    host.setParam ("Key", "C");
    host.setParam ("Scale", "Major");
    out.clear();
    for (int b = 0; b < 150; ++b)
        host.processBlock ([] (int, int, juce::int64 n) { return tone (281.2, n, 0.3, 6); }, &out);
    const float snapped = measureHz (out, 40000);
    check (std::fabs (snapped - 293.66f) < 3.0f || std::fabs (snapped - 261.63f) < 3.0f, "C major snaps C#+25ct to a scale note");
}

void testDucker (Host& host)
{
    std::printf ("Ducker checks\n");
    host.setParam ("Mode", "External sidechain");
    host.setParam ("Threshold", "-40 dB");
    host.setParam ("Ratio", "10:1");
    host.setParam ("Mix", "100 %");

    const bool hasSidechain = host.plugin->getBusCount (true) > 1 && host.plugin->getTotalNumInputChannels() >= 3;
    check (hasSidechain, "sidechain bus is exposed and enabled");
    const int scOffset = host.plugin->getChannelCountOfBus (true, 0);

    auto main = [] (int ch, int, juce::int64 n) { return ch < 2 ? tone (1000.0, n, 0.5) : 0.0f; };
    auto mainPlusSc = [scOffset] (int ch, int, juce::int64 n)
    {
        return ch < scOffset ? tone (1000.0, n, 0.5) : tone (100.0, n, 0.9);
    };

    std::vector<float> quiet, ducked;
    for (int b = 0; b < 40; ++b) host.processBlock (main, &quiet);
    for (int b = 0; b < 40; ++b) host.processBlock (mainPlusSc, &ducked);
    check (host.outputIsFinite(), "ducker output is finite");

    const double unducked = rmsOf (quiet, quiet.size() - kBlock * 8, quiet.size());
    const double duckedLvl = rmsOf (ducked, ducked.size() - kBlock * 8, ducked.size());
    checkNear (unducked, 0.5 / std::sqrt (2.0), 0.03, "no sidechain -> unity gain");
    check (duckedLvl < unducked * 0.5, "loud sidechain ducks the main input by > 6 dB");

    // Ghost mode with the transport running: gain must dip on every beat.
    host.setParam ("Mode", "Ghost (tempo)");
    host.setParam ("Depth", "24 dB");
    host.setParam ("Division", "1/4");
    std::vector<float> ghost;
    for (int b = 0; b < 120; ++b) host.processBlock (main, &ghost);
    // Peak envelope over the last second, in 10 ms windows.
    double maxWin = 0.0, minWin = 1.0;
    const size_t start = ghost.size() - (size_t) kSampleRate;
    for (size_t w = start; w + 480 <= ghost.size(); w += 480)
    {
        const double r = rmsOf (ghost, w, w + 480);
        maxWin = std::max (maxWin, r);
        minWin = std::min (minWin, r);
    }
    check (maxWin > 0.3 && minWin < maxWin * 0.2, "ghost mode pumps the level in time");

    // Lookahead changes the reported latency.
    host.setParam ("Lookahead", "5 ms");
    for (int b = 0; b < 2; ++b) host.processBlock (main);
    checkNear (host.plugin->getLatencySamples(), 240, 2, "5 ms lookahead reports 240 samples of latency");
}

void testHumline (Host& host)
{
    std::printf ("Humline checks\n");
    check (host.plugin->getTotalNumInputChannels() >= 1, "instrument exposes an audio input bus");
    const int latency = host.plugin->getLatencySamples();
    check (latency >= (int) (kSampleRate * 0.010) && latency <= (int) (kSampleRate * 0.300), "latency between 10 and 300 ms");

    host.setParam ("Key", "C");
    host.setParam ("Scale", "Major");
    host.setParam ("Snap", "100 %");
    host.setParam ("Pitch Bend", "Off");
    host.setParam ("Grid", "Off");
    host.setParam ("Timbre", "Soft Lead");
    host.setParam ("Glide", "0 ms");
    host.setParam ("Dry Voice", "0 %");
    host.setParam ("Synth Level", "-6 dB");
    host.setParam ("Send MIDI", "On");
    host.midiOut.clear();

    // One second of a "voice" 31 cents above A3, then silence: A3 played by the synth, MIDI out.
    std::vector<float> out;
    for (int b = 0; b < (int) (kSampleRate / kBlock); ++b)
        host.processBlock ([] (int, int, juce::int64 n) { return tone (224.0, n, 0.3, 6); }, &out);
    check (host.outputIsFinite(), "synth output is finite");
    check (rmsOf (out, 24000, out.size()) > 0.01, "humming makes the synth sound");
    checkNear (measureHz (out, 30000), 220.0, 3.0, "the synth plays A3 (220 Hz), in tune");

    std::vector<float> tail;
    for (int b = 0; b < 200; ++b)
        host.processBlock ([] (int, int, juce::int64) { return 0.0f; }, &tail);
    check (rmsOf (tail, tail.size() - kBlock * 4, tail.size()) < 1.0e-3, "silence releases the note and the synth decays");

    int noteOns = 0, noteOffs = 0, bends = 0, lastNote = -1;
    for (const auto& m : host.midiOut)
    {
        if (m.isNoteOn()) { ++noteOns; lastNote = m.getNoteNumber(); }
        else if (m.isNoteOff()) ++noteOffs;
        else if (m.isPitchWheel()) ++bends;
    }
    check (noteOns >= 1 && noteOns <= 3, "a steady tone yields one note (no chatter)");
    check (lastNote == 57, "224 Hz maps to A3 (MIDI 57)");
    check (noteOffs == noteOns, "every note-on gets a note-off after silence");
    check (bends == 0, "Pitch Bend off emits no bend");

    // Send MIDI off: nothing reaches the host.
    host.setParam ("Send MIDI", "Off");
    host.midiOut.clear();
    for (int b = 0; b < 60; ++b)
        host.processBlock ([] (int, int, juce::int64 n) { return tone (224.0, n, 0.3, 6); });
    for (int b = 0; b < 40; ++b)
        host.processBlock ([] (int, int, juce::int64) { return 0.0f; });
    check (host.midiOut.empty(), "Send MIDI off emits no MIDI");
    host.setParam ("Send MIDI", "On");

    // Dry voice only: the (delayed) input comes through at unity.
    host.setParam ("Dry Voice", "100 %");
    host.setParam ("Synth Level", "-60 dB");
    out.clear();
    for (int b = 0; b < 60; ++b)
        host.processBlock ([] (int, int, juce::int64 n) { return tone (220.0, n, 0.3); }, &out);
    const double dryLevel = rmsOf (out, out.size() - kBlock * 8, out.size());
    checkNear (20.0 * std::log10 (dryLevel / (0.3 / std::sqrt (2.0))), 0.0, 3.0, "Dry Voice passes the input within 3 dB");
    for (int b = 0; b < 40; ++b) host.processBlock ([] (int, int, juce::int64) { return 0.0f; });
    host.setParam ("Dry Voice", "0 %");
    host.setParam ("Synth Level", "-6 dB");

    // Snapping: D#4 (311 Hz) is not in C major pentatonic; the played note must be.
    host.setParam ("Scale", "Major Pentatonic");
    host.midiOut.clear();
    for (int b = 0; b < 60; ++b)
        host.processBlock ([] (int, int, juce::int64 n) { return tone (311.13, n, 0.3, 6); });
    for (int b = 0; b < 40; ++b)
        host.processBlock ([] (int, int, juce::int64) { return 0.0f; });
    int snapped = -1;
    for (const auto& m : host.midiOut) if (m.isNoteOn()) snapped = m.getNoteNumber();
    const bool inScale = snapped >= 0 && ap::Scale::fromType (ap::ScaleType::MajorPentatonic, 0).contains (snapped);
    check (inScale && (snapped == 62 || snapped == 64), "Snap 100 % moves D#4 onto the C major pentatonic (D4 or E4)");
}

double bandRmsOf (const std::vector<float>& v, double lowHz, double highHz, size_t from)
{
    // Goertzel bank, same approach as the module tests.
    double acc = 0.0;
    const int bins = 24;
    for (int b = 0; b < bins; ++b)
    {
        const double hz = lowHz * std::pow (highHz / lowHz, (b + 0.5) / bins);
        const double w = ap::kTwoPiD * hz / kSampleRate;
        const double c = 2.0 * std::cos (w);
        double s0 = 0.0, s1 = 0.0, s2 = 0.0;
        for (size_t i = from; i < v.size(); ++i) { s0 = v[i] + c * s1 - s2; s2 = s1; s1 = s0; }
        const double power = std::max (0.0, s1 * s1 + s2 * s2 - c * s1 * s2);
        const double amp = std::sqrt (power) / (double) (v.size() - from) * 2.0;
        acc += amp * amp * 0.5;
    }
    return std::sqrt (acc);
}

float pinkSample (juce::int64 n, int channel)
{
    // Deterministic pink-ish noise per channel: sum of white octaves (cheap Voss style).
    static juce::Random rngL (1), rngR (2);
    juce::Random& r = channel == 0 ? rngL : rngR;
    (void) n;
    float s = 0.0f;
    for (int o = 0; o < 6; ++o) s += r.nextFloat() * 2.0f - 1.0f;
    return s * 0.08f;
}

void testFrisson (Host& host)
{
    std::printf ("Frisson checks\n");
    const int latency = host.plugin->getLatencySamples();
    check (latency >= 0 && latency <= (int) (kSampleRate * 0.005), "latency within 5 ms");

    // Pink test material, stored so we can compare against the delayed input.
    std::vector<float> pinkL, pinkR;
    for (int i = 0; i < kBlock * 400; ++i) { pinkL.push_back (pinkSample (i, 0)); pinkR.push_back (pinkSample (i, 1)); }
    auto feed = [&] (int ch, int, juce::int64 n) { return ch == 0 ? pinkL[(size_t) (n % (juce::int64) pinkL.size())] : pinkR[(size_t) (n % (juce::int64) pinkR.size())]; };

    // Dose 0 -> bit-exact (latency only) bypass.
    host.setParam ("Dose", "0 %");
    host.setParam ("Match", "Off");
    host.playHead.samples = 0;
    std::vector<float> out;
    for (int b = 0; b < 120; ++b) host.processBlock (feed, &out);   // > 1 s: lets every module's fade-out finish
    float maxd = 0.0f;
    for (size_t i = (size_t) latency + 48000; i < out.size(); ++i)
        maxd = std::max (maxd, std::fabs (out[i] - pinkL[i - (size_t) latency]));
    check (maxd < 1.0e-5f, "Dose 0 is a latency-only bypass");

    // Defaults at full dose: finite, sensible level.
    host.setParam ("Dose", "100 %");
    for (const char* k : { "Weight", "Warmth", "Rush", "Focus", "Shine", "Space", "Groove", "Chills", "Life" })
        host.setParam (k, "40 %");
    out.clear();
    const auto t0 = juce::Time::getHighResolutionTicks();
    for (int b = 0; b < 400; ++b) host.processBlock (feed, &out);
    const double seconds = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
    std::printf ("  (full recipe: %.1f%% of real time for %d blocks of %d)\n", 100.0 * seconds / (400.0 * kBlock / kSampleRate), 400, kBlock);
    check (host.outputIsFinite(), "full recipe is finite");
    const double dryLevel = rmsOf (pinkL, 20000, pinkL.size());
    const double wetLevel = rmsOf (out, 20000, out.size());
    check (wetLevel > dryLevel * 0.5 && wetLevel < dryLevel * 2.0, "full recipe stays within 6 dB of the input level");

    // WEIGHT alone raises the low end.
    for (const char* k : { "Weight", "Warmth", "Rush", "Focus", "Shine", "Space", "Groove", "Chills", "Life" })
        host.setParam (k, "0 %");
    host.setParam ("Weight", "100 %");
    out.clear();
    for (int b = 0; b < 120; ++b) host.processBlock (feed, &out);
    const double lowIn = bandRmsOf (pinkL, 40.0, 90.0, 10000);
    const double lowOut = bandRmsOf (out, 40.0, 90.0, 10000);
    check (lowOut > lowIn * 1.15, "WEIGHT raises 40-90 Hz");

    // SHINE alone raises the top.
    host.setParam ("Weight", "0 %");
    host.setParam ("Shine", "100 %");
    out.clear();
    for (int b = 0; b < 120; ++b) host.processBlock (feed, &out);
    const double hiIn = bandRmsOf (pinkL, 10000.0, 16000.0, 10000);
    const double hiOut = bandRmsOf (out, 10000.0, 16000.0, 10000);
    check (hiOut > hiIn * 1.15, "SHINE raises 10-16 kHz");

    // MATCH: hearing the dry signal, level-matched to the processed one.
    host.setParam ("Shine", "0 %");
    host.setParam ("Rush", "100 %");
    host.setParam ("Rush Auto Gain", "Off");
    host.setParam ("Match", "On");
    out.clear();
    const juce::int64 matchStart = host.playHead.samples;
    for (int b = 0; b < 500; ++b) host.processBlock (feed, &out);   // > 5 s so both meters settle
    // Correlation with the delayed dry input must be ~1 (it is the dry signal, just gained).
    double sab = 0.0, saa = 0.0, sbb = 0.0;
    for (size_t i = out.size() - 48000; i < out.size(); ++i)
    {
        const double a = out[i];
        const double b = pinkL[(size_t) ((matchStart + (juce::int64) i - latency) % (juce::int64) pinkL.size())];
        sab += a * b; saa += a * a; sbb += b * b;
    }
    check (sab / std::sqrt (saa * sbb) > 0.98, "MATCH outputs the dry signal");
    check (std::fabs (rmsOf (out, out.size() - 48000, out.size()) / rmsOf (pinkL, 0, 48000)) > 0.5, "MATCH gain is finite and sane");
    host.setParam ("Match", "Off");
}

} // namespace

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf ("usage: VST3HostTest <plugin.vst3> <Strata|VoxNote|Intone|Ducker|Frisson|Humline>\n");
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::String path (argv[1]);
    const juce::String which (argv[2]);

    Host host;
    if (! host.load (path)) return 1;

    if (which == "Ducker" || which == "Strata" || which == "Humline")
        host.plugin->enableAllBuses();

    host.prepare();
    commonChecks (host);

    if (which == "Strata")       testStrata (host);
    else if (which == "VoxNote") testVoxNote (host);
    else if (which == "Intone")  testIntone (host);
    else if (which == "Ducker")  testDucker (host);
    else if (which == "Frisson") testFrisson (host);
    else if (which == "Humline") testHumline (host);
    else { std::printf ("unknown plugin tag %s\n", which.toRawUTF8()); return 2; }

    host.plugin->releaseResources();
    host.plugin.reset();

    std::printf ("%s: %d failure(s)\n", which.toRawUTF8(), failures);
    return failures == 0 ? 0 : 1;
}
