#pragma once

#include "cp0_lvgl_app.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cp0_sudo {

enum class State { RESERVED, QUEUED, PROMPT, RUNNING, TERMINAL };
enum class ActionType { SHOW_PROMPT, SHOW_AUTH_ERROR, DESTROY_PROMPT, START_WORKER,
                        CALL_OUTPUT, CALL_COMPLETE };
enum class OutputResult { ACCEPTED, WOULD_BLOCK, TERMINAL, TOO_LARGE };

struct Request {
    uint64_t id = 0;
    State state = State::QUEUED;
    std::vector<std::string> argv;
    bool use_login_shell = false;
    cp0_sudo_callback_thread_t callback_thread = CP0_SUDO_CALLBACK_LVGL;
    cp0_sudo_output_cb_t output_cb = nullptr;
    cp0_sudo_complete_cb_t complete_cb = nullptr;
    void *user = nullptr;
    int auth_attempts = 0;
    int auth_timeout_ms = 0;
    int exec_timeout_ms = 0;
    int queue_timeout_ms = 0;
    int64_t deadline_ms = 0;
    std::atomic<bool> cancel_requested{false};
    bool authentication_complete = false;
    size_t pending_bytes = 0;
};

struct Action {
    ActionType type;
    std::shared_ptr<Request> request;
    std::string data;
    cp0_sudo_result_t result = CP0_SUDO_RESULT_EXEC_FAILED;
    int exit_code = -1;

    Action(ActionType action_type, std::shared_ptr<Request> action_request,
           std::string action_data = {},
           cp0_sudo_result_t action_result = CP0_SUDO_RESULT_EXEC_FAILED,
           int action_exit_code = -1)
        : type(action_type), request(std::move(action_request)),
          data(std::move(action_data)), result(action_result),
          exit_code(action_exit_code) {}
};

struct Budget { size_t events = 64; size_t output_bytes = 64 * 1024; };

class Coordinator {
public:
    explicit Coordinator(size_t pending_limit = 256 * 1024,
                         size_t terminal_limit = 256);
    bool reserve(std::shared_ptr<Request> request);
    std::vector<Action> commit_reserved(uint64_t id, int64_t now_ms);
    std::vector<Action> release_reserved(uint64_t id, int64_t now_ms);
    std::vector<Action> enqueue(std::shared_ptr<Request> request, int64_t now_ms);
    std::vector<Action> begin_shutdown(int error, int64_t now_ms);
    void requeue_actions(std::vector<Action> actions);
    std::vector<Action> fail_all(int error, int64_t now_ms);
    int cancel(uint64_t id, std::vector<Action> &actions, int64_t now_ms);
    std::vector<Action> tick(int64_t now_ms, Budget budget);
    std::vector<Action> submit_password(uint64_t id);
    std::vector<Action> worker_auth_result(uint64_t id, cp0_sudo_result_t result,
                                           int exit_code, int64_t now_ms);
    std::vector<Action> worker_authenticated(uint64_t id);
    OutputResult worker_output(uint64_t id, std::string data);
    void output_delivered(uint64_t id, size_t size);
    void worker_complete(uint64_t id, cp0_sudo_result_t result, int exit_code);
    std::shared_ptr<Request> find(uint64_t id) const;
    State state(uint64_t id) const;
    bool accepting() const;
    bool resume();
    size_t max_output_chunk() const { return pending_limit_; }

private:
    struct Pending {
        std::shared_ptr<Request> request;
        std::string data;
        cp0_sudo_result_t result = CP0_SUDO_RESULT_EXEC_FAILED;
        int exit_code = -1;
        bool completion = false;
    };
    std::vector<Action> start_next_locked(int64_t now_ms);
    void expire_queued_locked(int64_t now_ms);
    std::vector<Action> promote_reservations_locked(int64_t now_ms);
    void terminal_locked(std::shared_ptr<Request> request,
                         cp0_sudo_result_t result, int exit_code);
    void remember_terminal_locked(uint64_t id);

    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, std::shared_ptr<Request>> requests_;
    std::deque<std::shared_ptr<Request>> reservations_;
    std::unordered_map<uint64_t, bool> reservation_ready_;
    std::unordered_map<uint64_t, std::weak_ptr<Request>> delivery_requests_;
    std::deque<std::shared_ptr<Request>> queue_;
    std::shared_ptr<Request> current_;
    std::deque<Action> controls_;
    std::deque<Pending> callbacks_;
    std::deque<uint64_t> terminal_order_;
    std::unordered_map<uint64_t, bool> terminals_;
    size_t pending_limit_;
    size_t terminal_limit_;
    size_t terminal_completions_pending_ = 0;
    bool accepting_ = true;
};

} // namespace cp0_sudo
