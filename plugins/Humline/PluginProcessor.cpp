#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <ap_plug/Params.h>

namespace humline
{

using namespace ap::plug;

namespace
{
    struct TrackingPreset { float minHz, maxHz; int window; };
    constexpr TrackingPreset kPresets[3] = {
        { 40.0f, 1000.0f, 2048 },   // Low: bass voices, more latency
        { 60.0f, 1600.0f, 1024 },   // Mid: typical singing & humming
        { 100.0f, 3000.0f, 640 },   // High: whistling, less latency
    };

    constexpr float kRetriggerCents = 60.0f;
    constexpr float kVelMinDb = -40.0f, kVelMaxDb = -8.0f;
    constexpr int   kTicksPerQuarter = 960;

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

    juce::StringArray gridNames()
    {
        juce::StringArray a { "Off" };
        for (auto d : kGridDivisions) a.add (ap::noteDivisionName (d));
        return a;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout Processor::createLayout()
{
    Layout layout;
    // Detection
    layout.add (choiceParam (ids::tracking, "Tracking", { "Low (bass)", "Mid (voice)", "High (whistle)" }, 1));
    layout.add (dbParam (ids::gate, "Gate", -80.0f, -10.0f, -45.0f));
    layout.add (percentParam (ids::sensitivity, "Sensitivity", 55.0f));
    layout.add (msParam (ids::minNote, "Min Note", 20.0f, 200.0f, 60.0f));
    layout.add (msParam (ids::release, "Release", 10.0f, 300.0f, 50.0f));
    // Pitch
    layout.add (choiceParam (ids::key, "Key", keyNames(), 0));
    layout.add (choiceParam (ids::scale, "Scale", scaleNames(), (int) ap::ScaleType::Major));
    layout.add (percentParam (ids::snap, "Snap", 100.0f));
    layout.add (intParam (ids::octave, "Octave", -2, 2, 0));
    layout.add (boolParam (ids::bend, "Pitch Bend", false));
    layout.add (intParam (ids::bendRange, "Bend Range", 1, 24, 2, "st"));
    // Rhythm
    layout.add (choiceParam (ids::grid, "Grid", gridNames(), 0));
    // Synth
    layout.add (choiceParam (ids::timbre, "Timbre", { "Soft Lead", "Saw Lead", "Pluck", "Pad", "Bass", "Bell" }, 0));
    layout.add (hzParam (ids::synthCutoff, "Cutoff", 20.0f, 20000.0f, 3000.0f));
    layout.add (percentParam (ids::synthRes, "Resonance", 20.0f));
    layout.add (msParam (ids::synthAttack, "Attack", 1.0f, 2000.0f, 5.0f));
    layout.add (msParam (ids::synthRelease, "Synth Release", 1.0f, 4000.0f, 250.0f));
    layout.add (msParam (ids::glide, "Glide", 0.0f, 500.0f, 20.0f));
    layout.add (dbParam (ids::synthLevel, "Synth Level", -60.0f, 6.0f, -6.0f));
    // Output
    layout.add (percentParam (ids::dryMix, "Dry Voice", 0.0f));
    layout.add (boolParam (ids::midiOut, "Send MIDI", true));
    layout.add (intParam (ids::channel, "MIDI Channel", 1, 16, 1));
    layout.add (boolParam (ids::velDynamic, "Dynamic Vel", true));
    layout.add (intParam (ids::velFixed, "Fixed Vel", 1, 127, 100));
    return layout;
}

Processor::Processor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "HUMLINE", createLayout())
{
    prm.tracking = raw (apvts, ids::tracking);       prm.gate = raw (apvts, ids::gate);
    prm.sensitivity = raw (apvts, ids::sensitivity); prm.minNote = raw (apvts, ids::minNote);
    prm.release = raw (apvts, ids::release);         prm.key = raw (apvts, ids::key);
    prm.scale = raw (apvts, ids::scale);             prm.snap = raw (apvts, ids::snap);
    prm.octave = raw (apvts, ids::octave);           prm.bend = raw (apvts, ids::bend);
    prm.bendRange = raw (apvts, ids::bendRange);     prm.grid = raw (apvts, ids::grid);
    prm.timbre = raw (apvts, ids::timbre);           prm.synthCutoff = raw (apvts, ids::synthCutoff);
    prm.synthRes = raw (apvts, ids::synthRes);       prm.synthAttack = raw (apvts, ids::synthAttack);
    prm.synthRelease = raw (apvts, ids::synthRelease); prm.glide = raw (apvts, ids::glide);
    prm.synthLevel = raw (apvts, ids::synthLevel);   prm.dryMix = raw (apvts, ids::dryMix);
    prm.midiOut = raw (apvts, ids::midiOut);         prm.channel = raw (apvts, ids::channel);
    prm.velDynamic = raw (apvts, ids::velDynamic);   prm.velFixed = raw (apvts, ids::velFixed);

    for (int i = 0; i < kVoices; ++i)
    {
        auto* v = new ap::synth::Voice (settings);
        v->setGlideSource (&lastNoteHz);
        synth.addVoice (v);
    }
    synth.addSound (new ap::synth::Sound());
    synth.setNoteStealingEnabled (true);
    synth.setPolyphony (kVoices);
}

void Processor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    int maxLatency = 0;
    for (int i = 0; i < 3; ++i)
    {
        auto& t = trackers[(size_t) i];
        t.prepare (sampleRate, kHop, kPresets[i].window, kPresets[i].minHz, kPresets[i].maxHz);
        maxLatency = juce::jmax (maxLatency, t.latencySamples());
    }
    maxLatency += (int) std::ceil (0.2 * sampleRate) + 2 * kHop;    // Min Note hold up to 200 ms, rounded to hops
    for (auto& d : dryDelay) d.prepare (maxLatency);

