#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

#include <ap_plug/Params.h>

namespace intone
{

using namespace ap::plug;

namespace
{
    constexpr int kHop = 128;
    constexpr float kShifterMinHz = 50.0f;

    struct TrackingPreset { float minHz, maxHz; int window; };
    constexpr TrackingPreset kPresets[3] = {
        { 50.0f, 900.0f, 2048 },    // Low: bass / baritone
        { 70.0f, 1400.0f, 1024 },   // Mid: most voices
        { 120.0f, 2200.0f, 640 },   // High: soprano / falsetto
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
        for (int i = 0; i < (int) ap::ScaleType::Count; ++i) a.add (ap::scaleName ((ap::ScaleType) i));
        return a;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout Processor::createLayout()
{
    Layout layout;
    layout.add (choiceParam (ids::key, "Key", keyNames(), 0));
    layout.add (choiceParam (ids::scale, "Scale", scaleNames(), (int) ap::ScaleType::Chromatic));
    layout.add (msParam (ids::speed, "Retune Speed", 0.0f, 400.0f, 20.0f));
    layout.add (percentParam (ids::amount, "Amount", 100.0f));
    layout.add (intParam (ids::transpose, "Transpose", -12, 12, 0, "st"));
    layout.add (choiceParam (ids::tracking, "Voice Range", { "Low", "Mid", "High" }, 1));
    layout.add (percentParam (ids::sensitivity, "Sensitivity", 50.0f));
    layout.add (percentParam (ids::mix, "Mix", 100.0f));

    for (int pc = 0; pc < 12; ++pc)
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID (ids::note (pc), 1),
                                                                juce::String (ap::noteName (pc)), true));

    layout.add (percentParam (ids::deessAmount, "De-ess", 0.0f));
    layout.add (hzParam (ids::deessFreq, "De-ess Freq", 3000.0f, 12000.0f, 6500.0f));
    layout.add (dbParam (ids::compThresh, "Threshold", -50.0f, 0.0f, -18.0f));
    layout.add (floatParam (ids::compRatio, "Ratio", 1.0f, 20.0f, 3.0f, 4.0f, ":1", 1));
    layout.add (msParam (ids::compAttack, "Attack", 0.5f, 100.0f, 10.0f));
    layout.add (msParam (ids::compRelease, "Release", 10.0f, 1000.0f, 120.0f));
    layout.add (dbParam (ids::compMakeup, "Makeup", 0.0f, 24.0f, 0.0f));

    layout.add (hzParam (ids::lowCut, "Low Cut", 20.0f, 400.0f, 80.0f));
    layout.add (dbParam (ids::body, "Body", -12.0f, 12.0f, 0.0f));
    layout.add (dbParam (ids::presence, "Presence", -12.0f, 12.0f, 0.0f));
    layout.add (dbParam (ids::air, "Air", -12.0f, 12.0f, 0.0f));
    layout.add (dbParam (ids::output, "Output", -24.0f, 24.0f, 0.0f));
    return layout;
}

Processor::Processor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "INTONE", createLayout()),
      programs (*this, factoryPresets())
{
    prm.key = raw (apvts, ids::key);             prm.scaleType = raw (apvts, ids::scale);
    prm.speed = raw (apvts, ids::speed);         prm.amount = raw (apvts, ids::amount);
    prm.transpose = raw (apvts, ids::transpose); prm.tracking = raw (apvts, ids::tracking);
    prm.sensitivity = raw (apvts, ids::sensitivity); prm.mix = raw (apvts, ids::mix);
    prm.deessAmount = raw (apvts, ids::deessAmount); prm.deessFreq = raw (apvts, ids::deessFreq);
    prm.compThresh = raw (apvts, ids::compThresh); prm.compRatio = raw (apvts, ids::compRatio);
    prm.compAttack = raw (apvts, ids::compAttack); prm.compRelease = raw (apvts, ids::compRelease);
    prm.compMakeup = raw (apvts, ids::compMakeup);
    prm.lowCut = raw (apvts, ids::lowCut);       prm.body = raw (apvts, ids::body);
    prm.presence = raw (apvts, ids::presence);   prm.air = raw (apvts, ids::air);
    prm.output = raw (apvts, ids::output);
    for (int pc = 0; pc < 12; ++pc)
        prm.notes[(size_t) pc] = apvts.getRawParameterValue (ids::note (pc));
}

