#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace ap
{

/**
    Wait-free single-producer / single-consumer ring of trivially copyable
    items, for handing data from the audio thread to one other thread (a
    sender thread, an editor timer) without locks or allocation.

    push() is producer-only and drops the item when the ring is full; front(),
    pop() and clear() are consumer-only.
*/
template <typename T, size_t Capacity>
class SpscQueue
{
    static_assert (Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    /** Producer: appends `item`; returns false (and drops it) when full. */
    bool push (const T& item) noexcept
    {
        const size_t w = writeIndex.load (std::memory_order_relaxed);
        if (w - readIndex.load (std::memory_order_acquire) >= Capacity)
            return false;
        items[w & (Capacity - 1)] = item;
        writeIndex.store (w + 1, std::memory_order_release);
        return true;
    }

    /** Consumer: the oldest item, or nullptr when empty. Stays valid until pop(). */
    const T* front() const noexcept
    {
        const size_t r = readIndex.load (std::memory_order_relaxed);
        if (r == writeIndex.load (std::memory_order_acquire))
            return nullptr;
        return &items[r & (Capacity - 1)];
    }

    /** Consumer: removes the oldest item. Only call after front() returned non-null. */
    void pop() noexcept
    {
        readIndex.store (readIndex.load (std::memory_order_relaxed) + 1, std::memory_order_release);
    }

    /** Consumer: copies out and removes the oldest item; false when empty. */
    bool pop (T& out) noexcept
    {
        if (const T* f = front())
        {
            out = *f;
            pop();
            return true;
        }
        return false;
    }

    /** Consumer: discards everything queued so far. */
    void clear() noexcept
    {
        readIndex.store (writeIndex.load (std::memory_order_acquire), std::memory_order_release);
    }

    size_t size() const noexcept
    {
        return writeIndex.load (std::memory_order_acquire) - readIndex.load (std::memory_order_acquire);
    }

    static constexpr size_t capacity() noexcept { return Capacity; }

private:
    std::array<T, Capacity> items {};
    alignas (64) std::atomic<size_t> writeIndex { 0 };
    alignas (64) std::atomic<size_t> readIndex { 0 };
};

} // namespace ap
