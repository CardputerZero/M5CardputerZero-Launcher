#include "../main/ui/settings/settings_about_info_model.hpp"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>

namespace {

constexpr const char *kCommit = "cd0981140bacbeca1f58f722312e0b7cfac2221a";

void test_example()
{
    const auto info = SettingsAboutInfoModel::parse_os_issue_text(
        "Raspberry Pi reference 2026-08-27\n"
        "Generated using pi-gen, https://github.com/RPi-Distro/pi-gen, "
        "cd0981140bacbeca1f58f722312e0b7cfac2221a, stage4\n");
    assert(info.build_date == "2026-08-27");
    assert(info.commit == kCommit);
}

void test_crlf_and_extra_whitespace()
{
    const auto info = SettingsAboutInfoModel::parse_os_issue_text(
        "  Raspberry   Pi  reference   2024-02-29  \r\n"
        " Generated   using pi-gen  ,  https://github.com/RPi-Distro/pi-gen  , "
        " cd0981140bacbeca1f58f722312e0b7cfac2221a , stage4 \r\n");
    assert(info.build_date == "2024-02-29");
    assert(info.commit == kCommit);
}

void test_invalid_fields_fall_back_independently()
{
    const auto invalid = SettingsAboutInfoModel::parse_os_issue_text(
        "Raspberry Pi reference 2025-02-29\n"
        "Generated using something-else, https://example.com, not-a-commit, stage4\n");
    assert(invalid.build_date == "unknown");
    assert(invalid.commit == "unknown");

    const auto date_only = SettingsAboutInfoModel::parse_os_issue_text(
        "Raspberry Pi reference 2026-08-27\n"
        "Generated using pi-gen, https://example.com, invalid, stage4\n");
    assert(date_only.build_date == "2026-08-27");
    assert(date_only.commit == "unknown");
}

void test_file_reading()
{
    const std::string missing_path = "/tmp/applaunch-settings-about-info-missing.txt";
    std::remove(missing_path.c_str());
    const auto missing = SettingsAboutInfoModel::read_os_issue_file(missing_path);
    assert(missing.build_date == "unknown");
    assert(missing.commit == "unknown");

    const std::string path = "/tmp/applaunch-settings-about-info-test.txt";
    {
        std::ofstream file(path);
        assert(file);
        file << "Raspberry Pi reference 2026-08-27\n"
             << "Generated using pi-gen, https://github.com/RPi-Distro/pi-gen, "
             << kCommit << ", stage4\n";
    }
    const auto info = SettingsAboutInfoModel::read_os_issue_file(path);
    std::remove(path.c_str());
    assert(info.build_date == "2026-08-27");
    assert(info.commit == kCommit);
}

} // namespace

int main()
{
    test_example();
    test_crlf_and_extra_whitespace();
    test_invalid_fields_fall_back_independently();
    test_file_reading();
}
