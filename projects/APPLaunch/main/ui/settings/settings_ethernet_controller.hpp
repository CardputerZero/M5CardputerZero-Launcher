#pragma once

#include "settings_async_dispatch.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <mutex>
#include <string>
#include <string_view>

namespace settings_ethernet {

enum class Operation {
    QueryState,
    Connect,
    Disconnect,
};

struct CommandResult {
    int code = -1;
    std::string output;
};

struct Snapshot {
    bool connected = false;
    bool known = false;
    bool pending = false;
    bool last_request_succeeded = false;
    std::uint64_t generation = 0;
};

using Executor = std::function<CommandResult(Operation)>;

bool parse_general_state(std::string_view output, bool &connected) noexcept;
std::list<std::string> process_request(Operation operation);

class Controller {
public:
    explicit Controller(Executor executor);
    ~Controller();

    Controller(const Controller &) = delete;
    Controller &operator=(const Controller &) = delete;

    bool request_refresh();
    bool toggle();
    Snapshot snapshot() const noexcept;

    void wait_for_idle() noexcept;

private:
    CommandResult execute(Operation operation) noexcept;
    bool start_refresh(std::chrono::steady_clock::time_point now);
    void finish_refresh(std::uint64_t generation, CommandResult result) noexcept;
    void finish_toggle(std::uint64_t generation,
                       bool desired,
                       CommandResult command_result,
                       CommandResult state_result) noexcept;

    Executor executor_;
    mutable std::mutex mutex_;
    bool connected_ = false;
    bool known_ = false;
    bool pending_ = false;
    bool last_request_succeeded_ = false;
    std::uint64_t generation_ = 1;
    std::chrono::steady_clock::time_point next_refresh_allowed_{};
    SettingsAsyncTaskRegistry tasks_;
};

} // namespace settings_ethernet
