#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

#include <ap_plug/Params.h>

namespace voxnote
{

using namespace ap::plug;

namespace
{
    constexpr int kHop = 256;

    struct TrackingPreset { float minHz, maxHz; int window; };
    constexpr TrackingPreset kPresets[3] = {
        { 40.0f, 1000.0f, 2048 },   // Low: bass voices / instruments, more latency
        { 60.0f, 1600.0f, 1024 },   // Mid: typical singing & humming
        { 100.0f, 3000.0f, 640 },   // High: whistling, high voices, less latency
    };

    juce::StringArray keyNames()
    {
        juce::StringArray a;
        for (int i = 0; i < 12; ++i) a.add (ap::noteName (i));
        return a;
    }

    juce::StringArray scaleNames()
    {
        juce::StringArray a;
        for (int i = 0; i < (int) ap::ScaleType::Custom; ++i) a.add (ap::scaleName ((ap::ScaleType) i));
        return a;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout Processor::createLayout()
{
    Layout layout;
    layout.add (choiceParam (ids::tracking, "Tracking", { "Low (bass)", "Mid (voice)", "High (whistle)" }, 1));
    layout.add (dbParam (ids::gate, "Gate", -80.0f, -10.0f, -45.0f));
    layout.add (percentParam (ids::sensitivity, "Sensitivity", 55.0f));
    layout.add (msParam (ids::onset, "Onset", 5.0f, 120.0f, 15.0f));
    layout.add (msParam (ids::release, "Release", 10.0f, 300.0f, 40.0f));
    layout.add (floatParam (ids::retrigger, "Retrigger", 30.0f, 100.0f, 60.0f, 0.0f, "ct", 0));
    layout.add (intParam (ids::transpose, "Transpose", -24, 24, 0, "st"));
    layout.add (boolParam (ids::snap, "Snap To Scale", false));
    layout.add (choiceParam (ids::key, "Key", keyNames(), 0));
    layout.add (choiceParam (ids::scale, "Scale", scaleNames(), (int) ap::ScaleType::Chromatic));
    layout.add (boolParam (ids::bend, "Pitch Bend", true));
    layout.add (intParam (ids::bendRange, "Bend Range", 1, 24, 2, "st"));
    layout.add (boolParam (ids::velDynamic, "Dynamic Vel", true));
    layout.add (intParam (ids::velFixed, "Fixed Vel", 1, 127, 100));
    layout.add (dbParam (ids::velMin, "Vel Min", -60.0f, -20.0f, -40.0f));
    layout.add (dbParam (ids::velMax, "Vel Max", -30.0f, 0.0f, -8.0f));
    layout.add (intParam (ids::channel, "MIDI Channel", 1, 16, 1));
    layout.add (boolParam (ids::audioThru, "Audio Thru", true));
    layout.add (boolParam (ids::monitor, "Monitor Synth", false));
    layout.add (dbParam (ids::monitorLevel, "Monitor Level", -40.0f, 0.0f, -12.0f));
    return layout;
}

Processor::Processor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "VOXNOTE", createLayout()),
      programs (*this, factoryPresets())
{
    prm.tracking = raw (apvts, ids::tracking);   prm.gate = raw (apvts, ids::gate);
    prm.sensitivity = raw (apvts, ids::sensitivity); prm.onset = raw (apvts, ids::onset);
    prm.release = raw (apvts, ids::release);     prm.retrigger = raw (apvts, ids::retrigger);
    prm.transpose = raw (apvts, ids::transpose); prm.snap = raw (apvts, ids::snap);
    prm.key = raw (apvts, ids::key);             prm.scale = raw (apvts, ids::scale);
    prm.bend = raw (apvts, ids::bend);           prm.bendRange = raw (apvts, ids::bendRange);
    prm.velDynamic = raw (apvts, ids::velDynamic); prm.velFixed = raw (apvts, ids::velFixed);
    prm.velMin = raw (apvts, ids::velMin);       prm.velMax = raw (apvts, ids::velMax);
    prm.channel = raw (apvts, ids::channel);     prm.audioThru = raw (apvts, ids::audioThru);
    prm.monitor = raw (apvts, ids::monitor);     prm.monitorLevel = raw (apvts, ids::monitorLevel);
}

void Processor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    for (int i = 0; i < 3; ++i)
        trackers[(size_t) i].prepare (sampleRate, kHop, kPresets[i].window, kPresets[i].minHz, kPresets[i].maxHz);
    noteTracker.reset();
    monitorOsc.prepare (sampleRate);
    monitorOsc.setWaveform (ap::Waveform::Sine);
    monitorEnv = 0.0f;
    updateTrackerParams();
}

void Processor::reset()
{
    // Called by hosts on transport stop / bypass: make sure no note hangs.
    noteTracker.reset();
    monitorTarget = 0.0f;
    pendingAllNotesOff.store (true);
}

bool Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo()) return false;
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return true;
}

