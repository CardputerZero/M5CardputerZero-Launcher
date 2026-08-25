#pragma once

#include <algorithm>
#include <charconv>
#include <cctype>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace ui_test_soundcard {

struct Card {
    int index = -1;
    std::string label;
};

struct Control {
    Control() = default;

    Control(std::string control_name,
            std::string control_type,
            int control_minimum,
            int control_maximum,
            int control_step,
            std::string control_current_text,
            int control_current_value)
        : name(std::move(control_name)),
          type(std::move(control_type)),
          minimum(control_minimum),
          maximum(control_maximum),
          step(control_step),
          current_text(std::move(control_current_text)),
          current_value(control_current_value)
    {
    }

    std::string name;
    std::string type;
    int minimum = 0;
    int maximum = 0;
    int step = 1;
    std::string current_text;
    int current_value = 0;
    bool writable = true;
    std::vector<std::string> options;
    std::string current_option;
};

namespace soundcard_model_detail {

inline std::string trim(const std::string &text)
{
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

inline bool parse_integer(const std::string &text, int &value)
{
    if (text.empty()) return false;
    const char *first = text.data();
    const char *last = first + text.size();
    const auto result = std::from_chars(first, last, value);
    return result.ec == std::errc{} && result.ptr == last;
}

inline bool parse_limits(const std::string &line, int &minimum, int &maximum)
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

inline bool is_value_line(const std::string &line)
{
    static constexpr const char *prefixes[] = {
        "Mono:", "Front Left:", "Front Right:", "Rear Left:", "Rear Right:",
        "Center:", "LFE:", "Side Left:", "Side Right:", "Capture:", "Playback:",
        "Item0:", "Item1:", "Item2:", "Item3:", "Item4:", "Item5:",
        "Item6:", "Item7:", "Item8:", "Item9:",
    };
    for (const char *prefix : prefixes) {
        if (line.rfind(prefix, 0) == 0) return true;
    }
    if (line.rfind("Item", 0) != 0) return false;
    size_t cursor = 4;
    while (cursor < line.size() && std::isdigit(static_cast<unsigned char>(line[cursor]))) ++cursor;
    return cursor > 4 && cursor < line.size() && line[cursor] == ':';
}

inline bool is_item_line(const std::string &line)
{
    if (line.rfind("Item", 0) != 0) return false;
    size_t cursor = 4;
    while (cursor < line.size() && std::isdigit(static_cast<unsigned char>(line[cursor]))) ++cursor;
    return cursor > 4 && cursor < line.size() && line[cursor] == ':';
}

inline std::string item_text(const std::string &line)
{
    const size_t separator = line.find(':');
    if (separator == std::string::npos) return {};
    std::string value = trim(line.substr(separator + 1));
    if (value.size() >= 2 && value.front() == '\'' && value.back() == '\'')
        value = value.substr(1, value.size() - 2);
    return value;
}

inline std::vector<std::string> item_values(const std::string &line)
{
    std::vector<std::string> values;
    size_t cursor = line.find('\'');
    while (cursor != std::string::npos) {
        const size_t end = line.find('\'', cursor + 1);
        if (end == std::string::npos) break;
        values.push_back(line.substr(cursor + 1, end - cursor - 1));
        cursor = line.find('\'', end + 1);
    }
    return values;
}

inline int parse_current_value(const std::string &line, int fallback)
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

inline bool writable_capabilities(const std::string &line)
{
    const size_t marker = line.find(':');
    if (marker == std::string::npos) return false;
    std::istringstream values(line.substr(marker + 1));
    std::string token;
    while (values >> token) {
        for (char &character : token)
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        if (token.rfind("pvolume", 0) == 0 || token.rfind("cvolume", 0) == 0 ||
            token == "volume" || token.rfind("pswitch", 0) == 0 ||
            token.rfind("cswitch", 0) == 0 || token == "switch" ||
            token.rfind("penum", 0) == 0 || token.rfind("cenum", 0) == 0 ||
            token == "enum" || token == "boolean" || token.rfind("pboolean", 0) == 0 ||
            token.rfind("cboolean", 0) == 0)
            return true;
    }
    return false;
}

inline bool has_non_whitespace(const std::string &text)
{
    return text.find_first_not_of(" \t\r\n") != std::string::npos;
}

}

class SoundCardModel {
public:
    static std::vector<Card> parse_cards(const std::string &data)
    {
        std::vector<Card> cards;
        std::istringstream lines(data);
        std::string line;
        while (std::getline(lines, line)) {
            const size_t tab = line.find('\t');
            if (tab == std::string::npos) continue;
            int index = 0;
            if (!soundcard_model_detail::parse_integer(line.substr(0, tab), index) || index < 0)
                continue;
            cards.push_back({index, line.substr(tab + 1)});
        }
        return cards;
    }

