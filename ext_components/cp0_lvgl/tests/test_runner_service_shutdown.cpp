#include "cp0_runner_shutdown.hpp"

#include <cassert>
#include <stdexcept>
#include <vector>

int main()
{
    std::vector<int> order;
    cp0::runner::shutdown_services(
        [&] { order.push_back(1); },
        [&] { order.push_back(2); },
        [&] { order.push_back(3); },
        [&] { order.push_back(4); },
        [&] { order.push_back(5); },
        [&] { order.push_back(6); },
        [&] { order.push_back(7); },
        [&] { order.push_back(8); },
        [&] { order.push_back(9); },
        [&] { order.push_back(10); });
    assert((order == std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10}));

    order.clear();
    cp0::runner::shutdown_services(
        [&] {
            order.push_back(1);
            throw std::runtime_error("sudo shutdown");
        },
        [&] {
            order.push_back(2);
            throw std::runtime_error("rpc shutdown");
        },
        [&] {
            order.push_back(3);
            throw std::runtime_error("camera shutdown");
        },
        [&] {
            order.push_back(4);
            throw std::runtime_error("input shutdown");
        },
        [&] {
            order.push_back(5);
            throw std::runtime_error("wifi shutdown");
        },
        [&] {
            order.push_back(6);
            throw std::runtime_error("lora shutdown");
        },
        [&] {
            order.push_back(7);
            throw std::runtime_error("lora shutdown");
        },
        [&] {
            order.push_back(8);
            throw std::runtime_error("battery shutdown");
        },
        [&] {
            order.push_back(9);
            throw std::runtime_error("last service shutdown");
        },
        [&] { order.push_back(10); });
    assert((order == std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10}));
}
