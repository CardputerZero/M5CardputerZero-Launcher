#include "../main/ui/settings/settings_storage_model.hpp"

#include <cassert>
#include <map>
#include <string>

namespace {

SettingsStorageModel::SpaceProbe probe_from(
    const std::map<std::string, std::pair<std::uintmax_t, std::uintmax_t>> &spaces)
{
    return [&spaces](const std::string &path,
                     std::uintmax_t &capacity,
                     std::uintmax_t &available) {
        const auto found = spaces.find(path);
        if (found == spaces.end()) return false;
        capacity = found->second.first;
        available = found->second.second;
        return true;
    };
}

SettingsStorageModel::DeviceTypeProbe type_probe_from(
    const std::map<std::string, SettingsMmcDeviceType> &types)
{
    return [&types](const std::string &device_name) {
        const auto found = types.find(device_name);
        return found == types.end() ? SettingsMmcDeviceType::Unknown : found->second;
    };
}

} // namespace

int main()
{
    assert(SettingsStorageModel::format_bytes(0) == "0 B");
    assert(SettingsStorageModel::format_bytes(1023) == "1023 B");
    assert(SettingsStorageModel::format_bytes(1024) == "1.0 KiB");
    assert(SettingsStorageModel::format_bytes(5ULL * 1024 * 1024 * 1024) == "5.0 GiB");

    const std::string mixed_mmc_mounts =
        "/dev/mmcblk0p1 /boot vfat rw 0 0\n"
        "/dev/mmcblk0p2 / ext4 rw 0 0\n"
        "/dev/mmcblk1p1 /media/SD\\040Card exfat rw 0 0\n"
        "/dev/nvme0n1p1 /home ext4 rw 0 0\n";
    const std::map<std::string, std::pair<std::uintmax_t, std::uintmax_t>> mixed_spaces = {
        {"/boot", {512, 128}},
        {"/", {4096, 1536}},
        {"/media/SD Card", {32768, 24576}},
        {"/home", {8192, 4096}},
    };
    const std::map<std::string, SettingsMmcDeviceType> mixed_types = {
        {"mmcblk0", SettingsMmcDeviceType::Mmc},
        {"mmcblk1", SettingsMmcDeviceType::Sd},
    };
    const SettingsStorageInfo mixed = SettingsStorageModel::read_from_mount_table(
        mixed_mmc_mounts, probe_from(mixed_spaces), type_probe_from(mixed_types));
    assert(mixed.valid);
    assert(mixed.mount_path == "/media/SD Card");
    assert(mixed.total_bytes == 32768);
    assert(mixed.available_bytes == 24576);

    const SettingsStorageInfo emmc_only = SettingsStorageModel::read_from_mount_table(
        "/dev/mmcblk0p2 / ext4 rw 0 0\n",
        probe_from(mixed_spaces),
        type_probe_from(mixed_types));
    assert(!emmc_only.valid);

    const std::string partitioned_sd_mounts =
        "/dev/mmcblk1p1 /media/SD\\040Boot vfat rw 0 0\n"
        "/dev/mmcblk1p2 /media/SD\\040Data exfat rw 0 0\n";
    const std::map<std::string, std::pair<std::uintmax_t, std::uintmax_t>> separate_spaces = {
        {"/media/SD Boot", {512, 128}},
        {"/media/SD Data", {32768, 24576}},
    };
    const std::map<std::string, SettingsMmcDeviceType> sd_types = {
        {"mmcblk1", SettingsMmcDeviceType::Sd},
    };
    const SettingsStorageInfo separate = SettingsStorageModel::read_from_mount_table(
        partitioned_sd_mounts, probe_from(separate_spaces), type_probe_from(sd_types));
    assert(separate.valid);
    assert(separate.mount_path == "/media/SD Data");
    assert(separate.total_bytes == 32768);
    assert(separate.available_bytes == 24576);

    const std::string host_mounts =
        "/dev/nvme0n1p2 / ext4 rw 0 0\n"
        "tmpfs /tmp tmpfs rw 0 0\n";
    const SettingsStorageInfo host = SettingsStorageModel::read_from_mount_table(
        host_mounts, probe_from(mixed_spaces), type_probe_from(mixed_types));
    assert(!host.valid);

    const std::string malformed_mmc =
        "/dev/notmmcblk0p1 /fake ext4 rw 0 0\n"
        "/dev/mmcblkXp1 /also-fake ext4 rw 0 0\n";
    const SettingsStorageInfo malformed = SettingsStorageModel::read_from_mount_table(
        malformed_mmc, probe_from(mixed_spaces), type_probe_from(mixed_types));
    assert(!malformed.valid);

    const std::map<std::string, std::pair<std::uintmax_t, std::uintmax_t>> invalid_space = {
        {"/", {1024, 2048}},
    };
    const SettingsStorageInfo invalid = SettingsStorageModel::read_from_mount_table(
        "/dev/mmcblk1p2 / ext4 rw 0 0\n",
        probe_from(invalid_space),
        type_probe_from(sd_types));
    assert(!invalid.valid);

    const SettingsStorageInfo no_type_probe = SettingsStorageModel::read_from_mount_table(
        partitioned_sd_mounts, probe_from(separate_spaces), {});
    assert(!no_type_probe.valid);

#if !defined(CONFIG_APPLAUNCH_TARGET_SD_MMC_STORAGE) || !defined(__linux__)
    // Standalone and SDL builds have no target MMC contract and must not expose host `/`.
    assert(!SettingsStorageModel::read().valid);
#endif
}
