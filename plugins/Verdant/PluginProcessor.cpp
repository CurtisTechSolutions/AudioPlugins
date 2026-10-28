#include "PluginProcessor.h"

#include "BackgroundTasks.h"
#include "PluginEditor.h"
#include "PresetManager.h"

#include "engine/FactoryPresets.h"

#include <cmath>

namespace verdant
{

namespace
{
    /** Number with up to `decimals` decimals, trailing zeros trimmed. */
    juce::String number (float v, int decimals)
    {
        juce::String s (v, decimals);
        if (decimals > 0 && s.containsChar ('.'))
            s = s.trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
        return s == "-0" ? juce::String ("0") : s;
    }

    juce::String formatValue (float v, const juce::String& unit)
    {
        const float a = std::fabs (v);
        if (unit == "Hz")
            return a >= 1000.0f ? number (v / 1000.0f, 2) + " kHz"
                                : number (v, a < 1.0f ? 3 : (a < 10.0f ? 2 : (a < 100.0f ? 1 : 0))) + " Hz";
        if (unit == "ms")
            return a >= 1000.0f ? number (v / 1000.0f, 2) + " s"
                                : number (v, a < 1.0f ? 2 : (a < 10.0f ? 1 : 0)) + " ms";
        if (unit == "%")       return number (v, a < 10.0f ? 1 : 0) + " %";
        if (unit == "dB")      return number (v, 1) + " dB";
        if (unit == "ct")      return number (v, 1) + " ct";
        if (unit == "st")      return number (v, 2) + " st";
        if (unit == ":1")      return number (v, 1) + ":1";
        if (unit.isEmpty())    return number (v, 2);
        if (unit == juce::String::fromUTF8 ("\xC2\xB0")) return number (v, 0) + unit;
        return number (v, 2) + " " + unit;
    }

    float parseValue (const juce::String& text, const juce::String& unit)
    {
        const float v = text.retainCharacters ("0123456789.-").getFloatValue();
        if (unit == "Hz" && text.containsIgnoreCase ("k"))
            return v * 1000.0f;
        if (unit == "ms" && text.trim().endsWithIgnoreCase ("s") && ! text.containsIgnoreCase ("ms"))
            return v * 1000.0f;
        return v;
    }

    std::unique_ptr<juce::RangedAudioParameter> makeParameter (const ParamInfo& in)
    {
        const juce::ParameterID pid (in.id, 1);
        const auto name = juce::String::fromUTF8 (in.name);
        const auto unit = juce::String::fromUTF8 (in.unit);

        switch (in.kind)
        {
            case ParamKind::Int:
                return std::make_unique<juce::AudioParameterInt> (pid, name, (int) in.min, (int) in.max, (int) in.def,
                                                                  juce::AudioParameterIntAttributes().withLabel (unit));
            case ParamKind::Choice:
            {
                juce::StringArray choices;
                for (int c = 0; c < in.numChoices; ++c)
                    choices.add (juce::String::fromUTF8 (in.choices[c]));
                return std::make_unique<juce::AudioParameterChoice> (pid, name, choices, (int) in.def);
            }
            case ParamKind::Bool:
                return std::make_unique<juce::AudioParameterBool> (pid, name, in.def > 0.5f);
            case ParamKind::Float:
                break;
        }

        juce::NormalisableRange<float> range (in.min, in.max);
        if (in.midpoint > 0.0f && in.midpoint > in.min && in.midpoint < in.max)
            range.setSkewForCentre (in.midpoint);   // same mapping as verdant::toNormalised

        auto attrs = juce::AudioParameterFloatAttributes()
                         .withLabel (unit)
                         .withStringFromValueFunction ([unit] (float v, int) { return formatValue (v, unit); })
                         .withValueFromStringFunction ([unit] (const juce::String& t) { return parseValue (t, unit); });
        return std::make_unique<juce::AudioParameterFloat> (pid, name, range, in.def, attrs);
    }

