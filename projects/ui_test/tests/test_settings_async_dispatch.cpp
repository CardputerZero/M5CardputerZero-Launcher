#include "settings_async_dispatch.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

int main()
{
    SettingsAsync::Dispatch dispatch;
    const auto owner_thread = std::this_thread::get_id();
    const auto token = dispatch.token();
    std::mutex callback_mutex;
    std::vector<std::thread::id> callback_threads;
    std::atomic<int> callback_count{0};

    std::thread producer([token, &callback_mutex, &callback_threads, &callback_count] {
        for (int index = 0; index < 32; ++index) {
            const bool queued = SettingsAsync::Dispatch::enqueue_from_callback(
                token,
                [&callback_mutex, &callback_threads, &callback_count] {
                    std::lock_guard<std::mutex> lock(callback_mutex);
                    callback_threads.push_back(std::this_thread::get_id());
                    callback_count.fetch_add(1, std::memory_order_relaxed);
                });
            assert(queued);
        }
    });
    producer.join();

    assert(dispatch.pending() == 32);
    assert(dispatch.drain() == 32);
    assert(callback_count.load(std::memory_order_relaxed) == 32);
    for (const auto thread_id : callback_threads) assert(thread_id == owner_thread);

    const auto stale_token = dispatch.token();
    dispatch.advance_generation();
    assert(!stale_token.valid());
    assert(!SettingsAsync::Dispatch::enqueue_from_callback(stale_token, [] {}));
    assert(dispatch.pending() == 0);

    const auto current_token = dispatch.token();
    assert(SettingsAsync::Dispatch::enqueue_from_callback(current_token, [] {}));
    dispatch.cancel();
    assert(dispatch.drain() == 0);
    assert(!current_token.valid());
    assert(!SettingsAsync::Dispatch::enqueue_from_callback(current_token, [] {}));

    SettingsAsync::TaskRegistry registry;
    std::atomic<bool> started{false};
    assert(registry.start([&started](SettingsAsync::TaskRegistry::StopToken stop_token) {
        started.store(true, std::memory_order_release);
        while (!stop_token->load(std::memory_order_acquire)) std::this_thread::yield();
    }));
    for (int attempt = 0; attempt < 100 && !started.load(std::memory_order_acquire); ++attempt)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(started.load(std::memory_order_acquire));
    registry.cancel();
    registry.join_all();
    assert(registry.tracked() == 0);

    std::atomic<bool> finished{false};
    assert(registry.start([&finished] { finished.store(true, std::memory_order_release); }));
    for (int attempt = 0; attempt < 100 && !finished.load(std::memory_order_acquire); ++attempt)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    registry.reap_finished();
    assert(finished.load(std::memory_order_acquire));
    assert(registry.tracked() == 0);
    return 0;
}
