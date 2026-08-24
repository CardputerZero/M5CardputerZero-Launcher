#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace SettingsAsync {

class Dispatch {
public:
    struct Item {
        uint64_t generation;
        std::function<void()> callback;
    };

    struct State {
        mutable std::mutex mutex;
        bool accepting = true;
        uint64_t generation = 1;
        std::deque<Item> pending;
    };

    struct Token {
        std::weak_ptr<State> state;
        uint64_t generation = 0;

        bool valid() const noexcept
        {
            return Dispatch::token_is_current(*this);
        }
    };

    Dispatch() : state_(std::make_shared<State>())
    {
    }

    Dispatch(const Dispatch &) = delete;
    Dispatch &operator=(const Dispatch &) = delete;

    ~Dispatch()
    {
        cancel();
    }

    Token token() const noexcept
    {
        auto state = state_;
        if (!state) return {};

        std::lock_guard<std::mutex> lock(state->mutex);
        if (!state->accepting) return {};
        return Token{state, state->generation};
    }

    bool enqueue(std::function<void()> callback)
    {
        return enqueue(token(), std::move(callback));
    }

    bool enqueue(Token callback_token, std::function<void()> callback)
    {
        if (!callback) return false;

        auto state = callback_token.state.lock();
        if (!state) return false;

        try {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (!state->accepting || callback_token.generation == 0 ||
                callback_token.generation != state->generation)
                return false;
            state->pending.push_back(Item{callback_token.generation, std::move(callback)});
        } catch (...) {
            return false;
        }
        return true;
    }

    std::size_t drain(std::size_t limit = 0)
    {
        auto state = state_;
        if (!state) return 0;

        std::deque<Item> pending;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (!state->accepting) return 0;
            if (limit == 0 || state->pending.size() <= limit) {
                pending.swap(state->pending);
            } else {
                for (std::size_t index = 0; index < limit; ++index) {
                    pending.emplace_back(std::move(state->pending.front()));
                    state->pending.pop_front();
                }
            }
        }

        std::size_t processed = 0;
        while (!pending.empty()) {
            Item item = std::move(pending.front());
            pending.pop_front();
            if (!token_is_current(Token{state, item.generation})) continue;
            try {
                item.callback();
            } catch (...) {
            }
            ++processed;
        }
        return processed;
    }

    uint64_t advance_generation() noexcept
    {
        auto state = state_;
        if (!state) return 0;

        std::lock_guard<std::mutex> lock(state->mutex);
        if (!state->accepting) return 0;
        ++state->generation;
        if (state->generation == 0) state->generation = 1;
        state->pending.clear();
        return state->generation;
    }

    void cancel() noexcept
    {
        auto state = state_;
        if (!state) return;

        std::lock_guard<std::mutex> lock(state->mutex);
        state->accepting = false;
        ++state->generation;
        if (state->generation == 0) state->generation = 1;
        state->pending.clear();
    }

    bool accepting() const noexcept
    {
        auto state = state_;
        if (!state) return false;

        std::lock_guard<std::mutex> lock(state->mutex);
        return state->accepting;
    }

    std::size_t pending() const noexcept
    {
        auto state = state_;
        if (!state) return 0;

        std::lock_guard<std::mutex> lock(state->mutex);
        return state->pending.size();
    }

    static bool token_is_current(const Token &callback_token) noexcept
    {
        auto state = callback_token.state.lock();
        if (!state || callback_token.generation == 0) return false;

        std::lock_guard<std::mutex> lock(state->mutex);
        return state->accepting && callback_token.generation == state->generation;
    }

    static bool enqueue_from_callback(Token callback_token, std::function<void()> callback) noexcept
    {
        if (!callback) return false;

        auto state = callback_token.state.lock();
        if (!state) return false;

        try {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (!state->accepting || callback_token.generation == 0 ||
                callback_token.generation != state->generation)
                return false;
            state->pending.push_back(Item{callback_token.generation, std::move(callback)});
        } catch (...) {
            return false;
        }
        return true;
    }

private:
    std::shared_ptr<State> state_;
};

class TaskRegistry {
public:
    using StopToken = std::shared_ptr<std::atomic_bool>;

    TaskRegistry() = default;

    TaskRegistry(const TaskRegistry &) = delete;
    TaskRegistry &operator=(const TaskRegistry &) = delete;

    ~TaskRegistry()
    {
        cancel();
        join_all();
    }

    template <typename Function>
    bool start(Function &&function)
    {
        using Work = typename std::decay<Function>::type;

        StopToken stop_token;
        try {
            stop_token = std::make_shared<std::atomic_bool>(false);
        } catch (...) {
            return false;
        }

        std::thread worker;
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_.reserve(tasks_.size() + 1);

            Work work(std::forward<Function>(function));
            auto finished = std::make_shared<std::atomic_bool>(false);
            worker = std::thread([stop_token, finished, work = std::move(work)]() mutable {
                try {
                    invoke_work(work, stop_token);
                } catch (...) {
                }
                finished->store(true, std::memory_order_release);
            });
            tasks_.emplace_back(std::move(worker), std::move(stop_token), std::move(finished));
        } catch (...) {
            if (stop_token) stop_token->store(true, std::memory_order_release);
            if (worker.joinable()) worker.join();
            return false;
        }
        return true;
    }

    void cancel() noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto &task : tasks_) {
            if (task.stop_token) task.stop_token->store(true, std::memory_order_release);
        }
    }

    void reap_finished()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto task = tasks_.begin();
        while (task != tasks_.end()) {
            if (!task->finished || !task->finished->load(std::memory_order_acquire)) {
                ++task;
                continue;
            }
            join_task(task->worker);
            task = tasks_.erase(task);
        }
    }

    void join_all() noexcept
    {
        std::vector<std::thread> workers;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            workers.reserve(tasks_.size());
            for (auto &task : tasks_) workers.emplace_back(std::move(task.worker));
            tasks_.clear();
        }
        for (auto &worker : workers) join_task(worker);
    }

    std::size_t tracked() const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return tasks_.size();
    }

private:
    template <typename Function>
    static void invoke_work(Function &function, const StopToken &stop_token)
    {
        if constexpr (std::is_invocable<Function &, StopToken>::value)
            function(stop_token);
        else
            function();
    }

    static void join_task(std::thread &worker) noexcept
    {
        if (!worker.joinable()) return;
        if (worker.get_id() == std::this_thread::get_id()) {
            worker.detach();
            return;
        }
        try {
            worker.join();
        } catch (...) {
            if (worker.joinable()) worker.detach();
        }
    }

    struct Task {
        Task(std::thread task_worker, StopToken task_stop_token,
             std::shared_ptr<std::atomic_bool> task_finished) noexcept
            : worker(std::move(task_worker)), stop_token(std::move(task_stop_token)),
              finished(std::move(task_finished))
        {
        }

        Task(Task &&) noexcept = default;
        Task &operator=(Task &&) noexcept = default;

        std::thread worker;
        StopToken stop_token;
        std::shared_ptr<std::atomic_bool> finished;
    };

    mutable std::mutex mutex_;
    std::vector<Task> tasks_;
};

using SettingsAsyncTaskRegistry = TaskRegistry;

}  // namespace SettingsAsync

using SettingsAsyncDispatch    = SettingsAsync::Dispatch;
using SettingsAsyncTaskRegistry = SettingsAsync::TaskRegistry;
using SettingsAsyncToken       = SettingsAsyncDispatch::Token;

inline bool settings_async_post(SettingsAsyncToken token, std::function<void()> callback) noexcept
{
    return SettingsAsyncDispatch::enqueue_from_callback(std::move(token), std::move(callback));
}
