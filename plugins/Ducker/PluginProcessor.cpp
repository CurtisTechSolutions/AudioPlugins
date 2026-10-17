#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <ap_plug/Params.h>

namespace ducker
{

using namespace ap::plug;

namespace
{
    juce::StringArray divisionNames()
    {
        juce::StringArray a;
        for (int i = 0; i < (int) ap::NoteDivision::Count; ++i) a.add (ap::noteDivisionName ((ap::NoteDivision) i));
        return a;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout Processor::createLayout()
{
    Layout layout;
    layout.add (choiceParam (ids::mode, "Mode", { "External sidechain", "Internal", "Ghost (tempo)" }, 0));
    layout.add (dbParam (ids::threshold, "Threshold", -60.0f, 0.0f, -24.0f));
    layout.add (floatParam (ids::ratio, "Ratio", 1.0f, 30.0f, 6.0f, 6.0f, ":1", 1));
    layout.add (dbParam (ids::knee, "Knee", 0.0f, 18.0f, 6.0f));
    layout.add (msParam (ids::attack, "Attack", 0.1f, 100.0f, 2.0f));
    layout.add (msParam (ids::release, "Release", 5.0f, 1000.0f, 120.0f));
    layout.add (msParam (ids::lookahead, "Lookahead", 0.0f, kMaxLookaheadMs, 0.0f));
    layout.add (hzParam (ids::scHpf, "SC Low Cut", 20.0f, 1000.0f, 20.0f));
    layout.add (boolParam (ids::listen, "SC Listen", false));
    layout.add (dbParam (ids::makeup, "Makeup", 0.0f, 24.0f, 0.0f));
    layout.add (percentParam (ids::mix, "Mix", 100.0f));

    layout.add (choiceParam (ids::ghostDiv, "Division", divisionNames(), (int) ap::NoteDivision::Quarter));
    layout.add (dbParam (ids::ghostDepth, "Depth", 0.0f, 48.0f, 12.0f));
    layout.add (msParam (ids::ghostAttack, "G Attack", 1.0f, 100.0f, 5.0f));
    layout.add (msParam (ids::ghostHold, "G Hold", 0.0f, 500.0f, 40.0f));
    layout.add (msParam (ids::ghostRelease, "G Release", 10.0f, 2000.0f, 180.0f));
    layout.add (percentParam (ids::ghostCurve, "Curve", 50.0f));
    layout.add (percentParam (ids::ghostOffset, "Offset", 0.0f));
    layout.add (floatParam (ids::ghostBpm, "Free BPM", 40.0f, 240.0f, 120.0f, 0.0f, "bpm", 1));
    return layout;
}

Processor::Processor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                       .withInput ("Sidechain", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "DUCKER", createLayout())
{
    prm.mode = raw (apvts, ids::mode);           prm.threshold = raw (apvts, ids::threshold);
    prm.ratio = raw (apvts, ids::ratio);         prm.knee = raw (apvts, ids::knee);
    prm.attack = raw (apvts, ids::attack);       prm.release = raw (apvts, ids::release);
    prm.lookahead = raw (apvts, ids::lookahead); prm.scHpf = raw (apvts, ids::scHpf);
    prm.listen = raw (apvts, ids::listen);       prm.makeup = raw (apvts, ids::makeup);
    prm.mix = raw (apvts, ids::mix);
    prm.ghostDiv = raw (apvts, ids::ghostDiv);   prm.ghostDepth = raw (apvts, ids::ghostDepth);
    prm.ghostAttack = raw (apvts, ids::ghostAttack); prm.ghostHold = raw (apvts, ids::ghostHold);
    prm.ghostRelease = raw (apvts, ids::ghostRelease); prm.ghostCurve = raw (apvts, ids::ghostCurve);
    prm.ghostOffset = raw (apvts, ids::ghostOffset); prm.ghostBpm = raw (apvts, ids::ghostBpm);
}

void Processor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    detector.prepare (sampleRate);
    detector.setMode (ap::EnvelopeFollower::Mode::Peak);

    juce::dsp::ProcessSpec mono { sampleRate, (juce::uint32) samplesPerBlock, 1 };
    const int maxLookahead = (int) std::ceil (kMaxLookaheadMs * 0.001 * sampleRate) + 4;
    for (auto& d : lookaheadDelay)
    {
        d.prepare (mono);
        d.setMaximumDelayInSamples (maxLookahead);
        d.reset();
    }

    gainSmoother.prepare (sampleRate, 0.5f);
    gainSmoother.snap (1.0f);
    mixSmoother.prepare (sampleRate, 20.0f);
    mixSmoother.snap (prm.mix->load() * 0.01f);
    makeupSmoother.prepare (sampleRate, 20.0f);
    makeupSmoother.snap (ap::dbToGain (prm.makeup->load()));

    lookaheadSamples = -1;    // force the latency to be published
    reset();
    updateParams();
}

void Processor::reset()
{
    detector.reset();
    for (auto& f : scFilters) f.reset();
    for (auto& d : lookaheadDelay) d.reset();
    freeRunBeats = 0.0;
}

bool Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo()) return false;

