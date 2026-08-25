#pragma once

#include "settings_sound_card_detail_page.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class LvSettingSoundCardPage4 : public DComponens::LvglComponensBase {
public:
    using Control = ui_test_soundcard::Control;

    LvSettingSoundCardPage4() = default;

    LvSettingSoundCardPage4(lv_obj_t *parent,
                           const NodeIter &parent_node,
                           std::function<void()> back_callback)
        : parent_node_(parent_node)
    {
        LeaveSelfPage = std::move(back_callback);
        create_ui(parent);
        if (ComponensObj)
            api_timer_ = lv_timer_create(&LvSettingSoundCardPage4::api_timer_cb, 50, this);
        load_cards();
    }

    void AnimateNextIn(std::function<void()> animate_over_func) override
    {
        if (animate_over_func) animate_over_func();
    }

    void AnimateNextOut(std::function<void()> animate_over_func) override
    {
        if (animate_over_func) animate_over_func();
    }

    void LoadNextPage() override {}

    void LeaveNextPage() override
    {
        if (LeaveSelfPage) LeaveSelfPage();
    }

    ~LvSettingSoundCardPage4() override
    {
        lv_async_call_cancel(&LvSettingSoundCardPage4::close_detail_async, this);
        detail_close_pending_ = false;
        if (api_timer_) {
            lv_timer_delete(api_timer_);
            api_timer_ = nullptr;
        }
        cancel_requests();
        page_lifetime_.reset();
        api_.shutdown();
        if (detail_page_ && detail_page_->Get()) {
            if (lv_obj_get_group(detail_page_->Get()))
                lv_group_remove_obj(detail_page_->Get());
        }
        detail_page_.reset();
        if (ComponensObj) {
            lv_anim_del(ComponensObj, nullptr);
            lv_obj_delete(ComponensObj);
            ComponensObj = nullptr;
        }
    }

    void create_ui(lv_obj_t *parent) override
    {
        if (!parent) return;
        ComponensObj = lv_obj_create(parent);
        if (!ComponensObj) return;
        lv_obj_set_size(ComponensObj,
                        metric(LayoutMetric::ScreenW),
                        metric(LayoutMetric::ScreenH));
        lv_obj_set_pos(ComponensObj, 0, 0);
        lv_obj_set_style_bg_color(ComponensObj, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(ComponensObj, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(ComponensObj, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(ComponensObj, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(ComponensObj, 0, LV_PART_MAIN);
        lv_obj_remove_flag(ComponensObj, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(ComponensObj, LV_OBJ_FLAG_SCROLLABLE);
        DComponens::lvgl_bind_event(
            ComponensObj,
            LV_EVENT_KEY,
            nullptr,
            std::bind(&LvSettingSoundCardPage4::handle_key_event,
                      this,
                      std::placeholders::_1));
    }

private:
    enum class View { Cards, Controls };
    enum class RequestKind { None, Cards, Controls, Detail, Set, RefreshDetail };

    enum class LayoutMetric : int {
        ScreenW     = 320,
        ScreenH     = 150,
        RowY        = 34,
        RowH        = 20,
        RowGap      = 2,
        VisibleRows = 5,
    };

    static constexpr int metric(LayoutMetric value)
    {
        return static_cast<int>(value);
    }

    using ApiHandler = std::function<void(int, std::string)>;

    static void api_timer_cb(lv_timer_t *timer) noexcept
    {
        auto *self = timer
            ? static_cast<LvSettingSoundCardPage4 *>(lv_timer_get_user_data(timer))
            : nullptr;
        if (!self || timer != self->api_timer_ || !self->ComponensObj) return;
        self->api_.drain([](ui_test_soundcard::SoundCardApiAdapter::Result &result) {
            if (result.handler)
                result.handler(result.code, std::move(result.data));
        });
    }

    template <typename Handler>
    void request_api(std::list<std::string> arguments,
                     RequestKind kind,
                     Handler handler)
    {
        cancel_requests();
        const uint64_t request_generation = generation_;
        const std::weak_ptr<bool> lifetime = page_lifetime_;
        pending_generation_ = request_generation;
        pending_kind_ = kind;
        request_pending_ = true;

        ApiHandler wrapped = [this,
                              lifetime,
                              request_generation,
                              kind,
                              handler = ApiHandler(std::move(handler))](
                                 int code, std::string data) mutable {
            if (lifetime.expired() || request_generation != generation_ ||
                pending_generation_ != request_generation || pending_kind_ != kind ||
                !ComponensObj)
                return;
            request_pending_ = false;
            pending_kind_ = RequestKind::None;
            if (handler) handler(code, std::move(data));
        };

        api_.request(std::move(arguments), request_generation, std::move(wrapped));
    }

    void cancel_requests()
    {
        ++generation_;
        if (generation_ == 0) generation_ = 1;
        request_pending_ = false;
        pending_kind_ = RequestKind::None;
        pending_generation_ = generation_;
        api_.discard_pending();
    }

    static std::string compact_error(const std::string &data)
    {
        const size_t first = data.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "Soundcard backend unavailable";
        size_t last = data.find_first_of("\r\n", first);
        if (last == std::string::npos) last = data.size();
        std::string result = data.substr(first, last - first);
        if (result.size() > 46) result.resize(46), result += "...";
        return result;
    }

    void load_cards()
    {
        view_ = View::Cards;
        selected_index_ = 0;
        card_index_ = -1;
        cards_.clear();
        controls_.clear();
        loading_ = true;
        backend_available_ = true;
        error_message_.clear();
        render();
        if (!api_timer_) {
            loading_ = false;
            backend_available_ = false;
            error_message_ = "Soundcard result queue unavailable";
            render();
            return;
        }
        request_api({"ListCards"}, RequestKind::Cards,
                    [this](int code, std::string data) {
                        loading_ = false;
                        if (code != 0) {
                            cards_.clear();
                            controls_.clear();
                            backend_available_ = false;
                            error_message_ = compact_error(data);
                            render();
                            return;
                        }
                        cards_ = ui_test_soundcard::SoundCardModel::parse_cards(data);
                        if (ui_test_soundcard::SoundCardModel::has_payload_content(data) &&
                            cards_.empty()) {
                            backend_available_ = false;
                            error_message_ = "Invalid card response";
                            render();
                            return;
                        }
                        backend_available_ = true;
                        error_message_.clear();
                        if (selected_index_ >= static_cast<int>(cards_.size())) selected_index_ = 0;
                        render();
                    });
    }

    void load_controls()
    {
        if (request_pending_ || cards_.empty() || selected_index_ < 0 ||
            selected_index_ >= static_cast<int>(cards_.size()))
            return;

        const int requested_card = cards_[static_cast<size_t>(selected_index_)].index;
        view_ = View::Controls;
        card_index_ = requested_card;
        selected_index_ = 0;
        controls_.clear();
        loading_ = true;
        backend_available_ = true;
        error_message_.clear();
        render();
        request_api({"ListControls", std::to_string(requested_card)},
                    RequestKind::Controls,
                    [this](int code, std::string data) {
                        loading_ = false;
                        if (code != 0) {
                            controls_.clear();
                            backend_available_ = false;
                            error_message_ = compact_error(data);
                            render();
                            return;
                        }
                        controls_ = ui_test_soundcard::SoundCardModel::parse_controls(data);
                        if (ui_test_soundcard::SoundCardModel::has_payload_content(data) &&
                            controls_.empty()) {
                            backend_available_ = false;
                            error_message_ = "Invalid control response";
                            render();
                            return;
                        }
                        backend_available_ = true;
                        error_message_.clear();
                        if (selected_index_ >= static_cast<int>(controls_.size())) selected_index_ = 0;
                        render();
                    });
    }

    void open_detail()
    {
        if (request_pending_ || controls_.empty() || selected_index_ < 0 ||
            selected_index_ >= static_cast<int>(controls_.size()))
            return;

        cancel_requests();
        const Control control = controls_[static_cast<size_t>(selected_index_)];
        if (!ui_test_soundcard::SoundCardModel::has_control_name(control)) {
            error_message_ = "Invalid control name";
            render();
            return;
        }
        const std::weak_ptr<bool> lifetime = page_lifetime_;
        detail_page_ = std::make_unique<LvSettingSoundCardDetailPage>(
            ComponensObj,
            card_index_,
            control,
            std::bind(&LvSettingSoundCardPage4::close_detail, this),
            [this, lifetime](std::string value) {
                if (lifetime.expired()) return;
                submit_control(std::move(value));
            });
        if (!detail_page_ || !detail_page_->Get()) {
            detail_page_.reset();
            return;
        }

        lv_group_t *group = ComponensObj ? lv_obj_get_group(ComponensObj) : nullptr;
        if (group) {
            if (lv_obj_get_group(ComponensObj) == group)
                lv_group_remove_obj(ComponensObj);
            lv_group_add_obj(group, detail_page_->Get());
            lv_group_focus_obj(detail_page_->Get());
        }

        request_api({"GetControlDetail", std::to_string(card_index_), control.name},
                    RequestKind::Detail,
                    [this, control](int code, std::string data) {
                        if (!detail_page_) return;
                        if (code != 0 || !ui_test_soundcard::SoundCardModel::has_detail_payload(data)) {
                            detail_page_->set_detail_error(compact_error(data));
                            return;
                        }
                        const Control detail = ui_test_soundcard::SoundCardModel::parse_detail(
                            data, control);
                        detail_page_->set_detail(detail);
                    });
    }

    void submit_control(std::string value)
    {
        if (!detail_page_ || request_pending_ || !detail_page_->detail_loaded() ||
            detail_page_->writing())
            return;
        const Control control = detail_page_->control();
        if (!ui_test_soundcard::SoundCardModel::has_control_name(control) ||
            !ui_test_soundcard::SoundCardModel::has_safe_wire_value(value)) {
            detail_page_->complete_write_failure("Invalid control value");
            return;
        }

        detail_page_->begin_write(value);
        request_api({"SetControl", std::to_string(card_index_), control.name, value},
                    RequestKind::Set,
                    [this, control](int code, std::string data) {
                        if (!detail_page_) return;
                        if (code != 0) {
                            detail_page_->complete_write_failure(compact_error(data));
                            return;
                        }
                        if (data != "0") {
                            detail_page_->complete_write_failure("Invalid write response");
                            return;
                        }
                        detail_page_->mark_refresh_pending();
                        request_api({"GetControlDetail",
                                     std::to_string(card_index_),
                                     control.name},
                                    RequestKind::RefreshDetail,
                                    [this, control](int refresh_code, std::string refresh_data) {
                                        if (!detail_page_) return;
                                        if (refresh_code != 0 ||
                                            !ui_test_soundcard::SoundCardModel::has_detail_payload(
                                                refresh_data)) {
                                            detail_page_->complete_write_failure(
                                                "Applied, refresh failed");
                                            return;
                                        }
                                        Control actual = ui_test_soundcard::SoundCardModel::parse_detail(
                                            refresh_data, control);
                                        if (actual.name.empty()) actual.name = control.name;
                                        if (actual.type.empty()) actual.type = control.type;
                                        if (actual.current_text.empty())
                                            actual.current_text = control.current_text;
                                        if (selected_index_ >= 0 &&
                                            selected_index_ < static_cast<int>(controls_.size()) &&
                                            controls_[static_cast<size_t>(selected_index_)].name ==
                                                actual.name)
                                            controls_[static_cast<size_t>(selected_index_)] = actual;
                                        detail_page_->complete_write_success(std::move(actual));
                                    });
                    });
    }

    void close_detail_async()
    {
        close_detail_now();
    }

    static void close_detail_async(void *user_data)
    {
        auto *self = static_cast<LvSettingSoundCardPage4 *>(user_data);
        if (self) self->close_detail_async();
    }

    void close_detail()
    {
        if (!detail_page_ || detail_close_pending_) return;
        detail_close_pending_ = true;
        if (lv_async_call(&LvSettingSoundCardPage4::close_detail_async, this) != LV_RESULT_OK) {
            detail_close_pending_ = false;
            close_detail_now();
        }
    }

    void close_detail_now()
    {
        detail_close_pending_ = false;
        cancel_requests();
        lv_group_t *group = detail_page_ && detail_page_->Get()
                                ? lv_obj_get_group(detail_page_->Get())
                                : nullptr;
        if (detail_page_ && detail_page_->Get() && group)
            lv_group_remove_obj(detail_page_->Get());
        detail_page_.reset();
        if (group && ComponensObj) {
            lv_group_add_obj(group, ComponensObj);
            lv_group_focus_obj(ComponensObj);
        }
        render();
    }

    lv_obj_t *label(const char *text,
                    int x,
                    int y,
                    int width,
                    int height,
                    uint32_t color,
                    int font_size,
                    bool scroll = false)
    {
        lv_obj_t *result = lv_label_create(ComponensObj);
        if (!result) return nullptr;
        lv_label_set_text(result, text ? text : "");
        lv_obj_set_size(result, width, height);
        lv_obj_set_pos(result, x, y);
        lv_obj_set_style_text_color(result, lv_color_hex(color), LV_PART_MAIN);
        lv_obj_set_style_text_font(
            result,
            cp0_fonts().get("Montserrat-Bold.ttf", font_size, LV_FREETYPE_FONT_STYLE_BOLD),
            LV_PART_MAIN);
        lv_obj_set_style_text_align(result, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_label_set_long_mode(result, scroll ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_CLIP);
        return result;
    }

    void render()
    {
        if (!ComponensObj || detail_page_) return;
        lv_obj_clean(ComponensObj);

        const bool controls_view = view_ == View::Controls;
        label(controls_view ? "Mixer Controls" : "Sound Cards",
              8,
              5,
              230,
              20,
              0x58A6FF,
              14);

        if (loading_) {
            label("Loading...", 8, 48, 304, 20, 0xAAAAAA, 13);
            label("ESC: back", 8, 126, 304, 14, 0x555555, 10);
            return;
        }

        if (!backend_available_) {
            label("Mixer unavailable", 8, 42, 304, 20, 0xFFAA00, 13);
            label(error_message_.c_str(), 8, 68, 304, 24, 0x888888, 11);
            label("ESC: back", 8, 126, 304, 14, 0x555555, 10);
            return;
        }

        const int count = controls_view ? static_cast<int>(controls_.size())
                                        : static_cast<int>(cards_.size());
        if (count == 0) {
            label(controls_view ? "No mixer controls" : "No ALSA cards found",
                  8,
                  54,
                  304,
                  20,
                  0x888888,
                  12);
            if (!error_message_.empty()) label(error_message_.c_str(), 8, 78, 304, 20, 0x888888, 10);
            label("ESC: back", 8, 126, 304, 14, 0x555555, 10);
            return;
        }

        const int offset = std::clamp(selected_index_ - metric(LayoutMetric::VisibleRows) / 2,
                                      0,
                                      std::max(0, count - metric(LayoutMetric::VisibleRows)));
        for (int visible = 0;
             visible < metric(LayoutMetric::VisibleRows) && offset + visible < count;
             ++visible) {
            const int index = offset + visible;
            const bool focused = index == selected_index_;
            const int y = metric(LayoutMetric::RowY) +
                          visible * (metric(LayoutMetric::RowH) + metric(LayoutMetric::RowGap));
            lv_obj_t *row = lv_obj_create(ComponensObj);
            if (!row) continue;
            lv_obj_set_size(row, 304, metric(LayoutMetric::RowH));
            lv_obj_set_pos(row, 8, y);
            lv_obj_set_style_bg_color(row,
                                      lv_color_hex(focused ? 0x2A2A2A : 0x000000),
                                      LV_PART_MAIN);
            lv_obj_set_style_bg_opa(row,
                                    focused ? LV_OPA_COVER : LV_OPA_TRANSP,
                                    LV_PART_MAIN);
            lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
            lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
            lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

            std::string name;
            std::string value;
            if (controls_view) {
                name = controls_[static_cast<size_t>(index)].name;
                value = controls_[static_cast<size_t>(index)].current_text;
            } else {
                name = cards_[static_cast<size_t>(index)].label;
                value = "card " + std::to_string(cards_[static_cast<size_t>(index)].index);
            }
            lv_obj_t *name_label = lv_label_create(row);
            if (name_label) {
                lv_label_set_text(name_label, name.c_str());
                lv_obj_set_size(name_label,
                                controls_view ? 175 : 230,
                                metric(LayoutMetric::RowH));
                lv_obj_set_pos(name_label, 4, 0);
                lv_obj_set_style_text_color(name_label,
                                            lv_color_hex(focused ? 0xFFFFFF : 0xCCCCCC),
                                            LV_PART_MAIN);
                lv_obj_set_style_text_font(
                    name_label,
                    cp0_fonts().get("Montserrat-Bold.ttf", 12, LV_FREETYPE_FONT_STYLE_BOLD),
                    LV_PART_MAIN);
                lv_label_set_long_mode(name_label,
                                       focused ? LV_LABEL_LONG_SCROLL_CIRCULAR
                                               : LV_LABEL_LONG_CLIP);
            }
            if (controls_view && !value.empty()) {
                lv_obj_t *value_label = lv_label_create(row);
                if (value_label) {
                    lv_label_set_text(value_label, value.c_str());
                    lv_obj_set_size(value_label, 112, metric(LayoutMetric::RowH));
                    lv_obj_set_pos(value_label, 184, 0);
                    lv_obj_set_style_text_color(value_label,
                                                lv_color_hex(focused ? 0xAADDFF : 0x777777),
                                                LV_PART_MAIN);
                    lv_obj_set_style_text_font(
                        value_label,
                        cp0_fonts().get("Montserrat-Bold.ttf", 10, LV_FREETYPE_FONT_STYLE_BOLD),
                        LV_PART_MAIN);
                    lv_label_set_long_mode(value_label, LV_LABEL_LONG_CLIP);
                }
            }
        }

        if (selected_index_ > 0) add_arrow(0, "up");
        if (selected_index_ + 1 < count)
            add_arrow(metric(LayoutMetric::ScreenH) - 19, "down");
        label(controls_view ? "OK: edit  ESC: back" : "OK: open  ESC: back",
              8,
              126,
              304,
              14,
              0x555555,
              10);
    }

    void add_arrow(int y, const char *direction)
    {
        lv_obj_t *arrow = lv_img_create(ComponensObj);
        if (!arrow) return;
        lv_img_set_src(arrow, std::string(direction) == "up" ? &setting_red_up : &setting_red_down);
        lv_obj_update_layout(arrow);
        lv_obj_set_pos(arrow,
                       metric(LayoutMetric::ScreenW) / 2 - lv_obj_get_width(arrow) / 2,
                       y);
    }

    void handle_key(uint32_t key)
    {
        if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
            if (view_ == View::Controls) {
                load_cards();
            } else if (LeaveSelfPage) {
                LeaveSelfPage();
            }
            return;
        }

        if (request_pending_ || loading_) return;
        const int count = view_ == View::Controls ? static_cast<int>(controls_.size())
                                                  : static_cast<int>(cards_.size());
        if (count == 0) return;
        if (key == LV_KEY_UP) {
            selected_index_ = selected_index_ > 0 ? selected_index_ - 1 : count - 1;
            render();
        } else if (key == LV_KEY_DOWN) {
            selected_index_ = selected_index_ + 1 < count ? selected_index_ + 1 : 0;
            render();
        } else if (key == LV_KEY_ENTER || key == LV_KEY_RIGHT) {
            if (view_ == View::Cards)
                load_controls();
            else
                open_detail();
        }
    }

    void handle_key_event(lv_event_t *event)
    {
        if (!event || lv_event_get_code(event) != LV_EVENT_KEY) return;
        handle_key(lv_event_get_key(event));
        lv_event_stop_processing(event);
    }

    NodeIter parent_node_;
    View view_ = View::Cards;
    std::vector<ui_test_soundcard::Card> cards_;
    std::vector<ui_test_soundcard::Control> controls_;
    int selected_index_ = 0;
    int card_index_ = -1;
    bool backend_available_ = false;
    bool loading_ = false;
    bool request_pending_ = false;
    bool detail_close_pending_ = false;
    RequestKind pending_kind_ = RequestKind::None;
    uint64_t generation_ = 0;
    uint64_t pending_generation_ = 0;
    std::string error_message_;
    lv_timer_t *api_timer_ = nullptr;
    std::shared_ptr<bool> page_lifetime_ = std::make_shared<bool>(true);
    ui_test_soundcard::SoundCardApiAdapter api_;
    std::unique_ptr<LvSettingSoundCardDetailPage> detail_page_;
};

inline std::unique_ptr<DComponens::LvglComponensBase> soundcard_page4_factory(
    lv_obj_t *parent, const NodeIter &page_node, std::function<void()> on_back)
{
    return std::make_unique<LvSettingSoundCardPage4>(
        parent, page_node, std::move(on_back));
}
