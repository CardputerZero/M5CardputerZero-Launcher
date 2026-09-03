#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

struct SettingsStorageInfo {
    bool valid = false;
    std::uintmax_t total_bytes = 0;
    std::uintmax_t available_bytes = 0;
    std::string mount_path;
};

enum class SettingsMmcDeviceType : std::uint8_t {
    Unknown,
    Sd,
    Mmc,
};

class SettingsStorageModel {
public:
    using SpaceProbe = std::function<bool(const std::string &path,
                                          std::uintmax_t &capacity,
                                          std::uintmax_t &available)>;
    using DeviceTypeProbe = std::function<SettingsMmcDeviceType(
        const std::string &device_name)>;

    static SettingsStorageInfo read() noexcept;
    static SettingsStorageInfo read_from_mount_table(
        std::string_view mount_table,
        const SpaceProbe &space_probe,
        const DeviceTypeProbe &device_type_probe) noexcept;
    static std::string format_bytes(std::uintmax_t bytes);
};
