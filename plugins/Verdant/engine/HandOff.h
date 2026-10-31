#pragma once

// Lock-free single-producer / single-consumer hand-off of heavy, immutable
// objects (wavetables, LFO shapes) from the message thread to the audio thread.
//
//   message thread:  post (object)   publishes the newest object; an older one the
//                                     audio thread never picked up is freed right here
//                    drainRetired()   frees objects the audio thread has swapped out
//                                     (post() drains too)
//   audio thread:    take()           returns the newest object (or the previous one
//                                     until a new one arrives); never allocates, frees,
//                                     locks or blocks
//
// Each posted object travels in a node that has exactly one owner at a time:
// the producer, the slot, the consumer (as "current"), or the retired ring. The
// audio thread therefore never drops the last reference to anything. If the
// retired ring is full (the message thread stopped draining), take() keeps the
// current object until there is room again rather than freeing anything itself.
//
// Destroy the HandOff only once the audio thread no longer calls take().

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

namespace verdant
{

template <class T>
class HandOff
{
public:
    HandOff() = default;
    HandOff (const HandOff&) = delete;
    HandOff& operator= (const HandOff&) = delete;

    ~HandOff()
    {
        delete slot.exchange (nullptr, std::memory_order_acquire);
        delete current;
        drainRetired();
    }

    /** Message thread. A null object is a valid value (take() then returns nullptr). */
    void post (std::shared_ptr<const T> object)
    {
        drainRetired();
        auto* node = new Node { std::move (object) };
        delete slot.exchange (node, std::memory_order_acq_rel);   // an object the audio thread never took
    }

    /** Audio thread: the newest posted object, or nullptr before the first post. */
    const T* take() noexcept
    {
        if (current == nullptr || canRetire())
        {
            if (Node* fresh = slot.exchange (nullptr, std::memory_order_acquire))
            {
                if (current != nullptr) retire (current);
                current = fresh;
            }
        }
        return current != nullptr ? current->object.get() : nullptr;
    }

    /** Message thread: frees the objects the audio thread has swapped out. */
    void drainRetired()
    {
        auto r = readIndex.load (std::memory_order_relaxed);
        const auto w = writeIndex.load (std::memory_order_acquire);
        while (r != w)
        {
            delete ring[r % kRingSize];
            ring[r % kRingSize] = nullptr;
            ++r;
        }
        readIndex.store (r, std::memory_order_release);
    }

private:
    struct Node { std::shared_ptr<const T> object; };

    static constexpr uint32_t kRingSize = 32;

    bool canRetire() const noexcept
    {
        return writeIndex.load (std::memory_order_relaxed) - readIndex.load (std::memory_order_acquire) < kRingSize;
    }

    void retire (Node* node) noexcept
    {
        const auto w = writeIndex.load (std::memory_order_relaxed);
        ring[w % kRingSize] = node;
        writeIndex.store (w + 1, std::memory_order_release);
    }

    std::atomic<Node*> slot { nullptr };          // producer -> consumer
    Node* current = nullptr;                      // consumer only
    std::array<Node*, kRingSize> ring {};         // consumer -> producer (retired nodes)
    std::atomic<uint32_t> writeIndex { 0 }, readIndex { 0 };
};

} // namespace verdant
