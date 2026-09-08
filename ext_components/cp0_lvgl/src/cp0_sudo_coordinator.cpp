#include "cp0_sudo_coordinator.hpp"
#include "cp0_async_testable_utils.hpp"

#include <cerrno>

namespace cp0_sudo {

Coordinator::Coordinator(size_t pending_limit, size_t terminal_limit)
    : pending_limit_(pending_limit ? pending_limit : 1), terminal_limit_(terminal_limit) {}

bool Coordinator::reserve(std::shared_ptr<Request> request)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!accepting_ || !request || requests_.count(request->id) || terminals_.count(request->id))
        return false;
    request->state = State::RESERVED;
    requests_[request->id] = request;
    reservations_.push_back(request);
    reservation_ready_[request->id] = false;
    return true;
}

std::vector<Action> Coordinator::promote_reservations_locked(int64_t now_ms)
{
    while (!reservations_.empty()) {
        auto request = reservations_.front();
        auto ready = reservation_ready_.find(request->id);
        if (ready == reservation_ready_.end() || !ready->second) break;
        reservations_.pop_front();
        reservation_ready_.erase(ready);
        if (request->cancel_requested.load())
            terminal_locked(request, CP0_SUDO_RESULT_CANCELLED, -ECANCELED);
        else {
            request->state = State::QUEUED;
            queue_.push_back(request);
        }
    }
    return start_next_locked(now_ms);
}

std::vector<Action> Coordinator::commit_reserved(uint64_t id, int64_t now_ms)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto ready = reservation_ready_.find(id);
    if (ready == reservation_ready_.end()) return {};
    auto request = requests_.find(id);
    if (request != requests_.end() && request->second->queue_timeout_ms > 0)
        request->second->deadline_ms = now_ms + request->second->queue_timeout_ms;
    ready->second = true;
    return promote_reservations_locked(now_ms);
}

std::vector<Action> Coordinator::release_reserved(uint64_t id, int64_t now_ms)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = requests_.find(id);
    if (it == requests_.end() || it->second->state != State::RESERVED) return {};
    requests_.erase(it);
    reservation_ready_.erase(id);
    for (auto reservation = reservations_.begin(); reservation != reservations_.end(); ++reservation) {
        if ((*reservation)->id == id) { reservations_.erase(reservation); break; }
    }
    return promote_reservations_locked(now_ms);
}

void Coordinator::remember_terminal_locked(uint64_t id)
{
    if (!terminal_limit_ || terminals_.count(id)) return;
    terminals_[id] = true;
    terminal_order_.push_back(id);
    if (terminal_order_.size() > terminal_limit_) {
        terminals_.erase(terminal_order_.front());
        terminal_order_.pop_front();
    }
}

std::vector<Action> Coordinator::start_next_locked(int64_t now_ms)
{
    std::vector<Action> actions;
    while (accepting_ && !current_ && terminal_completions_pending_ == 0 && !queue_.empty()) {
        auto request = queue_.front();
        queue_.pop_front();
        if (request->cancel_requested.load()) {
            terminal_locked(request, CP0_SUDO_RESULT_CANCELLED, -ECANCELED);
            continue;
        }
        if (request->deadline_ms > 0 && now_ms >= request->deadline_ms) {
            terminal_locked(request, CP0_SUDO_RESULT_TIMED_OUT, -ETIMEDOUT);
            continue;
        }
        current_ = request;
        request->state = State::PROMPT;
        request->deadline_ms = request->auth_timeout_ms > 0
            ? now_ms + request->auth_timeout_ms : 0;
        actions.push_back({ActionType::SHOW_PROMPT, request});
    }
    return actions;
}

void Coordinator::terminal_locked(std::shared_ptr<Request> request,
                                  cp0_sudo_result_t result, int exit_code)
{
    if (!request || request->state == State::TERMINAL) return;
    request->state = State::TERMINAL;
    if (current_ == request) current_.reset();
    requests_.erase(request->id);
    remember_terminal_locked(request->id);
    callbacks_.push_back({request, {}, result, exit_code, true});
    ++terminal_completions_pending_;
}

std::vector<Action> Coordinator::enqueue(std::shared_ptr<Request> request, int64_t now_ms)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!accepting_ || !request || requests_.count(request->id) || terminals_.count(request->id))
        return {};
    request->state = State::QUEUED;
    request->deadline_ms = request->queue_timeout_ms > 0
        ? now_ms + request->queue_timeout_ms : 0;
    requests_[request->id] = request;
    queue_.push_back(request);
    return start_next_locked(now_ms);
}

