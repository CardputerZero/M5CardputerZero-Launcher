#include "settings_sound_card_page.hpp"
#include "settings_fonts.hpp"

#include <algorithm>
#include <utility>

LvSettingSoundCardPage4::LvSettingSoundCardPage4() = default;

LvSettingSoundCardPage4::LvSettingSoundCardPage4(
    lv_obj_t *parent,
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

void LvSettingSoundCardPage4::AnimateNextIn(std::function<void()> animate_over_func)
{
    if (animate_over_func) animate_over_func();
}

void LvSettingSoundCardPage4::AnimateNextOut(std::function<void()> animate_over_func)
{
    if (animate_over_func) animate_over_func();
}

void LvSettingSoundCardPage4::LoadNextPage() {}

void LvSettingSoundCardPage4::LeaveNextPage()
{
    if (LeaveSelfPage) LeaveSelfPage();
}

LvSettingSoundCardPage4::~LvSettingSoundCardPage4()
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

void LvSettingSoundCardPage4::create_ui(lv_obj_t *parent)
{
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
    DComponens::lvgl_bind_event(
        ComponensObj,
        LV_EVENT_KEY,
        nullptr,
        std::bind(&LvSettingSoundCardPage4::handle_key_event, this, std::placeholders::_1));
}

void LvSettingSoundCardPage4::api_timer_cb(lv_timer_t *timer) noexcept
{
    auto *self = timer
        ? static_cast<LvSettingSoundCardPage4 *>(lv_timer_get_user_data(timer))
        : nullptr;
    if (!self || timer != self->api_timer_ || !self->ComponensObj) return;
    self->api_.drain([](ui_test_soundcard::SoundCardApiAdapter::Result &result) {
        if (result.handler) result.handler(result.code, std::move(result.data));
    });
}

template <typename Handler>
void LvSettingSoundCardPage4::request_api(std::list<std::string> arguments,
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
                          handler = ApiHandler(std::move(handler))](int code,
                                                                     std::string data) mutable {
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

void LvSettingSoundCardPage4::cancel_requests()
{
    ++generation_;
    if (generation_ == 0) generation_ = 1;
    request_pending_ = false;
    pending_kind_ = RequestKind::None;
    pending_generation_ = generation_;
    api_.discard_pending();
}

std::string LvSettingSoundCardPage4::compact_error(const std::string &data)
{
    const size_t first = data.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "Soundcard backend unavailable";
    size_t last = data.find_first_of("\r\n", first);
    if (last == std::string::npos) last = data.size();
    std::string result = data.substr(first, last - first);
    if (result.size() > 46) result.resize(46), result += "...";
    return result;
}

void LvSettingSoundCardPage4::load_cards()
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
    request_api({"ListCards"}, RequestKind::Cards, [this](int code, std::string data) {
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
        if (ui_test_soundcard::SoundCardModel::has_payload_content(data) && cards_.empty()) {
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

void LvSettingSoundCardPage4::load_controls()
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
    request_api({"ListControls", std::to_string(requested_card)}, RequestKind::Controls,
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

void LvSettingSoundCardPage4::open_detail()
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
        if (lv_obj_get_group(ComponensObj) == group) lv_group_remove_obj(ComponensObj);
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
                    const Control detail = ui_test_soundcard::SoundCardModel::parse_detail(data, control);
                    detail_page_->set_detail(detail);
                });
}

void LvSettingSoundCardPage4::submit_control(std::string value)
{
    if (!detail_page_ || request_pending_ || !detail_page_->detail_loaded() || detail_page_->writing())
        return;
    const Control control = detail_page_->control();
    if (!ui_test_soundcard::SoundCardModel::has_control_name(control) ||
        !ui_test_soundcard::SoundCardModel::has_safe_wire_value(value)) {
        detail_page_->complete_write_failure("Invalid control value");
        return;
    }

    detail_page_->begin_write(value);
    request_api({"SetControl", std::to_string(card_index_), control.name, value}, RequestKind::Set,
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
                    request_api({"GetControlDetail", std::to_string(card_index_), control.name},
                                RequestKind::RefreshDetail,
                                [this, control](int refresh_code, std::string refresh_data) {
                                    if (!detail_page_) return;
                                    if (refresh_code != 0 ||
                                        !ui_test_soundcard::SoundCardModel::has_detail_payload(refresh_data)) {
                                        detail_page_->complete_write_failure("Applied, refresh failed");
                                        return;
                                    }
                                    Control actual = ui_test_soundcard::SoundCardModel::parse_detail(
                                        refresh_data, control);
                                    if (actual.name.empty()) actual.name = control.name;
                                    if (actual.type.empty()) actual.type = control.type;
                                    if (actual.current_text.empty()) actual.current_text = control.current_text;
                                    if (selected_index_ >= 0 &&
                                        selected_index_ < static_cast<int>(controls_.size()) &&
                                        controls_[static_cast<size_t>(selected_index_)].name == actual.name)
                                        controls_[static_cast<size_t>(selected_index_)] = actual;
                                    detail_page_->complete_write_success(std::move(actual));
                                });
                });
}

void LvSettingSoundCardPage4::close_detail_async()
{
    close_detail_now();
}

void LvSettingSoundCardPage4::close_detail_async(void *user_data)
{
    auto *self = static_cast<LvSettingSoundCardPage4 *>(user_data);
    if (self) self->close_detail_async();
}

void LvSettingSoundCardPage4::close_detail()
{
    if (!detail_page_ || detail_close_pending_) return;
    detail_close_pending_ = true;
    if (lv_async_call(&LvSettingSoundCardPage4::close_detail_async, this) != LV_RESULT_OK) {
        detail_close_pending_ = false;
        close_detail_now();
    }
}

void LvSettingSoundCardPage4::close_detail_now()
{
    detail_close_pending_ = false;
    cancel_requests();
    lv_group_t *group = detail_page_ && detail_page_->Get() ? lv_obj_get_group(detail_page_->Get()) : nullptr;
    if (detail_page_ && detail_page_->Get() && group) lv_group_remove_obj(detail_page_->Get());
    detail_page_.reset();
    if (group && ComponensObj) {
        lv_group_add_obj(group, ComponensObj);
        lv_group_focus_obj(ComponensObj);
    }
    render();
}

lv_obj_t *LvSettingSoundCardPage4::label(const char *text,
                                         int x,
                                         int y,
                                         int width,
                                         int height,
                                         uint32_t color,
                                         int font_size,
                                         bool scroll)
{
    lv_obj_t *result = lv_label_create(ComponensObj);
    if (!result) return nullptr;
    lv_label_set_text(result, text ? text : "");
    lv_obj_set_size(result, width, height);
    lv_obj_set_pos(result, x, y);
    lv_obj_set_style_text_color(result, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_text_font(
        result,
        settings_fonts::cjk_sans(font_size),
        LV_PART_MAIN);
    lv_obj_set_style_text_align(result, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_label_set_long_mode(result, scroll ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_CLIP);
    return result;
}

void LvSettingSoundCardPage4::render()
{
    if (!ComponensObj || detail_page_) return;
    lv_obj_clean(ComponensObj);

    const bool controls_view = view_ == View::Controls;
    label(controls_view ? "Mixer Controls" : "Sound Cards",
          metric(LayoutMetric::ContentX),
          metric(LayoutMetric::TitleY),
          metric(LayoutMetric::TitleW),
          metric(LayoutMetric::TitleH),
          0x58A6FF,
          14);

    if (loading_) {
        label("Loading...", metric(LayoutMetric::ContentX), metric(LayoutMetric::LoadingY),
              metric(LayoutMetric::ContentW), metric(LayoutMetric::TitleH), 0xAAAAAA, 13);
        label("ESC: back", metric(LayoutMetric::ContentX), metric(LayoutMetric::FooterY),
              metric(LayoutMetric::ContentW), metric(LayoutMetric::FooterH), 0x555555, 10);
        return;
    }

    if (!backend_available_) {
        label("Mixer unavailable", metric(LayoutMetric::ContentX), metric(LayoutMetric::UnavailableY),
              metric(LayoutMetric::ContentW), metric(LayoutMetric::TitleH), 0xFFAA00, 13);
        label(error_message_.c_str(), metric(LayoutMetric::ContentX), metric(LayoutMetric::ErrorY),
              metric(LayoutMetric::ContentW), metric(LayoutMetric::ErrorH), 0x888888, 11);
        label("ESC: back", metric(LayoutMetric::ContentX), metric(LayoutMetric::FooterY),
              metric(LayoutMetric::ContentW), metric(LayoutMetric::FooterH), 0x555555, 10);
        return;
    }

    const int count = controls_view ? static_cast<int>(controls_.size()) : static_cast<int>(cards_.size());
    if (count == 0) {
        label(controls_view ? "No mixer controls" : "No ALSA cards found",
              metric(LayoutMetric::ContentX), metric(LayoutMetric::EmptyY),
              metric(LayoutMetric::ContentW), metric(LayoutMetric::TitleH), 0x888888, 12);
        if (!error_message_.empty())
            label(error_message_.c_str(), metric(LayoutMetric::ContentX), metric(LayoutMetric::DetailErrorY),
                  metric(LayoutMetric::ContentW), metric(LayoutMetric::TitleH), 0x888888, 10);
        label("ESC: back", metric(LayoutMetric::ContentX), metric(LayoutMetric::FooterY),
              metric(LayoutMetric::ContentW), metric(LayoutMetric::FooterH), 0x555555, 10);
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
        lv_obj_set_size(row, metric(LayoutMetric::ContentW), metric(LayoutMetric::RowH));
        lv_obj_set_pos(row, metric(LayoutMetric::ContentX), y);
        lv_obj_set_style_bg_color(row, lv_color_hex(focused ? 0x2A2A2A : 0x000000), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(row, focused ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
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
                            controls_view ? metric(LayoutMetric::ControlNameW)
                                          : metric(LayoutMetric::CardNameW),
                            metric(LayoutMetric::RowH));
            lv_obj_set_pos(name_label, metric(LayoutMetric::RowTextInset), 0);
            lv_obj_set_style_text_color(name_label, lv_color_hex(focused ? 0xFFFFFF : 0xCCCCCC), LV_PART_MAIN);
            lv_obj_set_style_text_font(
                name_label,
                settings_fonts::cjk_sans(12),
                LV_PART_MAIN);
            lv_label_set_long_mode(name_label,
                                   focused ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_CLIP);
        }
        if (controls_view && !value.empty()) {
            lv_obj_t *value_label = lv_label_create(row);
            if (value_label) {
                lv_label_set_text(value_label, value.c_str());
                lv_obj_set_size(value_label, metric(LayoutMetric::ValueW), metric(LayoutMetric::RowH));
                lv_obj_set_pos(value_label, metric(LayoutMetric::ValueX), 0);
                lv_obj_set_style_text_color(value_label,
                                            lv_color_hex(focused ? 0xAADDFF : 0x777777),
                                            LV_PART_MAIN);
                lv_obj_set_style_text_font(
                    value_label,
                    settings_fonts::sans(10),
                    LV_PART_MAIN);
                lv_label_set_long_mode(value_label, LV_LABEL_LONG_CLIP);
            }
        }
    }

    if (selected_index_ > 0) add_arrow(0, "up");
    if (selected_index_ + 1 < count) add_arrow(metric(LayoutMetric::ScreenH) - 19, "down");
    label(controls_view ? "OK: edit  ESC: back" : "OK: open  ESC: back",
          metric(LayoutMetric::ContentX),
          metric(LayoutMetric::FooterY),
          metric(LayoutMetric::ContentW),
          metric(LayoutMetric::FooterH),
          0x555555,
          10);
}

void LvSettingSoundCardPage4::add_arrow(int y, const char *direction)
{
    lv_obj_t *arrow = lv_img_create(ComponensObj);
    if (!arrow) return;
    lv_img_set_src(arrow, std::string(direction) == "up" ? &setting_red_up : &setting_red_down);
    lv_obj_update_layout(arrow);
    lv_obj_set_pos(arrow,
                   metric(LayoutMetric::ScreenW) / 2 - lv_obj_get_width(arrow) / 2,
                   y);
}

void LvSettingSoundCardPage4::handle_key(uint32_t key)
{
    if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
        if (view_ == View::Controls)
            load_cards();
        else if (LeaveSelfPage)
            LeaveSelfPage();
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

void LvSettingSoundCardPage4::handle_key_event(lv_event_t *event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_KEY) return;
    handle_key(lv_event_get_key(event));
    lv_event_stop_processing(event);
}

std::unique_ptr<DComponens::LvglComponensBase> soundcard_page4_factory(
    lv_obj_t *parent,
    const NodeIter &page_node,
    std::function<void()> on_back)
{
    return std::make_unique<LvSettingSoundCardPage4>(parent, page_node, std::move(on_back));
}
