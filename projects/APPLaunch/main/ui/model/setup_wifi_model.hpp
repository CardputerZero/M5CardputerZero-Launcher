#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

struct SetupWifiAccessPoint
{
    std::string ssid;
    std::string security;
    int signal = 0;
    bool in_use = false;
    bool saved = false;
};

struct SetupWifiStatus
{
    bool connected = false;
    std::string ssid;
    std::string ip;
};

class SetupWifiListModel
{
public:
    void begin_scan();
    void cancel_scan() { scanning_ = false; }
    void apply_scan(std::vector<SetupWifiAccessPoint> access_points);
    void set_status(SetupWifiStatus status) { status_ = std::move(status); }
    bool move_selection(int delta);

    bool scanning() const { return scanning_; }
    int selected_index() const { return selected_index_; }
    std::size_t size() const { return access_points_.size(); }
    const std::vector<SetupWifiAccessPoint> &access_points() const { return access_points_; }
    const SetupWifiAccessPoint *selected() const;
    const SetupWifiAccessPoint *at(int index) const;
    const SetupWifiStatus &status() const { return status_; }

private:
    std::vector<SetupWifiAccessPoint> access_points_;
    int selected_index_ = 0;
    bool scanning_ = false;
    SetupWifiStatus status_;
};

struct SetupWifiRowViewModel
{
    std::string ssid;
    std::string security;
    std::string signal;
    bool selected = false;
    bool in_use = false;
};

struct SetupWifiListSnapshot
{
    std::string title;
    std::string empty_message;
    std::string hint;
    std::vector<SetupWifiRowViewModel> rows;
};

class SetupWifiListViewModel
{
public:
    static constexpr int VISIBLE_ROWS = 5;
    static constexpr std::uint32_t KEY_REPEAT_INTERVAL_MS = 60;

    void begin_scan() { model_.begin_scan(); }
    void cancel_scan() { model_.cancel_scan(); }
    void apply_scan(std::vector<SetupWifiAccessPoint> access_points)
    {
        scan_error_.clear();
        model_.apply_scan(std::move(access_points));
    }
    void fail_scan(std::string message)
    {
        model_.apply_scan({});
        scan_error_ = std::move(message);
    }
    void clear_scan_error() { scan_error_.clear(); }
    void set_status(SetupWifiStatus status) { model_.set_status(std::move(status)); }
    bool move_selection(int delta) { return model_.move_selection(delta); }
    SetupWifiListSnapshot snapshot() const;

    bool scanning() const { return model_.scanning(); }
    int selected_index() const { return model_.selected_index(); }
    std::size_t size() const { return model_.size(); }
    const SetupWifiAccessPoint *selected() const { return model_.selected(); }
    const SetupWifiAccessPoint *at(int index) const { return model_.at(index); }

private:
    SetupWifiListModel model_;
    std::string scan_error_;
};

template <typename CallbackTimer, typename ActiveTimer, typename Token>
constexpr bool setup_wifi_feedback_timer_callback_ready(
    CallbackTimer callback_timer, ActiveTimer active_timer, Token token) noexcept
{
    return callback_timer && callback_timer == active_timer && token != Token{};
}

template <typename Target, typename CurrentTarget, typename TrackedScreen>
constexpr bool setup_wifi_feedback_screen_delete_allowed(
    Target target, CurrentTarget current_target, TrackedScreen tracked_screen) noexcept
{
    return target && target == current_target && target == tracked_screen;
}

class SetupWifiPasswordModel
{
public:
    // WPA passphrases use up to 63 bytes; a raw hexadecimal PSK uses 64.
    static constexpr std::size_t MAX_PASSWORD_BYTES = 64;

    ~SetupWifiPasswordModel();

    void begin(std::string ssid, std::string security = {});
    void reset();
    bool append(const std::string &text);
    bool erase_last();
    bool move_cursor_left();
    bool move_cursor_right();
    void clear_password();

    bool can_submit() const;
    std::string validation_error() const;
    const std::string &ssid() const { return ssid_; }
    const std::string &password() const { return password_; }
    std::size_t cursor_position() const;
    std::string masked_display() const;

private:
    void secure_clear_password();
    std::string ssid_;
    std::string security_;
    std::string password_;
    std::size_t cursor_byte_ = 0;
};

class SetupWifiSsidModel
{
public:
    static constexpr std::size_t MAX_SSID_BYTES = 32;

    void reset() { ssid_.clear(); cursor_byte_ = 0; }
    bool append(const std::string &text);
    bool erase_last();
    bool move_cursor_left();
    bool move_cursor_right();
    bool can_submit() const { return !ssid_.empty(); }
    const std::string &ssid() const { return ssid_; }
    std::size_t cursor_position() const;

private:
    std::string ssid_;
    std::size_t cursor_byte_ = 0;
};

class SetupWifiFeedbackModel
{
public:
    using Token = std::uint64_t;

    Token begin();
    bool complete(Token token);
    bool cancel(Token token);
    bool pending() const { return pending_; }

private:
    bool pending_ = false;
    Token generation_ = 0;
};
