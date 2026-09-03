#include "settings_ethernet_controller.hpp"

#include <charconv>
#include <utility>

namespace settings_ethernet {
namespace {

constexpr auto kRefreshCooldown = std::chrono::seconds(1);

} // namespace

bool parse_general_state(std::string_view output, bool &connected) noexcept
{
    const std::size_t first = output.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return false;

    const std::size_t last = output.find_last_not_of(" \t\r\n");
    output = output.substr(first, last - first + 1);
    const char *begin = output.data();
    const char *end = output.data() + output.size();
    int state = -1;
    const auto parsed = std::from_chars(begin, end, state);
    if (parsed.ec != std::errc{} || parsed.ptr == begin) return false;
    const char *suffix = parsed.ptr;
    while (suffix != end && (*suffix == ' ' || *suffix == '\t')) ++suffix;
    if (suffix != end && (*suffix != '(' || end[-1] != ')' ||
                          std::string_view(suffix, static_cast<std::size_t>(end - suffix)).find('\n') !=
                              std::string_view::npos))
        return false;
    if (state < 10 || state > 120 || state % 10 != 0) return false;

    connected = state == 100;
    return true;
}

std::list<std::string> process_request(Operation operation)
{
    std::list<std::string> request{"CaptureArgv", "nmcli", "--wait"};
    if (operation == Operation::QueryState) {
        request.emplace_back("5");
        request.insert(request.end(),
                       {"-g", "GENERAL.STATE", "device", "show", "eth0"});
        return request;
    }

    request.emplace_back("10");
    request.insert(request.end(),
                   {"device", operation == Operation::Connect ? "connect" : "disconnect", "eth0"});
    return request;
}

Controller::Controller(Executor executor) : executor_(std::move(executor))
{}

Controller::~Controller()
{
    wait_for_idle();
}

CommandResult Controller::execute(Operation operation) noexcept
{
    try {
        return executor_ ? executor_(operation) : CommandResult{};
    } catch (...) {
        return {};
    }
}

bool Controller::request_refresh()
{
    tasks_.reap_finished();
    return start_refresh(std::chrono::steady_clock::now());
}

bool Controller::start_refresh(std::chrono::steady_clock::time_point now)
{
    std::uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pending_ || now < next_refresh_allowed_) return false;
        pending_ = true;
        generation = ++generation_;
        if (generation_ == 0) generation = generation_ = 1;
    }

    if (tasks_.start([this, generation] {
            finish_refresh(generation, execute(Operation::QueryState));
        }))
        return true;

    std::lock_guard<std::mutex> lock(mutex_);
    if (generation_ == generation) {
        pending_ = false;
        last_request_succeeded_ = false;
        next_refresh_allowed_ = std::chrono::steady_clock::now() + kRefreshCooldown;
    }
    return false;
}

void Controller::finish_refresh(std::uint64_t generation, CommandResult result) noexcept
{
    bool connected = false;
    const bool succeeded = result.code == 0 && parse_general_state(result.output, connected);

    std::lock_guard<std::mutex> lock(mutex_);
    if (generation_ != generation) return;
    if (succeeded) {
        connected_ = connected;
        known_ = true;
    } else {
        connected_ = false;
        known_ = false;
    }
    pending_ = false;
    last_request_succeeded_ = succeeded;
    next_refresh_allowed_ = std::chrono::steady_clock::now() + kRefreshCooldown;
}

bool Controller::toggle()
{
    tasks_.reap_finished();

    bool previous = false;
    bool previous_known = false;
    bool desired = false;
    std::uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pending_) return false;
        previous = connected_;
        previous_known = known_;
        desired = !connected_;
        connected_ = desired;
        pending_ = true;
        last_request_succeeded_ = false;
        generation = ++generation_;
        if (generation_ == 0) generation = generation_ = 1;
    }

    if (tasks_.start([this, generation, desired] {
            const CommandResult command_result =
                execute(desired ? Operation::Connect : Operation::Disconnect);
            const CommandResult state_result = execute(Operation::QueryState);
            finish_toggle(generation, desired, command_result, state_result);
        }))
        return true;

    std::lock_guard<std::mutex> lock(mutex_);
    if (generation_ == generation) {
        connected_ = previous;
        known_ = previous_known;
        pending_ = false;
        next_refresh_allowed_ = std::chrono::steady_clock::now() + kRefreshCooldown;
    }
    return false;
}

void Controller::finish_toggle(std::uint64_t generation,
                               bool desired,
                               CommandResult command_result,
                               CommandResult state_result) noexcept
{
    bool actual = false;
    const bool state_verified =
        state_result.code == 0 && parse_general_state(state_result.output, actual);

    std::lock_guard<std::mutex> lock(mutex_);
    if (generation_ != generation) return;
    if (state_verified) {
        connected_ = actual;
        known_ = true;
    } else {
        connected_ = false;
        known_ = false;
    }
    pending_ = false;
    last_request_succeeded_ =
        command_result.code == 0 && state_verified && actual == desired;
    next_refresh_allowed_ = std::chrono::steady_clock::now() + kRefreshCooldown;
}

Snapshot Controller::snapshot() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return {connected_, known_, pending_, last_request_succeeded_, generation_};
}

void Controller::wait_for_idle() noexcept
{
    tasks_.join_all();
}

} // namespace settings_ethernet
