#include "settings_about_info_model.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <vector>

namespace {

std::string trim(std::string_view value)
{
    const auto is_space = [](unsigned char character) {
        return std::isspace(character) != 0;
    };
    const auto begin = std::find_if_not(value.begin(), value.end(), is_space);
    const auto end = std::find_if_not(value.rbegin(), value.rend(), is_space).base();
    return begin < end ? std::string(begin, end) : std::string{};
}

std::vector<std::string> whitespace_fields(std::string_view value)
{
    std::istringstream stream{std::string(value)};
    std::vector<std::string> fields;
    std::string field;
    while (stream >> field) fields.push_back(std::move(field));
    return fields;
}

std::vector<std::string> comma_fields(std::string_view value)
{
    std::vector<std::string> fields;
    std::size_t begin = 0;
    while (begin <= value.size()) {
        const std::size_t end = value.find(',', begin);
        fields.push_back(trim(value.substr(
            begin, end == std::string_view::npos ? value.size() - begin : end - begin)));
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return fields;
}

bool is_leap_year(int year)
{
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

bool is_valid_date(const std::string &value)
{
    if (value.size() != 10 || value[4] != '-' || value[7] != '-') return false;
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (index == 4 || index == 7) continue;
        if (!std::isdigit(static_cast<unsigned char>(value[index]))) return false;
    }

    const int year = std::stoi(value.substr(0, 4));
    const int month = std::stoi(value.substr(5, 2));
    const int day = std::stoi(value.substr(8, 2));
    if (year == 0 || month < 1 || month > 12 || day < 1) return false;

    static constexpr int kDaysPerMonth[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
    };
    int days = kDaysPerMonth[month - 1];
    if (month == 2 && is_leap_year(year)) ++days;
    return day <= days;
}

bool is_git_commit(const std::string &value)
{
    return value.size() >= 7 && value.size() <= 40 &&
           std::all_of(value.begin(), value.end(), [](unsigned char character) {
               return std::isxdigit(character) != 0;
           });
}

void parse_line(std::string_view line, SettingsAboutOsInfo &info)
{
    const auto words = whitespace_fields(line);
    if (words.size() == 4 && words[0] == "Raspberry" && words[1] == "Pi" &&
        words[2] == "reference" && is_valid_date(words[3])) {
        info.build_date = words[3];
        return;
    }

    const auto fields = comma_fields(line);
    if (fields.size() >= 4 &&
        whitespace_fields(fields[0]) ==
            std::vector<std::string>{"Generated", "using", "pi-gen"} &&
        !fields[1].empty() && is_git_commit(fields[2]) && !fields[3].empty()) {
        info.commit = fields[2];
    }
}

} // namespace

SettingsAboutOsInfo SettingsAboutInfoModel::parse_os_issue_text(std::string_view text)
{
    SettingsAboutOsInfo info;
    std::size_t begin = 0;
    while (begin <= text.size()) {
        const std::size_t end = text.find('\n', begin);
        parse_line(text.substr(
                       begin,
                       end == std::string_view::npos ? text.size() - begin : end - begin),
                   info);
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return info;
}

SettingsAboutOsInfo SettingsAboutInfoModel::read_os_issue_file(const std::string &path)
{
    std::ifstream file(path);
    if (!file) return {};

    std::ostringstream contents;
    contents << file.rdbuf();
    if (!file.good() && !file.eof()) return {};
    return parse_os_issue_text(contents.str());
}