    synth.setCurrentPlaybackSampleRate (sampleRate);
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* v = dynamic_cast<ap::synth::Voice*> (synth.getVoice (i)))
            v->prepare (sampleRate, samplesPerBlock);

    synthMidi.ensureSize (8192);
    dryGain.prepare (sampleRate, 20.0f);
    dryGain.snap (prm.dryMix->load() * 0.01f);
    synthGain.prepare (sampleRate, 20.0f);
    synthGain.snap (ap::dbToGain (prm.synthLevel->load()));

    noteTracker.reset();
    rhythm.reset();
    soundingNote = -1;
    clock = 0;
    freeRunBeats = 0.0;
    lastNoteHz = 0.0f;
    latency = -1;
    lastTimbre = -1;
    updateParams();
}

void Processor::reset()
{
    // Transport stop / bypass: nothing may hang. The note-offs go out at the
    // start of the next block (reset() is not necessarily on the audio thread).
    noteTracker.reset();
    pendingAllNotesOff.store (true);
}

bool Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (! in.isDisabled() && in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo()) return false;
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return true;
}

void Processor::applyTimbre (int timbre)
{
    using ap::Waveform;
    ap::synth::Settings s;        // Strata defaults, then the patch on top
    s.unisonVoices = 1;
    s.subLevel = 0.0f;
    s.noiseLevel = 0.0f;
    s.filterType = 0;
    s.keyTrack = 0.5f;
    s.filterLfoAmount = 0.0f;
    s.drive = 0.0f;
    s.lfo1DepthCents = 0.0f;
    s.velocityToAmp = 0.6f;
    s.velocityToFilter = 0.3f;

    switch (timbre)
    {
        default:
        case 0:   // Soft Lead
            s.oscA = { Waveform::Triangle, 0, 0, 0.0f, 0.7f, 0.5f };
            s.oscB = { Waveform::Saw, 0, 0, 5.0f, 0.25f, 0.5f };
            s.subLevel = 0.2f;
            s.filterEnvAmount = 0.3f;
            s.ampEnv = { 0.005f, 0.2f, 0.8f, 0.25f };
            s.filterEnv = { 0.005f, 0.3f, 0.3f, 0.25f };
            s.lfo1RateHz = 5.0f;
            s.lfo1DepthCents = 8.0f;
            s.lfo1FadeMs = 300.0f;
            break;
        case 1:   // Saw Lead
            s.oscA = { Waveform::Saw, 0, 0, 0.0f, 0.8f, 0.5f };
            s.oscB = { Waveform::Saw, 0, 0, 7.0f, 0.4f, 0.5f };
            s.unisonVoices = 2;
            s.unisonDetuneCents = 10.0f;
            s.unisonSpread = 0.4f;
            s.filterEnvAmount = 0.4f;
            s.drive = 0.15f;
            s.ampEnv = { 0.005f, 0.2f, 0.8f, 0.25f };
            s.filterEnv = { 0.005f, 0.3f, 0.3f, 0.25f };
            break;
        case 2:   // Pluck
            s.oscA = { Waveform::Saw, 0, 0, 0.0f, 0.7f, 0.5f };
            s.oscB = { Waveform::Square, 1, 0, 0.0f, 0.3f, 0.5f };
            s.filterEnvAmount = 0.6f;
            s.ampEnv = { 0.002f, 0.35f, 0.0f, 0.25f };
            s.filterEnv = { 0.002f, 0.25f, 0.0f, 0.3f };
            break;
        case 3:   // Pad
            s.oscA = { Waveform::Saw, 0, 0, 0.0f, 0.6f, 0.5f };
            s.oscB = { Waveform::Saw, 0, 0, -7.0f, 0.4f, 0.5f };
            s.unisonVoices = 4;
            s.unisonDetuneCents = 18.0f;
            s.unisonSpread = 0.8f;
            s.filterEnvAmount = 0.2f;
            s.ampEnv = { 0.3f, 0.5f, 0.9f, 0.8f };
            s.filterEnv = { 0.4f, 0.6f, 0.5f, 0.8f };
            s.lfo1RateHz = 0.3f;
            s.lfo1DepthCents = 4.0f;
            break;
        case 4:   // Bass
            s.oscA = { Waveform::Square, -1, 0, 0.0f, 0.8f, 0.5f };
            s.oscB = { Waveform::Saw, -1, 0, 0.0f, 0.3f, 0.5f };
            s.subLevel = 0.5f;
            s.subOctave = -1;
            s.filterEnvAmount = 0.5f;
            s.keyTrack = 0.3f;
            s.drive = 0.2f;
            s.ampEnv = { 0.002f, 0.2f, 0.8f, 0.15f };
            s.filterEnv = { 0.002f, 0.2f, 0.1f, 0.2f };
            break;
        case 5:   // Bell
            s.oscA = { Waveform::Triangle, 0, 0, 0.0f, 0.6f, 0.5f };
            s.oscB = { Waveform::Sine, 2, 0, 3.0f, 0.4f, 0.5f };
            s.filterEnvAmount = 0.5f;
            s.keyTrack = 0.7f;
            s.ampEnv = { 0.002f, 1.2f, 0.3f, 0.6f };
            s.filterEnv = { 0.002f, 0.8f, 0.1f, 0.6f };
            break;
    }
    settings = s;
}

