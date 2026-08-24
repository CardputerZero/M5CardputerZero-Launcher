#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace settings_t12b::launcher {

struct AppEntry
{
    std::string label;
    std::string config_key;
    bool enabled = false;
};

template <typename Descriptor, typename EnabledPredicate>
std::vector<AppEntry> configurable_entries(const Descriptor *entries,
                                            std::size_t count,
                                            EnabledPredicate enabled)
{
    std::vector<AppEntry> result;
    if (!entries || count == 0) return result;

    result.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const Descriptor &entry = entries[index];
        if (!entry.configurable || !entry.label || !entry.label[0] ||
            !entry.config_key || !entry.config_key[0])
            continue;
        result.push_back({entry.label, entry.config_key, enabled(entry)});
    }
    return result;
}

bool state_after_write(bool previous, bool requested, bool succeeded) noexcept;
bool should_notify_registry(bool succeeded) noexcept;

} // namespace settings_t12b::launcher
