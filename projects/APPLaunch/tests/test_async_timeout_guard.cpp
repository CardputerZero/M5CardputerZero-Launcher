#include "../main/ui/model/async_timeout_guard.hpp"

#include <cassert>
#include <functional>

int main()
{
    // Complete before timeout: on_timeout never fires, timer stopped.
    {
        AsyncTimeoutGuard guard;
        std::function<void()> pending;
        int stopped = 0;
        guard.set_timer_hooks(
            [&](uint32_t ms, std::function<void()> cb) {
                assert(ms == 3000);
                pending = std::move(cb);
            },
            [&] { ++stopped; });

        int fired = 0;
        guard.begin(3000, [&] { ++fired; });
        assert(guard.pending());
        assert(guard.complete());
        assert(!guard.pending());
        assert(fired == 0);
        assert(stopped == 1);
        // A stale timer firing after complete is a no-op.
        pending();
        assert(fired == 0);
    }

    // Timeout fires exactly once.
    {
        AsyncTimeoutGuard guard;
        std::function<void()> pending;
        int stopped = 0;
        guard.set_timer_hooks(
            [&](uint32_t, std::function<void()> cb) { pending = std::move(cb); },
            [&] { ++stopped; });
        int fired = 0;
        guard.begin(100, [&] { ++fired; });
        assert(guard.pending());
        pending();
        assert(fired == 1);
        assert(!guard.pending());
        pending(); // second firing is ignored
        assert(fired == 1);
        // complete() after timeout returns false.
        assert(!guard.complete());
    }

    // cancel suppresses the timeout.
    {
        AsyncTimeoutGuard guard;
        std::function<void()> pending;
        int stopped = 0;
        guard.set_timer_hooks(
            [&](uint32_t, std::function<void()> cb) { pending = std::move(cb); },
            [&] { ++stopped; });
        int fired = 0;
        guard.begin(50, [&] { ++fired; });
        guard.cancel();
        assert(!guard.pending());
        pending();
        assert(fired == 0);
        assert(stopped == 1);
    }

    // begin() replaces a previous pending guard.
    {
        AsyncTimeoutGuard guard;
        std::function<void()> pending;
        int starts = 0;
        int stops = 0;
        guard.set_timer_hooks(
            [&](uint32_t, std::function<void()> cb) {
                ++starts;
                pending = std::move(cb);
            },
            [&] { ++stops; });
        int first_fired = 0;
        int second_fired = 0;
        guard.begin(10, [&] { ++first_fired; });
        guard.begin(20, [&] { ++second_fired; });
        assert(starts == 2);
        assert(stops == 1);
        pending();
        assert(first_fired == 0);
        assert(second_fired == 1);
    }

    return 0;
}
