#include "../src/cp0_battery_snapshot_cache.hpp"

#include <cassert>

int main()
{
    using Cache = cp0::battery::SnapshotCache;
    const auto start = Cache::Clock::time_point{};
    Cache cache;
    assert(cache.read(start).valid == 0);

    cp0_battery_info_t info{};
    info.soc = 73;
    info.valid = 1;
    cache.update(info, start);
    assert(cache.read(start).soc == 73);
    assert(cache.read(start + Cache::kStaleAfter - std::chrono::seconds(1)).soc == 73);
    assert(cache.read(start + Cache::kStaleAfter).valid == 0);

    cp0_battery_info_t invalid{};
    cache.update(invalid, start + Cache::kStaleAfter);
    assert(cache.read(start + Cache::kStaleAfter).valid == 0);
}
