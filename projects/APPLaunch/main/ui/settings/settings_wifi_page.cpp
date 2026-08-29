#include "settings_wifi_page.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <iterator>
#include <new>
#include <utility>
#include <vector>

#include "cp0_font_service.hpp"

namespace {

class settings_wifi_com {
private:
    enum class WifiDefaultDef : std::size_t {
        kMaxPasswordBytes = 64,
    };

public:
    struct AccessPoint {
        std::string ssid;
        std::string security;
        int signal  = 0;
        bool in_use = false;
        bool saved  = false;
    };

    struct Status {
        bool connected = false;
        std::string ssid;
        std::string ip;
        int signal    = 0;
        bool ethernet = false;
    };

    template <std::size_t Size>
    static std::string bounded_string(const char (&value)[Size])
    {
        std::size_t length = 0;
        while (length < Size && value[length] != '\0') ++length;
        return std::string(value, length);
    }

    static bool valid_text(const std::string &value, std::size_t max_bytes, bool allow_empty)
    {
        if (!allow_empty && value.empty()) return false;
        if (value.size() >= max_bytes) return false;
        return std::all_of(value.begin(), value.end(),
                           [](unsigned char byte) { return byte >= 0x20u && byte != 0x7Fu; });
    }

    static int validate_credentials(const std::string &ssid, const std::string &password)
    {
        if (!valid_text(ssid, CP0_WIFI_SSID_MAX, false) || !valid_text(password, static_cast<std::size_t>(WifiDefaultDef::kMaxPasswordBytes) + 1, true))
            return CP0_WIFI_ERROR_INVALID;
        return 0;
    }

    static bool is_open_security(const std::string &security)
    {
        if (security.empty()) return true;
        std::string normalized = security;
        std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                       [](unsigned char byte) { return static_cast<char>(std::toupper(byte)); });
        return normalized == "OPEN" || normalized == "NONE" || normalized == "--";
    }

    static int radio_enabled()
    {
        return cp0_wifi_radio_enabled();
    }

    static int radio_set_enabled(bool enabled)
    {
        return cp0_wifi_radio_set_enabled(enabled ? 1 : 0);
    }

    static int read_status(Status &status)
    {
        status = {};
        cp0_wifi_status_t raw{};
        const int result = cp0_wifi_status_read(&raw);
        if (result != 0) return result;

        status.connected = raw.connected != 0;
        status.ssid      = bounded_string(raw.ssid);
        status.ip        = bounded_string(raw.ip);
        status.signal    = std::clamp(raw.signal, 0, 100);
        status.ethernet  = raw.ethernet != 0;
        return 0;
    }

    static int scan(std::vector<AccessPoint> &access_points, int max_entries)
    {
        access_points.clear();
        if (max_entries <= 0) return 0;

        const int limit = std::clamp(max_entries, 1, CP0_WIFI_AP_MAX);
        std::vector<cp0_wifi_ap_t> raw(static_cast<std::size_t>(limit));
        const int result = cp0_wifi_scan(raw.data(), limit);
        if (result < 0) return result;

        const int count = std::min(result, limit);
        access_points.reserve(static_cast<std::size_t>(count));
        for (int index = 0; index < count; ++index) {
            const cp0_wifi_ap_t &source = raw[static_cast<std::size_t>(index)];
            AccessPoint access_point;
            access_point.ssid     = bounded_string(source.ssid);
            access_point.security = bounded_string(source.security);
            access_point.signal   = std::clamp(source.signal, 0, 100);
            access_point.in_use   = source.in_use != 0;
            access_point.saved    = source.saved != 0;
            if (!access_point.ssid.empty()) access_points.push_back(std::move(access_point));
        }
        return static_cast<int>(access_points.size());
    }

    static int connect(const std::string &ssid, const std::string &password)
    {
        const int validation = validate_credentials(ssid, password);
        if (validation != 0) return validation;
        return cp0_wifi_connect(ssid.c_str(), password.empty() ? nullptr : password.c_str());
    }

    static int connect_hidden(const std::string &ssid, const std::string &password)
    {
        const int validation = validate_credentials(ssid, password);
        if (validation != 0) return validation;
        return cp0_wifi_connect_hidden(ssid.c_str(), password.empty() ? nullptr : password.c_str());
    }

    static int profile_forget(const std::string &ssid)
    {
        if (!valid_text(ssid, CP0_WIFI_SSID_MAX, false)) return CP0_WIFI_ERROR_INVALID;
        return cp0_wifi_profile_forget(ssid.c_str());
    }

    static int profile_disconnect_active()
    {
        return cp0_wifi_disconnect_active();
    }
};

} // namespace

using WifiAccessPoint = settings_wifi_com::AccessPoint;
using WifiStatus = settings_wifi_com::Status;

struct LvSettingWifiScanPage3::ScanState {
    std::atomic_bool stop{false};
    std::weak_ptr<bool> lifetime;
    LvSettingWifiScanPage3 *owner = nullptr;
    uint64_t generation           = 0;
    std::atomic_bool dispatch_failed{false};
};

struct LvSettingWifiScanPage3::ScanResult {
    std::shared_ptr<ScanState> state;
    std::array<WifiAccessPoint, static_cast<std::size_t>(LayoutMetric::ApMax)> access_points{};
    int count = 0;
    WifiStatus status;
    bool status_valid = false;
};

struct LvSettingWifiScanPage3::ConnectionState {
    std::atomic_bool stop{false};
    std::weak_ptr<bool> lifetime;
    LvSettingWifiScanPage3 *owner = nullptr;
    NetworkOperation operation    = NetworkOperation::Connect;
    ConnectionOrigin origin       = ConnectionOrigin::OpenNetwork;
    bool disconnect_active        = false;
    std::string ssid;
    std::string password;
    std::string security;
    uint64_t generation = 0;
    std::atomic_bool dispatch_failed{false};
};

struct LvSettingWifiScanPage3::ConnectionResult {
    std::shared_ptr<ConnectionState> state;
    int result = CP0_WIFI_ERROR_SERVICE;
    WifiStatus status;
    bool status_valid = false;
};

struct LvSettingWifiScanPage3::WifiData {
    std::vector<WifiAccessPoint> access_points;
    WifiStatus status;
};

LvSettingWifiScanPage3::LvSettingWifiScanPage3()
    : wifi_data_(std::make_unique<WifiData>())
{
}

LvSettingWifiScanPage3::LvSettingWifiScanPage3(lv_obj_t *parent, const NodeIter &parent_node, std::function<void()> back_callback)
    : LvSettingWifiScanPage3(parent, parent_node, std::move(back_callback), false, true)
{
}

LvSettingWifiScanPage3::LvSettingWifiScanPage3(lv_obj_t *parent, const NodeIter &parent_node, std::function<void()> back_callback,
                           bool hidden_network)
    : LvSettingWifiScanPage3(parent, parent_node, std::move(back_callback), hidden_network, true)
{
}

LvSettingWifiScanPage3::LvSettingWifiScanPage3(lv_obj_t *parent, const NodeIter &parent_node, std::function<void()> back_callback,
                           bool hidden_network, bool wifi_power_enabled)
    : parent_node_(parent_node),
      wifi_data_(std::make_unique<WifiData>()),
      hidden_network_(hidden_network),
      wifi_power_enabled_(wifi_power_enabled)
{
    LeaveSelfPage = std::move(back_callback);
    initialize(parent);
}

void LvSettingWifiScanPage3::AnimateNextIn(std::function<void()> animate_over_func){
        if (animate_over_func) animate_over_func();
    }

void LvSettingWifiScanPage3::AnimateNextOut(std::function<void()> animate_over_func){
        if (animate_over_func) animate_over_func();
    }

void LvSettingWifiScanPage3::LoadNextPage(){
    }

void LvSettingWifiScanPage3::LeaveNextPage(){
        stop_scan();
        stop_connection();
        if (LeaveSelfPage) LeaveSelfPage();
    }

LvSettingWifiScanPage3::~LvSettingWifiScanPage3(){
        if (keyboard_root_ && keyboard_event_dsc_) {
            lv_obj_remove_event_dsc(keyboard_root_, keyboard_event_dsc_);
            keyboard_event_dsc_ = nullptr;
        }
        restore_text_input_mode();
        ++generation_;
        if (ui_dispatch_timer_) {
            lv_timer_delete(ui_dispatch_timer_);
            ui_dispatch_timer_ = nullptr;
        }
        if (ui_dispatch_) {
            std::lock_guard<std::mutex> lock(ui_dispatch_->mutex);
            ui_dispatch_->stopped = true;
            ui_dispatch_->pending.clear();
        }
        lifetime_token_.reset();
        if (password_cursor_timer_) {
            lv_timer_delete(password_cursor_timer_);
            password_cursor_timer_ = nullptr;
        }
        stop_scan();
        stop_connection();
        scan_tasks_.join_all();
        connection_tasks_.join_all();
        if (ComponensObj) {
            lv_anim_del(ComponensObj, nullptr);
            lv_obj_delete(ComponensObj);
            ComponensObj = nullptr;
        }
        title_                 = nullptr;
        empty_                 = nullptr;
        hint_                  = nullptr;
        hidden_panel_          = nullptr;
        hidden_ssid_input_     = nullptr;
        hidden_password_input_ = nullptr;
        hidden_hint_           = nullptr;
        for (auto &row : rows_) row = {};
    }