    static std::vector<Control> parse_controls(const std::string &data)
    {
        std::vector<Control> controls;
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
            if (!soundcard_model_detail::parse_integer(columns[2], minimum) ||
                !soundcard_model_detail::parse_integer(columns[3], maximum) ||
                !soundcard_model_detail::parse_integer(columns[4], step) ||
                !soundcard_model_detail::parse_integer(columns[6], current))
                continue;
            Control control{columns[0], columns[1], minimum, maximum, step,
                             columns[5], current};
            if (!has_control_name(control)) continue;
            if (control.type == "ENUMERATED")
                control.current_option = soundcard_model_detail::item_text(control.current_text);
            controls.push_back(std::move(control));
        }
        return controls;
    }

    static Control parse_detail(const std::string &data, const Control &fallback)
    {
        Control detail;
        detail.name = fallback.name;
        detail.type = fallback.type;
        detail.minimum = fallback.minimum;
        detail.maximum = fallback.maximum;
        detail.step = fallback.step;
        detail.writable = fallback.writable;
        bool saw_capabilities = false;
        bool saw_options = false;
        std::istringstream lines(data);
        std::string line;
        while (std::getline(lines, line)) {
            const std::string text = soundcard_model_detail::trim(line);
            if (text.rfind("Capabilities:", 0) == 0) {
                saw_capabilities = true;
                detail.type = text.find("enum") != std::string::npos ? "ENUMERATED" : "INTEGER";
                detail.writable = soundcard_model_detail::writable_capabilities(text);
            } else if (text.rfind("Limits:", 0) == 0) {
                soundcard_model_detail::parse_limits(text, detail.minimum, detail.maximum);
            } else if (text.rfind("Items:", 0) == 0) {
                saw_options = true;
                for (std::string option : soundcard_model_detail::item_values(text)) {
                    if (!option.empty() &&
                        std::find(detail.options.begin(), detail.options.end(), option) ==
                            detail.options.end())
                        detail.options.push_back(std::move(option));
                }
            } else if (soundcard_model_detail::is_item_line(text)) {
                saw_options = true;
                const std::string option = soundcard_model_detail::item_text(text);
                if (!option.empty() &&
                    std::find(detail.options.begin(), detail.options.end(), option) ==
                        detail.options.end())
                    detail.options.push_back(option);
                if (detail.current_text.empty()) {
                    detail.current_text = text;
                    detail.current_option = option;
                }
            } else if (detail.current_text.empty() &&
                       soundcard_model_detail::is_value_line(text)) {
                detail.current_text = text;
                detail.current_value =
                    soundcard_model_detail::parse_current_value(text, detail.current_value);
            }
        }
        if (detail.maximum == 0 && fallback.maximum != 0) {
            detail.minimum = fallback.minimum;
            detail.maximum = fallback.maximum;
        }
        if (!saw_capabilities) detail.writable = fallback.writable;
        if (!saw_options) {
            detail.options = fallback.options;
            detail.current_option = fallback.current_option;
        }
        if (detail.type == "ENUMERATED" && detail.current_option.empty())
            detail.current_option = soundcard_model_detail::item_text(detail.current_text);
        return detail;
    }

    static int parse_value(const std::string &text, int fallback)
    {
        int value = 0;
        return soundcard_model_detail::parse_integer(text, value) ? value : fallback;
    }

    static int clamp_value(int value, const Control &control)
    {
        if (control.maximum <= control.minimum) return value;
        if (value < control.minimum) return control.minimum;
        if (value > control.maximum) return control.maximum;
        return value;
    }

    static bool has_detail_payload(const std::string &data)
    {
        if (!soundcard_model_detail::has_non_whitespace(data)) return false;
        std::istringstream lines(data);
        std::string line;
        while (std::getline(lines, line)) {
            const std::string text = soundcard_model_detail::trim(line);
            if (text.rfind("Capabilities:", 0) == 0 ||
                text.rfind("Limits:", 0) == 0 ||
                text.rfind("Items:", 0) == 0 ||
                soundcard_model_detail::is_item_line(text) ||
                soundcard_model_detail::is_value_line(text))
                return true;
        }
        return false;
    }

    static bool has_payload_content(const std::string &data)
    {
        return soundcard_model_detail::has_non_whitespace(data);
    }

    static bool has_control_name(const Control &control)
    {
        if (control.name.empty() || control.name.size() > 128) return false;
        for (const char character : control.name)
            if (character == '\0' || character == '\t' || character == '\r' || character == '\n')
                return false;
        return true;
    }

    static bool has_safe_wire_value(const std::string &value)
    {
        if (value.empty() || value.size() > 128) return false;
        for (const char character : value)
            if (character == '\0' || character == '\t' || character == '\r' || character == '\n')
                return false;
        return true;
    }
};

}
