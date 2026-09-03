#include "settings_ethernet_controller.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <future>
#include <list>
#include <mutex>
#include <string>
#include <vector>

namespace {

using settings_ethernet::CommandResult;
using settings_ethernet::Controller;
using settings_ethernet::Operation;

void test_general_state_parser()
{
    bool connected = false;
    assert(settings_ethernet::parse_general_state("100 (connected)\n", connected));
    assert(connected);

    connected = true;
    assert(settings_ethernet::parse_general_state("30 (disconnected)\n", connected));
    assert(!connected);

    connected = true;
    assert(settings_ethernet::parse_general_state("110 (deactivating)\n", connected));
    assert(!connected);

    assert(settings_ethernet::parse_general_state("  120 (failed)  \n", connected));
    assert(!connected);

    assert(!settings_ethernet::parse_general_state("", connected));
    assert(!settings_ethernet::parse_general_state("connected", connected));
    assert(!settings_ethernet::parse_general_state("0 (unknown)", connected));
    assert(!settings_ethernet::parse_general_state("35 (invalid)", connected));
    assert(!settings_ethernet::parse_general_state("130 (out of range)", connected));
    assert(!settings_ethernet::parse_general_state("100 connected", connected));
    assert(!settings_ethernet::parse_general_state("100 (connected) trailing", connected));
}

void test_process_requests_are_bounded()
{
    const std::list<std::string> query{
        "CaptureArgv", "nmcli", "--wait", "5", "-g", "GENERAL.STATE", "device", "show", "eth0"};
    assert(settings_ethernet::process_request(Operation::QueryState) == query);

    const std::list<std::string> connect{
        "CaptureArgv", "nmcli", "--wait", "10", "device", "connect", "eth0"};
    assert(settings_ethernet::process_request(Operation::Connect) == connect);

    const std::list<std::string> disconnect{
        "CaptureArgv", "nmcli", "--wait", "10", "device", "disconnect", "eth0"};
    assert(settings_ethernet::process_request(Operation::Disconnect) == disconnect);
}

void test_refresh_is_asynchronous()
{
    std::promise<void> entered;
    auto entered_future = entered.get_future();
    std::promise<void> release;
    auto release_future = release.get_future().share();

    Controller controller([&](Operation operation) {
        assert(operation == Operation::QueryState);
        entered.set_value();
        release_future.wait();
        return CommandResult{0, "100 (connected)\n"};
    });

    auto request = std::async(std::launch::async, [&] { return controller.request_refresh(); });
    assert(entered_future.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    const bool returned_while_blocked =
        request.wait_for(std::chrono::seconds(1)) == std::future_status::ready;
    assert(controller.snapshot().pending);

    release.set_value();
    assert(returned_while_blocked);
    assert(request.get());
    controller.wait_for_idle();

    const auto state = controller.snapshot();
    assert(!state.pending);
    assert(state.known);
    assert(state.connected);
    assert(state.last_request_succeeded);
    assert(!controller.request_refresh());
    assert(controller.snapshot().connected);
}

void test_failed_refresh_is_unknown()
{
    Controller controller([](Operation operation) {
        assert(operation == Operation::QueryState);
        return CommandResult{1, "NetworkManager unavailable"};
    });
    assert(controller.request_refresh());
    controller.wait_for_idle();
    const auto state = controller.snapshot();
    assert(!state.pending);
    assert(!state.known);
    assert(!state.connected);
    assert(!state.last_request_succeeded);
}

void test_toggle_is_asynchronous_and_rejects_duplicates()
{
    std::mutex mutex;
    std::promise<void> entered;
    auto entered_future = entered.get_future();
    std::promise<void> release;
    auto release_future = release.get_future().share();
    std::vector<Operation> operations;

    Controller controller([&](Operation operation) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            operations.push_back(operation);
        }
        if (operation == Operation::Connect) {
            entered.set_value();
            release_future.wait();
        }
        if (operation == Operation::QueryState) return CommandResult{0, "100 (connected)\n"};
        return CommandResult{0, {}};
    });