    bool convertMidi (const juce::MidiMessage& m, int samplePosition, int numSamples, MidiEvent& e)
    {
        e = MidiEvent();
        e.sampleOffset = juce::jlimit (0, juce::jmax (0, numSamples - 1), samplePosition);
        e.channel = m.getChannel();

        if (m.isNoteOn())
        {
            e.type = MidiEvent::Type::NoteOn;
            e.note = m.getNoteNumber();
            e.value = m.getFloatVelocity();
        }
        else if (m.isNoteOff())
        {
            e.type = MidiEvent::Type::NoteOff;
            e.note = m.getNoteNumber();
            e.value = m.getFloatVelocity();
        }
        else if (m.isPitchWheel())
        {
            e.type = MidiEvent::Type::PitchBend;
            e.value = juce::jlimit (-1.0f, 1.0f, (float) (m.getPitchWheelValue() - 8192) / 8192.0f);
        }
        else if (m.isAftertouch())
        {
            e.type = MidiEvent::Type::PolyAftertouch;
            e.note = m.getNoteNumber();
            e.value = (float) m.getAfterTouchValue() / 127.0f;
        }
        else if (m.isChannelPressure())
        {
            e.type = MidiEvent::Type::Aftertouch;
            e.value = (float) m.getChannelPressureValue() / 127.0f;
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            e.type = MidiEvent::Type::AllNotesOff;
        }
        else if (m.isController())
        {
            e.cc = m.getControllerNumber();
            e.value = (float) m.getControllerValue() / 127.0f;
            e.type = e.cc == 1 ? MidiEvent::Type::ModWheel : (e.cc == 64 ? MidiEvent::Type::Sustain : MidiEvent::Type::Cc);
        }
        else
        {
            return false;
        }
        return true;
    }

    std::shared_ptr<const Wavetable> buildTable (const UserTable& t)
    {
        if (t.numFrames <= 0 || t.frames.size() < (size_t) t.numFrames * (size_t) kFrameSize)
            return nullptr;
        return Wavetable::fromFrames (t.frames.data(), t.numFrames, t.name);
    }

    bool sameTable (const UserTable& a, const UserTable& b)
    {
        return a.numFrames == b.numFrames && a.name == b.name && a.frames == b.frames;
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout Processor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (int i = 0; i < kNumParams; ++i)
        layout.add (makeParameter (info ((P) i)));
    return layout;
}

Processor::Processor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "VERDANT", createLayout())
{
    for (int i = 0; i < kNumParams; ++i)
    {
        const char* id = info ((P) i).id;
        raw[(size_t) i] = apvts.getRawParameterValue (id);
        parameters[(size_t) i] = apvts.getParameter (id);
        jassert (raw[(size_t) i] != nullptr && parameters[(size_t) i] != nullptr);
    }

    monoScratch.assign ((size_t) kMaxBlock, 0.0f);
    snapshot = defaultValues();
    postExtrasToEngine (extras);

    presetManager = std::make_unique<PresetManager> (*this);
    backgroundTasks = std::make_unique<BackgroundTasks> (*this);
}

Processor::~Processor()
{
    backgroundTasks.reset();   // stop the worker before anything it might call into goes away
}

const juce::String Processor::getName() const
{
   #ifdef JucePlugin_Name
    return JucePlugin_Name;
   #else
    return "Verdant";   // built outside the plugin wrapper (verdant_render)
   #endif
}

void Processor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    eng.prepare (sampleRate, juce::jlimit (1, kMaxBlock, samplesPerBlock));
    setLatencySamples (eng.latencySamples());
}

bool Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

Transport Processor::readTransport()
{
    Transport t;
    if (auto* ph = getPlayHead())
    {
        if (const auto pos = ph->getPosition())
        {
            if (const auto bpm = pos->getBpm())          { t.bpm = *bpm; t.hasTempo = true; }
            if (const auto ppq = pos->getPpqPosition())  t.ppq = *ppq;
            if (const auto time = pos->getTimeInSamples()) t.sampleTime = *time;
            t.playing = pos->getIsPlaying();
        }
    }
    return t;
}

void Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numSamples <= 0 || numChannels <= 0)
        return;

    for (size_t i = 0; i < (size_t) kNumParams; ++i)
        snapshot[i] = raw[i]->load (std::memory_order_relaxed);
    eng.setParams (snapshot);
    eng.setTransport (readTransport());

    int numEvents = 0;
    for (const auto meta : midi)
    {
        if (numEvents >= kMaxMidiEvents) break;
        if (convertMidi (meta.getMessage(), meta.samplePosition, numSamples, midiEvents[(size_t) numEvents]))
            ++numEvents;
    }

    float* left = buffer.getWritePointer (0);
    float* right = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;
    int nextEvent = 0;

    for (int pos = 0; pos < numSamples; pos += kMaxBlock)
    {
        const int n = juce::jmin (kMaxBlock, numSamples - pos);
        const int first = nextEvent;
        while (nextEvent < numEvents && midiEvents[(size_t) nextEvent].sampleOffset < pos + n)
            midiEvents[(size_t) nextEvent++].sampleOffset -= pos;

        float* r = right != nullptr ? right + pos : monoScratch.data();
        eng.process (left + pos, r, n, midiEvents.data() + first, nextEvent - first);

        if (right == nullptr)
            for (int i = 0; i < n; ++i)
                left[pos + i] = 0.5f * (left[pos + i] + monoScratch[(size_t) i]);
    }

    for (int ch = 2; ch < numChannels; ++ch)
        buffer.clear (ch, 0, numSamples);

    outputPeak.store (buffer.getMagnitude (0, numSamples), std::memory_order_relaxed);
}

juce::AudioProcessorEditor* Processor::createEditor()
{
    return new Editor (*this);
}

//==============================================================================
int Processor::getNumPrograms()
{
    return juce::jmax (1, (int) factoryPresets().size());
}

int Processor::getCurrentProgram()
{
    return currentProgram;
}

void Processor::setCurrentProgram (int index)
{
    if (index < 0 || index >= (int) factoryPresets().size())
        return;
    currentProgram = index;
    presetManager->load (index);   // factory presets are the first entries
}

const juce::String Processor::getProgramName (int index)
{
    const auto& bank = factoryPresets();
    return index >= 0 && index < (int) bank.size() ? juce::String::fromUTF8 (bank[(size_t) index].name.c_str()) : juce::String();
}

void Processor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = patchToXml (currentPatch()))
        copyXmlToBinary (*xml, destData);
}

void Processor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    Patch p;
    if (patchFromXml (*xml, p))
        applyPatchInternal (p, false, true);
}

//==============================================================================
Patch Processor::currentPatch() const
{
    Patch p;
    {
        const juce::ScopedLock sl (extrasLock);
        p = extras;
    }
    for (size_t i = 0; i < (size_t) kNumParams; ++i)
        p.values[i] = raw[i]->load (std::memory_order_relaxed);
    return p;
}

void Processor::applyPatch (const Patch& patch, bool keepName)
{
    applyPatchInternal (patch, keepName, false);
}

void Processor::applyPatchInternal (const Patch& patch, bool keepName, bool restoringState)
{
    for (int i = 0; i < kNumParams; ++i)
    {
        const float target = clampToRange ((P) i, patch.values[(size_t) i]);
        if (! juce::exactlyEqual (raw[(size_t) i]->load(), target))
        {
            auto* p = parameters[(size_t) i];
            p->setValueNotifyingHost (p->convertTo0to1 (target));
        }
    }

    Patch copy;
    {
        const juce::ScopedLock sl (extrasLock);
        Patch next = patch;
        if (keepName)
        {
            next.name = extras.name;
            next.category = extras.category;
            next.author = extras.author;
            next.tags = extras.tags;
        }
        if (restoringState)
        {
            // a saved session restores its seed and lock exactly
        }
        else if (extras.seedLocked)
        {
            next.livingSeed = extras.livingSeed;
            next.seedLocked = true;
        }
        else
        {
            next.seedLocked = false;   // loading never changes the lock; the patch's seed is adopted
        }
        extras = std::move (next);
        copy = extras;
    }

    postExtrasToEngine (copy);
    patchChanged.sendChangeMessage();
}

void Processor::postExtrasToEngine (const Patch& p)
{
    const juce::ScopedLock sl (engineLock);
    for (int o = 0; o < 2; ++o)
    {
        const auto& table = p.userTables[(size_t) o];
        if (sameTable (table, installedTables[(size_t) o]))
            continue;
        eng.setUserTable (o, buildTable (table));
        installedTables[(size_t) o] = table;
    }
    for (int l = 0; l < kNumLfos; ++l)
        eng.setLfoShape (l, std::make_shared<const LfoShape> (p.lfoShapes[(size_t) l]));
    eng.setLivingSeed (p.livingSeed);
}

