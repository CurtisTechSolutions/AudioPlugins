#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <ap_plug/Params.h>

namespace frisson
{

using namespace ap::plug;

juce::AudioProcessorValueTreeState::ParameterLayout Processor::createLayout()
{
    Layout layout;
    layout.add (percentParam (ids::dose,   "Dose",   40.0f));
    layout.add (percentParam (ids::weight, "Weight", 25.0f));
    layout.add (percentParam (ids::warmth, "Warmth", 20.0f));
    layout.add (percentParam (ids::focus,  "Focus",  30.0f));
    layout.add (percentParam (ids::groove, "Groove", 15.0f));
    layout.add (percentParam (ids::shine,  "Shine",  35.0f));
    layout.add (percentParam (ids::space,  "Space",  20.0f));
    layout.add (percentParam (ids::rush,   "Rush",   25.0f));
    layout.add (percentParam (ids::chills, "Chills", 30.0f));
    layout.add (percentParam (ids::life,   "Life",   20.0f));
    layout.add (boolParam (ids::match, "Match", false));
    layout.add (dbParam (ids::output, "Output", -12.0f, 12.0f, 0.0f));

    layout.add (percentParam (ids::chillsSense, "Chills Sensitivity", 50.0f));
    layout.add (boolParam (ids::groovePocket, "Groove Pocket", true));
    layout.add (floatParam (ids::lifeRate, "Life Rate", 0.5f, 2.0f, 1.0f, 1.0f, "x", 2));
    layout.add (hzParam (ids::spaceMonoHz, "Space Mono Below", 60.0f, 300.0f, 120.0f));
    layout.add (percentParam (ids::spaceRoom, "Space Room", 30.0f));
    layout.add (dbParam (ids::rushCeiling, "Rush Ceiling", -3.0f, 0.0f, -0.3f));
    layout.add (boolParam (ids::rushAutoGain, "Rush Auto Gain", true));
    return layout;
}

Processor::Processor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "FRISSON", createLayout())
{
    prm.dose = raw (apvts, ids::dose);     prm.weight = raw (apvts, ids::weight); prm.warmth = raw (apvts, ids::warmth);
    prm.focus = raw (apvts, ids::focus);   prm.groove = raw (apvts, ids::groove); prm.shine = raw (apvts, ids::shine);
    prm.space = raw (apvts, ids::space);   prm.rush = raw (apvts, ids::rush);     prm.chills = raw (apvts, ids::chills);
    prm.life = raw (apvts, ids::life);     prm.match = raw (apvts, ids::match);   prm.output = raw (apvts, ids::output);
    prm.chillsSense = raw (apvts, ids::chillsSense);   prm.groovePocket = raw (apvts, ids::groovePocket);
    prm.lifeRate = raw (apvts, ids::lifeRate);         prm.spaceMonoHz = raw (apvts, ids::spaceMonoHz);
    prm.spaceRoom = raw (apvts, ids::spaceRoom);       prm.rushCeiling = raw (apvts, ids::rushCeiling);
    prm.rushAutoGain = raw (apvts, ids::rushAutoGain);
    for (auto& a : activity) a.store (0.0f);
}

void Processor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    maxBlock = juce::jmax (16, samplesPerBlock);

    for (auto* m : chain) m->prepare (sampleRate, maxBlock);
    chills.prepare (sampleRate, maxBlock);
    life.prepare (sampleRate, maxBlock);

    latency = 0;
    for (auto* m : chain) latency += m->latencySamples();
    setLatencySamples (latency);

    for (auto& d : dryDelay) d.prepare (latency + 1);
    dryL.assign ((size_t) maxBlock * 2, 0.0f);
    dryR.assign ((size_t) maxBlock * 2, 0.0f);
    scratchR.assign ((size_t) maxBlock * 2, 0.0f);

    dryMeter.prepare (sampleRate, 2);
    wetMeter.prepare (sampleRate, 2);

    doseSmoother.prepare (sampleRate, 30.0f);
    doseSmoother.snap (prm.dose->load() * 0.01f);
    outputSmoother.prepare (sampleRate, 20.0f);
    outputSmoother.snap (ap::dbToGain (prm.output->load()));
    gainLSmoother.prepare (sampleRate, 50.0f);  gainLSmoother.snap (1.0f);
    gainRSmoother.prepare (sampleRate, 50.0f);  gainRSmoother.snap (1.0f);
    matchSmoother.prepare (sampleRate, 500.0f); matchSmoother.snap (1.0f);

    reset();
    updateModuleSettings();
}

void Processor::reset()
{
    for (auto* m : chain) m->reset();
    chills.reset();
    life.reset();
    for (auto& d : dryDelay) d.reset();
    dryMeter.reset();
    wetMeter.reset();
}

bool Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void Processor::updateModuleSettings()
{
    chills.setSensitivity (prm.chillsSense->load() * 0.01f);
    groove.setPocketEnabled (prm.groovePocket->load() > 0.5f);
    life.setRate (prm.lifeRate->load());
    space.setMonoBelowHz (prm.spaceMonoHz->load());
    space.setRoom (prm.spaceRoom->load() * 0.01f);
    rush.setCeilingDb (prm.rushCeiling->load());
    rush.setAutoGain (prm.rushAutoGain->load() > 0.5f);
}

void Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();
    if (n == 0 || numCh == 0) return;
    if ((int) dryL.size() < n) return;   // host exceeded its promised block size; skip rather than allocate

    updateModuleSettings();

    float* left  = buffer.getWritePointer (0);
    float* right = numCh > 1 ? buffer.getWritePointer (1) : scratchR.data();
    if (numCh == 1) std::copy (left, left + n, right);

    // Dry copy (un-delayed, for the modulators) and the latency-matched dry path.
    std::copy (left,  left + n,  dryL.data());
    std::copy (right, right + n, dryR.data());
    dryMeter.process (dryL.data(), dryR.data(), n);

    // Transport.
    Transport t;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) { t.bpm = *b; t.hasTempo = true; }
            if (auto p = pos->getPpqPosition()) t.ppqPosition = *p;
            t.playing = pos->getIsPlaying();
        }

    // Dose and per-knob depth.
    const float dose = prm.dose->load() * 0.01f;
    doseSmoother.setTarget (dose);
    auto depth = [dose] (std::atomic<float>* p) { return p->load() * 0.01f * dose; };

    // Modulators read the dry input.
    chills.setAmount (depth (prm.chills));
    chills.setTransport (t);
    life.setAmount (depth (prm.life));
    const Modulation mod = chills.analyze (dryL.data(), dryR.data(), n).combined (life.analyze (dryL.data(), dryR.data(), n));

    // Ingredients in the fixed chain order.
    weight.setAmount (depth (prm.weight));
    warmth.setAmount (depth (prm.warmth));
    focus.setAmount  (depth (prm.focus));
    groove.setAmount (depth (prm.groove));
    shine.setAmount  (depth (prm.shine));
    space.setAmount  (depth (prm.space));
    rush.setAmount   (depth (prm.rush));
    for (auto* m : chain)
    {
        m->setModulation (mod);
        m->setTransport (t);
        m->process (left, right, n);
    }

    // Per-channel trims from LIFE, output gain, wet metering.
    gainLSmoother.setTarget (mod.gainL);
    gainRSmoother.setTarget (mod.gainR);
    outputSmoother.setTarget (ap::dbToGain (prm.output->load()));
    for (int i = 0; i < n; ++i)
    {
        const float og = outputSmoother.next();
        left[i]  *= gainLSmoother.next() * og;
        right[i] *= gainRSmoother.next() * og;
    }
    wetMeter.process (left, right, n);

    // Loudness bookkeeping for MATCH and the readout.
    const bool haveLoudness = dryMeter.hasShortTerm() && wetMeter.hasShortTerm();
    const float dLufs = dryMeter.shortTermLufs(), wLufs = wetMeter.shortTermLufs();
    dryLufs.store (dLufs, std::memory_order_relaxed);
    wetLufs.store (wLufs, std::memory_order_relaxed);
    const float deltaDb = (haveLoudness && dLufs > -100.0f && wLufs > -100.0f) ? juce::jlimit (-24.0f, 24.0f, wLufs - dLufs) : 0.0f;
    loudnessDeltaDb.store (deltaDb, std::memory_order_relaxed);

    const bool match = prm.match->load() > 0.5f;
    matchSmoother.setTarget (match ? ap::dbToGain (deltaDb) : 1.0f);
    matchGainDb.store (ap::gainToDb (matchSmoother.getCurrent()), std::memory_order_relaxed);

    // Blend with the latency-aligned dry signal; MATCH replaces the output with
    // the dry signal raised to the processed loudness.
    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float dl = dryDelay[0].process (dryL[(size_t) i], latency);
        const float dr = dryDelay[1].process (dryR[(size_t) i], latency);
        const float d = doseSmoother.next();
        const float mg = matchSmoother.next();
        float ol = dl + (left[i]  - dl) * d;
        float orr = dr + (right[i] - dr) * d;
        if (match)
        {
            ol = dl * mg;
            orr = dr * mg;
        }
        left[i] = ol;
        right[i] = orr;
        peak = juce::jmax (peak, std::fabs (ol), std::fabs (orr));
    }
    outputPeak.store (peak, std::memory_order_relaxed);

    // Activity for the editor rings.
    activity[kWeight].store (weight.activity(), std::memory_order_relaxed);
    activity[kWarmth].store (warmth.activity(), std::memory_order_relaxed);
    activity[kRush].store   (rush.activity(),   std::memory_order_relaxed);
    activity[kFocus].store  (focus.activity(),  std::memory_order_relaxed);
    activity[kShine].store  (shine.activity(),  std::memory_order_relaxed);
    activity[kSpace].store  (space.activity(),  std::memory_order_relaxed);
    activity[kGroove].store (groove.activity(), std::memory_order_relaxed);
    activity[kChills].store (chills.activity(), std::memory_order_relaxed);
    activity[kLife].store   (life.activity(),   std::memory_order_relaxed);
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

} // namespace frisson

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new frisson::Processor();
}
