// HandOff: the audio thread always sees the newest object and never frees
// anything; objects are released on the message thread exactly once.

#include "TestHarness.h"

#include "engine/HandOff.h"

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

using verdant::HandOff;

namespace
{
    struct Obj
    {
        explicit Obj (int v) : value (v), check (v * 3 + 1) { alive.fetch_add (1); }
        ~Obj() { alive.fetch_sub (1); }
        int value, check;
        static std::atomic<int> alive;
    };
    std::atomic<int> Obj::alive { 0 };
}

AP_TEST (handoff_single_thread_sequence)
{
    {
        HandOff<Obj> h;
        CHECK (h.take() == nullptr);

        auto a = std::make_shared<Obj> (1);
        std::weak_ptr<Obj> wa = a;
        h.post (std::move (a));
        const Obj* got = h.take();
        CHECK (got != nullptr && got->value == 1);
        CHECK (h.take() == got);                       // stays current until something new arrives

        h.post (std::make_shared<Obj> (2));
        CHECK (h.take()->value == 2);
        CHECK (! wa.expired());                        // retired, but only the message thread frees it
        h.drainRetired();
        CHECK (wa.expired());

        // An object that was never taken is freed by the next post.
        auto c = std::make_shared<Obj> (3);
        std::weak_ptr<Obj> wc = c;
        h.post (std::move (c));
        h.post (std::make_shared<Obj> (4));
        CHECK (wc.expired());
        CHECK (h.take()->value == 4);

        // The same object posted twice survives the retirement of its first node.
        auto e = std::make_shared<Obj> (5);
        std::weak_ptr<Obj> we = e;
        h.post (e);
        CHECK (h.take()->value == 5);
        h.post (e);
        e.reset();
        CHECK (h.take()->value == 5);
        h.drainRetired();
        CHECK (! we.expired());

        // Null is a value too.
        h.post (nullptr);
        CHECK (h.take() == nullptr);
        h.drainRetired();
        CHECK (we.expired());

        h.post (std::make_shared<Obj> (6));
        CHECK (h.take()->value == 6);
    }
    CHECK_EQ (Obj::alive.load(), 0);                  // the destructor releases everything
}

AP_TEST (handoff_many_posts_keep_one_alive)
{
    HandOff<Obj> h;
    int lastSeen = -1;
    for (int i = 0; i < 100; ++i)
    {
        h.post (std::make_shared<Obj> (i));
        lastSeen = h.take()->value;
    }
    CHECK_EQ (lastSeen, 99);
    h.drainRetired();
    CHECK_EQ (Obj::alive.load(), 1);   // retired objects are gone, the current one stays
}

AP_TEST (handoff_two_thread_smoke)
{
    constexpr int kPosts = 20000;
    {
        HandOff<Obj> h;
        std::atomic<bool> done { false };
        std::atomic<int> errors { 0 };
        std::atomic<int> lastValue { -1 };

        std::thread audio ([&]
        {
            int last = -1;
            while (! done.load (std::memory_order_acquire) || last < kPosts - 1)
            {
                if (const Obj* o = h.take())
                {
                    if (o->check != o->value * 3 + 1 || o->value < last) errors.fetch_add (1);
                    last = o->value;
                }
            }
            lastValue.store (last);
        });

        for (int i = 0; i < kPosts; ++i)
        {
            h.post (std::make_shared<Obj> (i));
            if ((i & 63) == 0) h.drainRetired();
        }
        done.store (true, std::memory_order_release);
        audio.join();

        CHECK_EQ (errors.load(), 0);
        CHECK_EQ (lastValue.load(), kPosts - 1);
        h.drainRetired();
        CHECK_EQ (Obj::alive.load(), 1);                // only the current object is still alive
    }
    CHECK_EQ (Obj::alive.load(), 0);
}
