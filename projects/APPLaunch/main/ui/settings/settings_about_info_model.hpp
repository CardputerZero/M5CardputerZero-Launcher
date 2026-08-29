#pragma once

#include <string>
#include <string_view>

struct SettingsAboutOsInfo {
    std::string build_date = "unknown";
    std::string commit = "unknown";
};

class SettingsAboutInfoModel {
public:
    static SettingsAboutOsInfo parse_os_issue_text(std::string_view text);
    static SettingsAboutOsInfo read_os_issue_file(
        const std::string &path = "/boot/issue.txt");
};
