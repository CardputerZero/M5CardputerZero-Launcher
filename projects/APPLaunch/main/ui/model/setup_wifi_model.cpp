#include "setup_wifi_model.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace {

bool is_continuation(unsigned char value)
{
    return (value & 0xc0) == 0x80;
}

bool is_valid_utf8_text(const std::string &text)
{
    std::size_t index = 0;
    while (index < text.size()) {
        const unsigned char lead = static_cast<unsigned char>(text[index]);
        std::size_t length = 0;
        unsigned int codepoint = 0;
        if (lead < 0x80) {
            length = 1;
            codepoint = lead;
        } else if ((lead & 0xe0) == 0xc0) {
            length = 2;
            codepoint = lead & 0x1f;
        } else if ((lead & 0xf0) == 0xe0) {
            length = 3;
            codepoint = lead & 0x0f;
        } else if ((lead & 0xf8) == 0xf0) {
            length = 4;
            codepoint = lead & 0x07;
        } else {
            return false;
        }
        if (index + length > text.size()) return false;
        for (std::size_t offset = 1; offset < length; ++offset) {
            const unsigned char value = static_cast<unsigned char>(text[index + offset]);
            if (!is_continuation(value)) return false;
            codepoint = (codepoint << 6) | (value & 0x3f);
        }
        if ((length == 2 && codepoint < 0x80) ||
            (length == 3 && codepoint < 0x800) ||
            (length == 4 && codepoint < 0x10000) ||
            (codepoint >= 0xd800 && codepoint <= 0xdfff) || codepoint > 0x10ffff ||
            codepoint < 0x20 || codepoint == 0x7f)
            return false;
        index += length;
    }
    return true;
}

std::size_t previous_utf8_start(const std::string &text, std::size_t cursor)
{
    if (cursor == 0) return 0;
    std::size_t start = cursor - 1;
    while (start > 0 && is_continuation(static_cast<unsigned char>(text[start]))) --start;
    return start;
}

std::size_t next_utf8_end(const std::string &text, std::size_t cursor)
{
    if (cursor >= text.size()) return text.size();
    ++cursor;
    while (cursor < text.size() &&
           is_continuation(static_cast<unsigned char>(text[cursor]))) ++cursor;
    return cursor;
}

std::size_t utf8_cursor_position(const std::string &text, std::size_t cursor)
{
    std::size_t position = 0;
    for (std::size_t index = 0; index < cursor; ++index) {
        if (!is_continuation(static_cast<unsigned char>(text[index]))) ++position;
    }
    return position;
}

} // namespace

void SetupWifiListModel::begin_scan()
{
    scanning_ = true;
}

void SetupWifiListModel::apply_scan(std::vector<SetupWifiAccessPoint> access_points)
{
    std::string selected_ssid;
    if (const auto *current = selected()) selected_ssid = current->ssid;

    access_points_ = std::move(access_points);
    scanning_ = false;
    if (access_points_.empty()) {
        selected_index_ = 0;
        return;
    }

    const auto selected = std::find_if(access_points_.begin(), access_points_.end(),
        [&](const SetupWifiAccessPoint &access_point) {
            return !selected_ssid.empty() && access_point.ssid == selected_ssid;
        });
    if (selected != access_points_.end())
        selected_index_ = static_cast<int>(std::distance(access_points_.begin(), selected));
    else
        selected_index_ = std::clamp(selected_index_, 0, static_cast<int>(access_points_.size()) - 1);
}

bool SetupWifiListModel::move_selection(int delta)
{
    if (access_points_.empty() || delta == 0) return false;
    const int next = std::clamp(selected_index_ + delta, 0,
                                static_cast<int>(access_points_.size()) - 1);
    if (next == selected_index_) return false;
    selected_index_ = next;
    return true;
}

const SetupWifiAccessPoint *SetupWifiListModel::selected() const
{
    return at(selected_index_);
}

const SetupWifiAccessPoint *SetupWifiListModel::at(int index) const
{
    return index >= 0 && index < static_cast<int>(access_points_.size())
        ? &access_points_[static_cast<std::size_t>(index)] : nullptr;
}

SetupWifiListSnapshot SetupWifiListViewModel::snapshot() const
{
    SetupWifiListSnapshot result;
    const SetupWifiStatus &status = model_.status();
    result.title = status.connected
        ? "Connected WiFi: " + status.ssid + "  " + status.ip
        : "WiFi: Not connected";
    result.empty_message = !scan_error_.empty() ? scan_error_ : model_.scanning()
        ? "Scanning for WiFi networks..."
        : "No networks found. Press R to rescan.";
    result.hint = model_.scanning()
        ? "Scanning...  OK:connect  D:forget  ESC:back"
        : "OK:connect  R:rescan  D:forget  ESC:back";

    const int count = static_cast<int>(model_.size());
    int offset = model_.selected_index() - VISIBLE_ROWS / 2;
    offset = std::max(0, std::min(offset, count - VISIBLE_ROWS));
    for (int visible = 0; visible < VISIBLE_ROWS && offset + visible < count; ++visible) {
        const int index = offset + visible;
        const auto *access_point = model_.at(index);
        if (!access_point) continue;
        SetupWifiRowViewModel row;
        row.ssid = access_point->ssid + (access_point->saved ? " *" : "");
        row.security = access_point->security.empty() ? "Open" : access_point->security;
        row.signal = std::to_string(access_point->signal) + "%";
        row.selected = index == model_.selected_index();
        row.in_use = access_point->in_use;
        result.rows.push_back(std::move(row));
    }
    return result;
}

