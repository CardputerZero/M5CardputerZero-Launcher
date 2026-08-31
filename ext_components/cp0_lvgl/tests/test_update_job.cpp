#include "../src/cp0_update_job.hpp"

#include <cassert>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>

int main()
{
    cp0::update::Jobs jobs;
    const std::string success = jobs.start([] { return cp0::update::Result{0, "installed"}; });
    const std::string failure = jobs.start([] { return cp0::update::Result{7, "checksum"}; });
    std::string state;
    for (int attempt = 0; attempt < 100; ++attempt) {
        assert(jobs.status(success, state));
        if (state != "running") break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(state == "succeeded:installed");
    assert(!jobs.status(success, state));

    std::atomic<bool> progress_started{false};
    const std::string progressing = jobs.start(
        [&](const std::atomic<bool> &) {
            progress_started.store(true);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            return cp0::update::Result{0, "installed"};
        },
        [&] { return progress_started.load() ? "downloading" : std::string(); });
    for (int attempt = 0; attempt < 100; ++attempt) {
        assert(jobs.status(progressing, state));
        if (state == "downloading") break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(state == "downloading");
    for (int attempt = 0; attempt < 100; ++attempt) {
        assert(jobs.status(progressing, state));
        if (state != "downloading" && state != "running") break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(state == "succeeded:installed");
    assert(!jobs.status(progressing, state));

    for (int attempt = 0; attempt < 100; ++attempt) {
        assert(jobs.status(failure, state));
        if (state != "running") break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(state == "failed:checksum:7");
    assert(!jobs.status(failure, state));
    assert(!jobs.status("missing", state));

    std::atomic<bool> cancellation_observed{false};
    const std::string abandoned = jobs.start([&](const std::atomic<bool> &cancel) {
        while (!cancel.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        cancellation_observed.store(true);
        return cp0::update::Result{-ECANCELED, "cancelled"};
    });
    assert(jobs.cancel(abandoned));
    for (int attempt = 0; attempt < 100 && !cancellation_observed.load(); ++attempt)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(cancellation_observed.load());
    assert(jobs.status(abandoned, state));
    assert(state == "cancelled");
    assert(!jobs.status(abandoned, state));
    assert(!jobs.cancel(abandoned));

    std::atomic<bool> teardown_finished{false};
    {
        cp0::update::Jobs owned;
        owned.start([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            teardown_finished.store(true);
            return cp0::update::Result{0, "owned"};
        });
    }
    assert(teardown_finished.load());
}
