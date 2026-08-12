#include "cp0_bounded_task_registry.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>

using namespace std::chrono_literals;

int main()
{
    Cp0BoundedTaskRegistry registry;
    std::atomic<bool> release{false};
    assert(registry.start([&] {
        while (!release.load()) std::this_thread::sleep_for(1ms);
    }));
    const auto reap_started = std::chrono::steady_clock::now();
    registry.reap_finished();
    assert(std::chrono::steady_clock::now() - reap_started < 50ms);
    assert(registry.tracked() == 1);

    std::atomic<unsigned> generation{1};
    std::atomic<unsigned> observed{0};
    std::atomic<bool> generation_task_started{false};
    assert(registry.start([&] {
        const unsigned mine = generation.load();
        generation_task_started.store(true);
        while (generation.load() == mine) std::this_thread::sleep_for(1ms);
        observed.store(mine);
    }));
    while (!generation_task_started.load()) std::this_thread::sleep_for(1ms);
    generation.store(2);
    for (int attempt = 0; attempt < 100 && observed.load() != 1; ++attempt)
        std::this_thread::sleep_for(1ms);
    assert(observed.load() == 1);
    registry.reap_finished();
    assert(registry.tracked() == 1);

    release.store(true);
    registry.join_all();
    assert(registry.tracked() == 0);

    std::atomic<bool> teardown_finished{false};
    const auto teardown_started = std::chrono::steady_clock::now();
    {
        Cp0BoundedTaskRegistry teardown;
        assert(teardown.start([&] {
            std::this_thread::sleep_for(30ms);
            teardown_finished.store(true);
        }));
    }
    assert(teardown_finished.load());
    assert(std::chrono::steady_clock::now() - teardown_started < 1s);
}
