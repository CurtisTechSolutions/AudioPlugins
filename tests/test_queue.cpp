#include "TestHarness.h"

#include <ap/SpscQueue.h>

#include <thread>

using namespace ap;

AP_TEST (spsc_queue_keeps_order_and_drops_when_full)
{
    SpscQueue<int, 8> q;
    CHECK (q.front() == nullptr);
    CHECK_EQ ((int) q.size(), 0);

    for (int i = 0; i < 8; ++i)
        CHECK (q.push (i));
    CHECK (! q.push (99));            // full: dropped, the queued items are untouched
    CHECK_EQ ((int) q.size(), 8);

    int v = -1;
    for (int i = 0; i < 8; ++i)
    {
        CHECK (q.pop (v));
        CHECK_EQ (v, i);
    }
    CHECK (! q.pop (v));
}

AP_TEST (spsc_queue_wraps_and_clears)
{
    SpscQueue<int, 4> q;
    int v = 0;
    // Many times round the ring: indices wrap, order is kept.
    for (int i = 0; i < 1000; ++i)
    {
        CHECK (q.push (i));
        CHECK (q.push (i + 1));
        CHECK (q.front() != nullptr && *q.front() == i);
        q.pop();
        CHECK (q.pop (v) && v == i + 1);
    }
    q.push (1); q.push (2); q.push (3);
    q.clear();
    CHECK (q.front() == nullptr);
    CHECK (q.push (7) && q.pop (v) && v == 7);
}

AP_TEST (spsc_queue_two_threads_lose_nothing_in_order)
{
    // The producer retries when full, so every value must arrive exactly once and in order.
    constexpr int count = 200000;
    SpscQueue<int, 64> q;
    std::thread producer ([&q]
    {
        for (int i = 0; i < count; ++i)
            while (! q.push (i)) std::this_thread::yield();
    });

    int expected = 0;
    bool inOrder = true;
    while (expected < count)
    {
        int v;
        if (q.pop (v))
        {
            inOrder = inOrder && v == expected;
            ++expected;
        }
        else
        {
            std::this_thread::yield();
        }
    }
    producer.join();
    CHECK (inOrder);
    CHECK_EQ (expected, count);
    CHECK (q.front() == nullptr);
}
