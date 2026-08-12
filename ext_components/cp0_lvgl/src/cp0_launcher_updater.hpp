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
    // The fixed system unit owns download, verification, rollback and install.
    // Keeping that work in one process avoids downloading the same package
    // twice and lets slow links use the unit's full timeout.
    const int code = run({"systemctl", "start", "applaunch-updater.service"});
    std::string state;
    if (capture({"cat", "/var/lib/applaunch-updater/status"}, state) == 0) {
        state = trim(std::move(state));
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
