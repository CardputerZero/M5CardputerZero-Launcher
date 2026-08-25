#include "sound_card_model.hpp"

#include <charconv>
#include <limits>
#include <sstream>

namespace setting {
namespace {

std::string trim(const std::string &text)
{
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

bool parse_integer(const std::string &text, int &value)
{
    if (text.empty()) return false;
    const char *first = text.data();
    const char *last = first + text.size();
    const auto result = std::from_chars(first, last, value);
    return result.ec == std::errc{} && result.ptr == last;
}

bool parse_limits(const std::string &line, int &minimum, int &maximum)
{
    const size_t marker = line.find("Limits:");
    if (marker == std::string::npos) return false;
    std::string values = trim(line.substr(marker + 7));
    for (const char *prefix : {"Playback ", "Capture "}) {
        if (values.rfind(prefix, 0) == 0) {
            values = values.substr(std::char_traits<char>::length(prefix));
            break;
        }
    }
    const size_t separator = values.find(" - ");
    if (separator == std::string::npos) return false;
    int parsed_minimum = 0;
    int parsed_maximum = 0;
    if (!parse_integer(trim(values.substr(0, separator)), parsed_minimum) ||
        !parse_integer(trim(values.substr(separator + 3)), parsed_maximum))
        return false;
    minimum = parsed_minimum;
    maximum = parsed_maximum;
    return true;
}

bool is_value_line(const std::string &line)
{
    static constexpr const char *prefixes[] = {
        "Mono:", "Front Left:", "Front Right:", "Rear Left:", "Rear Right:",
        "Center:", "LFE:", "Side Left:", "Side Right:", "Capture:", "Playback:",
        "Item0:",
    };
    for (const char *prefix : prefixes) {
        if (line.rfind(prefix, 0) == 0) return true;
    }
    return false;
}

std::string extract_value_text(const std::string &line)
{
    return trim(line);
}

int parse_current_value(const std::string &line, int fallback)
{
    const size_t separator = line.find(": ");
    if (separator == std::string::npos) return fallback;
    const char *cursor = line.c_str() + separator + 2;
    while (*cursor && *cursor != '-' && (*cursor < '0' || *cursor > '9')) ++cursor;
    if (!*cursor) return fallback;
    const char *end = cursor;
    if (*end == '-') ++end;
    while (*end >= '0' && *end <= '9') ++end;
    int value = 0;
    const auto result = std::from_chars(cursor, end, value);
    return result.ec == std::errc{} && result.ptr == end ? value : fallback;
}

} // namespace

std::vector<SoundCardInfo> SoundCardModel::parse_cards(const std::string &data)
{
    std::vector<SoundCardInfo> cards;
    std::istringstream lines(data);
    std::string line;
    while (std::getline(lines, line)) {
        const size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        int index = 0;
        if (!parse_integer(line.substr(0, tab), index) || index < 0) continue;
        cards.push_back({index, line.substr(tab + 1)});
    }
    return cards;
}

std::vector<SoundControlInfo> SoundCardModel::parse_controls(const std::string &data)
{
    std::vector<SoundControlInfo> controls;
    std::istringstream lines(data);
    std::string line;
    while (std::getline(lines, line)) {
        std::vector<std::string> columns;
        std::istringstream row(line);
        std::string column;
        while (std::getline(row, column, '\t')) columns.push_back(column);
        if (columns.size() < 7) continue;
        int minimum = 0;
        int maximum = 0;
        int step = 0;
        int current = 0;
        if (!parse_integer(columns[2], minimum) || !parse_integer(columns[3], maximum) ||
            !parse_integer(columns[4], step) || !parse_integer(columns[6], current))
            continue;
        controls.push_back({columns[0], columns[1], minimum, maximum, step,
                            columns[5], current});
    }
    return controls;
}

SoundControlInfo SoundCardModel::parse_detail(const std::string &data,
                                              const SoundControlInfo &fallback)
{
    SoundControlInfo detail;
    detail.name = fallback.name;
    std::istringstream lines(data);
    std::string line;
    while (std::getline(lines, line)) {
        const std::string text = trim(line);
        if (text.rfind("Capabilities:", 0) == 0) {
            detail.type = text.find("enum") != std::string::npos ? "ENUMERATED" : "INTEGER";
        } else if (text.rfind("Limits:", 0) == 0) {
            parse_limits(text, detail.min_value, detail.max_value);
        } else if (detail.current_text.empty() && is_value_line(text)) {
            detail.current_text = extract_value_text(text);
            detail.current_value = parse_current_value(text, detail.current_value);
        }
    }
    if (detail.max_value == 0 && fallback.max_value != 0) {
        detail.min_value = fallback.min_value;
        detail.max_value = fallback.max_value;
    }
    return detail;
}

int SoundCardModel::parse_value(const std::string &text, int fallback)
{
    int value = 0;
    return parse_integer(text, value) ? value : fallback;
}

int SoundCardModel::clamp_value(int value, const SoundControlInfo &control)
{
    if (control.max_value <= control.min_value) return value;
    if (value < control.min_value) return control.min_value;
    if (value > control.max_value) return control.max_value;
    return value;
}

bool SoundCardTransitionModel::begin()
{
    if (pending_) return false;
    pending_ = true;
    return true;
}

bool SoundCardTransitionModel::complete()
{
    if (!pending_) return false;
    pending_ = false;
    return true;
}

} // namespace setting
