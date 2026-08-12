#pragma once

#include "../include/cp0_bounded_task_registry.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace cp0::update {

struct Result {
    int code = -1;
    std::string stage = "unknown";
};

class Jobs {
public:
    using Operation = std::function<Result(const std::atomic<bool> &cancel_requested)>;

    std::string start(Operation operation)
    {
        const std::string id = std::to_string(next_.fetch_add(1));
        auto job = std::make_shared<Job>();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            jobs_[id] = job;
        }
        std::lock_guard<std::mutex> task_lock(task_mutex_);
        const bool started = tasks_.start([job, operation = std::move(operation)]() mutable {
            Result result;
            try { result = operation ? operation(job->cancel_requested) : Result{}; } catch (...) {}
            {
                std::lock_guard<std::mutex> lock(job->mutex);
                job->stage = std::move(result.stage);
            }
            job->result.store(result.code);
            job->done.store(true);
        });
        if (!started) {
            std::lock_guard<std::mutex> lock(job->mutex);
            job->stage = "thread-start";
            job->result.store(-1);
            job->done.store(true);
        }
        return id;
    }

    std::string start(std::function<Result()> operation)
    {
        return start([operation = std::move(operation)](const std::atomic<bool> &) mutable {
            return operation ? operation() : Result{};
        });
    }

    bool status(const std::string &id, std::string &value)
    {
        {
            std::lock_guard<std::mutex> lock(task_mutex_);
            tasks_.reap_finished();
        }
        std::shared_ptr<Job> job;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto found = jobs_.find(id);
            if (found == jobs_.end()) return false;
            job = found->second;
        }
        if (!job->done.load()) value = "running";
        else {
            std::string stage;
            { std::lock_guard<std::mutex> lock(job->mutex); stage = job->stage; }
            if (job->result.load() == 0) value = "succeeded:" + stage;
            else value = "failed:" + stage + ":" + std::to_string(job->result.load());
            std::lock_guard<std::mutex> lock(mutex_);
            jobs_.erase(id);
        }
        return true;
    }

    bool cancel(const std::string &id)
    {
        {
            std::lock_guard<std::mutex> lock(task_mutex_);
            tasks_.reap_finished();
        }
        std::shared_ptr<Job> job;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto found = jobs_.find(id);
            if (found == jobs_.end()) return false;
            job = found->second;
        }
        job->cancel_requested.store(true);
        return true;
    }

private:
    struct Job {
        std::atomic<bool> done{false};
        std::atomic<bool> cancel_requested{false};
        std::atomic<int> result{-1};
        std::mutex mutex;
        std::string stage = "starting";
    };
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<Job>> jobs_;
    std::atomic<unsigned long> next_{1};
    std::mutex task_mutex_;
    Cp0BoundedTaskRegistry tasks_;
};

} // namespace cp0::update
