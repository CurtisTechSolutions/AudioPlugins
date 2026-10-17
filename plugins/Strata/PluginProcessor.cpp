#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <ap_plug/Params.h>

namespace strata
{

using namespace ap::plug;

namespace
{
    juce::StringArray waveNames()
    {
        juce::StringArray a;
        for (int i = 0; i < (int) ap::Waveform::Count; ++i) a.add (ap::waveformName ((ap::Waveform) i));
        return a;
    }

    juce::StringArray lfoShapeNames()
    {
        juce::StringArray a;
        for (int i = 0; i < (int) ap::Lfo::Shape::Count; ++i) a.add (ap::Lfo::shapeName ((ap::Lfo::Shape) i));
        return a;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout Processor::createLayout()
{
    Layout layout;

    // Oscillator A
    layout.add (choiceParam (ids::aWave, "Wave A", waveNames(), (int) ap::Waveform::Saw));
    layout.add (intParam (ids::aOct, "Octave A", -2, 2, 0));
    layout.add (intParam (ids::aSemi, "Semi A", -12, 12, 0));
    layout.add (floatParam (ids::aFine, "Fine A", -100.0f, 100.0f, 0.0f, 0.0f, "ct", 0));
    layout.add (percentParam (ids::aLevel, "Level A", 80.0f));
    layout.add (percentParam (ids::aPw, "Width A", 50.0f));
    layout.add (intParam (ids::uniVoices, "Unison", 1, 4, 1));
    layout.add (floatParam (ids::uniDetune, "Detune", 0.0f, 100.0f, 12.0f, 0.0f, "ct", 0));
    layout.add (percentParam (ids::uniSpread, "Spread", 50.0f));

    // Oscillator B
    layout.add (choiceParam (ids::bWave, "Wave B", waveNames(), (int) ap::Waveform::Square));
    layout.add (intParam (ids::bOct, "Octave B", -2, 2, 0));
    layout.add (intParam (ids::bSemi, "Semi B", -12, 12, 0));
    layout.add (floatParam (ids::bFine, "Fine B", -100.0f, 100.0f, 7.0f, 0.0f, "ct", 0));
    layout.add (percentParam (ids::bLevel, "Level B", 40.0f));
    layout.add (percentParam (ids::bPw, "Width B", 50.0f));

    // Sub & noise
    layout.add (percentParam (ids::subLevel, "Sub Level", 0.0f));
    layout.add (choiceParam (ids::subWave, "Sub Wave", { "Sine", "Square" }, 0));
    layout.add (choiceParam (ids::subOct, "Sub Octave", { "-1", "-2" }, 0));
    layout.add (percentParam (ids::noiseLevel, "Noise", 0.0f));

    // Filter
    layout.add (choiceParam (ids::fType, "Filter Type", { "Low-pass", "Band-pass", "High-pass" }, 0));
    layout.add (hzParam (ids::fCutoff, "Cutoff", 20.0f, 20000.0f, 4000.0f));
    layout.add (percentParam (ids::fRes, "Resonance", 20.0f));
    layout.add (floatParam (ids::fEnvAmt, "Env Amount", -100.0f, 100.0f, 40.0f, 0.0f, "%", 0));
    layout.add (percentParam (ids::fKey, "Key Track", 50.0f));
    layout.add (floatParam (ids::fLfo, "LFO 2 Amount", -100.0f, 100.0f, 0.0f, 0.0f, "%", 0));
    layout.add (percentParam (ids::fDrive, "Drive", 0.0f));

    // Envelopes
    layout.add (msParam (ids::ampA, "Attack", 1.0f, 5000.0f, 5.0f));
    layout.add (msParam (ids::ampD, "Decay", 1.0f, 5000.0f, 200.0f));
    layout.add (percentParam (ids::ampS, "Sustain", 80.0f));
    layout.add (msParam (ids::ampR, "Release", 1.0f, 8000.0f, 300.0f));
    layout.add (msParam (ids::fenvA, "F Attack", 1.0f, 5000.0f, 5.0f));
    layout.add (msParam (ids::fenvD, "F Decay", 1.0f, 5000.0f, 300.0f));
    layout.add (percentParam (ids::fenvS, "F Sustain", 20.0f));
    layout.add (msParam (ids::fenvR, "F Release", 1.0f, 8000.0f, 300.0f));

    // LFOs
    layout.add (choiceParam (ids::lfo1Shape, "LFO 1 Shape", lfoShapeNames(), 0));
    layout.add (floatParam (ids::lfo1Rate, "LFO 1 Rate", 0.05f, 20.0f, 5.0f, 2.0f, "Hz", 2));
    layout.add (floatParam (ids::lfo1Depth, "Vibrato", 0.0f, 100.0f, 0.0f, 0.0f, "ct", 0));
    layout.add (msParam (ids::lfo1Fade, "Vib Fade", 0.0f, 3000.0f, 0.0f));
    layout.add (choiceParam (ids::lfo2Shape, "LFO 2 Shape", lfoShapeNames(), 0));
    layout.add (floatParam (ids::lfo2Rate, "LFO 2 Rate", 0.05f, 20.0f, 1.0f, 2.0f, "Hz", 2));

    // Global
    layout.add (intParam (ids::voices, "Voices", 1, 16, 8));
    layout.add (msParam (ids::glide, "Glide", 0.0f, 2000.0f, 0.0f));
    layout.add (percentParam (ids::velAmp, "Vel > Amp", 70.0f));
    layout.add (percentParam (ids::velFilter, "Vel > Filter", 50.0f));
    layout.add (intParam (ids::bendRange, "Bend Range", 0, 24, 2, "st"));
    layout.add (dbParam (ids::master, "Master", -60.0f, 12.0f, -6.0f));

    // Effects
    layout.add (percentParam (ids::chorusMix, "Chorus", 0.0f));
    layout.add (floatParam (ids::chorusRate, "Chorus Rate", 0.1f, 5.0f, 0.8f, 1.0f, "Hz", 2));
    layout.add (msParam (ids::delayTime, "Delay Time", 10.0f, 2000.0f, 375.0f));
    layout.add (percentParam (ids::delayFb, "Feedback", 35.0f));
    layout.add (percentParam (ids::delayMix, "Delay", 0.0f));
    layout.add (percentParam (ids::reverbSize, "Room Size", 50.0f));
    layout.add (percentParam (ids::reverbMix, "Reverb", 0.0f));

    return layout;
}

Processor::Processor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STRATA", createLayout())
{
    prm.aWave = ap::plug::raw (apvts, ids::aWave); prm.aOct = ap::plug::raw (apvts, ids::aOct); prm.aSemi = ap::plug::raw (apvts, ids::aSemi);
    prm.aFine = ap::plug::raw (apvts, ids::aFine); prm.aLevel = ap::plug::raw (apvts, ids::aLevel); prm.aPw = ap::plug::raw (apvts, ids::aPw);
    prm.uniVoices = ap::plug::raw (apvts, ids::uniVoices); prm.uniDetune = ap::plug::raw (apvts, ids::uniDetune); prm.uniSpread = ap::plug::raw (apvts, ids::uniSpread);
    prm.bWave = ap::plug::raw (apvts, ids::bWave); prm.bOct = ap::plug::raw (apvts, ids::bOct); prm.bSemi = ap::plug::raw (apvts, ids::bSemi);
    prm.bFine = ap::plug::raw (apvts, ids::bFine); prm.bLevel = ap::plug::raw (apvts, ids::bLevel); prm.bPw = ap::plug::raw (apvts, ids::bPw);
    prm.subLevel = ap::plug::raw (apvts, ids::subLevel); prm.subWave = ap::plug::raw (apvts, ids::subWave); prm.subOct = ap::plug::raw (apvts, ids::subOct);
    prm.noiseLevel = ap::plug::raw (apvts, ids::noiseLevel);
    prm.fType = ap::plug::raw (apvts, ids::fType); prm.fCutoff = ap::plug::raw (apvts, ids::fCutoff); prm.fRes = ap::plug::raw (apvts, ids::fRes);
    prm.fEnvAmt = ap::plug::raw (apvts, ids::fEnvAmt); prm.fKey = ap::plug::raw (apvts, ids::fKey); prm.fLfo = ap::plug::raw (apvts, ids::fLfo);
    prm.fDrive = ap::plug::raw (apvts, ids::fDrive);
    prm.ampA = ap::plug::raw (apvts, ids::ampA); prm.ampD = ap::plug::raw (apvts, ids::ampD); prm.ampS = ap::plug::raw (apvts, ids::ampS); prm.ampR = ap::plug::raw (apvts, ids::ampR);
    prm.fenvA = ap::plug::raw (apvts, ids::fenvA); prm.fenvD = ap::plug::raw (apvts, ids::fenvD); prm.fenvS = ap::plug::raw (apvts, ids::fenvS); prm.fenvR = ap::plug::raw (apvts, ids::fenvR);
    prm.lfo1Shape = ap::plug::raw (apvts, ids::lfo1Shape); prm.lfo1Rate = ap::plug::raw (apvts, ids::lfo1Rate);
    prm.lfo1Depth = ap::plug::raw (apvts, ids::lfo1Depth); prm.lfo1Fade = ap::plug::raw (apvts, ids::lfo1Fade);
    prm.lfo2Shape = ap::plug::raw (apvts, ids::lfo2Shape); prm.lfo2Rate = ap::plug::raw (apvts, ids::lfo2Rate);
    prm.voices = ap::plug::raw (apvts, ids::voices); prm.glide = ap::plug::raw (apvts, ids::glide);
    prm.velAmp = ap::plug::raw (apvts, ids::velAmp); prm.velFilter = ap::plug::raw (apvts, ids::velFilter);
    prm.bendRange = ap::plug::raw (apvts, ids::bendRange); prm.master = ap::plug::raw (apvts, ids::master);
    prm.chorusMix = ap::plug::raw (apvts, ids::chorusMix); prm.chorusRate = ap::plug::raw (apvts, ids::chorusRate);
    prm.delayTime = ap::plug::raw (apvts, ids::delayTime); prm.delayFb = ap::plug::raw (apvts, ids::delayFb); prm.delayMix = ap::plug::raw (apvts, ids::delayMix);
    prm.reverbSize = ap::plug::raw (apvts, ids::reverbSize); prm.reverbMix = ap::plug::raw (apvts, ids::reverbMix);

    for (int i = 0; i < 16; ++i)
    {
        auto* v = new Voice (settings);
        v->setGlideSource (&lastNoteHz);
        synth.addVoice (v);
    }
    synth.addSound (new Sound());
    synth.setNoteStealingEnabled (true);
}

void Processor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    synth.setCurrentPlaybackSampleRate (sampleRate);
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* v = dynamic_cast<Voice*> (synth.getVoice (i)))
            v->prepare (sampleRate, samplesPerBlock);

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 2 };
    chorus.prepare (spec);
    chorus.setCentreDelay (12.0f);
    chorus.setDepth (0.35f);
    chorus.setFeedback (0.1f);