void Processor::updateTrackerParams()
{
    activeTracker = juce::jlimit (0, 2, (int) prm.tracking->load());
    const float hopMs = (float) (1000.0 * kHop / sr);

    ap::NoteTracker::Params p;
    p.gateDb = prm.gate->load();
    p.minConfidence = ap::lerp (0.9f, 0.3f, prm.sensitivity->load() * 0.01f);
    p.onsetFrames = juce::jmax (1, (int) std::lround (prm.onset->load() / hopMs));
    p.releaseFrames = juce::jmax (1, (int) std::lround (prm.release->load() / hopMs));
    p.changeFrames = juce::jmax (1, p.onsetFrames);
    p.retriggerCents = prm.retrigger->load();
    p.pitchBend = prm.bend->load() > 0.5f;
    p.bendRangeSemis = prm.bendRange->load();
    p.transpose = (int) prm.transpose->load();
    p.snapToScale = prm.snap->load() > 0.5f;
    p.scale = ap::Scale::fromType ((ap::ScaleType) (int) prm.scale->load(), (int) prm.key->load());
    p.dynamicVelocity = prm.velDynamic->load() > 0.5f;
    p.fixedVelocity = (int) prm.velFixed->load();
    p.velocityMinDb = prm.velMin->load();
    p.velocityMaxDb = juce::jmax (p.velocityMinDb + 1.0f, prm.velMax->load());
    noteTracker.setParams (p);

    for (auto& t : trackers)
        t.setThreshold (ap::lerp (0.08f, 0.35f, prm.sensitivity->load() * 0.01f));
}

void Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();
    updateTrackerParams();

    const int numSamples = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int channel = juce::jlimit (1, 16, (int) prm.channel->load());
    auto& tracker = trackers[(size_t) activeTracker];

    if (pendingAllNotesOff.exchange (false))
        midi.addEvent (juce::MidiMessage::allNotesOff (activeChannel), 0);

    // Feed the tracker sample by sample so each frame's events land at the
    // right position inside the block.
    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        float s = 0.0f;
        for (int ch = 0; ch < numIn; ++ch) s += buffer.getReadPointer (ch)[i];
        s /= (float) juce::jmax (1, numIn);
        peak = juce::jmax (peak, std::fabs (s));

        tracker.process (&s, 1, [&] (const ap::PitchTracker::Frame& f)
        {
            detectedHz.store (f.voiced ? f.frequency : 0.0f, std::memory_order_relaxed);
            noteTracker.processFrame (f, [&] (const ap::NoteTracker::Event& e)
            {
                switch (e.type)
                {
                    case ap::NoteTracker::Event::Type::NoteOn:
                        activeChannel = channel;
                        midi.addEvent (juce::MidiMessage::noteOn (channel, e.note, (juce::uint8) e.velocity), i);
                        monitorTarget = ap::midiToHz ((float) e.note);
                        monitorBend = 0.0f;
                        break;
                    case ap::NoteTracker::Event::Type::NoteOff:
                        midi.addEvent (juce::MidiMessage::noteOff (activeChannel, e.note), i);
                        monitorTarget = 0.0f;
                        break;
                    case ap::NoteTracker::Event::Type::PitchBend:
                    {
                        const int wheel = juce::jlimit (0, 16383, (int) std::lround (8192.0f + e.bend * 8191.0f));
                        midi.addEvent (juce::MidiMessage::pitchWheel (activeChannel, wheel), i);
                        monitorBend = e.bend * prm.bendRange->load();
                        break;
                    }
                }
            });
            currentNote.store (noteTracker.currentMidi(), std::memory_order_relaxed);
        });
    }
    inputLevel.store (peak, std::memory_order_relaxed);

    // Audio output: pass-through and/or monitor synth.
    const bool thru = prm.audioThru->load() > 0.5f;
    const bool monitorOn = prm.monitor->load() > 0.5f;
    const float monitorGain = ap::dbToGain (prm.monitorLevel->load());

    if (! thru)
        buffer.clear();
    else
        for (int ch = numIn; ch < numOut; ++ch)
            buffer.copyFrom (ch, 0, buffer, 0, 0, numSamples);

    if (monitorOn || monitorEnv > 1.0e-4f)
    {
        const float attack = ap::onePoleCoeff (4.0f, sr);
        const float release = ap::onePoleCoeff (60.0f, sr);
        for (int i = 0; i < numSamples; ++i)
        {
            const bool gateOn = monitorOn && monitorTarget > 0.0f;
            const float target = gateOn ? 1.0f : 0.0f;
            monitorEnv = target + (gateOn ? attack : release) * (monitorEnv - target);
            if (gateOn && (i & 15) == 0)
                monitorOsc.setFrequency (monitorTarget * ap::semitonesToRatio (monitorBend));
            const float s = monitorOsc.next() * monitorEnv * monitorGain;
            for (int ch = 0; ch < numOut; ++ch)
                buffer.getWritePointer (ch)[i] += s;
        }
    }
}

juce::AudioProcessorEditor* Processor::createEditor()
{
    return new Editor (*this);
}

void Processor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    programs.saveTo (state);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void Processor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            programs.restoreFrom (*xml);
        }
}

} // namespace voxnote

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new voxnote::Processor();
}