void Processor::setParameterPlain (P param, float plain)
{
    auto* p = parameters[(size_t) param];
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 (clampToRange (param, plain)));
    p->endChangeGesture();
}

bool Processor::setModulation (int destIndex, ModSource source, float amount)
{
    if (destIndex < 1 || destIndex > numModDestinations() || source == ModSource::None || source >= ModSource::Count)
        return false;

    const bool clear = juce::exactlyEqual (amount, 0.0f);
    amount = juce::jlimit (-1.0f, 1.0f, amount);

    auto srcOf = [this] (int s) { return (int) std::lround (raw[(size_t) modSrc (s)]->load()); };
    auto dstOf = [this] (int s) { return (int) std::lround (raw[(size_t) modDst (s)]->load()); };

    int slot = -1;
    for (int s = 0; s < kModSlots && slot < 0; ++s)
        if (srcOf (s) == (int) source && dstOf (s) == destIndex)
            slot = s;

    if (slot < 0)
    {
        if (clear)
            return true;   // nothing routed, nothing to clear
        for (int s = 0; s < kModSlots && slot < 0; ++s)
            if (srcOf (s) == (int) ModSource::None && dstOf (s) == 0)
                slot = s;
        if (slot < 0)
            return false;   // all 24 slots in use
        setParameterPlain (modSrc (slot), (float) source);
        setParameterPlain (modDst (slot), (float) destIndex);
    }

    if (clear)
    {
        setParameterPlain (modSrc (slot), 0.0f);
        setParameterPlain (modDst (slot), 0.0f);
        setParameterPlain (modAmt (slot), 0.0f);
    }
    else
    {
        setParameterPlain (modAmt (slot), amount * 100.0f);
    }
    return true;
}

void Processor::rerollSeed()
{
    uint32_t seed = 0;
    {
        const juce::ScopedLock sl (extrasLock);
        if (extras.seedLocked)
            return;
        seed = (uint32_t) juce::Random::getSystemRandom().nextInt64();
        extras.livingSeed = seed;
    }
    {
        const juce::ScopedLock sl (engineLock);
        eng.setLivingSeed (seed);
    }
    patchChanged.sendChangeMessage();
}

void Processor::setSeedLocked (bool locked)
{
    {
        const juce::ScopedLock sl (extrasLock);
        extras.seedLocked = locked;
    }
    patchChanged.sendChangeMessage();
}

bool Processor::isSeedLocked() const
{
    const juce::ScopedLock sl (extrasLock);
    return extras.seedLocked;
}

uint32_t Processor::livingSeed() const
{
    const juce::ScopedLock sl (extrasLock);
    return extras.livingSeed;
}

void Processor::setMacroName (int macro, const juce::String& name)
{
    if (macro < 0 || macro >= kNumMacros)
        return;
    {
        const juce::ScopedLock sl (extrasLock);
        extras.macroNames[(size_t) macro] = name.toStdString();
    }
    patchChanged.sendChangeMessage();
}

void Processor::setLfoShape (int lfo, const LfoShape& shape)
{
    if (lfo < 0 || lfo >= kNumLfos)
        return;
    {
        const juce::ScopedLock sl (extrasLock);
        extras.lfoShapes[(size_t) lfo] = shape;
    }
    {
        const juce::ScopedLock sl (engineLock);
        eng.setLfoShape (lfo, std::make_shared<const LfoShape> (shape));
    }
    patchChanged.sendChangeMessage();
}

void Processor::setUserTable (int osc, const UserTable& table, std::shared_ptr<const Wavetable> built)
{
    if (osc < 0 || osc > 1)
        return;
    {
        const juce::ScopedLock sl (extrasLock);
        extras.userTables[(size_t) osc] = table;
    }
    {
        const juce::ScopedLock sl (engineLock);
        eng.setUserTable (osc, built != nullptr ? std::move (built) : buildTable (table));
        installedTables[(size_t) osc] = table;
    }
    setParameterPlain (oscParam (osc, OscParam::Table), (float) kNumFactoryTables);   // "User"
    patchChanged.sendChangeMessage();
}

} // namespace verdant

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new verdant::Processor();
}