void Processor::updateParams()
{
    const int wanted = juce::jlimit (0, 2, (int) prm.tracking->load());
    if (wanted != activeTracker)
    {
        trackers[(size_t) wanted].resync (trackers[(size_t) activeTracker].currentTime());
        activeTracker = wanted;
    }
    const float sens = prm.sensitivity->load() * 0.01f;
    for (auto& t : trackers) t.setThreshold (ap::lerp (0.08f, 0.35f, sens));

    const float hopMs = (float) (1000.0 * kHop / sr);
    const int holdFrames = juce::jmax (1, (int) std::lround (prm.minNote->load() / hopMs));
    holdSamples = holdFrames * kHop;

    ap::NoteTracker::Params p;
    p.gateDb = prm.gate->load();
    p.minConfidence = ap::lerp (0.9f, 0.3f, sens);
    p.onsetFrames = holdFrames;
    p.changeFrames = holdFrames;
    p.releaseFrames = juce::jmax (1, (int) std::lround (prm.release->load() / hopMs));
    p.retriggerCents = kRetriggerCents;
    p.pitchBend = prm.bend->load() > 0.5f;
    p.bendRangeSemis = prm.bendRange->load();
    p.transpose = 12 * (int) prm.octave->load();
    p.snapToScale = true;
    p.snapStrength = prm.snap->load() * 0.01f;
    p.scale = ap::Scale::fromType ((ap::ScaleType) (int) prm.scale->load(), (int) prm.key->load());
    p.dynamicVelocity = prm.velDynamic->load() > 0.5f;
    p.fixedVelocity = (int) prm.velFixed->load();
    p.velocityMinDb = kVelMinDb;
    p.velocityMaxDb = kVelMaxDb;
    noteTracker.setParams (p);

    const int newLatency = trackers[(size_t) activeTracker].latencySamples() + holdSamples;
    if (newLatency != latency)
    {
        latency = newLatency;
        setLatencySamples (latency);
    }
    capture.setMinimumLength (holdSamples);

    rhythm.setGrid (gridBeatsForChoice ((int) prm.grid->load()));

    // Synth: the timbre patch, then the knobs that always win.
    applyTimbre ((int) prm.timbre->load());
    settings.cutoffHz = prm.synthCutoff->load();
    settings.resonance = prm.synthRes->load() * 0.01f;
    settings.ampEnv.attack = prm.synthAttack->load() * 0.001f;
    settings.ampEnv.release = prm.synthRelease->load() * 0.001f;
    settings.filterEnv.release = settings.ampEnv.release;
    settings.glideMs = prm.glide->load();
    settings.pitchBendRange = prm.bendRange->load();

    dryGain.setTarget (prm.dryMix->load() * 0.01f);
    synthGain.setTarget (ap::dbToGain (prm.synthLevel->load()));
}

void Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();
    synthMidi.clear();
    updateParams();

    const int numSamples = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int channel = juce::jlimit (1, 16, (int) prm.channel->load());
    const bool sendMidi = prm.midiOut->load() > 0.5f;
    auto& tracker = trackers[(size_t) activeTracker];

    // Beat clock for this block.
    double bpm = 120.0, ppq = 0.0;
    bool playing = false;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = juce::jmax (1.0, *b);
            if (auto p = pos->getPpqPosition()) { ppq = *p; playing = pos->getIsPlaying(); }
        }
    }
    ap::BeatClock bc;
    bc.startSample = clock;
    bc.startBeat = playing ? ppq : freeRunBeats;
    bc.beatsPerSample = bpm / (60.0 * sr);
    const int64_t blockStart = clock;

    // Capture housekeeping (editor requests arrive through atomics).
    const bool capturingNow = capturing.load (std::memory_order_relaxed);
    if (clearRequested.exchange (false)) capture.clear();
    if (! capturingNow && capture.isNoteOpen())
        capture.noteOff (capture.openNoteNumber(), blockStart - latency, bc.beatAt (blockStart - latency));

    // Where quantised events go: the embedded synth and, optionally, the host.
    auto emitOut = [&] (const ap::TimedNoteEvent& e)
    {
        const int pos = (int) juce::jlimit<int64_t> (0, numSamples - 1, e.time - blockStart);
        switch (e.type)
        {
            case ap::TimedNoteEvent::Type::NoteOn:
                activeChannel = channel;
                synthMidi.addEvent (juce::MidiMessage::noteOn (1, e.note, (juce::uint8) e.velocity), pos);
                if (sendMidi) midi.addEvent (juce::MidiMessage::noteOn (channel, e.note, (juce::uint8) e.velocity), pos);
                soundingNote = e.note;
                break;
            case ap::TimedNoteEvent::Type::NoteOff:
                synthMidi.addEvent (juce::MidiMessage::noteOff (1, e.note), pos);
                if (sendMidi) midi.addEvent (juce::MidiMessage::noteOff (activeChannel, e.note), pos);
                if (soundingNote == e.note) soundingNote = -1;
                break;
            case ap::TimedNoteEvent::Type::PitchBend:
            {
                const int wheel = juce::jlimit (0, 16383, (int) std::lround (8192.0f + e.bend * 8191.0f));
                synthMidi.addEvent (juce::MidiMessage::pitchWheel (1, wheel), pos);
                if (sendMidi) midi.addEvent (juce::MidiMessage::pitchWheel (activeChannel, wheel), pos);
                break;
            }
        }
    };

    if (pendingAllNotesOff.exchange (false))
    {
        rhythm.flush (blockStart, emitOut);
        synthMidi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
        if (sendMidi) midi.addEvent (juce::MidiMessage::allNotesOff (activeChannel), 0);
        soundingNote = -1;
        if (capture.isNoteOpen())
            capture.noteOff (capture.openNoteNumber(), blockStart - latency, bc.beatAt (blockStart - latency));
    }

    // Detection, sample by sample, so events land at the right position.
    float inPeak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        const int64_t now = blockStart + i;
        rhythm.advance (now, emitOut);

        float s = 0.0f;
        for (int ch = 0; ch < numIn; ++ch)
        {
            const float x = buffer.getReadPointer (ch)[i];
            if (ch < 2) dryDelay[(size_t) ch].push (x);
            s += x;
        }
        s /= (float) juce::jmax (1, numIn);
        inPeak = juce::jmax (inPeak, std::fabs (s));

        tracker.process (&s, 1, [&] (const ap::PitchTracker::Frame& f)
        {
            detectedHz.store (f.voiced ? f.frequency : 0.0f, std::memory_order_relaxed);
            noteTracker.processFrame (f, [&] (const ap::NoteTracker::Event& e)
            {
                ap::TimedNoteEvent t;
                t.note = e.note;
                t.velocity = e.velocity;
                t.bend = e.bend;
                t.time = now;
                switch (e.type)
                {
                    case ap::NoteTracker::Event::Type::NoteOn:    t.type = ap::TimedNoteEvent::Type::NoteOn; break;
                    case ap::NoteTracker::Event::Type::NoteOff:   t.type = ap::TimedNoteEvent::Type::NoteOff; break;
                    case ap::NoteTracker::Event::Type::PitchBend: t.type = ap::TimedNoteEvent::Type::PitchBend; break;
                }

                if (capturingNow && t.type != ap::TimedNoteEvent::Type::PitchBend)
                {
                    const int64_t when = now - latency;    // line the take up with the singing
                    if (t.type == ap::TimedNoteEvent::Type::NoteOn)
                    {
                        if (capture.size() == 0 && ! capture.isNoteOpen())
                            captureBpm.store ((float) bpm, std::memory_order_relaxed);
                        capture.noteOn (t.note, t.velocity, when, bc.beatAt (when));
                    }
                    else
                        capture.noteOff (t.note, when, bc.beatAt (when));
                }

                rhythm.push (t, bc, emitOut);
            });
        });
    }
    if (numIn == 0) detectedHz.store (0.0f, std::memory_order_relaxed);
    inputLevel.store (inPeak, std::memory_order_relaxed);
    currentNote.store (soundingNote, std::memory_order_relaxed);
    playedHz.store (soundingNote >= 0 ? ap::midiToHz ((float) soundingNote) : 0.0f, std::memory_order_relaxed);

    // Synth, rendered straight into the output (the input is already in the dry delay line).
    buffer.clear();
    if (numOut > 0)
    {
        juce::AudioBuffer<float> outView (buffer.getArrayOfWritePointers(), juce::jmin (2, numOut), numSamples);
        synth.renderNextBlock (outView, synthMidi, 0, numSamples);
    }

    // Mix: synth level + delayed dry voice.
    float outPeak = 0.0f;
    const int dryChannels = juce::jmin (2, numIn);
    const int synthChannels = juce::jmin (2, numOut);
    for (int i = 0; i < numSamples; ++i)
    {
        const float dg = dryGain.next();
        const float sg = synthGain.next();
        for (int ch = 0; ch < numOut; ++ch)
        {
            const float dry = dryChannels > 0 ? dryDelay[(size_t) juce::jmin (ch, dryChannels - 1)].read (latency) : 0.0f;
            const float syn = ch < synthChannels ? buffer.getReadPointer (ch)[i] : buffer.getReadPointer (synthChannels - 1)[i];
            const float out = dry * dg + syn * sg;
            buffer.getWritePointer (ch)[i] = out;
            outPeak = juce::jmax (outPeak, std::fabs (out));
        }
    }
    outputLevel.store (outPeak, std::memory_order_relaxed);

    clock += numSamples;
    freeRunBeats = bc.beatAt (clock);
}

