#include "settings_system_page.hpp"

#include "cp0_font_service.hpp"
#include "settings_about_info_model.hpp"
#include "settings_system_api.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cerrno>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <utility>

namespace {

constexpr int kScreenWidth = 320;
constexpr int kScreenHeight = 150;
constexpr int kTextWidth = 304;
constexpr int kUpdatePollMs = 500;
constexpr std::chrono::seconds kUpdateTimeout{20 * 60};

#define SETTINGS_SYSTEM_STRINGIFY_IMPL(value) #value
#define SETTINGS_SYSTEM_STRINGIFY(value) SETTINGS_SYSTEM_STRINGIFY_IMPL(value)

const char *launcher_version()
{
#ifdef LAUNCHER_VERSION
    return LAUNCHER_VERSION;
#elif defined(LAUNCHER_VERSION_RAW)
    return SETTINGS_SYSTEM_STRINGIFY(LAUNCHER_VERSION_RAW);
#else
    return "0.0.0-dev";
#endif
}

const char *launcher_channel()
{
#ifdef LAUNCHER_CHANNEL
    return LAUNCHER_CHANNEL;
#elif defined(LAUNCHER_CHANNEL_RAW)
    return SETTINGS_SYSTEM_STRINGIFY(LAUNCHER_CHANNEL_RAW);
#else
    return "development";
#endif
}

const char *launcher_build_date()
{
#ifdef LAUNCHER_BUILD_DATE
    return LAUNCHER_BUILD_DATE;
#elif defined(LAUNCHER_BUILD_DATE_RAW)
    return SETTINGS_SYSTEM_STRINGIFY(LAUNCHER_BUILD_DATE_RAW);
#else
    return "unknown";
#endif
}

const char *launcher_commit()
{
#ifdef LAUNCHER_GIT_COMMIT
    return LAUNCHER_GIT_COMMIT;
#elif defined(LAUNCHER_GIT_COMMIT_RAW)
    return SETTINGS_SYSTEM_STRINGIFY(LAUNCHER_GIT_COMMIT_RAW);
#else
    return "unknown";
#endif
}

std::string lvgl_version()
{
    return std::to_string(LVGL_VERSION_MAJOR) + "." +
           std::to_string(LVGL_VERSION_MINOR) + "." +
           std::to_string(LVGL_VERSION_PATCH);
}

const lv_font_t *title_font()
{
    const lv_font_t *font = cp0_fonts().get("Montserrat-Bold.ttf", 14, LV_FREETYPE_FONT_STYLE_BOLD);
    return font ? font : &lv_font_montserrat_14;
}

const lv_font_t *body_font()
{
    return &lv_font_montserrat_12;
}

const lv_font_t *hint_font()
{
    const lv_font_t *font = cp0_fonts().get("Montserrat-Bold.ttf", 12, LV_FREETYPE_FONT_STYLE_BOLD);
    return font ? font : &lv_font_montserrat_12;
}

lv_obj_t *create_label(lv_obj_t *parent,
                       int x,
                       int y,
                       int width,
                       const char *text,
                       uint32_t color,
                       const lv_font_t *font,
                       bool wrap = false)
{
    if (!parent) return nullptr;
    lv_obj_t *label = lv_label_create(parent);
    if (!label) return nullptr;
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, width);
    lv_label_set_long_mode(label, wrap ? LV_LABEL_LONG_WRAP : LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, font ? font : body_font(), LV_PART_MAIN);
    return label;
}

