#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "lvgl_components.hpp"
#include "settings_rtc_api.hpp"

class LvSettingRtcPage3 : public LvSettingValuePage3Base {
public:
    LvSettingRtcPage3() = default;

    LvSettingRtcPage3(lv_obj_t *parent, const NodeIter &parent_node)
        : LvSettingValuePage3Base(parent_node, {})
    {
        install_actions();
        initialize(parent);
        create_status_label();
        start_refresh();
    }

    LvSettingRtcPage3(lv_obj_t *parent,
                      const NodeIter &parent_node,
                      std::function<void()> back_callback)
        : LvSettingValuePage3Base(parent_node, std::move(back_callback))
    {
        install_actions();
        initialize(parent);
        create_status_label();
        start_refresh();
    }

    ~LvSettingRtcPage3() override
    {
        cancel_async_tasks();
        restore_actions();
        status_label_ = nullptr;
    }

    const std::string &last_error() const noexcept { return last_error_; }

protected:
    int initial_selection() const override
    {
        settings_rtc::RtcField field = settings_rtc::RtcField::YEAR;
        if (!settings_rtc::RtcStateModel::field_from_name(parent_node()->label, field)) return 0;
        const int selection = settings_rtc::session().state().field_selection_index(field);
        return selection < 0 ? 0 : selection;
    }

private:
    struct ActionBackup {
        SettingEntry *entry = nullptr;
        SettingApiAsyncCallBackFunc async_api;
        SettingActivationPolicy policy = SettingActivationPolicy::LeaveImmediately;
    };

    static bool field_for_node(const NodeIter &node, settings_rtc::RtcField &field)
    {
        return settings_rtc::RtcStateModel::field_from_name(node->label, field);
    }

    void install_actions()
    {
        settings_rtc::RtcField field = settings_rtc::RtcField::YEAR;
        if (!field_for_node(parent_node(), field)) return;

        for (auto child = parent_node().begin(); child != parent_node().end(); ++child) {
            actions_.push_back(ActionBackup{&*child, child->Async_api, child->activation_policy});
            child->Async_api = [this, field](int command, void *data) -> SettingApiResult {
                if (command != SettingApiActivate) return SettingApiResult::NotHandled;
                auto *value_page = static_cast<LvSettingValuePage3Base *>(data);
                if (!value_page || value_page->selected_index < 0) {
                    set_error("Invalid RTC value");
                    return SettingApiResult::Failure;
                }

                auto &workflow = settings_rtc::session();
                if (refresh_pending_) {
                    set_error("Reading RTC status");
                    return SettingApiResult::Failure;
                }
                if (workflow.pending()) {
                    set_error("RTC operation is pending");
                    return SettingApiResult::Failure;
                }
                if (workflow.state().ntp_on()) {
                    set_error("Disable NTP before editing time");
                    return SettingApiResult::Failure;
                }
                if (!workflow.edit_field_selection(field,
                                                   static_cast<std::size_t>(value_page->selected_index))) {
                    set_error("Invalid calendar date");
                    return SettingApiResult::Failure;
                }

                clear_error();
                return SettingApiResult::Success;
            };
            child->activation_policy = SettingActivationPolicy::WaitForResult;
        }
    }

    void restore_actions() noexcept
    {
        for (const ActionBackup &backup : actions_) {
            if (!backup.entry) continue;
            backup.entry->Async_api = backup.async_api;
            backup.entry->activation_policy = backup.policy;
        }
        actions_.clear();
    }

    void create_status_label()
    {
        if (!ComponensObj) return;
        status_label_ = lv_label_create(ComponensObj);
        if (!status_label_) return;
        lv_obj_set_width(status_label_, 232);
        lv_obj_set_pos(status_label_, 84, 118);
        lv_obj_set_style_text_color(status_label_, lv_color_hex(0xFF6666), LV_PART_MAIN);
        lv_obj_set_style_text_font(
            status_label_, cp0_fonts().get("Montserrat-Bold.ttf", 10, LV_FREETYPE_FONT_STYLE_BOLD), LV_PART_MAIN);
        lv_label_set_text(status_label_, "");
        lv_obj_add_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
    }

    void start_refresh()
    {
        refresh_pending_ = true;
        if (!ensure_async_dispatch()) {
            refresh_pending_ = false;
            set_error("RTC refresh unavailable");
            return;
        }

        const auto token = async_token();
        const int start_result = settings_rtc::refresh_async(
            [this, token](settings_rtc::RefreshResult result) mutable {
                SettingsAsync::Dispatch::enqueue_from_callback(
                    token,
                    [this, token, result = std::move(result)]() mutable {
                        if (!token.valid() || !ComponensObj) return;
                        refresh_pending_ = false;
                        auto &workflow = settings_rtc::session();
                        workflow.set_ntp_status(result.ntp.status);
                        settings_rtc::update_ntp_cache(result.ntp.status);
                        const bool time_loaded =
                            result.time.valid && workflow.load_local_time(result.time.payload);
                        if (!result.ntp.available) {
                            set_error("NTP status unavailable");
                        } else if (!time_loaded) {
                            set_error("RTC time unavailable");
                        } else {
                            clear_error();
                        }
                        select(initial_selection());
                    });
            });
        if (start_result != 0) {
            refresh_pending_ = false;
            set_error("Unable to read RTC status");
        }
    }

