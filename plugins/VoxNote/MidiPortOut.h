#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <ap/SpscQueue.h>

#include <array>
#include <atomic>
#include <bitset>
#include <functional>
#include <memory>
#include <vector>

namespace voxnote
{

/**
    Sends VoxNote's MIDI straight to a system MIDI port, in addition to the
    host's MIDI output. Hosts that do not pass MIDI out of an audio effect on
    to an instrument (FL Studio's VST3 wrapper, Logic, GarageBand) can then
    record VoxNote like any MIDI keyboard: from a loopMIDI port on Windows, or
    from VoxNote's own virtual port on macOS and Linux.

    Threads:
      - push() runs on the audio thread: wait-free, no allocation, no locks.
      - select(), availablePorts() and the status getters run on the message
        thread (JUCE's MIDI device list is message-thread only).
      - A background thread, running only while a port is open, sends the
        queued messages when they fall due and turns off every note it
        started before a port is closed or changed.
*/
class MidiPortOut : private juce::Thread,
                    private juce::AsyncUpdater
{
public:
    /** Pseudo identifier for "create VoxNote's own virtual port" (macOS / Linux). */
    static constexpr const char* virtualPortId = "voxnote:virtual";
    static constexpr const char* virtualPortName = "VoxNote";

    struct Port
    {
        juce::String identifier, name;
    };

    MidiPortOut();
    ~MidiPortOut() override;

    //==============================================================================
    // Audio thread.

    /** True while a port is open; push() is a no-op otherwise. */
    bool isActive() const noexcept { return active.load (std::memory_order_acquire); }

    /** Queues a short (≤ 3 byte) message to go out at `dueMs` on the
        juce::Time::getMillisecondCounterHiRes() clock. Drops it when the queue is full. */
    void push (const juce::MidiMessage& message, double dueMs) noexcept;

    //==============================================================================
    // Message thread.

    /** Whether this platform can create VoxNote's own virtual port. */
    static bool canCreateVirtualPort() noexcept;

    /** The virtual port first (where supported), then every MIDI output on the system. */
    std::vector<Port> availablePorts() const;

    /** Opens the port (empty identifier = off). When the identifier is no longer
        known (device IDs can change between sessions) a port with the same name
        is used instead. Returns whether a port is now open. */
    bool select (const juce::String& identifier, const juce::String& name);

    /** select() on the message thread later; safe to call from any thread. */
    void selectAsync (const juce::String& identifier, const juce::String& name);

    /** What was asked for (kept when it failed to open, so the choice survives a save). */
    juce::String requestedIdentifier() const;
    juce::String requestedName() const;
    bool isOpen() const;

    /** One line for the editor: "Off", "Sending to …", "Can't open …". */
    juce::String statusText() const;

    /** Called on the message thread after every select(). */
    std::function<void()> onChange;

private:
    struct Event
    {
        juce::uint8 data[3] {};
        juce::uint8 size = 0;
        double dueMs = 0.0;
    };

    void run() override;
    void handleAsyncUpdate() override;

    /** Needs deviceLock. Note-off for every note this sender started on the current device. */
    void releaseSoundingNotes();
    /** Needs deviceLock. Sends one event and tracks which notes are sounding. */
    void send (const Event&);
    void closeDevice();

    ap::SpscQueue<Event, 1024> queue;
    std::atomic<bool> active { false };

    juce::CriticalSection deviceLock;            // message thread + sender thread, never the audio thread
    std::unique_ptr<juce::MidiOutput> device;
    std::array<std::bitset<128>, 16> sounding;   // notes on, per channel, sent to `device`

    juce::String wantedId, wantedName, openedName;
    bool openFailed = false;

    juce::CriticalSection pendingLock;
    juce::String pendingId, pendingName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiPortOut)
};

} // namespace voxnote
