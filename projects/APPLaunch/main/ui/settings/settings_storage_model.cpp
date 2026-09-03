#include "settings_storage_model.hpp"

#if __has_include("global_config.h")
#include "global_config.h"
#endif

#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>

namespace {

struct MountEntry {
    std::string device_name;
    std::string path;
};

bool all_decimal(std::string_view value)
{
    if (value.empty()) return false;
    for (const unsigned char character : value) {
        if (!std::isdigit(character)) return false;
    }
    return true;
}

std::optional<std::string> mmc_block_device_name(std::string_view source)
{
    const std::size_t slash = source.find_last_of('/');
    std::string_view name = slash == std::string_view::npos
        ? source
        : source.substr(slash + 1);
    constexpr std::string_view prefix = "mmcblk";
    if (name.compare(0, prefix.size(), prefix) != 0) return std::nullopt;
    name.remove_prefix(prefix.size());

    const std::size_t partition = name.find('p');
    const std::string_view device_index = name.substr(0, partition);
    if (!all_decimal(device_index)) return std::nullopt;
    if (partition != std::string_view::npos &&
        !all_decimal(name.substr(partition + 1)))
        return std::nullopt;
    return std::string(prefix) + std::string(device_index);
}

std::optional<std::string> resolve_mmc_block_device_name(const std::string &source)
{
    if (source.compare(0, 5, "/dev/") != 0) return std::nullopt;
    if (const auto direct = mmc_block_device_name(source)) return direct;

    std::error_code error;
    const std::filesystem::path resolved = std::filesystem::canonical(source, error);
    if (error) return std::nullopt;
    return mmc_block_device_name(resolved.string());
}

std::string decode_mount_field(std::string_view encoded)
{
    std::string decoded;
    decoded.reserve(encoded.size());
    for (std::size_t index = 0; index < encoded.size(); ++index) {
        if (encoded[index] == '\\' && index + 3 < encoded.size() &&
            encoded[index + 1] >= '0' && encoded[index + 1] <= '7' &&
            encoded[index + 2] >= '0' && encoded[index + 2] <= '7' &&
            encoded[index + 3] >= '0' && encoded[index + 3] <= '7') {
            const unsigned int value =
                static_cast<unsigned int>(encoded[index + 1] - '0') * 64U +
                static_cast<unsigned int>(encoded[index + 2] - '0') * 8U +
                static_cast<unsigned int>(encoded[index + 3] - '0');
            decoded.push_back(static_cast<char>(value));
            index += 3;
        } else {
            decoded.push_back(encoded[index]);
        }
    }
    return decoded;
}

std::vector<MountEntry> mmc_mounts(std::string_view mount_table)
{
    std::vector<MountEntry> result;
    std::istringstream input{std::string(mount_table)};
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream fields(line);
        std::string source;
        std::string path;
        if (!(fields >> source >> path)) continue;
        source = decode_mount_field(source);
        path = decode_mount_field(path);
        const auto device_name = resolve_mmc_block_device_name(source);
        if (path.empty() || !device_name) continue;
        result.push_back({*device_name, std::move(path)});
    }
    return result;
}

#if defined(CONFIG_APPLAUNCH_TARGET_SD_MMC_STORAGE) && defined(__linux__)
bool native_space_probe(const std::string &path,
                        std::uintmax_t &capacity,
                        std::uintmax_t &available)
{
    std::error_code error;
    const std::filesystem::space_info space = std::filesystem::space(path, error);
    if (error || space.capacity == static_cast<std::uintmax_t>(-1) ||
        space.available == static_cast<std::uintmax_t>(-1) ||
        space.capacity == 0 || space.available > space.capacity)
        return false;
    capacity = space.capacity;
    available = space.available;
    return true;
}

SettingsMmcDeviceType native_device_type_probe(const std::string &device_name)
{
    if (!mmc_block_device_name(device_name)) return SettingsMmcDeviceType::Unknown;
    std::ifstream type_file(
        std::filesystem::path("/sys/class/block") / device_name / "device/type");
    std::string type;
    if (!(type_file >> type)) return SettingsMmcDeviceType::Unknown;
    if (type == "SD") return SettingsMmcDeviceType::Sd;
    if (type == "MMC") return SettingsMmcDeviceType::Mmc;
    return SettingsMmcDeviceType::Unknown;
}
#endif

} // namespace

SettingsStorageInfo SettingsStorageModel::read() noexcept
{
#if defined(CONFIG_APPLAUNCH_TARGET_SD_MMC_STORAGE) && defined(__linux__)
    try {
        std::ifstream mounts("/proc/self/mounts");
        if (!mounts) return {};
        std::ostringstream contents;
        contents << mounts.rdbuf();
        if (!mounts.good() && !mounts.eof()) return {};
        return read_from_mount_table(
            contents.str(), native_space_probe, native_device_type_probe);
    } catch (...) {
        return {};
    }
#else
    return {};
#endif
}

SettingsStorageInfo SettingsStorageModel::read_from_mount_table(
    std::string_view mount_table,
    const SpaceProbe &space_probe,
    const DeviceTypeProbe &device_type_probe) noexcept
{
    if (!space_probe || !device_type_probe) return {};

    try {
        SettingsStorageInfo best;
        for (const MountEntry &mount : mmc_mounts(mount_table)) {
            if (device_type_probe(mount.device_name) != SettingsMmcDeviceType::Sd)
                continue;
            std::uintmax_t capacity = 0;
            std::uintmax_t available = 0;
            if (!space_probe(mount.path, capacity, available) || capacity == 0 ||
                available > capacity)
                continue;

            SettingsStorageInfo candidate{
                true,
                capacity,
                available,
                mount.path,
            };
            if (!best.valid || candidate.total_bytes > best.total_bytes ||
                (candidate.total_bytes == best.total_bytes &&
                 candidate.available_bytes > best.available_bytes))
                best = std::move(candidate);
        }
        return best;
    } catch (...) {
        return {};
    }
}

std::string SettingsStorageModel::format_bytes(std::uintmax_t bytes)
{
    static constexpr std::array<const char *, 5> units = {
        "B", "KiB", "MiB", "GiB", "TiB",
    };

    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < units.size()) {
        value /= 1024.0;
        ++unit;
    }

    std::ostringstream output;
    if (unit == 0)
        output << bytes;
    else
        output << std::fixed << std::setprecision(1) << value;
    output << ' ' << units[unit];
    return output.str();
}