    void set_error(const char *message)
    {
        last_error_ = message ? message : "RTC operation failed";
        if (status_label_) {
            lv_label_set_text(status_label_, last_error_.c_str());
            lv_obj_clear_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void clear_error()
    {
        last_error_.clear();
        if (status_label_) {
            lv_label_set_text(status_label_, "");
            lv_obj_add_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    std::vector<ActionBackup> actions_;
    lv_obj_t *status_label_ = nullptr;
    std::string last_error_;
    bool refresh_pending_ = false;
};

class LvSettingRtcConfirmPage3 : public LvSettingValuePage3Base {
public:
    LvSettingRtcConfirmPage3() = default;

    LvSettingRtcConfirmPage3(lv_obj_t *parent,
                             const NodeIter &parent_node,
                             std::function<void()> back_callback = {})
        : LvSettingValuePage3Base(parent_node, {}), back_callback_(std::move(back_callback))
    {
        LeaveSelfPage = [this] { leave_page(); };
        install_actions();
        initialize(parent);
        create_status_label();
    }

    ~LvSettingRtcConfirmPage3() override
    {
        cancel_backend_request();
        cancel_async_tasks();
        restore_actions();
        status_label_ = nullptr;
    }

    const std::string &last_error() const noexcept { return last_error_; }

protected:
    int initial_selection() const override
    {
        return 1;
    }

private:
    struct ActionBackup {
        SettingEntry *entry = nullptr;
        SettingApiAsyncCallBackFunc async_api;
        SettingActivationPolicy policy = SettingActivationPolicy::LeaveImmediately;
    };

    struct RequestState {
        std::atomic<std::uint64_t> request_id{0};
        std::atomic_bool cancelled{false};
        std::atomic_bool terminal{false};
    };

    void install_actions()
    {
        for (auto child = parent_node().begin(); child != parent_node().end(); ++child) {
            actions_.push_back(ActionBackup{&*child, child->Async_api, child->activation_policy});
            const bool save = child->label == "Yes";
            child->Async_api = [this, save](int command, void *) -> SettingApiResult {
                if (command != SettingApiActivate) return SettingApiResult::NotHandled;
                return save ? begin_save() : discard_and_leave();
            };
            child->activation_policy = SettingActivationPolicy::WaitForResult;
        }
    }

    void restore_actions() noexcept
    {
        for (const ActionBackup &backup : actions_) {
            if (!backup.entry) continue;
            backup.entry->Async_api = backup.async_api;
            backup.entry->activation_policy = backup.policy;
        }
        actions_.clear();
    }

    SettingApiResult discard_and_leave()
    {
        if (activation_pending()) return SettingApiResult::Failure;
        cancel_backend_request();
        settings_rtc::session().discard_edits();
        clear_error();
        return SettingApiResult::Success;
    }

    SettingApiResult begin_save()
    {
        auto &workflow = settings_rtc::session();
        const auto eligibility = workflow.commit_eligibility();
        if (eligibility == settings_rtc::CommitEligibility::NTP_ENABLED) {
            set_error("Disable NTP before writing RTC");
            return SettingApiResult::Failure;
        }
        if (eligibility == settings_rtc::CommitEligibility::NO_EDITS) {
            set_error("No time changes to save");
            return SettingApiResult::Failure;
        }
        if (!workflow.begin_time_commit()) {
            set_error("RTC write is already pending");
            return SettingApiResult::Failure;
        }

        auto request = std::make_shared<RequestState>();
        request_state_ = request;
        const auto sink = activation_sink();
        const std::string timestamp = workflow.state().timestamp();
        const int start_result = settings_rtc::set_time_async(
            timestamp,
            [sink, request](settings_rtc::PrivilegedResult result) {
                enqueue_outcome(sink, request, result.kind);
            },
            [sink, request](int start_code, std::uint64_t request_id) {
                if (start_code != 0) {
                    enqueue_outcome(sink, request, settings_rtc::PrivilegedResultKind::EXEC_FAILED);
                    return;
                }

                request->request_id.store(request_id, std::memory_order_release);
                if (request->cancelled.load(std::memory_order_acquire)) {
                    const std::uint64_t active_request =
                        request->request_id.exchange(0, std::memory_order_acq_rel);
                    if (active_request != 0) settings_rtc::cancel_request(active_request);
                }
            });

        if (start_result != 0) {
            workflow.finish_time_commit(false);
            request_state_.reset();
            set_error("Unable to start RTC write");
            return SettingApiResult::Failure;
        }
        clear_error();
        return SettingApiResult::Pending;
    }

    static void enqueue_outcome(const ActivationSink &sink,
                                const std::shared_ptr<RequestState> &request,
                                settings_rtc::PrivilegedResultKind outcome) noexcept
    {
        if (request->terminal.exchange(true, std::memory_order_acq_rel)) return;
        if (!sink.valid()) return;

        const bool queued = SettingsAsync::Dispatch::enqueue_from_callback(
            sink.dispatch_token,
            [sink, request, outcome] {
                if (!sink.valid()) return;
                auto *page = static_cast<LvSettingRtcConfirmPage3 *>(sink.owner);
                const bool succeeded = outcome == settings_rtc::PrivilegedResultKind::SUCCESS;
                settings_rtc::session().finish_time_commit(succeeded);
                if (page) {
                    request->request_id.store(0, std::memory_order_release);
                    if (!succeeded) page->set_error(error_message(outcome));
                    else page->clear_error();
                    if (page->request_state_ == request) page->request_state_.reset();
                }
                page->finish_activation(
                    sink,
                    succeeded ? SettingApiResult::Success
                              : (outcome == settings_rtc::PrivilegedResultKind::CANCELLED
                                     ? SettingApiResult::Cancelled
                                     : SettingApiResult::Failure));
            });
        if (!queued) settings_rtc::session().cancel_time_commit();
    }

    static const char *error_message(settings_rtc::PrivilegedResultKind result) noexcept
    {
        switch (result) {
        case settings_rtc::PrivilegedResultKind::AUTH_FAILED: return "RTC authentication failed";
        case settings_rtc::PrivilegedResultKind::CANCELLED: return "RTC write cancelled";
        case settings_rtc::PrivilegedResultKind::TIMED_OUT: return "RTC write timed out";
        case settings_rtc::PrivilegedResultKind::EXEC_FAILED: return "RTC write failed";
        case settings_rtc::PrivilegedResultKind::SUCCESS: break;
        }
        return "RTC write failed";
    }

    void create_status_label()
    {
        if (!ComponensObj) return;
        status_label_ = lv_label_create(ComponensObj);
        if (!status_label_) return;
        lv_obj_set_width(status_label_, 232);
        lv_obj_set_pos(status_label_, 84, 118);
        lv_obj_set_style_text_color(status_label_, lv_color_hex(0xFF6666), LV_PART_MAIN);
        lv_obj_set_style_text_font(
            status_label_, cp0_fonts().get("Montserrat-Bold.ttf", 10, LV_FREETYPE_FONT_STYLE_BOLD), LV_PART_MAIN);
        lv_label_set_text(status_label_, "");
        lv_obj_add_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
    }

    void set_error(const char *message)
    {
        last_error_ = message ? message : "RTC write failed";
        if (status_label_) {
            lv_label_set_text(status_label_, last_error_.c_str());
            lv_obj_clear_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void clear_error()
    {
        last_error_.clear();
        if (status_label_) {
            lv_label_set_text(status_label_, "");
            lv_obj_add_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void cancel_backend_request() noexcept
    {
        if (request_state_) {
            const auto request = request_state_;
            request->cancelled.store(true, std::memory_order_release);
            request->terminal.store(true, std::memory_order_release);
            const std::uint64_t request_id = request->request_id.exchange(0, std::memory_order_acq_rel);
            if (request_id != 0) settings_rtc::cancel_request(request_id);
            request_state_.reset();
        }
        settings_rtc::session().cancel_time_commit();
    }

    void leave_page()
    {
        cancel_backend_request();
        settings_rtc::session().discard_edits();
        if (back_callback_) back_callback_();
    }

    std::function<void()> back_callback_;
    std::vector<ActionBackup> actions_;
    std::shared_ptr<RequestState> request_state_;
    lv_obj_t *status_label_ = nullptr;
    std::string last_error_;
};

inline std::unique_ptr<DComponens::LvglComponensBase> settings_rtc_page_factory(
    lv_obj_t *parent,
    const NodeIter &parent_node,
    std::function<void()> back_callback)
{
    return std::make_unique<LvSettingRtcPage3>(parent, parent_node, std::move(back_callback));
}

inline std::unique_ptr<DComponens::LvglComponensBase> settings_rtc_confirm_page_factory(
    lv_obj_t *parent,
    const NodeIter &parent_node,
    std::function<void()> back_callback)
{
    return std::make_unique<LvSettingRtcConfirmPage3>(parent, parent_node, std::move(back_callback));
}