void configure_page(lv_obj_t *object)
{
    if (!object) return;
    lv_obj_set_size(object, kScreenWidth, kScreenHeight);
    lv_obj_set_pos(object, 0, 0);
    lv_obj_set_style_bg_color(object, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(object, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(object, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(object, 0, LV_PART_MAIN);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

void set_label(lv_obj_t *label, const std::string &text)
{
    if (label) lv_label_set_text(label, text.c_str());
}

SettingsSystemPageKind resolve_kind(const NodeIter &page_node, SettingsSystemPageKind requested)
{
    if (requested != SettingsSystemPageKind::Auto) return requested;
    if (page_node.node && !page_node->label.empty()) {
        if (page_node->label == "Password") return SettingsSystemPageKind::Password;
        if (page_node->label == "OS") return SettingsSystemPageKind::OS;
        if (page_node->label == "Username" || page_node->label == "Hostname" ||
            page_node->label == "Account")
            return SettingsSystemPageKind::Account;
        if (page_node->label == "Version") return SettingsSystemPageKind::Version;
        if (page_node->label == "Build") return SettingsSystemPageKind::Build;
        if (page_node->label == "Ethernet" || page_node->label == "IP" ||
            page_node->label == "Gateway" || page_node->label == "MAC")
            return SettingsSystemPageKind::Ethernet;
    }
    return SettingsSystemPageKind::Network;
}

bool is_update_page_label(const NodeIter &page_node)
{
    if (!page_node.node) return false;
    return page_node->label == "Update" || page_node->label == "Update Launcher" ||
           page_node->label == "System update" || page_node->label == "Check system";
}

template <typename Owner>
bool start_osinfo_request(Owner *owner,
                          std::list<std::string> arguments,
                          std::function<void(int, std::string)> handler,
                          std::function<void(int, std::string)> stale_handler = {})
{
    if (!owner || !handler || !owner->ensure_async_dispatch()) return false;

    const auto token = owner->async_token();
    if (!token.valid()) return false;

    auto callback = [token,
                    handler = std::move(handler),
                    stale_handler = std::move(stale_handler)](int code,
                                                                std::string data) mutable {
        auto deliver_stale = [&stale_handler](int result_code, std::string result_data) {
            if (!stale_handler) return;
            try {
                stale_handler(result_code, std::move(result_data));
            } catch (...) {
            }
        };

        if (!token.valid()) {
            deliver_stale(code, std::move(data));
            return;
        }

        const std::string stale_data = data;
        const bool queued = SettingsAsync::Dispatch::enqueue_from_callback(
            token,
            [handler = std::move(handler), code, data = std::move(data)]() mutable {
                if (handler) handler(code, std::move(data));
            });
        if (!queued) deliver_stale(code, stale_data);
    };

    return owner->async_tasks().start(
        [arguments = std::move(arguments), callback = std::move(callback)](auto stop) mutable {
            if (stop && stop->load(std::memory_order_acquire)) return;
            settings_system::request(std::move(arguments), std::move(callback));
        });
}

void stop_timer(lv_timer_t *&timer)
{
    if (!timer) return;
    lv_timer_delete(timer);
    timer = nullptr;
}

} // namespace

struct LvSettingSystemInfoPage3::State
{
    SettingsSystemPageKind kind = SettingsSystemPageKind::Network;
    uint64_t generation = 1;
    bool request_pending = false;
    bool used_default_fallback = false;
    std::chrono::steady_clock::time_point request_deadline;
    lv_timer_t *request_timer = nullptr;
    settings_system::NetworkInfo network{"--", "--", "--"};
    settings_system::AccountInfo account{"--", "--"};
    SettingsAboutOsInfo os_info;
    std::string status;
    lv_obj_t *title = nullptr;
    lv_obj_t *line_one = nullptr;
    lv_obj_t *line_two = nullptr;
    lv_obj_t *line_three = nullptr;
    lv_obj_t *status_label = nullptr;
    lv_obj_t *hint = nullptr;
};

namespace {

constexpr std::chrono::seconds kInfoRequestTimeout{15};

void render_system_info(LvSettingSystemInfoPage3 *page);

void stop_system_request_timer(LvSettingSystemInfoPage3::State *state)
{
    if (!state) return;
    stop_timer(state->request_timer);
}

void system_info_request_timeout_cb(lv_timer_t *timer);

bool arm_system_request_timer(LvSettingSystemInfoPage3 *page)
{
    if (!page || !page->state_) return false;
    auto *state = page->state_.get();
    stop_system_request_timer(state);
    state->request_deadline = std::chrono::steady_clock::now() + kInfoRequestTimeout;
    state->request_timer = lv_timer_create(system_info_request_timeout_cb, 250, page);
    return state->request_timer != nullptr;
}

void system_info_request_timeout_cb(lv_timer_t *timer)
{
    auto *page = timer ? static_cast<LvSettingSystemInfoPage3 *>(lv_timer_get_user_data(timer)) : nullptr;
    if (!page || !page->state_ || timer != page->state_->request_timer) return;
    auto *state = page->state_.get();
    if (!state->request_pending) {
        stop_system_request_timer(state);
        return;
    }
    if (std::chrono::steady_clock::now() < state->request_deadline) return;

    stop_system_request_timer(state);
    state->request_pending = false;
    ++state->generation;
    if (state->generation == 0) state->generation = 1;
    page->advance_async_generation();
    state->status = settings_system::backend_error_label(-ETIMEDOUT);
    render_system_info(page);
}

void render_system_info(LvSettingSystemInfoPage3 *page)
{
    if (!page || !page->Get()) return;
    auto *state = page->state_.get();
    if (!state) return;

    switch (state->kind) {
    case SettingsSystemPageKind::Network:
    case SettingsSystemPageKind::Ethernet:
        set_label(state->title, "Ethernet");
        set_label(state->line_one, "IP: " + state->network.ip);
        set_label(state->line_two, "Gateway: " + state->network.gateway);
        set_label(state->line_three, "MAC: " + state->network.mac);
        break;
    case SettingsSystemPageKind::Account:
        set_label(state->title, "Account");
        set_label(state->line_one, "Username: " + state->account.username);
        set_label(state->line_two, "Password: " + settings_system::password_unsupported_label());
        set_label(state->line_three, "Hostname: " + state->account.hostname);
        break;
    case SettingsSystemPageKind::Password:
        set_label(state->title, "Account password");
        set_label(state->line_one, settings_system::password_unsupported_label());
        set_label(state->line_two, "The backend exposes read-only account information.");
        set_label(state->line_three, "No password is changed or stored by Settings.");
        break;
    case SettingsSystemPageKind::OS:
        set_label(state->title, "OS");
        set_label(state->line_one, "Build date: " + state->os_info.build_date);
        set_label(state->line_two, "Commit: " + state->os_info.commit);
        set_label(state->line_three, "");
        break;
    case SettingsSystemPageKind::Version:
        set_label(state->title, "Version");
        set_label(state->line_one, settings_system::version_label(launcher_version()));
        set_label(state->line_two, settings_system::build_label(
                                      launcher_build_date(), launcher_channel(), launcher_commit()));
        set_label(state->line_three, std::string("Commit: ") + launcher_commit());
        break;
    case SettingsSystemPageKind::Build:
        set_label(state->title, "Build");
        set_label(state->line_one, settings_system::build_label(
                                      launcher_build_date(), launcher_channel(), launcher_commit()));
        set_label(state->line_two, std::string("Build date: ") + launcher_build_date());
        set_label(state->line_three, std::string("Commit: ") + launcher_commit());
        break;
    case SettingsSystemPageKind::Auto:
        break;
    }

    set_label(state->status_label, state->status);
    if (state->hint) {
        const char *hint = state->request_pending ? "Loading..." : "ENTER: refresh   ESC: back";
        lv_label_set_text(state->hint, hint);
    }
}

void set_system_error(LvSettingSystemInfoPage3 *page, int code, const std::string &payload)
{
    if (!page || !page->state_) return;
    if (code == 0)
        page->state_->status = "Invalid system information payload";
    else
        page->state_->status = settings_system::backend_error_label(code, payload);
    render_system_info(page);
}

void request_network(LvSettingSystemInfoPage3 *page)
{
    if (!page || !page->state_ || page->state_->request_pending) return;
    auto *state = page->state_.get();
    page->advance_async_generation();
    ++state->generation;
    if (state->generation == 0) state->generation = 1;
    const uint64_t request_generation = state->generation;
    state->request_pending = true;
    state->status = "Loading Ethernet information...";
    state->used_default_fallback = false;
    if (!arm_system_request_timer(page)) {
        state->request_pending = false;
        state->status = "Unable to create Ethernet timeout timer";
        render_system_info(page);
        return;
    }
    render_system_info(page);

    const bool use_default = state->kind == SettingsSystemPageKind::Network;
    const std::string command = use_default ? "NetworkDefaultInfoRead" : "EthInfoRead";
    if (!start_osinfo_request(
            page,
            {command},
            [page, request_generation, use_default](int code, std::string payload) {
                auto *state = page ? page->state_.get() : nullptr;
                if (!state || request_generation != state->generation || !page->Get()) return;
                stop_system_request_timer(state);
                state->request_pending = false;
                settings_system::NetworkInfo candidate;
                if (code == 0 && settings_system::parse_network_info_strict(payload, candidate)) {
                    state->network = std::move(candidate);
                    state->status = "Ethernet information loaded";
                    render_system_info(page);
                    return;
                }

                if (!use_default && !state->used_default_fallback) {
                    state->used_default_fallback = true;
                    state->request_pending = true;
                    state->status = "Trying default network information...";
                    page->advance_async_generation();
                    const uint64_t fallback_generation = ++state->generation;
                    if (!arm_system_request_timer(page)) {
                        state->request_pending = false;
                        set_system_error(page, -1, "Unable to create Ethernet timeout timer");
                        return;
                    }
                    render_system_info(page);
                    if (start_osinfo_request(
                            page,
                            {"NetworkDefaultInfoRead"},
                            [page, fallback_generation](int fallback_code,
                                                         std::string fallback_payload) {
                                auto *fallback_state = page ? page->state_.get() : nullptr;
                                if (!fallback_state || fallback_generation != fallback_state->generation ||
                                    !page->Get())
                                    return;
                                stop_system_request_timer(fallback_state);
                                fallback_state->request_pending = false;
                                settings_system::NetworkInfo fallback_info;
                                if (fallback_code == 0 &&
                                    settings_system::parse_network_info_strict(fallback_payload, fallback_info)) {
                                    fallback_state->network = std::move(fallback_info);
                                    fallback_state->status = "Ethernet information loaded";
                                    render_system_info(page);
                                } else {
                                    set_system_error(page, fallback_code, fallback_payload);
                                }
                            }))
                        return;
                    state->request_pending = false;
                    stop_system_request_timer(state);
                }
                set_system_error(page, code, payload);
            })) {
        state->request_pending = false;
        stop_system_request_timer(state);
        state->status = "Unable to schedule Ethernet request";
        render_system_info(page);
    }
}

void request_account(LvSettingSystemInfoPage3 *page)
{
    if (!page || !page->state_ || page->state_->request_pending) return;
    auto *state = page->state_.get();
    page->advance_async_generation();
    ++state->generation;
    if (state->generation == 0) state->generation = 1;
    const uint64_t request_generation = state->generation;
    state->request_pending = true;
    state->status = "Loading account information...";
    if (!arm_system_request_timer(page)) {
        state->request_pending = false;
        state->status = "Unable to create account timeout timer";
        render_system_info(page);
        return;
    }
    render_system_info(page);

    if (!start_osinfo_request(
            page,
            {"AccountInfoRead"},
            [page, request_generation](int code, std::string payload) {
                auto *state = page ? page->state_.get() : nullptr;
                if (!state || request_generation != state->generation || !page->Get()) return;
                stop_system_request_timer(state);
                state->request_pending = false;
                settings_system::AccountInfo candidate;
                if (code == 0 && settings_system::parse_account_info_strict(payload, candidate)) {
                    state->account = std::move(candidate);
                    state->status = "Account information loaded";
                    render_system_info(page);
                } else {
                    set_system_error(page, code, payload);
                }
            })) {
        state->request_pending = false;
        stop_system_request_timer(state);
        state->status = "Unable to schedule account request";
        render_system_info(page);
    }
}

void refresh_system_info(LvSettingSystemInfoPage3 *page)
{
    if (!page || !page->state_) return;
    switch (page->state_->kind) {
    case SettingsSystemPageKind::Network:
    case SettingsSystemPageKind::Ethernet: request_network(page); break;
    case SettingsSystemPageKind::Account: request_account(page); break;
    case SettingsSystemPageKind::Password:
        page->state_->status = "Read-only account information";
        render_system_info(page);
        break;
    case SettingsSystemPageKind::OS:
        page->state_->os_info = SettingsAboutInfoModel::read_os_issue_file();
        if (page->state_->os_info.build_date == "unknown" &&
            page->state_->os_info.commit == "unknown")
            page->state_->status = "OS build information unavailable";
        else if (page->state_->os_info.build_date == "unknown" ||
                 page->state_->os_info.commit == "unknown")
            page->state_->status = "OS build information is incomplete";
        else
            page->state_->status = "OS build information loaded";
        render_system_info(page);
        break;
    case SettingsSystemPageKind::Version:
    case SettingsSystemPageKind::Build:
        page->state_->status = "Build information loaded";
        render_system_info(page);
        break;
    case SettingsSystemPageKind::Auto: break;
    }
}

void system_info_key_event(LvSettingSystemInfoPage3 *page, lv_event_t *event)
{
    if (!page || !event || lv_event_get_code(event) != LV_EVENT_KEY) return;
    const uint32_t key = lv_event_get_key(event);
    if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
        if (page->state_) {
            ++page->state_->generation;
            page->state_->request_pending = false;
            stop_system_request_timer(page->state_.get());
            page->advance_async_generation();
        }
        if (page->LeaveSelfPage) page->LeaveSelfPage();
    } else if (key == LV_KEY_ENTER || key == LV_KEY_RIGHT) {
        refresh_system_info(page);
    }
    lv_event_stop_processing(event);
}

} // namespace

LvSettingSystemInfoPage3::LvSettingSystemInfoPage3(lv_obj_t *parent,
                                                   const NodeIter &page_node,
                                                   std::function<void()> back_callback,
                                                   SettingsSystemPageKind kind)
    : state_(std::make_unique<State>())
{
    state_->kind = resolve_kind(page_node, kind);
    LeaveSelfPage = std::move(back_callback);
    create_ui(parent);
}

LvSettingSystemInfoPage3::~LvSettingSystemInfoPage3()
{
    if (state_) {
        ++state_->generation;
        state_->request_pending = false;
        stop_system_request_timer(state_.get());
    }
    cancel_async_tasks();
    if (ComponensObj) {
        lv_obj_delete(ComponensObj);
        ComponensObj = nullptr;
    }
    state_.reset();
}

void LvSettingSystemInfoPage3::AnimateNextIn(std::function<void()> animate_over_func)
{
    if (animate_over_func) animate_over_func();
}

void LvSettingSystemInfoPage3::AnimateNextOut(std::function<void()> animate_over_func)
{
    if (animate_over_func) animate_over_func();
}

void LvSettingSystemInfoPage3::LoadNextPage()
{
}

void LvSettingSystemInfoPage3::LeaveNextPage()
{
    if (state_) {
        ++state_->generation;
        if (state_->generation == 0) state_->generation = 1;
        state_->request_pending = false;
        stop_system_request_timer(state_.get());
        advance_async_generation();
    }
    if (LeaveSelfPage) LeaveSelfPage();
}

void LvSettingSystemInfoPage3::create_ui(lv_obj_t *parent)
{
    if (!parent || !state_) return;
    ComponensObj = lv_obj_create(parent);
    if (!ComponensObj) return;
    configure_page(ComponensObj);
    DComponens::lvgl_bind_event(
        ComponensObj,
        LV_EVENT_KEY,
        nullptr,
        std::bind(&system_info_key_event, this, std::placeholders::_1));

    state_->title = create_label(ComponensObj, 8, 3, kTextWidth, "System", 0x58A6FF, title_font());
    state_->line_one = create_label(ComponensObj, 8, 31, kTextWidth, "", 0xECECEC, body_font());
    state_->line_two = create_label(ComponensObj, 8, 53, kTextWidth, "", 0xCCCCCC, body_font());
    state_->line_three = create_label(ComponensObj, 8, 75, kTextWidth, "", 0xAAAAAA, body_font());
    state_->status_label = create_label(ComponensObj, 8, 99, kTextWidth, "", 0xF0C850, body_font(), true);
    state_->hint = create_label(ComponensObj, 8, 133, kTextWidth, "", 0x46DC87, hint_font());
    refresh_system_info(this);
}

struct LvSettingUpdatePage3::State
{
    settings_system::UpdateAction action = settings_system::UpdateAction::UpdateLauncher;
    uint64_t generation = 1;
    bool request_pending = false;
    bool poll_pending = false;
    bool update_pending = false;
    bool checking = false;
    bool cancelling = false;
    bool confirming = false;
    bool update_selected = true;
    bool leaving = false;
    std::string job_id;
    std::string backend_state;
    std::string candidate_version;
    std::string candidate_commit;
    std::string status = "Press ENTER to check for updates";
    int progress = settings_system::UpdateStatusInfo::kUnknownProgress;
    settings_system::UpdatePhase phase = settings_system::UpdatePhase::Idle;
    std::chrono::steady_clock::time_point deadline;
    lv_timer_t *poll_timer = nullptr;
    lv_obj_t *title = nullptr;
    lv_obj_t *device = nullptr;
    lv_obj_t *lvgl = nullptr;
    lv_obj_t *version = nullptr;
    lv_obj_t *build = nullptr;
    lv_obj_t *commit = nullptr;
    lv_obj_t *status_label = nullptr;
    lv_obj_t *hint = nullptr;
    lv_obj_t *dialog = nullptr;
    lv_obj_t *dialog_message = nullptr;
    lv_obj_t *update_button = nullptr;
    lv_obj_t *skip_button = nullptr;
    lv_obj_t *progress_bar = nullptr;
    lv_obj_t *progress_label = nullptr;
};

namespace {

bool is_update_job_state(const std::string &state)
{
    const auto info = settings_system::parse_update_status(state);
    return state == "running" || !info.stage.empty() ||
           state.rfind("recovering:", 0) == 0 ||
           state == "cancelled" || state == "canceled" ||
           state == "timeout" || state == "timed-out" ||
           state.rfind("succeeded:", 0) == 0 || state.rfind("failed:", 0) == 0;
}

bool is_update_progress_state(const std::string &state)
{
    const auto info = settings_system::parse_update_status(state);
    return !info.terminal && (info.stage == "running" || info.stage == "checking" ||
           info.stage == "downloading" || info.stage == "verifying" ||
           info.stage == "repairing" || info.stage == "installing" ||
           info.stage == "restarting" || info.stage == "recovering");
}

int stage_progress(const settings_system::UpdateStatusInfo &info)
{
    if (info.progress != settings_system::UpdateStatusInfo::kUnknownProgress)
        return info.progress;
    if (info.stage == "checking") return 10;
    if (info.stage == "downloading") return 25;
    if (info.stage == "verifying") return 45;
    if (info.stage == "repairing") return 55;
    if (info.stage == "installing") return 75;
    if (info.stage == "recovering") return 85;
    if (info.stage == "restarting") return 95;
    return 0;
}

std::string progress_status(const settings_system::UpdateStatusInfo &info)
{
    if (info.stage == "checking") return "Checking for updates...";
    if (info.stage == "downloading") return "Downloading APPLaunch update...";
    if (info.stage == "verifying") return "Verifying APPLaunch update...";
    if (info.stage == "repairing") return "Repairing package state...";
    if (info.stage == "installing") return "Installing APPLaunch update...";
    if (info.stage == "restarting") return "Restarting APPLaunch...";
    if (info.stage == "recovering") return "Restoring previous APPLaunch...";
    return "Working...";
}

void clear_dialog_state(LvSettingUpdatePage3::State *state)
{
    if (!state) return;
    state->dialog = nullptr;
    state->dialog_message = nullptr;
    state->update_button = nullptr;
    state->skip_button = nullptr;
    state->progress_bar = nullptr;
    state->progress_label = nullptr;
}

void close_update_dialog(LvSettingUpdatePage3 *page)
{
    if (!page || !page->state_) return;
    lv_obj_t *dialog = page->state_->dialog;
    clear_dialog_state(page->state_.get());
    if (dialog) lv_obj_delete(dialog);
}

void style_dialog(lv_obj_t *dialog)
{
    if (!dialog) return;
    lv_obj_set_size(dialog, 296, 132);
    lv_obj_center(dialog);
    lv_obj_set_style_radius(dialog, 4, LV_PART_MAIN);
    lv_obj_set_style_border_width(dialog, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(dialog, lv_color_hex(0x58A6FF), LV_PART_MAIN);
    lv_obj_set_style_bg_color(dialog, lv_color_hex(0x171717), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dialog, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);
}

void render_dialog_selection(LvSettingUpdatePage3::State *state)
{
    if (!state || !state->update_button || !state->skip_button) return;
    const auto style_button = [](lv_obj_t *button, bool selected) {
        lv_obj_set_style_bg_color(
            button, lv_color_hex(selected ? 0x2878C8 : 0x333333), LV_PART_MAIN);
        lv_obj_set_style_border_width(button, selected ? 2 : 0, LV_PART_MAIN);
        lv_obj_set_style_border_color(button, lv_color_hex(0x8FCBFF), LV_PART_MAIN);
    };
    style_button(state->update_button, state->update_selected);
    style_button(state->skip_button, !state->update_selected);
}

void show_update_confirmation(LvSettingUpdatePage3 *page)
{
    if (!page || !page->state_ || !page->Get()) return;
    auto *state = page->state_.get();
    close_update_dialog(page);
    state->confirming = true;
    state->update_selected = true;
    state->dialog = lv_msgbox_create(page->Get());
    if (!state->dialog) {
        state->confirming = false;
        state->status = "Unable to show update confirmation";
        return;
    }
    style_dialog(state->dialog);
    lv_obj_t *title = lv_msgbox_add_title(state->dialog, "New APPLaunch version");
    if (title) lv_obj_set_style_text_color(title, lv_color_hex(0x58A6FF), LV_PART_MAIN);
    const std::string message = "Version: " +
        (state->candidate_version.empty() ? std::string("unknown") : state->candidate_version) +
        "\nCommit: " +
        (state->candidate_commit.empty() ? std::string("unknown") : state->candidate_commit);
    state->dialog_message = lv_msgbox_add_text(state->dialog, message.c_str());
    if (state->dialog_message) {
        lv_label_set_long_mode(state->dialog_message, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(state->dialog_message, &lv_font_montserrat_12, LV_PART_MAIN);
    }
    state->update_button = lv_msgbox_add_footer_button(state->dialog, "Update");
    state->skip_button = lv_msgbox_add_footer_button(state->dialog, "Not now");
    if (state->update_button) lv_obj_set_flex_grow(state->update_button, 1);
    if (state->skip_button) lv_obj_set_flex_grow(state->skip_button, 1);
    render_dialog_selection(state);
}

void show_update_progress(LvSettingUpdatePage3 *page)
{
    if (!page || !page->state_ || !page->Get()) return;
    auto *state = page->state_.get();
    close_update_dialog(page);
    state->dialog = lv_msgbox_create(page->Get());
    if (!state->dialog) return;
    style_dialog(state->dialog);
    lv_obj_t *title = lv_msgbox_add_title(state->dialog, "Updating APPLaunch");
    if (title) lv_obj_set_style_text_color(title, lv_color_hex(0x58A6FF), LV_PART_MAIN);
    lv_obj_t *content = lv_msgbox_get_content(state->dialog);
    if (!content) return;
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(content, 8, LV_PART_MAIN);
    state->progress_label = lv_label_create(content);
    if (state->progress_label) {
        lv_obj_set_width(state->progress_label, lv_pct(100));
        lv_label_set_long_mode(state->progress_label, LV_LABEL_LONG_DOT);
        lv_label_set_text(state->progress_label, state->status.c_str());
    }
    state->progress_bar = lv_bar_create(content);
    if (state->progress_bar) {
        lv_obj_set_size(state->progress_bar, lv_pct(100), 10);
        lv_bar_set_range(state->progress_bar, 0, 100);
        lv_bar_set_value(state->progress_bar, std::max(0, state->progress), LV_ANIM_OFF);
        lv_obj_set_style_bg_color(
            state->progress_bar, lv_color_hex(0x2878C8), LV_PART_INDICATOR);
    }
}

std::string update_failure_status(settings_system::UpdateAction action,
                                  int code,
                                  const std::string &state)
{
    if (code != 0 && (state.empty() || !is_update_job_state(state)))
        return settings_system::backend_error_label(code, state);
    return settings_system::update_job_label(action, code, state);
}

void render_update_page(LvSettingUpdatePage3 *page)
{
    if (!page || !page->Get() || !page->state_) return;
    auto *state = page->state_.get();
    set_label(state->title, "APPLaunch");
    set_label(state->device, "Device: M5CardputerZero");
    set_label(state->lvgl, "LVGL: " + lvgl_version());
    set_label(state->version, "APPLaunch: " + std::string(launcher_version()));
    set_label(state->build, "Build date: " + std::string(launcher_build_date()));
    set_label(state->commit, "Commit: " + std::string(launcher_commit()));
    set_label(state->status_label, state->status);
    if (state->progress_label) set_label(state->progress_label, state->status);
    if (state->progress_bar)
        lv_bar_set_value(state->progress_bar, std::max(0, state->progress), LV_ANIM_ON);

    const char *hint = "ENTER: check   ESC: back";
    if (state->request_pending || state->update_pending)
        hint = "ESC: cancel";
    else if (state->confirming)
        hint = "";
    else if (state->phase != settings_system::UpdatePhase::Idle)
        hint = "ENTER: check again   ESC: back";
    if (state->hint) lv_label_set_text(state->hint, hint);
}

void schedule_job_cancel(std::string job_id)
{
    if (job_id.empty()) return;
    settings_system::request(
        {"UpdateJobCancel", std::move(job_id)}, [](int, std::string) {});
}

void stop_update_poll(LvSettingUpdatePage3::State *state)
{
    if (!state) return;
    stop_timer(state->poll_timer);
    state->poll_pending = false;
}

void finish_update(LvSettingUpdatePage3 *page,
                   settings_system::UpdatePhase phase,
                   int code,
                   std::string backend_state,
                   bool cancel_backend)
{
    if (!page || !page->state_) return;
    auto *state = page->state_.get();
    const std::string job_id = std::move(state->job_id);
    state->job_id.clear();
    stop_update_poll(state);
    state->request_pending = false;
    state->update_pending = false;
    state->checking = false;
    state->cancelling = false;
    state->confirming = false;
    ++state->generation;
    if (state->generation == 0) state->generation = 1;
    page->advance_async_generation();
    state->backend_state = std::move(backend_state);
    state->phase = phase;
    if (phase == settings_system::UpdatePhase::Succeeded) {
        state->status = settings_system::update_job_label(state->action, code, state->backend_state);
    } else if (phase == settings_system::UpdatePhase::Failed) {
        state->status = update_failure_status(state->action, code, state->backend_state);
    } else {
        state->status = settings_system::update_phase_label(
            state->action, phase, code, state->backend_state);
    }
    if (phase != settings_system::UpdatePhase::Running) close_update_dialog(page);
    render_update_page(page);
    if (cancel_backend && !job_id.empty()) schedule_job_cancel(job_id);
}

void poll_update(LvSettingUpdatePage3 *page);

void finish_check(LvSettingUpdatePage3 *page,
                  int code,
                  std::string payload,
                  const settings_system::UpdateStatusInfo &info)
{
    if (!page || !page->state_) return;
    if (code != 0 || !info.terminal || !info.availability_known) {
        finish_update(page, settings_system::UpdatePhase::Failed,
                      code == 0 ? -1 : code, std::move(payload), false);
        if (page->state_) {
            page->state_->status = "Unable to determine whether an update is available";
            render_update_page(page);
        }
        return;
    }

    const std::string version = info.version;
    const std::string commit = info.commit;
    finish_update(page, settings_system::UpdatePhase::Succeeded, code, payload, false);
    if (!page->state_) return;
    auto *state = page->state_.get();
    state->candidate_version = version;
    state->candidate_commit = commit;
    if (!info.available) {
        state->status = version.empty()
            ? "APPLaunch is up to date"
            : "APPLaunch " + version + " is up to date";
        render_update_page(page);
        return;
    }
    state->status = version.empty()
        ? "A new APPLaunch version is available"
        : "APPLaunch " + version + " is available";
    show_update_confirmation(page);
    render_update_page(page);
}

void update_poll_timer_cb(lv_timer_t *timer) noexcept
{
    auto *page = timer ? static_cast<LvSettingUpdatePage3 *>(lv_timer_get_user_data(timer)) : nullptr;
    if (!page || !page->state_ || timer != page->state_->poll_timer) return;
    poll_update(page);
}

void poll_update(LvSettingUpdatePage3 *page)
{
    if (!page || !page->state_) return;
    auto *state = page->state_.get();
    if (!state->request_pending && !state->update_pending) return;
    if (std::chrono::steady_clock::now() >= state->deadline) {
        finish_update(page, settings_system::UpdatePhase::TimedOut, -ETIMEDOUT, "timeout", true);
        return;
    }
    if (!state->update_pending || state->poll_pending || state->job_id.empty()) return;

    state->poll_pending = true;
    const uint64_t request_generation = state->generation;
    const std::string job_id = state->job_id;
    if (!start_osinfo_request(
            page,
            {"UpdateJobStatus", job_id},
            [page, request_generation](int code, std::string payload) {
                auto *state = page ? page->state_.get() : nullptr;
                if (!state || request_generation != state->generation || !page->Get()) return;
                state->poll_pending = false;
                if (!state->update_pending) return;
                const auto info = settings_system::parse_update_status(payload);
                if (code == 0 && is_update_progress_state(payload)) {
                    state->phase = settings_system::UpdatePhase::Running;
                    state->progress = stage_progress(info);
                    state->status = state->cancelling
                        ? "Cancelling update..."
                        : progress_status(info);
                    render_update_page(page);
                    return;
                }
                if (payload == "cancelled" || payload == "canceled" ||
                    payload.rfind("failed:cancelled:", 0) == 0 ||
                    payload.rfind("failed:canceled:", 0) == 0) {
                    finish_update(page, settings_system::UpdatePhase::Cancelled, code,
                                  std::move(payload), false);
                    return;
                }
                if (state->checking) {
                    finish_check(page, code, std::move(payload), info);
                    return;
                }
                if (code == 0 && payload.rfind("succeeded:", 0) == 0) {
                    state->progress = 100;
                    finish_update(page, settings_system::UpdatePhase::Succeeded, code,
                                  std::move(payload), false);
                    return;
                }
                finish_update(page, settings_system::UpdatePhase::Failed, code,
                              std::move(payload), false);
            })) {
        state->poll_pending = false;
        finish_update(page, settings_system::UpdatePhase::Failed, -1,
                      "status request could not be scheduled", true);
    }
}

void start_update(LvSettingUpdatePage3 *page, bool check_only = false)
{
    if (!page || !page->state_) return;
    auto *state = page->state_.get();
    if (state->request_pending || state->update_pending || state->leaving) return;

    page->advance_async_generation();
    ++state->generation;
    if (state->generation == 0) state->generation = 1;
    const uint64_t request_generation = state->generation;
    state->request_pending = true;
    state->checking = check_only;
    state->confirming = false;
    state->progress = 0;
    state->phase = settings_system::UpdatePhase::Starting;
    state->status = check_only ? "Starting update check..." : "Starting APPLaunch update...";
    state->deadline = std::chrono::steady_clock::now() + kUpdateTimeout;
    state->poll_timer = lv_timer_create(update_poll_timer_cb, kUpdatePollMs, page);
    if (!state->poll_timer) {
        state->request_pending = false;
        finish_update(page, settings_system::UpdatePhase::Failed, -1,
                      "status timer unavailable", false);
        return;
    }
    if (!check_only) show_update_progress(page);
    render_update_page(page);

    const char *command = check_only
        ? "UpdateLauncherCheckStart"
        : settings_system::update_request(state->action);
    if (!start_osinfo_request(
            page,
            {command},
            [page, request_generation](int code, std::string payload) {
                auto *state = page ? page->state_.get() : nullptr;
                if (!state || request_generation != state->generation || !page->Get()) return;
                state->request_pending = false;
                if (code != 0 || payload.empty()) {
                    finish_update(page, settings_system::UpdatePhase::Failed,
                                  code == 0 ? -1 : code,
                                  payload.empty() ? "update service returned no job id" :
                                                     std::move(payload),
                                  false);
                    return;
                }
                state->job_id = std::move(payload);
                state->update_pending = true;
                state->phase = settings_system::UpdatePhase::Running;
                if (state->cancelling) {
                    schedule_job_cancel(state->job_id);
                    state->status = "Cancellation requested...";
                } else {
                    state->status = state->checking
                        ? "Checking for updates..."
                        : "Updating APPLaunch...";
                }
                render_update_page(page);
                lv_timer_ready(state->poll_timer);
            },
            [](int code, std::string payload) {
                if (code != 0 || payload.empty()) return;
                settings_system::request(
                    {"UpdateJobCancel", std::move(payload)},
                    [](int, std::string) {});
            })) {
        state->request_pending = false;
        finish_update(page, settings_system::UpdatePhase::Failed, -1,
                      "update request could not be scheduled", false);
    }
}

void cancel_update(LvSettingUpdatePage3 *page, bool leave_page)
{
    if (!page || !page->state_ || page->state_->leaving) return;
    auto *state = page->state_.get();
    const bool active = state->request_pending || state->update_pending;
    if (active) {
        if (leave_page) {
            if (!state->job_id.empty()) schedule_job_cancel(state->job_id);
        } else if (!state->cancelling) {
            if (state->job_id.empty()) {
                state->cancelling = true;
                state->status = "Waiting for update job before cancelling...";
            } else {
                state->cancelling = true;
                state->status = "Cancelling update...";
                const uint64_t request_generation = state->generation;
                const std::string job_id = state->job_id;
                if (!start_osinfo_request(
                        page,
                        {"UpdateJobCancel", job_id},
                        [page, request_generation](int code, std::string) {
                            auto *cancel_state = page ? page->state_.get() : nullptr;
                            if (!cancel_state || request_generation != cancel_state->generation ||
                                !page->Get())
                                return;
                            if (code != 0) {
                                cancel_state->cancelling = false;
                                cancel_state->status = "Unable to cancel; update is still running";
                            } else {
                                cancel_state->status = "Cancellation requested...";
                            }
                            render_update_page(page);
                        })) {
                    state->cancelling = false;
                    state->status = "Unable to send cancellation request";
                }
            }
            render_update_page(page);
        }
    } else {
        ++state->generation;
        if (state->generation == 0) state->generation = 1;
        page->advance_async_generation();
    }
    if (leave_page) {
        state->leaving = true;
        if (page->LeaveSelfPage) page->LeaveSelfPage();
    }
}

void update_key_event(LvSettingUpdatePage3 *page, lv_event_t *event)
{
    if (!page || !event || lv_event_get_code(event) != LV_EVENT_KEY) return;
    const uint32_t key = lv_event_get_key(event);
    auto *state = page->state_.get();
    if (!state) return;

    if (state->confirming) {
        if (key == LV_KEY_LEFT || key == LV_KEY_RIGHT) {
            state->update_selected = key == LV_KEY_LEFT;
            render_dialog_selection(state);
        } else if (key == LV_KEY_ENTER) {
            const bool update = state->update_selected;
            state->confirming = false;
            close_update_dialog(page);
            if (update) start_update(page, false);
            else {
                state->status = "Update skipped";
                render_update_page(page);
            }
        } else if (key == LV_KEY_ESC) {
            state->confirming = false;
            close_update_dialog(page);
            state->status = "Update skipped";
            render_update_page(page);
        }
    } else if (state->request_pending || state->update_pending) {
        if (key == LV_KEY_ESC) cancel_update(page, false);
    } else if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
        if (page->LeaveSelfPage) page->LeaveSelfPage();
    } else if (key == LV_KEY_ENTER) {
        start_update(page, true);
    }
    lv_event_stop_processing(event);
}

} // namespace

LvSettingUpdatePage3::LvSettingUpdatePage3(lv_obj_t *parent,
                                           const NodeIter &page_node,
                                           std::function<void()> back_callback,
                                           settings_system::UpdateAction action)
    : state_(std::make_unique<State>())
{
    state_->action = action;
    if (page_node.node &&
        (page_node->label == "System update" || page_node->label == "Check system"))
        state_->action = settings_system::UpdateAction::CheckSystem;
    LeaveSelfPage = std::move(back_callback);
    create_ui(parent);
}

LvSettingUpdatePage3::~LvSettingUpdatePage3()
{
    if (state_) {
        state_->leaving = true;
        const std::string job_id = std::move(state_->job_id);
        state_->job_id.clear();
        stop_update_poll(state_.get());
        ++state_->generation;
        state_->request_pending = false;
        state_->update_pending = false;
        if (!job_id.empty()) schedule_job_cancel(job_id);
    }
    cancel_async_tasks();
    if (ComponensObj) {
        lv_obj_delete(ComponensObj);
        ComponensObj = nullptr;
    }
    state_.reset();
}

void LvSettingUpdatePage3::AnimateNextIn(std::function<void()> animate_over_func)
{
    if (animate_over_func) animate_over_func();
}

void LvSettingUpdatePage3::AnimateNextOut(std::function<void()> animate_over_func)
{
    if (animate_over_func) animate_over_func();
}

void LvSettingUpdatePage3::LoadNextPage()
{
}

void LvSettingUpdatePage3::LeaveNextPage()
{
    cancel_update(this, true);
}

void LvSettingUpdatePage3::create_ui(lv_obj_t *parent)
{
    if (!parent || !state_) return;
    ComponensObj = lv_obj_create(parent);
    if (!ComponensObj) return;
    configure_page(ComponensObj);
    DComponens::lvgl_bind_event(
        ComponensObj,
        LV_EVENT_KEY,
        nullptr,
        std::bind(&update_key_event, this, std::placeholders::_1));

    state_->title = create_label(ComponensObj, 8, 2, kTextWidth, "APPLaunch", 0x58A6FF, title_font());
    state_->device = create_label(ComponensObj, 8, 22, kTextWidth, "", 0xECECEC, body_font());
    state_->lvgl = create_label(ComponensObj, 8, 39, kTextWidth, "", 0xDADADA, body_font());
    state_->version = create_label(ComponensObj, 8, 56, kTextWidth, "", 0xCCCCCC, body_font());
    state_->build = create_label(ComponensObj, 8, 73, kTextWidth, "", 0xBBBBBB, body_font());
    state_->commit = create_label(ComponensObj, 8, 90, kTextWidth, "", 0xAAAAAA, body_font());
    state_->status_label = create_label(ComponensObj, 8, 108, kTextWidth, "", 0xF0C850, body_font(), true);
    state_->hint = create_label(ComponensObj, 8, 133, kTextWidth, "", 0x46DC87, hint_font());
    render_update_page(this);
}

std::unique_ptr<DComponens::LvglComponensBase> settings_system_page_factory(
    lv_obj_t *parent,
    const NodeIter &page_node,
    std::function<void()> back_callback)
{
    if (is_update_page_label(page_node))
        return settings_update_page_factory(parent, page_node, std::move(back_callback));
    return std::make_unique<LvSettingSystemInfoPage3>(
        parent, page_node, std::move(back_callback), SettingsSystemPageKind::Auto);
}

std::unique_ptr<DComponens::LvglComponensBase> settings_ethernet_page_factory(
    lv_obj_t *parent,
    const NodeIter &page_node,
    std::function<void()> back_callback)
{
    return std::make_unique<LvSettingSystemInfoPage3>(
        parent, page_node, std::move(back_callback), SettingsSystemPageKind::Ethernet);
}

std::unique_ptr<DComponens::LvglComponensBase> settings_account_page_factory(
    lv_obj_t *parent,
    const NodeIter &page_node,
    std::function<void()> back_callback)
{
    return std::make_unique<LvSettingSystemInfoPage3>(
        parent, page_node, std::move(back_callback), SettingsSystemPageKind::Account);
}

std::unique_ptr<DComponens::LvglComponensBase> settings_update_page_factory(
    lv_obj_t *parent,
    const NodeIter &page_node,
    std::function<void()> back_callback)
{
    return std::make_unique<LvSettingUpdatePage3>(
        parent, page_node, std::move(back_callback), settings_system::UpdateAction::UpdateLauncher);
}

std::unique_ptr<DComponens::LvglComponensBase> settings_system_info_page3_factory(
    lv_obj_t *parent,
    const NodeIter &page_node,
    std::function<void()> back_callback)
{
    return settings_system_page_factory(parent, page_node, std::move(back_callback));
}

std::unique_ptr<DComponens::LvglComponensBase> settings_ethernet_page3_factory(
    lv_obj_t *parent,
    const NodeIter &page_node,
    std::function<void()> back_callback)
{
    return settings_ethernet_page_factory(parent, page_node, std::move(back_callback));
}

std::unique_ptr<DComponens::LvglComponensBase> settings_account_page3_factory(
    lv_obj_t *parent,
    const NodeIter &page_node,
    std::function<void()> back_callback)
{
    return settings_account_page_factory(parent, page_node, std::move(back_callback));
}

std::unique_ptr<DComponens::LvglComponensBase> settings_update_page3_factory(
    lv_obj_t *parent,
    const NodeIter &page_node,
    std::function<void()> back_callback)
{
    return settings_update_page_factory(parent, page_node, std::move(back_callback));
}
