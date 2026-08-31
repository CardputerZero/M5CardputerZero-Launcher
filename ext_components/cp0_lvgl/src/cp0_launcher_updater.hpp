#pragma once

#include "cp0_update_job.hpp"

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace cp0::update {

using Run = std::function<int(const std::vector<std::string> &)>;
using Capture = std::function<int(const std::vector<std::string> &, std::string &)>;
using ProgressUpdate = std::function<void(int)>;

struct LauncherUpdateInfo {
    std::string version;
    std::string package_version;
    std::string commit;
};

inline std::string trim(std::string value)
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
    return value.substr(first);
}

inline bool checksum_value(const std::string &text, std::string &hash)
{
    const std::string value = trim(text);
    if (value.size() < 64) return false;
    hash = value.substr(0, 64);
    for (char character : hash)
        if (!std::isxdigit(static_cast<unsigned char>(character))) return false;
    return value.size() == 64 || std::isspace(static_cast<unsigned char>(value[64]));
}

inline bool safe_metadata_value(const std::string &value)
{
    if (value.empty() || value.size() > 128) return false;
    for (const unsigned char character : value) {
        if (character < 0x21 || character > 0x7e || character == '|' || character == '=')
            return false;
    }
    return true;
}

inline bool launcher_update_info(const std::string &text, LauncherUpdateInfo &info)
{
    LauncherUpdateInfo parsed;
    bool format_seen = false;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const size_t separator = line.find('=');
        if (separator == std::string::npos) return false;
        const std::string key = line.substr(0, separator);
        const std::string value = line.substr(separator + 1);
        if (key == "format") {
            if (format_seen || value != "1") return false;
            format_seen = true;
        } else if (key == "version") {
            if (!parsed.version.empty()) return false;
            parsed.version = value;
        } else if (key == "package_version") {
            if (!parsed.package_version.empty()) return false;
            parsed.package_version = value;
        } else if (key == "commit") {
            if (!parsed.commit.empty()) return false;
            parsed.commit = value;
        }
    }
    if (!format_seen || !safe_metadata_value(parsed.version) ||
        !safe_metadata_value(parsed.commit))
        return false;
    if (parsed.package_version.empty()) parsed.package_version = parsed.version;
    if (!safe_metadata_value(parsed.package_version)) return false;
    for (const unsigned char character : parsed.commit)
        if (!std::isxdigit(character)) return false;
    if (parsed.commit.size() < 7 || parsed.commit.size() > 64) return false;
    info = std::move(parsed);
    return true;
}

inline Result check_launcher(const Capture &capture, const std::string &release_url,
                             const ProgressUpdate &progress = {})
{
    if (progress) progress(5);
    std::string metadata;
    const int download = capture(
        {"wget", "-q", "--https-only", "--timeout=30", "--tries=3", "-O", "-",
         release_url + "/applaunch_arm64.deb.update-info"}, metadata);
    if (download != 0)
        return {download, download == -ECANCELED ? "cancelled" : "check-download"};

    if (progress) progress(60);
    LauncherUpdateInfo info;
    if (!launcher_update_info(metadata, info)) return {-EINVAL, "check-metadata"};

    std::string installed;
    const int query = capture(
        {"dpkg-query", "-W", "-f=${Version}", "applaunch"}, installed);
    installed = trim(std::move(installed));
    if (query != 0 || installed.empty())
        return {query == 0 ? -ENOENT : query, "installed-version"};

    if (progress) progress(85);
    std::string ignored;
    const int compare = capture(
        {"dpkg", "--compare-versions", info.package_version, "gt", installed}, ignored);
    if (compare != 0 && compare != 1)
        return {compare, compare == -ECANCELED ? "cancelled" : "compare-version"};

    if (progress) progress(100);
    return {0, std::string(compare == 0 ? "update-available" : "up-to-date") +
                   "|version=" + info.version + "|commit=" + info.commit};
}

inline Result launcher(const Run &run, const Capture &capture)
{
    // The fixed system unit owns download, verification, rollback and install.
    // Keeping that work in one process avoids downloading the same package
    // twice and lets slow links use the unit's full timeout.
    const int code = run({"systemctl", "start", "applaunch-updater.service"});
    std::string state;
    if (capture({"cat", "/var/lib/applaunch-updater/status"}, state) == 0) {
        state = trim(std::move(state));
        if (state == "cancelled") return {-ECANCELED, "cancelled"};
        if (code == 0 && state.rfind("succeeded:", 0) == 0)
            return {0, state.substr(10)};
        if (state.rfind("failed:", 0) == 0) {
            std::string stage = state.substr(7);
            const size_t detail = stage.find(':');
            if (detail != std::string::npos) stage.resize(detail);
            return {code == 0 ? -1 : code, stage.empty() ? "service" : stage};
        }
    }
    return {code, code == 0 ? "completed" : "service"};
}

} // namespace cp0::update
