#include "bluetooth_page_model.hpp"

#include <cctype>
#include <charconv>
#include <sstream>
#include <system_error>
#include <utility>

namespace {

std::vector<std::string> split_record(const std::string &record)
{
    std::vector<std::string> columns;
    std::string column;
    std::istringstream input(record);
    while (std::getline(input, column, '\t')) columns.push_back(column);
    if (!record.empty() && record.back() == '\t') columns.emplace_back();
    return columns;
}

bool parse_int(const std::string &text, int &value)
{
    if (text.empty()) return false;
    int parsed = 0;
    const char *begin = text.data();
    const char *end = begin + text.size();
    const auto result = std::from_chars(begin, end, parsed, 10);
    if (result.ec != std::errc() || result.ptr != end) return false;
    value = parsed;
    return true;
}

bool parse_bool(const std::string &text, bool &value)
{
    int parsed = 0;
    if (!parse_int(text, parsed) || (parsed != 0 && parsed != 1)) return false;
    value = parsed != 0;
    return true;
}

} // namespace

bool BluetoothPageModel::decode_status_record(const std::string &record,
                                              BluetoothWireStatus &status)
{
    const auto columns = split_record(record);
    if (columns.size() != 4 || columns[1].empty()) return false;
    BluetoothWireStatus parsed;
    if (!parse_bool(columns[0], parsed.powered) ||
        !parse_bool(columns[2], parsed.discoverable))
        return false;
    parsed.address = columns[1];
    parsed.alias = columns[3];
    status = std::move(parsed);
    return true;
}

bool BluetoothPageModel::decode_device_record(const std::string &record,
                                              BluetoothWireDevice &device)
{
    const auto columns = split_record(record);
    if (columns.size() != 6 || columns[0].empty()) return false;
    BluetoothWireDevice parsed;
    if (!parse_int(columns[1], parsed.rssi) ||
        !parse_bool(columns[2], parsed.connected) ||
        !parse_bool(columns[3], parsed.paired) ||
        !parse_bool(columns[4], parsed.trusted))
        return false;
    parsed.address = columns[0];
    parsed.name = columns[5];
    device = std::move(parsed);
    return true;
}

void BluetoothPageModel::set_list_mode(BluetoothListMode mode)
{
    list_mode_ = mode;
    clear_selection();
    cancel_feedback();
}

bool BluetoothPageModel::begin_feedback()
{
    if (feedback_pending_) return false;
    feedback_pending_ = true;
    return true;
}

bool BluetoothPageModel::finish_feedback()
{
    if (!feedback_pending_) return false;
    feedback_pending_ = false;
    return true;
}

void BluetoothPageModel::rebuild_rows(const std::vector<BluetoothDeviceState> &devices)
{
    rows_.clear();
    bool has_devices = false;
    for (size_t index = 0; index < devices.size(); ++index) {
        const BluetoothDeviceState &device = devices[index];
        if (should_hide_device(device) ||
            (list_mode_ == BluetoothListMode::MANAGED && !device.connected))
            continue;
        if (!has_devices) {
            rows_.push_back({-1, list_mode_ == BluetoothListMode::MANAGED
                                     ? "Connected Devices"
                                     : "Discovered Devices",
                             true});
            has_devices = true;
        }
        rows_.push_back({static_cast<int>(index), nullptr, false});
    }

    if (list_selection_ >= static_cast<int>(rows_.size()))
        list_selection_ = rows_.empty() ? 0 : static_cast<int>(rows_.size()) - 1;
    if (!rows_.empty() && rows_[list_selection_].is_header)
        select_next_device(1);
}

void BluetoothPageModel::select_next_device(int direction)
{
    if (rows_.empty() || direction == 0)
        return;
    int index = list_selection_;
    for (size_t steps = 0; steps < rows_.size(); ++steps) {
        index += direction;
        if (index < 0 || index >= static_cast<int>(rows_.size()))
            return;
        if (!rows_[index].is_header) {
            list_selection_ = index;
            return;
        }
    }
}

int BluetoothPageModel::selected_device_index() const
{
    if (list_selection_ < 0 || list_selection_ >= static_cast<int>(rows_.size()))
        return -1;
    const BluetoothListRow &row = rows_[list_selection_];
    return row.is_header ? -1 : row.device_index;
}

void BluetoothPageModel::set_alias(std::string alias)
{
    alias_ = alias.empty() ? DEFAULT_ALIAS : std::move(alias);
}

void BluetoothPageModel::begin_alias_edit()
{
    alias_input_ = alias_.empty() ? DEFAULT_ALIAS : alias_;
    if (alias_input_.size() > ALIAS_INPUT_LIMIT)
        alias_input_.resize(ALIAS_INPUT_LIMIT);
}

bool BluetoothPageModel::append_alias_text(const char *text)
{
    if (!text)
        return false;
    bool changed = false;
    while (*text && alias_input_.size() < ALIAS_INPUT_LIMIT) {
        const unsigned char character = static_cast<unsigned char>(*text++);
        if (alias_character_allowed(character)) {
            alias_input_.push_back(static_cast<char>(character));
            changed = true;
        }
    }
    return changed;
}

bool BluetoothPageModel::erase_alias_character()
{
    if (alias_input_.empty())
        return false;
    alias_input_.pop_back();
    return true;
}

std::string BluetoothPageModel::sanitized_alias() const
{
    std::string result;
    result.reserve(alias_input_.size());
    for (unsigned char character : alias_input_) {
        if (alias_character_allowed(character))
            result.push_back(static_cast<char>(character));
    }
    if (result.empty())
        result = DEFAULT_ALIAS;
    if (result.size() > ALIAS_INPUT_LIMIT)
        result.resize(ALIAS_INPUT_LIMIT);
    return result;
}

bool BluetoothPageModel::alias_character_allowed(unsigned char character)
{
    return std::isprint(character) && character != '\t' && character != '\n' && character != '\r';
}

std::string BluetoothPageModel::normalized_mac_text(const std::string &text)
{
    std::string result;
    for (unsigned char character : text) {
        if (std::isxdigit(character))
            result.push_back(static_cast<char>(std::tolower(character)));
        else if (character != ':' && character != '-' && character != '_' && character != ' ')
            return {};
    }
    return result;
}

bool BluetoothPageModel::should_hide_device(const BluetoothDeviceState &device) const
{
    if (!named_only_)
        return false;
    if (device.name.empty())
        return true;
    const std::string name_hex = normalized_mac_text(device.name);
    const std::string address_hex = normalized_mac_text(device.address);
    return !name_hex.empty() && (name_hex == address_hex || name_hex.size() == 12);
}