juce::MidiFile Processor::buildTakeMidi() const
{
    std::vector<ap::CapturedNote> notes;
    capture.snapshot (notes);
    ap::quantizeTake (notes, gridBeatsForChoice ((int) prm.grid->load()));

    juce::MidiFile file;
    file.setTicksPerQuarterNote (kTicksPerQuarter);
    juce::MidiMessageSequence seq;

    const double bpm = juce::jmax (1.0f, captureBpm.load());
    seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / bpm)), 0.0);
    seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);

    if (! notes.empty())
    {
        // Keep the phrase where it sat inside its bar.
        const double barStart = std::floor (notes.front().startBeat / 4.0) * 4.0;
        for (const auto& n : notes)
        {
            const double on  = juce::jmax (0.0, n.startBeat - barStart) * kTicksPerQuarter;
            const double off = juce::jmax (0.0, n.endBeat - barStart) * kTicksPerQuarter;
            seq.addEvent (juce::MidiMessage::noteOn (1, n.note, (juce::uint8) juce::jlimit (1, 127, n.velocity)), on);
            seq.addEvent (juce::MidiMessage::noteOff (1, n.note), juce::jmax (on + 1.0, off));
        }
    }
    seq.updateMatchedPairs();
    file.addTrack (seq);
    return file;
}

bool Processor::writeTakeToFile (const juce::File& file) const
{
    auto midiFile = buildTakeMidi();
    file.deleteFile();
    juce::FileOutputStream out (file);
    if (! out.openedOk()) return false;
    return midiFile.writeTo (out, 0);
}

juce::AudioProcessorEditor* Processor::createEditor()
{
    return new Editor (*this);
}

void Processor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void Processor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

} // namespace humline

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new humline::Processor();
}
