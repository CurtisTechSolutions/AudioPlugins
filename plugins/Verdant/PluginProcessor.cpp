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

    bool sameShape (const LfoShape& a, const LfoShape& b)
    {
        if (a.points.size() != b.points.size())
            return false;
        for (size_t i = 0; i < a.points.size(); ++i)
            if (! juce::exactlyEqual (a.points[i].x, b.points[i].x)
                || ! juce::exactlyEqual (a.points[i].y, b.points[i].y)
                || ! juce::exactlyEqual (a.points[i].curve, b.points[i].curve))
                return false;
        return true;
    }

    /** Engine::releaseRetired() frees tables / shapes the audio thread has swapped out (W-D adds it;
        compiled in only when the engine provides it — HandOff::post() also drains). */
    template <class E>
    void releaseRetiredOf (E& engine)
    {
        if constexpr (requires { engine.releaseRetired(); })
            engine.releaseRetired();
    }

    bool isMessageThread()
    {
        auto* mm = juce::MessageManager::getInstanceWithoutCreating();
        return mm != nullptr && mm->isThisTheMessageThread();
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
    uiExtras = extras;

    breedState.parentA = initPatch();
    breedState.parentB = initPatch();
    breedState.parentAName = breedState.parentBName = juce::String::fromUTF8 (breedState.parentA.name.c_str());

    presetManager = std::make_unique<PresetManager> (*this);
    backgroundTasks = std::make_unique<BackgroundTasks> (*this);

    startTimer (500);
}

Processor::~Processor()
{
    stopTimer();
    cancelPendingUpdate();
    backgroundTasks.reset();   // cancels and joins the worker (bounded wait) before anything it calls goes away
    presetManager.reset();
}

const juce::String Processor::getName() const
{
   #ifdef JucePlugin_Name
    return JucePlugin_Name;
   #else
    return "Verdant";   // built outside the plugin wrapper (verdant_render)
   #endif
}

double Processor::getTailLengthSeconds() const
{
    const double t = (double) eng.tailSeconds();
    return std::isfinite (t) ? juce::jlimit (0.0, 60.0, t) : 10.0;
}

void Processor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    preparedBlock = juce::jlimit (1, kMaxBlock, samplesPerBlock > 0 ? samplesPerBlock : 512);
    eng.prepare (sampleRate > 0.0 ? sampleRate : 44100.0, preparedBlock);
    auditionPlaying = -1;
    auditionRemaining = 0;
    setLatencySamples (eng.latencySamples());
}

void Processor::reset()
{
    eng.reset();
    auditionPlaying = -1;
    auditionRemaining = 0;
}

bool Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (! layouts.getMainInputChannelSet().isDisabled())
        return false;
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
            if (const auto bpm = pos->getBpm(); bpm.hasValue() && *bpm > 0.0 && std::isfinite (*bpm))
            {
                t.bpm = *bpm;
                t.hasTempo = true;
            }
            if (const auto ppq = pos->getPpqPosition(); ppq.hasValue() && std::isfinite (*ppq))
                t.ppq = *ppq;
            if (const auto time = pos->getTimeInSamples())
                t.sampleTime = *time;
            t.playing = pos->getIsPlaying();
        }
    }
    return t;
}