void Processor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    for (int i = 0; i < 3; ++i)
        trackers[(size_t) i].prepare (sampleRate, kHop, kPresets[i].window, kPresets[i].minHz, kPresets[i].maxHz);

    for (auto& s : shifters) s.prepare (sampleRate, kShifterMinHz, juce::jmax (samplesPerBlock, kControlInterval));
    latency = shifters[0].latencySamples();

    juce::dsp::ProcessSpec mono { sampleRate, (juce::uint32) samplesPerBlock, 1 };
    for (auto& d : dryDelay)
    {
        d.prepare (mono);
        d.setMaximumDelayInSamples (latency + 8);
        d.setDelay ((float) latency);
        d.reset();
    }
    setLatencySamples (latency);

    for (auto& c : chains)
    {
        c.deessEnv.prepare (sampleRate);
        c.deessEnv.setMode (ap::EnvelopeFollower::Mode::Peak);
        c.deessEnv.setTimes (1.0f, 40.0f);
        c.compEnv.prepare (sampleRate);
        c.compEnv.setMode (ap::EnvelopeFollower::Mode::Rms);
    }
    outputSmoother.prepare (sampleRate, 20.0f);
    outputSmoother.snap (ap::dbToGain (prm.output->load()));
    mixSmoother.prepare (sampleRate, 20.0f);
    mixSmoother.snap (prm.mix->load() * 0.01f);

    reset();
    updateParams();
}

void Processor::reset()
{
    timeline.clear();
    for (auto& t : trackers) t.resync (0);     // keep the frame clock aligned with the shifters' clock
    for (auto& s : shifters) s.reset();
    for (auto& c : chains)
    {
        c.lowCut.reset(); c.deessSplit.reset(); c.body.reset(); c.presence.reset(); c.air.reset();
        c.deessEnv.reset(); c.compEnv.reset();
    }
    haveTarget = false;
    controlCounter = 0;
}

bool Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo()) return false;
    return in == out;
}

void Processor::updateParams()
{
    const int wanted = juce::jlimit (0, 2, (int) prm.tracking->load());
    if (wanted != activeTracker)
    {
        // Hand the clock over so timeline lookups stay aligned after a range switch.
        trackers[(size_t) wanted].resync (trackers[(size_t) activeTracker].currentTime());
        activeTracker = wanted;
    }
    const float sens = prm.sensitivity->load() * 0.01f;
    for (auto& t : trackers) t.setThreshold (ap::lerp (0.08f, 0.35f, sens));

    uint16_t custom = 0;
    for (int pc = 0; pc < 12; ++pc)
        if (prm.notes[(size_t) pc]->load() > 0.5f) custom = (uint16_t) (custom | (1u << pc));
    // The custom mask is absolute (C..B); rotate so it is relative to the root the Scale expects.
    const int root = (int) prm.key->load();
    const uint16_t rotated = (uint16_t) (((custom >> root) | (custom << (12 - root))) & 0x0FFF);
    scale = ap::Scale::fromType ((ap::ScaleType) (int) prm.scaleType->load(), root, rotated);

    const float speedMs = prm.speed->load();
    retuneCoeff = speedMs <= 0.0f ? 0.0f
                                  : (float) std::exp (-(double) kControlInterval / (0.001 * speedMs * sr));

    compGc.thresholdDb = prm.compThresh->load();
    compGc.ratio = prm.compRatio->load();
    compGc.kneeDb = 6.0f;
    const float deess = prm.deessAmount->load() * 0.01f;
    deessGc.thresholdDb = ap::lerp (0.0f, -50.0f, deess);
    deessGc.ratio = 6.0f;
    deessGc.kneeDb = 6.0f;

    for (auto& c : chains)
        c.compEnv.setTimes (prm.compAttack->load(), prm.compRelease->load());

    for (int ch = 0; ch < kMaxChannels; ++ch)
        updateChannelFilters (ch);
}

void Processor::updateChannelFilters (int ch)
{
    auto& c = chains[(size_t) ch];
    c.lowCut.configure (ap::Biquad::Type::HighPass, sr, prm.lowCut->load(), 0.707f);
    c.deessSplit.configure (ap::Biquad::Type::HighPass, sr, prm.deessFreq->load(), 0.707f);
    c.body.configure (ap::Biquad::Type::Peak, sr, 250.0f, 1.0f, prm.body->load());
    c.presence.configure (ap::Biquad::Type::Peak, sr, 3200.0f, 1.2f, prm.presence->load());
    c.air.configure (ap::Biquad::Type::HighShelf, sr, 11000.0f, 0.707f, prm.air->load());
}

void Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    updateParams();

    const int numSamples = buffer.getNumSamples();
    const int numCh = juce::jmin (kMaxChannels, buffer.getNumChannels());
    const int transpose = (int) prm.transpose->load();
    const float amount = prm.amount->load() * 0.01f;
    auto& tracker = trackers[(size_t) activeTracker];

    mixSmoother.setTarget (prm.mix->load() * 0.01f);
    outputSmoother.setTarget (ap::dbToGain (prm.output->load()));

    float maxGr = 0.0f;
    float peak = 0.0f;
    bool  sawVoiced = false;
    float lastDetected = 0.0f;

    for (int start = 0; start < numSamples; start += kControlInterval)
    {
        const int n = juce::jmin (kControlInterval, numSamples - start);

        // 1) Detection on the mono sum of this sub-block.
        for (int i = 0; i < n; ++i)
        {
            float s = 0.0f;
            for (int ch = 0; ch < numCh; ++ch) s += buffer.getReadPointer (ch)[start + i];
            s /= (float) juce::jmax (1, numCh);
            tracker.process (&s, 1, [&] (const ap::PitchTracker::Frame& f) { timeline.push (f); });
        }

        // 2) Control: look up the estimate for the audio the shifters are about to output.
        const auto* f = timeline.at (shifters[0].outputTime());
        if (f != nullptr && f->voiced && f->frequency > 0.0f)
        {
            const float detMidi = f->midi;
            const float target = (float) scale.nearest (detMidi + (float) transpose);
            if (! haveTarget)
            {
                smoothedTargetMidi = detMidi;      // start where the singer is, then pull in
                haveTarget = true;
            }
            smoothedTargetMidi = target + retuneCoeff * (smoothedTargetMidi - target);
            const float corrected = detMidi + amount * (smoothedTargetMidi - detMidi);
            const float ratio = ap::semitonesToRatio (corrected - detMidi);
            const float period = (float) (sr / (double) f->frequency);
            for (int ch = 0; ch < numCh; ++ch)
            {
                shifters[(size_t) ch].setPeriod (period);
                shifters[(size_t) ch].setRatio (ratio);
            }
            sawVoiced = true;
            lastDetected = f->frequency;
            targetHz.store (ap::midiToHz (smoothedTargetMidi), std::memory_order_relaxed);
        }
        else
        {
            for (int ch = 0; ch < numCh; ++ch) shifters[(size_t) ch].setPeriod (0.0f);
            haveTarget = false;
        }

        // 3) Shift, then the vocal chain, then mix.
        for (int ch = 0; ch < numCh; ++ch)
        {
            float* data = buffer.getWritePointer (ch) + start;
            auto& chain = chains[(size_t) ch];
            auto& dry = dryDelay[(size_t) ch];

            float wet[kControlInterval];
            shifters[(size_t) ch].process (data, wet, n);

            for (int i = 0; i < n; ++i)
            {
                const float dryS = dry.popSample (0);
                dry.pushSample (0, data[i]);

                float x = chain.lowCut.process (wet[i]);

                // De-esser: duck the band above the split frequency when it gets harsh.
                const float high = chain.deessSplit.process (x);
                const float low = x - high;
                const float hDb = ap::gainToDb (chain.deessEnv.process (high));
                const float dGr = deessGc.gainReductionDb (hDb);
                x = low + high * ap::dbToGain (dGr);

                // Compressor.
                const float lvlDb = ap::gainToDb (chain.compEnv.process (x));
                const float gr = compGc.gainReductionDb (lvlDb);
                x *= ap::dbToGain (gr + prm.compMakeup->load());
                if (ch == 0) maxGr = juce::jmin (maxGr, gr);

                // Tone.
                x = chain.body.process (x);
                x = chain.presence.process (x);
                x = chain.air.process (x);

                const float mix = ch == 0 ? mixSmoother.next() : mixSmoother.getCurrent();
                const float og = ch == 0 ? outputSmoother.next() : outputSmoother.getCurrent();
                const float out = (dryS + (x - dryS) * mix) * og;
                data[i] = out;
                peak = juce::jmax (peak, std::fabs (out));
            }
        }
    }

    // Stereo output from a mono input: copy.
    for (int ch = numCh; ch < buffer.getNumChannels(); ++ch)
        buffer.copyFrom (ch, 0, buffer, 0, 0, numSamples);

    detectedHz.store (sawVoiced ? lastDetected : 0.0f, std::memory_order_relaxed);
    if (! sawVoiced) targetHz.store (0.0f, std::memory_order_relaxed);
    gainReductionDb.store (maxGr, std::memory_order_relaxed);
    outputLevel.store (peak, std::memory_order_relaxed);
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

} // namespace intone

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new intone::Processor();
}
