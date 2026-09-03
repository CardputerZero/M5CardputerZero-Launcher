#pragma once

#include "../desktop_entry.h"

#include <string>
#include <string_view>
#include <vector>

struct PreinstalledDesktopApp
{
    std::string filename;
    DesktopEntry entry;
};

std::vector<PreinstalledDesktopApp> parse_preinstalled_app_manifest(
    std::string_view contents);

bool preinstalled_app_manifest_contains(
    const std::vector<PreinstalledDesktopApp> &entries,
    std::string_view filename,
    const DesktopEntry &entry) noexcept;
