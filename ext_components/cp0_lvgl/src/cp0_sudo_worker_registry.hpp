#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace cp0_sudo {

enum class JoinResult {
    Joined,
    SelfJoin,
    AlreadyJoining,
};

class WorkerRegistry {
public:
    WorkerRegistry() = default;
    WorkerRegistry(const WorkerRegistry &) = delete;
    WorkerRegistry &operator=(const WorkerRegistry &) = delete;

    ~WorkerRegistry()
    {
        request_shutdown();
        if (join() != JoinResult::Joined) std::terminate();
    }

    template <typename Cancel, typename Worker>
    bool start(Cancel &&cancel, Worker &&worker)
    {
        return start(std::forward<Cancel>(cancel), std::forward<Worker>(worker), [] {});
    }

    template <typename Cancel, typename Worker, typename Failure>
    bool start(Cancel &&cancel, Worker &&worker, Failure &&failure)
    {
        auto entry = std::make_unique<Entry>();
        try {
            entry->cancel = std::forward<Cancel>(cancel);
        } catch (...) {
            return false;
        }

        Entry *raw = entry.get();
        std::unique_lock<std::mutex> lock(mutex_);
        if (!accepting_ || joining_) return false;
        std::vector<std::thread> finished;
        for (auto it = entries_.begin(); it != entries_.end();) {
            if ((*it)->finished) {
                if ((*it)->worker.joinable())
                    finished.push_back(std::move((*it)->worker));
                it = entries_.erase(it);
            } else {
                ++it;
            }
        }
        lock.unlock();
        for (auto &thread : finished) thread.join();
        lock.lock();
        if (!accepting_ || joining_) return false;
        ++active_;
        entries_.push_back(std::move(entry));
        try {
            raw->worker = std::thread(
                [this, raw, task = std::forward<Worker>(worker),
                 on_failure = std::forward<Failure>(failure)]() mutable noexcept {
                    current_registry_ = this;
                    try {
                        task();
                    } catch (...) {
                        try {
                            on_failure();
                        } catch (...) {
                        }
                    }
                    current_registry_ = nullptr;
                    worker_finished(raw);
                });
        } catch (...) {
            entries_.pop_back();
            --active_;
            idle_.notify_all();
            return false;
        }
        return true;
    }

    void request_shutdown() noexcept
    {
        std::vector<std::function<void()>> cancellations;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            accepting_ = false;
            if (shutdown_requested_) return;
            shutdown_requested_ = true;
            cancellations.reserve(entries_.size());
            for (const auto &entry : entries_)
                if (entry->cancel) cancellations.push_back(entry->cancel);
        }
        for (auto &cancel : cancellations) {
            try {
                cancel();
            } catch (...) {
            }
        }
    }

    JoinResult join() noexcept
    {
        std::vector<std::thread> workers;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            accepting_ = false;
            if (current_registry_ == this) return JoinResult::SelfJoin;
            const std::thread::id caller = std::this_thread::get_id();
            for (const auto &entry : entries_)
                if (entry->worker.joinable() && entry->worker.get_id() == caller)
                    return JoinResult::SelfJoin;
            if (joined_) return JoinResult::Joined;
            if (joining_) return JoinResult::AlreadyJoining;
            joining_ = true;
            idle_.wait(lock, [this] { return active_ == 0; });
            workers.reserve(entries_.size());
            for (auto &entry : entries_)
                if (entry->worker.joinable()) workers.push_back(std::move(entry->worker));
            entries_.clear();
        }
        for (auto &worker : workers) worker.join();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            joining_ = false;
            joined_ = true;
        }
        return JoinResult::Joined;
    }

    bool accepting() const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return accepting_;
    }

    std::size_t active() const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return active_;
    }

    std::size_t tracked() const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return entries_.size();
    }

    bool is_current_worker() const noexcept
    {
        return current_registry_ == this;
    }

private:
    struct Entry {
        std::function<void()> cancel;
        std::thread worker;
        bool finished = false;
    };

    void worker_finished(Entry *entry) noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (entry) entry->finished = true;
        --active_;
        idle_.notify_all();
    }

    mutable std::mutex mutex_;
    std::condition_variable idle_;
    std::vector<std::unique_ptr<Entry>> entries_;
    std::size_t active_ = 0;
    bool accepting_ = true;
    bool shutdown_requested_ = false;
    bool joining_ = false;
    bool joined_ = false;
    inline static thread_local WorkerRegistry *current_registry_ = nullptr;
};

} // namespace cp0_sudo
