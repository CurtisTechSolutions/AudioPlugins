// End-to-end check of a built VST3: load it through JUCE's hosting code, run
// audio/MIDI through processBlock, inspect the output.
//
//   VST3HostTest <path/to/Plugin.vst3> <Strata|VoxNote|Intone|Ducker|Frisson|Verdant>

#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

#include <ap/Math.h>
#include <ap/PitchDetector.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <iterator>
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

/** The factory presets of the APVTS plugins: exposed as programs (program 0 "Default"), each one
    changes parameters through the VST3 wrapper, plays finite (instruments: audible and unclipped)
    audio, and the program number survives a state round trip. */
/** The program-change and bypass parameters the JUCE VST3 wrapper adds of its own. */
bool isWrapperParameter (const juce::AudioProcessorParameter& p)
{
    const auto name = p.getName (64);
    return name == "Program" || name == "Bypass";
}

void factoryProgramChecks (Host& host, bool instrument)
{
    std::printf ("Factory presets\n");
    auto* plugin = host.plugin.get();
    const int n = plugin->getNumPrograms();
    std::printf ("    %d programs\n", n);
    check (n >= 15, "at least 15 factory presets are exposed as programs");
    check (plugin->getProgramName (0) == "Default", "program 0 is \"Default\"");

    juce::StringArray names;
    for (int i = 0; i < n; ++i) names.addIfNotAlreadyThere (plugin->getProgramName (i));
    check (names.size() == n && ! names.contains ({}), "program names are unique and non-empty");

    auto silence = [] (int, int, juce::int64) { return 0.0f; };
    auto signal = [] (int, int, juce::int64 t) { return tone (220.0, t, 0.25, 6); };
    int wrongProgram = 0, unchanged = 0, nonFinite = 0, quiet = 0, clipped = 0;
    for (int i = 0; i < n; ++i)
    {
        plugin->setCurrentProgram (i);
        // VST3 delivers the program change with the next block; then let gain smoothing settle.
        for (int b = 0; b < 8; ++b) host.processBlock (silence);
        if (plugin->getCurrentProgram() != i) ++wrongProgram;

        bool changed = false;
        for (auto* p : plugin->getParameters())
            if (! isWrapperParameter (*p) && std::fabs (p->getValue() - p->getDefaultValue()) > 1.0e-4f) changed = true;
        if (i > 0 && ! changed) { ++unchanged; std::printf ("    %s: no parameter differs from its default\n",
                                                           plugin->getProgramName (i).toRawUTF8()); }

        std::vector<float> out;
        bool finite = true;
        if (instrument)
        {
            for (int note : { 48, 55, 60 }) host.midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
            for (int b = 0; b < 60; ++b) { host.processBlock (silence, &out); finite = finite && host.outputIsFinite(); }
            for (int note : { 48, 55, 60 }) host.midi.addEvent (juce::MidiMessage::noteOff (1, note), 0);
            for (int b = 0; b < 20; ++b) { host.processBlock (silence, &out); finite = finite && host.outputIsFinite(); }
            host.midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            host.midi.addEvent (juce::MidiMessage::allSoundOff (1), 0);
            // Let release and FX tails die away so the next preset starts from silence.
            std::vector<float> tail (out.end() - kBlock, out.end());
            for (int b = 0; b < 600 && rmsOf (tail, 0, tail.size()) > 1.0e-5; ++b)
            {
                tail.clear();
                host.processBlock (silence, &tail);
                finite = finite && host.outputIsFinite();
            }
        }
        else
        {
            for (int b = 0; b < 30; ++b) host.processBlock (signal);   // let dynamics and smoothing settle
            for (int b = 0; b < 60; ++b) { host.processBlock (signal, &out); finite = finite && host.outputIsFinite(); }
            for (int b = 0; b < 20; ++b) host.processBlock (silence);
        }

        float peak = 0.0f;
        for (float x : out) peak = std::max (peak, std::fabs (x));
        const double rms = rmsOf (out, 0, (size_t) kBlock * 60);
        std::printf ("    %3d %-24s rms %6.1f dBFS  peak %.3f\n", i, plugin->getProgramName (i).toRawUTF8(),
                     20.0 * std::log10 (std::max (rms, 1.0e-9)), (double) peak);
        if (! finite) ++nonFinite;
        if (instrument && rms < 1.0e-3) ++quiet;
        if (instrument && peak > 0.98f) ++clipped;
    }
    check (wrongProgram == 0, "every program is selected by setCurrentProgram");
    check (unchanged == 0, "every preset changes parameters through the wrapper");
    check (nonFinite == 0, "every preset produces finite output");
    if (instrument)
    {
        check (quiet == 0, "every preset is audible (RMS above -60 dBFS)");
        check (clipped == 0, "no preset clips a three-note chord (peak below 0.98)");
    }

    // The program number travels with the state.
    const int k = n / 2;
    plugin->setCurrentProgram (k);
    host.processBlock (silence);
    juce::MemoryBlock state;
    plugin->getStateInformation (state);
    plugin->setCurrentProgram (0);
    host.processBlock (silence);
    plugin->setStateInformation (state.getData(), (int) state.getSize());
    host.processBlock (silence);
    check (plugin->getCurrentProgram() == k, "the current program survives a state round trip");

    plugin->setCurrentProgram (0);
    for (int b = 0; b < 4; ++b) host.processBlock (silence);
    bool allDefault = true;
    for (auto* p : plugin->getParameters())
        if (! isWrapperParameter (*p) && std::fabs (p->getValue() - p->getDefaultValue()) > 1.0e-4f)
        {
            std::printf ("    %s is not at its default after program 0\n", p->getName (64).toRawUTF8());
            allDefault = false;
        }
    check (allDefault, "program 0 returns every parameter to its default");
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

void testVerdant (Host& host)
{
    std::printf ("Verdant checks\n");
    const auto silence = [] (int, int, juce::int64) { return 0.0f; };
    std::vector<float> out;

    host.midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    for (int b = 0; b < 30; ++b)
        host.processBlock (silence, &out);

    check (host.outputIsFinite(), "synth output is finite");
    const double level = rmsOf (out, 2000, out.size());
    check (level > 0.01, "note-on produces sound");
    checkNear (measureHz (out, 4096), 261.63, 4.0, "C4 is in tune");

    host.midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
    std::vector<float> tail;
    for (int b = 0; b < 300; ++b)
        host.processBlock (silence, &tail);
    check (rmsOf (tail, tail.size() - kBlock * 4, tail.size()) < 1.0e-4, "release decays to silence");

    // Polyphony: a chord is louder than a single note.
    std::vector<float> chord;
    for (int n : { 60, 64, 67 }) host.midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
    for (int b = 0; b < 30; ++b)
        host.processBlock (silence, &chord);
    check (rmsOf (chord, 2000, chord.size()) > level * 1.2, "three notes are louder than one");
    for (int n : { 60, 64, 67 }) host.midi.addEvent (juce::MidiMessage::noteOff (1, n), 0);
    for (int b = 0; b < 100; ++b) host.processBlock (silence);

    // Polyphony limit: Voices = 1 must steal, so a second note silences the first.
    host.setParam ("Voices", "1");
    host.setParam ("Env 1 Release", "20 ms");
    host.midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    for (int b = 0; b < 10; ++b) host.processBlock (silence);
    host.midi.addEvent (juce::MidiMessage::noteOn (1, 67, (juce::uint8) 100), 0);
    std::vector<float> mono;
    for (int b = 0; b < 30; ++b) host.processBlock (silence, &mono);
    const double c4 = bandRmsOf (mono, 255.0, 268.0, 4096);
    const double g4 = bandRmsOf (mono, 385.0, 400.0, 4096);
    check (g4 > c4 * 4.0, "Voices = 1 steals: only the newest note sounds");
    for (int n : { 60, 67 }) host.midi.addEvent (juce::MidiMessage::noteOff (1, n), 0);
    for (int b = 0; b < 50; ++b) host.processBlock (silence);
    host.setParam ("Voices", "16");
    host.setParam ("Env 1 Release", "300 ms");

    // MIDI conversion: the pitch wheel at full up bends by the bend range (default 2 st: C4 -> D4).
    host.midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    host.midi.addEvent (juce::MidiMessage::pitchWheel (1, 16383), 0);
    std::vector<float> bent;
    for (int b = 0; b < 30; ++b) host.processBlock (silence, &bent);
    checkNear (measureHz (bent, 4096), 293.66, 5.0, "pitch wheel up bends by the bend range");
    host.midi.addEvent (juce::MidiMessage::pitchWheel (1, 8192), 0);
    host.midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
    for (int b = 0; b < 100; ++b) host.processBlock (silence);

    // Sustain pedal (CC64) holds released notes until the pedal goes up.
    host.midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 0);
    host.midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    for (int b = 0; b < 10; ++b) host.processBlock (silence);
    host.midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
    std::vector<float> held;
    for (int b = 0; b < 60; ++b) host.processBlock (silence, &held);
    check (rmsOf (held, held.size() - kBlock * 4, held.size()) > 0.01, "sustain pedal holds a released note");
    host.midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, 0), 0);
    std::vector<float> pedalUp;
    for (int b = 0; b < 300; ++b) host.processBlock (silence, &pedalUp);
    check (rmsOf (pedalUp, pedalUp.size() - kBlock * 4, pedalUp.size()) < 1.0e-4, "pedal up releases the held note");

    // All Notes Off (CC123) releases everything that is held.
    for (int n : { 60, 64 }) host.midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
    for (int b = 0; b < 10; ++b) host.processBlock (silence);
    host.midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
    std::vector<float> allOff;
    for (int b = 0; b < 300; ++b) host.processBlock (silence, &allOff);
    check (rmsOf (allOff, allOff.size() - kBlock * 4, allOff.size()) < 1.0e-4, "all notes off silences held notes");

    // State round trip: changed parameters of every kind come back exactly after other edits.
    {
        struct Probe { const char* name; const char* text; float other; };
        struct Saved { float value = 0.0f; juce::String text; };
        Probe probes[] = { { "Osc A Position", "37 %", 0.9f },        // linear float
                           { "Filter 1 Cutoff", "1.5 kHz", 0.95f },    // skewed float
                           { "Voice Mode", "Mono", 1.0f },             // choice
                           { "Osc A Unison", "5", 0.0f },              // int
                           { "Mod 3 Amount", "-42 %", 0.75f } };       // bipolar float
        for (auto& p : probes)
            host.setParam (p.name, p.text);
        host.processBlock (silence);   // VST3 delivers parameter edits to the processor with the next block
        Saved saved[std::size (probes)];
        for (size_t i = 0; i < std::size (probes); ++i)
            if (auto* param = host.param (probes[i].name))
                saved[i] = { param->getValue(), param->getCurrentValueAsText() };

        juce::MemoryBlock state;
        host.plugin->getStateInformation (state);
        for (auto& p : probes)
            host.setParamNormalised (p.name, p.other);
        host.processBlock (silence);
        host.plugin->setStateInformation (state.getData(), (int) state.getSize());
        host.processBlock (silence);

        bool valuesBack = true, textBack = true;
        for (size_t i = 0; i < std::size (probes); ++i)
            if (auto* param = host.param (probes[i].name))
            {
                if (std::fabs (param->getValue() - saved[i].value) > 1.0e-4f)
                {
                    std::printf ("    %s: %.5f after restore, %.5f saved\n", probes[i].name, param->getValue(), saved[i].value);
                    valuesBack = false;
                }
                if (param->getCurrentValueAsText() != saved[i].text)
                {
                    std::printf ("    %s: '%s' after restore, '%s' saved\n", probes[i].name,
                                 param->getCurrentValueAsText().toRawUTF8(), saved[i].text.toRawUTF8());
                    textBack = false;
                }
            }
        check (valuesBack, "state round trip restores the changed parameter values");
        check (textBack, "restored parameters read back as the saved text (e.g. 37 %, 1.5 kHz, Mono)");
        check (host.outputIsFinite(), "still processing after restoring a changed state");

        for (auto& p : probes)   // back to the defaults for the program check below
            if (auto* param = host.param (p.name))
                param->setValueNotifyingHost (param->getDefaultValue());
        host.processBlock (silence);
    }

    // Factory presets are the program list; loading one still plays.
    check (host.plugin->getNumPrograms() > 1, "factory presets are exposed as programs");
    check (host.plugin->getProgramName (1).isNotEmpty(), "program 1 has a name");
    host.plugin->setCurrentProgram (1);
    check (host.plugin->getCurrentProgram() == 1, "program 1 is selected");
    std::vector<float> preset;
    host.midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    for (int b = 0; b < 40; ++b) host.processBlock (silence, &preset);
    check (host.outputIsFinite(), "factory preset 1 output is finite");
    check (rmsOf (preset, 0, preset.size()) > 1.0e-4, "factory preset 1 produces sound");
    host.midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
    for (int b = 0; b < 100; ++b) host.processBlock (silence);

    // The newest preset (the end of the latest expansion) loads and plays through the wrapper too.
    const int last = host.plugin->getNumPrograms() - 1;
    check (last >= 198, "the expansion presets are in the program list (199 or more)");
    host.plugin->setCurrentProgram (last);
    host.processBlock (silence);
    check (host.plugin->getCurrentProgram() == last, "the last program is selected");
    std::printf ("    last program: %d '%s'\n", last, host.plugin->getProgramName (last).toRawUTF8());
    std::vector<float> newest;
    host.midi.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0);
    for (int b = 0; b < 40; ++b) host.processBlock (silence, &newest);
    check (host.outputIsFinite() && rmsOf (newest, 0, newest.size()) > 1.0e-4, "the last factory preset produces sound");
    host.midi.addEvent (juce::MidiMessage::noteOff (1, 48), 0);
    for (int b = 0; b < 100; ++b) host.processBlock (silence);
}

} // namespace

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf ("usage: VST3HostTest <plugin.vst3> <Strata|VoxNote|Intone|Ducker|Frisson|Verdant>\n");
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::String path (argv[1]);
    const juce::String which (argv[2]);

    Host host;
    if (! host.load (path)) return 1;

    if (which == "Ducker" || which == "Strata" || which == "Verdant")
        host.plugin->enableAllBuses();

    host.prepare();
    commonChecks (host);

    if (which == "Strata")       testStrata (host);
    else if (which == "VoxNote") testVoxNote (host);
    else if (which == "Intone")  testIntone (host);
    else if (which == "Ducker")  testDucker (host);
    else if (which == "Frisson") testFrisson (host);
    else if (which == "Verdant") testVerdant (host);
    else { std::printf ("unknown plugin tag %s\n", which.toRawUTF8()); return 2; }

    if (which != "Verdant")   // Verdant's bank has its own checks (testVerdant and verdant_tests)
        factoryProgramChecks (host, which == "Strata");

    host.plugin->releaseResources();
    host.plugin.reset();

    std::printf ("%s: %d failure(s)\n", which.toRawUTF8(), failures);
    return failures == 0 ? 0 : 1;
}
