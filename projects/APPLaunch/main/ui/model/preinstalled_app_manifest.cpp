#include "preinstalled_app_manifest.hpp"

#include <algorithm>
#include <array>

namespace {

bool parse_bool(std::string_view value, bool &result)
{
    if (value == "true") {
        result = true;
        return true;
    }
    if (value == "false") {
        result = false;
        return true;
    }
    return false;
}

bool identity_valid(const DesktopEntry &entry)
{
    if (entry.name.empty() || entry.exec.empty() ||
        entry.name.size() > DESKTOP_ENTRY_MAX_NAME_BYTES ||
        entry.icon.size() > DESKTOP_ENTRY_MAX_ICON_BYTES ||
        entry.exec.size() > DESKTOP_ENTRY_MAX_EXEC_BYTES)
        return false;

    for (const std::string *value : {&entry.name, &entry.icon, &entry.exec}) {
        for (const unsigned char character : *value)
            if (character < 0x20 || character == 0x7F) return false;
    }
    return true;
}

} // namespace

std::vector<PreinstalledDesktopApp> parse_preinstalled_app_manifest(
    std::string_view contents)
{
    std::vector<PreinstalledDesktopApp> entries;
    while (!contents.empty()) {
        const std::size_t newline = contents.find('\n');
        std::string_view line = contents.substr(0, newline);
        contents = newline == std::string_view::npos
            ? std::string_view{}
            : contents.substr(newline + 1);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty() || line.front() == '#') continue;

        std::array<std::string_view, 6> fields{};
        std::size_t field_index = 0;
        bool has_extra_field = false;
        while (field_index < fields.size()) {
            const std::size_t separator = line.find('\t');
            fields[field_index++] = line.substr(0, separator);
            if (separator == std::string_view::npos) break;
            line.remove_prefix(separator + 1);
            if (field_index == fields.size()) has_extra_field = true;
        }
        if (field_index != fields.size() || has_extra_field)
            continue;

        DesktopEntry identity;
        identity.name = fields[1];
        identity.icon = fields[2];
        identity.exec = fields[3];
        if (!desktop_entry_filename_valid(fields[0]) ||
            !parse_bool(fields[4], identity.terminal) ||
            !parse_bool(fields[5], identity.sysplause) ||
            !identity_valid(identity))
            continue;

        const bool duplicate = std::any_of(
            entries.begin(), entries.end(), [filename = fields[0]](const auto &entry) {
                return entry.filename == filename;
            });
        if (duplicate) continue;
        entries.push_back({std::string(fields[0]), std::move(identity)});
    }
    return entries;
}

bool preinstalled_app_manifest_contains(
    const std::vector<PreinstalledDesktopApp> &entries,
    std::string_view filename,
    const DesktopEntry &entry) noexcept
{
    return std::any_of(entries.begin(), entries.end(),
                       [filename, &entry](const auto &expected) {
                           return expected.filename == filename &&
                                  expected.entry.name == entry.name &&
                                  expected.entry.icon == entry.icon &&
                                  expected.entry.exec == entry.exec &&
                                  expected.entry.terminal == entry.terminal &&
                                  expected.entry.sysplause == entry.sysplause;
                       });
}