std::vector<Action> Coordinator::begin_shutdown(int error, int64_t now_ms)
{
    std::lock_guard<std::mutex> lock(mutex_);
    (void)now_ms;
    if (!accepting_) return {};
    accepting_ = false;

    std::vector<Action> actions;
    if (current_) {
        auto request = current_;
        request->cancel_requested.store(true);
        if (request->state == State::RUNNING) {
            if (!request->authentication_complete) {
                request->authentication_complete = true;
                actions.push_back({ActionType::DESTROY_PROMPT, request});
            }
        } else {
            actions.push_back({ActionType::DESTROY_PROMPT, request});
            terminal_locked(request, CP0_SUDO_RESULT_EXEC_FAILED, error);
        }
    }
    while (!queue_.empty()) {
        auto request = queue_.front();
        queue_.pop_front();
        request->cancel_requested.store(true);
        terminal_locked(request, CP0_SUDO_RESULT_EXEC_FAILED, error);
    }
    while (!reservations_.empty()) {
        auto request = reservations_.front();
        reservations_.pop_front();
        reservation_ready_.erase(request->id);
        request->cancel_requested.store(true);
        terminal_locked(request, CP0_SUDO_RESULT_EXEC_FAILED, error);
    }
    return actions;
}

std::vector<Action> Coordinator::fail_all(int error, int64_t now_ms)
{
    std::lock_guard<std::mutex> lock(mutex_);
    (void)now_ms;
    std::vector<Action> actions;
    if (current_) {
        auto request = current_;
        request->cancel_requested.store(true);
        actions.push_back({ActionType::DESTROY_PROMPT, request});
        terminal_locked(request, CP0_SUDO_RESULT_EXEC_FAILED, error);
    }
    while (!queue_.empty()) {
        auto request = queue_.front();
        queue_.pop_front();
        request->cancel_requested.store(true);
        terminal_locked(request, CP0_SUDO_RESULT_EXEC_FAILED, error);
    }
    while (!reservations_.empty()) {
        auto request = reservations_.front();
        reservations_.pop_front();
        reservation_ready_.erase(request->id);
        request->cancel_requested.store(true);
        terminal_locked(request, CP0_SUDO_RESULT_EXEC_FAILED, error);
    }
    return actions;
}

void Coordinator::requeue_actions(std::vector<Action> actions)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = actions.rbegin(); it != actions.rend(); ++it)
        controls_.push_front(std::move(*it));
}

int Coordinator::cancel(uint64_t id, std::vector<Action> &actions, int64_t now_ms)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = requests_.find(id);
    if (it == requests_.end()) return terminals_.count(id) ? 0 : -ENOENT;
    auto request = it->second;
    request->cancel_requested.store(true);
    if (request->state == State::RESERVED) return 0;
    if (request->state == State::RUNNING) return 0;
    if (request == current_) actions.push_back({ActionType::DESTROY_PROMPT, request});
    terminal_locked(request, CP0_SUDO_RESULT_CANCELLED, -ECANCELED);
    (void)now_ms;
    return 0;
}

std::vector<Action> Coordinator::submit_password(uint64_t id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = requests_.find(id);
    if (it == requests_.end() || it->second->state != State::PROMPT) return {};
    auto request = it->second;
    request->state = State::RUNNING;
    ++request->auth_attempts;
    return {{ActionType::START_WORKER, request}};
}

std::vector<Action> Coordinator::worker_auth_result(uint64_t id, cp0_sudo_result_t result,
                                                     int exit_code, int64_t now_ms)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = requests_.find(id);
    if (it == requests_.end() || it->second->state != State::RUNNING) return {};
    auto request = it->second;
    if (request->cancel_requested.load()) result = CP0_SUDO_RESULT_CANCELLED;
    if (result == CP0_SUDO_RESULT_AUTH_FAILED && request->auth_attempts < 3) {
        request->state = State::PROMPT;
        return {{ActionType::SHOW_AUTH_ERROR, request}};
    }
    controls_.push_back({ActionType::DESTROY_PROMPT, request});
    terminal_locked(request, result, exit_code);
    (void)now_ms;
    return {};
}

std::vector<Action> Coordinator::worker_authenticated(uint64_t id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = requests_.find(id);
    if (it == requests_.end() || it->second->state != State::RUNNING ||
        it->second->cancel_requested.load() || it->second->authentication_complete)
        return {};
    it->second->authentication_complete = true;
    return {{ActionType::DESTROY_PROMPT, it->second}};
}