int Processor::collectMidi (const juce::MidiBuffer& midi, int numSamples) noexcept
{
    int count = 0;
    const auto push = [this, &count] (MidiEvent::Type type, int offset, int channel, int note, float value, int cc) noexcept
    {
        if (count < kMaxMidiEvents)
            midiEvents[(size_t) count++] = MidiEvent { type, offset, channel, note, value, cc };
    };

    // Pending auditions start at the top of the block, in request order (a new note ends the previous one;
    // a note-off request, velocity 0, ends a held note).
    if (const int ready = auditionFifo.getNumReady(); ready > 0)
    {
        auditionFifo.read (ready).forEach ([this, &push] (int index)
        {
            const auto request = auditionQueue[(size_t) index];
            if (request.velocity <= 0.0f)
            {
                if (auditionPlaying == request.note)
                {
                    push (MidiEvent::Type::NoteOff, 0, 1, auditionPlaying, 0.0f, 0);
                    auditionPlaying = -1;
                }
                return;
            }
            if (auditionPlaying >= 0)
                push (MidiEvent::Type::NoteOff, 0, 1, auditionPlaying, 0.0f, 0);
            push (MidiEvent::Type::NoteOn, 0, 1, request.note, request.velocity, 0);
            auditionPlaying = request.note;
            auditionRemaining = request.seconds < 0.0f ? -1 : (juce::int64) std::llround ((double) request.seconds * getSampleRate());
        });
    }

    // Host MIDI, decoded straight from the raw bytes (MidiMessage could allocate for long sysex).
    for (const auto meta : midi)
    {
        if (meta.numBytes < 1 || meta.data == nullptr)
            continue;
        const int status = meta.data[0];
        if (status < 0x80 || status >= 0xF0)
            continue;   // data without status / system messages

        const int offset = juce::jlimit (0, juce::jmax (0, numSamples - 1), meta.samplePosition);
        const int channel = (status & 0x0F) + 1;
        const int d1 = meta.numBytes > 1 ? (meta.data[1] & 0x7F) : 0;
        const int d2 = meta.numBytes > 2 ? (meta.data[2] & 0x7F) : 0;
        const float v2 = (float) d2 / 127.0f;

        switch (status & 0xF0)
        {
            case 0x80: push (MidiEvent::Type::NoteOff, offset, channel, d1, v2, 0); break;
            case 0x90: push (d2 > 0 ? MidiEvent::Type::NoteOn : MidiEvent::Type::NoteOff, offset, channel, d1, v2, 0); break;
            case 0xA0: push (MidiEvent::Type::PolyAftertouch, offset, channel, d1, v2, 0); break;
            case 0xD0: push (MidiEvent::Type::Aftertouch, offset, channel, 0, (float) d1 / 127.0f, 0); break;
            case 0xE0:
            {
                const int bend = d1 | (d2 << 7);   // 0..16383, centre 8192; scaled so both ends reach exactly +-1
                const float value = (float) (bend - 8192) / (bend >= 8192 ? 8191.0f : 8192.0f);
                push (MidiEvent::Type::PitchBend, offset, channel, 0, juce::jlimit (-1.0f, 1.0f, value), 0);
                break;
            }
            case 0xB0:
                switch (d1)
                {
                    case 1:  push (MidiEvent::Type::ModWheel, offset, channel, 0, v2, d1); break;
                    case 64: push (MidiEvent::Type::Sustain, offset, channel, 0, v2, d1); break;
                    case 121:   // Reset All Controllers
                        push (MidiEvent::Type::PitchBend, offset, channel, 0, 0.0f, 0);
                        push (MidiEvent::Type::ModWheel, offset, channel, 0, 0.0f, 1);
                        push (MidiEvent::Type::Aftertouch, offset, channel, 0, 0.0f, 0);
                        push (MidiEvent::Type::Sustain, offset, channel, 0, 0.0f, 64);
                        break;
                    case 120:   // All Sound Off
                    case 123:   // All Notes Off
                    case 124: case 125: case 126: case 127:   // omni / mono / poly mode changes imply all notes off
                        push (MidiEvent::Type::AllNotesOff, offset, channel, 0, 0.0f, d1);
                        break;
                    default: push (MidiEvent::Type::Cc, offset, channel, 0, v2, d1); break;
                }
                break;
            default:
                break;   // program change (0xC0) is not handled on the audio thread
        }
    }

    // The audition's note-off, inserted in order, once its time is up (held notes wait for their note-off request).
    if (auditionPlaying >= 0 && auditionRemaining >= 0)
    {
        if (auditionRemaining < (juce::int64) numSamples)
        {
            const int offset = (int) juce::jlimit ((juce::int64) 0, (juce::int64) juce::jmax (0, numSamples - 1), auditionRemaining);
            if (count < kMaxMidiEvents)
            {
                int at = count;
                while (at > 0 && midiEvents[(size_t) at - 1].sampleOffset > offset)
                {
                    midiEvents[(size_t) at] = midiEvents[(size_t) at - 1];
                    --at;
                }
                midiEvents[(size_t) at] = MidiEvent { MidiEvent::Type::NoteOff, offset, 1, auditionPlaying, 0.0f, 0 };
                ++count;
            }
            auditionPlaying = -1;
        }
        else
        {
            auditionRemaining -= numSamples;
        }
    }
    return count;
}

void Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numSamples <= 0 || numChannels <= 0)
        return;

    // Parameters: one flat copy of the APVTS atomics per block (the engine smooths every change).
    for (size_t i = 0; i < (size_t) kNumParams; ++i)
        snapshot[i] = raw[i]->load (std::memory_order_relaxed);
    eng.setParams (snapshot);

    const Transport transport = readTransport();
    const int numEvents = collectMidi (midi, numSamples);

    float* left = buffer.getWritePointer (0);
    float* right = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;
    const int chunk = juce::jlimit (1, kMaxBlock, preparedBlock);   // never more than the engine was prepared for
    const double samplesPerBeat = transport.bpm > 0.0 ? 60.0 * getSampleRate() / transport.bpm : 0.0;
    int nextEvent = 0;

    for (int pos = 0; pos < numSamples; pos += chunk)
    {
        const int n = juce::jmin (chunk, numSamples - pos);

        Transport t = transport;
        if (pos > 0)
        {
            t.sampleTime += pos;
            if (samplesPerBeat > 0.0)
                t.ppq += (double) pos / samplesPerBeat;
        }
        eng.setTransport (t);

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
    return juce::jlimit (0, getNumPrograms() - 1, currentProgram.load());
}

void Processor::setCurrentProgram (int index)
{
    const auto& bank = factoryPresets();
    if (index < 0 || index >= (int) bank.size())
        return;

    // Hosts may call this off the message thread: apply the factory patch directly (no file I/O, no
    // preset-list access) and let the preset manager record it.
    applyPatch (bank[(size_t) index], false);
    currentProgram.store (index);
    presetManager->programLoaded (index);
}

const juce::String Processor::getProgramName (int index)
{
    const auto& bank = factoryPresets();
    return index >= 0 && index < (int) bank.size() ? juce::String::fromUTF8 (bank[(size_t) index].name.c_str()) : juce::String();
}

void Processor::setCurrentProgramIndex (int index, bool notifyHost)
{
    if (index < 0 || index >= (int) factoryPresets().size() || index == currentProgram.load())
        return;
    currentProgram.store (index);
    if (notifyHost)
        updateHostDisplay (ChangeDetails().withProgramChanged (true));
}

void Processor::getStateInformation (juce::MemoryBlock& destData)
{
    auto xml = patchToXml (currentPatch());
    xml->setAttribute ("program", currentProgram.load());
    copyXmlToBinary (*xml, destData);
}

void Processor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;

    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    Patch p;
    if (! patchFromXml (*xml, p))
        return;

    applyPatchInternal (p, false, true);

    const int program = xml->getIntAttribute ("program", -1);
    if (program >= 0 && program < (int) factoryPresets().size())
        currentProgram.store (program);
    presetManager->stateRestored (p, program);
}

//==============================================================================
ParamValues Processor::parameterValues() const noexcept
{
    ParamValues v;
    for (size_t i = 0; i < (size_t) kNumParams; ++i)
        v[i] = raw[i]->load (std::memory_order_relaxed);
    return v;
}

Patch Processor::currentPatch() const
{
    Patch p;
    {
        const juce::ScopedLock sl (extrasLock);
        p = extras;
    }
    p.values = parameterValues();
    return p;
}

void Processor::applyPatch (const Patch& patch, bool keepName)
{
    applyPatchInternal (patch, keepName, false);
}

