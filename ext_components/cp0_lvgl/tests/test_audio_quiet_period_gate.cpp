#include "cp0_audio_quiet_period_gate.hpp"

#include <cassert>
#include <chrono>
#include <future>
#include <thread>

using namespace std::chrono_literals;

int main()
{
    cp0::audio::QuietPeriodGate gate;
    gate.request();

    const auto started = std::chrono::steady_clock::now();
    auto quiet = std::async(std::launch::async, [&gate] {
        return gate.wait_until_quiet(80ms);
    });
    std::this_thread::sleep_for(40ms);
    gate.request();

    const std::uint64_t generation = quiet.get();
    const auto elapsed = std::chrono::steady_clock::now() - started;
    assert(elapsed >= 100ms);
    assert(elapsed < 1s);
    assert(gate.current(generation));

    gate.request();
    assert(!gate.current(generation));

    cp0::audio::QuietPeriodGate stopped_gate;
    auto stopped = std::async(std::launch::async, [&stopped_gate] {
        return stopped_gate.wait_until_quiet(5s);
    });
    std::this_thread::sleep_for(20ms);
    stopped_gate.stop();
    assert(stopped.get() == 0);
}
