#include "../src/cp0_sudo_coordinator.hpp"
#include "cp0_dispatch_testable.hpp"

#include <cassert>
#include <cerrno>
#include <functional>
#include <thread>

using namespace cp0_sudo;

static std::shared_ptr<Request> request(uint64_t id, int timeout = 0)
{
    auto value = std::make_shared<Request>();
    value->id = id;
    value->auth_timeout_ms = timeout;
    return value;
}

static bool has(const std::vector<Action> &actions, ActionType type, uint64_t id)
{
    for (const auto &action : actions)
        if (action.type == type && action.request->id == id) return true;
    return false;
}

int main()
{
    {
        Coordinator c;
        auto reserved = request(1);
        assert(c.reserve(reserved));
        std::function<void()> delayed;
        assert(cp0_testable::dispatch_task(
            [&](auto task) { delayed = task; return true; },
            [&] { c.commit_reserved(1, 0); }));
        std::vector<Action> actions;
        assert(c.cancel(1, actions, 0) == 0 && actions.empty());
        delayed();
        auto done = c.tick(0, {});
        assert(!has(done, ActionType::SHOW_PROMPT, 1));
        assert(has(done, ActionType::CALL_COMPLETE, 1));
        for (const auto &action : done)
            if (action.type == ActionType::CALL_COMPLETE)
                assert(action.result == CP0_SUDO_RESULT_CANCELLED);
    }
    {
        Coordinator c;
        assert(c.reserve(request(1)));
        assert(c.reserve(request(2)));
        assert(c.commit_reserved(2, 0).empty());
        auto first = c.commit_reserved(1, 0);
        assert(has(first, ActionType::SHOW_PROMPT, 1));
        std::vector<Action> cancel_actions;
        assert(c.cancel(1, cancel_actions, 0) == 0);
        auto next = c.tick(0, {});
        assert(has(next, ActionType::CALL_COMPLETE, 1));
        assert(has(next, ActionType::SHOW_PROMPT, 2));
    }
    {
        Coordinator c;
        assert(c.reserve(request(1)));
        assert(c.reserve(request(2)));
        assert(c.commit_reserved(2, 0).empty());
        auto promoted = c.release_reserved(1, 0);
        assert(has(promoted, ActionType::SHOW_PROMPT, 2));
        std::vector<Action> ignored;
        assert(c.cancel(1, ignored, 0) == -ENOENT);
    }
    {
        Coordinator c;
        assert(has(c.enqueue(request(1), 0), ActionType::SHOW_PROMPT, 1));
        assert(c.enqueue(request(2), 0).empty());
        std::vector<Action> actions;
        assert(c.cancel(1, actions, 0) == 0);
        assert(has(actions, ActionType::DESTROY_PROMPT, 1));
        auto completed = c.tick(0, {});
        assert(has(completed, ActionType::CALL_COMPLETE, 1));
        assert(has(completed, ActionType::SHOW_PROMPT, 2));
    }
    {
        Coordinator c;
        c.enqueue(request(1), 0);
        c.enqueue(request(2), 0);
        std::vector<Action> actions;
        assert(c.cancel(2, actions, 0) == 0 && actions.empty());
        auto delivered = c.tick(0, {});
        assert(has(delivered, ActionType::CALL_COMPLETE, 2));
    }
    {
        Coordinator c;
        c.enqueue(request(1, 10), 100);
        assert(c.tick(109, {}).empty());
        auto timed = c.tick(110, {});
        assert(has(timed, ActionType::DESTROY_PROMPT, 1));
        assert(has(c.tick(110, {}), ActionType::CALL_COMPLETE, 1));
    }
    {
        Coordinator c;
        assert(has(c.enqueue(request(1), 0), ActionType::SHOW_PROMPT, 1));
        auto queued = request(2);
        queued->queue_timeout_ms = 10;
        assert(c.enqueue(queued, 0).empty());
        assert(c.tick(9, {}).empty());
        auto timed = c.tick(10, {});
        assert(has(timed, ActionType::CALL_COMPLETE, 2));
        for (const auto &action : timed) {
            if (action.type == ActionType::CALL_COMPLETE && action.request->id == 2) {
                assert(action.result == CP0_SUDO_RESULT_TIMED_OUT);
                assert(action.exit_code == -ETIMEDOUT);
            }
        }
        assert(c.state(1) == State::PROMPT);
    }
    {
        Coordinator c;
        c.enqueue(request(1), 0);
        assert(has(c.submit_password(1), ActionType::START_WORKER, 1));
        std::vector<Action> actions;
        assert(c.cancel(1, actions, 0) == 0 && actions.empty());
        c.worker_complete(1, CP0_SUDO_RESULT_SUCCESS, 0);
        auto done = c.tick(0, {});
        assert(has(done, ActionType::CALL_COMPLETE, 1));
        for (const auto &a : done) if (a.type == ActionType::CALL_COMPLETE)
            assert(a.result == CP0_SUDO_RESULT_CANCELLED);
    }
    {
        Coordinator c;
        auto r = request(1, 100);
        c.enqueue(r, 0);
        for (int attempt = 1; attempt <= 2; ++attempt) {
            c.submit_password(1);
            auto retry = c.worker_auth_result(1, CP0_SUDO_RESULT_AUTH_FAILED, 1, 20 * attempt);
            assert(has(retry, ActionType::SHOW_AUTH_ERROR, 1));
            assert(r->deadline_ms == 100);
        }
        c.submit_password(1);
        c.worker_auth_result(1, CP0_SUDO_RESULT_AUTH_FAILED, 1, 60);
        auto done = c.tick(60, {});
        assert(has(done, ActionType::CALL_COMPLETE, 1));
    }
    {
        Coordinator c;
        c.enqueue(request(1), 0);
        c.enqueue(request(2), 0);
        std::vector<Action> actions;
        assert(c.cancel(1, actions, 0) == 0);
        c.requeue_actions(std::move(actions));
        auto first = c.tick(0, {});
        assert(first.size() >= 3);
        assert(first[0].type == ActionType::DESTROY_PROMPT);
        assert(has(first, ActionType::CALL_COMPLETE, 1));
        assert(first.back().type == ActionType::SHOW_PROMPT);
        assert(first.back().request->id == 2);
    }
    {
        Coordinator c(16, 2);
        c.enqueue(request(1), 0);
        c.submit_password(1);
        auto authenticated = c.worker_authenticated(1);
        assert(authenticated.size() == 1);
        assert(authenticated[0].type == ActionType::DESTROY_PROMPT);
        assert(c.worker_authenticated(1).empty());
        assert(c.worker_output(1, "one") == OutputResult::ACCEPTED);
        assert(c.worker_output(1, "two") == OutputResult::ACCEPTED);
        c.worker_complete(1, CP0_SUDO_RESULT_SUCCESS, 0);
        auto all = c.tick(0, {});
        assert(all.size() == 3);
        assert(all[0].type == ActionType::CALL_OUTPUT);
        assert(all[1].type == ActionType::CALL_OUTPUT);
        assert(all[2].type == ActionType::CALL_COMPLETE);
        c.output_delivered(1, 3);
        c.output_delivered(1, 3);
        c.worker_complete(1, CP0_SUDO_RESULT_SUCCESS, 0);
        assert(c.tick(0, {}).empty());
        std::vector<Action> actions;
        assert(c.cancel(1, actions, 0) == 0);
        c.enqueue(request(2), 0); c.cancel(2, actions, 0);
        c.enqueue(request(3), 0); c.cancel(3, actions, 0);
        assert(c.cancel(1, actions, 0) == -ENOENT);
    }
    {
        Coordinator c(4, 4);
        c.enqueue(request(1), 0);
        c.submit_password(1);
        assert(c.worker_output(1, "1234") == OutputResult::ACCEPTED);
        auto delivery = c.tick(0, {});
        assert(has(delivery, ActionType::CALL_OUTPUT, 1));
        assert(c.worker_output(1, "x") == OutputResult::WOULD_BLOCK);
        c.output_delivered(1, 4);
        assert(c.worker_output(1, "x") == OutputResult::ACCEPTED);
        assert(c.worker_output(1, "12345") == OutputResult::TOO_LARGE);
        c.worker_complete(1, CP0_SUDO_RESULT_SUCCESS, 0);
        assert(c.worker_output(1, "x") == OutputResult::TERMINAL);
    }
    {
        Coordinator c(0, 4);
        c.enqueue(request(1), 0);
        c.submit_password(1);
        assert(c.max_output_chunk() == 1);
        assert(c.worker_output(1, "x") == OutputResult::ACCEPTED);
        assert(c.worker_output(1, "y") == OutputResult::WOULD_BLOCK);
        assert(c.worker_output(1, "zz") == OutputResult::TOO_LARGE);
    }
    {
        Coordinator c;
        auto running = request(1);
        c.enqueue(running, 0);
        c.enqueue(request(2), 0);
        c.submit_password(1);
        auto controls = c.fail_all(-ENOMEM, 0);
        assert(has(controls, ActionType::DESTROY_PROMPT, 1));
        assert(running->cancel_requested.load());
        auto done = c.tick(0, {});
        int completes = 0;
        for (const auto &action : done) {
            assert(action.type != ActionType::START_WORKER);
            if (action.type == ActionType::CALL_COMPLETE) {
                ++completes;
                assert(action.exit_code == -ENOMEM);
            }
        }
        assert(completes == 2);
    }
    {
        Coordinator c;
        auto running = request(1);
        auto queued = request(2);
        auto reserved = request(3);
        c.enqueue(running, 0);
        c.enqueue(queued, 0);
        assert(c.reserve(reserved));
        c.submit_password(1);

        auto shutdown = c.begin_shutdown(-ESHUTDOWN, 10);
        assert(!c.accepting());
        assert(has(shutdown, ActionType::DESTROY_PROMPT, 1));
        assert(running->cancel_requested.load());
        assert(c.find(1) == running);
        assert(c.find(2) == nullptr);
        assert(c.find(3) == nullptr);
        assert(!c.reserve(request(4)));
        assert(c.enqueue(request(5), 10).empty());
        assert(c.begin_shutdown(-EIO, 11).empty());

        auto early = c.tick(10, {});
        int early_completions = 0;
        for (const auto &action : early) {
            assert(action.type != ActionType::START_WORKER);
            if (action.type == ActionType::CALL_COMPLETE) {
                ++early_completions;
                assert(action.request->id == 2 || action.request->id == 3);
                assert(action.exit_code == -ESHUTDOWN);
            }
        }
        assert(early_completions == 2);

        c.worker_complete(1, CP0_SUDO_RESULT_SUCCESS, 0);
        c.worker_complete(1, CP0_SUDO_RESULT_SUCCESS, 0);
        auto late = c.tick(12, {});
        int late_completions = 0;
        for (const auto &action : late) {
            if (action.type == ActionType::CALL_COMPLETE) {
                ++late_completions;
                assert(action.request->id == 1);
                assert(action.result == CP0_SUDO_RESULT_CANCELLED);
                assert(action.exit_code == -ECANCELED);
            }
        }
        assert(late_completions == 1);
        assert(c.tick(12, {}).empty());
    }
    {
        Coordinator c;
        c.enqueue(request(1), 0);
        std::vector<Action> first;
        std::vector<Action> second;
        std::thread one([&] { first = c.begin_shutdown(-ESHUTDOWN, 1); });
        std::thread two([&] { second = c.begin_shutdown(-ESHUTDOWN, 1); });
        one.join();
        two.join();
        assert(has(first, ActionType::DESTROY_PROMPT, 1) !=
               has(second, ActionType::DESTROY_PROMPT, 1));
        auto completed = c.tick(1, {});
        int completion_count = 0;
        for (const auto &action : completed)
            if (action.type == ActionType::CALL_COMPLETE && action.request->id == 1)
                ++completion_count;
        assert(completion_count == 1);
        assert(c.tick(1, {}).empty());
    }
    {
        Coordinator c;
        c.enqueue(request(1), 0);
        auto shutdown = c.begin_shutdown(-ESHUTDOWN, 1);
        assert(has(shutdown, ActionType::DESTROY_PROMPT, 1));
        auto completed = c.tick(1, {});
        assert(has(completed, ActionType::CALL_COMPLETE, 1));
        assert(c.resume());
        assert(c.accepting());
        assert(has(c.enqueue(request(2), 2), ActionType::SHOW_PROMPT, 2));
    }
}