void Processor::applyPatchInternal (const Patch& patch, bool keepName, bool restoringState)
{
    // State restores, preset and program loads notify every parameter, even unchanged ones: a host may
    // hold edits the processor has not received yet (VST3 delivers them with the next block), and only
    // a notification brings its view back in line (pluginval "Plugin state restoration"). Live blends /
    // auditions (keepName) touch only what changed, so a 20 Hz blend does not flood the host.
    const bool notifyAll = restoringState || ! keepName;
    for (int i = 0; i < kNumParams; ++i)
    {
        const float v = patch.values[(size_t) i];
        const float target = std::isfinite (v) ? clampToRange ((P) i, v) : info ((P) i).def;
        if (notifyAll || ! juce::exactlyEqual (raw[(size_t) i]->load(), target))
        {
            auto* p = parameters[(size_t) i];
            p->setValueNotifyingHost (p->convertTo0to1 (target));   // the layout's own plain -> normalised mapping
        }
    }

    Patch copy;
    {
        const juce::ScopedLock sl (extrasLock);
        Patch next = patch;
        next.values = {};   // extras only; parameters live in the APVTS
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
    extrasChanged (true);
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
    {
        if (shapesInstalled && sameShape (p.lfoShapes[(size_t) l], installedShapes[(size_t) l]))
            continue;
        eng.setLfoShape (l, std::make_shared<const LfoShape> (p.lfoShapes[(size_t) l]));
        installedShapes[(size_t) l] = p.lfoShapes[(size_t) l];
    }
    shapesInstalled = true;
    eng.setLivingSeed (p.livingSeed);
    releaseRetiredOf (eng);
}

void Processor::extrasChanged (bool modifiesPatch)
{
    if (modifiesPatch)
        extrasRev.fetch_add (1, std::memory_order_acq_rel);

    if (isMessageThread())
    {
        cancelPendingUpdate();
        handleAsyncUpdate();
    }
    else
    {
        triggerAsyncUpdate();   // a host thread (state restore, program change): refresh on the message thread
    }
}

void Processor::handleAsyncUpdate()
{
    {
        const juce::ScopedLock sl (extrasLock);
        uiExtras = extras;
    }
    patchChanged.sendChangeMessage();
}

void Processor::timerCallback()
{
    const juce::ScopedLock sl (engineLock);
    releaseRetiredOf (eng);
}

void Processor::setParameterPlain (P param, float plain)
{
    auto* p = parameters[(size_t) param];
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 (clampToRange (param, plain)));
    p->endChangeGesture();
}

void Processor::setPatchInfo (const juce::String& name, const juce::String& category,
                              const juce::StringArray& tags, const juce::String& author)
{
    {
        const juce::ScopedLock sl (extrasLock);
        extras.name = name.toStdString();
        extras.category = category.toStdString();
        extras.author = author.toStdString();
        extras.tags.clear();
        for (const auto& t : tags)
            if (t.trim().isNotEmpty())
                extras.tags.push_back (t.trim().toStdString());
    }
    extrasChanged (true);
}

//==============================================================================
bool Processor::setModulation (int destIndex, ModSource source, float amount)
{
    if (destIndex < 1 || destIndex > numModDestinations() || source == ModSource::None || source >= ModSource::Count
        || ! std::isfinite (amount))
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

std::vector<std::pair<int, ModSource>> Processor::modulationsFor (int destIndex) const
{
    std::vector<std::pair<int, ModSource>> result;
    if (destIndex < 1 || destIndex > numModDestinations())
        return result;
    for (int s = 0; s < kModSlots; ++s)
    {
        const int src = (int) std::lround (raw[(size_t) modSrc (s)]->load());
        const int dst = (int) std::lround (raw[(size_t) modDst (s)]->load());
        if (dst == destIndex && src > (int) ModSource::None && src < (int) ModSource::Count)
            result.emplace_back (s, (ModSource) src);
    }
    return result;
}

//==============================================================================
void Processor::rerollSeed()
{
    uint32_t seed = 0;
    {
        const juce::ScopedLock sl (extrasLock);
        if (extras.seedLocked)
            return;
        auto& rng = juce::Random::getSystemRandom();
        do { seed = (uint32_t) rng.nextInt64(); } while (seed == extras.livingSeed);
        extras.livingSeed = seed;
    }
    {
        const juce::ScopedLock sl (engineLock);
        eng.setLivingSeed (seed);
    }
    extrasChanged (true);
}

void Processor::setSeedLocked (bool locked)
{
    {
        const juce::ScopedLock sl (extrasLock);
        if (extras.seedLocked == locked)
            return;
        extras.seedLocked = locked;
    }
    extrasChanged (false);   // a performance setting: the preset does not count as modified
}

bool Processor::seedLocked() const
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
        extras.macroNames[(size_t) macro] = name.removeCharacters ("\r\n").toStdString();
    }
    extrasChanged (true);
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
        installedShapes[(size_t) lfo] = shape;
        releaseRetiredOf (eng);
    }
    extrasChanged (true);
}