    juce::dsp::ProcessSpec mono { sampleRate, (juce::uint32) samplesPerBlock, 1 };
    delayL.prepare (mono);
    delayR.prepare (mono);
    delayL.setMaximumDelayInSamples ((int) (sampleRate * 2.1));
    delayR.setMaximumDelayInSamples ((int) (sampleRate * 2.1));
    delayL.reset();
    delayR.reset();
    delaySmoother.prepare (sampleRate, 80.0f);
    delaySmoother.snap ((float) (sampleRate * prm.delayTime->load() * 0.001));

    reverb.prepare (spec);
    masterSmoother.prepare (sampleRate, 20.0f);
    masterSmoother.snap (ap::dbToGain (prm.master->load()));

    fxBuffer.setSize (2, samplesPerBlock);
    updateSettings();
}

bool Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void Processor::updateSettings()
{
    auto pct = [] (std::atomic<float>* p) { return p->load() * 0.01f; };

    settings.oscA.wave = (ap::Waveform) (int) prm.aWave->load();
    settings.oscA.octave = (int) prm.aOct->load();
    settings.oscA.semi = (int) prm.aSemi->load();
    settings.oscA.fineCents = prm.aFine->load();
    settings.oscA.level = pct (prm.aLevel);
    settings.oscA.pulseWidth = pct (prm.aPw);
    settings.unisonVoices = (int) prm.uniVoices->load();
    settings.unisonDetuneCents = prm.uniDetune->load();
    settings.unisonSpread = pct (prm.uniSpread);

    settings.oscB.wave = (ap::Waveform) (int) prm.bWave->load();
    settings.oscB.octave = (int) prm.bOct->load();
    settings.oscB.semi = (int) prm.bSemi->load();
    settings.oscB.fineCents = prm.bFine->load();
    settings.oscB.level = pct (prm.bLevel);
    settings.oscB.pulseWidth = pct (prm.bPw);

    settings.subLevel = pct (prm.subLevel);
    settings.subSquare = prm.subWave->load() > 0.5f;
    settings.subOctave = prm.subOct->load() > 0.5f ? -2 : -1;
    settings.noiseLevel = pct (prm.noiseLevel) * 0.5f;

    settings.filterType = (int) prm.fType->load();
    settings.cutoffHz = prm.fCutoff->load();
    settings.resonance = pct (prm.fRes);
    settings.filterEnvAmount = pct (prm.fEnvAmt);
    settings.keyTrack = pct (prm.fKey);
    settings.filterLfoAmount = pct (prm.fLfo);
    settings.drive = pct (prm.fDrive);

    settings.ampEnv = { prm.ampA->load() * 0.001f, prm.ampD->load() * 0.001f, pct (prm.ampS), prm.ampR->load() * 0.001f };
    settings.filterEnv = { prm.fenvA->load() * 0.001f, prm.fenvD->load() * 0.001f, pct (prm.fenvS), prm.fenvR->load() * 0.001f };

    settings.lfo1Shape = (ap::Lfo::Shape) (int) prm.lfo1Shape->load();
    settings.lfo1RateHz = prm.lfo1Rate->load();
    settings.lfo1DepthCents = prm.lfo1Depth->load();
    settings.lfo1FadeMs = prm.lfo1Fade->load();
    settings.lfo2Shape = (ap::Lfo::Shape) (int) prm.lfo2Shape->load();
    settings.lfo2RateHz = prm.lfo2Rate->load();

    settings.glideMs = prm.glide->load();
    settings.velocityToAmp = pct (prm.velAmp);
    settings.velocityToFilter = pct (prm.velFilter);
    settings.pitchBendRange = prm.bendRange->load();

    synth.setPolyphony ((int) prm.voices->load());
}

void Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    updateSettings();

    // Mod wheel -> vibrato on all voices.
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isController() && m.getControllerNumber() == 1)
        {
            const float amt = (float) m.getControllerValue() / 127.0f;
            for (int i = 0; i < synth.getNumVoices(); ++i)
                if (auto* v = dynamic_cast<Voice*> (synth.getVoice (i))) v->setModWheel (amt);
        }
    }

    synth.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());

    const int numSamples = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();

    // --- Chorus
    const float chorusMix = prm.chorusMix->load() * 0.01f;
    if (chorusMix > 0.0f && numCh == 2)
    {
        chorus.setRate (prm.chorusRate->load());
        chorus.setMix (chorusMix);
        juce::dsp::AudioBlock<float> block (buffer);
        chorus.process (juce::dsp::ProcessContextReplacing<float> (block));
    }

    // --- Stereo delay with feedback
    const float delayMix = prm.delayMix->load() * 0.01f;
    const float fb = prm.delayFb->load() * 0.01f * 0.95f;
    delaySmoother.setTarget ((float) (sr * prm.delayTime->load() * 0.001));
    if (delayMix > 0.0f || delayTailActive)
    {
        float* l = buffer.getWritePointer (0);
        float* r = numCh > 1 ? buffer.getWritePointer (1) : l;
        float tailPeak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            const float d = delaySmoother.next();
            delayL.setDelay (d);
            delayR.setDelay (d);
            const float dl = delayL.popSample (0);
            const float dr = delayR.popSample (0);
            delayL.pushSample (0, l[i] * delayMix + dr * fb);   // ping-pong style cross feedback
            delayR.pushSample (0, r[i] * delayMix + dl * fb);
            l[i] += dl;
            if (numCh > 1) r[i] += dr;
            tailPeak = juce::jmax (tailPeak, std::fabs (dl), std::fabs (dr));
        }
        delayTailActive = tailPeak > 1.0e-5f;
    }

    // --- Reverb
    const float reverbMix = prm.reverbMix->load() * 0.01f;
    if (reverbMix > 0.0f || reverbTailActive)
    {
        juce::dsp::Reverb::Parameters p;
        p.roomSize = prm.reverbSize->load() * 0.01f;
        p.damping = 0.4f;
        p.width = 1.0f;
        p.wetLevel = reverbMix * 0.6f;
        p.dryLevel = 1.0f;
        reverb.setParameters (p);
        juce::dsp::AudioBlock<float> block (buffer);
        reverb.process (juce::dsp::ProcessContextReplacing<float> (block));
        reverbTailActive = reverbMix > 0.0f;
    }

    // --- Master
    masterSmoother.setTarget (ap::dbToGain (prm.master->load()));
    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = masterSmoother.next();
        for (int ch = 0; ch < numCh; ++ch)
        {
            float& s = buffer.getWritePointer (ch)[i];
            s *= g;
            peak = juce::jmax (peak, std::fabs (s));
        }
    }
    outputLevel.store (peak, std::memory_order_relaxed);
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

} // namespace strata

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new strata::Processor();
}