OutputResult Coordinator::worker_output(uint64_t id, std::string data)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = requests_.find(id);
    if (it == requests_.end() || it->second->state != State::RUNNING ||
        it->second->cancel_requested.load()) return OutputResult::TERMINAL;
    if (data.size() > pending_limit_) return OutputResult::TOO_LARGE;
    if (!cp0_testable::pending_output_fits(it->second->pending_bytes, data.size(), pending_limit_))
        return OutputResult::WOULD_BLOCK;
    it->second->pending_bytes += data.size();
    delivery_requests_[id] = it->second;
    callbacks_.push_back({it->second, std::move(data), CP0_SUDO_RESULT_EXEC_FAILED, -1, false});
    return OutputResult::ACCEPTED;
}

void Coordinator::output_delivered(uint64_t id, size_t size)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = delivery_requests_.find(id);
    if (it == delivery_requests_.end()) return;
    auto request = it->second.lock();
    if (!request) { delivery_requests_.erase(it); return; }
    request->pending_bytes = size > request->pending_bytes ? 0 : request->pending_bytes - size;
    if (request->pending_bytes == 0) delivery_requests_.erase(it);
}

void Coordinator::worker_complete(uint64_t id, cp0_sudo_result_t result, int exit_code)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = requests_.find(id);
    if (it == requests_.end() || it->second->state != State::RUNNING) return;
    if (it->second->cancel_requested.load()) { result = CP0_SUDO_RESULT_CANCELLED; exit_code = -ECANCELED; }
    if (!it->second->authentication_complete)
        controls_.push_back({ActionType::DESTROY_PROMPT, it->second});
    terminal_locked(it->second, result, exit_code);
}

void Coordinator::expire_queued_locked(int64_t now_ms)
{
    for (auto it = queue_.begin(); it != queue_.end();) {
        const auto &request = *it;
        if (request->deadline_ms > 0 && now_ms >= request->deadline_ms) {
            auto expired = request;
            it = queue_.erase(it);
            terminal_locked(expired, CP0_SUDO_RESULT_TIMED_OUT, -ETIMEDOUT);
        } else {
            ++it;
        }
    }
}

std::vector<Action> Coordinator::tick(int64_t now_ms, Budget budget)
{
    std::lock_guard<std::mutex> lock(mutex_);
    expire_queued_locked(now_ms);
    std::vector<Action> actions;
    while (!controls_.empty()) {
        actions.push_back(std::move(controls_.front()));
        controls_.pop_front();
    }
    size_t events = 0, bytes = 0;
    while (!callbacks_.empty()) {
        auto &next = callbacks_.front();
        const size_t next_bytes = next.completion ? 0 : next.data.size();
        if (!cp0_testable::callback_event_fits_tick(events, bytes, next_bytes,
                                                     budget.events, budget.output_bytes)) break;
        Pending pending = std::move(next);
        callbacks_.pop_front();
        ++events;
        bytes += next_bytes;
        if (pending.completion) {
            actions.push_back({ActionType::CALL_COMPLETE, pending.request, {}, pending.result, pending.exit_code});
            --terminal_completions_pending_;
            auto next_actions = start_next_locked(now_ms);
            actions.insert(actions.end(), next_actions.begin(), next_actions.end());
        } else {
            actions.push_back({ActionType::CALL_OUTPUT, pending.request, std::move(pending.data)});
        }
    }
    if (current_ && current_->state == State::PROMPT) {
        if (current_->cancel_requested.load() ||
            (current_->auth_timeout_ms > 0 && now_ms >= current_->deadline_ms)) {
            auto request = current_;
            actions.push_back({ActionType::DESTROY_PROMPT, request});
            terminal_locked(request, request->cancel_requested.load()
                ? CP0_SUDO_RESULT_CANCELLED : CP0_SUDO_RESULT_TIMED_OUT,
                request->cancel_requested.load() ? -ECANCELED : -ETIMEDOUT);
        }
    }
    return actions;
}

std::shared_ptr<Request> Coordinator::find(uint64_t id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = requests_.find(id);
    return it == requests_.end() ? nullptr : it->second;
}

State Coordinator::state(uint64_t id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = requests_.find(id);
    return it == requests_.end() ? State::TERMINAL : it->second->state;
}

bool Coordinator::accepting() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return accepting_;
}

bool Coordinator::resume()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (current_ || !requests_.empty() || !reservations_.empty() || !queue_.empty() ||
        !controls_.empty() || !callbacks_.empty() || terminal_completions_pending_ != 0)
        return false;
    accepting_ = true;
    return true;
}

} // namespace cp0_sudo