    auto request = std::async(std::launch::async, [&] { return controller.toggle(); });
    assert(entered_future.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    const bool returned_while_blocked =
        request.wait_for(std::chrono::seconds(1)) == std::future_status::ready;
    const auto pending = controller.snapshot();
    assert(pending.pending);
    assert(pending.connected);
    assert(!controller.toggle());

    release.set_value();
    assert(returned_while_blocked);
    assert(request.get());
    controller.wait_for_idle();

    const auto complete = controller.snapshot();
    assert(!complete.pending);
    assert(complete.connected);
    assert(complete.known);
    assert(complete.last_request_succeeded);
    assert((operations == std::vector<Operation>{Operation::Connect, Operation::QueryState}));
}

void test_failed_action_rolls_back_to_verified_state()
{
    int query_count = 0;
    Controller controller([&](Operation operation) {
        if (operation == Operation::QueryState) {
            ++query_count;
            return CommandResult{0, "30 (disconnected)\n"};
        }
        assert(operation == Operation::Connect);
        return CommandResult{10, "connect failed"};
    });

    assert(controller.request_refresh());
    controller.wait_for_idle();
    assert(controller.snapshot().known);
    assert(!controller.snapshot().connected);

    assert(controller.toggle());
    controller.wait_for_idle();
    const auto state = controller.snapshot();
    assert(query_count == 2);
    assert(!state.connected);
    assert(state.known);
    assert(!state.pending);
    assert(!state.last_request_succeeded);
}

void test_unknown_verification_fails_closed()
{
    int query_count = 0;
    Controller controller([&](Operation operation) {
        if (operation == Operation::QueryState) {
            ++query_count;
            return query_count == 1 ? CommandResult{0, "100 (connected)\n"}
                                    : CommandResult{0, "0 (unknown)\n"};
        }
        assert(operation == Operation::Disconnect);
        return CommandResult{0, {}};
    });

    assert(controller.request_refresh());
    controller.wait_for_idle();
    assert(controller.snapshot().connected);
    assert(controller.toggle());
    controller.wait_for_idle();
    const auto state = controller.snapshot();
    assert(!state.pending);
    assert(!state.known);
    assert(!state.connected);
    assert(!state.last_request_succeeded);
}

void test_failed_verification_is_unknown()
{
    int query_count = 0;
    Controller controller([&](Operation operation) {
        if (operation == Operation::QueryState) {
            ++query_count;
            if (query_count == 1) return CommandResult{0, "30 (disconnected)\n"};
            return CommandResult{1, "query failed"};
        }
        assert(operation == Operation::Connect);
        return CommandResult{0, {}};
    });

    assert(controller.request_refresh());
    controller.wait_for_idle();
    assert(controller.toggle());
    controller.wait_for_idle();

    const auto state = controller.snapshot();
    assert(!state.connected);
    assert(!state.known);
    assert(!state.pending);
    assert(!state.last_request_succeeded);
}

void test_failed_action_and_verification_are_unknown()
{
    int query_count = 0;
    Controller controller([&](Operation operation) {
        if (operation == Operation::QueryState) {
            ++query_count;
            if (query_count == 1) return CommandResult{0, "30 (disconnected)\n"};
            return CommandResult{1, "query failed"};
        }
        assert(operation == Operation::Connect);
        return CommandResult{10, "connect failed"};
    });

    assert(controller.request_refresh());
    controller.wait_for_idle();
    assert(controller.snapshot().known);
    assert(!controller.snapshot().connected);

    assert(controller.toggle());
    controller.wait_for_idle();

    const auto state = controller.snapshot();
    assert(query_count == 2);
    assert(!state.connected);
    assert(!state.known);
    assert(!state.pending);
    assert(!state.last_request_succeeded);
}

void test_verified_actual_state_wins()
{
    int query_count = 0;
    Controller controller([&](Operation operation) {
        if (operation == Operation::QueryState) {
            ++query_count;
            return CommandResult{0, "30 (disconnected)\n"};
        }
        assert(operation == Operation::Connect);
        return CommandResult{0, {}};
    });

    assert(controller.request_refresh());
    controller.wait_for_idle();
    assert(controller.toggle());
    controller.wait_for_idle();

    const auto state = controller.snapshot();
    assert(query_count == 2);
    assert(!state.connected);
    assert(state.known);
    assert(!state.last_request_succeeded);
}

void test_destructor_joins_an_active_worker()
{
    std::atomic_bool finished{false};
    {
        Controller controller([&](Operation operation) {
            assert(operation == Operation::QueryState);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            finished.store(true, std::memory_order_release);
            return CommandResult{0, "30 (disconnected)\n"};
        });
        assert(controller.request_refresh());
    }
    assert(finished.load(std::memory_order_acquire));
}

}  // namespace

int main()
{
    test_general_state_parser();
    test_process_requests_are_bounded();
    test_refresh_is_asynchronous();
    test_failed_refresh_is_unknown();
    test_toggle_is_asynchronous_and_rejects_duplicates();
    test_failed_action_rolls_back_to_verified_state();
    test_failed_verification_is_unknown();
    test_failed_action_and_verification_are_unknown();
    test_unknown_verification_fails_closed();
    test_verified_actual_state_wins();
    test_destructor_joins_an_active_worker();
    return 0;
}