void Processor::setUserTable (int osc, const UserTable& table, std::shared_ptr<const Wavetable> built)
{
    if (osc < 0 || osc > 1)
        return;
    if (built == nullptr)
        built = buildTable (table);   // nullptr for an empty table: the engine falls back to factory table 0
    const bool empty = built == nullptr;
    {
        const juce::ScopedLock sl (extrasLock);
        extras.userTables[(size_t) osc] = table;
    }
    {
        const juce::ScopedLock sl (engineLock);
        eng.setUserTable (osc, std::move (built));
        installedTables[(size_t) osc] = table;
        releaseRetiredOf (eng);
    }
    if (! empty)
        setParameterPlain (oscParam (osc, OscParam::Table), (float) kNumFactoryTables);   // "User"
    extrasChanged (true);
}

void Processor::auditionNote (int midiNote, float seconds, float velocity)
{
    const AuditionRequest request { juce::jlimit (0, 127, midiNote),
                                    std::isfinite (velocity) ? juce::jlimit (0.01f, 1.0f, velocity) : 0.8f,
                                    std::isfinite (seconds) ? juce::jlimit (0.01f, 10.0f, seconds) : 0.4f };
    auditionFifo.write (1).forEach ([this, &request] (int index) { auditionQueue[(size_t) index] = request; });
}

void Processor::auditionNoteOn (int midiNote, float velocity)
{
    const AuditionRequest request { juce::jlimit (0, 127, midiNote),
                                    std::isfinite (velocity) ? juce::jlimit (0.01f, 1.0f, velocity) : 0.8f, -1.0f };
    auditionFifo.write (1).forEach ([this, &request] (int index) { auditionQueue[(size_t) index] = request; });
}

void Processor::auditionNoteOff (int midiNote)
{
    const AuditionRequest request { juce::jlimit (0, 127, midiNote), 0.0f, 0.0f };
    auditionFifo.write (1).forEach ([this, &request] (int index) { auditionQueue[(size_t) index] = request; });
}

//==============================================================================
void Processor::breedNow()
{
    auto& b = breedState;
    BreedSettings settings;
    settings.mutation = juce::jlimit (0.0f, 1.0f, std::isfinite (b.mutation) ? b.mutation : 0.25f);
    settings.count = 8;
    settings.seed = b.seed;

    auto children = verdant::breed (b.parentA, b.parentB, settings);
    b.history.push (b.parentA, b.parentB, children);
    b.children = std::move (children);
    breedingChanged.sendChangeMessage();
}

void Processor::applyChild (int index)
{
    if (index < 0 || index >= (int) breedState.children.size())
        return;
    const Patch child = breedState.children[(size_t) index];   // applyPatch may re-enter the UI
    applyPatch (child, false);
}

void Processor::undoBreed()
{
    auto& b = breedState;
    if (! b.history.canUndo())
        return;
    b.history.undo();
    if (const auto* kids = b.history.current())
        b.children = *kids;
    else
        b.children.clear();
    breedingChanged.sendChangeMessage();
}

void Processor::setParentFromCurrent (int which)
{
    if (which < 0 || which > 1)
        return;
    auto p = currentPatch();
    const auto name = juce::String::fromUTF8 (p.name.c_str());
    (which == 0 ? breedState.parentA : breedState.parentB) = std::move (p);
    (which == 0 ? breedState.parentAName : breedState.parentBName) = name;
    breedingChanged.sendChangeMessage();
}

void Processor::setParentFromPreset (int which, int presetIndex)
{
    if (which < 0 || which > 1)
        return;
    Patch p;
    if (! presetManager->readPatch (presetIndex, p))
        return;
    const auto name = juce::String::fromUTF8 (p.name.c_str());
    (which == 0 ? breedState.parentA : breedState.parentB) = std::move (p);
    (which == 0 ? breedState.parentAName : breedState.parentBName) = name;
    breedingChanged.sendChangeMessage();
}

} // namespace verdant

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new verdant::Processor();
}
