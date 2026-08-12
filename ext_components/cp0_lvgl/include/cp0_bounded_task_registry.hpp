#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

class Cp0BoundedTaskRegistry
{
public:
    Cp0BoundedTaskRegistry() = default;
    ~Cp0BoundedTaskRegistry() { join_all(); }

    Cp0BoundedTaskRegistry(const Cp0BoundedTaskRegistry &) = delete;
    Cp0BoundedTaskRegistry &operator=(const Cp0BoundedTaskRegistry &) = delete;

    template <typename Function>
    bool start(Function &&function)
    {
        reap_finished();
        auto finished = std::make_shared<std::atomic<bool>>(false);
        try {
            tasks_.reserve(tasks_.size() + 1);
            std::thread worker([finished, work = std::forward<Function>(function)]() mutable {
                    struct Completion {
                        std::shared_ptr<std::atomic<bool>> flag;
                        ~Completion() { flag->store(true, std::memory_order_release); }
                    } completion{finished};
                    try {
                        work();
                    } catch (...) {
                    }
                });
            tasks_.emplace_back(std::move(worker), std::move(finished));
        } catch (...) {
            return false;
        }
        return true;
    }

    void reap_finished()
    {
        auto task = tasks_.begin();
        while (task != tasks_.end()) {
            if (!task->finished->load(std::memory_order_acquire)) {
                ++task;
                continue;
            }
            if (task->thread.joinable()) task->thread.join();
            task = tasks_.erase(task);
        }
    }

    void join_all()
    {
        for (auto &task : tasks_)
            if (task.thread.joinable()) task.thread.join();
        tasks_.clear();
    }

    std::size_t tracked() const { return tasks_.size(); }

private:
    struct Task {
        Task(std::thread worker, std::shared_ptr<std::atomic<bool>> completion) noexcept
            : thread(std::move(worker)), finished(std::move(completion))
        {
        }
        Task(Task &&) noexcept = default;
        Task &operator=(Task &&) noexcept = default;
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> finished;
    };
    std::vector<Task> tasks_;
};