void LvSettingWifiScanPage3::create_ui(lv_obj_t *parent){
        if (!parent) return;

        ComponensObj = lv_obj_create(parent);
        if (!ComponensObj) return;
        lv_obj_set_size(ComponensObj, metric(LayoutMetric::ScreenW), metric(LayoutMetric::ScreenH));
        lv_obj_set_pos(ComponensObj, 0, 0);
        lv_obj_set_style_bg_color(ComponensObj, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(ComponensObj, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(ComponensObj, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(ComponensObj, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(ComponensObj, 0, LV_PART_MAIN);
        lv_obj_remove_flag(ComponensObj, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(ComponensObj, LV_OBJ_FLAG_SCROLLABLE);
        DComponens::lvgl_bind_event(ComponensObj, LV_EVENT_KEY, nullptr,
                                    std::bind(&LvSettingWifiScanPage3::handle_key_event, this, std::placeholders::_1));

        keyboard_root_ = lv_screen_active();
        if (keyboard_root_ && LV_EVENT_KEYBOARD != 0) {
            keyboard_event_dsc_ = lv_obj_add_event_cb(keyboard_root_, keyboard_event_cb,
                                                      static_cast<lv_event_code_t>(LV_EVENT_KEYBOARD), this);
        }

        title_ = create_label(ComponensObj, "", 8, 2, 0x58A6FF,
                              settings_fonts::cjk_sans(12, LV_FREETYPE_FONT_STYLE_BOLD));
        if (title_) {
            lv_obj_set_width(title_, metric(LayoutMetric::TitleW));
            lv_label_set_long_mode(title_, LV_LABEL_LONG_SCROLL_CIRCULAR);
        }

        create_label(ComponensObj, "SSID", 8, 18, 0x888888, settings_fonts::sans(10));
        create_label(ComponensObj, "Security", 180, 18, 0x888888, settings_fonts::sans(10));
        create_label(ComponensObj, "Signal", 270, 18, 0x888888, settings_fonts::sans(10));

        for (int index = 0; index < metric(LayoutMetric::VisibleRows); ++index) {
            auto &row   = rows_[index];
            const int y = metric(LayoutMetric::RowY) + index * metric(LayoutMetric::RowH);

            row.background = lv_obj_create(ComponensObj);
            if (row.background) {
                lv_obj_set_size(row.background, metric(LayoutMetric::ScreenW) - 8, metric(LayoutMetric::RowH) - 2);
                lv_obj_set_pos(row.background, 4, y);
                lv_obj_set_style_radius(row.background, 2, LV_PART_MAIN);
                lv_obj_set_style_bg_color(row.background, lv_color_hex(0x1F3A5F), LV_PART_MAIN);
                lv_obj_set_style_bg_opa(row.background, LV_OPA_COVER, LV_PART_MAIN);
                lv_obj_set_style_border_width(row.background, 0, LV_PART_MAIN);
                lv_obj_remove_flag(row.background, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_remove_flag(row.background, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_add_flag(row.background, LV_OBJ_FLAG_HIDDEN);
            }

            row.ssid     = create_label(ComponensObj, "", 8, y + 2, 0xCCCCCC, settings_fonts::cjk_sans(12));
            row.security = create_label(ComponensObj, "", 180, y + 2, 0xCCCCCC, settings_fonts::sans(10));
            row.signal   = create_label(ComponensObj, "", 275, y + 2, 0xCCCCCC, settings_fonts::mono(10));
            if (row.ssid) {
                lv_obj_set_width(row.ssid, 165);
                lv_label_set_long_mode(row.ssid, LV_LABEL_LONG_CLIP);
                lv_obj_add_flag(row.ssid, LV_OBJ_FLAG_HIDDEN);
            }
            if (row.security) lv_obj_add_flag(row.security, LV_OBJ_FLAG_HIDDEN);
            if (row.signal) lv_obj_add_flag(row.signal, LV_OBJ_FLAG_HIDDEN);
        }

        empty_ = create_label(ComponensObj, "", 8, 50, 0x666666, settings_fonts::sans(12));
        hint_ = create_label(ComponensObj, "", 8, metric(LayoutMetric::ScreenH) - 14, 0x555555, settings_fonts::sans(10));
        create_password_panel();
        create_hidden_network_panel();
        password_cursor_timer_ = lv_timer_create(password_cursor_timer_cb, 500, this);
        ui_dispatch_timer_     = lv_timer_create(ui_dispatch_timer_cb, 30, this);

        refresh_status();
        if (!wifi_power_enabled_) {
            render();
            show_power_warning();
            return;
        }
        if (hidden_network_) {
            show_hidden_ssid_prompt();
        } else {
            render();
            start_scan();
        }
    }

void LvSettingWifiScanPage3::mark_scan_dispatch_failed(const std::shared_ptr<ScanState> &state) noexcept{
        if (!state) return;
        state->dispatch_failed.store(true, std::memory_order_release);
        state->stop.store(true, std::memory_order_release);
    }

void LvSettingWifiScanPage3::mark_connection_dispatch_failed(const std::shared_ptr<ConnectionState> &state) noexcept{
        if (!state) return;
        state->dispatch_failed.store(true, std::memory_order_release);
        state->stop.store(true, std::memory_order_release);
    }

bool LvSettingWifiScanPage3::enqueue_ui_task(const std::shared_ptr<UiDispatchState> &dispatch, std::function<void()> task) noexcept{
        if (!dispatch || !task) return false;
        try {
            std::lock_guard<std::mutex> lock(dispatch->mutex);
            if (dispatch->stopped) return false;
            dispatch->pending.emplace_back(std::move(task));
            return true;
        } catch (...) {
            return false;
        }
    }

void LvSettingWifiScanPage3::ui_dispatch_timer_cb(lv_timer_t *timer) noexcept{
        try {
            auto *self = timer ? static_cast<LvSettingWifiScanPage3 *>(lv_timer_get_user_data(timer)) : nullptr;
            if (!self || timer != self->ui_dispatch_timer_ || !self->ui_dispatch_) return;

            std::deque<std::function<void()>> pending;
            {
                std::lock_guard<std::mutex> lock(self->ui_dispatch_->mutex);
                if (self->ui_dispatch_->stopped) return;
                pending.swap(self->ui_dispatch_->pending);
            }

            self->handle_dispatch_failures();

            while (!pending.empty()) {
                std::function<void()> task = std::move(pending.front());
                pending.pop_front();
                try {
                    if (task) task();
                } catch (...) {
                }
            }
        } catch (...) {
        }
    }

bool LvSettingWifiScanPage3::enqueue_scan_result(const std::shared_ptr<UiDispatchState> &dispatch,
                                    const std::shared_ptr<ScanResult> &result) noexcept{
        try {
            if (!result || !result->state) return false;
            const auto state = result->state;
            return enqueue_ui_task(dispatch, [state, result] {
                auto lifetime = state->lifetime.lock();
                if (!lifetime || state->stop.load(std::memory_order_acquire)) return;
                LvSettingWifiScanPage3 *self = state->owner;
                if (self) self->process_scan_result(*result);
            });
        } catch (...) {
            return false;
        }
    }

bool LvSettingWifiScanPage3::enqueue_scan_failure(const std::shared_ptr<UiDispatchState> &dispatch,
                                     const std::shared_ptr<ScanState> &state) noexcept{
        try {
            auto result = std::shared_ptr<ScanResult>(new (std::nothrow) ScanResult());
            if (!result) return false;
            result->state = state;
            result->count = CP0_WIFI_ERROR_SERVICE;
            return enqueue_scan_result(dispatch, result);
        } catch (...) {
            return false;
        }
    }

bool LvSettingWifiScanPage3::enqueue_connection_result(const std::shared_ptr<UiDispatchState> &dispatch,
                                          const std::shared_ptr<ConnectionResult> &result) noexcept{
        try {
            if (!result || !result->state) return false;
            const auto state = result->state;
            return enqueue_ui_task(dispatch, [state, result] {
                auto lifetime = state->lifetime.lock();
                if (!lifetime || state->stop.load(std::memory_order_acquire)) return;
                LvSettingWifiScanPage3 *self = state->owner;
                if (self) self->process_connection_result(*result);
            });
        } catch (...) {
            return false;
        }
    }

bool LvSettingWifiScanPage3::enqueue_connection_failure(const std::shared_ptr<UiDispatchState> &dispatch,
                                           const std::shared_ptr<ConnectionState> &state) noexcept{
        try {
            auto result = std::shared_ptr<ConnectionResult>(new (std::nothrow) ConnectionResult());
            if (!result) return false;
            result->state        = state;
            result->result       = CP0_WIFI_ERROR_SERVICE;
            result->status       = {};
            result->status_valid = false;
            return enqueue_connection_result(dispatch, result);
        } catch (...) {
            return false;
        }
    }

lv_obj_t *LvSettingWifiScanPage3::create_label(lv_obj_t *parent, const char *text, int x, int y, uint32_t color,
                                  const lv_font_t *font){
        if (!parent) return nullptr;
        lv_obj_t *label = lv_label_create(parent);
        if (!label) return nullptr;
        lv_label_set_text(label, text ? text : "");
        lv_obj_set_pos(label, x, y);
        lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
        if (font) lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
        return label;
    }

const lv_font_t *LvSettingWifiScanPage3::input_font(uint16_t size){
        return settings_fonts::cjk_sans(size);
    }

const char *LvSettingWifiScanPage3::scan_error_message(int result){
        switch (result) {
            case CP0_WIFI_ERROR_RADIO_OFF:
                return "WiFi is off. Turn on Power to scan";
            case CP0_WIFI_ERROR_TIMEOUT:
                return "WiFi scan timed out. Press R to retry";
            case CP0_WIFI_ERROR_SERVICE:
                return "WiFi service unavailable. Press R to retry";
            default:
                return "WiFi scan failed. Press R to retry";
        }
    }

const char *LvSettingWifiScanPage3::connection_error_message(int result){
        switch (result) {
            case CP0_WIFI_ERROR_RADIO_OFF:
                return "WiFi is off";
            case CP0_WIFI_ERROR_AUTH:
                return "Incorrect password";
            case CP0_WIFI_ERROR_NOT_FOUND:
                return "Network is no longer available";
            case CP0_WIFI_ERROR_IP_CONFIG:
                return "Connected, but IP setup failed";
            case CP0_WIFI_ERROR_TIMEOUT:
                return "Network operation timed out";
            case CP0_WIFI_ERROR_INVALID:
                return "Invalid WiFi credentials";
            default:
                return "WiFi service unavailable";
        }
    }

bool LvSettingWifiScanPage3::is_open_security(const std::string &security){
        return settings_wifi_com::is_open_security(security);
    }

static bool status_matches_network(const WifiStatus &status, const std::string &ssid){
        return status.connected && status.ssid == ssid;
    }

bool LvSettingWifiScanPage3::is_utf8_continuation(unsigned char value){
        return (value & 0xC0u) == 0x80u;
    }

std::size_t LvSettingWifiScanPage3::previous_utf8_start(const std::string &value, std::size_t cursor){
        if (cursor == 0 || cursor > value.size()) return 0;
        std::size_t start = cursor - 1;
        while (start > 0 && is_utf8_continuation(static_cast<unsigned char>(value[start]))) --start;
        return start;
    }

std::size_t LvSettingWifiScanPage3::next_utf8_end(const std::string &value, std::size_t cursor){
        if (cursor >= value.size()) return value.size();
        std::size_t end = cursor + 1;
        while (end < value.size() && is_utf8_continuation(static_cast<unsigned char>(value[end]))) ++end;
        return end;
    }

std::size_t LvSettingWifiScanPage3::utf8_cursor_position(const std::string &value, std::size_t cursor){
        std::size_t position = 0;
        for (std::size_t index = 0; index < cursor && index < value.size(); ++index) {
            if (!is_utf8_continuation(static_cast<unsigned char>(value[index]))) ++position;
        }
        return position;
    }

std::string LvSettingWifiScanPage3::masked_password(const std::string &password, bool visible){
        if (visible) return password;
        std::size_t codepoints = 0;
        for (unsigned char value : password)
            if (!is_utf8_continuation(value)) ++codepoints;
        return std::string(codepoints, '*');
    }

std::size_t LvSettingWifiScanPage3::display_cursor_offset(const std::string &password, std::size_t cursor, bool visible){
        if (visible) return std::min(cursor, password.size());
        std::size_t offset      = 0;
        std::size_t index       = 0;
        const std::size_t limit = std::min(cursor, password.size());
        while (index < limit) {
            index = next_utf8_end(password, index);
            ++offset;
        }
        return offset;
    }

void LvSettingWifiScanPage3::initialize(lv_obj_t *parent){
        create_ui(parent);
    }

void LvSettingWifiScanPage3::create_password_panel(){
        password_panel_ = lv_obj_create(ComponensObj);
        if (!password_panel_) return;
        lv_obj_set_size(password_panel_, metric(LayoutMetric::ScreenW), metric(LayoutMetric::ScreenH));
        lv_obj_set_pos(password_panel_, 0, 0);
        lv_obj_set_style_bg_color(password_panel_, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(password_panel_, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(password_panel_, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(password_panel_, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(password_panel_, 0, LV_PART_MAIN);
        lv_obj_remove_flag(password_panel_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(password_panel_, LV_OBJ_FLAG_SCROLLABLE);

        password_title_      = create_label(password_panel_, "WiFi password", 8, 8, 0x58A6FF,
                                            settings_fonts::sans(13, LV_FREETYPE_FONT_STYLE_BOLD));
        password_network_    = create_label(password_panel_, "", 8, 28, 0xCCCCCC, settings_fonts::cjk_sans(10));
        password_value_      = create_label(password_panel_, "", metric(LayoutMetric::PasswordTextX), 50, 0xFFFFFF, input_font(16));
        password_prefix_     = create_label(password_panel_, "", metric(LayoutMetric::PasswordTextX), 50, 0xFFFFFF, input_font(16));
        password_suffix_     = create_label(password_panel_, "", metric(LayoutMetric::PasswordTextX), 50, 0xFFFFFF, input_font(16));
        password_cursor_bar_ = lv_obj_create(password_panel_);
        if (password_cursor_bar_) {
            lv_obj_set_size(password_cursor_bar_, metric(LayoutMetric::CursorWidth), metric(LayoutMetric::CursorHeight));
            lv_obj_set_style_bg_color(password_cursor_bar_, lv_color_hex(0x58A6FF), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(password_cursor_bar_, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_width(password_cursor_bar_, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(password_cursor_bar_, 0, LV_PART_MAIN);
            lv_obj_clear_flag(password_cursor_bar_, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_flag(password_cursor_bar_, LV_OBJ_FLAG_HIDDEN);
        }
        if (password_prefix_) lv_obj_add_flag(password_prefix_, LV_OBJ_FLAG_HIDDEN);
        if (password_suffix_) lv_obj_add_flag(password_suffix_, LV_OBJ_FLAG_HIDDEN);
        password_status_ = create_label(password_panel_, "", 8, 78, 0xFF4444, settings_fonts::sans(10));
        password_hint_ =
            create_label(password_panel_, "", 8, metric(LayoutMetric::ScreenH) - 14, 0x555555, settings_fonts::sans(10));

        if (password_network_) lv_obj_set_width(password_network_, metric(LayoutMetric::ScreenW) - 16);
        if (password_value_) {
            lv_obj_set_width(password_value_, metric(LayoutMetric::ScreenW) - 16);
            lv_label_set_long_mode(password_value_, LV_LABEL_LONG_CLIP);
        }
        if (password_status_) lv_obj_set_width(password_status_, metric(LayoutMetric::ScreenW) - 16);
        if (password_hint_) lv_obj_set_width(password_hint_, metric(LayoutMetric::ScreenW) - 16);
        lv_obj_add_flag(password_panel_, LV_OBJ_FLAG_HIDDEN);
    }

void LvSettingWifiScanPage3::render_password_editor(){
        if (!password_prefix_ || !password_suffix_) return;
        const std::string display = masked_password(password_, password_visible_);
        const std::size_t split =
            std::min(display_cursor_offset(password_, password_cursor_byte_, password_visible_), display.size());
        const std::string prefix = display.substr(0, split);
        const std::string suffix = display.substr(split);
        const int field_right    = metric(LayoutMetric::ScreenW) - metric(LayoutMetric::PasswordTextRightInset);
        const int max_prefix_width =
            std::max(0, field_right - metric(LayoutMetric::PasswordTextX) - metric(LayoutMetric::CursorGap) -
                                metric(LayoutMetric::CursorWidth) - metric(LayoutMetric::CursorGap));

        lv_obj_set_width(password_prefix_, LV_SIZE_CONTENT);
        lv_label_set_long_mode(password_prefix_, LV_LABEL_LONG_CLIP);
        lv_label_set_text(password_prefix_, prefix.c_str());
        lv_obj_set_pos(password_prefix_, metric(LayoutMetric::PasswordTextX), 50);
        lv_obj_update_layout(password_prefix_);
        const int measured_prefix_width = lv_obj_get_width(password_prefix_);
        const int prefix_width          = std::min(measured_prefix_width, max_prefix_width);
        if (measured_prefix_width > max_prefix_width) {
            lv_obj_set_width(password_prefix_, max_prefix_width);
            lv_label_set_long_mode(password_prefix_, LV_LABEL_LONG_CLIP);
        }

        const int cursor_x = metric(LayoutMetric::PasswordTextX) + prefix_width + metric(LayoutMetric::CursorGap);
        if (password_cursor_bar_) {
            lv_obj_set_pos(password_cursor_bar_, cursor_x, 49);
            if (password_cursor_visible_)
                lv_obj_clear_flag(password_cursor_bar_, LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_add_flag(password_cursor_bar_, LV_OBJ_FLAG_HIDDEN);
        }

        const int suffix_x = cursor_x + metric(LayoutMetric::CursorWidth) + metric(LayoutMetric::CursorGap);
        lv_label_set_text(password_suffix_, suffix.c_str());
        lv_obj_set_pos(password_suffix_, suffix_x, 50);
        lv_obj_set_width(password_suffix_, std::max(1, field_right - suffix_x));
        lv_label_set_long_mode(password_suffix_, LV_LABEL_LONG_CLIP);
    }

void LvSettingWifiScanPage3::password_cursor_timer_cb(lv_timer_t *timer) noexcept{
        try {
            auto *self = timer ? static_cast<LvSettingWifiScanPage3 *>(lv_timer_get_user_data(timer)) : nullptr;
            if (!self || timer != self->password_cursor_timer_ || self->view_ != View::Password) return;
            self->password_cursor_visible_ = !self->password_cursor_visible_;
            self->render_password_panel();
        } catch (...) {
        }
    }

lv_obj_t *LvSettingWifiScanPage3::create_hidden_input(lv_obj_t *parent, int y, uint32_t max_length){
        if (!parent) return nullptr;

        lv_obj_t *input = lv_textarea_create(parent);
        if (!input) return nullptr;
        lv_obj_remove_style_all(input);
        lv_obj_set_pos(input, metric(LayoutMetric::HiddenInputX), y);
        lv_obj_set_size(input, metric(LayoutMetric::HiddenInputW), metric(LayoutMetric::HiddenInputH));
        lv_textarea_set_one_line(input, true);
        lv_textarea_set_max_length(input, max_length);
        lv_textarea_set_text(input, "");
        lv_obj_set_style_text_font(input, input_font(14), LV_PART_MAIN);
        lv_obj_set_style_text_letter_space(input, metric(LayoutMetric::HiddenInputLetterSpace), LV_PART_MAIN);
        lv_obj_set_style_text_color(input, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_bg_color(input, lv_color_hex(0x181818), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(input, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(input, lv_color_hex(0x444444), LV_PART_MAIN);
        lv_obj_set_style_border_width(input, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(input, 3, LV_PART_MAIN);
        lv_obj_set_style_pad_left(input, 6, LV_PART_MAIN);
        lv_obj_set_style_pad_right(input, 6, LV_PART_MAIN);
        lv_obj_set_style_pad_top(input, 3, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(input, LV_OPA_TRANSP, LV_PART_CURSOR);
        lv_obj_set_style_border_color(input, lv_color_hex(0x58A6FF), LV_PART_CURSOR);
        lv_obj_set_style_border_width(input, metric(LayoutMetric::HiddenInputCursorWidth), LV_PART_CURSOR);
        lv_obj_set_style_border_side(input, LV_BORDER_SIDE_LEFT, LV_PART_CURSOR);
        lv_obj_set_style_pad_left(input, -1, LV_PART_CURSOR);
        lv_obj_set_style_anim_duration(input, 400, LV_PART_CURSOR);
        return input;
    }

void LvSettingWifiScanPage3::create_hidden_network_panel(){
        hidden_panel_ = lv_obj_create(ComponensObj);
        if (!hidden_panel_) return;
        lv_obj_set_size(hidden_panel_, metric(LayoutMetric::ScreenW), metric(LayoutMetric::ScreenH));
        lv_obj_set_pos(hidden_panel_, 0, 0);
        lv_obj_set_style_bg_color(hidden_panel_, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(hidden_panel_, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(hidden_panel_, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(hidden_panel_, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(hidden_panel_, 0, LV_PART_MAIN);
        lv_obj_remove_flag(hidden_panel_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(hidden_panel_, LV_OBJ_FLAG_SCROLLABLE);

        create_label(hidden_panel_, "Add Hidden WiFi", 10, 4, 0x58A6FF, settings_fonts::sans(12));
        create_label(hidden_panel_, "SSID", 10, 25, 0xCCCCCC, settings_fonts::sans(10));
        create_label(hidden_panel_, "PASSWORD", 10, 59, 0xCCCCCC, settings_fonts::sans(10));

        hidden_ssid_input_     = create_hidden_input(hidden_panel_, 18, metric(LayoutMetric::MaxSsidBytes));
        hidden_password_input_ = create_hidden_input(hidden_panel_, 52, metric(LayoutMetric::MaxPasswordBytes));
        if (!hidden_ssid_input_ || !hidden_password_input_) return;

        lv_textarea_set_password_bullet(hidden_password_input_, "*");
        lv_textarea_set_password_show_time(hidden_password_input_, 0);
        lv_textarea_set_password_mode(hidden_password_input_, true);

        hidden_hint_ = create_label(hidden_panel_, "TAB:switch  ALT:show  OK:connect", 10,
                                    metric(LayoutMetric::ScreenH) - 14, 0x555555, settings_fonts::sans(10));
        lv_obj_add_flag(hidden_panel_, LV_OBJ_FLAG_HIDDEN);
    }

void LvSettingWifiScanPage3::render_password_panel(){
        if (!password_panel_) return;
        if (hidden_panel_) lv_obj_add_flag(hidden_panel_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(password_panel_, LV_OBJ_FLAG_HIDDEN);

        if (view_ == View::Connecting) {
            if (password_value_) lv_obj_clear_flag(password_value_, LV_OBJ_FLAG_HIDDEN);
            if (password_prefix_) lv_obj_add_flag(password_prefix_, LV_OBJ_FLAG_HIDDEN);
            if (password_suffix_) lv_obj_add_flag(password_suffix_, LV_OBJ_FLAG_HIDDEN);
            if (password_cursor_bar_) lv_obj_add_flag(password_cursor_bar_, LV_OBJ_FLAG_HIDDEN);
            const bool forgetting = connection_state_ && connection_state_->operation == NetworkOperation::Forget;
            if (password_title_)
                lv_label_set_text(password_title_, forgetting ? "Forgetting WiFi..." : "Connecting...");
            if (password_network_) {
                const std::string text = "Network: " + password_ssid_;
                lv_label_set_text(password_network_, text.c_str());
            }
            if (password_value_)
                lv_label_set_text(password_value_, forgetting ? "Removing saved profile..." : "Please wait...");
            if (password_status_) {
                lv_label_set_text(password_status_, "");
                lv_obj_set_style_text_color(password_status_, lv_color_hex(0xCCCCCC), LV_PART_MAIN);
            }
            if (password_hint_) lv_label_set_text(password_hint_, "ESC:back");
            return;
        }

        if (password_value_) lv_obj_add_flag(password_value_, LV_OBJ_FLAG_HIDDEN);
        if (password_prefix_) lv_obj_clear_flag(password_prefix_, LV_OBJ_FLAG_HIDDEN);
        if (password_suffix_) lv_obj_clear_flag(password_suffix_, LV_OBJ_FLAG_HIDDEN);
        if (password_title_) lv_label_set_text(password_title_, "WiFi password");
        if (password_network_) {
            std::string text = password_ssid_;
            if (!password_security_.empty()) {
                text += "  [";
                text += password_security_;
                text += "]";
            }
            lv_label_set_text(password_network_, text.c_str());
        }
        render_password_editor();
        if (password_status_) {
            lv_label_set_text(password_status_, password_error_.c_str());
            lv_obj_set_style_text_color(password_status_, lv_color_hex(password_error_.empty() ? 0x666666 : 0xFF4444),
                                        LV_PART_MAIN);
        }
        if (password_hint_) {
            lv_label_set_text(password_hint_,
                              password_visible_ ? "ALT:hide  OK:connect  ESC:back" : "ALT:show  OK:connect  ESC:back");
        }
    }

void LvSettingWifiScanPage3::update_hidden_input(lv_obj_t *input, const std::string &value, std::size_t cursor_position){
        if (!input) return;
        lv_textarea_set_text(input, value.c_str());
        lv_textarea_set_cursor_pos(input, static_cast<int32_t>(utf8_cursor_position(value, cursor_position)));
    }

void LvSettingWifiScanPage3::set_hidden_focus(int focus){
        hidden_focus_ = focus == 0 ? 0 : 1;
        if (!hidden_ssid_input_ || !hidden_password_input_) return;

        lv_obj_t *focused   = hidden_focus_ == 0 ? hidden_ssid_input_ : hidden_password_input_;
        lv_obj_t *unfocused = hidden_focus_ == 0 ? hidden_password_input_ : hidden_ssid_input_;
        lv_obj_add_state(focused, LV_STATE_FOCUSED);
        lv_obj_remove_state(unfocused, LV_STATE_FOCUSED);
        lv_obj_set_style_border_color(focused, lv_color_hex(0x58A6FF), LV_PART_MAIN);
        lv_obj_set_style_border_color(unfocused, lv_color_hex(0x444444), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(focused, LV_OPA_TRANSP, LV_PART_CURSOR);
        lv_obj_set_style_border_color(focused, lv_color_hex(0x58A6FF), LV_PART_CURSOR);
        lv_obj_set_style_border_width(focused, metric(LayoutMetric::HiddenInputCursorWidth), LV_PART_CURSOR);
        lv_obj_set_style_border_side(focused, LV_BORDER_SIDE_LEFT, LV_PART_CURSOR);
        lv_obj_set_style_bg_opa(unfocused, LV_OPA_TRANSP, LV_PART_CURSOR);
        lv_obj_set_style_border_width(unfocused, 0, LV_PART_CURSOR);
        lv_obj_invalidate(focused);
        lv_obj_invalidate(unfocused);
    }

void LvSettingWifiScanPage3::render_hidden_network_panel(){
        if (!hidden_panel_) return;
        if (password_panel_) lv_obj_add_flag(password_panel_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(hidden_panel_, LV_OBJ_FLAG_HIDDEN);

        update_hidden_input(hidden_ssid_input_, hidden_ssid_, hidden_ssid_cursor_byte_);
        update_hidden_input(hidden_password_input_, password_, password_cursor_byte_);
        lv_textarea_set_password_mode(hidden_password_input_, !password_visible_);
        set_hidden_focus(hidden_focus_);

        if (!hidden_hint_) return;
        const char *hint = connection_pending_       ? "Connecting..."
                           : password_error_.empty() ? (password_visible_ ? "TAB:switch  ALT:hide  OK:connect"
                                                                          : "TAB:switch  ALT:show  OK:connect")
                                                     : password_error_.c_str();
        lv_label_set_text(hidden_hint_, hint);
        lv_obj_set_style_text_color(hidden_hint_, lv_color_hex(password_error_.empty() ? 0x555555 : 0xFF4444),
                                    LV_PART_MAIN);
    }

void LvSettingWifiScanPage3::refresh_status(){
        if (wifi_data_->status.connected && !wifi_data_->status.ssid.empty()) {
            title_text_ = "Connected WiFi: ";
            title_text_ += wifi_data_->status.ssid;
            title_text_ += "  ";
            title_text_ += wifi_data_->status.ip.empty() ? "No IP" : wifi_data_->status.ip;
        } else {
            title_text_ = "WiFi: Not connected";
        }
    }

void LvSettingWifiScanPage3::show_power_warning(){
        if (power_warning_ || !ComponensObj) return;
        power_warning_overlay_ = lv_obj_create(ComponensObj);
        if (!power_warning_overlay_) return;
        lv_obj_set_size(power_warning_overlay_, metric(LayoutMetric::ScreenW), metric(LayoutMetric::ScreenH));
        lv_obj_set_pos(power_warning_overlay_, 0, 0);
        lv_obj_set_style_bg_color(power_warning_overlay_, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(power_warning_overlay_, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(power_warning_overlay_, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(power_warning_overlay_, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(power_warning_overlay_, 0, LV_PART_MAIN);
        lv_obj_clear_flag(power_warning_overlay_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(power_warning_overlay_, LV_OBJ_FLAG_SCROLLABLE);

        power_warning_ = lv_msgbox_create(power_warning_overlay_);
        if (!power_warning_) {
            lv_obj_delete(power_warning_overlay_);
            power_warning_overlay_ = nullptr;
            return;
        }
        lv_obj_set_size(power_warning_, 280, 92);
        lv_obj_center(power_warning_);
        lv_obj_set_style_radius(power_warning_, 4, LV_PART_MAIN);
        lv_obj_set_style_border_width(power_warning_, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(power_warning_, lv_color_hex(0xFFAA00), LV_PART_MAIN);
        lv_obj_set_style_bg_color(power_warning_, lv_color_hex(0x171717), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(power_warning_, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_pad_all(power_warning_, 0, LV_PART_MAIN);
        lv_obj_clear_flag(power_warning_, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *title     = lv_msgbox_add_title(power_warning_, "WiFi power is off");
        lv_obj_t *header    = lv_msgbox_get_header(power_warning_);
        lv_obj_t *content   = lv_msgbox_get_content(power_warning_);
        lv_obj_t *message   = lv_msgbox_add_text(power_warning_, "Turn on Power before continuing.");
        lv_obj_t *ok_button = lv_msgbox_add_footer_button(power_warning_, "OK");
        lv_obj_t *footer    = lv_msgbox_get_footer(power_warning_);
        lv_obj_t *ok_label  = ok_button ? lv_obj_get_child(ok_button, 0) : nullptr;

        if (header) {
            lv_obj_set_height(header, 30);
            lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(header, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_left(header, 12, LV_PART_MAIN);
            lv_obj_set_style_pad_right(header, 12, LV_PART_MAIN);
            lv_obj_set_style_pad_top(header, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_bottom(header, 0, LV_PART_MAIN);
        }
        if (title) {
            lv_obj_set_style_text_color(title, lv_color_hex(0xFFAA00), LV_PART_MAIN);
            lv_obj_set_style_text_font(title, settings_fonts::sans(14, LV_FREETYPE_FONT_STYLE_BOLD), LV_PART_MAIN);
        }
        if (content) {
            lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);
            lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(content, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_left(content, 12, LV_PART_MAIN);
            lv_obj_set_style_pad_right(content, 12, LV_PART_MAIN);
        }
        if (message) {
            lv_obj_set_style_text_color(message, lv_color_hex(0xCCCCCC), LV_PART_MAIN);
            lv_obj_set_style_text_font(message, settings_fonts::sans(12), LV_PART_MAIN);
        }
        if (footer) {
            lv_obj_set_height(footer, 28);
            lv_obj_set_style_bg_opa(footer, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(footer, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_left(footer, 12, LV_PART_MAIN);
            lv_obj_set_style_pad_right(footer, 6, LV_PART_MAIN);
            lv_obj_set_style_pad_top(footer, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_bottom(footer, 0, LV_PART_MAIN);
            lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        }
        if (ok_button) {
            lv_obj_set_width(ok_button, 28);
            lv_obj_set_height(ok_button, 22);
            lv_obj_set_style_bg_opa(ok_button, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(ok_button, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_FOCUSED);
            lv_obj_set_style_bg_opa(ok_button, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_PRESSED);
            lv_obj_set_style_border_width(ok_button, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(ok_button, 0, LV_PART_MAIN);
        }
        if (ok_label) {
            lv_obj_set_style_text_color(ok_label, lv_color_hex(0x58A6FF), LV_PART_MAIN);
            lv_obj_set_style_text_font(ok_label, settings_fonts::sans(12), LV_PART_MAIN);
        }
        DComponens::lvgl_bind_event(power_warning_, LV_EVENT_KEY, nullptr,
                                    std::bind(&LvSettingWifiScanPage3::handle_key_event, this, std::placeholders::_1));
    }

void LvSettingWifiScanPage3::close_power_warning(){
        if (!power_warning_) return;
        lv_msgbox_close(power_warning_);
        power_warning_ = nullptr;
        if (power_warning_overlay_) {
            lv_obj_delete(power_warning_overlay_);
            power_warning_overlay_ = nullptr;
        }
        if (LeaveSelfPage) LeaveSelfPage();
    }

void LvSettingWifiScanPage3::render(){
        if (!ComponensObj) return;

        if (view_ == View::HiddenSsid) {
            render_hidden_network_panel();
            return;
        }
        if (view_ != View::List) {
            render_password_panel();
            return;
        }
        if (hidden_panel_) lv_obj_add_flag(hidden_panel_, LV_OBJ_FLAG_HIDDEN);
        if (password_panel_) lv_obj_add_flag(password_panel_, LV_OBJ_FLAG_HIDDEN);

        if (title_) lv_label_set_text(title_, title_text_.c_str());
        if (empty_) {
            const std::string message = !scan_error_.empty() ? scan_error_
                                        : scanning_          ? "Scanning for WiFi networks..."
                                                             : "No networks found. Press R to rescan.";
            lv_label_set_text(empty_, message.c_str());
            if (wifi_data_->access_points.empty())
                lv_obj_remove_flag(empty_, LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_add_flag(empty_, LV_OBJ_FLAG_HIDDEN);
        }
        if (hint_) {
            lv_label_set_text(hint_, scanning_ ? "Scanning...  OK:connect  R:rescan  D:forget  ESC:back"
                                               : "OK:connect  R:rescan  D:forget  ESC:back");
        }

        const int count = static_cast<int>(wifi_data_->access_points.size());
        int offset      = selected_index_ - metric(LayoutMetric::VisibleRows) / 2;
        offset          = std::max(0, std::min(offset, std::max(0, count - metric(LayoutMetric::VisibleRows))));

        for (int visible = 0; visible < metric(LayoutMetric::VisibleRows); ++visible) {
            auto &row       = rows_[visible];
            const int index = offset + visible;
            if (index < 0 || index >= count) {
                hide_row(row);
                continue;
            }

            const auto &access_point = wifi_data_->access_points[static_cast<std::size_t>(index)];
            const bool selected      = index == selected_index_;
            const uint32_t color     = access_point.in_use ? 0x58A6FF : selected ? 0xFFFFFF : 0xCCCCCC;
            std::string ssid         = access_point.ssid;
            if (access_point.saved) ssid += " *";
            set_row_text(row.ssid, ssid);
            set_row_text(row.security, access_point.security.empty() ? "Open" : access_point.security);
            char signal[16];
            std::snprintf(signal, sizeof(signal), "%d%%", std::clamp(access_point.signal, 0, 100));
            set_row_text(row.signal, signal);

            if (row.ssid) lv_obj_set_style_text_color(row.ssid, lv_color_hex(color), LV_PART_MAIN);
            if (row.security) lv_obj_set_style_text_color(row.security, lv_color_hex(color), LV_PART_MAIN);
            if (row.signal) lv_obj_set_style_text_color(row.signal, lv_color_hex(color), LV_PART_MAIN);
            if (row.background) {
                if (selected)
                    lv_obj_remove_flag(row.background, LV_OBJ_FLAG_HIDDEN);
                else
                    lv_obj_add_flag(row.background, LV_OBJ_FLAG_HIDDEN);
            }
            show_row(row);
            row.selected = selected;
            row.color    = color;
        }
    }

void LvSettingWifiScanPage3::set_row_text(lv_obj_t *label, const std::string &text){
        if (label) lv_label_set_text(label, text.c_str());
    }

void LvSettingWifiScanPage3::set_row_text(lv_obj_t *label, const char *text){
        if (label) lv_label_set_text(label, text ? text : "");
    }

void LvSettingWifiScanPage3::hide_row(RowObjects &row){
        if (row.background) lv_obj_add_flag(row.background, LV_OBJ_FLAG_HIDDEN);
        if (row.ssid) lv_obj_add_flag(row.ssid, LV_OBJ_FLAG_HIDDEN);
        if (row.security) lv_obj_add_flag(row.security, LV_OBJ_FLAG_HIDDEN);
        if (row.signal) lv_obj_add_flag(row.signal, LV_OBJ_FLAG_HIDDEN);
        row.visible  = false;
        row.selected = false;
    }

void LvSettingWifiScanPage3::show_row(RowObjects &row){
        if (row.ssid) lv_obj_remove_flag(row.ssid, LV_OBJ_FLAG_HIDDEN);
        if (row.security) lv_obj_remove_flag(row.security, LV_OBJ_FLAG_HIDDEN);
        if (row.signal) lv_obj_remove_flag(row.signal, LV_OBJ_FLAG_HIDDEN);
        row.visible = true;
    }

void LvSettingWifiScanPage3::enter_text_input_mode(){
        if (!keyboard_mode_saved_) {
            previous_input_context_    = cp0_keyboard_get_input_context();
            previous_keypad_intercept_ = cp0_keyboard_get_lvgl_keypad_intercept();
            keyboard_mode_saved_       = true;
        }
        cp0_keyboard_set_input_context(KBD_INPUT_CONTEXT_TEXT);
        cp0_keyboard_set_lvgl_keypad_intercept(1);
    }

void LvSettingWifiScanPage3::restore_text_input_mode(){
        if (!keyboard_mode_saved_) return;
        cp0_keyboard_set_input_context(previous_input_context_);
        cp0_keyboard_set_lvgl_keypad_intercept(previous_keypad_intercept_);
        keyboard_mode_saved_ = false;
    }

void LvSettingWifiScanPage3::clear_password(){
        volatile char *bytes = password_.empty() ? nullptr : password_.data();
        for (std::size_t index = 0; bytes && index < password_.size(); ++index) bytes[index] = '\0';
        password_.clear();
        password_cursor_byte_ = 0;
    }

void LvSettingWifiScanPage3::clear_hidden_ssid(){
        volatile char *bytes = hidden_ssid_.empty() ? nullptr : hidden_ssid_.data();
        for (std::size_t index = 0; bytes && index < hidden_ssid_.size(); ++index) bytes[index] = '\0';
        hidden_ssid_.clear();
        hidden_ssid_cursor_byte_ = 0;
    }

bool LvSettingWifiScanPage3::append_text(std::string &value, std::size_t &cursor, const char *text, std::size_t max_bytes){
        if (!text || !text[0]) return false;
        const std::string input(text);
        if (input.empty() || value.size() + input.size() > max_bytes) return false;
        for (unsigned char byte : input) {
            if (byte < 0x20u || byte == 0x7Fu) return false;
        }
        value.insert(cursor, input);
        cursor += input.size();
        return true;
    }

bool LvSettingWifiScanPage3::append_password_text(const char *text){
        const bool changed = append_text(password_, password_cursor_byte_, text,
                                         static_cast<std::size_t>(metric(LayoutMetric::MaxPasswordBytes)));
        if (changed) password_cursor_visible_ = true;
        return changed;
    }

bool LvSettingWifiScanPage3::append_hidden_ssid_text(const char *text){
        return append_text(hidden_ssid_, hidden_ssid_cursor_byte_, text,
                           static_cast<std::size_t>(metric(LayoutMetric::MaxSsidBytes)));
    }

bool LvSettingWifiScanPage3::erase_password_last(){
        if (password_cursor_byte_ == 0) return false;
        const std::size_t start = previous_utf8_start(password_, password_cursor_byte_);
        volatile char *bytes    = password_.data();
        for (std::size_t index = start; index < password_cursor_byte_; ++index) bytes[index] = '\0';
        password_.erase(start, password_cursor_byte_ - start);
        password_cursor_byte_    = start;
        password_cursor_visible_ = true;
        return true;
    }

bool LvSettingWifiScanPage3::move_password_cursor_left(){
        const std::size_t next = previous_utf8_start(password_, password_cursor_byte_);
        if (next == password_cursor_byte_) return false;
        password_cursor_byte_    = next;
        password_cursor_visible_ = true;
        return true;
    }

bool LvSettingWifiScanPage3::move_password_cursor_right(){
        const std::size_t next = next_utf8_end(password_, password_cursor_byte_);
        if (next == password_cursor_byte_) return false;
        password_cursor_byte_    = next;
        password_cursor_visible_ = true;
        return true;
    }

bool LvSettingWifiScanPage3::erase_hidden_ssid_last(){
        if (hidden_ssid_cursor_byte_ == 0) return false;
        const std::size_t start = previous_utf8_start(hidden_ssid_, hidden_ssid_cursor_byte_);
        volatile char *bytes    = hidden_ssid_.data();
        for (std::size_t index = start; index < hidden_ssid_cursor_byte_; ++index) bytes[index] = '\0';
        hidden_ssid_.erase(start, hidden_ssid_cursor_byte_ - start);
        hidden_ssid_cursor_byte_ = start;
        return true;
    }

bool LvSettingWifiScanPage3::move_hidden_ssid_cursor_left(){
        const std::size_t next = previous_utf8_start(hidden_ssid_, hidden_ssid_cursor_byte_);
        if (next == hidden_ssid_cursor_byte_) return false;
        hidden_ssid_cursor_byte_ = next;
        return true;
    }

bool LvSettingWifiScanPage3::move_hidden_ssid_cursor_right(){
        const std::size_t next = next_utf8_end(hidden_ssid_, hidden_ssid_cursor_byte_);
        if (next == hidden_ssid_cursor_byte_) return false;
        hidden_ssid_cursor_byte_ = next;
        return true;
    }

bool LvSettingWifiScanPage3::append_hidden_text(const char *text){
        return hidden_focus_ == 0 ? append_hidden_ssid_text(text) : append_password_text(text);
    }

bool LvSettingWifiScanPage3::erase_hidden_text(){
        return hidden_focus_ == 0 ? erase_hidden_ssid_last() : erase_password_last();
    }

bool LvSettingWifiScanPage3::move_hidden_cursor_left(){
        return hidden_focus_ == 0 ? move_hidden_ssid_cursor_left() : move_password_cursor_left();
    }

bool LvSettingWifiScanPage3::move_hidden_cursor_right(){
        return hidden_focus_ == 0 ? move_hidden_ssid_cursor_right() : move_password_cursor_right();
    }

std::string LvSettingWifiScanPage3::password_validation_error() const{
        if (password_.empty()) return "Password required";

        std::string security = password_security_;
        std::transform(security.begin(), security.end(), security.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
        if (security.find("802.1X") != std::string::npos || security.find("EAP") != std::string::npos ||
            security.find("ENTERPRISE") != std::string::npos)
            return "Enterprise WiFi is not supported";
        if (security.find("WPA") == std::string::npos) return {};
        if (password_.size() >= 8 && password_.size() <= 63) return {};
        if (password_.size() == 64 &&
            std::all_of(password_.begin(), password_.end(), [](unsigned char ch) { return std::isxdigit(ch) != 0; }))
            return {};
        return "WPA password: 8-63 chars or 64 hex";
    }

void LvSettingWifiScanPage3::show_password_prompt(const std::string &ssid, const std::string &security, const std::string &error){
        stop_scan();
        stop_connection();
        view_                    = View::Password;
        password_ssid_           = ssid;
        password_security_       = security;
        password_error_          = error;
        password_visible_        = false;
        password_cursor_visible_ = true;
        clear_password();
        enter_text_input_mode();
        render();
    }

void LvSettingWifiScanPage3::show_hidden_ssid_prompt(const std::string &initial){
        stop_scan();
        stop_connection();
        hidden_network_ = true;
        view_           = View::HiddenSsid;
        password_ssid_.clear();
        password_security_ = "WPA2";
        password_error_.clear();
        password_visible_        = false;
        password_cursor_visible_ = true;
        hidden_focus_            = 0;
        clear_hidden_ssid();
        clear_password();
        if (!initial.empty()) append_hidden_ssid_text(initial.c_str());
        enter_text_input_mode();
        render();
    }

void LvSettingWifiScanPage3::leave_hidden_ssid_prompt(){
        restore_text_input_mode();
        clear_hidden_ssid();
        clear_password();
        password_ssid_.clear();
        password_security_.clear();
        password_error_.clear();
        password_visible_ = false;
        hidden_network_   = false;
        if (LeaveSelfPage) {
            LeaveSelfPage();
            return;
        }
        view_ = View::List;
        render();
        start_scan();
    }

void LvSettingWifiScanPage3::submit_hidden_ssid(){
        if (view_ != View::HiddenSsid || connection_pending_) return;
        if (hidden_ssid_.empty()) {
            password_error_ = "SSID required";
            render();
            return;
        }

        password_error_.clear();
        if (!start_connection(hidden_ssid_, password_, ConnectionOrigin::HiddenPasswordEntry)) {
            password_error_ = "Unable to start connection";
            render();
        }
    }

void LvSettingWifiScanPage3::leave_password_prompt(){
        if (hidden_network_ && view_ == View::Password) {
            show_hidden_ssid_prompt(hidden_ssid_);
            return;
        }
        restore_text_input_mode();
        clear_password();
        password_ssid_.clear();
        password_security_.clear();
        password_error_.clear();
        password_visible_ = false;
        hidden_ssid_.clear();
        hidden_network_ = false;
        view_           = View::List;
    }

void LvSettingWifiScanPage3::cancel_password_prompt(){
        const bool was_hidden = hidden_network_;
        leave_password_prompt();
        if (!was_hidden && view_ == View::List) {
            render();
            start_scan();
        }
    }

void LvSettingWifiScanPage3::submit_password(){
        if (view_ != View::Password || connection_pending_) return;
        const std::string error = password_validation_error();
        if (!error.empty()) {
            password_error_ = error;
            render();
            return;
        }
        start_connection(password_ssid_, password_, ConnectionOrigin::PasswordEntry, password_security_);
    }

bool LvSettingWifiScanPage3::start_network_operation(NetworkOperation operation, const std::string &ssid, const std::string &password,
                                 const std::string &security, ConnectionOrigin origin, bool disconnect_active){
        if (ssid.empty() || connection_pending_ || !ui_dispatch_timer_) return false;

        auto state               = std::make_shared<ConnectionState>();
        state->lifetime          = lifetime_token_;
        state->owner             = this;
        state->operation         = operation;
        state->origin            = origin;
        state->disconnect_active = disconnect_active;
        state->ssid              = ssid;
        state->password          = password;
        state->security          = security;
        state->generation        = ++generation_;
        connection_state_        = state;
        connection_pending_      = true;
        password_ssid_           = ssid;
        password_security_       = security;
        if (origin != ConnectionOrigin::HiddenPasswordEntry) {
            restore_text_input_mode();
            clear_password();
        }
        view_ = origin == ConnectionOrigin::HiddenPasswordEntry ? View::HiddenSsid : View::Connecting;
        render();

        connection_tasks_.reap_finished();
        const auto dispatch = ui_dispatch_;
        if (!connection_tasks_.start([state, dispatch] {
                try {
                    if (state->stop.load(std::memory_order_acquire)) return;

                    int result = 0;
                    WifiStatus status;
                    bool status_valid = false;
                    if (state->operation == NetworkOperation::Forget) {
                        result = settings_wifi_com::profile_forget(state->ssid);
                        if (result == 0) {
                            WifiStatus before_disconnect;
                            const bool read_ok           = settings_wifi_com::read_status(before_disconnect) == 0;
                            const bool should_disconnect = read_ok
                                                               ? status_matches_network(before_disconnect, state->ssid)
                                                               : state->disconnect_active;
                            if (should_disconnect) {
                                int disconnect_result = settings_wifi_com::profile_disconnect_active();
                                if (disconnect_result != 0) {
                                    WifiStatus after_disconnect;
                                    if (settings_wifi_com::read_status(after_disconnect) == 0 &&
                                        !after_disconnect.connected)
                                        disconnect_result = 0;
                                }
                                if (disconnect_result != 0) result = disconnect_result;
                            }
                        }
                    } else if (state->origin == ConnectionOrigin::HiddenPasswordEntry) {
                        result = settings_wifi_com::connect_hidden(state->ssid, state->password);
                    } else {
                        result = settings_wifi_com::connect(state->ssid, state->password);
                    }

                    if (state->stop.load(std::memory_order_acquire)) return;
                    status_valid = settings_wifi_com::read_status(status) == 0;

                    auto queued = std::shared_ptr<ConnectionResult>(
                        new (std::nothrow) ConnectionResult{state, result, std::move(status), status_valid});
                    if (!queued || !enqueue_connection_result(dispatch, queued)) {
                        mark_connection_dispatch_failed(state);
                        return;
                    }
                } catch (...) {
                    if (!state->stop.load(std::memory_order_acquire) && !enqueue_connection_failure(dispatch, state))
                        mark_connection_dispatch_failed(state);
                }
            })) {
            connection_state_.reset();
            connection_pending_ = false;
            ++generation_;
            const std::string error = "Unable to start WiFi operation";
            if (origin == ConnectionOrigin::HiddenPasswordEntry) {
                clear_password();
                password_error_ = error;
                view_           = View::HiddenSsid;
                render();
            } else if (origin == ConnectionOrigin::SavedProfile || origin == ConnectionOrigin::PasswordEntry) {
                show_password_prompt(ssid, security.empty() ? "WPA" : security, error);
            } else {
                view_       = View::List;
                scan_error_ = error;
                render();
            }
            return false;
        }
        return true;
    }

bool LvSettingWifiScanPage3::start_connection(const std::string &ssid, const std::string &password, ConnectionOrigin origin,
                          const std::string &security){
        stop_scan();
        return start_network_operation(NetworkOperation::Connect, ssid, password, security, origin);
    }

void LvSettingWifiScanPage3::stop_connection(){
        if (connection_state_) connection_state_->stop.store(true, std::memory_order_release);
        connection_state_.reset();
        connection_pending_ = false;
        ++generation_;
    }

void LvSettingWifiScanPage3::cancel_connection(){
        if (!connection_pending_) return;
        const bool hidden = connection_state_ && connection_state_->origin == ConnectionOrigin::HiddenPasswordEntry;
        stop_connection();
        if (hidden) {
            clear_password();
            password_error_ = "Connection cancelled";
            view_           = View::HiddenSsid;
            render();
            return;
        }
        password_ssid_.clear();
        password_security_.clear();
        password_error_.clear();
        view_ = View::List;
        scan_error_.clear();
        render();
        start_scan();
    }

void LvSettingWifiScanPage3::process_connection_result(const ConnectionResult &result){
        if (!result.state) return;
        auto lifetime = result.state->lifetime.lock();
        if (!lifetime || result.state->stop.load(std::memory_order_acquire)) return;

        LvSettingWifiScanPage3 *self = result.state->owner;
        if (self != this || connection_state_.get() != result.state.get() || result.state->generation != generation_)
            return;
        connection_tasks_.reap_finished();
        const auto state = result.state;
        connection_state_.reset();
        connection_pending_ = false;

        int operation_result = result.result;
        if (state->operation == NetworkOperation::Connect && operation_result == 0 &&
            (!result.status_valid || !status_matches_network(result.status, state->ssid))) {
            operation_result = result.status_valid ? CP0_WIFI_ERROR_IP_CONFIG : CP0_WIFI_ERROR_SERVICE;
        }

        if (state->origin == ConnectionOrigin::HiddenPasswordEntry) {
            if (operation_result == 0) {
                wifi_data_->status = result.status;
                restore_text_input_mode();
                clear_hidden_ssid();
                clear_password();
                password_ssid_.clear();
                password_security_.clear();
                password_error_.clear();
                password_visible_ = false;
                hidden_network_   = false;
                view_             = View::List;
                refresh_status();
                render();
                start_scan();
                return;
            }

            password_error_ = connection_error_message(operation_result);
            clear_password();
            view_ = View::HiddenSsid;
            render();
            return;
        }

        if (state->operation == NetworkOperation::Forget) {
            view_ = View::List;
            password_ssid_.clear();
            password_security_.clear();
            scan_error_ = operation_result == 0 ? std::string() : connection_error_message(operation_result);
            if (result.status_valid) wifi_data_->status = result.status;
            refresh_status();
            render();
            if (operation_result == 0) start_scan();
            return;
        }

        if (operation_result == 0) {
            wifi_data_->status = result.status;
            leave_password_prompt();
            refresh_status();
            render();
            start_scan();
            return;
        }

        const std::string error = connection_error_message(operation_result);
        if (state->origin == ConnectionOrigin::SavedProfile || state->origin == ConnectionOrigin::PasswordEntry) {
            show_password_prompt(state->ssid, state->security.empty() ? "WPA" : state->security, error);
            return;
        }

        view_       = View::List;
        scan_error_ = error;
        render();
    }

void LvSettingWifiScanPage3::handle_dispatch_failures(){
        if (scan_state_ && scan_state_->dispatch_failed.exchange(false, std::memory_order_acq_rel)) {
            scan_state_->stop.store(true, std::memory_order_release);
            scan_state_.reset();
            scanning_             = false;
            scan_restart_pending_ = false;
            ++generation_;
            scan_error_ = "Unable to deliver WiFi scan result";
            render();
        }

        if (!connection_state_ || !connection_state_->dispatch_failed.exchange(false, std::memory_order_acq_rel))
            return;

        const auto state = connection_state_;
        state->stop.store(true, std::memory_order_release);
        connection_state_.reset();
        connection_pending_ = false;
        ++generation_;
        const std::string error = "Unable to deliver WiFi operation result";
        if (state->origin == ConnectionOrigin::HiddenPasswordEntry) {
            password_error_ = error;
            clear_password();
            view_ = View::HiddenSsid;
            render();
        } else if (state->origin == ConnectionOrigin::SavedProfile ||
                   state->origin == ConnectionOrigin::PasswordEntry) {
            show_password_prompt(state->ssid, state->security.empty() ? "WPA" : state->security, error);
        } else {
            view_       = View::List;
            scan_error_ = error;
            render();
        }
    }

void LvSettingWifiScanPage3::activate_selected(){
        if (view_ != View::List || connection_pending_ || wifi_data_->access_points.empty()) return;
        if (selected_index_ < 0 || selected_index_ >= static_cast<int>(wifi_data_->access_points.size())) return;

        const WifiAccessPoint access_point = wifi_data_->access_points[static_cast<std::size_t>(selected_index_)];
        if (access_point.in_use || access_point.ssid.empty()) return;
        if (is_open_security(access_point.security)) {
            start_connection(access_point.ssid, {}, ConnectionOrigin::OpenNetwork, access_point.security);
        } else if (access_point.saved) {
            start_connection(access_point.ssid, {}, ConnectionOrigin::SavedProfile, access_point.security);
        } else {
            show_password_prompt(access_point.ssid, access_point.security);
        }
    }

void LvSettingWifiScanPage3::forget_selected(){
        if (view_ != View::List || connection_pending_ || scanning_ || wifi_data_->access_points.empty()) return;
        if (selected_index_ < 0 || selected_index_ >= static_cast<int>(wifi_data_->access_points.size())) return;
        const WifiAccessPoint &access_point = wifi_data_->access_points[static_cast<std::size_t>(selected_index_)];
        if (!access_point.saved || access_point.ssid.empty()) return;
        stop_scan();
        start_network_operation(NetworkOperation::Forget, access_point.ssid, {}, access_point.security,
                                ConnectionOrigin::OpenNetwork, access_point.in_use);
    }

void LvSettingWifiScanPage3::keyboard_event_cb(lv_event_t *event){
        if (!event) return;
        auto *self = static_cast<LvSettingWifiScanPage3 *>(lv_event_get_user_data(event));
        auto *item = static_cast<const key_item *>(lv_event_get_param(event));
        if (!self || !item) return;

        const bool pressed = item->key_state == KBD_KEY_PRESSED || item->key_state == KBD_KEY_REPEATED;
        if (!pressed) return;

        if (self->view_ == View::HiddenSsid) {
            if (self->connection_pending_) {
                if (item->key_code == KEY_ESC) self->cancel_connection();
                lv_event_stop_processing(event);
                return;
            }
            if (item->key_code == KEY_ESC) {
                self->leave_hidden_ssid_prompt();
            } else if (item->key_code == KEY_TAB) {
                self->set_hidden_focus(self->hidden_focus_ == 0 ? 1 : 0);
                self->render();
            } else if (item->key_code == KEY_UP || item->key_code == KEY_DOWN) {
                self->set_hidden_focus(item->key_code == KEY_UP ? 0 : 1);
                self->render();
            } else if (item->key_code == KEY_ENTER || item->key_code == KEY_KPENTER) {
                self->submit_hidden_ssid();
            } else if (item->key_code == KEY_LEFTALT) {
                self->password_visible_ = !self->password_visible_;
                self->render();
            } else if (item->key_code == KEY_BACKSPACE || item->key_code == KEY_DELETE) {
                self->erase_hidden_text();
                self->render();
            } else if (item->key_code == KEY_LEFT) {
                self->move_hidden_cursor_left();
                self->render();
            } else if (item->key_code == KEY_RIGHT) {
                self->move_hidden_cursor_right();
                self->render();
            } else if (item->utf8[0]) {
                self->append_hidden_text(item->utf8);
                self->render();
            }
            lv_event_stop_processing(event);
            return;
        }

        if (self->view_ == View::Password) {
            if (item->key_code == KEY_ESC) {
                self->cancel_password_prompt();
            } else if (item->key_code == KEY_ENTER || item->key_code == KEY_KPENTER) {
                self->submit_password();
            } else if (item->key_code == KEY_BACKSPACE || item->key_code == KEY_DELETE) {
                self->erase_password_last();
                self->render();
            } else if (item->key_code == KEY_LEFT) {
                self->move_password_cursor_left();
                self->render();
            } else if (item->key_code == KEY_RIGHT) {
                self->move_password_cursor_right();
                self->render();
            } else if (item->key_code == KEY_LEFTALT) {
                self->password_visible_ = !self->password_visible_;
                self->render();
            } else if (item->utf8[0]) {
                self->append_password_text(item->utf8);
                self->render();
            }
            lv_event_stop_processing(event);
            return;
        }

        if (item->key_code == KEY_R || item->semantic_key == KEY_R) {
            self->start_scan();
        } else if (item->key_code == KEY_D || item->semantic_key == KEY_D) {
            self->forget_selected();
        }
        lv_event_stop_processing(event);
    }

void LvSettingWifiScanPage3::start_scan(){
        if (scanning_) {
            scan_restart_pending_ = true;
            return;
        }
        if (!ui_dispatch_timer_) {
            scanning_   = false;
            scan_error_ = "Unable to start WiFi scan";
            render();
            return;
        }

        selected_ssid_before_scan_.clear();
        if (selected_index_ >= 0 && selected_index_ < static_cast<int>(wifi_data_->access_points.size())) {
            selected_ssid_before_scan_ = wifi_data_->access_points[static_cast<std::size_t>(selected_index_)].ssid;
        }

        stop_scan();
        scan_error_.clear();
        scanning_ = true;
        render();

        auto state          = std::make_shared<ScanState>();
        state->lifetime     = lifetime_token_;
        state->owner        = this;
        state->generation   = ++generation_;
        scan_state_         = state;
        const auto dispatch = ui_dispatch_;

        if (!scan_tasks_.start([state, dispatch] {
                try {
                    auto result = std::shared_ptr<ScanResult>(new (std::nothrow) ScanResult());
                    if (!result) {
                        mark_scan_dispatch_failed(state);
                        return;
                    }
                    result->state = state;
                    std::vector<WifiAccessPoint> access_points;
                    result->count = settings_wifi_com::scan(access_points, metric(LayoutMetric::ApMax));
                    if (result->count >= 0) {
                        result->count = std::min(result->count, metric(LayoutMetric::ApMax));
                        for (int index = 0; index < result->count; ++index)
                            result->access_points[static_cast<std::size_t>(index)] =
                                access_points[static_cast<std::size_t>(index)];
                    }
                    result->status_valid = settings_wifi_com::read_status(result->status) == 0;
                    if (state->stop.load(std::memory_order_acquire)) return;

                    if (!enqueue_scan_result(dispatch, result)) {
                        mark_scan_dispatch_failed(state);
                        return;
                    }
                } catch (...) {
                    if (!state->stop.load(std::memory_order_acquire) && !enqueue_scan_failure(dispatch, state))
                        mark_scan_dispatch_failed(state);
                }
            })) {
            scanning_   = false;
            scan_error_ = "Unable to start WiFi scan";
            render();
        }
    }

void LvSettingWifiScanPage3::stop_scan(){
        if (scan_state_) scan_state_->stop.store(true, std::memory_order_release);
        scan_state_.reset();
        scanning_             = false;
        scan_restart_pending_ = false;
        ++generation_;
    }

void LvSettingWifiScanPage3::process_scan_result(const ScanResult &result){
        if (!result.state) return;
        auto lifetime = result.state->lifetime.lock();
        if (!lifetime || result.state->stop.load(std::memory_order_acquire)) return;

        LvSettingWifiScanPage3 *self = result.state->owner;
        if (self != this || scan_state_.get() != result.state.get() || result.state->generation != generation_) return;
        scan_tasks_.reap_finished();
        scanning_ = false;
        apply_scan_result(result);
        if (scan_restart_pending_) {
            scan_restart_pending_ = false;
            start_scan();
        }
    }

void LvSettingWifiScanPage3::apply_scan_result(const ScanResult &result){
        const int count = std::clamp(result.count, 0, metric(LayoutMetric::ApMax));
        if (result.status_valid) wifi_data_->status = result.status;
        if (result.count < 0) {
            wifi_data_->access_points.clear();
            selected_index_ = 0;
            selected_ssid_before_scan_.clear();
            scan_error_ = scan_error_message(result.count);
            if (result.count == CP0_WIFI_ERROR_RADIO_OFF) {
                wifi_power_enabled_ = false;
            }
            refresh_status();
            render();
            if (result.count == CP0_WIFI_ERROR_RADIO_OFF) {
                show_power_warning();
            }
            return;
        }

        wifi_data_->access_points.assign(result.access_points.begin(), result.access_points.begin() + count);
        if (wifi_data_->access_points.empty()) {
            selected_index_ = 0;
        } else {
            selected_index_ = std::clamp(selected_index_, 0, static_cast<int>(wifi_data_->access_points.size()) - 1);
            if (!selected_ssid_before_scan_.empty()) {
                const auto selected = std::find_if(wifi_data_->access_points.begin(), wifi_data_->access_points.end(),
                                                   [this](const WifiAccessPoint &access_point) {
                                                       return selected_ssid_before_scan_ == access_point.ssid;
                                                   });
                if (selected != wifi_data_->access_points.end()) {
                    selected_index_ = static_cast<int>(std::distance(wifi_data_->access_points.begin(), selected));
                }
            }
        }
        selected_ssid_before_scan_.clear();
        scan_error_.clear();
        refresh_status();
        render();
    }

bool LvSettingWifiScanPage3::move_selection(int delta){
        if (view_ != View::List || wifi_data_->access_points.empty() || delta == 0) return false;
        const int next = std::clamp(selected_index_ + delta, 0, static_cast<int>(wifi_data_->access_points.size()) - 1);
        if (next == selected_index_) return false;
        selected_index_ = next;
        return true;
    }

void LvSettingWifiScanPage3::handle_key_event(lv_event_t *event){
        if (!event || lv_event_get_code(event) != LV_EVENT_KEY) return;
        const uint32_t key = lv_event_get_key(event);

        if (power_warning_) {
            if (key == LV_KEY_ESC || key == LV_KEY_LEFT || key == LV_KEY_ENTER || key == LV_KEY_RIGHT)
                close_power_warning();
            lv_event_stop_processing(event);
            return;
        }

        if (view_ == View::HiddenSsid) {
            if (connection_pending_) {
                if (key == LV_KEY_ESC) cancel_connection();
            } else {
                if (key == LV_KEY_ESC) {
                    leave_hidden_ssid_prompt();
                } else if (key == LV_KEY_UP || key == LV_KEY_DOWN) {
                    set_hidden_focus(key == LV_KEY_UP ? 0 : 1);
                    render();
                } else if (key == LV_KEY_ENTER) {
                    submit_hidden_ssid();
                } else if (key == LV_KEY_BACKSPACE || key == LV_KEY_DEL) {
                    erase_hidden_text();
                    render();
                } else if (key == LV_KEY_LEFT) {
                    move_hidden_cursor_left();
                    render();
                } else if (key == LV_KEY_RIGHT) {
                    move_hidden_cursor_right();
                    render();
                }
            }
            lv_event_stop_processing(event);
            return;
        }

        if (view_ == View::Password) {
            if (key == LV_KEY_ESC) {
                cancel_password_prompt();
            } else if (key == LV_KEY_ENTER) {
                submit_password();
            } else if (key == LV_KEY_BACKSPACE || key == LV_KEY_DEL) {
                erase_password_last();
                render();
            } else if (key == LV_KEY_LEFT) {
                move_password_cursor_left();
                render();
            } else if (key == LV_KEY_RIGHT) {
                move_password_cursor_right();
                render();
            }
            lv_event_stop_processing(event);
            return;
        }

        if (view_ == View::Connecting) {
            if (key == LV_KEY_ESC) {
                stop_connection();
                view_ = View::List;
                scan_error_.clear();
                render();
                start_scan();
            } else if (key == LV_KEY_LEFT && LeaveSelfPage) {
                stop_connection();
                LeaveSelfPage();
            }
            lv_event_stop_processing(event);
            return;
        }

        if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
            stop_scan();
            if (LeaveSelfPage) LeaveSelfPage();
        } else if (key == LV_KEY_UP) {
            if (move_selection(-1)) render();
        } else if (key == LV_KEY_DOWN) {
            if (move_selection(1)) render();
        } else if (key == LV_KEY_ENTER || key == LV_KEY_RIGHT) {
            activate_selected();
        } else if (key == LV_KEY_DEL) {
            forget_selected();
        } else if (key == LV_KEY_NEXT) {
            start_scan();
        }
        lv_event_stop_processing(event);
    }