    // The sidechain may be absent, mono or stereo.
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void Processor::updateParams()
{
    detector.setTimes (prm.attack->load(), prm.release->load());
    gc.thresholdDb = prm.threshold->load();
    gc.ratio = prm.ratio->load();
    gc.kneeDb = prm.knee->load();

    for (auto& f : scFilters)
        f.configure (ap::Biquad::Type::HighPass, sr, prm.scHpf->load(), 0.707f);

    const int la = (int) std::lround (prm.lookahead->load() * 0.001 * sr);
    if (la != lookaheadSamples)
    {
        lookaheadSamples = la;
        for (auto& d : lookaheadDelay) d.setDelay ((float) la);
        setLatencySamples (la);
    }

    ap::GhostDuckEnvelope::Shape shape;
    shape.attackMs = prm.ghostAttack->load();
    shape.holdMs = prm.ghostHold->load();
    shape.releaseMs = prm.ghostRelease->load();
    shape.depthDb = prm.ghostDepth->load();
    shape.curve = prm.ghostCurve->load() * 0.01f;
    ghost.setShape (shape);

    mixSmoother.setTarget (prm.mix->load() * 0.01f);
    makeupSmoother.setTarget (ap::dbToGain (prm.makeup->load()));
}

void Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    updateParams();

    const int numSamples = buffer.getNumSamples();
    auto main = getBusBuffer (buffer, true, 0);
    const int numCh = juce::jmin (kMaxChannels, main.getNumChannels());
    const auto mode = (Mode) (int) prm.mode->load();
    const bool listen = prm.listen->load() > 0.5f;

    // Sidechain bus: present only if the host enabled it.
    const bool haveSc = getBusCount (true) > 1 && getBus (true, 1)->isEnabled() && getChannelCountOfBus (true, 1) > 0;
    juce::AudioBuffer<float> sc;
    if (haveSc) sc = getBusBuffer (buffer, true, 1);
    sidechainConnected.store (haveSc, std::memory_order_relaxed);

    const bool useExternal = mode == Mode::External && haveSc;

    // Transport info for ghost mode.
    double bpm = prm.ghostBpm->load();
    double ppq = 0.0;
    bool   transportPlaying = false;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = juce::jmax (1.0, *b);
            if (auto p = pos->getPpqPosition()) { ppq = *p; transportPlaying = pos->getIsPlaying(); }
        }
    }
    const double beatsPerSample = bpm / (60.0 * sr);
    const auto division = (ap::NoteDivision) (int) prm.ghostDiv->load();
    const double offset = prm.ghostOffset->load() * 0.01;

    float minGain = 1.0f;
    float scPeak = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        // --- Detector input
        float det = 0.0f;
        if (mode != Mode::Ghost)
        {
            if (useExternal)
            {
                const int scCh = juce::jmin (kMaxChannels, sc.getNumChannels());
                for (int ch = 0; ch < scCh; ++ch)
                    det = juce::jmax (det, std::fabs (scFilters[(size_t) ch].process (sc.getReadPointer (ch)[i])));
            }
            else
            {
                for (int ch = 0; ch < numCh; ++ch)
                    det = juce::jmax (det, std::fabs (scFilters[(size_t) ch].process (main.getReadPointer (ch)[i])));
            }
            scPeak = juce::jmax (scPeak, det);
        }

        // --- Gain
        float targetGain;
        if (mode == Mode::Ghost)
        {
            const double beat = transportPlaying ? ppq + (double) i * beatsPerSample : freeRunBeats + (double) i * beatsPerSample;
            targetGain = ghost.gainAtBeat (beat, bpm, division, offset);
        }
        else
        {
            const float env = detector.process (det);
            targetGain = ap::dbToGain (gc.gainReductionDb (ap::gainToDb (env)));
        }
        gainSmoother.setTarget (targetGain);
        const float gain = gainSmoother.next();
        minGain = juce::jmin (minGain, gain);

        const float mix = mixSmoother.next();
        const float makeup = makeupSmoother.next();

        // --- Apply
        for (int ch = 0; ch < numCh; ++ch)
        {
            float* data = main.getWritePointer (ch);
            auto& delay = lookaheadDelay[(size_t) ch];
            delay.pushSample (0, data[i]);
            const float dry = delay.popSample (0);
            float out = dry * (1.0f + (gain * makeup - 1.0f) * mix);
            if (listen && useExternal)
                out = sc.getReadPointer (juce::jmin (ch, sc.getNumChannels() - 1))[i];
            data[i] = out;
        }
    }

    if (! transportPlaying)
        freeRunBeats += (double) numSamples * beatsPerSample;

    gainReductionDb.store (ap::gainToDb (minGain), std::memory_order_relaxed);
    sidechainLevel.store (scPeak, std::memory_order_relaxed);
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

} // namespace ducker

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ducker::Processor();
}