SetupWifiPasswordModel::~SetupWifiPasswordModel()
{
    secure_clear_password();
}

void SetupWifiPasswordModel::begin(std::string ssid, std::string security)
{
    ssid_ = std::move(ssid);
    security_ = std::move(security);
    secure_clear_password();
    cursor_byte_ = 0;
}

void SetupWifiPasswordModel::reset()
{
    ssid_.clear();
    security_.clear();
    secure_clear_password();
    cursor_byte_ = 0;
}

std::string SetupWifiPasswordModel::validation_error() const
{
    if (ssid_.empty()) return "SSID required";
    if (password_.empty()) return "Password required";
    std::string security = security_;
    std::transform(security.begin(), security.end(), security.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    if (security.find("802.1X") != std::string::npos ||
        security.find("EAP") != std::string::npos ||
        security.find("ENTERPRISE") != std::string::npos)
        return "Enterprise WiFi is not supported by this form";
    if (security.find("WPA") == std::string::npos) return {};
    if (password_.size() >= 8 && password_.size() <= 63) return {};
    if (password_.size() == 64 && std::all_of(password_.begin(), password_.end(), [](unsigned char ch) {
            return std::isxdigit(ch) != 0;
        })) return {};
    return "WPA password must be 8-63 chars or 64 hex";
}

bool SetupWifiPasswordModel::can_submit() const
{
    return validation_error().empty();
}

bool SetupWifiPasswordModel::append(const std::string &text)
{
    if (text.empty() || password_.size() + text.size() > MAX_PASSWORD_BYTES)
        return false;
    if (!is_valid_utf8_text(text)) return false;
    password_.insert(cursor_byte_, text);
    cursor_byte_ += text.size();
    return true;
}

bool SetupWifiPasswordModel::erase_last()
{
    if (cursor_byte_ == 0) return false;
    const std::size_t start = previous_utf8_start(password_, cursor_byte_);
    volatile char *bytes = password_.data();
    for (std::size_t index = start; index < cursor_byte_; ++index) bytes[index] = '\0';
    password_.erase(start, cursor_byte_ - start);
    cursor_byte_ = start;
    return true;
}

bool SetupWifiPasswordModel::move_cursor_left()
{
    const std::size_t next = previous_utf8_start(password_, cursor_byte_);
    if (next == cursor_byte_) return false;
    cursor_byte_ = next;
    return true;
}

bool SetupWifiPasswordModel::move_cursor_right()
{
    const std::size_t next = next_utf8_end(password_, cursor_byte_);
    if (next == cursor_byte_) return false;
    cursor_byte_ = next;
    return true;
}

std::size_t SetupWifiPasswordModel::cursor_position() const
{
    return utf8_cursor_position(password_, cursor_byte_);
}

void SetupWifiPasswordModel::clear_password()
{
    secure_clear_password();
    cursor_byte_ = 0;
}

bool SetupWifiSsidModel::append(const std::string &text)
{
    if (text.empty() || ssid_.size() + text.size() > MAX_SSID_BYTES ||
        !is_valid_utf8_text(text))
        return false;
    ssid_.insert(cursor_byte_, text);
    cursor_byte_ += text.size();
    return true;
}

bool SetupWifiSsidModel::erase_last()
{
    if (cursor_byte_ == 0) return false;
    const std::size_t start = previous_utf8_start(ssid_, cursor_byte_);
    ssid_.erase(start, cursor_byte_ - start);
    cursor_byte_ = start;
    return true;
}

bool SetupWifiSsidModel::move_cursor_left()
{
    const std::size_t next = previous_utf8_start(ssid_, cursor_byte_);
    if (next == cursor_byte_) return false;
    cursor_byte_ = next;
    return true;
}

bool SetupWifiSsidModel::move_cursor_right()
{
    const std::size_t next = next_utf8_end(ssid_, cursor_byte_);
    if (next == cursor_byte_) return false;
    cursor_byte_ = next;
    return true;
}

std::size_t SetupWifiSsidModel::cursor_position() const
{
    return utf8_cursor_position(ssid_, cursor_byte_);
}

void SetupWifiPasswordModel::secure_clear_password()
{
    volatile char *bytes = password_.data();
    for (std::size_t index = 0; index < password_.size(); ++index) bytes[index] = '\0';
    password_.clear();
}

std::string SetupWifiPasswordModel::masked_display() const
{
    std::size_t codepoints = 0;
    for (unsigned char value : password_)
        if (!is_continuation(value)) ++codepoints;
    return std::string(codepoints, '*') + '_';
}

SetupWifiFeedbackModel::Token SetupWifiFeedbackModel::begin()
{
    if (pending_) return 0;
    pending_ = true;
    if (++generation_ == 0) ++generation_;
    return generation_;
}

bool SetupWifiFeedbackModel::complete(Token token)
{
    if (!pending_ || token == 0 || token != generation_) return false;
    pending_ = false;
    return true;
}

bool SetupWifiFeedbackModel::cancel(Token token)
{
    if (!pending_ || token == 0 || token != generation_) return false;
    pending_ = false;
    return true;
}
