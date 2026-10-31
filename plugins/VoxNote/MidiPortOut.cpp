#include "MidiPortOut.h"

namespace voxnote
{

MidiPortOut::MidiPortOut() : juce::Thread ("VoxNote MIDI port") {}

MidiPortOut::~MidiPortOut()
{
    cancelPendingUpdate();
    closeDevice();
}

//==============================================================================
void MidiPortOut::push (const juce::MidiMessage& message, double dueMs) noexcept
{
    const int size = message.getRawDataSize();
    if (! isActive() || size <= 0 || size > 3)
        return;

    Event e;
    const auto* raw = message.getRawData();
    for (int i = 0; i < size; ++i)
        e.data[i] = raw[i];
    e.size = (juce::uint8) size;
    e.dueMs = dueMs;
    queue.push (e);
}

void MidiPortOut::run()
{
    while (! threadShouldExit())
    {
        {
            const juce::ScopedLock sl (deviceLock);
            const double now = juce::Time::getMillisecondCounterHiRes();
            while (const Event* e = queue.front())
            {
                if (e->dueMs > now)
                    break;
                send (*e);
                queue.pop();
            }
        }
        wait (1.0);
    }
}

void MidiPortOut::send (const Event& e)
{
    if (device == nullptr)
        return;

    const auto m = e.size == 3 ? juce::MidiMessage (e.data[0], e.data[1], e.data[2])
                 : e.size == 2 ? juce::MidiMessage (e.data[0], e.data[1])
                               : juce::MidiMessage (e.data[0]);
    const int ch = m.getChannel() - 1;
    if (ch >= 0 && ch < 16)
    {
        if (m.isNoteOn())
            sounding[(size_t) ch].set ((size_t) m.getNoteNumber());
        else if (m.isNoteOff())
            sounding[(size_t) ch].reset ((size_t) m.getNoteNumber());
        else if (m.isAllNotesOff())
            sounding[(size_t) ch].reset();
    }
    device->sendMessageNow (m);
}

void MidiPortOut::releaseSoundingNotes()
{
    if (device == nullptr)
        return;
    for (int ch = 0; ch < 16; ++ch)
    {
        for (int n = 0; n < 128; ++n)
            if (sounding[(size_t) ch].test ((size_t) n))
                device->sendMessageNow (juce::MidiMessage::noteOff (ch + 1, n));
        sounding[(size_t) ch].reset();
    }
}

void MidiPortOut::closeDevice()
{
    active.store (false, std::memory_order_release);
    stopThread (500);

    const juce::ScopedLock sl (deviceLock);
    // Flush what was already queued (usually the note-offs that end the phrase), then
    // make sure nothing is left hanging on the receiving instrument.
    while (const Event* e = queue.front())
    {
        send (*e);
        queue.pop();
    }
    releaseSoundingNotes();
    device.reset();
    openedName.clear();
}

//==============================================================================
bool MidiPortOut::canCreateVirtualPort() noexcept
{
   #if JUCE_MAC || JUCE_LINUX || JUCE_BSD
    return true;
   #else
    return false;   // Windows: create a port with loopMIDI and pick it from the list
   #endif
}

std::vector<MidiPortOut::Port> MidiPortOut::availablePorts() const
{
    std::vector<Port> ports;
    if (canCreateVirtualPort())
        ports.push_back ({ virtualPortId, juce::String (virtualPortName) + " (virtual port)" });
    for (const auto& d : juce::MidiOutput::getAvailableDevices())
        ports.push_back ({ d.identifier, d.name });
    return ports;
}

bool MidiPortOut::select (const juce::String& identifier, const juce::String& name)
{
    JUCE_ASSERT_MESSAGE_THREAD
    closeDevice();

    wantedId = identifier;
    wantedName = name;
    openFailed = false;

    std::unique_ptr<juce::MidiOutput> opened;
    juce::String shownName;
    if (identifier == virtualPortId)
    {
        opened = juce::MidiOutput::createNewDevice (virtualPortName);
        shownName = juce::String (virtualPortName) + " (virtual port)";
    }
    else if (identifier.isNotEmpty())
    {
        opened = juce::MidiOutput::openDevice (identifier);
        shownName = name;
        if (opened == nullptr && name.isNotEmpty())
        {
            for (const auto& d : juce::MidiOutput::getAvailableDevices())
                if (d.name == name)
                {
                    opened = juce::MidiOutput::openDevice (d.identifier);
                    if (opened != nullptr)
                        wantedId = d.identifier;
                    break;
                }
        }
        if (opened != nullptr)
            shownName = opened->getName();
    }

    if (identifier.isNotEmpty() && opened == nullptr)
        openFailed = true;

    if (opened != nullptr)
    {
        {
            const juce::ScopedLock sl (deviceLock);
            device = std::move (opened);
            openedName = shownName;
            for (auto& s : sounding) s.reset();
        }
        queue.clear();
        startThread (juce::Thread::Priority::high);
        active.store (true, std::memory_order_release);
    }

    if (onChange != nullptr)
        onChange();
    return isOpen();
}

void MidiPortOut::selectAsync (const juce::String& identifier, const juce::String& name)
{
    {
        const juce::ScopedLock sl (pendingLock);
        pendingId = identifier;
        pendingName = name;
    }
    triggerAsyncUpdate();
}

void MidiPortOut::handleAsyncUpdate()
{
    juce::String id, name;
    {
        const juce::ScopedLock sl (pendingLock);
        id = pendingId;
        name = pendingName;
    }
    if (id != wantedId || (id.isNotEmpty() && ! isOpen()))
        select (id, name);
}

juce::String MidiPortOut::requestedIdentifier() const { return wantedId; }
juce::String MidiPortOut::requestedName() const       { return wantedName; }

bool MidiPortOut::isOpen() const
{
    const juce::ScopedLock sl (deviceLock);
    return device != nullptr;
}

juce::String MidiPortOut::statusText() const
{
    if (wantedId.isEmpty())
        return "Off: MIDI goes to the host only";
    if (openFailed)
        return "Can't open " + (wantedName.isNotEmpty() ? wantedName : wantedId);
    const juce::ScopedLock sl (deviceLock);
    return "Sending to " + openedName;
}

} // namespace voxnote
