#pragma once

#include "cp0_update_job.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace cp0::update {

using Run = std::function<int(const std::vector<std::string> &)>;
using Capture = std::function<int(const std::vector<std::string> &, std::string &)>;

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

inline Result launcher(const Run &run, const Capture &capture)
{
    char directory[] = "/tmp/applaunch-update-XXXXXX";
    if (!mkdtemp(directory)) return {-1, "prepare"};
    if (chmod(directory, 0700) != 0) {
        rmdir(directory);
        return {-1, "prepare"};
    }
    const std::string base(directory);
    const std::string package = base + "/applaunch_arm64.deb";
    const std::string checksum = base + "/applaunch_arm64.deb.sha256";
    auto finish = [&](Result result) {
        std::remove(package.c_str());
        std::remove(checksum.c_str());
        rmdir(base.c_str());
        return result;
    };
    const std::string release =
        "https://github.com/CardputerZero/launcher/releases/download/launcher-latest/";
    if (run({"wget", "-q", "--https-only", release + "applaunch_arm64.deb", "-O", package}) != 0)
        return finish({-1, "download-package"});
    if (run({"wget", "-q", "--https-only", release + "applaunch_arm64.deb.sha256", "-O", checksum}) != 0)
        return finish({-1, "download-checksum"});

    std::string expected, checksum_text, actual;
    if (capture({"cat", checksum}, checksum_text) != 0 || !checksum_value(checksum_text, expected))
        return finish({-1, "checksum-manifest"});
    if (capture({"sha256sum", package}, actual) != 0 || actual.size() < 64 || actual.substr(0, 64) != expected)
        return finish({-1, "checksum"});

    std::string name, architecture, candidate, installed;
    if (capture({"dpkg-deb", "-f", package, "Package"}, name) != 0 || trim(name) != "applaunch")
        return finish({-1, "package-name"});
    if (capture({"dpkg-deb", "-f", package, "Architecture"}, architecture) != 0 ||
        trim(architecture) != "arm64")
        return finish({-1, "architecture"});
    if (capture({"dpkg-deb", "-f", package, "Version"}, candidate) != 0 || trim(candidate).empty())
        return finish({-1, "candidate-version"});
    if (capture({"dpkg-query", "-W", "-f=${Version}", "applaunch"}, installed) != 0 || trim(installed).empty())
        return finish({-1, "installed-version"});
    if (run({"dpkg", "--compare-versions", trim(candidate), "gt", trim(installed)}) != 0)
        return finish({-1, "version-not-newer"});
    // Installation is deliberately delegated to a fixed system unit.  dpkg
    // must not remain in APPLaunch.service's cgroup because postinst restarts
    // APPLaunch and would otherwise terminate its own package transaction.
    if (run({"systemctl", "start", "--no-block", "applaunch-updater.service"}) != 0)
        return finish({-1, "submit"});
    return finish({0, "submitted"});
}

} // namespace cp0::update
